#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "DB.h"

class Renderer;
class ImGuiLayer;
class Rigidbody;
class ParticleSystem;
class b2World;
struct lua_State;
namespace luabridge {
	class LuaRef;
	class LuaException;
}

class Engine
{
public:
	friend struct Actor;
	friend class Rigidbody;

	struct DialogueLine {
		int actorId;
		std::string text;
	};

	struct PendingOnStart {
		int actor_id = -1;
		std::string component_key;
	};

	struct EventSubscription {
		Actor* actor = nullptr;
		std::string component_key = "";
		std::shared_ptr<luabridge::LuaRef> component_ref;
		std::shared_ptr<luabridge::LuaRef> function_ref;
	};

	enum class PendingEventChangeType {
		Subscribe,
		Unsubscribe
	};

	struct PendingEventChange {
		PendingEventChangeType type = PendingEventChangeType::Subscribe;
		std::string event_type = "";
		Actor* actor = nullptr;
		std::string component_key = "";
		std::shared_ptr<luabridge::LuaRef> component_ref;
		std::shared_ptr<luabridge::LuaRef> function_ref;
	};

	Engine();
	~Engine();

	void ProcessInput(bool& windowRunning, ImGuiLayer* imguiLayer = nullptr);
	bool TryMoveActor(int actorIndex, const glm::vec2& delta);
	void UpdateGameplayActors();
	void RenderIntro(Renderer& renderer);
	bool RenderGameplay(Renderer& renderer);
	void RenderHUD(Renderer& renderer);
	void PrimeActorRenderCaches();
	void StartGameplayAudioIfNeeded();
	bool HasActiveIntro() const;
	std::size_t GetCurrentIntroImageIndex() const;
	std::size_t GetCurrentIntroTextIndex() const;
	bool ProcessDialogueCommands(
		const std::vector<DialogueLine>& lines,
		std::vector<std::string>& dialogueToRender,
		bool renderDialogueText);
	void RenderDialogue(const std::vector<std::string>& dialogueToRender);
	void TriggerGameOver(bool won);
	void RenderGameOver(Renderer& renderer) const;
	void LoadCurrentSceneComponents();
	void ProcessPendingSceneLoad();
	void RunPendingOnStarts();
	void StepPhysics();

	//private:

	DB _db; // Centralized container for everything we'll ever need 

