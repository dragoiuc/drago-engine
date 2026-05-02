#pragma once
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "glm/glm.hpp"
#include "SceneDB.h"

/* Class to hold all information in one location */
/* Single Source of all Truth */
class DB
{
public:
	std::string game_title = "";
	std::string font = "";
	std::string intro_bgm = "";
	std::string gameplay_audio = "";
	std::string score_sfx = "";
	std::string hp_image = "";
	std::string game_over_bad_image = "";
	std::string game_over_bad_audio = "";
	std::string game_over_good_image = "";
	std::string game_over_good_audio = "";
	std::vector<std::string> intro_images;
	std::vector<std::string> intro_text;

	int x_resolution = 640;
	int y_resolution = 360;

	int clear_color_r = 255;
	int clear_color_g = 255;
	int clear_color_b = 255;
	float player_movement_speed = 0.02f;
	float zoom_factor = 1.0f;
	float cam_ease_factor = 1.0f;
	bool x_scale_actor_flipping_on_movement = false;
	float cam_offset_x = 0.0f;
	float cam_offset_y = 0.0f;

	void initDatabase();

	std::deque<Actor> actors;
	int playerIndex = -1;
	std::unordered_map<uint64_t, std::vector<int>> occupancy;

	// Accelerating Data Structures
	std::vector<int> updating_actor_indices;
	std::unordered_map<int, int> actor_index_by_id;


	SceneDB _sceneLoader;

	void loadSceneOrExit(const std::string& sceneName);
	void addActorToRuntimeStructures(int actorIndex);
	void removeActorFromRuntimeStructures(int actorIndex);
	void rebuildOccupancy();
	void moveActorInOccupancy(int actorIndex, glm::vec2 oldPos, glm::vec2 newPos);
	void getActorsInCellRect(int min_x, int min_y, int max_x, int max_y, std::vector<int>& out) const;
	void getPotentialColliderIndices(int actorIndex, glm::vec2 position, std::vector<int>& out) const;
	void getPotentialTriggerIndices(int actorIndex, glm::vec2 position, std::vector<int>& out) const;
	int getActorIndexById(int actorId) const;
	const Actor* getActorById(int actorId) const;
	Actor* getActorById(int actorId);
	static uint64_t cellKey(glm::vec2 p);
	void printError(const std::string& s) const;

private:
	std::unordered_map<uint64_t, std::vector<int>> collider_regions;
	std::unordered_map<uint64_t, std::vector<int>> trigger_regions;
	mutable std::vector<int> collider_query_stamp;
	mutable std::vector<int> trigger_query_stamp;
	mutable int collider_query_generation = 1;
	mutable int trigger_query_generation = 1;
};
