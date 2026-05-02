#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <regex>
#include <thread>
#include <vector>
#include <SDL.h>
#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>
#include <box2d/box2d.h>
#include "EngineLuaHelper.h"
#include "Input.h"
#include "Helper.h"
#include "AudioDB.h"
#include "ImGuiLayer.h"
#include "ImageDB.h"
#include "ResourcePaths.h"
#include "Renderer.h"
#include "TextDB.h"

namespace fs = std::filesystem;

/* Helper Functions */
static bool containsCmd(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

static bool extractProceedScene(const std::string& text, std::string& outScene) {
    static const std::regex pattern(R"(\bproceed to\s+([A-Za-z0-9_\-]+))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    if (match.size() < 2) return false;
    outScene = match[1].str();
    return !outScene.empty();
}

static std::string FormatLuaExceptionMessage(const std::string& rawMessage) {
    std::string formattedMessage = rawMessage;
    std::replace(formattedMessage.begin(), formattedMessage.end(), '\\', '/');

    const std::string resourcePrefix = "resources/";
    const std::size_t resourcePosition = formattedMessage.find(resourcePrefix);
    if (resourcePosition != std::string::npos) {
        formattedMessage = formattedMessage.substr(resourcePosition);
    }

    return formattedMessage;
}

static std::string QuoteForShell(const std::string& rawText) {
    std::string quotedText = "\"";
    for (char ch : rawText) {
        if (ch == '"' || ch == '\\') {
            quotedText.push_back('\\');
        }
        quotedText.push_back(ch);
    }
    quotedText.push_back('"');
    return quotedText;
}

static std::string NormalizeSceneName(const std::string& sceneName) {
    const fs::path scenePath(sceneName);
    const std::string stem = scenePath.stem().string();
    return stem.empty() ? sceneName : stem;
}

static float GetSortKey(const Actor& actor) {
    return actor.render_order.has_value()
        ? static_cast<float>(*actor.render_order)
        : actor.position.y;
}

static void ApplyMovementAnimationState(Actor& actor, const glm::vec2& intent, bool flipOnMovement) {
    actor.move_intent = intent;

    if (flipOnMovement) {
        if (intent.x > 0.0f) {
            actor.transform_scale.x = glm::abs(actor.transform_scale.x);
        }
        else if (intent.x < 0.0f) {
            actor.transform_scale.x = -glm::abs(actor.transform_scale.x);
        }
    }

    if (!actor.view_image_back.empty()) {
        if (intent.y < 0.0f) {
            actor.use_back_view = true;
        }
        else if (intent.y > 0.0f) {
            actor.use_back_view = false;
        }
    }
}

struct Bounds {
    float left;
    float top;
    float right;
    float bottom;
};

static std::optional<Bounds> GetActorBounds(const Actor& actor, const glm::vec2& position, bool trigger) {
    const std::optional<float>& width = trigger ? actor.box_trigger_width : actor.box_collider_width;
    const std::optional<float>& height = trigger ? actor.box_trigger_height : actor.box_collider_height;
    if (!width.has_value() || !height.has_value()) {
        return std::nullopt;
    }

    const float halfWidth = *width * glm::abs(actor.transform_scale.x) * 0.5f;
    const float halfHeight = *height * glm::abs(actor.transform_scale.y) * 0.5f;

    return Bounds{
        position.x - halfWidth,
        position.y - halfHeight,
        position.x + halfWidth,
        position.y + halfHeight
    };
}

static bool BoundsOverlap(const Bounds& lhs, const Bounds& rhs) {
    return lhs.left < rhs.right &&
        lhs.right > rhs.left &&
        lhs.top < rhs.bottom &&
        lhs.bottom > rhs.top;
}

static void RecordCollision(Actor& lhs, int lhsIndex, Actor& rhs, int rhsIndex) {
    lhs.colliding_actor_indices_this_frame.insert(rhsIndex);
    rhs.colliding_actor_indices_this_frame.insert(lhsIndex);
}

static int ResolvePlayerIndex(const std::deque<Actor>& actors, const Actor* playerActor, int fallbackIndex) {
    if (fallbackIndex >= 0 && fallbackIndex < static_cast<int>(actors.size())) {
        if (!actors[fallbackIndex].destroyed && actors[fallbackIndex].actor_name == "player") {
            return fallbackIndex;
        }
        if (!actors[fallbackIndex].destroyed &&
            playerActor != nullptr &&
            actors[fallbackIndex].id == playerActor->id) {
            return fallbackIndex;
        }
    }

    if (playerActor != nullptr) {
        for (int i = 0; i < static_cast<int>(actors.size()); ++i) {
            if (!actors[i].destroyed && actors[i].id == playerActor->id) {
                return i;
            }
        }
    }

    if (fallbackIndex >= 0 && fallbackIndex < static_cast<int>(actors.size())) {
        return fallbackIndex;
    }

    return -1;
}

static int GameplaySfxChannel(int currentFrame) {
    return currentFrame % 48 + 2;
}

static const Actor::RenderImageCache& SelectActorImageCache(
    const Actor& actor,
    const Actor* playerActor,
    int currentFrame) {
    if (playerActor != nullptr &&
        actor.id == playerActor->id &&
        currentFrame < actor.damage_visual_until_frame &&
        actor.view_image_damage_cache.texture != nullptr) {
        return actor.view_image_damage_cache;
    }

    if ((playerActor == nullptr || actor.id != playerActor->id) &&
        currentFrame < actor.attack_visual_until_frame &&
        actor.view_image_attack_cache.texture != nullptr) {
        return actor.view_image_attack_cache;
    }

    if (actor.use_back_view && actor.view_image_back_cache.texture != nullptr) {
        return actor.view_image_back_cache;
    }

    return actor.view_image_cache;
}

static void PopulateRenderImageCache(Actor::RenderImageCache& cache, const std::string& imageName) {
    cache = Actor::RenderImageCache{};
    if (imageName.empty()) {
        return;
    }

    const ImageDB::ImageInfo& info = ImageDB::GetImageInfo(imageName);
    cache.texture = info.texture;
    cache.width = info.width;
    cache.height = info.height;
}

static void PopulateActorRenderCaches(
    Actor& actor,
    float& max_actor_half_width_units,
    float& max_actor_half_height_units) {
    PopulateRenderImageCache(actor.view_image_cache, actor.view_image);
    PopulateRenderImageCache(actor.view_image_back_cache, actor.view_image_back);
    PopulateRenderImageCache(actor.view_image_damage_cache, actor.view_image_damage);
    PopulateRenderImageCache(actor.view_image_attack_cache, actor.view_image_attack);

    const float maxWidth =
        std::max(std::max(actor.view_image_cache.width, actor.view_image_back_cache.width),
            std::max(actor.view_image_damage_cache.width, actor.view_image_attack_cache.width));
    const float maxHeight =
        std::max(std::max(actor.view_image_cache.height, actor.view_image_back_cache.height),
            std::max(actor.view_image_damage_cache.height, actor.view_image_attack_cache.height));

    max_actor_half_width_units = std::max(
        max_actor_half_width_units,
        maxWidth * glm::abs(actor.transform_scale.x) * 0.5f / 100.0f);
    max_actor_half_height_units = std::max(
        max_actor_half_height_units,
        maxHeight * glm::abs(actor.transform_scale.y) * 0.5f / 100.0f);
}

static Engine* g_active_engine = nullptr;
static bool IsComponentEnabled(Actor::ComponentInstance& component);
static bool g_physics_callbacks_enabled = true;

struct Collision {
    Actor* other = nullptr;
    b2Vec2 point = b2Vec2(0.0f, 0.0f);
    b2Vec2 relative_velocity = b2Vec2(0.0f, 0.0f);
    b2Vec2 normal = b2Vec2(0.0f, 0.0f);
};

struct HitResult {
    Actor* actor = nullptr;
    b2Vec2 point = b2Vec2(0.0f, 0.0f);
    b2Vec2 normal = b2Vec2(0.0f, 0.0f);
    bool is_trigger = false;
};

static bool IsRigidbodyComponentType(const std::string& componentType) {
    return componentType == "Rigidbody";
}

static bool IsParticleSystemComponentType(const std::string& componentType) {
    return componentType == "ParticleSystem";
}

class Rigidbody {
public:
    struct FixtureBinding {
        Rigidbody* rigidbody = nullptr;
        Actor* actor = nullptr;
        bool is_trigger = false;
        bool is_phantom = false;
    };

    std::string key = "";
    Actor* actor = nullptr;
    bool enabled = true;
    float x = 0.0f;
    float y = 0.0f;
    std::string body_type = "dynamic";
    bool precise = true;
    float gravity_scale = 1.0f;
    float density = 1.0f;
    float angular_friction = 0.3f;
    float rotation = 0.0f;
    bool has_collider = true;
    bool has_trigger = true;
    std::string collider_type = "box";
    std::string trigger_type = "box";
    float width = 1.0f;
    float height = 1.0f;
    float radius = 0.5f;
    float trigger_width = 1.0f;
    float trigger_height = 1.0f;
    float trigger_radius = 0.5f;
    float friction = 0.3f;
    float bounciness = 0.3f;

    Rigidbody() = default;
    ~Rigidbody() {
        DestroyBody();
    }

    void OnStart() {
        if (body == nullptr && g_active_engine != nullptr) {
            b2World* world = g_active_engine->EnsurePhysicsWorld();
            if (world != nullptr) {
                b2BodyDef bodyDef;
                bodyDef.type = ResolveBodyType(body_type);
                bodyDef.position.Set(x, y);
                bodyDef.angle = GetRotationRadians();
                bodyDef.bullet = precise;
                bodyDef.gravityScale = gravity_scale;
                bodyDef.angularDamping = angular_friction;

                body = world->CreateBody(&bodyDef);
                CreateSimulationFixture();

                body->SetLinearVelocity(linear_velocity);
                body->SetAngularVelocity(angular_velocity_degrees * (b2_pi / 180.0f));
                if (pending_force != b2Vec2_zero) {
                    body->ApplyForceToCenter(pending_force, true);
                    pending_force.SetZero();
                }
            }
        }

        SyncActorTransform();
    }

    void OnDestroy() {
        DestroyBody();
    }

    b2Vec2 GetPosition() const {
        if (body != nullptr) {
            return body->GetPosition();
        }

        return b2Vec2(x, y);
    }

    float GetRotation() const {
        if (body != nullptr) {
            return body->GetAngle() * (180.0f / b2_pi);
        }

        return rotation;
    }

    void AddForce(const b2Vec2& force) {
        if (body != nullptr) {
            body->ApplyForceToCenter(force, true);
            return;
        }

        pending_force += force;
    }

    void SetVelocity(const b2Vec2& velocity) {
        linear_velocity = velocity;
        if (body != nullptr) {
            body->SetLinearVelocity(velocity);
        }
    }

    void SetPosition(const b2Vec2& position) {
        x = position.x;
        y = position.y;
        if (body != nullptr) {
            body->SetTransform(position, body->GetAngle());
        }
        SyncActorTransform();
    }

    void SetRotation(float degrees_clockwise) {
        rotation = degrees_clockwise;
        if (body != nullptr) {
            body->SetTransform(body->GetPosition(), degrees_clockwise * (b2_pi / 180.0f));
        }
        SyncActorTransform();
    }

    void SetAngularVelocity(float degrees_clockwise) {
        angular_velocity_degrees = degrees_clockwise;
        if (body != nullptr) {
            body->SetAngularVelocity(degrees_clockwise * (b2_pi / 180.0f));
        }
    }

    void SetGravityScale(float scale) {
        gravity_scale = scale;
        if (body != nullptr) {
            body->SetGravityScale(scale);
        }
    }

    void SetUpDirection(b2Vec2 direction) {
        direction.Normalize();
        SetRotation(glm::atan(direction.x, -direction.y) * (180.0f / b2_pi));
    }

    void SetRightDirection(b2Vec2 direction) {
        direction.Normalize();
        SetRotation((glm::atan(direction.x, -direction.y) - b2_pi / 2.0f) * (180.0f / b2_pi));
    }

    b2Vec2 GetVelocity() const {
        if (body != nullptr) {
            return body->GetLinearVelocity();
        }

        return linear_velocity;
    }

    float GetAngularVelocity() const {
        if (body != nullptr) {
            return body->GetAngularVelocity() * (180.0f / b2_pi);
        }

        return angular_velocity_degrees;
    }

    float GetGravityScale() const {
        if (body != nullptr) {
            return body->GetGravityScale();
        }

        return gravity_scale;
    }

    b2Vec2 GetUpDirection() const {
        const float angle = GetRotationRadians();
        return b2Vec2(glm::sin(angle), -glm::cos(angle));
    }

    b2Vec2 GetRightDirection() const {
        const float angle = GetRotationRadians();
        return b2Vec2(glm::cos(angle), glm::sin(angle));
    }

    void SyncActorTransform() const {
        if (actor == nullptr) {
            return;
        }

        const b2Vec2 position = GetPosition();
        actor->position = glm::vec2(position.x, position.y);
        actor->transform_rotation_degrees = GetRotation();
    }

    void DestroyBody() {
        if (body == nullptr) {
            return;
        }

        b2World* world = body->GetWorld();
        if (world != nullptr) {
            const bool previous_callbacks_enabled = g_physics_callbacks_enabled;
            g_physics_callbacks_enabled = false;
            world->DestroyBody(body);
            g_physics_callbacks_enabled = previous_callbacks_enabled;
        }
        body = nullptr;
        fixture_bindings.clear();
    }

private:
    void CreateSimulationFixture() {
        if (body == nullptr) {
            return;
        }

        if (has_collider) {
            CreateFixture(false, false, false);
        }

        if (has_trigger) {
            CreateFixture(true, true, false);
        }

        if (!has_collider && !has_trigger) {
            CreateFixture(true, false, true);
        }
    }

    void CreateFixture(bool isSensor, bool isTrigger, bool isPhantom) {
        b2FixtureDef fixtureDef;
        fixtureDef.density = density;
        fixtureDef.friction = friction;
        fixtureDef.restitution = bounciness;
        fixtureDef.isSensor = isSensor;

        const std::string& shape_type = isTrigger ? trigger_type : collider_type;
        const float box_width = isTrigger ? trigger_width : width;
        const float box_height = isTrigger ? trigger_height : height;
        const float circle_radius = isTrigger ? trigger_radius : radius;

        if (shape_type == "circle") {
            b2CircleShape circleShape;
            circleShape.m_radius = circle_radius;
            fixtureDef.shape = &circleShape;
            RegisterFixture(body->CreateFixture(&fixtureDef), isTrigger, isPhantom);
            return;
        }

        b2PolygonShape boxShape;
        boxShape.SetAsBox(box_width * 0.5f, box_height * 0.5f);
        fixtureDef.shape = &boxShape;
        RegisterFixture(body->CreateFixture(&fixtureDef), isTrigger, isPhantom);
    }

    void RegisterFixture(b2Fixture* fixture, bool isTrigger, bool isPhantom) {
        if (fixture == nullptr) {
            return;
        }

        auto binding = std::make_unique<FixtureBinding>();
        binding->rigidbody = this;
        binding->actor = actor;
        binding->is_trigger = isTrigger;
        binding->is_phantom = isPhantom;
        fixture->GetUserData().pointer = reinterpret_cast<uintptr_t>(binding.get());
        fixture_bindings.push_back(std::move(binding));
    }

    float GetRotationRadians() const {
        if (body != nullptr) {
            return body->GetAngle();
        }

        return rotation * (b2_pi / 180.0f);
    }

    static b2BodyType ResolveBodyType(const std::string& type) {
        if (type == "static") {
            return b2_staticBody;
        }
        if (type == "kinematic") {
            return b2_kinematicBody;
        }
        return b2_dynamicBody;
    }

    b2Body* body = nullptr;
    b2Vec2 linear_velocity = b2Vec2(0.0f, 0.0f);
    b2Vec2 pending_force = b2Vec2(0.0f, 0.0f);
    float angular_velocity_degrees = 0.0f;
    std::vector<std::unique_ptr<FixtureBinding>> fixture_bindings;
};

namespace ParticleSystemSeeds {
    constexpr int EmitAngle = 298;
    constexpr int EmitRadius = 404;
    constexpr int Rotation = 440;
    constexpr int StartScale = 494;
    constexpr int StartSpeed = 498;
    constexpr int RotationSpeed = 305;
}

class ParticleSystem {
public:
    struct Particle {
        bool active = false;
        int frames_alive = 0;
        float x = 0.0f;
        float y = 0.0f;
        float velocity_x = 0.0f;
        float velocity_y = 0.0f;
        float rotation_degrees = 0.0f;
        float angular_velocity_degrees = 0.0f;
        float start_scale = 1.0f;
    };

    std::string key = "";
    Actor* actor = nullptr;
    bool enabled = true;
    float x = 0.0f;
    float y = 0.0f;
    int frames_between_bursts = 1;
    int burst_quantity = 1;
    float start_scale_min = 1.0f;
    float start_scale_max = 1.0f;
    float rotation_min = 0.0f;
    float rotation_max = 0.0f;
    int start_color_r = 255;
    int start_color_g = 255;
    int start_color_b = 255;
    int start_color_a = 255;
    float emit_radius_min = 0.0f;
    float emit_radius_max = 0.5f;
    float emit_angle_min = 0.0f;
    float emit_angle_max = 360.0f;
    int duration_frames = 300;
    float start_speed_min = 0.0f;
    float start_speed_max = 0.0f;
    float rotation_speed_min = 0.0f;
    float rotation_speed_max = 0.0f;
    float gravity_scale_x = 0.0f;
    float gravity_scale_y = 0.0f;
    float drag_factor = 1.0f;
    float angular_drag_factor = 1.0f;
    float end_scale = std::numeric_limits<float>::quiet_NaN();
    int end_color_r = -1;
    int end_color_g = -1;
    int end_color_b = -1;
    int end_color_a = -1;
    std::string image = "";
    int sorting_order = 9999;

    void OnStart() {
        EnsureRandomEnginesConfigured();
    }

    void OnUpdate() {
        EnsureRandomEnginesConfigured();
        if (playback_active &&
            local_frame_number % configured_frames_between_bursts == 0) {
            EmitBurst();
        }

        ProcessParticles();
        ++local_frame_number;
    }

    void Stop() {
        playback_active = false;
    }

    void Play() {
        playback_active = true;
    }

    void Burst() {
        EnsureRandomEnginesConfigured();
        EmitBurst();
    }

private:
    static constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

    int local_frame_number = 0;
    bool playback_active = true;
    bool random_engines_configured = false;
    RandomEngine emit_angle_distribution;
    RandomEngine emit_radius_distribution;
    RandomEngine rotation_distribution;
    RandomEngine scale_distribution;
    RandomEngine speed_distribution;
    RandomEngine rotation_speed_distribution;
    std::vector<float> lifetime_progress_by_frame;
    std::vector<int> draw_color_r_by_frame;
    std::vector<int> draw_color_g_by_frame;
    std::vector<int> draw_color_b_by_frame;
    std::vector<int> draw_color_a_by_frame;
    std::vector<Particle> particles;
    std::vector<std::size_t> free_particle_indices;
    std::size_t free_particle_read_index = 0;
    const ImageDB::ImageInfo* configured_image_info = nullptr;
    int configured_frames_between_bursts = 1;
    int configured_burst_quantity = 1;
    int configured_duration_frames = 300;
    int configured_start_color_r = 255;
    int configured_start_color_g = 255;
    int configured_start_color_b = 255;
    int configured_start_color_a = 255;
    int configured_end_color_r = 255;
    int configured_end_color_g = 255;
    int configured_end_color_b = 255;
    int configured_end_color_a = 255;
    int configured_sorting_order = 9999;
    float configured_inverse_duration_minus_one = 0.0f;
    bool configured_has_end_scale = false;
    bool configured_has_end_color_r = false;
    bool configured_has_end_color_g = false;
    bool configured_has_end_color_b = false;
    bool configured_has_end_color_a = false;
    bool configured_has_scale_animation = false;
    bool configured_has_color_animation = false;
    bool configured_needs_lifetime_progress = false;
    bool configured_has_linear_motion = false;
    bool configured_has_angular_motion = false;
    bool configured_has_drag = false;
    bool configured_has_angular_drag = false;

    void EnsureRandomEnginesConfigured() {
        if (random_engines_configured) {
            return;
        }

        emit_angle_distribution.Configure(
            emit_angle_min,
            emit_angle_max,
            ParticleSystemSeeds::EmitAngle);
        emit_radius_distribution.Configure(
            emit_radius_min,
            emit_radius_max,
            ParticleSystemSeeds::EmitRadius);
        rotation_distribution.Configure(
            rotation_min,
            rotation_max,
            ParticleSystemSeeds::Rotation);
        scale_distribution.Configure(
            start_scale_min,
            start_scale_max,
            ParticleSystemSeeds::StartScale);
        speed_distribution.Configure(
            start_speed_min,
            start_speed_max,
            ParticleSystemSeeds::StartSpeed);
        rotation_speed_distribution.Configure(
            rotation_speed_min,
            rotation_speed_max,
            ParticleSystemSeeds::RotationSpeed);

        configured_frames_between_bursts = std::max(frames_between_bursts, 1);
        configured_burst_quantity = std::max(burst_quantity, 1);
        configured_duration_frames = GetClampedDurationFrames();
        configured_start_color_r = ClampColorValue(start_color_r);
        configured_start_color_g = ClampColorValue(start_color_g);
        configured_start_color_b = ClampColorValue(start_color_b);
        configured_start_color_a = ClampColorValue(start_color_a);
        configured_has_end_scale = !std::isnan(end_scale);
        configured_has_end_color_r = end_color_r >= 0;
        configured_has_end_color_g = end_color_g >= 0;
        configured_has_end_color_b = end_color_b >= 0;
        configured_has_end_color_a = end_color_a >= 0;
        configured_has_scale_animation = configured_has_end_scale;
        configured_has_color_animation =
            configured_has_end_color_r ||
            configured_has_end_color_g ||
            configured_has_end_color_b ||
            configured_has_end_color_a;
        configured_needs_lifetime_progress =
            configured_has_scale_animation || configured_has_color_animation;
        configured_has_linear_motion =
            gravity_scale_x != 0.0f ||
            gravity_scale_y != 0.0f ||
            drag_factor != 1.0f ||
            start_speed_min != 0.0f ||
            start_speed_max != 0.0f;
        configured_has_drag = drag_factor != 1.0f;
        configured_has_angular_motion =
            rotation_speed_min != 0.0f ||
            rotation_speed_max != 0.0f ||
            angular_drag_factor != 1.0f;
        configured_has_angular_drag = angular_drag_factor != 1.0f;
        configured_end_color_r = ClampColorValue(end_color_r);
        configured_end_color_g = ClampColorValue(end_color_g);
        configured_end_color_b = ClampColorValue(end_color_b);
        configured_end_color_a = ClampColorValue(end_color_a);
        configured_sorting_order = sorting_order;
        configured_inverse_duration_minus_one = configured_duration_frames <= 1
            ? 0.0f
            : 1.0f / static_cast<float>(configured_duration_frames);
        configured_image_info = &(image.empty()
            ? ImageDB::GetDefaultParticleImageInfo()
            : ImageDB::GetImageInfo(image));

        const std::size_t estimated_max_particles =
            static_cast<std::size_t>(
                ((configured_duration_frames + configured_frames_between_bursts - 1) /
                    configured_frames_between_bursts) *
                configured_burst_quantity);
        particles.reserve(std::max(particles.capacity(), estimated_max_particles));
        free_particle_indices.reserve(std::max(free_particle_indices.capacity(), estimated_max_particles));
        BuildColorLookupTables();
        random_engines_configured = true;
    }

    static int ClampColorValue(int value) {
        return std::clamp(value, 0, 255);
    }

    static float LerpFloat(float start, float end, float progress) {
        return start + (end - start) * progress;
    }

    int GetClampedDurationFrames() const {
        return std::max(duration_frames, 1);
    }

    void BuildColorLookupTables() {
        lifetime_progress_by_frame.assign(configured_duration_frames, 0.0f);
        draw_color_r_by_frame.assign(configured_duration_frames, configured_start_color_r);
        draw_color_g_by_frame.assign(configured_duration_frames, configured_start_color_g);
        draw_color_b_by_frame.assign(configured_duration_frames, configured_start_color_b);
        draw_color_a_by_frame.assign(configured_duration_frames, configured_start_color_a);

        for (int frame = 0; frame < configured_duration_frames; ++frame) {
            const float lifetime_progress = configured_needs_lifetime_progress
                ? static_cast<float>(frame) * configured_inverse_duration_minus_one
                : 0.0f;
            lifetime_progress_by_frame[frame] = lifetime_progress;

            if (configured_has_end_color_r) {
                draw_color_r_by_frame[frame] = ClampColorValue(static_cast<int>(LerpFloat(
                    static_cast<float>(configured_start_color_r),
                    static_cast<float>(configured_end_color_r),
                    lifetime_progress)));
            }
            if (configured_has_end_color_g) {
                draw_color_g_by_frame[frame] = ClampColorValue(static_cast<int>(LerpFloat(
                    static_cast<float>(configured_start_color_g),
                    static_cast<float>(configured_end_color_g),
                    lifetime_progress)));
            }
            if (configured_has_end_color_b) {
                draw_color_b_by_frame[frame] = ClampColorValue(static_cast<int>(LerpFloat(
                    static_cast<float>(configured_start_color_b),
                    static_cast<float>(configured_end_color_b),
                    lifetime_progress)));
            }
            if (configured_has_end_color_a) {
                draw_color_a_by_frame[frame] = ClampColorValue(static_cast<int>(LerpFloat(
                    static_cast<float>(configured_start_color_a),
                    static_cast<float>(configured_end_color_a),
                    lifetime_progress)));
            }
        }
    }

    Particle CreateParticle() {
        const float angle_radians = emit_angle_distribution.Sample() * kDegreesToRadians;
        const float cos_angle = glm::cos(angle_radians);
        const float sin_angle = glm::sin(angle_radians);
        const float radius = emit_radius_distribution.Sample();
        const float speed = speed_distribution.Sample();

        Particle particle;
        particle.active = true;
        particle.frames_alive = 0;
        particle.x = x + cos_angle * radius;
        particle.y = y + sin_angle * radius;
        particle.velocity_x = cos_angle * speed;
        particle.velocity_y = sin_angle * speed;
        particle.rotation_degrees = rotation_distribution.Sample();
        particle.angular_velocity_degrees = rotation_speed_distribution.Sample();
        particle.start_scale = scale_distribution.Sample();
        return particle;
    }

    void EmitBurst() {
        const int particles_to_emit = configured_burst_quantity;
        for (int i = 0; i < particles_to_emit; ++i) {
            if (free_particle_read_index < free_particle_indices.size()) {
                const std::size_t recycled_index = free_particle_indices[free_particle_read_index];
                ++free_particle_read_index;
                if (free_particle_read_index == free_particle_indices.size()) {
                    free_particle_indices.clear();
                    free_particle_read_index = 0;
                }
                particles[recycled_index] = CreateParticle();
            }
            else {
                particles.push_back(CreateParticle());
            }
        }
    }

    void ProcessParticles() {
        if (configured_image_info == nullptr || configured_image_info->texture == nullptr) {
            return;
        }

        for (std::size_t particle_index = 0; particle_index < particles.size(); ++particle_index) {
            Particle& particle = particles[particle_index];
            if (!particle.active) {
                continue;
            }

            if (particle.frames_alive >= configured_duration_frames) {
                particle.active = false;
                free_particle_indices.push_back(particle_index);
                continue;
            }

            if (configured_has_linear_motion) {
                particle.velocity_x += gravity_scale_x;
                particle.velocity_y += gravity_scale_y;
                if (configured_has_drag) {
                    particle.velocity_x *= drag_factor;
                    particle.velocity_y *= drag_factor;
                }

                particle.x += particle.velocity_x;
                particle.y += particle.velocity_y;
            }

            if (configured_has_angular_motion) {
                if (configured_has_angular_drag) {
                    particle.angular_velocity_degrees *= angular_drag_factor;
                }
                particle.rotation_degrees += particle.angular_velocity_degrees;
            }

            const int particle_age = particle.frames_alive;
            const float lifetime_progress = configured_has_scale_animation
                ? lifetime_progress_by_frame[particle_age]
                : 0.0f;

            const float draw_scale = configured_has_scale_animation
                ? LerpFloat(particle.start_scale, end_scale, lifetime_progress)
                : particle.start_scale;
            const int draw_a = draw_color_a_by_frame[particle_age];
            if (draw_scale <= 0.0f || draw_a <= 0) {
                ++particle.frames_alive;
                continue;
            }

            ImageDB::QueueDrawEx(
                *configured_image_info,
                particle.x,
                particle.y,
                static_cast<int>(particle.rotation_degrees),
                draw_scale,
                draw_scale,
                0.5f,
                0.5f,
                draw_color_r_by_frame[particle_age],
                draw_color_g_by_frame[particle_age],
                draw_color_b_by_frame[particle_age],
                draw_a,
                configured_sorting_order);

            ++particle.frames_alive;
        }
    }
};

static const b2Vec2 kInvalidContactVector(-999.0f, -999.0f);

static bool IsInvalidContactVector(const b2Vec2& value) {
    return value == kInvalidContactVector;
}

static Rigidbody::FixtureBinding* GetFixtureBinding(b2Fixture* fixture) {
    if (fixture == nullptr || fixture->GetUserData().pointer == 0) {
        return nullptr;
    }

    return reinterpret_cast<Rigidbody::FixtureBinding*>(fixture->GetUserData().pointer);
}

static void CallComponentPhysicsLifecycleFunction(
    Actor& actor,
    Actor::ComponentInstance& component,
    const luabridge::LuaRef& lifecycle_function,
    const Collision& collision) {
    if (g_active_engine == nullptr ||
        g_active_engine->lua_state == nullptr ||
        !component.lua_component_ref.has_value()) {
        return;
    }

    try {
        lifecycle_function.push(g_active_engine->lua_state);
        component.lua_component_ref->push(g_active_engine->lua_state);
        luabridge::Stack<Collision>::push(g_active_engine->lua_state, collision);
        luabridge::LuaException::pcall(g_active_engine->lua_state, 2, 0);
    }
    catch (const luabridge::LuaException& exception) {
        Engine::ReportLuaException(actor.actor_name, exception);
    }
}

static void DispatchPhysicsLifecycleToActor(
    Actor& actor,
    const char* function_name,
    const Collision& collision) {
    for (auto& [componentKey, component] : actor.components) {
        (void)componentKey;
        if (component.removed ||
            component.owner == nullptr ||
            component.owner->destroyed ||
            !component.on_start_called ||
            !IsComponentEnabled(component) ||
            !component.lua_component_ref.has_value()) {
            continue;
        }

        luabridge::LuaRef lifecycle_function = (*component.lua_component_ref)[function_name];
        if (!lifecycle_function.isFunction()) {
            continue;
        }

        CallComponentPhysicsLifecycleFunction(actor, component, lifecycle_function, collision);
    }
}

struct RaycastCandidate {
    HitResult hit;
    float fraction = 0.0f;
};

static b2Vec2 ComputeCollisionRelativeVelocity(
    b2Fixture* self_fixture,
    b2Fixture* other_fixture,
    const b2Vec2& point) {
    if (self_fixture == nullptr || other_fixture == nullptr) {
        return b2Vec2_zero;
    }

    b2Body* self_body = self_fixture->GetBody();
    b2Body* other_body = other_fixture->GetBody();
    if (self_body == nullptr || other_body == nullptr) {
        return b2Vec2_zero;
    }

    if (!IsInvalidContactVector(point)) {
        const b2Vec2 self_velocity = self_body->GetLinearVelocityFromWorldPoint(point);
        const b2Vec2 other_velocity = other_body->GetLinearVelocityFromWorldPoint(point);
        return self_velocity - other_velocity;
    }

    return self_body->GetLinearVelocity() - other_body->GetLinearVelocity();
}

static Collision BuildCollisionForFixture(
    Actor* other_actor,
    const b2Vec2& point,
    const b2Vec2& normal,
    const b2Vec2& relative_velocity) {
    Collision collision;
    collision.other = other_actor;
    collision.point = point;
    collision.normal = normal;
    collision.relative_velocity = relative_velocity;
    return collision;
}

static HitResult BuildHitResultFromFixture(
    Rigidbody::FixtureBinding* binding,
    const b2Vec2& point,
    const b2Vec2& normal) {
    HitResult hit;
    if (binding != nullptr) {
        hit.actor = binding->actor;
        hit.is_trigger = binding->is_trigger;
    }
    hit.point = point;
    hit.normal = normal;
    return hit;
}

class RaycastClosestCallback : public b2RayCastCallback {
public:
    std::optional<RaycastCandidate> closest_hit;

    float ReportFixture(
        b2Fixture* fixture,
        const b2Vec2& point,
        const b2Vec2& normal,
        float fraction) override {
        Rigidbody::FixtureBinding* binding = GetFixtureBinding(fixture);
        if (binding == nullptr || binding->actor == nullptr) {
            return -1.0f;
        }

        if (binding->actor->destroyed || binding->is_phantom) {
            return -1.0f;
        }

        RaycastCandidate candidate;
        candidate.hit = BuildHitResultFromFixture(binding, point, normal);
        candidate.fraction = fraction;

        if (!closest_hit.has_value() || fraction < closest_hit->fraction) {
            closest_hit = candidate;
        }

        return 1.0f;
    }
};

class RaycastAllCallback : public b2RayCastCallback {
public:
    std::vector<RaycastCandidate> hits;

    float ReportFixture(
        b2Fixture* fixture,
        const b2Vec2& point,
        const b2Vec2& normal,
        float fraction) override {
        Rigidbody::FixtureBinding* binding = GetFixtureBinding(fixture);
        if (binding == nullptr || binding->actor == nullptr) {
            return -1.0f;
        }

        if (binding->actor->destroyed || binding->is_phantom) {
            return -1.0f;
        }

        hits.push_back(RaycastCandidate{
            BuildHitResultFromFixture(binding, point, normal),
            fraction
            });
        return 1.0f;
    }
};

static luabridge::LuaRef LuaPhysicsRaycast(const b2Vec2& pos, b2Vec2 dir, float dist) {
    lua_State* luaState = (g_active_engine != nullptr) ? g_active_engine->lua_state : nullptr;
    if (g_active_engine == nullptr || luaState == nullptr || dist <= 0.0f || !g_active_engine->physics_world) {
        return luabridge::LuaRef(luaState);
    }

    if (dir.Normalize() == 0.0f) {
        return luabridge::LuaRef(luaState);
    }

    RaycastClosestCallback callback;
    const b2Vec2 end = pos + dir * dist;
    g_active_engine->physics_world->RayCast(&callback, pos, end);

    if (!callback.closest_hit.has_value()) {
        return luabridge::LuaRef(luaState);
    }

    return luabridge::LuaRef(luaState, callback.closest_hit->hit);
}

static luabridge::LuaRef LuaPhysicsRaycastAll(const b2Vec2& pos, b2Vec2 dir, float dist) {
    lua_State* luaState = (g_active_engine != nullptr) ? g_active_engine->lua_state : nullptr;
    if (luaState == nullptr) {
        return luabridge::LuaRef(luaState);
    }

    luabridge::LuaRef results = luabridge::newTable(luaState);
    if (g_active_engine == nullptr || dist <= 0.0f || !g_active_engine->physics_world) {
        return results;
    }

    if (dir.Normalize() == 0.0f) {
        return results;
    }

    RaycastAllCallback callback;
    const b2Vec2 end = pos + dir * dist;
    g_active_engine->physics_world->RayCast(&callback, pos, end);

    std::stable_sort(
        callback.hits.begin(),
        callback.hits.end(),
        [](const RaycastCandidate& lhs, const RaycastCandidate& rhs) {
            return lhs.fraction < rhs.fraction;
        });

    for (std::size_t i = 0; i < callback.hits.size(); ++i) {
        results[static_cast<int>(i) + 1] = callback.hits[i].hit;
    }

    return results;
}

static void DispatchPhysicsContactEvent(b2Contact* contact, bool is_begin_contact) {
    if (g_active_engine == nullptr || contact == nullptr || !g_physics_callbacks_enabled) {
        return;
    }

    b2Fixture* fixtureA = contact->GetFixtureA();
    b2Fixture* fixtureB = contact->GetFixtureB();
    Rigidbody::FixtureBinding* bindingA = GetFixtureBinding(fixtureA);
    Rigidbody::FixtureBinding* bindingB = GetFixtureBinding(fixtureB);
    if (bindingA == nullptr || bindingB == nullptr) {
        return;
    }

    if (bindingA->actor == nullptr || bindingB->actor == nullptr ||
        bindingA->actor->destroyed || bindingB->actor->destroyed) {
        return;
    }

    const bool is_trigger_contact =
        bindingA->is_trigger && bindingB->is_trigger &&
        !bindingA->is_phantom && !bindingB->is_phantom &&
        fixtureA->IsSensor() && fixtureB->IsSensor();
    const bool is_collision_contact =
        !bindingA->is_trigger && !bindingB->is_trigger &&
        !bindingA->is_phantom && !bindingB->is_phantom &&
        !fixtureA->IsSensor() && !fixtureB->IsSensor();

    if (!is_trigger_contact && !is_collision_contact) {
        return;
    }

    b2Vec2 point = kInvalidContactVector;
    b2Vec2 normal = kInvalidContactVector;
    if (is_begin_contact && is_collision_contact) {
        b2WorldManifold world_manifold;
        contact->GetWorldManifold(&world_manifold);
        point = world_manifold.points[0];
        normal = world_manifold.normal;
    }
    const b2Vec2 relative_velocity = ComputeCollisionRelativeVelocity(
        fixtureA,
        fixtureB,
        point);

    const Collision collisionA = BuildCollisionForFixture(
        bindingB->actor,
        point,
        normal,
        relative_velocity);
    const Collision collisionB = BuildCollisionForFixture(
        bindingA->actor,
        point,
        normal,
        relative_velocity);

    const char* lifecycle_function = nullptr;
    if (is_trigger_contact) {
        lifecycle_function = is_begin_contact ? "OnTriggerEnter" : "OnTriggerExit";
    }
    else {
        lifecycle_function = is_begin_contact ? "OnCollisionEnter" : "OnCollisionExit";
    }

    DispatchPhysicsLifecycleToActor(
        *bindingA->actor,
        lifecycle_function,
        collisionA);
    DispatchPhysicsLifecycleToActor(
        *bindingB->actor,
        lifecycle_function,
        collisionB);
}

class PhysicsContactListener : public b2ContactListener {
public:
    void BeginContact(b2Contact* contact) override {
        DispatchPhysicsContactEvent(contact, true);
    }

    void EndContact(b2Contact* contact) override {
        DispatchPhysicsContactEvent(contact, false);
    }
};

static PhysicsContactListener g_physics_contact_listener;

static luabridge::LuaRef MakeNilLuaRef(lua_State* luaState) {
    return luabridge::LuaRef(luaState);
}

static bool TryResolveEventComponentIdentity(
    const luabridge::LuaRef& component_ref,
    Actor*& actor,
    std::string& component_key) {
    actor = nullptr;
    component_key.clear();

    if (component_ref.isNil()) {
        return false;
    }

    try {
        luabridge::LuaRef actor_ref = component_ref["actor"];
        luabridge::LuaRef key_ref = component_ref["key"];
        if (actor_ref.isNil() || key_ref.isNil()) {
            return false;
        }

        actor = actor_ref.cast<Actor*>();
        component_key = key_ref.cast<std::string>();
        return actor != nullptr && !component_key.empty();
    }
    catch (const luabridge::LuaException&) {
        return false;
    }
    catch (const std::exception&) {
        return false;
    }
}

static bool EventSubscriptionRefsMatch(
    const std::shared_ptr<luabridge::LuaRef>& lhs,
    const std::shared_ptr<luabridge::LuaRef>& rhs) {
    return lhs != nullptr &&
        rhs != nullptr &&
        lhs->rawequal(*rhs);
}

static bool EventSubscriptionMatches(
    const Engine::EventSubscription& subscription,
    const Engine::PendingEventChange& change) {
    return subscription.actor == change.actor &&
        subscription.component_key == change.component_key &&
        EventSubscriptionRefsMatch(subscription.component_ref, change.component_ref) &&
        EventSubscriptionRefsMatch(subscription.function_ref, change.function_ref);
}

static bool IsEventSubscriptionAlive(const Engine::EventSubscription& subscription) {
    if (subscription.actor == nullptr || subscription.actor->destroyed) {
        return false;
    }

    const auto found = subscription.actor->components.find(subscription.component_key);
    if (found == subscription.actor->components.end()) {
        return false;
    }

    const Actor::ComponentInstance& component = found->second;
    return !component.removed &&
        component.lua_component_ref.has_value() &&
        subscription.component_ref != nullptr &&
        component.lua_component_ref->rawequal(*subscription.component_ref);
}

static b2Vec2 AddVector2(const b2Vec2* lhs, const b2Vec2& rhs) {
    return *lhs + rhs;
}

static b2Vec2 SubtractVector2(const b2Vec2* lhs, const b2Vec2& rhs) {
    return *lhs - rhs;
}

static b2Vec2 MultiplyVector2(const b2Vec2* vector, float scalar) {
    return *vector * scalar;
}

static bool ComponentComesBefore(
    const Actor::ComponentInstance* lhs,
    const Actor::ComponentInstance* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    const int lhsActorId = (lhs->owner != nullptr) ? lhs->owner->id : -1;
    const int rhsActorId = (rhs->owner != nullptr) ? rhs->owner->id : -1;
    if (lhsActorId != rhsActorId) {
        return lhsActorId < rhsActorId;
    }

    return lhs->key < rhs->key;
}

static void InsertLifecycleComponentSorted(
    std::vector<Actor::ComponentInstance*>& lifecycle_components,
    Actor::ComponentInstance& component) {
    if (lifecycle_components.empty() ||
        !ComponentComesBefore(&component, lifecycle_components.back())) {
        lifecycle_components.push_back(&component);
        return;
    }

    const auto insertIt = std::upper_bound(
        lifecycle_components.begin(),
        lifecycle_components.end(),
        &component,
        [](const Actor::ComponentInstance* value, const Actor::ComponentInstance* existing) {
            return ComponentComesBefore(value, existing);
        });
    lifecycle_components.insert(insertIt, &component);
}

static bool IsComponentEnabled(Actor::ComponentInstance& component) {
    if (!component.lua_component_ref.has_value()) {
        return false;
    }

    lua_State* luaState = component.lua_component_ref->state();
    if (luaState == nullptr) {
        return component.enabled;
    }

    component.lua_component_ref->push(luaState);
    lua_getfield(luaState, -1, "enabled");

    bool enabled = component.enabled;
    if (lua_isboolean(luaState, -1)) {
        enabled = lua_toboolean(luaState, -1) != 0;
    }

    lua_pop(luaState, 2);
    component.enabled = enabled;
    return enabled;
}

static void SetComponentEnabled(Actor::ComponentInstance& component, bool enabled) {
    component.enabled = enabled;
    if (component.lua_component_ref.has_value()) {
        (*component.lua_component_ref)["enabled"] = enabled;
    }
}

static void ApplyComponentProperty(
    const luabridge::LuaRef& componentRef,
    const std::string& propertyName,
    const Actor::ComponentProperty& propertyValue) {
    switch (propertyValue.type) {
    case Actor::ComponentProperty::Type::Nil:
        componentRef[propertyName] = luabridge::Nil();
        break;
    case Actor::ComponentProperty::Type::Boolean:
        componentRef[propertyName] = propertyValue.bool_value;
        break;
    case Actor::ComponentProperty::Type::Integer:
        componentRef[propertyName] = propertyValue.integer_value;
        break;
    case Actor::ComponentProperty::Type::Number:
        componentRef[propertyName] = propertyValue.number_value;
        break;
    case Actor::ComponentProperty::Type::String:
        componentRef[propertyName] = propertyValue.string_value;
        break;
    }
}

static void CacheComponentLifecycleFunctions(Actor::ComponentInstance& instance) {
    if (!instance.lua_component_ref.has_value()) {
        return;
    }

    luabridge::LuaRef onStartFunction = (*instance.lua_component_ref)["OnStart"];
    if (onStartFunction.isFunction()) {
        instance.on_start_function = std::move(onStartFunction);
        instance.has_on_start = true;
    }

    luabridge::LuaRef onUpdateFunction = (*instance.lua_component_ref)["OnUpdate"];
    if (onUpdateFunction.isFunction()) {
        instance.on_update_function = std::move(onUpdateFunction);
        instance.has_on_update = true;
    }

    luabridge::LuaRef onLateUpdateFunction = (*instance.lua_component_ref)["OnLateUpdate"];
    if (onLateUpdateFunction.isFunction()) {
        instance.on_late_update_function = std::move(onLateUpdateFunction);
        instance.has_on_late_update = true;
    }

    luabridge::LuaRef onDestroyFunction = (*instance.lua_component_ref)["OnDestroy"];
    if (onDestroyFunction.isFunction()) {
        instance.on_destroy_function = std::move(onDestroyFunction);
        instance.has_on_destroy = true;
    }
}

static Actor* FindFirstActorByName(const std::string& name) {
    if (g_active_engine == nullptr) {
        return nullptr;
    }

    const auto found = g_active_engine->actor_refs_by_name.find(name);
    if (found == g_active_engine->actor_refs_by_name.end() || found->second.empty()) {
        return nullptr;
    }

    return found->second.front();
}

static luabridge::LuaRef FindAllActorsByName(const std::string& name) {
    lua_State* luaState = (g_active_engine != nullptr) ? g_active_engine->lua_state : nullptr;
    if (luaState == nullptr) {
        return MakeNilLuaRef(luaState);
    }

    luabridge::LuaRef matches = luabridge::newTable(luaState);

    if (g_active_engine == nullptr) {
        return matches;
    }

    const auto found = g_active_engine->actor_refs_by_name.find(name);
    if (found == g_active_engine->actor_refs_by_name.end()) {
        return matches;
    }

    int luaIndex = 1;
    for (Actor* actor : found->second) {
        if (actor != nullptr && !actor->destroyed) {
            matches[luaIndex] = actor;
            ++luaIndex;
        }
    }

    return matches;
}

std::string Actor::GetName() const {
    return actor_name;
}

int Actor::GetID() const {
    return id;
}

luabridge::LuaRef Actor::AddComponent(const std::string& type_name) {
    if (g_active_engine == nullptr) {
        return MakeNilLuaRef(lua_state);
    }

    return g_active_engine->AddRuntimeComponent(*this, type_name);
}

void Actor::RemoveComponent(const luabridge::LuaRef& component_ref) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->RemoveRuntimeComponent(*this, component_ref);
}

luabridge::LuaRef Actor::GetComponentByKey(const std::string& key) const {
    if (lua_state == nullptr || destroyed) {
        return MakeNilLuaRef(lua_state);
    }

    const auto found = components.find(key);
    if (found == components.end() ||
        !found->second.lua_component_ref.has_value() ||
        found->second.removed) {
        return MakeNilLuaRef(lua_state);
    }

    return *found->second.lua_component_ref;
}

luabridge::LuaRef Actor::GetComponent(const std::string& type_name) const {
    if (lua_state == nullptr || destroyed) {
        return MakeNilLuaRef(lua_state);
    }

    const auto indexedComponents = component_instances_by_type.find(type_name);
    if (indexedComponents == component_instances_by_type.end()) {
        return MakeNilLuaRef(lua_state);
    }

    for (const ComponentInstance* component : indexedComponents->second) {
        if (component != nullptr &&
            !component->removed &&
            component->lua_component_ref.has_value()) {
            return *component->lua_component_ref;
        }
    }

    return MakeNilLuaRef(lua_state);
}

luabridge::LuaRef Actor::GetComponents(const std::string& type_name) const {
    if (lua_state == nullptr) {
        return MakeNilLuaRef(lua_state);
    }

    if (destroyed) {
        return luabridge::newTable(lua_state);
    }

    luabridge::LuaRef matches = luabridge::newTable(lua_state);

    const auto indexedComponents = component_instances_by_type.find(type_name);
    if (indexedComponents == component_instances_by_type.end()) {
        return matches;
    }

    int luaIndex = 1;
    for (const ComponentInstance* component : indexedComponents->second) {
        if (component != nullptr &&
            !component->removed &&
            component->lua_component_ref.has_value()) {
            matches[luaIndex] = *component->lua_component_ref;
            ++luaIndex;
        }
    }

    return matches;
}

Engine::Engine() {
    lua_state = luabridge::CreateLuaState();
    g_active_engine = this;
    if (lua_state == nullptr) {
        return;
    }

    RegisterDebugAPI();
    RegisterActorAPI();
    RegisterApplicationAPI();
    RegisterInputAPI();
    RegisterPhysicsAPI();
    RegisterParticleAPI();
    RegisterTextAPI();
    RegisterAudioAPI();
    RegisterImageAPI();
    RegisterCameraAPI();
    RegisterSceneAPI();
    RegisterEventAPI();
}

Engine::~Engine() {
    ClearComponentInstanceRefs();
    component_type_refs.clear();

    luabridge::CloseLuaState(lua_state);
    lua_state = nullptr;
    if (g_active_engine == this) {
        g_active_engine = nullptr;
    }
}

void Engine::LuaDebugLog(const std::string& message) {
    std::cout << message << '\n';
}

void Engine::ReportLuaException(const std::string& actor_name, const luabridge::LuaException& exception) {
    std::cout
        << "\033[31m" << actor_name << " : "
        << FormatLuaExceptionMessage(exception.what())
        << "\033[0m"
        << '\n';
}

Actor* Engine::LuaActorInstantiate(const std::string& actor_template_name) {
    if (g_active_engine == nullptr) {
        return nullptr;
    }

    return g_active_engine->InstantiateRuntimeActor(actor_template_name);
}

void Engine::LuaActorDestroy(Actor* actor) {
    if (g_active_engine == nullptr || actor == nullptr) {
        return;
    }

    g_active_engine->DestroyRuntimeActor(*actor);
}

void Engine::LuaApplicationQuit() {
    std::exit(0);
}

void Engine::LuaApplicationSleep(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int Engine::LuaApplicationGetFrame() {
    return Helper::GetFrameNumber();
}

void Engine::LuaApplicationOpenURL(const std::string& url) {
#ifdef _WIN32
    const std::string command = "start \"\" " + QuoteForShell(url);
#elif defined(__APPLE__)
    const std::string command = "open " + QuoteForShell(url);
#else
    const std::string command = "xdg-open " + QuoteForShell(url);
#endif
    std::system(command.c_str());
}

void Engine::LuaTextDraw(
    const std::string& text,
    float x,
    float y,
    const std::string& font_name,
    float font_size,
    float r,
    float g,
    float b,
    float a) {
    TextDB::DrawText(text, x, y, font_name, font_size, r, g, b, a);
}

void Engine::LuaAudioPlay(int channel, const std::string& clip_name, bool does_loop) {
    AudioDB::PlayChannel(channel, clip_name, does_loop ? -1 : 0);
}

void Engine::LuaAudioHalt(int channel) {
    AudioDB::HaltChannel(channel);
}

void Engine::LuaAudioSetVolume(int channel, float volume) {
    AudioDB::SetVolume(channel, static_cast<int>(volume));
}

void Engine::LuaImageDrawUI(const std::string& image_name, float x, float y) {
    ImageDB::DrawUI(image_name, x, y);
}

void Engine::LuaImageDrawUIEx(
    const std::string& image_name,
    float x,
    float y,
    float r,
    float g,
    float b,
    float a,
    float sorting_order) {
    ImageDB::DrawUIEx(image_name, x, y, r, g, b, a, sorting_order);
}

void Engine::LuaImageDraw(const std::string& image_name, float x, float y) {
    ImageDB::Draw(image_name, x, y);
}

void Engine::LuaImageDrawEx(
    const std::string& image_name,
    float x,
    float y,
    float rotation_degrees,
    float scale_x,
    float scale_y,
    float pivot_x,
    float pivot_y,
    float r,
    float g,
    float b,
    float a,
    float sorting_order) {
    ImageDB::DrawEx(
        image_name,
        x,
        y,
        rotation_degrees,
        scale_x,
        scale_y,
        pivot_x,
        pivot_y,
        r,
        g,
        b,
        a,
        sorting_order);
}

int Engine::LuaImageDrawExRaw(lua_State* luaState) {
    const std::string imageName = luaL_checkstring(luaState, 1);
    const float x = static_cast<float>(luaL_checknumber(luaState, 2));
    const float y = static_cast<float>(luaL_checknumber(luaState, 3));
    const float rotationDegrees = static_cast<float>(luaL_checknumber(luaState, 4));
    const float scaleX = static_cast<float>(luaL_checknumber(luaState, 5));
    const float scaleY = static_cast<float>(luaL_checknumber(luaState, 6));
    const float pivotX = static_cast<float>(luaL_checknumber(luaState, 7));
    const float pivotY = static_cast<float>(luaL_checknumber(luaState, 8));
    const float r = static_cast<float>(luaL_checknumber(luaState, 9));
    const float g = static_cast<float>(luaL_checknumber(luaState, 10));
    const float b = static_cast<float>(luaL_checknumber(luaState, 11));
    const float a = static_cast<float>(luaL_checknumber(luaState, 12));
    const float sortingOrder = static_cast<float>(luaL_checknumber(luaState, 13));

    LuaImageDrawEx(
        imageName,
        x,
        y,
        rotationDegrees,
        scaleX,
        scaleY,
        pivotX,
        pivotY,
        r,
        g,
        b,
        a,
        sortingOrder);
    return 0;
}

void Engine::LuaImageDrawPixel(float x, float y, float r, float g, float b, float a) {
    ImageDB::DrawPixel(x, y, r, g, b, a);
}

void Engine::LuaCameraSetPosition(float x, float y) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->currentCameraPosition = glm::vec2(x, y);
    g_active_engine->cameraPositionInitialized = true;
}

float Engine::LuaCameraGetPositionX() {
    if (g_active_engine == nullptr) {
        return 0.0f;
    }

    return g_active_engine->currentCameraPosition.x;
}

float Engine::LuaCameraGetPositionY() {
    if (g_active_engine == nullptr) {
        return 0.0f;
    }

    return g_active_engine->currentCameraPosition.y;
}

void Engine::LuaCameraSetZoom(float zoom_factor) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->_db.zoom_factor = zoom_factor;
}

