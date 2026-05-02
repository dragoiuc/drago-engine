#include "SceneDB.h"
#include "DB.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>

#include "ActorID.h"
#include "EngineUtils.h"
#include "ResourcePaths.h"

namespace fs = std::filesystem;

static std::optional<Actor::ComponentProperty> ParseComponentProperty(const rapidjson::Value& value) {
    Actor::ComponentProperty property;

    if (value.IsNull()) {
        property.type = Actor::ComponentProperty::Type::Nil;
        return property;
    }

    if (value.IsBool()) {
        property.type = Actor::ComponentProperty::Type::Boolean;
        property.bool_value = value.GetBool();
        return property;
    }

    if (value.IsNumber()) {
        if (value.IsInt64()) {
            property.type = Actor::ComponentProperty::Type::Integer;
            property.integer_value = value.GetInt64();
        }
        else if (value.IsUint64()) {
            const uint64_t integerValue = value.GetUint64();
            if (integerValue <= static_cast<uint64_t>(std::numeric_limits<long long>::max())) {
                property.type = Actor::ComponentProperty::Type::Integer;
                property.integer_value = static_cast<long long>(integerValue);
            }
            else {
                property.type = Actor::ComponentProperty::Type::Number;
                property.number_value = value.GetDouble();
            }
        }
        else {
            property.type = Actor::ComponentProperty::Type::Number;
            property.number_value = value.GetDouble();
        }
        return property;
    }

    if (value.IsString()) {
        property.type = Actor::ComponentProperty::Type::String;
        property.string_value = value.GetString();
        return property;
    }

    return std::nullopt;
}

std::string SceneDB::StripSceneExtension(const std::string& name) {
    if (name.size() >= 6 && name.substr(name.size() - 6) == ".scene") {
        return name.substr(0, name.size() - 6);
    }
    return name;
}

std::string SceneDB::EnsureSceneExtension(const std::string& name) {
    if (name.size() >= 6 && name.substr(name.size() - 6) == ".scene") {
        return name;
    }
    return name + ".scene";
}

std::string SceneDB::BuildSceneErrorName(const std::string& name) {
    return StripSceneExtension(name);
}

void SceneDB::applyActorFields(Actor& actor, const rapidjson::Value& src) const {
    if (!src.IsObject()) return;

    if (src.HasMember("name") && src["name"].IsString()) {
        actor.actor_name = src["name"].GetString();
    }

    if (src.HasMember("components") && src["components"].IsObject()) {
        for (auto componentIt = src["components"].MemberBegin();
            componentIt != src["components"].MemberEnd();
            ++componentIt) {
            if (!componentIt->name.IsString() || !componentIt->value.IsObject()) {
                continue;
            }

            const std::string componentKey = componentIt->name.GetString();
            const rapidjson::Value& componentValue = componentIt->value;
            Actor::ComponentDefinition& componentDefinition = actor.component_definitions[componentKey];

            if (componentValue.HasMember("type") && componentValue["type"].IsString()) {
                componentDefinition.type = componentValue["type"].GetString();
            }

            for (auto propertyIt = componentValue.MemberBegin();
                propertyIt != componentValue.MemberEnd();
                ++propertyIt) {
                if (!propertyIt->name.IsString()) {
                    continue;
                }

                const std::string propertyName = propertyIt->name.GetString();
                if (propertyName == "type") {
                    continue;
                }

                const std::optional<Actor::ComponentProperty> property =
                    ParseComponentProperty(propertyIt->value);
                if (property.has_value()) {
                    componentDefinition.properties[propertyName] = *property;
                }
            }
        }
    }

    if (src.HasMember("view_image") && src["view_image"].IsString()) {
        actor.view_image = src["view_image"].GetString();
    }
    if (src.HasMember("view_image_back") && src["view_image_back"].IsString()) {
        actor.view_image_back = src["view_image_back"].GetString();
    }
    if (src.HasMember("view_image_damage") && src["view_image_damage"].IsString()) {
        actor.view_image_damage = src["view_image_damage"].GetString();
    }
    if (src.HasMember("view_image_attack") && src["view_image_attack"].IsString()) {
        actor.view_image_attack = src["view_image_attack"].GetString();
    }
    if (src.HasMember("damage_sfx") && src["damage_sfx"].IsString()) {
        actor.damage_sfx = src["damage_sfx"].GetString();
    }
    if (src.HasMember("step_sfx") && src["step_sfx"].IsString()) {
        actor.step_sfx = src["step_sfx"].GetString();
    }
    if (src.HasMember("nearby_dialogue_sfx") && src["nearby_dialogue_sfx"].IsString()) {
        actor.nearby_dialogue_sfx = src["nearby_dialogue_sfx"].GetString();
    }

    if (src.HasMember("x") && src["x"].IsNumber()) {
        actor.position.x = src["x"].GetFloat();
    }
    if (src.HasMember("transform_position_x") && src["transform_position_x"].IsNumber()) {
        actor.position.x = src["transform_position_x"].GetFloat();
    }

    if (src.HasMember("y") && src["y"].IsNumber()) {
        actor.position.y = src["y"].GetFloat();
    }
    if (src.HasMember("transform_position_y") && src["transform_position_y"].IsNumber()) {
        actor.position.y = src["transform_position_y"].GetFloat();
    }

    if (src.HasMember("vel_x") && src["vel_x"].IsNumber()) {
        actor.velocity.x = src["vel_x"].GetFloat();
    }

    if (src.HasMember("vel_y") && src["vel_y"].IsNumber()) {
        actor.velocity.y = src["vel_y"].GetFloat();
    }

    if (src.HasMember("transform_scale_x") && src["transform_scale_x"].IsNumber()) {
        actor.transform_scale.x = src["transform_scale_x"].GetFloat();
    }

    if (src.HasMember("transform_scale_y") && src["transform_scale_y"].IsNumber()) {
        actor.transform_scale.y = src["transform_scale_y"].GetFloat();
    }

    if (src.HasMember("transform_rotation_degrees") && src["transform_rotation_degrees"].IsNumber()) {
        actor.transform_rotation_degrees = src["transform_rotation_degrees"].GetFloat();
    }

    if (src.HasMember("view_pivot_offset_x") && src["view_pivot_offset_x"].IsNumber()) {
        actor.view_pivot_offset_x = src["view_pivot_offset_x"].GetFloat();
    }

    if (src.HasMember("view_pivot_offset_y") && src["view_pivot_offset_y"].IsNumber()) {
        actor.view_pivot_offset_y = src["view_pivot_offset_y"].GetFloat();
    }

    if (src.HasMember("render_order") && src["render_order"].IsInt()) {
        actor.render_order = src["render_order"].GetInt();
    }

    if (src.HasMember("box_collider_width") && src["box_collider_width"].IsNumber()) {
        actor.box_collider_width = src["box_collider_width"].GetFloat();
    }
    if (src.HasMember("box_collider_height") && src["box_collider_height"].IsNumber()) {
        actor.box_collider_height = src["box_collider_height"].GetFloat();
    }
    if (src.HasMember("box_trigger_width") && src["box_trigger_width"].IsNumber()) {
        actor.box_trigger_width = src["box_trigger_width"].GetFloat();
    }
    if (src.HasMember("box_trigger_height") && src["box_trigger_height"].IsNumber()) {
        actor.box_trigger_height = src["box_trigger_height"].GetFloat();
    }

    if (src.HasMember("blocking") && src["blocking"].IsBool()) {
        actor.blocking = src["blocking"].GetBool();
    }
    if (src.HasMember("movement_bounce_enabled") && src["movement_bounce_enabled"].IsBool()) {
        actor.movement_bounce_enabled = src["movement_bounce_enabled"].GetBool();
    }

    if (src.HasMember("nearby_dialogue") && src["nearby_dialogue"].IsString()) {
        actor.nearby_dialogue = src["nearby_dialogue"].GetString();
    }

    if (src.HasMember("contact_dialogue") && src["contact_dialogue"].IsString()) {
        actor.contact_dialogue = src["contact_dialogue"].GetString();
    }
}

