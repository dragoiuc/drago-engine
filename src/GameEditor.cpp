#include "GameEditor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "EngineUtils.h"
#include "Helper.h"
#include "ImGuiLayer.h"
#include "Renderer.h"
#include "ResourcePaths.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace fs = std::filesystem;

namespace {
    constexpr int kJsonTypeCount = 5;
    const char* kJsonTypeLabels[kJsonTypeCount] = { "Null", "Bool", "Int", "Number", "String" };

    bool HasSceneExtension(const fs::path& path) {
        return path.extension() == ".scene";
    }

    bool HasLuaExtension(const fs::path& path) {
        return path.extension() == ".lua";
    }

    bool HasTextEditingExtension(const fs::path& path) {
        static const std::set<std::string> kEditableExtensions = {
            "",
            ".cfg",
            ".config",
            ".csv",
            ".frag",
            ".glsl",
            ".ini",
            ".json",
            ".lua",
            ".md",
            ".scene",
            ".shader",
            ".template",
            ".txt",
            ".vert",
            ".xml",
            ".yaml",
            ".yml"
        };

        return kEditableExtensions.find(path.extension().string()) != kEditableExtensions.end();
    }

    std::string TrimWhitespace(const std::string& text) {
        std::size_t start = 0;
        while (start < text.size() &&
            std::isspace(static_cast<unsigned char>(text[start])) != 0) {
            ++start;
        }

        std::size_t end = text.size();
        while (end > start &&
            std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }

        return text.substr(start, end - start);
    }

    bool IsContainedRelativePath(const fs::path& path) {
        if (path.empty() || path.is_absolute() || path.has_root_path()) {
            return false;
        }

        for (const fs::path& part : path) {
            if (part.empty() || part == "." || part == "..") {
                return false;
            }
        }

        return true;
    }

    bool TryBuildNewFileRelativePath(
        const std::string& raw_name,
        const std::string& required_extension,
        bool allow_subdirectories,
        fs::path& out_relative_path) {
        const std::string trimmedName = TrimWhitespace(raw_name);
        if (trimmedName.empty()) {
            return false;
        }

        const fs::path inputPath(trimmedName);
        if (!IsContainedRelativePath(inputPath)) {
            return false;
        }
        if (!allow_subdirectories && inputPath.has_parent_path()) {
            return false;
        }

        fs::path normalizedPath = inputPath.lexically_normal();
        if (normalizedPath.filename().empty()) {
            return false;
        }

        if (normalizedPath.extension().empty()) {
            normalizedPath = fs::path(normalizedPath.generic_string() + required_extension);
        }
        else if (normalizedPath.extension() != required_extension) {
            return false;
        }

        if (normalizedPath.stem().empty()) {
            return false;
        }

        out_relative_path = normalizedPath;
        return true;
    }

    bool ReadTextFile(const fs::path& path, std::string& out_text) {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return false;
        }

        out_text.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
        return input.good() || input.eof();
    }

    bool WriteTextFile(const fs::path& path, const std::string& contents) {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return output.good();
    }

    bool FilesAreBinaryEqual(const fs::path& lhs, const fs::path& rhs) {
        std::error_code error;
        const std::uintmax_t lhsSize = fs::file_size(lhs, error);
        if (error) {
            return false;
        }

        const std::uintmax_t rhsSize = fs::file_size(rhs, error);
        if (error || lhsSize != rhsSize) {
            return false;
        }

        std::ifstream lhsStream(lhs, std::ios::binary);
        std::ifstream rhsStream(rhs, std::ios::binary);
        if (!lhsStream.is_open() || !rhsStream.is_open()) {
            return false;
        }

        constexpr std::size_t kChunkSize = 4096;
        std::array<char, kChunkSize> lhsBuffer{};
        std::array<char, kChunkSize> rhsBuffer{};

        while (lhsStream.good() || rhsStream.good()) {
            lhsStream.read(lhsBuffer.data(), static_cast<std::streamsize>(lhsBuffer.size()));
            rhsStream.read(rhsBuffer.data(), static_cast<std::streamsize>(rhsBuffer.size()));

            const std::streamsize lhsRead = lhsStream.gcount();
            const std::streamsize rhsRead = rhsStream.gcount();
            if (lhsRead != rhsRead) {
                return false;
            }

            if (lhsRead <= 0) {
                break;
            }

            if (!std::equal(lhsBuffer.begin(), lhsBuffer.begin() + lhsRead, rhsBuffer.begin())) {
                return false;
            }
        }

        return true;
    }

    bool TextMatchesFileContents(const fs::path& path, const std::string& contents) {
        std::string existingContents;
        return ReadTextFile(path, existingContents) && existingContents == contents;
    }

    bool CopyFile(const fs::path& source_path, const fs::path& destination_path) {
        std::error_code error;
        if (destination_path.has_parent_path()) {
            fs::create_directories(destination_path.parent_path(), error);
            if (error) {
                return false;
            }
        }

        error.clear();
        fs::copy_file(source_path, destination_path, fs::copy_options::overwrite_existing, error);
        return !error;
    }

    std::string BuildLuaScriptTemplate(const fs::path& script_relative_path) {
        const std::string componentName = script_relative_path.stem().string();
        return componentName + " = {\n"
            "\n"
            "\tOnStart = function(self)\n"
            "\t\t-- initialization logic here\n"
            "\tend,\n"
            "\n"
            "\tOnUpdate = function(self)\n"
            "\t\t-- per-frame logic here\n"
            "\tend,\n"
            "\n"
            "\tOnLateUpdate = function(self)\n"
            "\t\t-- late per-frame logic here\n"
            "\tend\n"
            "}\n";
    }

    std::string SerializeJsonDocument(const rapidjson::Document& document) {
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        return std::string(buffer.GetString(), buffer.GetSize());
    }

    std::string BuildEmptySceneDocumentText() {
        rapidjson::Document document;
        document.SetObject();
        document.AddMember("actors", rapidjson::kArrayType, document.GetAllocator());
        return SerializeJsonDocument(document);
    }

    std::string BuildDefaultFileContents(const fs::path& relative_path) {
        if (HasLuaExtension(relative_path)) {
            return BuildLuaScriptTemplate(relative_path);
        }
        if (HasSceneExtension(relative_path)) {
            return BuildEmptySceneDocumentText();
        }

        const std::string extension = relative_path.extension().string();
        if (extension == ".config" || extension == ".json" || extension == ".template") {
            return "{\n}\n";
        }

        return "";
    }

    bool CopyStringToBuffer(const std::string& value, char* buffer, std::size_t buffer_size) {
        if (buffer_size == 0) {
            return false;
        }

        std::snprintf(buffer, buffer_size, "%s", value.c_str());
        return true;
    }

    int DetectJsonTypeIndex(const rapidjson::Value& value) {
        if (value.IsNull()) {
            return 0;
        }
        if (value.IsBool()) {
            return 1;
        }
        if (value.IsInt64() || value.IsUint64()) {
            return 2;
        }
        if (value.IsNumber()) {
            return 3;
        }
        if (value.IsString()) {
            return 4;
        }

        return 4;
    }

    ImVec2 CalculateAvailableWindowSize(const ImVec2& margins) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        return ImVec2(
            std::max(240.0f, displaySize.x - margins.x),
            std::max(180.0f, displaySize.y - margins.y));
    }

    ImVec2 ClampWindowSizeToAvailable(const ImVec2& desiredSize, const ImVec2& availableSize) {
        return ImVec2(
            std::min(desiredSize.x, availableSize.x),
            std::min(desiredSize.y, availableSize.y));
    }

    std::string DescribeShadowChangeType(ResourcePaths::ShadowChangeType type) {
        switch (type) {
        case ResourcePaths::ShadowChangeType::Added:
            return "Added";
        case ResourcePaths::ShadowChangeType::Modified:
            return "Modified";
        case ResourcePaths::ShadowChangeType::Deleted:
            return "Deleted";
        }

        return "Changed";
    }
}

GameEditor::GameEditor(fs::path game_root_path)
    : game_root(std::move(game_root_path)) {
}

void GameEditor::ResetCurrentSceneState() {
    current_scene_document.SetObject();
    current_scene_document.AddMember(
        "actors",
        rapidjson::kArrayType,
        current_scene_document.GetAllocator());
    current_scene_name.clear();
    selected_actor_index = -1;
    scene_loaded = false;
}

void GameEditor::ResetCurrentScriptState() {
    current_script_relative_path.clear();
    current_script_text.clear();
    script_loaded = false;
}

void GameEditor::ResetCurrentTextResourceState() {
    current_text_resource_relative_path.clear();
    current_text_resource_text.clear();
    text_resource_loaded = false;
}