float Engine::LuaCameraGetZoom() {
    if (g_active_engine == nullptr) {
        return 1.0f;
    }

    return g_active_engine->_db.zoom_factor;
}

void Engine::LuaSceneLoad(const std::string& scene_name) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->pending_scene_load_name = NormalizeSceneName(scene_name);
}

std::string Engine::LuaSceneGetCurrent() {
    if (g_active_engine == nullptr) {
        return "";
    }

    return g_active_engine->current_scene_name;
}

void Engine::LuaSceneDontDestroy(Actor* actor) {
    if (actor == nullptr || actor->destroyed) {
        return;
    }

    actor->dont_destroy = true;
}

void Engine::LuaEventPublish(const std::string& event_type, const luabridge::LuaRef& event_object) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->PublishEvent(event_type, event_object);
}

void Engine::LuaEventSubscribe(
    const std::string& event_type,
    const luabridge::LuaRef& component_ref,
    const luabridge::LuaRef& function_ref) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->QueueEventSubscriptionChange(
        PendingEventChangeType::Subscribe,
        event_type,
        component_ref,
        function_ref);
}

void Engine::LuaEventUnsubscribe(
    const std::string& event_type,
    const luabridge::LuaRef& component_ref,
    const luabridge::LuaRef& function_ref) {
    if (g_active_engine == nullptr) {
        return;
    }

    g_active_engine->QueueEventSubscriptionChange(
        PendingEventChangeType::Unsubscribe,
        event_type,
        component_ref,
        function_ref);
}