const rapidjson::Document& SceneDB::loadTemplateDocument(const std::string& templateName) {
    auto found = _templateCache.find(templateName);
    if (found != _templateCache.end()) return found->second;

    const fs::path templatePath = ResourcePaths::ResolveExistingGameFile(
        fs::path("actor_templates") / fs::path(templateName + ".template"));
    if (templatePath.empty()) {
        std::cout << "error: template " << templateName << " is missing";
        std::exit(0);
    }

    rapidjson::Document doc;
    EngineUtils::ReadJsonFile(templatePath.string(), doc);
    if (!doc.IsObject()) {
        std::cout << "error: template " << templateName << " is missing";
        std::exit(0);
    }

    auto inserted = _templateCache.emplace(templateName, rapidjson::Document());
    inserted.first->second.CopyFrom(doc, inserted.first->second.GetAllocator());
    return inserted.first->second;
}

Actor SceneDB::loadActorFromTemplate(const std::string& templateName) {
    auto found = _actorTemplateCache.find(templateName);
    if (found == _actorTemplateCache.end()) {
        Actor templateActor;
        const rapidjson::Document& templateDoc = loadTemplateDocument(templateName);
        applyActorFields(templateActor, templateDoc);
        found = _actorTemplateCache.emplace(templateName, std::move(templateActor)).first;
    }

    Actor actor = found->second;
    actor.id = ActorID::Next();
    return actor;
}

std::deque<Actor> SceneDB::loadScene(const std::string& sceneToLoad) {

    const std::string sceneFileName = EnsureSceneExtension(sceneToLoad);
    const std::string sceneErrorName = BuildSceneErrorName(sceneToLoad);

    const fs::path scenePath = ResourcePaths::ResolveExistingGameFile(
        fs::path("scenes") / fs::path(sceneFileName));
    if (scenePath.empty()) {
        std::cout << "error: scene " << sceneErrorName << " is missing";
        std::exit(0);
    }

    rapidjson::Document sceneDoc;
    EngineUtils::ReadJsonFile(scenePath.string(), sceneDoc);
    if (!sceneDoc.IsObject() || !sceneDoc.HasMember("actors") || !sceneDoc["actors"].IsArray()) {
        std::cout << "error: scene " << sceneErrorName << " is missing";
        std::exit(0);
    }

    const rapidjson::Value& actors = sceneDoc["actors"];
    std::deque<Actor> temp;

    for (const rapidjson::Value& a : actors.GetArray()) {
        Actor actor;

        if (a.IsObject() && a.HasMember("template") && a["template"].IsString()) {
            const std::string templateName = a["template"].GetString();
            actor = loadActorFromTemplate(templateName);
        }

        applyActorFields(actor, a);
        if (actor.id < 0) {
            actor.id = ActorID::Next();
        }
        temp.push_back(actor);
    }
    return temp;
}
