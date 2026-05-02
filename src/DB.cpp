#include "DB.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>

#include "EngineUtils.h"
#include "ResourcePaths.h"
#include "rapidjson/document.h"

namespace fs = std::filesystem;
static constexpr float COLLIDER_REGION_SIZE = 1.0f;
static constexpr float TRIGGER_REGION_SIZE = 4.0f;

static glm::ivec2 CellPositionFromWorldPosition(const glm::vec2& position) {
    return glm::ivec2(
        static_cast<int>(std::floor(position.x)),
        static_cast<int>(std::floor(position.y)));
}

struct SpatialRegionRange {
    int min_x;
    int min_y;
    int max_x;
    int max_y;
};

static uint64_t SpatialKey(int x, int y) {
    const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x));
    const uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(y));
    return (ux << 32) | uy;
}

static bool BuildActorBounds(
    const Actor& actor,
    const glm::vec2& position,
    bool trigger,
    float& left,
    float& top,
    float& right,
    float& bottom) {
    const std::optional<float>& width = trigger ? actor.box_trigger_width : actor.box_collider_width;
    const std::optional<float>& height = trigger ? actor.box_trigger_height : actor.box_collider_height;
    if (!width.has_value() || !height.has_value()) {
        return false;
    }

    const float halfWidth = *width * glm::abs(actor.transform_scale.x) * 0.5f;
    const float halfHeight = *height * glm::abs(actor.transform_scale.y) * 0.5f;
    left = position.x - halfWidth;
    top = position.y - halfHeight;
    right = position.x + halfWidth;
    bottom = position.y + halfHeight;
    return true;
}

static SpatialRegionRange BuildRegionRange(
    float left,
    float top,
    float right,
    float bottom,
    float regionSize) {
    return SpatialRegionRange{
        static_cast<int>(std::floor(left / regionSize)),
        static_cast<int>(std::floor(top / regionSize)),
        static_cast<int>(std::floor(right / regionSize)),
        static_cast<int>(std::floor(bottom / regionSize))
    };
}

static bool GetActorRegionRange(
    const Actor& actor,
    const glm::vec2& position,
    bool trigger,
    float regionSize,
    SpatialRegionRange& outRange) {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    if (!BuildActorBounds(actor, position, trigger, left, top, right, bottom)) {
        return false;
    }

    outRange = BuildRegionRange(left, top, right, bottom, regionSize);
    return true;
}

static void AddActorToRegions(
    std::unordered_map<uint64_t, std::vector<int>>& regions,
    int actorIndex,
    const SpatialRegionRange& range) {
    for (int y = range.min_y; y <= range.max_y; ++y) {
        for (int x = range.min_x; x <= range.max_x; ++x) {
            regions[SpatialKey(x, y)].push_back(actorIndex);
        }
    }
}

static void RemoveActorFromRegions(
    std::unordered_map<uint64_t, std::vector<int>>& regions,
    int actorIndex,
    const SpatialRegionRange& range) {
    for (int y = range.min_y; y <= range.max_y; ++y) {
        for (int x = range.min_x; x <= range.max_x; ++x) {
            const uint64_t key = SpatialKey(x, y);
            auto it = regions.find(key);
            if (it == regions.end()) {
                continue;
            }

            std::vector<int>& indices = it->second;
            indices.erase(std::remove(indices.begin(), indices.end(), actorIndex), indices.end());
            if (indices.empty()) {
                regions.erase(it);
            }
        }
    }
}

static bool RegionRangesEqual(const SpatialRegionRange& lhs, const SpatialRegionRange& rhs) {
    return lhs.min_x == rhs.min_x &&
        lhs.min_y == rhs.min_y &&
        lhs.max_x == rhs.max_x &&
        lhs.max_y == rhs.max_y;
}

uint64_t DB::cellKey(glm::vec2 p) {
    const glm::ivec2 cell = CellPositionFromWorldPosition(p);
    const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cell.x));
    const uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(cell.y));
    return (ux << 32) | uy;
}