void Engine::RegisterDebugAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Debug")
        .addFunction("Log", &Engine::LuaDebugLog)
        .endNamespace();
}

void Engine::RegisterActorAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginClass<Actor>("ActorRef")
        .addFunction("GetName", &Actor::GetName)
        .addFunction("GetID", &Actor::GetID)
        .addFunction("AddComponent", &Actor::AddComponent)
        .addFunction("RemoveComponent", &Actor::RemoveComponent)
        .addFunction("GetComponentByKey", &Actor::GetComponentByKey)
        .addFunction("GetComponent", &Actor::GetComponent)
        .addFunction("GetComponents", &Actor::GetComponents)
        .endClass()
        .beginNamespace("Actor")
        .addFunction("Find", &FindFirstActorByName)
        .addFunction("FindAll", &FindAllActorsByName)
        .addFunction("Instantiate", &Engine::LuaActorInstantiate)
        .addFunction("Destroy", &Engine::LuaActorDestroy)
        .endNamespace();
}

void Engine::RegisterApplicationAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Application")
        .addFunction("Quit", &Engine::LuaApplicationQuit)
        .addFunction("Sleep", &Engine::LuaApplicationSleep)
        .addFunction("GetFrame", &Engine::LuaApplicationGetFrame)
        .addFunction("OpenURL", &Engine::LuaApplicationOpenURL)
        .endNamespace();
}

