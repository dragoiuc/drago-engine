#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ResourcePaths
{
    enum class ShadowChangeType {
        Added,
        Modified,
        Deleted
    };

    struct ShadowChange {
        std::filesystem::path relative_path;
        ShadowChangeType type = ShadowChangeType::Modified;
    };

    void SetGameRoot(const std::filesystem::path& game_root);
    const std::filesystem::path& GetGameRoot();
    std::filesystem::path GetDefaultGameLibraryRoot();
    std::filesystem::path GetShadowLibraryRoot();
    std::filesystem::path GetShadowGameRoot();
    std::filesystem::path GetBaseGameFilePath(const std::filesystem::path& relative_path);
    std::filesystem::path GetShadowFilePath(const std::filesystem::path& relative_path);
    std::filesystem::path GetShadowDeletionRoot();
    std::filesystem::path GetDeletionMarkerPath(const std::filesystem::path& relative_path);
    bool BasePathExists(const std::filesystem::path& relative_path);
    bool ShadowPathExists(const std::filesystem::path& relative_path);
    bool IsDeletedInShadow(const std::filesystem::path& relative_path);
    bool ClearDeletionMarker(const std::filesystem::path& relative_path, std::string& error_message);
    bool DiscardShadowChange(const std::filesystem::path& relative_path, std::string& error_message);
    bool MarkDeletedInShadow(const std::filesystem::path& relative_path, std::string& error_message);
    bool PromoteShadowChangeToBase(const std::filesystem::path& relative_path, std::string& error_message);
    std::vector<ShadowChange> CollectShadowChanges();
    std::filesystem::path ResolveExistingGameFile(const std::filesystem::path& relative_path);
    std::filesystem::path ResolveExistingGameFile(
        const std::vector<std::filesystem::path>& relative_candidates);
    std::filesystem::path GetAudioDirectory();
    std::filesystem::path GetImagesDirectory();
    std::filesystem::path GetFontsDirectory();
    std::filesystem::path GetScenesDirectory();
    std::filesystem::path GetActorTemplatesDirectory();
    std::filesystem::path GetComponentTypesDirectory();
}
