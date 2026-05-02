#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ResourcePaths.h"
#include "rapidjson/document.h"

class Renderer;
class ImGuiLayer;

class GameEditor
{
public:
    enum class Result {
        BackToLauncher,
        QuitApplication
    };

    enum class EditorTab {
        Scene,
        LuaScripts,
        Resources,
        Changes
    };

    explicit GameEditor(std::filesystem::path game_root);

    Result Run(Renderer& renderer, ImGuiLayer& imguiLayer);

private:
    struct ResourceEntry {
        std::filesystem::path relative_path;
        bool is_directory = false;
        bool exists_in_base = false;
        bool exists_in_shadow = false;
    };

    using Document = rapidjson::Document;
    using Value = rapidjson::Value;
    using Allocator = rapidjson::Document::AllocatorType;

    void RebuildResourceIndices();
    bool RefreshSceneList();
    bool LoadScene(const std::string& scene_name, bool update_status = true);
    bool SaveCurrentSceneToShadow();
    bool SaveCurrentSceneToGame();
    bool DiscardCurrentSceneShadow();
    bool CreateScene(const std::string& raw_scene_name);
    bool DeleteScene(std::string scene_name);
    bool RefreshScriptList();
    bool LoadScript(const std::string& script_relative_path, bool update_status = true);
    bool SaveCurrentScriptToShadow();
    bool SaveCurrentScriptToGame();
    bool DiscardCurrentScriptShadow();
    bool CreateScript(const std::string& raw_script_name);
    bool DeleteScript(std::string script_relative_path);
    bool LoadTextResource(const std::filesystem::path& relative_path, bool update_status = true);
    bool SaveCurrentTextResourceToShadow();
    bool CreateResourceFile(const std::string& raw_resource_name);
    bool CreateResourceDirectory(const std::string& raw_directory_name);
    bool DeleteSelectedResource();
    bool ImportExternalFile(const std::filesystem::path& source_path);
    bool MergeSelectedResourceToGame();
    bool DiscardSelectedResourceChange();
    bool MergeShadowChangeToGame(std::filesystem::path relative_path);
    bool DiscardShadowChange(std::filesystem::path relative_path);
    bool OpenSelectedResourceInDedicatedEditor();

    bool RenderEditorUI(Result& result);
    bool RenderSceneEditorTab();
    bool RenderScenePanel();
    bool RenderActorPanel();
    bool RenderInspectorPanel();
    bool RenderActorInspector(Value& actor);
    bool RenderComponentEditor(Value& actor);
    bool RenderLuaScriptTab();
    bool RenderLuaScriptListPanel();
    bool RenderLuaScriptEditorPanel();
    bool RenderResourceBrowserTab();
    bool RenderResourceTreePanel();
    bool RenderResourceDetailsPanel();
    bool RenderShadowChangesTab();
    bool RenderShadowChangeListPanel();
    bool RenderShadowChangeDetailsPanel();
    bool EditJsonValue(const std::string& label, Value& value);

    Value& EnsureActorsArray();
    static std::string SceneFilename(const std::string& scene_name);
    static std::string SceneDisplayName(const std::string& scene_name);
    static std::string ScriptDisplayName(const std::string& script_relative_path);
    static bool IsRelativePathUnder(
        const std::filesystem::path& relative_path,
        const std::filesystem::path& root_path);
    static bool CanOpenInSceneTab(const std::filesystem::path& relative_path);
    static bool CanOpenInLuaTab(const std::filesystem::path& relative_path);
    static bool IsTextEditableResourcePath(const std::filesystem::path& relative_path);

    void CreateActor();
    void DuplicateSelectedActor();
    void DeleteSelectedActor();
    void ResetCurrentSceneState();
    void ResetCurrentScriptState();
    void ResetCurrentTextResourceState();
    void RefreshSelectionsFromIndices(
        bool reload_current_scene,
        bool reload_current_script,
        bool reload_current_text_resource);
    void SelectResource(const std::filesystem::path& relative_path, bool is_directory);

    std::filesystem::path GetSelectedResourceDirectory() const;
    const ResourceEntry* FindResourceEntry(
        const std::filesystem::path& relative_path,
        bool is_directory) const;
    const ResourcePaths::ShadowChange* FindShadowChange(
        const std::filesystem::path& relative_path) const;

    bool EditStringField(Value& object, const char* key, const char* label, std::size_t buffer_size = 256);
    bool EditFloatField(Value& object, const char* key, const char* label);
    bool EditIntField(Value& object, const char* key, const char* label);
    bool EditBoolField(Value& object, const char* key, const char* label);

    static Value* FindMember(Value& object, const char* key);
    static const Value* FindMember(const Value& object, const char* key);
    static void SetString(Value& object, const char* key, const std::string& value, Allocator& allocator);
    static void SetDouble(Value& object, const char* key, double value, Allocator& allocator);
    static void SetInt(Value& object, const char* key, int value, Allocator& allocator);
    static void SetBool(Value& object, const char* key, bool value, Allocator& allocator);
    static Value MakeDefaultValueForType(int type_index, Allocator& allocator);

    std::filesystem::path game_root;
    EditorTab active_tab = EditorTab::Scene;
    EditorTab pending_tab_selection_target = EditorTab::Scene;
    std::vector<std::string> scene_names;
    std::vector<std::string> script_names;
    std::vector<ResourceEntry> resource_entries;
    std::vector<ResourcePaths::ShadowChange> shadow_changes;
    Document current_scene_document;
    std::string current_scene_name = "";
    std::string current_script_relative_path = "";
    std::string current_script_text = "";
    std::string current_text_resource_relative_path = "";
    std::string current_text_resource_text = "";
    std::string status_message = "";
    int selected_actor_index = -1;
    bool scene_loaded = false;
    bool script_loaded = false;
    bool text_resource_loaded = false;
    bool pending_tab_selection = true;
    std::string pending_scene_delete_name = "";
    std::string pending_script_delete_name = "";
    std::filesystem::path selected_resource_path;
    std::filesystem::path selected_shadow_change_path;
    std::filesystem::path pending_resource_delete_path;
    bool selected_resource_is_directory = true;
    bool pending_resource_delete_is_directory = false;
    char new_scene_name[128] = "";
    char new_script_name[128] = "";
    char new_resource_name[256] = "";
    char new_component_key[128] = "";
    char new_component_type[128] = "";
    char new_property_name[128] = "";
    int new_property_type = 4;
};