void Engine::RegisterInputAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginClass<glm::vec2>("vec2")
        .addData("x", &glm::vec2::x)
        .addData("y", &glm::vec2::y)
        .endClass()
        .beginNamespace("Input")
        .addFunction("GetKey", static_cast<bool(*)(const std::string&)>(&Input::GetKey))
        .addFunction("GetKeyDown", static_cast<bool(*)(const std::string&)>(&Input::GetKeyDown))
        .addFunction("GetKeyUp", static_cast<bool(*)(const std::string&)>(&Input::GetKeyUp))
        .addFunction("GetMousePosition", &Input::GetMousePosition)
        .addFunction("GetMouseButton", &Input::GetMouseButton)
        .addFunction("GetMouseButtonDown", &Input::GetMouseButtonDown)
        .addFunction("GetMouseButtonUp", &Input::GetMouseButtonUp)
        .addFunction("GetMouseScrollDelta", &Input::GetMouseScrollDelta)
        .addFunction("HideCursor", &Input::HideCursor)
        .addFunction("ShowCursor", &Input::ShowCursor)
        .endNamespace();
}

void Engine::RegisterPhysicsAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginClass<Rigidbody>("Rigidbody")
        .addConstructor<void(*)()>()
        .addData("key", &Rigidbody::key)
        .addData("actor", &Rigidbody::actor)
        .addData("enabled", &Rigidbody::enabled)
        .addData("x", &Rigidbody::x)
        .addData("y", &Rigidbody::y)
        .addData("body_type", &Rigidbody::body_type)
        .addData("precise", &Rigidbody::precise)
        .addData("gravity_scale", &Rigidbody::gravity_scale)
        .addData("density", &Rigidbody::density)
        .addData("angular_friction", &Rigidbody::angular_friction)
        .addData("rotation", &Rigidbody::rotation)
        .addData("has_collider", &Rigidbody::has_collider)
        .addData("has_trigger", &Rigidbody::has_trigger)
        .addData("collider_type", &Rigidbody::collider_type)
        .addData("trigger_type", &Rigidbody::trigger_type)
        .addData("width", &Rigidbody::width)
        .addData("height", &Rigidbody::height)
        .addData("radius", &Rigidbody::radius)
        .addData("trigger_width", &Rigidbody::trigger_width)
        .addData("trigger_height", &Rigidbody::trigger_height)
        .addData("trigger_radius", &Rigidbody::trigger_radius)
        .addData("friction", &Rigidbody::friction)
        .addData("bounciness", &Rigidbody::bounciness)
        .addFunction("OnStart", &Rigidbody::OnStart)
        .addFunction("OnDestroy", &Rigidbody::OnDestroy)
        .addFunction("AddForce", &Rigidbody::AddForce)
        .addFunction("SetVelocity", &Rigidbody::SetVelocity)
        .addFunction("SetPosition", &Rigidbody::SetPosition)
        .addFunction("SetRotation", &Rigidbody::SetRotation)
        .addFunction("SetAngularVelocity", &Rigidbody::SetAngularVelocity)
        .addFunction("SetGravityScale", &Rigidbody::SetGravityScale)
        .addFunction("SetUpDirection", &Rigidbody::SetUpDirection)
        .addFunction("SetRightDirection", &Rigidbody::SetRightDirection)
        .addFunction("GetPosition", &Rigidbody::GetPosition)
        .addFunction("GetRotation", &Rigidbody::GetRotation)
        .addFunction("GetVelocity", &Rigidbody::GetVelocity)
        .addFunction("GetAngularVelocity", &Rigidbody::GetAngularVelocity)
        .addFunction("GetGravityScale", &Rigidbody::GetGravityScale)
        .addFunction("GetUpDirection", &Rigidbody::GetUpDirection)
        .addFunction("GetRightDirection", &Rigidbody::GetRightDirection)
        .endClass()
        .beginClass<Collision>("Collision")
        .addData("other", &Collision::other)
        .addData("point", &Collision::point)
        .addData("relative_velocity", &Collision::relative_velocity)
        .addData("normal", &Collision::normal)
        .endClass()
        .beginClass<HitResult>("HitResult")
        .addData("actor", &HitResult::actor)
        .addData("point", &HitResult::point)
        .addData("normal", &HitResult::normal)
        .addData("is_trigger", &HitResult::is_trigger)
        .endClass()
        .beginClass<b2Vec2>("Vector2")
        .addConstructor<void(*)(float, float)>()
        .addData("x", &b2Vec2::x)
        .addData("y", &b2Vec2::y)
        .addFunction("Normalize", &b2Vec2::Normalize)
        .addFunction("Length", &b2Vec2::Length)
        .addFunction("__add", &AddVector2)
        .addFunction("__sub", &SubtractVector2)
        .addFunction("__mul", &MultiplyVector2)
        .addStaticFunction("Distance", static_cast<float(*)(const b2Vec2&, const b2Vec2&)>(&b2Distance))
        .addStaticFunction("Dot", static_cast<float(*)(const b2Vec2&, const b2Vec2&)>(&b2Dot))
        .endClass()
        .beginNamespace("Physics")
        .addFunction("Raycast", &LuaPhysicsRaycast)
        .addFunction("RaycastAll", &LuaPhysicsRaycastAll)
        .endNamespace();
}