void DB::printError(const std::string& s) const {
    std::cout << "error: " << s << " missing";
}

void DB::loadSceneOrExit(const std::string& sceneName) {
    actors = _sceneLoader.loadScene(sceneName);
    occupancy.reserve(actors.size());
    updating_actor_indices.clear();
    updating_actor_indices.reserve(actors.size());
    actor_index_by_id.clear();
    actor_index_by_id.reserve(actors.size());
    playerIndex = -1;

    for (int i = 0; i < static_cast<int>(actors.size()); ++i) {
        actor_index_by_id[actors[i].id] = i;
        if (actors[i].actor_name == "player") {
            playerIndex = i;
            updating_actor_indices.push_back(i);
        }
        else if (actors[i].velocity != glm::vec2(0.0f, 0.0f)) {
            updating_actor_indices.push_back(i);
        }
    }

    rebuildOccupancy();
}

void DB::initDatabase() {
    rapidjson::Document gameDoc;
    const fs::path base = ResourcePaths::GetGameRoot();

    if (!fs::exists(base)) {
        printError(base.string());
        std::exit(0);
    }

    const fs::path gameConfigPath = ResourcePaths::ResolveExistingGameFile("game.config");
    if (gameConfigPath.empty()) {
        printError(gameConfigPath.string());
        std::exit(0);
    }

    EngineUtils::ReadJsonFile(gameConfigPath.string(), gameDoc);

    if (gameDoc.HasMember("game_title") && gameDoc["game_title"].IsString()) {
        game_title = gameDoc["game_title"].GetString();
    }
    if (gameDoc.HasMember("font") && gameDoc["font"].IsString()) {
        font = gameDoc["font"].GetString();
    }
    if (gameDoc.HasMember("intro_bgm") && gameDoc["intro_bgm"].IsString()) {
        intro_bgm = gameDoc["intro_bgm"].GetString();
    }
    if (gameDoc.HasMember("gameplay_audio") && gameDoc["gameplay_audio"].IsString()) {
        gameplay_audio = gameDoc["gameplay_audio"].GetString();
    }
    if (gameDoc.HasMember("score_sfx") && gameDoc["score_sfx"].IsString()) {
        score_sfx = gameDoc["score_sfx"].GetString();
    }
    if (gameDoc.HasMember("player_movement_speed") && gameDoc["player_movement_speed"].IsNumber()) {
        player_movement_speed = gameDoc["player_movement_speed"].GetFloat();
    }
    if (gameDoc.HasMember("hp_image") && gameDoc["hp_image"].IsString()) {
        hp_image = gameDoc["hp_image"].GetString();
    }
    if (gameDoc.HasMember("game_over_bad_image") && gameDoc["game_over_bad_image"].IsString()) {
        game_over_bad_image = gameDoc["game_over_bad_image"].GetString();
    }
    if (gameDoc.HasMember("game_over_bad_audio") && gameDoc["game_over_bad_audio"].IsString()) {
        game_over_bad_audio = gameDoc["game_over_bad_audio"].GetString();
    }
    if (gameDoc.HasMember("game_over_good_image") && gameDoc["game_over_good_image"].IsString()) {
        game_over_good_image = gameDoc["game_over_good_image"].GetString();
    }
    if (gameDoc.HasMember("game_over_good_audio") && gameDoc["game_over_good_audio"].IsString()) {
        game_over_good_audio = gameDoc["game_over_good_audio"].GetString();
    }
    if (gameDoc.HasMember("intro_image") && gameDoc["intro_image"].IsArray()) {
        for (const rapidjson::Value& introImage : gameDoc["intro_image"].GetArray()) {
            if (introImage.IsString()) {
                intro_images.push_back(introImage.GetString());
            }
        }
    }
    if (gameDoc.HasMember("intro_text") && gameDoc["intro_text"].IsArray()) {
        for (const rapidjson::Value& introTextLine : gameDoc["intro_text"].GetArray()) {
            if (introTextLine.IsString()) {
                intro_text.push_back(introTextLine.GetString());
            }
        }
    }
    const fs::path renderingConfigPath = ResourcePaths::ResolveExistingGameFile("rendering.config");
    if (!renderingConfigPath.empty()) {
        rapidjson::Document renderDoc;
        EngineUtils::ReadJsonFile(renderingConfigPath.string(), renderDoc);
        if (renderDoc.HasMember("x_resolution") && renderDoc["x_resolution"].IsInt()) {
            x_resolution = renderDoc["x_resolution"].GetInt();
        }
        if (renderDoc.HasMember("y_resolution") && renderDoc["y_resolution"].IsInt()) {
            y_resolution = renderDoc["y_resolution"].GetInt();
        }
        if (renderDoc.HasMember("clear_color_r") && renderDoc["clear_color_r"].IsInt()) {
            clear_color_r = renderDoc["clear_color_r"].GetInt();
        }
        if (renderDoc.HasMember("clear_color_g") && renderDoc["clear_color_g"].IsInt()) {
            clear_color_g = renderDoc["clear_color_g"].GetInt();
        }
        if (renderDoc.HasMember("clear_color_b") && renderDoc["clear_color_b"].IsInt()) {
            clear_color_b = renderDoc["clear_color_b"].GetInt();
        }
        if (renderDoc.HasMember("zoom_factor") && renderDoc["zoom_factor"].IsNumber()) {
            zoom_factor = renderDoc["zoom_factor"].GetFloat();
        }
        if (renderDoc.HasMember("cam_ease_factor") && renderDoc["cam_ease_factor"].IsNumber()) {
            cam_ease_factor = renderDoc["cam_ease_factor"].GetFloat();
        }
        if (renderDoc.HasMember("x_scale_actor_flipping_on_movement") &&
            renderDoc["x_scale_actor_flipping_on_movement"].IsBool()) {
            x_scale_actor_flipping_on_movement =
                renderDoc["x_scale_actor_flipping_on_movement"].GetBool();
        }
        if (renderDoc.HasMember("cam_offset_x") && renderDoc["cam_offset_x"].IsNumber()) {
            cam_offset_x = renderDoc["cam_offset_x"].GetFloat();
        }
        if (renderDoc.HasMember("cam_offset_y") && renderDoc["cam_offset_y"].IsNumber()) {
            cam_offset_y = renderDoc["cam_offset_y"].GetFloat();
        }
    }

    if (!gameDoc.IsObject() ||
        !gameDoc.HasMember("initial_scene") ||
        !gameDoc["initial_scene"].IsString() ||
        gameDoc["initial_scene"].GetStringLength() == 0) {
        std::cout << "error: initial_scene unspecified";
        std::exit(0);
    }

    _sceneLoader.initialScene = gameDoc["initial_scene"].GetString();
    loadSceneOrExit(_sceneLoader.initialScene);
}

