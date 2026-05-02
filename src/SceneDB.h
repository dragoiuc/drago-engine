#pragma once
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "Actor.h"
#include "rapidjson/document.h"

class SceneDB
{
public:
	std::string initialScene;

	std::deque<Actor> loadScene(const std::string& sceneToLoad);
	Actor loadActorFromTemplate(const std::string& templateName);

private:
	std::unordered_map<std::string, rapidjson::Document> _templateCache;
	std::unordered_map<std::string, Actor> _actorTemplateCache;
	static std::string StripSceneExtension(const std::string& name);
	static std::string EnsureSceneExtension(const std::string& name);
	static std::string BuildSceneErrorName(const std::string& name);
	void applyActorFields(Actor& actor, const rapidjson::Value& src) const;
	const rapidjson::Document& loadTemplateDocument(const std::string& templateName);
};