void Engine::RegisterParticleAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginClass<ParticleSystem>("ParticleSystem")
        .addConstructor<void(*)()>()
        .addData("key", &ParticleSystem::key)
        .addData("actor", &ParticleSystem::actor)
        .addData("enabled", &ParticleSystem::enabled)
        .addData("x", &ParticleSystem::x)
        .addData("y", &ParticleSystem::y)
        .addData("frames_between_bursts", &ParticleSystem::frames_between_bursts)
        .addData("burst_quantity", &ParticleSystem::burst_quantity)
        .addData("start_scale_min", &ParticleSystem::start_scale_min)
        .addData("start_scale_max", &ParticleSystem::start_scale_max)
        .addData("rotation_min", &ParticleSystem::rotation_min)
        .addData("rotation_max", &ParticleSystem::rotation_max)
        .addData("start_color_r", &ParticleSystem::start_color_r)
        .addData("start_color_g", &ParticleSystem::start_color_g)
        .addData("start_color_b", &ParticleSystem::start_color_b)
        .addData("start_color_a", &ParticleSystem::start_color_a)
        .addData("emit_radius_min", &ParticleSystem::emit_radius_min)
        .addData("emit_radius_max", &ParticleSystem::emit_radius_max)
        .addData("emit_angle_min", &ParticleSystem::emit_angle_min)
        .addData("emit_angle_max", &ParticleSystem::emit_angle_max)
        .addData("duration_frames", &ParticleSystem::duration_frames)
        .addData("start_speed_min", &ParticleSystem::start_speed_min)
        .addData("start_speed_max", &ParticleSystem::start_speed_max)
        .addData("rotation_speed_min", &ParticleSystem::rotation_speed_min)
        .addData("rotation_speed_max", &ParticleSystem::rotation_speed_max)
        .addData("gravity_scale_x", &ParticleSystem::gravity_scale_x)
        .addData("gravity_scale_y", &ParticleSystem::gravity_scale_y)
        .addData("drag_factor", &ParticleSystem::drag_factor)
        .addData("angular_drag_factor", &ParticleSystem::angular_drag_factor)
        .addData("end_scale", &ParticleSystem::end_scale)
        .addData("end_color_r", &ParticleSystem::end_color_r)
        .addData("end_color_g", &ParticleSystem::end_color_g)
        .addData("end_color_b", &ParticleSystem::end_color_b)
        .addData("end_color_a", &ParticleSystem::end_color_a)
        .addData("image", &ParticleSystem::image)
        .addData("sorting_order", &ParticleSystem::sorting_order)
        .addFunction("OnStart", &ParticleSystem::OnStart)
        .addFunction("OnUpdate", &ParticleSystem::OnUpdate)
        .addFunction("Stop", &ParticleSystem::Stop)
        .addFunction("Play", &ParticleSystem::Play)
        .addFunction("Burst", &ParticleSystem::Burst)
        .endClass();
}

void Engine::RegisterTextAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Text")
        .addFunction("Draw", &Engine::LuaTextDraw)
        .endNamespace();
}

void Engine::RegisterAudioAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Audio")
        .addFunction("Play", &Engine::LuaAudioPlay)
        .addFunction("Halt", &Engine::LuaAudioHalt)
        .addFunction("SetVolume", &Engine::LuaAudioSetVolume)
        .endNamespace();
}

void Engine::RegisterImageAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Image")
        .addFunction("DrawUI", &Engine::LuaImageDrawUI)
        .addFunction("DrawUIEx", &Engine::LuaImageDrawUIEx)
        .addFunction("Draw", &Engine::LuaImageDraw)
        .addFunction("DrawEx", &Engine::LuaImageDrawExRaw)
        .addFunction("DrawPixel", &Engine::LuaImageDrawPixel)
        .endNamespace();
}

void Engine::RegisterCameraAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Camera")
        .addFunction("SetPosition", &Engine::LuaCameraSetPosition)
        .addFunction("GetPositionX", &Engine::LuaCameraGetPositionX)
        .addFunction("GetPositionY", &Engine::LuaCameraGetPositionY)
        .addFunction("SetZoom", &Engine::LuaCameraSetZoom)
        .addFunction("GetZoom", &Engine::LuaCameraGetZoom)
        .endNamespace();
}

void Engine::RegisterSceneAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Scene")
        .addFunction("Load", &Engine::LuaSceneLoad)
        .addFunction("GetCurrent", &Engine::LuaSceneGetCurrent)
        .addFunction("DontDestroy", &Engine::LuaSceneDontDestroy)
        .endNamespace();
}

void Engine::RegisterEventAPI() {
    if (lua_state == nullptr) {
        return;
    }

    luabridge::getGlobalNamespace(lua_state)
        .beginNamespace("Event")
        .addFunction("Publish", &Engine::LuaEventPublish)
        .addFunction("Subscribe", &Engine::LuaEventSubscribe)
        .addFunction("Unsubscribe", &Engine::LuaEventUnsubscribe)
        .endNamespace();
}

b2World* Engine::EnsurePhysicsWorld() {
    if (!physics_world) {
        physics_world = std::make_unique<b2World>(b2Vec2(0.0f, 9.8f));
        physics_world->SetContactListener(&g_physics_contact_listener);
    }

    return physics_world.get();
}

void Engine::SyncRigidbodyActors() {
    for (Actor& actor : _db.actors) {
        if (actor.destroyed) {
            continue;
        }

        const auto rigidbodyComponents = actor.component_instances_by_type.find("Rigidbody");
        if (rigidbodyComponents == actor.component_instances_by_type.end()) {
            continue;
        }

        for (Actor::ComponentInstance* component : rigidbodyComponents->second) {
            if (component == nullptr || component->removed || !component->rigidbody) {
                continue;
            }

            component->rigidbody->SyncActorTransform();
            break;
        }
    }
}

void Engine::StepPhysics() {
    if (!physics_world) {
        return;
    }

    physics_world->Step(1.0f / 60.0f, 8, 3);
    SyncRigidbodyActors();
}

void Engine::ClearComponentInstanceRefs() {
    components_with_on_update.clear();
    components_with_on_late_update.clear();
    actors_pending_cleanup.clear();
    actor_refs_by_name.clear();
    event_subscriptions.clear();
    pending_event_changes.clear();
    for (Actor& actor : _db.actors) {
        actor.lua_state = lua_state;
        actor.components.clear();
        actor.component_instances_by_type.clear();
    }
    pending_on_starts.clear();
    cleanup_removed_components_pending = false;
}

void Engine::RebuildActorComponentTypeIndex(Actor& actor) {
    actor.component_instances_by_type.clear();

    for (auto& [componentKey, component] : actor.components) {
        if (!component.removed) {
            (void)componentKey;
            actor.component_instances_by_type[component.type].push_back(&component);
        }
    }
}

void Engine::RebuildActorNameIndex() {
    actor_refs_by_name.clear();
    actor_refs_by_name.reserve(_db.actors.size());

    for (Actor& actor : _db.actors) {
        if (!actor.destroyed) {
            actor_refs_by_name[actor.actor_name].push_back(&actor);
        }
    }
}