void DB::rebuildOccupancy() {
    occupancy.clear();
    collider_regions.clear();
    trigger_regions.clear();
    collider_query_stamp.assign(actors.size(), 0);
    trigger_query_stamp.assign(actors.size(), 0);
    collider_query_generation = 1;
    trigger_query_generation = 1;

    for (int i = 0; i < static_cast<int>(actors.size()); ++i) {
        if (actors[i].destroyed) {
            continue;
        }

        occupancy[cellKey(actors[i].position)].push_back(i);

        SpatialRegionRange range;
        if (GetActorRegionRange(actors[i], actors[i].position, false, COLLIDER_REGION_SIZE, range)) {
            AddActorToRegions(collider_regions, i, range);
        }
        if (GetActorRegionRange(actors[i], actors[i].position, true, TRIGGER_REGION_SIZE, range)) {
            AddActorToRegions(trigger_regions, i, range);
        }
    }
}

void DB::addActorToRuntimeStructures(int actorIndex) {
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return;
    }

    actor_index_by_id[actors[actorIndex].id] = actorIndex;
    if (collider_query_stamp.size() < actors.size()) {
        collider_query_stamp.push_back(0);
    }
    if (trigger_query_stamp.size() < actors.size()) {
        trigger_query_stamp.push_back(0);
    }

    if (actors[actorIndex].destroyed) {
        return;
    }

    occupancy[cellKey(actors[actorIndex].position)].push_back(actorIndex);

    SpatialRegionRange range;
    if (GetActorRegionRange(actors[actorIndex], actors[actorIndex].position, false, COLLIDER_REGION_SIZE, range)) {
        AddActorToRegions(collider_regions, actorIndex, range);
    }
    if (GetActorRegionRange(actors[actorIndex], actors[actorIndex].position, true, TRIGGER_REGION_SIZE, range)) {
        AddActorToRegions(trigger_regions, actorIndex, range);
    }
}