void GameEditor::RebuildResourceIndices() {
    resource_entries.clear();
    scene_names.clear();
    script_names.clear();
    shadow_changes = ResourcePaths::CollectShadowChanges();

    std::unordered_set<std::string> deleted_paths;
    for (const ResourcePaths::ShadowChange& change : shadow_changes) {
        if (change.type == ResourcePaths::ShadowChangeType::Deleted) {
            deleted_paths.insert(change.relative_path.generic_string());
        }
    }

    std::map<std::string, ResourceEntry> entries_by_path;

    const auto mark_parent_directories =
        [&entries_by_path](const fs::path& relative_path, bool mark_base, bool mark_shadow) {
        for (fs::path parent = relative_path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            ResourceEntry& parent_entry = entries_by_path[parent.generic_string()];
            parent_entry.relative_path = parent;
            parent_entry.is_directory = true;
            if (mark_base && fs::exists(ResourcePaths::GetBaseGameFilePath(parent))) {
                parent_entry.exists_in_base = true;
            }
            if (mark_shadow && fs::exists(ResourcePaths::GetShadowFilePath(parent))) {
                parent_entry.exists_in_shadow = true;
            }
        }
    };

    const auto scan_root = [&](const fs::path& root_path, bool is_shadow_root) {
        if (!fs::exists(root_path) || !fs::is_directory(root_path)) {
            return;
        }

        for (auto it = fs::recursive_directory_iterator(
                 root_path,
                 fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator();
             ++it) {
            const fs::directory_entry& entry = *it;

            std::error_code error;
            const fs::path relative_path = fs::relative(entry.path(), root_path, error);
            if (error || relative_path.empty()) {
                continue;
            }

            const auto first_part = relative_path.begin();
            if (is_shadow_root && first_part != relative_path.end() && *first_part == ".shadow_state") {
                if (entry.is_directory()) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (!entry.is_regular_file() && !entry.is_directory()) {
                continue;
            }

            if (deleted_paths.find(relative_path.generic_string()) != deleted_paths.end()) {
                if (entry.is_directory()) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            ResourceEntry& resource_entry = entries_by_path[relative_path.generic_string()];
            resource_entry.relative_path = relative_path;
            resource_entry.is_directory = entry.is_directory();
            if (is_shadow_root) {
                resource_entry.exists_in_shadow = true;
            }
            else {
                resource_entry.exists_in_base = true;
            }

            mark_parent_directories(relative_path, !is_shadow_root, is_shadow_root);
        }
    };

    scan_root(ResourcePaths::GetGameRoot(), false);
    scan_root(ResourcePaths::GetShadowGameRoot(), true);

    for (const std::string& deleted_path : deleted_paths) {
        entries_by_path.erase(deleted_path);
    }

    bool removed_directory = true;
    while (removed_directory) {
        removed_directory = false;
        std::unordered_map<std::string, int> child_counts;
        for (const auto& [path_key, entry] : entries_by_path) {
            const std::string parent_key = entry.relative_path.parent_path().generic_string();
            child_counts[parent_key] += 1;
            (void)path_key;
        }

        for (auto it = entries_by_path.begin(); it != entries_by_path.end();) {
            const ResourceEntry& entry = it->second;
            if (entry.is_directory &&
                child_counts[it->first] == 0 &&
                !entry.exists_in_shadow) {
                it = entries_by_path.erase(it);
                removed_directory = true;
                continue;
            }

            ++it;
        }
    }

    resource_entries.reserve(entries_by_path.size());
    for (const auto& [_, entry] : entries_by_path) {
        resource_entries.push_back(entry);
    }

    std::sort(resource_entries.begin(), resource_entries.end(),
        [](const ResourceEntry& lhs, const ResourceEntry& rhs) {
            return lhs.relative_path.generic_string() < rhs.relative_path.generic_string();
        });

    for (const ResourceEntry& entry : resource_entries) {
        if (entry.is_directory) {
            continue;
        }

        if (CanOpenInSceneTab(entry.relative_path)) {
            scene_names.push_back(entry.relative_path.stem().string());
        }
        if (CanOpenInLuaTab(entry.relative_path)) {
            const fs::path relative_script_path =
                entry.relative_path.lexically_relative(fs::path("component_types"));
            script_names.push_back(relative_script_path.generic_string());
        }
    }

    std::sort(scene_names.begin(), scene_names.end());
    std::sort(script_names.begin(), script_names.end());

    if (!selected_resource_path.empty() &&
        FindResourceEntry(selected_resource_path, selected_resource_is_directory) == nullptr) {
        const fs::path parent_path = selected_resource_path.parent_path();
        if (!parent_path.empty() && FindResourceEntry(parent_path, true) != nullptr) {
            selected_resource_path = parent_path;
        }
        else {
            selected_resource_path.clear();
        }
        selected_resource_is_directory = true;
    }

    if (!selected_shadow_change_path.empty() &&
        FindShadowChange(selected_shadow_change_path) == nullptr) {
        selected_shadow_change_path.clear();
    }
}

bool GameEditor::RefreshSceneList() {
    RebuildResourceIndices();
    RefreshSelectionsFromIndices(false, false, false);
    return !scene_names.empty();
}

bool GameEditor::RefreshScriptList() {
    RebuildResourceIndices();
    RefreshSelectionsFromIndices(false, false, false);
    return !script_names.empty();
}

void GameEditor::RefreshSelectionsFromIndices(
    bool reload_current_scene,
    bool reload_current_script,
    bool reload_current_text_resource) {
    if (!current_scene_name.empty()) {
        if (std::find(scene_names.begin(), scene_names.end(), current_scene_name) == scene_names.end()) {
            if (scene_names.empty()) {
                ResetCurrentSceneState();
            }
            else {
                LoadScene(scene_names.front(), false);
            }
        }
        else if (reload_current_scene) {
            LoadScene(current_scene_name, false);
        }
    }
    else if (scene_names.empty()) {
        ResetCurrentSceneState();
    }

    if (!current_script_relative_path.empty()) {
        if (std::find(script_names.begin(), script_names.end(), current_script_relative_path) == script_names.end()) {
            if (script_names.empty()) {
                ResetCurrentScriptState();
            }
            else {
                LoadScript(script_names.front(), false);
            }
        }
        else if (reload_current_script) {
            LoadScript(current_script_relative_path, false);
        }
    }
    else if (script_names.empty()) {
        ResetCurrentScriptState();
    }

    if (!current_text_resource_relative_path.empty()) {
        const fs::path relative_path(current_text_resource_relative_path);
        const ResourceEntry* text_entry = FindResourceEntry(relative_path, false);
        if (text_entry == nullptr || !IsTextEditableResourcePath(relative_path)) {
            ResetCurrentTextResourceState();
        }
        else if (reload_current_text_resource) {
            const fs::path resolved_path = ResourcePaths::ResolveExistingGameFile(relative_path);
            std::string reloaded_text;
            if (resolved_path.empty() || !ReadTextFile(resolved_path, reloaded_text)) {
                ResetCurrentTextResourceState();
            }
            else {
                current_text_resource_text = std::move(reloaded_text);
                current_text_resource_relative_path = relative_path.generic_string();
                text_resource_loaded = true;
            }
        }
    }
}

const GameEditor::ResourceEntry* GameEditor::FindResourceEntry(
    const fs::path& relative_path,
    bool is_directory) const {
    const std::string relative_key = relative_path.generic_string();
    for (const ResourceEntry& entry : resource_entries) {
        if (entry.is_directory == is_directory &&
            entry.relative_path.generic_string() == relative_key) {
            return &entry;
        }
    }

    return nullptr;
}

const ResourcePaths::ShadowChange* GameEditor::FindShadowChange(
    const fs::path& relative_path) const {
    const std::string relative_key = relative_path.generic_string();
    for (const ResourcePaths::ShadowChange& change : shadow_changes) {
        if (change.relative_path.generic_string() == relative_key) {
            return &change;
        }
    }

    return nullptr;
}

bool GameEditor::IsRelativePathUnder(
    const fs::path& relative_path,
    const fs::path& root_path) {
    if (root_path.empty()) {
        return true;
    }

    auto root_it = root_path.begin();
    auto path_it = relative_path.begin();
    for (; root_it != root_path.end(); ++root_it, ++path_it) {
        if (path_it == relative_path.end() || *path_it != *root_it) {
            return false;
        }
    }

    return true;
}

bool GameEditor::CanOpenInSceneTab(const fs::path& relative_path) {
    return relative_path.parent_path() == fs::path("scenes") && HasSceneExtension(relative_path);
}

bool GameEditor::CanOpenInLuaTab(const fs::path& relative_path) {
    return IsRelativePathUnder(relative_path, fs::path("component_types")) &&
        HasLuaExtension(relative_path);
}

bool GameEditor::IsTextEditableResourcePath(const fs::path& relative_path) {
    if (CanOpenInSceneTab(relative_path) || CanOpenInLuaTab(relative_path)) {
        return false;
    }

    return HasTextEditingExtension(relative_path);
}

void GameEditor::SelectResource(const fs::path& relative_path, bool is_directory) {
    selected_resource_path = relative_path;
    selected_resource_is_directory = is_directory;

    if (is_directory || relative_path.empty()) {
        ResetCurrentTextResourceState();
        return;
    }

    if (IsTextEditableResourcePath(relative_path)) {
        LoadTextResource(relative_path, false);
    }
    else {
        ResetCurrentTextResourceState();
    }
}

fs::path GameEditor::GetSelectedResourceDirectory() const {
    if (selected_resource_path.empty()) {
        return {};
    }

    return selected_resource_is_directory
        ? selected_resource_path
        : selected_resource_path.parent_path();
}

bool GameEditor::LoadScene(const std::string& scene_name, bool update_status) {
    const fs::path relative_scene_path = fs::path("scenes") / fs::path(SceneFilename(scene_name));
    const fs::path resolved_path = ResourcePaths::ResolveExistingGameFile(relative_scene_path);
    if (resolved_path.empty()) {
        ResetCurrentSceneState();
        if (update_status) {
            status_message = "Scene file is missing: " + scene_name;
        }
        return false;
    }

    Document document;
    EngineUtils::ReadJsonFile(resolved_path.string(), document);
    if (!document.IsObject()) {
        ResetCurrentSceneState();
        if (update_status) {
            status_message = "Scene file is not a valid JSON object: " + scene_name;
        }
        return false;
    }
    if (!document.HasMember("actors") || !document["actors"].IsArray()) {
        document.RemoveAllMembers();
        document.SetObject();
        document.AddMember("actors", rapidjson::kArrayType, document.GetAllocator());
    }

    current_scene_document.CopyFrom(document, current_scene_document.GetAllocator());
    current_scene_name = scene_name;
    selected_actor_index = current_scene_document["actors"].Empty() ? -1 : 0;
    scene_loaded = true;
    SelectResource(relative_scene_path, false);
    if (update_status) {
        status_message = "Loaded scene: " + scene_name;
    }
    return true;
}

bool GameEditor::LoadScript(const std::string& script_relative_path, bool update_status) {
    const fs::path relative_script_path = fs::path("component_types") / fs::path(script_relative_path);
    const fs::path resolved_path = ResourcePaths::ResolveExistingGameFile(relative_script_path);
    if (resolved_path.empty()) {
        ResetCurrentScriptState();
        if (update_status) {
            status_message = "Lua script is missing: " + script_relative_path;
        }
        return false;
    }

    std::string script_text;
    if (!ReadTextFile(resolved_path, script_text)) {
        ResetCurrentScriptState();
        if (update_status) {
            status_message = "Failed to read Lua script: " + resolved_path.lexically_normal().string();
        }
        return false;
    }

    current_script_relative_path = script_relative_path;
    current_script_text = std::move(script_text);
    script_loaded = true;
    SelectResource(relative_script_path, false);
    if (update_status) {
        status_message = "Loaded script: " + script_relative_path;
    }
    return true;
}

bool GameEditor::LoadTextResource(const fs::path& relative_path, bool update_status) {
    if (!IsTextEditableResourcePath(relative_path)) {
        ResetCurrentTextResourceState();
        if (update_status) {
            status_message = "That file opens through a dedicated editor or is not editable as text.";
        }
        return false;
    }

    const fs::path resolved_path = ResourcePaths::ResolveExistingGameFile(relative_path);
    if (resolved_path.empty()) {
        ResetCurrentTextResourceState();
        if (update_status) {
            status_message = "Resource file is missing: " + relative_path.generic_string();
        }
        return false;
    }

    std::string text_contents;
    if (!ReadTextFile(resolved_path, text_contents)) {
        ResetCurrentTextResourceState();
        if (update_status) {
            status_message = "Failed to read resource file: " + resolved_path.lexically_normal().string();
        }
        return false;
    }

    current_text_resource_relative_path = relative_path.generic_string();
    current_text_resource_text = std::move(text_contents);
    text_resource_loaded = true;
    if (update_status) {
        status_message = "Opened resource file: " + relative_path.generic_string();
    }
    return true;
}

bool GameEditor::SaveCurrentSceneToShadow() {
    if (!scene_loaded || current_scene_name.empty()) {
        return false;
    }

    const fs::path relative_scene_path = fs::path("scenes") / fs::path(SceneFilename(current_scene_name));
    const std::string serialized_scene = SerializeJsonDocument(current_scene_document);
    std::string error_message;

    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_scene_path);
    if (ResourcePaths::BasePathExists(relative_scene_path) &&
        TextMatchesFileContents(base_path, serialized_scene)) {
        if (!ResourcePaths::DiscardShadowChange(relative_scene_path, error_message)) {
            status_message = error_message;
            return false;
        }

        RebuildResourceIndices();
        status_message = "Scene matches the main resources again: " + current_scene_name;
        return true;
    }

    if (!ResourcePaths::ClearDeletionMarker(relative_scene_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_scene_path);
    if (!WriteTextFile(shadow_path, serialized_scene)) {
        status_message = "Failed to save scene into shadow resources: " + shadow_path.lexically_normal().string();
        return false;
    }

    RebuildResourceIndices();
    status_message = "Staged scene changes in shadow: " + relative_scene_path.generic_string();
    return true;
}

bool GameEditor::SaveCurrentScriptToShadow() {
    if (!script_loaded || current_script_relative_path.empty()) {
        return false;
    }

    const fs::path relative_script_path =
        fs::path("component_types") / fs::path(current_script_relative_path);
    std::string error_message;

    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_script_path);
    if (ResourcePaths::BasePathExists(relative_script_path) &&
        TextMatchesFileContents(base_path, current_script_text)) {
        if (!ResourcePaths::DiscardShadowChange(relative_script_path, error_message)) {
            status_message = error_message;
            return false;
        }

        RebuildResourceIndices();
        status_message = "Lua script matches the main resources again: " + current_script_relative_path;
        return true;
    }

    if (!ResourcePaths::ClearDeletionMarker(relative_script_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_script_path);
    if (!WriteTextFile(shadow_path, current_script_text)) {
        status_message = "Failed to save Lua script into shadow resources: " + shadow_path.lexically_normal().string();
        return false;
    }

    RebuildResourceIndices();
    status_message = "Staged Lua changes in shadow: " + current_script_relative_path;
    return true;
}

bool GameEditor::SaveCurrentTextResourceToShadow() {
    if (!text_resource_loaded || current_text_resource_relative_path.empty()) {
        return false;
    }

    const fs::path relative_path(current_text_resource_relative_path);
    std::string error_message;

    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_path);
    if (ResourcePaths::BasePathExists(relative_path) &&
        TextMatchesFileContents(base_path, current_text_resource_text)) {
        if (!ResourcePaths::DiscardShadowChange(relative_path, error_message)) {
            status_message = error_message;
            return false;
        }

        RebuildResourceIndices();
        status_message = "Resource matches the main folder again: " + current_text_resource_relative_path;
        return true;
    }

    if (!ResourcePaths::ClearDeletionMarker(relative_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_path);
    if (!WriteTextFile(shadow_path, current_text_resource_text)) {
        status_message = "Failed to save resource into shadow: " + shadow_path.lexically_normal().string();
        return false;
    }

    RebuildResourceIndices();
    status_message = "Staged resource changes in shadow: " + current_text_resource_relative_path;
    return true;
}

bool GameEditor::SaveCurrentSceneToGame() {
    if (!scene_loaded || current_scene_name.empty()) {
        return false;
    }

    const fs::path relative_scene_path = fs::path("scenes") / fs::path(SceneFilename(current_scene_name));
    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_scene_path);
    const std::string serialized_scene = SerializeJsonDocument(current_scene_document);
    if (!WriteTextFile(base_path, serialized_scene)) {
        status_message = "Failed to merge scene into main resources: " + base_path.lexically_normal().string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::DiscardShadowChange(relative_scene_path, error_message)) {
        status_message = error_message;
        return false;
    }

    RebuildResourceIndices();
    status_message = "Merged scene into main resources: " + current_scene_name;
    return true;
}

bool GameEditor::SaveCurrentScriptToGame() {
    if (!script_loaded || current_script_relative_path.empty()) {
        return false;
    }

    const fs::path relative_script_path =
        fs::path("component_types") / fs::path(current_script_relative_path);
    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_script_path);
    if (!WriteTextFile(base_path, current_script_text)) {
        status_message = "Failed to merge Lua script into main resources: " + base_path.lexically_normal().string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::DiscardShadowChange(relative_script_path, error_message)) {
        status_message = error_message;
        return false;
    }

    RebuildResourceIndices();
    status_message = "Merged Lua script into main resources: " + current_script_relative_path;
    return true;
}

bool GameEditor::MergeShadowChangeToGame(fs::path relative_path) {
    if (FindShadowChange(relative_path) == nullptr) {
        status_message = "No shadow change exists for " + relative_path.generic_string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::PromoteShadowChangeToBase(relative_path, error_message)) {
        status_message = error_message;
        return false;
    }

    RebuildResourceIndices();
    RefreshSelectionsFromIndices(false, false, false);
    status_message = "Merged shadow change into main resources: " + relative_path.generic_string();
    return true;
}

bool GameEditor::DiscardShadowChange(fs::path relative_path) {
    if (FindShadowChange(relative_path) == nullptr) {
        status_message = "No shadow change exists for " + relative_path.generic_string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::DiscardShadowChange(relative_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const bool reload_scene =
        current_scene_name == relative_path.stem().string() && relative_path.parent_path() == fs::path("scenes");
    const bool reload_script =
        CanOpenInLuaTab(relative_path) &&
        current_script_relative_path ==
        relative_path.lexically_relative(fs::path("component_types")).generic_string();
    const bool reload_text =
        current_text_resource_relative_path == relative_path.generic_string();

    RebuildResourceIndices();
    RefreshSelectionsFromIndices(reload_scene, reload_script, reload_text);
    status_message = "Discarded shadow change: " + relative_path.generic_string();
    return true;
}

bool GameEditor::MergeSelectedResourceToGame() {
    if (selected_resource_is_directory || selected_resource_path.empty()) {
        status_message = "Select a file with a shadow change to merge it.";
        return false;
    }

    return MergeShadowChangeToGame(selected_resource_path);
}

bool GameEditor::DiscardSelectedResourceChange() {
    if (selected_resource_is_directory || selected_resource_path.empty()) {
        status_message = "Select a file with a shadow change to discard it.";
        return false;
    }

    return DiscardShadowChange(selected_resource_path);
}

bool GameEditor::DiscardCurrentSceneShadow() {
    if (!scene_loaded || current_scene_name.empty()) {
        return false;
    }

    return DiscardShadowChange(fs::path("scenes") / fs::path(SceneFilename(current_scene_name)));
}

bool GameEditor::DiscardCurrentScriptShadow() {
    if (!script_loaded || current_script_relative_path.empty()) {
        return false;
    }

    return DiscardShadowChange(
        fs::path("component_types") / fs::path(current_script_relative_path));
}

bool GameEditor::CreateScene(const std::string& raw_scene_name) {
    fs::path scene_relative_path;
    if (!TryBuildNewFileRelativePath(raw_scene_name, ".scene", false, scene_relative_path)) {
        status_message = "Enter a valid scene name. Use a simple name and optional `.scene` extension.";
        return false;
    }

    const fs::path relative_scene_path = fs::path("scenes") / scene_relative_path;
    if (ResourcePaths::ResolveExistingGameFile(relative_scene_path).empty() == false) {
        status_message = "Scene already exists: " + scene_relative_path.filename().string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::ClearDeletionMarker(relative_scene_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_scene_path);
    if (!WriteTextFile(shadow_path, BuildEmptySceneDocumentText())) {
        status_message = "Failed to create scene in shadow: " + shadow_path.lexically_normal().string();
        return false;
    }

    new_scene_name[0] = '\0';
    RebuildResourceIndices();
    LoadScene(scene_relative_path.stem().string(), false);
    status_message = "Created scene in shadow: " + relative_scene_path.generic_string();
    return true;
}

bool GameEditor::DeleteScene(std::string scene_name) {
    if (scene_name.empty()) {
        return false;
    }

    const fs::path relative_scene_path = fs::path("scenes") / fs::path(SceneFilename(scene_name));
    if (ResourcePaths::ResolveExistingGameFile(relative_scene_path).empty() &&
        !ResourcePaths::IsDeletedInShadow(relative_scene_path)) {
        status_message = "Scene is already missing: " + scene_name;
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::MarkDeletedInShadow(relative_scene_path, error_message)) {
        status_message = error_message;
        return false;
    }

    pending_scene_delete_name.clear();
    RebuildResourceIndices();
    RefreshSelectionsFromIndices(false, false, false);
    status_message = "Deleted scene in shadow: " + scene_name;
    return true;
}

bool GameEditor::CreateScript(const std::string& raw_script_name) {
    fs::path script_relative_path;
    if (!TryBuildNewFileRelativePath(raw_script_name, ".lua", false, script_relative_path)) {
        status_message = "Enter a valid Lua file name. Use a simple name and optional `.lua` extension.";
        return false;
    }

    const fs::path relative_script_path = fs::path("component_types") / script_relative_path;
    if (!ResourcePaths::ResolveExistingGameFile(relative_script_path).empty()) {
        status_message = "Lua script already exists: " + script_relative_path.filename().string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::ClearDeletionMarker(relative_script_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_script_path);
    if (!WriteTextFile(shadow_path, BuildLuaScriptTemplate(script_relative_path))) {
        status_message = "Failed to create Lua script in shadow: " + shadow_path.lexically_normal().string();
        return false;
    }

    new_script_name[0] = '\0';
    RebuildResourceIndices();
    LoadScript(script_relative_path.generic_string(), false);
    status_message = "Created Lua script in shadow: " + relative_script_path.generic_string();
    return true;
}

bool GameEditor::DeleteScript(std::string script_relative_path) {
    if (script_relative_path.empty()) {
        return false;
    }

    const fs::path relative_script_path = fs::path("component_types") / fs::path(script_relative_path);
    if (ResourcePaths::ResolveExistingGameFile(relative_script_path).empty() &&
        !ResourcePaths::IsDeletedInShadow(relative_script_path)) {
        status_message = "Lua script is already missing: " + script_relative_path;
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::MarkDeletedInShadow(relative_script_path, error_message)) {
        status_message = error_message;
        return false;
    }

    pending_script_delete_name.clear();
    RebuildResourceIndices();
    RefreshSelectionsFromIndices(false, false, false);
    status_message = "Deleted Lua script in shadow: " + script_relative_path;
    return true;
}

bool GameEditor::CreateResourceFile(const std::string& raw_resource_name) {
    const std::string trimmed_name = TrimWhitespace(raw_resource_name);
    if (trimmed_name.empty()) {
        status_message = "Enter a file name to create.";
        return false;
    }

    const fs::path input_path(trimmed_name);
    if (!IsContainedRelativePath(input_path) || input_path.filename().empty()) {
        status_message = "Enter a valid relative file name for the selected folder.";
        return false;
    }

    const fs::path relative_path = (GetSelectedResourceDirectory() / input_path).lexically_normal();
    if (!IsContainedRelativePath(relative_path) || relative_path.filename().empty()) {
        status_message = "The requested file path is outside the game resources.";
        return false;
    }

    if (FindResourceEntry(relative_path, false) != nullptr ||
        FindResourceEntry(relative_path, true) != nullptr) {
        status_message = "A resource already exists at " + relative_path.generic_string();
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::ClearDeletionMarker(relative_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const std::string contents = BuildDefaultFileContents(relative_path);
    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_path);
    if (!WriteTextFile(shadow_path, contents)) {
        status_message = "Failed to create resource file: " + shadow_path.lexically_normal().string();
        return false;
    }

    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_path);
    if (ResourcePaths::BasePathExists(relative_path) && TextMatchesFileContents(base_path, contents)) {
        if (!ResourcePaths::DiscardShadowChange(relative_path, error_message)) {
            status_message = error_message;
            return false;
        }
    }

    new_resource_name[0] = '\0';
    RebuildResourceIndices();
    SelectResource(relative_path, false);
    status_message = "Created resource file in shadow: " + relative_path.generic_string();
    return true;
}

bool GameEditor::CreateResourceDirectory(const std::string& raw_directory_name) {
    const std::string trimmed_name = TrimWhitespace(raw_directory_name);
    if (trimmed_name.empty()) {
        status_message = "Enter a folder name to create.";
        return false;
    }

    const fs::path input_path(trimmed_name);
    if (!IsContainedRelativePath(input_path)) {
        status_message = "Enter a valid relative folder name for the selected location.";
        return false;
    }

    const fs::path relative_path = (GetSelectedResourceDirectory() / input_path).lexically_normal();
    if (!IsContainedRelativePath(relative_path) || relative_path.empty()) {
        status_message = "The requested folder path is outside the game resources.";
        return false;
    }

    if (FindResourceEntry(relative_path, true) != nullptr || FindResourceEntry(relative_path, false) != nullptr) {
        status_message = "A resource already exists at " + relative_path.generic_string();
        return false;
    }

    std::error_code error;
    fs::create_directories(ResourcePaths::GetShadowFilePath(relative_path), error);
    if (error) {
        status_message = "Failed to create shadow folder: " + relative_path.generic_string();
        return false;
    }

    new_resource_name[0] = '\0';
    RebuildResourceIndices();
    SelectResource(relative_path, true);
    status_message = "Created shadow folder: " + relative_path.generic_string();
    return true;
}

bool GameEditor::DeleteSelectedResource() {
    if (selected_resource_path.empty()) {
        status_message = "Select a resource to delete.";
        return false;
    }

    if (!selected_resource_is_directory) {
        std::string error_message;
        if (!ResourcePaths::MarkDeletedInShadow(selected_resource_path, error_message)) {
            status_message = error_message;
            return false;
        }

        const fs::path deleted_path = selected_resource_path;
        selected_resource_path = deleted_path.parent_path();
        selected_resource_is_directory = true;

        RebuildResourceIndices();
        RefreshSelectionsFromIndices(false, false, false);
        status_message = "Deleted resource in shadow: " + deleted_path.generic_string();
        return true;
    }

    std::vector<fs::path> descendant_files;
    for (const ResourceEntry& entry : resource_entries) {
        if (!entry.is_directory &&
            IsRelativePathUnder(entry.relative_path, selected_resource_path)) {
            descendant_files.push_back(entry.relative_path);
        }
    }

    std::string error_message;
    for (const fs::path& file_path : descendant_files) {
        if (!ResourcePaths::MarkDeletedInShadow(file_path, error_message)) {
            status_message = error_message;
            return false;
        }
    }

    std::error_code error;
    const fs::path shadow_directory_path = ResourcePaths::GetShadowFilePath(selected_resource_path);
    if (fs::exists(shadow_directory_path)) {
        fs::remove_all(shadow_directory_path, error);
        if (error) {
            status_message = "Failed to remove shadow folder: " + selected_resource_path.generic_string();
            return false;
        }
    }

    const fs::path deleted_directory = selected_resource_path;
    selected_resource_path = deleted_directory.parent_path();
    selected_resource_is_directory = true;
    ResetCurrentTextResourceState();

    RebuildResourceIndices();
    RefreshSelectionsFromIndices(false, false, false);
    status_message = "Deleted folder contents in shadow: " + deleted_directory.generic_string();
    return true;
}

bool GameEditor::ImportExternalFile(const fs::path& source_path) {
    if (!fs::exists(source_path) || !fs::is_regular_file(source_path)) {
        status_message = "Dropped item is not a regular file: " + source_path.lexically_normal().string();
        return false;
    }

    const fs::path relative_path =
        (GetSelectedResourceDirectory() / source_path.filename()).lexically_normal();
    if (!IsContainedRelativePath(relative_path)) {
        status_message = "Cannot import files outside the selected game's resource folder.";
        return false;
    }

    std::string error_message;
    if (!ResourcePaths::ClearDeletionMarker(relative_path, error_message)) {
        status_message = error_message;
        return false;
    }

    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(relative_path);
    if (ResourcePaths::BasePathExists(relative_path) && FilesAreBinaryEqual(base_path, source_path)) {
        if (!ResourcePaths::DiscardShadowChange(relative_path, error_message)) {
            status_message = error_message;
            return false;
        }

        RebuildResourceIndices();
        SelectResource(relative_path, false);
        status_message = "Imported file matches the main resources, so no shadow copy was needed: " +
            relative_path.generic_string();
        return true;
    }

    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(relative_path);
    if (!CopyFile(source_path, shadow_path)) {
        status_message = "Failed to import file into shadow: " + shadow_path.lexically_normal().string();
        return false;
    }

    RebuildResourceIndices();
    SelectResource(relative_path, false);
    RefreshSelectionsFromIndices(
        CanOpenInSceneTab(relative_path) && current_scene_name == relative_path.stem().string(),
        CanOpenInLuaTab(relative_path) &&
            current_script_relative_path ==
            relative_path.lexically_relative(fs::path("component_types")).generic_string(),
        current_text_resource_relative_path == relative_path.generic_string());
    status_message = "Imported file into shadow: " + relative_path.generic_string();
    return true;
}

bool GameEditor::OpenSelectedResourceInDedicatedEditor() {
    if (selected_resource_is_directory || selected_resource_path.empty()) {
        return false;
    }

    if (CanOpenInSceneTab(selected_resource_path)) {
        pending_tab_selection_target = EditorTab::Scene;
        pending_tab_selection = true;
        return LoadScene(selected_resource_path.stem().string(), true);
    }

    if (CanOpenInLuaTab(selected_resource_path)) {
        const std::string relative_script_path =
            selected_resource_path.lexically_relative(fs::path("component_types")).generic_string();
        pending_tab_selection_target = EditorTab::LuaScripts;
        pending_tab_selection = true;
        return LoadScript(relative_script_path, true);
    }

    status_message = "That resource does not have a dedicated editor.";
    return false;
}

GameEditor::Result GameEditor::Run(Renderer& renderer, ImGuiLayer& imguiLayer) {
    ResourcePaths::SetGameRoot(game_root);
    RebuildResourceIndices();

    if (!scene_names.empty()) {
        LoadScene(scene_names.front(), false);
    }
    else {
        ResetCurrentSceneState();
        if (!script_names.empty()) {
            active_tab = EditorTab::LuaScripts;
            status_message = "No scenes were found for this game. Lua editing is still available.";
        }
        else {
            active_tab = EditorTab::Resources;
            status_message = "No scenes or Lua scripts were found. The full resource browser is still available.";
        }
    }

    pending_tab_selection_target = active_tab;

    Result result = Result::BackToLauncher;
    bool editor_running = true;
    while (editor_running) {
        SDL_Event event;
        while (Helper::SDL_PollEvent(&event)) {
            imguiLayer.ProcessEvent(event);
            if (event.type == SDL_QUIT) {
                return Result::QuitApplication;
            }
            if (event.type == SDL_DROPFILE) {
                if (event.drop.file != nullptr) {
                    ImportExternalFile(fs::path(event.drop.file));
                    SDL_free(event.drop.file);
                }
            }
        }

        renderer.BeginFrame(21, 23, 29);
        imguiLayer.BeginFrame();

        if (!RenderEditorUI(result)) {
            editor_running = false;
        }

        imguiLayer.Render(renderer.GetSDLRenderer());
        renderer.Present();
    }

    return result;
}

bool GameEditor::RenderEditorUI(Result& result) {
    bool sceneDataChanged = false;
    bool scriptDataChanged = false;
    bool resourceDataChanged = false;

    const ImVec2 availableSize = CalculateAvailableWindowSize(ImVec2(40.0f, 40.0f));
    const ImVec2 minimumSize =
        ClampWindowSizeToAvailable(ImVec2(1220.0f, 760.0f), availableSize);
    const ImVec2 initialSize =
        ClampWindowSizeToAvailable(ImVec2(1380.0f, 860.0f), availableSize);
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(minimumSize, availableSize);
    ImGui::SetNextWindowSize(initialSize, ImGuiCond_Appearing);
    if (ImGui::Begin(
        "Game Editor",
        nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::Button("Back To Launcher")) {
                result = Result::BackToLauncher;
                ImGui::EndMenuBar();
                ImGui::End();
                return false;
            }

            ImGui::SameLine();
            bool can_merge_current_item = false;
            bool can_discard_current_item = false;
            if (active_tab == EditorTab::Scene) {
                can_merge_current_item = scene_loaded && !current_scene_name.empty();
                can_discard_current_item =
                    FindShadowChange(fs::path("scenes") / fs::path(SceneFilename(current_scene_name))) != nullptr;
            }
            else if (active_tab == EditorTab::LuaScripts) {
                can_merge_current_item = script_loaded && !current_script_relative_path.empty();
                can_discard_current_item =
                    FindShadowChange(
                        fs::path("component_types") / fs::path(current_script_relative_path)) != nullptr;
            }
            else if (active_tab == EditorTab::Resources) {
                can_merge_current_item =
                    !selected_resource_is_directory &&
                    !selected_resource_path.empty() &&
                    FindShadowChange(selected_resource_path) != nullptr;
                can_discard_current_item = can_merge_current_item;
            }
            else if (active_tab == EditorTab::Changes) {
                can_merge_current_item = !selected_shadow_change_path.empty();
                can_discard_current_item = can_merge_current_item;
            }

            if (!can_merge_current_item) {
                ImGui::BeginDisabled();
            }
            if (active_tab == EditorTab::Scene) {
                if (ImGui::Button("Merge Scene To Main")) {
                    SaveCurrentSceneToGame();
                }
            }
            else if (active_tab == EditorTab::LuaScripts) {
                if (ImGui::Button("Merge Script To Main")) {
                    SaveCurrentScriptToGame();
                }
            }
            else if (active_tab == EditorTab::Resources) {
                if (ImGui::Button("Merge Resource To Main")) {
                    MergeSelectedResourceToGame();
                }
            }
            else if (active_tab == EditorTab::Changes) {
                if (ImGui::Button("Merge Selected Change")) {
                    MergeShadowChangeToGame(selected_shadow_change_path);
                }
            }
            if (!can_merge_current_item) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!can_discard_current_item) {
                ImGui::BeginDisabled();
            }
            if (active_tab == EditorTab::Scene) {
                if (ImGui::Button("Discard Scene Shadow")) {
                    DiscardCurrentSceneShadow();
                }
            }
            else if (active_tab == EditorTab::LuaScripts) {
                if (ImGui::Button("Discard Script Shadow")) {
                    DiscardCurrentScriptShadow();
                }
            }
            else if (active_tab == EditorTab::Resources) {
                if (ImGui::Button("Discard Resource Shadow")) {
                    DiscardSelectedResourceChange();
                }
            }
            else if (active_tab == EditorTab::Changes) {
                if (ImGui::Button("Discard Selected Change")) {
                    DiscardShadowChange(selected_shadow_change_path);
                }
            }
            if (!can_discard_current_item) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("Main: %s", ResourcePaths::GetGameRoot().lexically_normal().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("Shadow: %s", ResourcePaths::GetShadowGameRoot().lexically_normal().string().c_str());
            ImGui::EndMenuBar();
        }

        if (!status_message.empty()) {
            ImGui::TextWrapped("%s", status_message.c_str());
            ImGui::Separator();
        }

        if (ImGui::BeginTabBar("editor_tabs")) {
            const EditorTab requested_tab = pending_tab_selection
                ? pending_tab_selection_target
                : active_tab;
            const ImGuiTabItemFlags sceneTabFlags =
                pending_tab_selection && requested_tab == EditorTab::Scene
                ? ImGuiTabItemFlags_SetSelected
                : 0;
            const ImGuiTabItemFlags scriptTabFlags =
                pending_tab_selection && requested_tab == EditorTab::LuaScripts
                ? ImGuiTabItemFlags_SetSelected
                : 0;
            const ImGuiTabItemFlags resourcesTabFlags =
                pending_tab_selection && requested_tab == EditorTab::Resources
                ? ImGuiTabItemFlags_SetSelected
                : 0;
            const ImGuiTabItemFlags changesTabFlags =
                pending_tab_selection && requested_tab == EditorTab::Changes
                ? ImGuiTabItemFlags_SetSelected
                : 0;
            EditorTab rendered_tab = active_tab;
            bool rendered_any_tab = false;

            if (ImGui::BeginTabItem("Scenes", nullptr, sceneTabFlags)) {
                rendered_tab = EditorTab::Scene;
                rendered_any_tab = true;
                sceneDataChanged |= RenderSceneEditorTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Lua Scripts", nullptr, scriptTabFlags)) {
                rendered_tab = EditorTab::LuaScripts;
                rendered_any_tab = true;
                scriptDataChanged |= RenderLuaScriptTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Resources", nullptr, resourcesTabFlags)) {
                rendered_tab = EditorTab::Resources;
                rendered_any_tab = true;
                resourceDataChanged |= RenderResourceBrowserTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Shadow Changes", nullptr, changesTabFlags)) {
                rendered_tab = EditorTab::Changes;
                rendered_any_tab = true;
                RenderShadowChangesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            if (rendered_any_tab) {
                active_tab = rendered_tab;
            }
            if (pending_tab_selection && rendered_any_tab && rendered_tab == requested_tab) {
                pending_tab_selection = false;
            }
        }
    }
    ImGui::End();

    if (sceneDataChanged) {
        SaveCurrentSceneToShadow();
    }
    if (scriptDataChanged) {
        SaveCurrentScriptToShadow();
    }
    if (resourceDataChanged) {
        SaveCurrentTextResourceToShadow();
    }

    return true;
}

bool GameEditor::RenderSceneEditorTab() {
    bool changed = false;

    ImGui::Columns(3, "editor_columns", true);
    changed |= RenderScenePanel();
    ImGui::NextColumn();
    changed |= RenderActorPanel();
    ImGui::NextColumn();
    changed |= RenderInspectorPanel();
    ImGui::Columns(1);

    return changed;
}

bool GameEditor::RenderScenePanel() {
    bool changed = false;

    ImGui::TextUnformatted("Scenes");
    ImGui::Separator();
    ImGui::InputText("New Scene Name", new_scene_name, IM_ARRAYSIZE(new_scene_name));
    if (ImGui::Button("Create Scene")) {
        CreateScene(new_scene_name);
    }
    ImGui::SameLine();
    const bool canDeleteScene = scene_loaded && !current_scene_name.empty();
    if (!canDeleteScene) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Selected Scene")) {
        pending_scene_delete_name = current_scene_name;
        ImGui::OpenPopup("Delete Scene##confirm");
    }
    if (!canDeleteScene) {
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Creates files under scenes/. `.scene` is optional.");

    if (ImGui::BeginPopupModal("Delete Scene##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Delete scene `%s` in the shadow layer? The main resources file will stay untouched until you merge the deletion.",
            pending_scene_delete_name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            const std::string scene_name_to_delete = pending_scene_delete_name;
            DeleteScene(scene_name_to_delete);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return changed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pending_scene_delete_name.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return changed;
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    if (scene_names.empty()) {
        ImGui::TextWrapped("No visible scenes were discovered in this game folder.");
    }
    else {
        for (const std::string& sceneName : scene_names) {
            const bool hasShadow = fs::exists(ResourcePaths::GetShadowFilePath(
                fs::path("scenes") / fs::path(SceneFilename(sceneName))));
            std::string label = SceneDisplayName(sceneName);
            if (hasShadow) {
                label += " *";
            }

            if (ImGui::Selectable(label.c_str(), current_scene_name == sceneName)) {
                LoadScene(sceneName);
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Refresh Scene List")) {
        RebuildResourceIndices();
        RefreshSelectionsFromIndices(false, false, false);
        changed = false;
    }

    return changed;
}

bool GameEditor::RenderLuaScriptTab() {
    if (current_script_relative_path.empty() && !script_names.empty()) {
        LoadScript(script_names.front());
    }

    bool changed = false;

    ImGui::Columns(2, "lua_script_columns", true);
    changed |= RenderLuaScriptListPanel();
    ImGui::NextColumn();
    changed |= RenderLuaScriptEditorPanel();
    ImGui::Columns(1);

    return changed;
}

bool GameEditor::RenderLuaScriptListPanel() {
    ImGui::TextUnformatted("Lua Scripts");
    ImGui::Separator();
    ImGui::InputText("New Lua File", new_script_name, IM_ARRAYSIZE(new_script_name));
    if (ImGui::Button("Create Lua File")) {
        CreateScript(new_script_name);
    }
    ImGui::SameLine();
    const bool canDeleteScript = script_loaded && !current_script_relative_path.empty();
    if (!canDeleteScript) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Selected Lua")) {
        pending_script_delete_name = current_script_relative_path;
        ImGui::OpenPopup("Delete Lua Script##confirm");
    }
    if (!canDeleteScript) {
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Editing scripts from component_types/");
    ImGui::TextDisabled("Creates files under component_types/. `.lua` is optional.");

    if (ImGui::BeginPopupModal("Delete Lua Script##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Delete Lua file `%s` in the shadow layer? The main resources file will stay untouched until you merge the deletion.",
            pending_script_delete_name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            const std::string script_path_to_delete = pending_script_delete_name;
            DeleteScript(script_path_to_delete);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pending_script_delete_name.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return false;
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    if (script_names.empty()) {
        ImGui::TextWrapped("No visible Lua component scripts were discovered in this game folder.");
    }
    else {
        for (const std::string& scriptRelativePath : script_names) {
            const bool hasShadow = fs::exists(ResourcePaths::GetShadowFilePath(
                fs::path("component_types") / fs::path(scriptRelativePath)));
            std::string label = ScriptDisplayName(scriptRelativePath);
            if (hasShadow) {
                label += " *";
            }

            if (ImGui::Selectable(label.c_str(), current_script_relative_path == scriptRelativePath)) {
                LoadScript(scriptRelativePath);
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Refresh Script List")) {
        RebuildResourceIndices();
        RefreshSelectionsFromIndices(false, false, false);
    }

    return false;
}

bool GameEditor::RenderLuaScriptEditorPanel() {
    ImGui::TextUnformatted("Lua Editor");
    ImGui::Separator();

    if (script_names.empty()) {
        ImGui::TextWrapped("Add a `.lua` file under this game's `component_types` folder to edit it here.");
        return false;
    }

    if (!script_loaded || current_script_relative_path.empty()) {
        ImGui::TextWrapped("Select a Lua script to edit it.");
        return false;
    }

    const fs::path relativeScriptPath =
        fs::path("component_types") / fs::path(current_script_relative_path);
    const bool hasShadow = fs::exists(ResourcePaths::GetShadowFilePath(relativeScriptPath));

    ImGui::Text("Script: %s", current_script_relative_path.c_str());
    ImGui::TextDisabled("%s", hasShadow
        ? "Play mode will use the shadow version of this script."
        : "Play mode is currently using the version from the main resource folder.");
    ImGui::Spacing();

    const ImVec2 editorSize = ImVec2(-FLT_MIN, -FLT_MIN);
    return ImGui::InputTextMultiline(
        "##lua_script_editor",
        &current_script_text,
        editorSize,
        ImGuiInputTextFlags_AllowTabInput);
}

bool GameEditor::RenderResourceBrowserTab() {
    bool changed = false;

    ImGui::Columns(2, "resource_browser_columns", true);
    changed |= RenderResourceTreePanel();
    ImGui::NextColumn();
    changed |= RenderResourceDetailsPanel();
    ImGui::Columns(1);

    return changed;
}

bool GameEditor::RenderResourceTreePanel() {
    ImGui::TextUnformatted("Resources");
    ImGui::Separator();

    const fs::path target_directory = GetSelectedResourceDirectory();
    ImGui::TextDisabled("Create/import target: %s",
        target_directory.empty() ? "./" : target_directory.generic_string().c_str());
    ImGui::InputText("New Item", new_resource_name, IM_ARRAYSIZE(new_resource_name));
    if (ImGui::Button("New File")) {
        CreateResourceFile(new_resource_name);
    }
    ImGui::SameLine();
    if (ImGui::Button("New Folder")) {
        CreateResourceDirectory(new_resource_name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Game Root")) {
        SelectResource({}, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh Resources")) {
        RebuildResourceIndices();
        RefreshSelectionsFromIndices(false, false, false);
    }

    ImGui::TextDisabled("Drop files onto the editor window to import them into the selected folder.");
    ImGui::Separator();

    if (resource_entries.empty()) {
        ImGui::TextWrapped("No visible resources were discovered for this game yet.");
        return false;
    }

    std::map<std::string, std::vector<const ResourceEntry*>> children_by_parent;
    for (const ResourceEntry& entry : resource_entries) {
        children_by_parent[entry.relative_path.parent_path().generic_string()].push_back(&entry);
    }

    for (auto& [_, children] : children_by_parent) {
        std::sort(children.begin(), children.end(),
            [](const ResourceEntry* lhs, const ResourceEntry* rhs) {
                if (lhs->is_directory != rhs->is_directory) {
                    return lhs->is_directory > rhs->is_directory;
                }
                return lhs->relative_path.filename().string() < rhs->relative_path.filename().string();
            });
    }

    const auto build_label = [this](const ResourceEntry& entry) {
        std::string label = entry.relative_path.filename().string();
        if (label.empty()) {
            label = entry.relative_path.generic_string();
        }

        const ResourcePaths::ShadowChange* change = FindShadowChange(entry.relative_path);
        if (change != nullptr) {
            if (change->type == ResourcePaths::ShadowChangeType::Added) {
                label += " +";
            }
            else {
                label += " *";
            }
        }

        return label;
    };

    std::function<void(const std::string&)> render_children = [&](const std::string& parent_key) {
        const auto found = children_by_parent.find(parent_key);
        if (found == children_by_parent.end()) {
            return;
        }

        for (const ResourceEntry* entry : found->second) {
            const std::string label = build_label(*entry);
            if (entry->is_directory) {
                const bool is_selected =
                    selected_resource_is_directory &&
                    selected_resource_path.generic_string() == entry->relative_path.generic_string();
                const bool has_children =
                    children_by_parent.find(entry->relative_path.generic_string()) != children_by_parent.end();
                ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_SpanFullWidth |
                    (is_selected ? ImGuiTreeNodeFlags_Selected : 0) |
                    (!has_children ? ImGuiTreeNodeFlags_Leaf : 0);

                const bool open = ImGui::TreeNodeEx(
                    entry->relative_path.generic_string().c_str(),
                    flags,
                    "%s",
                    label.c_str());
                if (ImGui::IsItemClicked()) {
                    SelectResource(entry->relative_path, true);
                }
                if (open && has_children) {
                    render_children(entry->relative_path.generic_string());
                    ImGui::TreePop();
                }
            }
            else {
                const bool is_selected =
                    !selected_resource_is_directory &&
                    selected_resource_path.generic_string() == entry->relative_path.generic_string();
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    SelectResource(entry->relative_path, false);
                    if (ImGui::IsMouseDoubleClicked(0) &&
                        (CanOpenInSceneTab(entry->relative_path) || CanOpenInLuaTab(entry->relative_path))) {
                        OpenSelectedResourceInDedicatedEditor();
                    }
                }
            }
        }
    };

    render_children("");
    return false;
}

bool GameEditor::RenderResourceDetailsPanel() {
    bool changed = false;

    ImGui::TextUnformatted("Resource Details");
    ImGui::Separator();

    if (selected_resource_path.empty()) {
        ImGui::TextWrapped(
            "Select a file or folder from the resource tree. New files and drops will target the game root until you pick a folder.");
        return false;
    }

    const ResourceEntry* entry = FindResourceEntry(selected_resource_path, selected_resource_is_directory);
    if (entry == nullptr) {
        ImGui::TextWrapped("The selected resource is no longer visible.");
        return false;
    }

    const ResourcePaths::ShadowChange* change = selected_resource_is_directory
        ? nullptr
        : FindShadowChange(selected_resource_path);

    if (!entry->is_directory && CanOpenInSceneTab(selected_resource_path)) {
        const std::string selected_scene_name = selected_resource_path.stem().string();
        if (!scene_loaded || current_scene_name != selected_scene_name) {
            LoadScene(selected_scene_name, false);
        }
    }
    else if (!entry->is_directory && CanOpenInLuaTab(selected_resource_path)) {
        const std::string selected_script_path =
            selected_resource_path.lexically_relative(fs::path("component_types")).generic_string();
        if (!script_loaded || current_script_relative_path != selected_script_path) {
            LoadScript(selected_script_path, false);
        }
    }

    ImGui::Text("Path: %s", selected_resource_path.generic_string().c_str());
    ImGui::TextDisabled("Main: %s | Shadow: %s",
        entry->exists_in_base ? "yes" : "no",
        entry->exists_in_shadow ? "yes" : "no");
    if (change != nullptr) {
        ImGui::TextDisabled("Staged change: %s", DescribeShadowChangeType(change->type).c_str());
    }
    ImGui::Spacing();

    if (entry->is_directory) {
        if (entry->exists_in_shadow && !entry->exists_in_base) {
            if (ImGui::Button("Merge Folder To Main")) {
                std::string error_message;
                if (!ResourcePaths::PromoteShadowChangeToBase(selected_resource_path, error_message)) {
                    status_message = error_message;
                }
                else {
                    RebuildResourceIndices();
                    RefreshSelectionsFromIndices(false, false, false);
                    status_message = "Merged shadow folder into main resources: " +
                        selected_resource_path.generic_string();
                }
                return false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard Folder Shadow")) {
                std::string error_message;
                if (!ResourcePaths::DiscardShadowChange(selected_resource_path, error_message)) {
                    status_message = error_message;
                }
                else {
                    RebuildResourceIndices();
                    RefreshSelectionsFromIndices(false, false, false);
                    status_message = "Discarded shadow folder: " + selected_resource_path.generic_string();
                }
                return false;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Delete Folder In Shadow")) {
            pending_resource_delete_path = selected_resource_path;
            pending_resource_delete_is_directory = true;
            ImGui::OpenPopup("Delete Resource##confirm");
        }

        int visible_children = 0;
        for (const ResourceEntry& resource_entry : resource_entries) {
            if (resource_entry.relative_path.parent_path() == selected_resource_path) {
                ++visible_children;
            }
        }

        ImGui::Spacing();
        ImGui::TextWrapped(
            "This folder currently exposes %d visible child resource%s. New files and drag-drop imports will land here.",
            visible_children,
            visible_children == 1 ? "" : "s");
    }
    else {
        if (change != nullptr) {
            if (ImGui::Button("Merge File To Main")) {
                MergeSelectedResourceToGame();
                return false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard File Shadow")) {
                DiscardSelectedResourceChange();
                return false;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Delete File In Shadow")) {
            pending_resource_delete_path = selected_resource_path;
            pending_resource_delete_is_directory = false;
            ImGui::OpenPopup("Delete Resource##confirm");
        }

        if (CanOpenInSceneTab(selected_resource_path)) {
            ImGui::Spacing();
            ImGui::TextWrapped("This scene can be edited directly here.");
            ImGui::Spacing();

            ImGui::Columns(2, "resource_scene_columns", true);
            const bool actor_panel_changed = RenderActorPanel();
            ImGui::NextColumn();
            const bool inspector_panel_changed = RenderInspectorPanel();
            ImGui::Columns(1);

            if (actor_panel_changed || inspector_panel_changed) {
                SaveCurrentSceneToShadow();
            }
        }
        else if (CanOpenInLuaTab(selected_resource_path)) {
            ImGui::Spacing();
            ImGui::TextWrapped("This Lua file can be edited directly here.");
            ImGui::Spacing();

            if (RenderLuaScriptEditorPanel()) {
                SaveCurrentScriptToShadow();
            }
        }
        else if (IsTextEditableResourcePath(selected_resource_path)) {
            ImGui::Spacing();
            const ImVec2 editor_size = ImVec2(-FLT_MIN, -FLT_MIN);
            changed |= ImGui::InputTextMultiline(
                "##generic_resource_editor",
                &current_text_resource_text,
                editor_size,
                ImGuiInputTextFlags_AllowTabInput);
        }
        else {
            ImGui::Spacing();
            const fs::path resolved_path = ResourcePaths::ResolveExistingGameFile(selected_resource_path);
            std::error_code error;
            const std::uintmax_t file_size = resolved_path.empty()
                ? 0
                : fs::file_size(resolved_path, error);
            ImGui::TextWrapped("This file is treated as binary and is not edited inline.");
            if (!error && !resolved_path.empty()) {
                ImGui::TextDisabled("Visible file size: %llu bytes",
                    static_cast<unsigned long long>(file_size));
            }
            ImGui::TextDisabled("Replace it by dragging another file into the selected folder.");
        }
    }

    if (ImGui::BeginPopupModal("Delete Resource##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Delete `%s` in the shadow layer? The main resources folder will stay unchanged until you merge the deletion.",
            pending_resource_delete_path.generic_string().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            selected_resource_path = pending_resource_delete_path;
            selected_resource_is_directory = pending_resource_delete_is_directory;
            DeleteSelectedResource();
            pending_resource_delete_path.clear();
            pending_resource_delete_is_directory = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pending_resource_delete_path.clear();
            pending_resource_delete_is_directory = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return changed;
        }
        ImGui::EndPopup();
    }

    return changed;
}

bool GameEditor::RenderShadowChangesTab() {
    if (selected_shadow_change_path.empty() && !shadow_changes.empty()) {
        selected_shadow_change_path = shadow_changes.front().relative_path;
    }

    ImGui::Columns(2, "shadow_change_columns", true);
    RenderShadowChangeListPanel();
    ImGui::NextColumn();
    RenderShadowChangeDetailsPanel();
    ImGui::Columns(1);
    return false;
}

bool GameEditor::RenderShadowChangeListPanel() {
    ImGui::TextUnformatted("Shadow Changes");
    ImGui::Separator();
    if (ImGui::Button("Refresh Changes")) {
        RebuildResourceIndices();
        RefreshSelectionsFromIndices(false, false, false);
    }

    ImGui::Spacing();
    if (shadow_changes.empty()) {
        ImGui::TextWrapped("No staged shadow changes exist right now.");
        return false;
    }

    for (const ResourcePaths::ShadowChange& change : shadow_changes) {
        const std::string label =
            "[" + DescribeShadowChangeType(change.type) + "] " + change.relative_path.generic_string();
        if (ImGui::Selectable(
                label.c_str(),
                selected_shadow_change_path.generic_string() == change.relative_path.generic_string())) {
            selected_shadow_change_path = change.relative_path;
        }
    }

    return false;
}

bool GameEditor::RenderShadowChangeDetailsPanel() {
    ImGui::TextUnformatted("Change Review");
    ImGui::Separator();

    if (selected_shadow_change_path.empty()) {
        ImGui::TextWrapped("Select a staged change to compare it against the main resources folder.");
        return false;
    }

    const ResourcePaths::ShadowChange* change = FindShadowChange(selected_shadow_change_path);
    if (change == nullptr) {
        ImGui::TextWrapped("That staged change is no longer available.");
        return false;
    }

    ImGui::Text("Path: %s", change->relative_path.generic_string().c_str());
    ImGui::TextDisabled("Type: %s", DescribeShadowChangeType(change->type).c_str());
    if (ImGui::Button("Merge Change To Main")) {
        MergeShadowChangeToGame(change->relative_path);
        return false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard Change")) {
        DiscardShadowChange(change->relative_path);
        return false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reveal In Resources")) {
        pending_tab_selection_target = EditorTab::Resources;
        pending_tab_selection = true;
        SelectResource(change->relative_path, false);
    }

    if (CanOpenInSceneTab(change->relative_path) || CanOpenInLuaTab(change->relative_path)) {
        ImGui::SameLine();
        if (ImGui::Button(CanOpenInSceneTab(change->relative_path)
                ? "Open In Scene Editor"
                : "Open In Lua Editor")) {
            SelectResource(change->relative_path, false);
            OpenSelectedResourceInDedicatedEditor();
        }
    }

    const bool can_compare_as_text = HasTextEditingExtension(change->relative_path);
    const fs::path base_path = ResourcePaths::GetBaseGameFilePath(change->relative_path);
    const fs::path shadow_path = ResourcePaths::GetShadowFilePath(change->relative_path);

    if (!can_compare_as_text) {
        std::error_code error;
        const std::uintmax_t base_size = fs::exists(base_path) ? fs::file_size(base_path, error) : 0;
        error.clear();
        const std::uintmax_t shadow_size = fs::exists(shadow_path) ? fs::file_size(shadow_path, error) : 0;
        ImGui::Spacing();
        ImGui::TextWrapped("This change targets a binary file, so the review panel shows metadata only.");
        ImGui::TextDisabled("Main file present: %s", fs::exists(base_path) ? "yes" : "no");
        if (fs::exists(base_path)) {
            ImGui::TextDisabled("Main size: %llu bytes", static_cast<unsigned long long>(base_size));
        }
        ImGui::TextDisabled("Shadow file present: %s", fs::exists(shadow_path) ? "yes" : "no");
        if (fs::exists(shadow_path)) {
            ImGui::TextDisabled("Shadow size: %llu bytes", static_cast<unsigned long long>(shadow_size));
        }
        return false;
    }

    std::string base_contents = "<missing from main resources>";
    if (fs::exists(base_path)) {
        ReadTextFile(base_path, base_contents);
    }

    std::string shadow_contents = "<missing from shadow>";
    if (change->type == ResourcePaths::ShadowChangeType::Deleted) {
        shadow_contents = "<deleted in shadow>";
    }
    else if (fs::exists(shadow_path)) {
        ReadTextFile(shadow_path, shadow_contents);
    }

    ImGui::Spacing();
    ImGui::Columns(2, "shadow_compare_columns", true);
    ImGui::TextUnformatted("Main Resources");
    ImGui::Separator();
    ImGui::InputTextMultiline(
        "##main_compare_view",
        &base_contents,
        ImVec2(-FLT_MIN, -FLT_MIN),
        ImGuiInputTextFlags_ReadOnly);
    ImGui::NextColumn();
    ImGui::TextUnformatted("Shadow");
    ImGui::Separator();
    ImGui::InputTextMultiline(
        "##shadow_compare_view",
        &shadow_contents,
        ImVec2(-FLT_MIN, -FLT_MIN),
        ImGuiInputTextFlags_ReadOnly);
    ImGui::Columns(1);

    return false;
}

bool GameEditor::RenderActorPanel() {
    bool changed = false;

    ImGui::TextUnformatted("Actors");
    ImGui::Separator();
    if (!scene_loaded) {
        ImGui::TextWrapped("Load a scene to begin editing.");
        return false;
    }

    Value& actors = EnsureActorsArray();
    if (ImGui::Button("Add Actor")) {
        CreateActor();
        changed = true;
    }
    ImGui::SameLine();
    if (selected_actor_index >= 0 && selected_actor_index < actors.Size()) {
        if (ImGui::Button("Duplicate")) {
            DuplicateSelectedActor();
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            DeleteSelectedActor();
            changed = true;
        }
    }

    ImGui::Separator();
    for (rapidjson::SizeType i = 0; i < actors.Size(); ++i) {
        const Value& actor = actors[i];
        std::string label = "#" + std::to_string(static_cast<int>(i));
        if (const Value* nameValue = FindMember(actor, "name");
            nameValue != nullptr && nameValue->IsString() && nameValue->GetStringLength() > 0) {
            label += " ";
            label += nameValue->GetString();
        }

        if (ImGui::Selectable(label.c_str(), selected_actor_index == static_cast<int>(i))) {
            selected_actor_index = static_cast<int>(i);
        }
    }

    return changed;
}

bool GameEditor::RenderInspectorPanel() {
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    if (!scene_loaded) {
        ImGui::TextWrapped("No scene is currently loaded.");
        return false;
    }

    Value& actors = EnsureActorsArray();
    if (selected_actor_index < 0 || selected_actor_index >= static_cast<int>(actors.Size())) {
        ImGui::TextWrapped("Select an actor to edit it.");
        return false;
    }

    Value& actor = actors[static_cast<rapidjson::SizeType>(selected_actor_index)];
    if (!actor.IsObject()) {
        actor.SetObject();
    }

    return RenderActorInspector(actor);
}

bool GameEditor::RenderActorInspector(Value& actor) {
    bool changed = false;

    changed |= EditStringField(actor, "name", "Name");
    changed |= EditStringField(actor, "template", "Template");
    changed |= EditFloatField(actor, "x", "X");
    changed |= EditFloatField(actor, "y", "Y");
    changed |= EditFloatField(actor, "vel_x", "Velocity X");
    changed |= EditFloatField(actor, "vel_y", "Velocity Y");
    changed |= EditStringField(actor, "view_image", "View Image");
    changed |= EditStringField(actor, "view_image_back", "View Image Back");
    changed |= EditStringField(actor, "view_image_damage", "View Image Damage");
    changed |= EditStringField(actor, "view_image_attack", "View Image Attack");
    changed |= EditFloatField(actor, "transform_scale_x", "Scale X");
    changed |= EditFloatField(actor, "transform_scale_y", "Scale Y");
    changed |= EditFloatField(actor, "transform_rotation_degrees", "Rotation");
    changed |= EditIntField(actor, "render_order", "Render Order");
    changed |= EditBoolField(actor, "blocking", "Blocking");
    changed |= EditBoolField(actor, "movement_bounce_enabled", "Movement Bounce");
    changed |= EditStringField(actor, "nearby_dialogue", "Nearby Dialogue", 512);
    changed |= EditStringField(actor, "contact_dialogue", "Contact Dialogue", 512);

    ImGui::Spacing();
    changed |= RenderComponentEditor(actor);
    return changed;
}

bool GameEditor::RenderComponentEditor(Value& actor) {
    bool changed = false;

    Allocator& allocator = current_scene_document.GetAllocator();
    Value* components = FindMember(actor, "components");
    if (components == nullptr || !components->IsObject()) {
        if (components == nullptr) {
            actor.AddMember("components", rapidjson::kObjectType, allocator);
            components = FindMember(actor, "components");
        }
        else {
            components->SetObject();
        }
    }

    ImGui::TextUnformatted("Components");
    ImGui::Separator();

    ImGui::InputText("New Component Key", new_component_key, IM_ARRAYSIZE(new_component_key));
    ImGui::InputText("New Component Type", new_component_type, IM_ARRAYSIZE(new_component_type));
    if (ImGui::Button("Add Component") &&
        std::strlen(new_component_key) > 0 &&
        std::strlen(new_component_type) > 0 &&
        FindMember(*components, new_component_key) == nullptr) {
        rapidjson::Value componentObject(rapidjson::kObjectType);
        componentObject.AddMember(
            "type",
            rapidjson::Value(new_component_type, allocator).Move(),
            allocator);
        components->AddMember(
            rapidjson::Value(new_component_key, allocator).Move(),
            componentObject,
            allocator);
        new_component_key[0] = '\0';
        new_component_type[0] = '\0';
        changed = true;
    }

    std::string componentToDelete;
    for (auto componentIt = components->MemberBegin(); componentIt != components->MemberEnd(); ++componentIt) {
        if (!componentIt->name.IsString() || !componentIt->value.IsObject()) {
            continue;
        }

        ImGui::PushID(componentIt->name.GetString());
        bool open = ImGui::CollapsingHeader(componentIt->name.GetString(), ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            componentToDelete = componentIt->name.GetString();
        }

        if (open) {
            changed |= EditStringField(componentIt->value, "type", "Type");

            std::string propertyToDelete;
            for (auto propertyIt = componentIt->value.MemberBegin(); propertyIt != componentIt->value.MemberEnd(); ++propertyIt) {
                if (!propertyIt->name.IsString()) {
                    continue;
                }

                const std::string propertyName = propertyIt->name.GetString();
                if (propertyName == "type") {
                    continue;
                }

                ImGui::PushID(propertyName.c_str());
                ImGui::TextUnformatted(propertyName.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete Property")) {
                    propertyToDelete = propertyName;
                }
                changed |= EditJsonValue("Value", propertyIt->value);
                ImGui::PopID();
            }

            if (!propertyToDelete.empty()) {
                componentIt->value.RemoveMember(propertyToDelete.c_str());
                changed = true;
            }

            ImGui::Separator();
            ImGui::InputText("New Property Name", new_property_name, IM_ARRAYSIZE(new_property_name));
            ImGui::Combo("New Property Type", &new_property_type, kJsonTypeLabels, kJsonTypeCount);
            if (ImGui::Button("Add Property") &&
                std::strlen(new_property_name) > 0 &&
                FindMember(componentIt->value, new_property_name) == nullptr) {
                componentIt->value.AddMember(
                    rapidjson::Value(new_property_name, allocator).Move(),
                    MakeDefaultValueForType(new_property_type, allocator),
                    allocator);
                new_property_name[0] = '\0';
                changed = true;
            }
        }

        ImGui::PopID();
    }

    if (!componentToDelete.empty()) {
        components->RemoveMember(componentToDelete.c_str());
        changed = true;
    }

    return changed;
}

bool GameEditor::EditJsonValue(const std::string& label, Value& value) {
    bool changed = false;
    Allocator& allocator = current_scene_document.GetAllocator();

    int typeIndex = DetectJsonTypeIndex(value);
    if (ImGui::Combo((label + " Type").c_str(), &typeIndex, kJsonTypeLabels, kJsonTypeCount)) {
        value = MakeDefaultValueForType(typeIndex, allocator);
        changed = true;
    }

    switch (typeIndex) {
    case 0:
        ImGui::TextDisabled("null");
        break;
    case 1: {
        bool boolValue = value.IsBool() ? value.GetBool() : false;
        if (ImGui::Checkbox((label + " Bool").c_str(), &boolValue)) {
            value.SetBool(boolValue);
            changed = true;
        }
        break;
    }
    case 2: {
        long long intValue = 0;
        if (value.IsInt64()) {
            intValue = value.GetInt64();
        }
        else if (value.IsUint64()) {
            intValue = static_cast<long long>(value.GetUint64());
        }
        if (ImGui::InputScalar((label + " Int").c_str(), ImGuiDataType_S64, &intValue)) {
            value.SetInt64(intValue);
            changed = true;
        }
        break;
    }
    case 3: {
        double numberValue = value.IsNumber() ? value.GetDouble() : 0.0;
        if (ImGui::InputDouble((label + " Number").c_str(), &numberValue, 0.0, 0.0, "%.6f")) {
            value.SetDouble(numberValue);
            changed = true;
        }
        break;
    }
    case 4: {
        std::array<char, 256> buffer{};
        const std::string currentValue = value.IsString() ? value.GetString() : "";
        CopyStringToBuffer(currentValue, buffer.data(), buffer.size());
        if (ImGui::InputText((label + " String").c_str(), buffer.data(), buffer.size())) {
            value.SetString(buffer.data(), allocator);
            changed = true;
        }
        break;
    }
    default:
        break;
    }

    return changed;
}

GameEditor::Value& GameEditor::EnsureActorsArray() {
    Allocator& allocator = current_scene_document.GetAllocator();
    if (!current_scene_document.IsObject()) {
        current_scene_document.SetObject();
    }
    if (!current_scene_document.HasMember("actors")) {
        current_scene_document.AddMember("actors", rapidjson::kArrayType, allocator);
    }
    else if (!current_scene_document["actors"].IsArray()) {
        current_scene_document["actors"].SetArray();
    }

    return current_scene_document["actors"];
}

std::string GameEditor::SceneFilename(const std::string& scene_name) {
    if (scene_name.size() >= 6 && scene_name.substr(scene_name.size() - 6) == ".scene") {
        return scene_name;
    }
    return scene_name + ".scene";
}

std::string GameEditor::SceneDisplayName(const std::string& scene_name) {
    return scene_name;
}

std::string GameEditor::ScriptDisplayName(const std::string& script_relative_path) {
    return script_relative_path;
}

void GameEditor::CreateActor() {
    Value actorObject(rapidjson::kObjectType);
    actorObject.AddMember(
        "name",
        rapidjson::Value("NewActor", current_scene_document.GetAllocator()).Move(),
        current_scene_document.GetAllocator());
    actorObject.AddMember("components", rapidjson::kObjectType, current_scene_document.GetAllocator());

    Value& actors = EnsureActorsArray();
    actors.PushBack(actorObject, current_scene_document.GetAllocator());
    selected_actor_index = static_cast<int>(actors.Size()) - 1;
}

void GameEditor::DuplicateSelectedActor() {
    Value& actors = EnsureActorsArray();
    if (selected_actor_index < 0 || selected_actor_index >= static_cast<int>(actors.Size())) {
        return;
    }

    Value clone;
    clone.CopyFrom(
        actors[static_cast<rapidjson::SizeType>(selected_actor_index)],
        current_scene_document.GetAllocator());
    if (Value* nameValue = FindMember(clone, "name");
        nameValue != nullptr && nameValue->IsString()) {
        std::string cloneName = std::string(nameValue->GetString()) + " Copy";
        nameValue->SetString(cloneName.c_str(), current_scene_document.GetAllocator());
    }

    actors.PushBack(clone, current_scene_document.GetAllocator());
    selected_actor_index = static_cast<int>(actors.Size()) - 1;
}

void GameEditor::DeleteSelectedActor() {
    Value& actors = EnsureActorsArray();
    if (selected_actor_index < 0 || selected_actor_index >= static_cast<int>(actors.Size())) {
        return;
    }

    actors.Erase(actors.Begin() + selected_actor_index);
    if (actors.Empty()) {
        selected_actor_index = -1;
    }
    else if (selected_actor_index >= static_cast<int>(actors.Size())) {
        selected_actor_index = static_cast<int>(actors.Size()) - 1;
    }
}

bool GameEditor::EditStringField(Value& object, const char* key, const char* label, std::size_t buffer_size) {
    if (!object.IsObject()) {
        return false;
    }

    std::vector<char> buffer(buffer_size, '\0');
    const Value* existingValue = FindMember(object, key);
    const std::string currentValue = existingValue != nullptr && existingValue->IsString()
        ? existingValue->GetString()
        : "";
    CopyStringToBuffer(currentValue, buffer.data(), buffer.size());

    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }

    SetString(object, key, buffer.data(), current_scene_document.GetAllocator());
    return true;
}

bool GameEditor::EditFloatField(Value& object, const char* key, const char* label) {
    if (!object.IsObject()) {
        return false;
    }

    const Value* existingValue = FindMember(object, key);
    float currentValue = existingValue != nullptr && existingValue->IsNumber()
        ? static_cast<float>(existingValue->GetDouble())
        : 0.0f;
    if (!ImGui::InputFloat(label, &currentValue, 0.0f, 0.0f, "%.3f")) {
        return false;
    }

    SetDouble(object, key, currentValue, current_scene_document.GetAllocator());
    return true;
}

bool GameEditor::EditIntField(Value& object, const char* key, const char* label) {
    if (!object.IsObject()) {
        return false;
    }

    const Value* existingValue = FindMember(object, key);
    int currentValue = existingValue != nullptr && existingValue->IsInt()
        ? existingValue->GetInt()
        : 0;
    if (!ImGui::InputInt(label, &currentValue)) {
        return false;
    }

    SetInt(object, key, currentValue, current_scene_document.GetAllocator());
    return true;
}

bool GameEditor::EditBoolField(Value& object, const char* key, const char* label) {
    if (!object.IsObject()) {
        return false;
    }

    const Value* existingValue = FindMember(object, key);
    bool currentValue = existingValue != nullptr && existingValue->IsBool()
        ? existingValue->GetBool()
        : false;
    if (!ImGui::Checkbox(label, &currentValue)) {
        return false;
    }

    SetBool(object, key, currentValue, current_scene_document.GetAllocator());
    return true;
}

GameEditor::Value* GameEditor::FindMember(Value& object, const char* key) {
    if (!object.IsObject()) {
        return nullptr;
    }

    auto found = object.FindMember(key);
    return found == object.MemberEnd() ? nullptr : &found->value;
}

const GameEditor::Value* GameEditor::FindMember(const Value& object, const char* key) {
    if (!object.IsObject()) {
        return nullptr;
    }

    auto found = object.FindMember(key);
    return found == object.MemberEnd() ? nullptr : &found->value;
}

void GameEditor::SetString(Value& object, const char* key, const std::string& value, Allocator& allocator) {
    if (Value* existingValue = FindMember(object, key)) {
        existingValue->SetString(value.c_str(), allocator);
        return;
    }

    object.AddMember(
        rapidjson::Value(key, allocator).Move(),
        rapidjson::Value(value.c_str(), allocator).Move(),
        allocator);
}

void GameEditor::SetDouble(Value& object, const char* key, double value, Allocator& allocator) {
    if (Value* existingValue = FindMember(object, key)) {
        existingValue->SetDouble(value);
        return;
    }

    object.AddMember(rapidjson::Value(key, allocator).Move(), value, allocator);
}

void GameEditor::SetInt(Value& object, const char* key, int value, Allocator& allocator) {
    if (Value* existingValue = FindMember(object, key)) {
        existingValue->SetInt(value);
        return;
    }

    object.AddMember(rapidjson::Value(key, allocator).Move(), value, allocator);
}

void GameEditor::SetBool(Value& object, const char* key, bool value, Allocator& allocator) {
    if (Value* existingValue = FindMember(object, key)) {
        existingValue->SetBool(value);
        return;
    }

    object.AddMember(rapidjson::Value(key, allocator).Move(), value, allocator);
}

GameEditor::Value GameEditor::MakeDefaultValueForType(int type_index, Allocator& allocator) {
    switch (type_index) {
    case 0:
        return Value();
    case 1:
        return Value(false);
    case 2:
        return Value(0);
    case 3:
        return Value(0.0);
    case 4:
    default:
        return Value("", allocator);
    }
}