void Engine::IndexActorByName(Actor& actor) {
    if (!actor.destroyed) {
        actor_refs_by_name[actor.actor_name].push_back(&actor);
    }
}

void Engine::RemoveActorFromNameIndex(Actor& actor) {
    const auto found = actor_refs_by_name.find(actor.actor_name);
    if (found == actor_refs_by_name.end()) {
        return;
    }

    std::vector<Actor*>& actorRefs = found->second;
    actorRefs.erase(std::remove(actorRefs.begin(), actorRefs.end(), &actor), actorRefs.end());
    if (actorRefs.empty()) {
        actor_refs_by_name.erase(found);
    }
}

std::shared_ptr<luabridge::LuaRef> Engine::LoadComponentType(const std::string& component_type) {
    const auto found = component_type_refs.find(component_type);
    if (found != component_type_refs.end()) {
        return found->second;
    }

    const fs::path componentPath = ResourcePaths::ResolveExistingGameFile(
        fs::path("component_types") / fs::path(component_type + ".lua"));
    if (componentPath.empty()) {
        std::cout << "error: failed to locate component " << component_type;
        std::exit(0);
    }

    try {
        luabridge::LoadLuaFile(lua_state, componentPath.string());
    }
    catch (const luabridge::LuaException&) {
        std::cout << "problem with lua file " << componentPath.stem().string();
        std::exit(0);
    }

    luabridge::LuaRef componentType = luabridge::getGlobal(lua_state, component_type.c_str());
    if (!componentType.isTable()) {
        std::cout << "problem with lua file " << componentPath.stem().string();
        std::exit(0);
    }

    auto loadedComponentType = std::make_shared<luabridge::LuaRef>(componentType);
    component_type_refs.emplace(component_type, loadedComponentType);
    return loadedComponentType;
}

Actor::ComponentInstance Engine::CreateComponentInstance(
    Actor& actor,
    const std::string& component_key,
    const Actor::ComponentDefinition& definition) {
    Actor::ComponentInstance instance;
    instance.key = component_key;
    instance.type = definition.type;
    instance.owner = &actor;

    if (IsRigidbodyComponentType(definition.type)) {
        EnsurePhysicsWorld();

        instance.rigidbody = std::make_shared<Rigidbody>();
        instance.rigidbody->key = component_key;
        instance.rigidbody->actor = &actor;
        instance.rigidbody->enabled = true;
        instance.lua_component_ref = luabridge::LuaRef(lua_state, instance.rigidbody.get());
    }
    else if (IsParticleSystemComponentType(definition.type)) {
        instance.particle_system = std::make_shared<ParticleSystem>();
        instance.particle_system->key = component_key;
        instance.particle_system->actor = &actor;
        instance.particle_system->enabled = true;
        instance.lua_component_ref = luabridge::LuaRef(lua_state, instance.particle_system.get());
    }
    else {
        const std::shared_ptr<luabridge::LuaRef> baseRef = LoadComponentType(definition.type);
        instance.lua_component_ref = luabridge::newTable(lua_state);
        luabridge::EstablishInheritance(*instance.lua_component_ref, *baseRef);
        (*instance.lua_component_ref)["key"] = component_key;
        (*instance.lua_component_ref)["actor"] = &actor;
        (*instance.lua_component_ref)["enabled"] = true;
    }

    for (const auto& [propertyName, propertyValue] : definition.properties) {
        ApplyComponentProperty(*instance.lua_component_ref, propertyName, propertyValue);
    }

    if (instance.rigidbody) {
        instance.rigidbody->SyncActorTransform();
    }

    CacheComponentLifecycleFunctions(instance);

    instance.enabled = IsComponentEnabled(instance);
    return instance;
}

luabridge::LuaRef Engine::AddRuntimeComponent(Actor& actor, const std::string& type_name) {
    if (lua_state == nullptr || actor.destroyed) {
        return MakeNilLuaRef(lua_state);
    }

    const std::string componentKey = "r" + std::to_string(runtime_added_component_count);
    ++runtime_added_component_count;

    Actor::ComponentDefinition definition;
    definition.type = type_name;
    actor.component_definitions[componentKey] = definition;

    auto inserted = actor.components.emplace(
        componentKey,
        CreateComponentInstance(actor, componentKey, actor.component_definitions[componentKey]));
    RebuildActorComponentTypeIndex(actor);
    RegisterLifecycleComponent(inserted.first->second);
    pending_on_starts.push_back(PendingOnStart{ actor.id, componentKey });

    return *inserted.first->second.lua_component_ref;
}

void Engine::RemoveRuntimeComponent(Actor& actor, const luabridge::LuaRef& component_ref) {
    if (actor.destroyed || component_ref.isNil()) {
        return;
    }

    for (auto& [componentKey, component] : actor.components) {
        if (component.removed || !component.lua_component_ref.has_value()) {
            continue;
        }

        if (component.lua_component_ref->rawequal(component_ref)) {
            component.removed = true;
            SetComponentEnabled(component, false);
            actor.component_definitions.erase(componentKey);
            RebuildActorComponentTypeIndex(actor);
            actors_pending_cleanup.push_back(&actor);
            cleanup_removed_components_pending = true;
            return;
        }
    }
}

Actor* Engine::InstantiateRuntimeActor(const std::string& actor_template_name) {
    if (lua_state == nullptr) {
        return nullptr;
    }

    _db.actors.push_back(_db._sceneLoader.loadActorFromTemplate(actor_template_name));
    Actor& actor = _db.actors.back();
    const int actorIndex = static_cast<int>(_db.actors.size()) - 1;
    actor.lua_state = lua_state;

    for (const auto& [componentKey, definition] : actor.component_definitions) {
        auto inserted = actor.components.emplace(
            componentKey,
            CreateComponentInstance(actor, componentKey, definition));
        RegisterLifecycleComponent(inserted.first->second);
    }
    RebuildActorComponentTypeIndex(actor);

    _db.addActorToRuntimeStructures(actorIndex);
    IndexActorByName(actor);
    QueueActorOnStarts(actor);
    PopulateActorRenderCaches(actor, max_actor_half_width_units, max_actor_half_height_units);

    if (player == nullptr && actor.actor_name == "player") {
        player = &actor;
        _db.playerIndex = actorIndex;
    }

    return &actor;
}

void Engine::DestroyRuntimeActor(Actor& actor) {
    if (actor.destroyed) {
        return;
    }

    const int actorIndex = _db.getActorIndexById(actor.id);
    actor.destroyed = true;
    for (auto& [componentKey, component] : actor.components) {
        (void)componentKey;
        component.removed = true;
        SetComponentEnabled(component, false);
    }
    actors_pending_cleanup.push_back(&actor);
    cleanup_removed_components_pending = true;

    _db.removeActorFromRuntimeStructures(actorIndex);
    RemoveActorFromNameIndex(actor);

    if (player != nullptr && player->id == actor.id) {
        player = nullptr;
        _db.playerIndex = -1;
    }
}

void Engine::QueueActorOnStarts(const Actor& actor) {
    for (const auto& [componentKey, component] : actor.components) {
        if (!component.removed) {
            pending_on_starts.push_back(PendingOnStart{ actor.id, componentKey });
        }
    }
}

void Engine::RegisterLifecycleComponent(Actor::ComponentInstance& component) {
    if (component.has_on_update && component.on_update_function.has_value()) {
        InsertLifecycleComponentSorted(components_with_on_update, component);
    }

    if (component.has_on_late_update && component.on_late_update_function.has_value()) {
        InsertLifecycleComponentSorted(components_with_on_late_update, component);
    }
}

void Engine::RebuildLifecycleComponentLists() {
    components_with_on_update.clear();
    components_with_on_late_update.clear();

    for (Actor& actor : _db.actors) {
        if (actor.destroyed) {
            continue;
        }

        for (auto& [componentKey, component] : actor.components) {
            (void)componentKey;
            if (!component.removed) {
                RegisterLifecycleComponent(component);
            }
        }
    }
}

void Engine::LoadCurrentSceneComponents() {
    ClearComponentInstanceRefs();

    if (lua_state == nullptr) {
        return;
    }

    if (current_scene_name.empty()) {
        current_scene_name = NormalizeSceneName(_db._sceneLoader.initialScene);
    }

    for (Actor& actor : _db.actors) {
        actor.lua_state = lua_state;
        for (const auto& [componentKey, definition] : actor.component_definitions) {
            auto inserted = actor.components.emplace(
                componentKey,
                CreateComponentInstance(actor, componentKey, definition));
            RegisterLifecycleComponent(inserted.first->second);
        }
        RebuildActorComponentTypeIndex(actor);

        QueueActorOnStarts(actor);
    }

    RebuildActorNameIndex();
}

void Engine::ProcessPendingSceneLoad() {
    if (!pending_scene_load_name.has_value()) {
        if (current_scene_name.empty()) {
            current_scene_name = NormalizeSceneName(_db._sceneLoader.initialScene);
        }
        return;
    }

    const std::string sceneName = *pending_scene_load_name;
    pending_scene_load_name.reset();

    for (Actor& actor : _db.actors) {
        actor.colliding_actor_indices_this_frame.clear();

        if (actor.destroyed || actor.dont_destroy) {
            continue;
        }

        for (auto& [componentKey, component] : actor.components) {
            (void)componentKey;
            CallComponentOnDestroy(component);
        }

        actor.destroyed = true;
        actor.components.clear();
        actor.component_definitions.clear();
        actor.component_instances_by_type.clear();
    }

    const std::size_t firstNewActorIndex = _db.actors.size();
    std::deque<Actor> loadedActors = _db._sceneLoader.loadScene(sceneName);
    for (Actor& actor : loadedActors) {
        _db.actors.push_back(std::move(actor));
    }

    components_with_on_update.clear();
    components_with_on_late_update.clear();
    pending_on_starts.clear();
    actors_pending_cleanup.clear();
    cleanup_removed_components_pending = false;

    _db.updating_actor_indices.clear();
    _db.updating_actor_indices.reserve(_db.actors.size());
    _db.actor_index_by_id.clear();
    _db.actor_index_by_id.reserve(_db.actors.size());
    _db.playerIndex = -1;

    for (int actorIndex = 0; actorIndex < static_cast<int>(_db.actors.size()); ++actorIndex) {
        Actor& actor = _db.actors[actorIndex];
        if (actor.destroyed) {
            continue;
        }

        actor.lua_state = lua_state;
        actor.colliding_actor_indices_this_frame.clear();
        _db.actor_index_by_id[actor.id] = actorIndex;

        if (_db.playerIndex == -1 && actor.actor_name == "player") {
            _db.playerIndex = actorIndex;
        }
        if (actor.actor_name == "player" || actor.velocity != glm::vec2(0.0f, 0.0f)) {
            _db.updating_actor_indices.push_back(actorIndex);
        }

        if (static_cast<std::size_t>(actorIndex) < firstNewActorIndex) {
            for (auto& [componentKey, component] : actor.components) {
                if (component.removed) {
                    continue;
                }

                component.owner = &actor;
                if (component.lua_component_ref.has_value()) {
                    (*component.lua_component_ref)["actor"] = &actor;
                }

                RegisterLifecycleComponent(component);
                if (!component.on_start_called) {
                    pending_on_starts.push_back(PendingOnStart{ actor.id, componentKey });
                }
            }
            RebuildActorComponentTypeIndex(actor);
            continue;
        }

        for (const auto& [componentKey, definition] : actor.component_definitions) {
            auto inserted = actor.components.emplace(
                componentKey,
                CreateComponentInstance(actor, componentKey, definition));
            RegisterLifecycleComponent(inserted.first->second);
        }
        RebuildActorComponentTypeIndex(actor);

        QueueActorOnStarts(actor);
    }

    _db.rebuildOccupancy();
    PrimeActorRenderCaches();
    player = (_db.playerIndex >= 0 && _db.playerIndex < static_cast<int>(_db.actors.size()))
        ? &_db.actors[_db.playerIndex]
        : nullptr;
    RebuildActorNameIndex();
    score_awarded_ids.clear();
    nearby_dialogue_sfx_played_ids.clear();
    current_scene_name = sceneName;
    PruneEventSubscriptions();
}

void Engine::CallComponentOnStart(Actor::ComponentInstance& component) {
    if (lua_state == nullptr ||
        !component.lua_component_ref.has_value() ||
        !component.has_on_start ||
        !component.on_start_function.has_value() ||
        component.owner == nullptr ||
        component.owner->destroyed) {
        return;
    }

    CallComponentLifecycleFunction(*component.owner, component, *component.on_start_function);
}

void Engine::CallComponentOnDestroy(Actor::ComponentInstance& component) {
    if (component.on_destroy_called) {
        return;
    }

    component.on_destroy_called = true;
    if (lua_state == nullptr ||
        !component.lua_component_ref.has_value() ||
        !component.has_on_destroy ||
        !component.on_destroy_function.has_value() ||
        component.owner == nullptr) {
        return;
    }

    CallComponentLifecycleFunction(*component.owner, component, *component.on_destroy_function);
}

void Engine::RunPendingOnStarts() {
    std::vector<PendingOnStart> componentsToStart;
    componentsToStart.swap(pending_on_starts);

    for (const PendingOnStart& pendingComponent : componentsToStart) {
        Actor* actor = _db.getActorById(pendingComponent.actor_id);
        if (actor == nullptr) {
            continue;
        }

        const auto componentIt = actor->components.find(pendingComponent.component_key);
        if (componentIt == actor->components.end()) {
            continue;
        }

        Actor::ComponentInstance& component = componentIt->second;
        if (component.on_start_called || component.removed) {
            continue;
        }

        if (!IsComponentEnabled(component)) {
            pending_on_starts.push_back(pendingComponent);
            continue;
        }

        component.on_start_called = true;
        CallComponentOnStart(component);
    }
}