void DB::removeActorFromRuntimeStructures(int actorIndex) {
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return;
    }

    const Actor& actor = actors[actorIndex];
    actor_index_by_id.erase(actor.id);

    const uint64_t actorCellKey = cellKey(actor.position);
    auto occupancyIt = occupancy.find(actorCellKey);
    if (occupancyIt != occupancy.end()) {
        std::vector<int>& indices = occupancyIt->second;
        indices.erase(std::remove(indices.begin(), indices.end(), actorIndex), indices.end());
        if (indices.empty()) {
            occupancy.erase(occupancyIt);
        }
    }

    SpatialRegionRange range;
    if (GetActorRegionRange(actor, actor.position, false, COLLIDER_REGION_SIZE, range)) {
        RemoveActorFromRegions(collider_regions, actorIndex, range);
    }
    if (GetActorRegionRange(actor, actor.position, true, TRIGGER_REGION_SIZE, range)) {
        RemoveActorFromRegions(trigger_regions, actorIndex, range);
    }
}

void DB::moveActorInOccupancy(int actorIndex, glm::vec2 oldPos, glm::vec2 newPos) {
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return;
    }

    const uint64_t oldKey = cellKey(oldPos);
    const uint64_t newKey = cellKey(newPos);
    if (oldKey != newKey) {
        auto oldIt = occupancy.find(oldKey);
        if (oldIt != occupancy.end()) {
            std::vector<int>& indices = oldIt->second;
            indices.erase(std::remove(indices.begin(), indices.end(), actorIndex), indices.end());
            if (indices.empty()) {
                occupancy.erase(oldIt);
            }
        }

        occupancy[newKey].push_back(actorIndex);
    }

    const Actor& actor = actors[actorIndex];
    if (actor.destroyed) {
        return;
    }

    const bool hasCollider =
        actor.box_collider_width.has_value() && actor.box_collider_height.has_value();
    const bool hasTrigger =
        actor.box_trigger_width.has_value() && actor.box_trigger_height.has_value();
    if (!hasCollider && !hasTrigger) {
        return;
    }

    SpatialRegionRange oldRange;
    SpatialRegionRange newRange;
    if (hasCollider &&
        GetActorRegionRange(actor, oldPos, false, COLLIDER_REGION_SIZE, oldRange) &&
        GetActorRegionRange(actor, newPos, false, COLLIDER_REGION_SIZE, newRange) &&
        !RegionRangesEqual(oldRange, newRange)) {
        RemoveActorFromRegions(collider_regions, actorIndex, oldRange);
        AddActorToRegions(collider_regions, actorIndex, newRange);
    }

    if (hasTrigger &&
        GetActorRegionRange(actor, oldPos, true, TRIGGER_REGION_SIZE, oldRange) &&
        GetActorRegionRange(actor, newPos, true, TRIGGER_REGION_SIZE, newRange) &&
        !RegionRangesEqual(oldRange, newRange)) {
        RemoveActorFromRegions(trigger_regions, actorIndex, oldRange);
        AddActorToRegions(trigger_regions, actorIndex, newRange);
    }
}

