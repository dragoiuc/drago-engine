#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>
#include "glm/glm.hpp"

struct lua_State;
class Rigidbody;
class ParticleSystem;

struct SDL_Texture;

struct Actor {
    struct ComponentProperty {
        enum class Type {
            Nil,
            Boolean,
            Integer,
            Number,
            String
        };

        Type type = Type::Nil;
        bool bool_value = false;
        long long integer_value = 0;
        double number_value = 0.0f;
        std::string string_value = "";
    };

    struct ComponentDefinition {
        std::string type = "";
        std::unordered_map<std::string, ComponentProperty> properties;
    };

    struct ComponentInstance {
        std::string key = "";
        std::string type = "";
        Actor* owner = nullptr;
        std::shared_ptr<Rigidbody> rigidbody;
        std::shared_ptr<ParticleSystem> particle_system;
        std::optional<luabridge::LuaRef> lua_component_ref;
        std::optional<luabridge::LuaRef> on_start_function;
        std::optional<luabridge::LuaRef> on_update_function;
        std::optional<luabridge::LuaRef> on_late_update_function;
        std::optional<luabridge::LuaRef> on_destroy_function;
        bool enabled = true;
        bool removed = false;
        bool has_on_start = false;
        bool has_on_update = false;
        bool has_on_late_update = false;
        bool has_on_destroy = false;
        bool on_start_called = false;
        bool on_destroy_called = false;
    };

    struct RenderImageCache {
        SDL_Texture* texture = nullptr;
        float width = 0.0f;
        float height = 0.0f;
    };

    std::string actor_name = "";
    std::string view_image = "";
    std::string view_image_back = "";
    std::string view_image_damage = "";
    std::string view_image_attack = "";
    std::string damage_sfx = "";
    std::string step_sfx = "";
    std::string nearby_dialogue_sfx = "";
    glm::vec2 position = glm::vec2(0.0f, 0.0f);
    glm::vec2 velocity = glm::vec2(0.0f, 0.0f);
    glm::vec2 transform_scale = glm::vec2(1.0f, 1.0f);
    float transform_rotation_degrees = 0.0f;
    std::optional<float> view_pivot_offset_x;
    std::optional<float> view_pivot_offset_y;
    std::optional<int> render_order;
    std::optional<float> box_collider_width;
    std::optional<float> box_collider_height;
    std::optional<float> box_trigger_width;
    std::optional<float> box_trigger_height;
    bool blocking = false;
    bool movement_bounce_enabled = false;
    std::string nearby_dialogue = "";
    std::string contact_dialogue = "";
    int health = 3;
    int score = 0;
    int gameOverMessage = 0;
    int id = -1;
    bool destroyed = false;
    bool dont_destroy = false;
    lua_State* lua_state = nullptr;
    std::map<std::string, ComponentDefinition> component_definitions;
    std::map<std::string, ComponentInstance> components;
    std::unordered_map<std::string, std::vector<ComponentInstance*>> component_instances_by_type;
    glm::vec2 move_intent = glm::vec2(0.0f, 0.0f);
    bool use_back_view = false;
    int damage_visual_until_frame = -1;
    int attack_visual_until_frame = -1;
    RenderImageCache view_image_cache;
    RenderImageCache view_image_back_cache;
    RenderImageCache view_image_damage_cache;
    RenderImageCache view_image_attack_cache;
    std::unordered_set<int> colliding_actor_indices_this_frame;

    std::string GetName() const;
    int GetID() const;
    luabridge::LuaRef AddComponent(const std::string& type_name);
    void RemoveComponent(const luabridge::LuaRef& component_ref);
    luabridge::LuaRef GetComponentByKey(const std::string& key) const;
    luabridge::LuaRef GetComponent(const std::string& type_name) const;
    luabridge::LuaRef GetComponents(const std::string& type_name) const;
};