void Engine::CallComponentLifecycleFunction(
    const Actor& actor,
    Actor::ComponentInstance& component,
    const luabridge::LuaRef& lifecycle_function) {
    if (lua_state == nullptr ||
        !component.lua_component_ref.has_value()) {
        return;
    }

    try {
        lifecycle_function.push(lua_state);
        component.lua_component_ref->push(lua_state);
        luabridge::LuaException::pcall(lua_state, 1, 0);
    }
    catch (const luabridge::LuaException& exception) {
        ReportLuaException(actor.actor_name, exception);
    }
}

void Engine::RunComponentLifecyclePhase(bool late_update) {
    if (lua_state == nullptr) {
        return;
    }

    std::vector<Actor::ComponentInstance*>& lifecycleComponents =
        late_update ? components_with_on_late_update : components_with_on_update;
    const std::size_t componentCountAtPhaseStart = lifecycleComponents.size();
    for (std::size_t componentIndex = 0; componentIndex < componentCountAtPhaseStart; ++componentIndex) {
        Actor::ComponentInstance* component = lifecycleComponents[componentIndex];
        if (component == nullptr ||
            component->owner == nullptr ||
            component->owner->destroyed ||
            component->removed ||
            !component->on_start_called ||
            !IsComponentEnabled(*component)) {
            continue;
        }

        const std::optional<luabridge::LuaRef>& lifecycleFunction =
            late_update ? component->on_late_update_function : component->on_update_function;
        if (!lifecycleFunction.has_value()) {
            continue;
        }

        CallComponentLifecycleFunction(*component->owner, *component, *lifecycleFunction);
    }
}

void Engine::RunOnDestroyPhase() {
    if (actors_pending_cleanup.empty()) {
        return;
    }

    std::vector<Actor*> actorsToDestroy = actors_pending_cleanup;
    std::sort(actorsToDestroy.begin(), actorsToDestroy.end());
    actorsToDestroy.erase(std::unique(actorsToDestroy.begin(), actorsToDestroy.end()), actorsToDestroy.end());

    for (Actor* actor : actorsToDestroy) {
        if (actor == nullptr) {
            continue;
        }

        for (auto& [componentKey, component] : actor->components) {
            (void)componentKey;
            if (actor->destroyed || component.removed) {
                CallComponentOnDestroy(component);
            }
        }
    }
}

void Engine::RunPendingSceneLoadOnDestroyPhase() {
    if (!pending_scene_load_name.has_value()) {
        return;
    }

    for (Actor& actor : _db.actors) {
        if (actor.destroyed || actor.dont_destroy) {
            continue;
        }

        for (auto& [componentKey, component] : actor.components) {
            (void)componentKey;
            CallComponentOnDestroy(component);
        }
    }
}

void Engine::PublishEvent(const std::string& event_type, const luabridge::LuaRef& event_object) {
    if (lua_state == nullptr) {
        return;
    }

    PruneEventSubscriptions();

    const auto found = event_subscriptions.find(event_type);
    if (found == event_subscriptions.end()) {
        return;
    }

    const std::size_t subscription_count = found->second.size();
    for (std::size_t i = 0; i < subscription_count; ++i) {
        EventSubscription& subscription = found->second[i];
        if (!IsEventSubscriptionAlive(subscription) ||
            subscription.component_ref == nullptr ||
            subscription.function_ref == nullptr) {
            continue;
        }

        try {
            subscription.function_ref->push(lua_state);
            subscription.component_ref->push(lua_state);
            event_object.push(lua_state);
            luabridge::LuaException::pcall(lua_state, 2, 0);
        }
        catch (const luabridge::LuaException& exception) {
            const std::string actor_name =
                subscription.actor != nullptr ? subscription.actor->actor_name : "Event";
            ReportLuaException(actor_name, exception);
        }
    }
}

void Engine::QueueEventSubscriptionChange(
    PendingEventChangeType type,
    const std::string& event_type,
    const luabridge::LuaRef& component_ref,
    const luabridge::LuaRef& function_ref) {
    if (lua_state == nullptr ||
        event_type.empty() ||
        component_ref.isNil() ||
        !function_ref.isFunction()) {
        return;
    }

    Actor* actor = nullptr;
    std::string component_key;
    if (!TryResolveEventComponentIdentity(component_ref, actor, component_key)) {
        return;
    }

    PendingEventChange change;
    change.type = type;
    change.event_type = event_type;
    change.actor = actor;
    change.component_key = component_key;
    change.component_ref = std::make_shared<luabridge::LuaRef>(component_ref);
    change.function_ref = std::make_shared<luabridge::LuaRef>(function_ref);
    pending_event_changes.push_back(std::move(change));
}

void Engine::ApplyPendingEventSubscriptionChanges() {
    if (pending_event_changes.empty()) {
        PruneEventSubscriptions();
        return;
    }

    std::vector<PendingEventChange> pending_changes;
    pending_changes.swap(pending_event_changes);

    for (const PendingEventChange& change : pending_changes) {
        if (change.event_type.empty() ||
            change.component_ref == nullptr ||
            change.function_ref == nullptr) {
            continue;
        }

        std::vector<EventSubscription>& subscriptions = event_subscriptions[change.event_type];
        if (change.type == PendingEventChangeType::Subscribe) {
            EventSubscription subscription;
            subscription.actor = change.actor;
            subscription.component_key = change.component_key;
            subscription.component_ref = change.component_ref;
            subscription.function_ref = change.function_ref;
            subscriptions.push_back(std::move(subscription));
            continue;
        }

        subscriptions.erase(
            std::remove_if(
                subscriptions.begin(),
                subscriptions.end(),
                [&change](const EventSubscription& subscription) {
                    return EventSubscriptionMatches(subscription, change);
                }),
            subscriptions.end());

        if (subscriptions.empty()) {
            event_subscriptions.erase(change.event_type);
        }
    }

    PruneEventSubscriptions();
}

void Engine::PruneEventSubscriptions() {
    for (auto it = event_subscriptions.begin(); it != event_subscriptions.end();) {
        std::vector<EventSubscription>& subscriptions = it->second;
        subscriptions.erase(
            std::remove_if(
                subscriptions.begin(),
                subscriptions.end(),
                [](const EventSubscription& subscription) {
                    return !IsEventSubscriptionAlive(subscription);
                }),
            subscriptions.end());

        if (subscriptions.empty()) {
            it = event_subscriptions.erase(it);
            continue;
        }

        ++it;
    }
}

void Engine::CleanupRemovedComponents() {
    std::vector<Actor*> actorsToClean;
    actorsToClean.swap(actors_pending_cleanup);
    std::sort(actorsToClean.begin(), actorsToClean.end());
    actorsToClean.erase(std::unique(actorsToClean.begin(), actorsToClean.end()), actorsToClean.end());

    for (Actor* actor : actorsToClean) {
        if (actor == nullptr) {
            continue;
        }

        if (actor->destroyed) {
            actor->components.clear();
            actor->component_definitions.clear();
            actor->component_instances_by_type.clear();
            continue;
        }

        bool removedAnyComponents = false;
        for (auto componentIt = actor->components.begin(); componentIt != actor->components.end();) {
            if (!componentIt->second.removed) {
                ++componentIt;
                continue;
            }

            removedAnyComponents = true;
            actor->component_definitions.erase(componentIt->first);
            componentIt = actor->components.erase(componentIt);
        }

        if (removedAnyComponents) {
            RebuildActorComponentTypeIndex(*actor);
        }
    }

    auto pruneLifecycleList = [](std::vector<Actor::ComponentInstance*>& lifecycleComponents) {
        lifecycleComponents.erase(
            std::remove_if(
                lifecycleComponents.begin(),
                lifecycleComponents.end(),
                [](const Actor::ComponentInstance* component) {
                    return component == nullptr ||
                        component->owner == nullptr ||
                        component->owner->destroyed ||
                        component->removed;
                }),
            lifecycleComponents.end());
        };

    pruneLifecycleList(components_with_on_update);
    pruneLifecycleList(components_with_on_late_update);
    cleanup_removed_components_pending = false;
}

std::vector<Engine::DialogueLine> Engine::collectContactDialogueLines() const {
    if (player == nullptr) {
        return {};
    }

    const int playerIndex = ResolvePlayerIndex(_db.actors, player, _db.playerIndex);
    std::unordered_set<int> candidateIndices = player->colliding_actor_indices_this_frame;

    // Contact dialogue should also fire for actors already overlapping the player
    // at the end of the frame, including frame 0 before anyone has moved.
    const std::optional<Bounds> playerCollider = GetActorBounds(*player, player->position, false);
    if (playerCollider.has_value()) {
        std::vector<int> possibleOverlaps;
        _db.getPotentialColliderIndices(playerIndex, player->position, possibleOverlaps);
        for (int i : possibleOverlaps) {
            const Actor& actor = _db.actors[i];
            if (actor.destroyed) {
                continue;
            }

            if (actor.contact_dialogue.empty()) {
                continue;
            }

            const std::optional<Bounds> actorCollider = GetActorBounds(actor, actor.position, false);
            if (actorCollider.has_value() && BoundsOverlap(*playerCollider, *actorCollider)) {
                candidateIndices.insert(i);
            }
        }
    }

    std::vector<const Actor*> candidates;
    candidates.reserve(candidateIndices.size());
    for (int actorIndex : candidateIndices) {
        if (actorIndex < 0 || actorIndex >= static_cast<int>(_db.actors.size()) || actorIndex == playerIndex) {
            continue;
        }

        const Actor& actor = _db.actors[actorIndex];
        if (actor.destroyed) {
            continue;
        }

        if (!actor.contact_dialogue.empty()) {
            candidates.push_back(&actor);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Actor* lhs, const Actor* rhs) { return lhs->id < rhs->id; });

    std::vector<DialogueLine> lines;
    lines.reserve(candidates.size());
    for (const Actor* actor : candidates) {
        lines.push_back({ actor->id, actor->contact_dialogue });
    }

    return lines;
}

std::vector<Engine::DialogueLine> Engine::collectNearbyDialogueLines() const {
    std::vector<const Actor*> candidates;
    if (player == nullptr) {
        return {};
    }

    const int playerIndex = ResolvePlayerIndex(_db.actors, player, _db.playerIndex);
    const std::optional<Bounds> playerTrigger = GetActorBounds(*player, player->position, true);
    if (!playerTrigger.has_value()) {
        return {};
    }

    std::vector<int> possibleOverlaps;
    _db.getPotentialTriggerIndices(playerIndex, player->position, possibleOverlaps);
    candidates.reserve(possibleOverlaps.size());
    for (int i : possibleOverlaps) {
        const Actor& actor = _db.actors[i];
        if (actor.destroyed) {
            continue;
        }

        if (actor.nearby_dialogue.empty()) {
            continue;
        }

        const std::optional<Bounds> actorTrigger = GetActorBounds(actor, actor.position, true);
        if (!actorTrigger.has_value()) {
            continue;
        }

        if (BoundsOverlap(*playerTrigger, *actorTrigger)) {
            candidates.push_back(&actor);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Actor* lhs, const Actor* rhs) { return lhs->id < rhs->id; });

    std::vector<DialogueLine> lines;
    lines.reserve(candidates.size());
    for (const Actor* actor : candidates) {
        lines.push_back({ actor->id, actor->nearby_dialogue });
    }

    return lines;
}

void Engine::PrimeActorRenderCaches() {
    max_actor_half_width_units = 0.0f;
    max_actor_half_height_units = 0.0f;

    for (Actor& actor : _db.actors) {
        if (actor.destroyed) {
            continue;
        }

        PopulateActorRenderCaches(actor, max_actor_half_width_units, max_actor_half_height_units);
    }
}


void Engine::ProcessInput(bool& windowRunning, ImGuiLayer* imguiLayer) {
    SDL_Event e;
    advanceIntroRequested = false;
    Input::SetCaptureState(false, false);

    while (Helper::SDL_PollEvent(&e)) // empty / consume all events in the event queue
    {
        if (imguiLayer != nullptr) {
            imguiLayer->ProcessEvent(e);
        }

        const bool mouseCaptured = imguiLayer != nullptr && imguiLayer->WantsMouseCapture();
        if (e.type == SDL_QUIT) {
            windowRunning = false;
        }
        else if (!mouseCaptured &&
            e.type == SDL_MOUSEBUTTONDOWN &&
            e.button.button == SDL_BUTTON_LEFT) {
            advanceIntroRequested = true;
        }

        Input::ProcessEvent(e);
    }

    const bool keyboardCaptured = imguiLayer != nullptr && imguiLayer->WantsKeyboardCapture();
    if (!keyboardCaptured &&
        (Input::GetKeyDown(SDL_SCANCODE_SPACE) || Input::GetKeyDown(SDL_SCANCODE_RETURN))) {
        advanceIntroRequested = true;
    }

    if (HasActiveIntro() && advanceIntroRequested) {
        ++currentIntroStep;
        const std::size_t introImageCount = _db.intro_images.size();
        const std::size_t introTextCount = _db.intro_text.size();
        const std::size_t introLength = std::max(introImageCount, introTextCount);

        if (currentIntroStep >= introLength) {
            introSequenceComplete = true;
            if (introBgmWasPlaying) {
                AudioDB::HaltChannel(0);
            }
        }
    }
}

bool Engine::TryMoveActor(int actorIndex, const glm::vec2& delta) {
    if (actorIndex < 0 || actorIndex >= static_cast<int>(_db.actors.size())) {
        return false;
    }

    Actor& actor = _db.actors[actorIndex];
    if (actor.destroyed) {
        return false;
    }

    const glm::vec2 destination = actor.position + delta;
    bool blocked = false;

    const std::optional<Bounds> actorCollider = GetActorBounds(actor, destination, false);
    if (actorCollider.has_value()) {
        std::vector<int> possibleCollisions;
        _db.getPotentialColliderIndices(actorIndex, destination, possibleCollisions);
        for (int otherIndex : possibleCollisions) {
            Actor& otherActor = _db.actors[otherIndex];
            if (otherActor.destroyed) {
                continue;
            }

            const std::optional<Bounds> otherCollider = GetActorBounds(otherActor, otherActor.position, false);
            if (!otherCollider.has_value()) {
                continue;
            }

            if (BoundsOverlap(*actorCollider, *otherCollider)) {
                RecordCollision(actor, actorIndex, otherActor, otherIndex);
                blocked = true;
            }
        }
    }

    if (blocked) {
        return false;
    }

    const glm::vec2 oldPosition = actor.position;
    actor.position = destination;
    _db.moveActorInOccupancy(actorIndex, oldPosition, destination);
    return true;
}