void DB::getActorsInCellRect(int min_x, int min_y, int max_x, int max_y, std::vector<int>& out) const {
    out.clear();
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            auto it = occupancy.find(SpatialKey(x, y));
            if (it == occupancy.end()) {
                continue;
            }

            for (int actorIndex : it->second) {
                if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
                    continue;
                }
                if (!actors[actorIndex].destroyed) {
                    out.push_back(actorIndex);
                }
            }
        }
    }
}

int DB::getActorIndexById(int actorId) const {
    const auto found = actor_index_by_id.find(actorId);
    if (found == actor_index_by_id.end()) {
        return -1;
    }

    return found->second;
}

const Actor* DB::getActorById(int actorId) const {
    const int actorIndex = getActorIndexById(actorId);
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return nullptr;
    }

    if (actors[actorIndex].destroyed) {
        return nullptr;
    }

    return &actors[actorIndex];
}

Actor* DB::getActorById(int actorId) {
    const int actorIndex = getActorIndexById(actorId);
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return nullptr;
    }

    if (actors[actorIndex].destroyed) {
        return nullptr;
    }

    return &actors[actorIndex];
}

void DB::getPotentialColliderIndices(int actorIndex, glm::vec2 position, std::vector<int>& out) const {
    out.clear();
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return;
    }

    if (actors[actorIndex].destroyed) {
        return;
    }

    if (collider_query_generation == std::numeric_limits<int>::max()) {
        std::fill(collider_query_stamp.begin(), collider_query_stamp.end(), 0);
        collider_query_generation = 1;
    }
    const int generation = collider_query_generation++;

    SpatialRegionRange range;
    if (!GetActorRegionRange(actors[actorIndex], position, false, COLLIDER_REGION_SIZE, range)) {
        return;
    }

    for (int y = range.min_y; y <= range.max_y; ++y) {
        for (int x = range.min_x; x <= range.max_x; ++x) {
            auto it = collider_regions.find(SpatialKey(x, y));
            if (it == collider_regions.end()) {
                continue;
            }

            for (int candidate : it->second) {
                if (candidate == actorIndex) {
                    continue;
                }
                if (candidate < 0 || candidate >= static_cast<int>(actors.size()) || actors[candidate].destroyed) {
                    continue;
                }
                if (collider_query_stamp[candidate] == generation) {
                    continue;
                }

                collider_query_stamp[candidate] = generation;
                out.push_back(candidate);
            }
        }
    }
}

void DB::getPotentialTriggerIndices(int actorIndex, glm::vec2 position, std::vector<int>& out) const {
    out.clear();
    if (actorIndex < 0 || actorIndex >= static_cast<int>(actors.size())) {
        return;
    }

    if (actors[actorIndex].destroyed) {
        return;
    }

    if (trigger_query_generation == std::numeric_limits<int>::max()) {
        std::fill(trigger_query_stamp.begin(), trigger_query_stamp.end(), 0);
        trigger_query_generation = 1;
    }
    const int generation = trigger_query_generation++;

    SpatialRegionRange range;
    if (!GetActorRegionRange(actors[actorIndex], position, true, TRIGGER_REGION_SIZE, range)) {
        return;
    }

    for (int y = range.min_y; y <= range.max_y; ++y) {
        for (int x = range.min_x; x <= range.max_x; ++x) {
            auto it = trigger_regions.find(SpatialKey(x, y));
            if (it == trigger_regions.end()) {
                continue;
            }

            for (int candidate : it->second) {
                if (candidate == actorIndex) {
                    continue;
                }
                if (candidate < 0 || candidate >= static_cast<int>(actors.size()) || actors[candidate].destroyed) {
                    continue;
                }
                if (trigger_query_stamp[candidate] == generation) {
                    continue;
                }

                trigger_query_stamp[candidate] = generation;
                out.push_back(candidate);
            }
        }
    }
}