	void RegisterDebugAPI();
	void RegisterActorAPI();
	void RegisterApplicationAPI();
	void RegisterInputAPI();
	void RegisterPhysicsAPI();
	void RegisterParticleAPI();
	void RegisterTextAPI();
	void RegisterAudioAPI();
	void RegisterImageAPI();
	void RegisterCameraAPI();
	void RegisterSceneAPI();
	void RegisterEventAPI();
	b2World* EnsurePhysicsWorld();
	void SyncRigidbodyActors();
	void ClearComponentInstanceRefs();
	void RebuildActorComponentTypeIndex(Actor& actor);
	void RebuildActorNameIndex();
	void IndexActorByName(Actor& actor);
	void RemoveActorFromNameIndex(Actor& actor);
	std::shared_ptr<luabridge::LuaRef> LoadComponentType(const std::string& component_type);
	Actor::ComponentInstance CreateComponentInstance(
		Actor& actor,
		const std::string& component_key,
		const Actor::ComponentDefinition& definition);
	luabridge::LuaRef AddRuntimeComponent(Actor& actor, const std::string& type_name);
	void RemoveRuntimeComponent(Actor& actor, const luabridge::LuaRef& component_ref);
	Actor* InstantiateRuntimeActor(const std::string& actor_template_name);
	void DestroyRuntimeActor(Actor& actor);
	void QueueActorOnStarts(const Actor& actor);
	void RegisterLifecycleComponent(Actor::ComponentInstance& component);
	void RebuildLifecycleComponentLists();
	void RunComponentLifecyclePhase(bool late_update);
	void CallComponentLifecycleFunction(
		const Actor& actor,
		Actor::ComponentInstance& component,
		const luabridge::LuaRef& lifecycle_function);
	void CallComponentOnStart(Actor::ComponentInstance& component);
	void CallComponentOnDestroy(Actor::ComponentInstance& component);
	void RunOnDestroyPhase();
	void RunPendingSceneLoadOnDestroyPhase();
	void PublishEvent(const std::string& event_type, const luabridge::LuaRef& event_object);
	void QueueEventSubscriptionChange(
		PendingEventChangeType type,
		const std::string& event_type,
		const luabridge::LuaRef& component_ref,
		const luabridge::LuaRef& function_ref);
	void ApplyPendingEventSubscriptionChanges();
	void PruneEventSubscriptions();
	void CleanupRemovedComponents();
	static void LuaDebugLog(const std::string& message);
	static void ReportLuaException(const std::string& actor_name, const luabridge::LuaException& exception);
	static Actor* LuaActorInstantiate(const std::string& actor_template_name);
	static void LuaActorDestroy(Actor* actor);
	static void LuaApplicationQuit();
	static void LuaApplicationSleep(int milliseconds);
	static int LuaApplicationGetFrame();
	static void LuaApplicationOpenURL(const std::string& url);
	static void LuaTextDraw(
		const std::string& text,
		float x,
		float y,
		const std::string& font_name,
		float font_size,
		float r,
		float g,
		float b,
		float a);
	static void LuaAudioPlay(int channel, const std::string& clip_name, bool does_loop);
	static void LuaAudioHalt(int channel);
	static void LuaAudioSetVolume(int channel, float volume);
	static void LuaImageDrawUI(const std::string& image_name, float x, float y);
	static void LuaImageDrawUIEx(
		const std::string& image_name,
		float x,
		float y,
		float r,
		float g,
		float b,
		float a,
		float sorting_order);
	static void LuaImageDraw(const std::string& image_name, float x, float y);
	static void LuaImageDrawEx(
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
		float sorting_order);
	static int LuaImageDrawExRaw(lua_State* lua_state);
	static void LuaImageDrawPixel(float x, float y, float r, float g, float b, float a);
	static void LuaCameraSetPosition(float x, float y);
	static float LuaCameraGetPositionX();
	static float LuaCameraGetPositionY();
	static void LuaCameraSetZoom(float zoom_factor);
	static float LuaCameraGetZoom();
	static void LuaSceneLoad(const std::string& scene_name);
	static std::string LuaSceneGetCurrent();
	static void LuaSceneDontDestroy(Actor* actor);
	static void LuaEventPublish(const std::string& event_type, const luabridge::LuaRef& event_object);
	static void LuaEventSubscribe(
		const std::string& event_type,
		const luabridge::LuaRef& component_ref,
		const luabridge::LuaRef& function_ref);
	static void LuaEventUnsubscribe(
		const std::string& event_type,
		const luabridge::LuaRef& component_ref,
		const luabridge::LuaRef& function_ref);

	std::vector<DialogueLine> collectContactDialogueLines() const;
	std::vector<DialogueLine> collectNearbyDialogueLines() const;

	Actor* player = nullptr;
	std::unordered_set<int> score_awarded_ids;
	std::unordered_set<int> nearby_dialogue_sfx_played_ids;
	std::vector<PendingOnStart> pending_on_starts;
	std::vector<Actor*> actors_pending_cleanup;
	std::unordered_map<std::string, std::shared_ptr<luabridge::LuaRef>> component_type_refs;
	std::unordered_map<std::string, std::vector<Actor*>> actor_refs_by_name;
	std::unordered_map<std::string, std::vector<EventSubscription>> event_subscriptions;
	std::vector<PendingEventChange> pending_event_changes;
	std::vector<Actor::ComponentInstance*> components_with_on_update;
	std::vector<Actor::ComponentInstance*> components_with_on_late_update;
	lua_State* lua_state = nullptr;
	int runtime_added_component_count = 0;
	bool cleanup_removed_components_pending = false;

	bool advanceIntroRequested = false;
	bool introSequenceComplete = false;
	bool introBgmWasPlaying = false;
	bool gameplayAudioStarted = false;
	bool gameOverActive = false;
	bool gameOverWon = false;
	bool cameraPositionInitialized = false;
	int lastDamageFrame = -180;
	std::size_t currentIntroStep = 0;
	std::string current_scene_name = "";
	std::optional<std::string> pending_scene_load_name;
	glm::vec2 currentCameraPosition = glm::vec2(0.0f, 0.0f);
	float max_actor_half_width_units = 0.0f;
	float max_actor_half_height_units = 0.0f;
	std::unique_ptr<b2World> physics_world;

};

#endif