void Engine::UpdateGameplayActors() {
    RunComponentLifecyclePhase(false);
    RunComponentLifecyclePhase(true);
    RunPendingSceneLoadOnDestroyPhase();
    RunOnDestroyPhase();
    if (cleanup_removed_components_pending) {
        CleanupRemovedComponents();
    }
    ApplyPendingEventSubscriptionChanges();
    StepPhysics();
}

void Engine::TriggerGameOver(bool won) {
    const std::string& imageName = won ? _db.game_over_good_image : _db.game_over_bad_image;
    if (imageName.empty()) {
        std::exit(0);
    }

    gameOverActive = true;
    gameOverWon = won;
    AudioDB::HaltChannel(0);

    if (won) {
        if (!_db.game_over_good_audio.empty()) {
            AudioDB::PlayChannel(0, _db.game_over_good_audio, 0);
        }
        return;
    }

    if (!_db.game_over_bad_audio.empty()) {
        AudioDB::PlayChannel(0, _db.game_over_bad_audio, 0);
    }
}

void Engine::RenderGameOver(Renderer& renderer) const {
    if (!gameOverActive) {
        return;
    }

    const std::string& imageName = gameOverWon ? _db.game_over_good_image : _db.game_over_bad_image;
    if (imageName.empty()) {
        return;
    }

    SDL_Texture* texture = ImageDB::GetImage(imageName);
    SDL_FRect dstRect = {
        0.0f,
        0.0f,
        static_cast<float>(_db.x_resolution),
        static_cast<float>(_db.y_resolution)
    };
    Helper::SDL_RenderCopy(renderer.GetSDLRenderer(), texture, nullptr, &dstRect);
}

bool Engine::HasActiveIntro() const {
    return !_db.intro_images.empty() && !introSequenceComplete;
}

std::size_t Engine::GetCurrentIntroImageIndex() const {
    if (_db.intro_images.empty()) {
        return 0;
    }

    return std::min(currentIntroStep, _db.intro_images.size() - 1);
}

std::size_t Engine::GetCurrentIntroTextIndex() const {
    if (_db.intro_text.empty()) {
        return 0;
    }

    return std::min(currentIntroStep, _db.intro_text.size() - 1);
}

void Engine::RenderIntro(Renderer& renderer) {
    if (!HasActiveIntro()) {
        return;
    }

    SDL_Texture* texture = ImageDB::GetImage(_db.intro_images[GetCurrentIntroImageIndex()]);
    if (texture == nullptr) {
        return;
    }

    SDL_FRect dstRect = {
        0.0f,
        0.0f,
        static_cast<float>(_db.x_resolution),
        static_cast<float>(_db.y_resolution)
    };
    Helper::SDL_RenderCopy(renderer.GetSDLRenderer(), texture, nullptr, &dstRect);

    if (!_db.intro_text.empty()) {
        TextDB::DrawText(
            _db.intro_text[GetCurrentIntroTextIndex()],
            25.0f,
            static_cast<float>(_db.y_resolution - 50));
    }
}

void Engine::StartGameplayAudioIfNeeded() {
    if (gameplayAudioStarted || _db.gameplay_audio.empty()) {
        return;
    }

    AudioDB::PlayChannel(0, _db.gameplay_audio, -1);
    gameplayAudioStarted = true;
}

bool Engine::ProcessDialogueCommands(
    const std::vector<DialogueLine>& lines,
    std::vector<std::string>& dialogueToRender,
    bool renderDialogueText) {
    bool winTriggered = false;
    bool loseTriggered = false;
    std::string proceedScene;
    bool hasProceed = false;
    const int currentFrame = Helper::GetFrameNumber();

    for (const auto& line : lines) {
        const Actor* sourceActor = _db.getActorById(line.actorId);
        if (renderDialogueText &&
            sourceActor != nullptr &&
            !sourceActor->nearby_dialogue_sfx.empty() &&
            nearby_dialogue_sfx_played_ids.insert(line.actorId).second) {
            AudioDB::PlayChannel(GameplaySfxChannel(currentFrame), sourceActor->nearby_dialogue_sfx, 0);
        }

        std::string parsedScene;
        if (!hasProceed && extractProceedScene(line.text, parsedScene)) {
            hasProceed = true;
            proceedScene = parsedScene;
            continue;
        }

        if (containsCmd(line.text, "you win")) {
            winTriggered = true;
            if (player != nullptr) {
                player->gameOverMessage = 1;
            }
            continue;
        }
        else if (containsCmd(line.text, "game over")) {
            if (currentFrame >= lastDamageFrame + 180) {
                loseTriggered = true;
                if (player != nullptr) {
                    player->gameOverMessage = 0;
                }
            }
            continue;
        }

        if (renderDialogueText) {
            dialogueToRender.push_back(line.text);
        }

        if (containsCmd(line.text, "health down")) {
            if (player != nullptr && currentFrame >= lastDamageFrame + 180) {
                player->health -= 1;
                lastDamageFrame = currentFrame;
                player->damage_visual_until_frame = currentFrame + 30;
                if (!player->damage_sfx.empty()) {
                    AudioDB::PlayChannel(GameplaySfxChannel(currentFrame), player->damage_sfx, 0);
                }

                Actor* attackingActor = _db.getActorById(line.actorId);
                if (attackingActor != nullptr && attackingActor->id != player->id) {
                    attackingActor->attack_visual_until_frame = currentFrame + 30;
                }
            }
        }
        else if (containsCmd(line.text, "score up")) {
            if (player != nullptr && score_awarded_ids.insert(line.actorId).second) {
                player->score += 1;
                if (!_db.score_sfx.empty()) {
                    AudioDB::PlayChannel(1, _db.score_sfx, 0);
                }
            }
        }
    }

    if (winTriggered) {
        TriggerGameOver(true);
        return false;
    }

    if (loseTriggered) {
        TriggerGameOver(false);
        return false;
    }

    if (hasProceed) {
        int playerHealth = 0;
        int playerScore = 0;
        if (player != nullptr) {
            playerHealth = player->health;
            playerScore = player->score;
        }

        ClearComponentInstanceRefs();
        _db.loadSceneOrExit(proceedScene);
        LoadCurrentSceneComponents();
        score_awarded_ids.clear();
        nearby_dialogue_sfx_played_ids.clear();
        PrimeActorRenderCaches();
        if (_db.playerIndex >= 0 && _db.playerIndex < static_cast<int>(_db.actors.size())) {
            player = &_db.actors[_db.playerIndex];
            player->health = playerHealth;
            player->score = playerScore;
        }
        else {
            player = nullptr;
        }

        return true;
    }

    return false;
}

void Engine::RenderDialogue(const std::vector<std::string>& dialogueToRender) {
    const int messageCount = static_cast<int>(dialogueToRender.size());
    for (int i = 0; i < messageCount; ++i) {
        const float y = static_cast<float>(_db.y_resolution - 50 - (messageCount - 1 - i) * 50);
        TextDB::DrawText(dialogueToRender[i], 25.0f, y);
    }
}

bool Engine::RenderGameplay(Renderer& renderer) {
    if (HasActiveIntro()) {
        return true;
    }

    if (gameOverActive) {
        RenderGameOver(renderer);
        return true;
    }

    const glm::vec2 cameraPosition = currentCameraPosition;

    if (max_actor_half_width_units > 0.0f || max_actor_half_height_units > 0.0f) {
        const float screenCenterX = static_cast<float>(_db.x_resolution) * 0.5f;
        const float screenCenterY = static_cast<float>(_db.y_resolution) * 0.5f;
        const float zoomFactor = _db.zoom_factor > 0.0f ? _db.zoom_factor : 1.0f;
        const int currentFrame = Helper::GetFrameNumber();
        const float logicalScreenWidth = static_cast<float>(_db.x_resolution) / zoomFactor;
        const float logicalScreenHeight = static_cast<float>(_db.y_resolution) / zoomFactor;
        const float halfViewportWidthUnits = logicalScreenWidth * 0.5f / 100.0f;
        const float halfViewportHeightUnits = logicalScreenHeight * 0.5f / 100.0f;
        const float bounceOffset =
            glm::abs(glm::sin(static_cast<float>(currentFrame) * 0.15f)) * 10.0f;

        SDL_RenderSetScale(renderer.GetSDLRenderer(), zoomFactor, zoomFactor);

        struct RenderEntry {
            const Actor* actor = nullptr;
            SDL_Texture* texture = nullptr;
            SDL_FRect dstRect{};
            SDL_FPoint center{};
            SDL_RendererFlip flip = SDL_FLIP_NONE;
        };

        std::vector<RenderEntry> renderActors;
        std::vector<int> visibleActorIndices;
        const int minCellX = static_cast<int>(std::floor(
            cameraPosition.x - halfViewportWidthUnits - max_actor_half_width_units)) - 1;
        const int maxCellX = static_cast<int>(std::ceil(
            cameraPosition.x + halfViewportWidthUnits + max_actor_half_width_units)) + 1;
        const int minCellY = static_cast<int>(std::floor(
            cameraPosition.y - halfViewportHeightUnits - max_actor_half_height_units)) - 1;
        const int maxCellY = static_cast<int>(std::ceil(
            cameraPosition.y + halfViewportHeightUnits + max_actor_half_height_units)) + 1;
        _db.getActorsInCellRect(minCellX, minCellY, maxCellX, maxCellY, visibleActorIndices);

        renderActors.reserve(visibleActorIndices.size());
        for (int actorIndex : visibleActorIndices) {
            const Actor& actor = _db.actors[actorIndex];
            if (actor.destroyed) {
                continue;
            }

            if (actor.view_image_cache.texture == nullptr) {
                continue;
            }

            const Actor::RenderImageCache& imageInfo = SelectActorImageCache(actor, player, currentFrame);
            const Actor::RenderImageCache& baseImageInfo = actor.view_image_cache;
            if (imageInfo.texture == nullptr) {
                continue;
            }

            const float pivotX = actor.view_pivot_offset_x.value_or(baseImageInfo.width * 0.5f);
            const float pivotY = actor.view_pivot_offset_y.value_or(baseImageInfo.height * 0.5f);

            const float scaleX = glm::abs(actor.transform_scale.x);
            const float scaleY = glm::abs(actor.transform_scale.y);
            const float scaledWidth = imageInfo.width * scaleX;
            const float scaledHeight = imageInfo.height * scaleY;
            const float scaledPivotX = pivotX * scaleX;
            const float scaledPivotY = pivotY * scaleY;

            const float screenOffsetX =
                (static_cast<float>(actor.position.x) - cameraPosition.x) * 100.0f;
            const float screenOffsetY =
                (static_cast<float>(actor.position.y) - cameraPosition.y) * 100.0f;

            SDL_FRect dstRect = {
                screenOffsetX + (screenCenterX / zoomFactor) - scaledPivotX,
                screenOffsetY + (screenCenterY / zoomFactor) - scaledPivotY,
                scaledWidth,
                scaledHeight
            };

            if (actor.movement_bounce_enabled &&
                (actor.move_intent.x != 0.0f || actor.move_intent.y != 0.0f)) {
                dstRect.y -= bounceOffset;
            }

            if (dstRect.x + dstRect.w < 0.0f ||
                dstRect.y + dstRect.h < 0.0f ||
                dstRect.x > logicalScreenWidth ||
                dstRect.y > logicalScreenHeight) {
                continue;
            }

            SDL_RendererFlip flip = SDL_FLIP_NONE;
            if (actor.transform_scale.x < 0.0f) {
                flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_HORIZONTAL);
            }
            if (actor.transform_scale.y < 0.0f) {
                flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_VERTICAL);
            }

            renderActors.push_back(RenderEntry{
                &actor,
                imageInfo.texture,
                dstRect,
                SDL_FPoint{ scaledPivotX, scaledPivotY },
                flip
                });
        }

        std::sort(renderActors.begin(), renderActors.end(),
            [](const RenderEntry& lhs, const RenderEntry& rhs) {
                const float lhsKey = GetSortKey(*lhs.actor);
                const float rhsKey = GetSortKey(*rhs.actor);
                if (lhsKey != rhsKey) {
                    return lhsKey < rhsKey;
                }
                return lhs.actor->id < rhs.actor->id;
            });

        for (const RenderEntry& entry : renderActors) {
            const Actor& actor = *entry.actor;
            Helper::SDL_RenderCopyEx(
                actor.id,
                actor.actor_name,
                renderer.GetSDLRenderer(),
                entry.texture,
                nullptr,
                &entry.dstRect,
                actor.transform_rotation_degrees,
                &entry.center,
                entry.flip);
        }

        SDL_RenderSetScale(renderer.GetSDLRenderer(), 1.0f, 1.0f);
    }

    ImageDB::RenderQueuedImages(_db, cameraPosition);

    return true;
}

void Engine::RenderHUD(Renderer& renderer) {
    if (player == nullptr) {
        return;
    }

    if (!_db.hp_image.empty()) {
        SDL_Texture* hpTexture = ImageDB::GetImage(_db.hp_image);
        float iconWidth = 0.0f;
        float iconHeight = 0.0f;
        Helper::SDL_QueryTexture(hpTexture, &iconWidth, &iconHeight);

        for (int i = 0; i < player->health; ++i) {
            SDL_FRect dstRect = {
                5.0f + static_cast<float>(i) * (iconWidth + 5.0f),
                25.0f,
                iconWidth,
                iconHeight
            };
            Helper::SDL_RenderCopy(renderer.GetSDLRenderer(), hpTexture, nullptr, &dstRect);
        }
    }

    TextDB::DrawText("score : " + std::to_string(player->score), 5.0f, 5.0f);
}
