#include "ResourcePaths.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <map>

namespace fs = std::filesystem;

namespace {
    constexpr const char* kShadowStateDirectoryName = ".shadow_state";
    constexpr const char* kDeletedDirectoryName = "deleted";
    constexpr const char* kDeletionMarkerSuffix = ".deleted";

    fs::path& ActiveGameRoot() {
        static fs::path gameRoot = fs::current_path() / fs::path("resources");
        return gameRoot;
    }

    bool IsContainedRelativePath(const fs::path& path) {
        if (path.empty()) {
            return false;
        }

        for (const fs::path& part : path) {
            if (part == "..") {
                return false;
            }
        }

        return true;
    }

    fs::path RelativeGameRootForShadow() {
        std::error_code error;
        const fs::path relativeToWorkspace =
            fs::relative(ResourcePaths::GetGameRoot(), fs::current_path(), error);
        if (!error && IsContainedRelativePath(relativeToWorkspace)) {
            return relativeToWorkspace;
        }

        return fs::path("external") /
            fs::path(std::to_string(std::hash<std::string>{}(ResourcePaths::GetGameRoot().string())));
    }

    fs::path GetShadowStateDirectory() {
        return ResourcePaths::GetShadowGameRoot() / fs::path(kShadowStateDirectoryName);
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

    void PruneEmptyDirectories(const fs::path& start, const fs::path& stop_path) {
        std::error_code error;
        fs::path current = start;

        while (!current.empty() && current != stop_path && current != stop_path.parent_path()) {
            if (!fs::exists(current) || !fs::is_directory(current) || !fs::is_empty(current, error)) {
                return;
            }
            if (error) {
                return;
            }

            fs::remove(current, error);
            if (error) {
                return;
            }

            current = current.parent_path();
        }
    }

    bool RemovePathIfExists(const fs::path& path, std::string& error_message, bool& removed) {
        removed = false;

        std::error_code error;
        if (!fs::exists(path)) {
            return true;
        }

        const std::uintmax_t removedCount = fs::is_directory(path)
            ? fs::remove_all(path, error)
            : static_cast<std::uintmax_t>(fs::remove(path, error) ? 1 : 0);
        if (error) {
            error_message = "Failed to remove " + path.lexically_normal().string();
            return false;
        }

        removed = removedCount > 0;
        return true;
    }

    bool IsUnderShadowStateDirectory(const fs::path& relative_path) {
        auto it = relative_path.begin();
        return it != relative_path.end() && *it == kShadowStateDirectoryName;
    }

    bool TryResolveMarkerRelativePath(
        const fs::path& marker_relative_path,
        fs::path& out_relative_path) {
        const std::string markerPathString = marker_relative_path.generic_string();
        const std::size_t suffixLength = std::char_traits<char>::length(kDeletionMarkerSuffix);
        if (markerPathString.size() <= suffixLength ||
            markerPathString.substr(markerPathString.size() - suffixLength) != kDeletionMarkerSuffix) {
            return false;
        }

        const std::string originalPath = markerPathString.substr(0, markerPathString.size() - suffixLength);
        if (originalPath.empty()) {
            return false;
        }

        const fs::path relativePath(originalPath);
        if (!IsContainedRelativePath(relativePath)) {
            return false;
        }

        out_relative_path = relativePath;
        return true;
    }
}

void ResourcePaths::SetGameRoot(const fs::path& game_root) {
    if (game_root.empty()) {
        ActiveGameRoot() = GetDefaultGameLibraryRoot();
        return;
    }

    ActiveGameRoot() = fs::absolute(game_root).lexically_normal();
}

const fs::path& ResourcePaths::GetGameRoot() {
    return ActiveGameRoot();
}

fs::path ResourcePaths::GetDefaultGameLibraryRoot() {
    return (fs::current_path() / fs::path("resources")).lexically_normal();
}

fs::path ResourcePaths::GetShadowLibraryRoot() {
    return (fs::current_path() / fs::path("shadow_main")).lexically_normal();
}

fs::path ResourcePaths::GetShadowGameRoot() {
    return GetShadowLibraryRoot() / RelativeGameRootForShadow();
}

fs::path ResourcePaths::GetBaseGameFilePath(const fs::path& relative_path) {
    return GetGameRoot() / relative_path;
}

fs::path ResourcePaths::GetShadowFilePath(const fs::path& relative_path) {
    return GetShadowGameRoot() / relative_path;
}

fs::path ResourcePaths::GetShadowDeletionRoot() {
    return GetShadowStateDirectory() / fs::path(kDeletedDirectoryName);
}

fs::path ResourcePaths::GetDeletionMarkerPath(const fs::path& relative_path) {
    fs::path markerPath = GetShadowDeletionRoot() / relative_path;
    markerPath += kDeletionMarkerSuffix;
    return markerPath;
}

bool ResourcePaths::BasePathExists(const fs::path& relative_path) {
    return fs::exists(GetBaseGameFilePath(relative_path));
}

bool ResourcePaths::ShadowPathExists(const fs::path& relative_path) {
    return fs::exists(GetShadowFilePath(relative_path));
}

bool ResourcePaths::IsDeletedInShadow(const fs::path& relative_path) {
    return fs::exists(GetDeletionMarkerPath(relative_path));
}

bool ResourcePaths::ClearDeletionMarker(const fs::path& relative_path, std::string& error_message) {
    const fs::path markerPath = GetDeletionMarkerPath(relative_path);
    bool removed = false;
    if (!RemovePathIfExists(markerPath, error_message, removed)) {
        return false;
    }

    if (removed) {
        PruneEmptyDirectories(markerPath.parent_path(), GetShadowDeletionRoot());
    }

    return true;
}

bool ResourcePaths::DiscardShadowChange(const fs::path& relative_path, std::string& error_message) {
    const fs::path shadowPath = GetShadowFilePath(relative_path);
    bool removedShadowPath = false;
    if (!RemovePathIfExists(shadowPath, error_message, removedShadowPath)) {
        return false;
    }

    const fs::path markerPath = GetDeletionMarkerPath(relative_path);
    bool removedMarkerPath = false;
    if (!RemovePathIfExists(markerPath, error_message, removedMarkerPath)) {
        return false;
    }

    if (removedShadowPath) {
        PruneEmptyDirectories(shadowPath.parent_path(), GetShadowGameRoot());
    }
    if (removedMarkerPath) {
        PruneEmptyDirectories(markerPath.parent_path(), GetShadowDeletionRoot());
    }

    return true;
}

bool ResourcePaths::MarkDeletedInShadow(const fs::path& relative_path, std::string& error_message) {
    const fs::path shadowPath = GetShadowFilePath(relative_path);
    bool removedShadowPath = false;
    if (!RemovePathIfExists(shadowPath, error_message, removedShadowPath)) {
        return false;
    }
    if (removedShadowPath) {
        PruneEmptyDirectories(shadowPath.parent_path(), GetShadowGameRoot());
    }

    if (!BasePathExists(relative_path)) {
        return ClearDeletionMarker(relative_path, error_message);
    }

    const fs::path markerPath = GetDeletionMarkerPath(relative_path);
    std::error_code error;
    fs::create_directories(markerPath.parent_path(), error);
    if (error) {
        error_message = "Failed to create deletion marker directory: " + markerPath.parent_path().lexically_normal().string();
        return false;
    }

    std::ofstream markerStream(markerPath, std::ios::binary | std::ios::trunc);
    if (!markerStream.is_open()) {
        error_message = "Failed to write deletion marker: " + markerPath.lexically_normal().string();
        return false;
    }

    markerStream << "deleted\n";
    return markerStream.good();
}

bool ResourcePaths::PromoteShadowChangeToBase(const fs::path& relative_path, std::string& error_message) {
    const fs::path shadowPath = GetShadowFilePath(relative_path);
    const fs::path markerPath = GetDeletionMarkerPath(relative_path);
    const fs::path basePath = GetBaseGameFilePath(relative_path);

    if (fs::exists(markerPath)) {
        bool removedBasePath = false;
        if (!RemovePathIfExists(basePath, error_message, removedBasePath)) {
            return false;
        }

        bool removedMarkerPath = false;
        if (!RemovePathIfExists(markerPath, error_message, removedMarkerPath)) {
            return false;
        }

        if (removedMarkerPath) {
            PruneEmptyDirectories(markerPath.parent_path(), GetShadowDeletionRoot());
        }

        return true;
    }

    if (!fs::exists(shadowPath)) {
        return true;
    }

    std::error_code error;
    if (fs::is_directory(shadowPath)) {
        fs::create_directories(basePath, error);
        if (error) {
            error_message = "Failed to create directory in game resources: " + basePath.lexically_normal().string();
            return false;
        }
    }
    else {
        fs::create_directories(basePath.parent_path(), error);
        if (error) {
            error_message = "Failed to create directory in game resources: " + basePath.parent_path().lexically_normal().string();
            return false;
        }

        fs::copy_file(shadowPath, basePath, fs::copy_options::overwrite_existing, error);
        if (error) {
            error_message = "Failed to copy shadow file into game resources: " + relative_path.generic_string();
            return false;
        }
    }

    bool removedShadowPath = false;
    if (!RemovePathIfExists(shadowPath, error_message, removedShadowPath)) {
        return false;
    }
    if (removedShadowPath) {
        PruneEmptyDirectories(shadowPath.parent_path(), GetShadowGameRoot());
    }

    return true;
}

std::vector<ResourcePaths::ShadowChange> ResourcePaths::CollectShadowChanges() {
    std::map<std::string, ShadowChange> changesByPath;
    const fs::path shadowRoot = GetShadowGameRoot();

    if (fs::exists(shadowRoot) && fs::is_directory(shadowRoot)) {
        for (const fs::directory_entry& entry :
            fs::recursive_directory_iterator(shadowRoot, fs::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) {
                continue;
            }

            std::error_code error;
            const fs::path relativePath = fs::relative(entry.path(), shadowRoot, error);
            if (error || relativePath.empty() || IsUnderShadowStateDirectory(relativePath)) {
                continue;
            }

            const fs::path basePath = GetBaseGameFilePath(relativePath);
            const bool existsInBase = fs::exists(basePath);
            if (existsInBase && FilesAreBinaryEqual(basePath, entry.path())) {
                continue;
            }

            changesByPath[relativePath.generic_string()] = ShadowChange{
                relativePath,
                existsInBase ? ShadowChangeType::Modified : ShadowChangeType::Added
            };
        }
    }

    const fs::path deletionRoot = GetShadowDeletionRoot();
    if (fs::exists(deletionRoot) && fs::is_directory(deletionRoot)) {
        for (const fs::directory_entry& entry :
            fs::recursive_directory_iterator(deletionRoot, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::error_code error;
            const fs::path markerRelativePath = fs::relative(entry.path(), deletionRoot, error);
            if (error || markerRelativePath.empty()) {
                continue;
            }

            fs::path originalRelativePath;
            if (!TryResolveMarkerRelativePath(markerRelativePath, originalRelativePath)) {
                continue;
            }

            changesByPath[originalRelativePath.generic_string()] = ShadowChange{
                originalRelativePath,
                ShadowChangeType::Deleted
            };
        }
    }

    std::vector<ShadowChange> changes;
    changes.reserve(changesByPath.size());
    for (const auto& [_, change] : changesByPath) {
        changes.push_back(change);
    }

    return changes;
}

fs::path ResourcePaths::ResolveExistingGameFile(const fs::path& relative_path) {
    if (IsDeletedInShadow(relative_path)) {
        return {};
    }

    const fs::path shadowPath = GetShadowFilePath(relative_path);
    if (fs::exists(shadowPath)) {
        return shadowPath;
    }

    const fs::path basePath = GetBaseGameFilePath(relative_path);
    if (fs::exists(basePath)) {
        return basePath;
    }

    return {};
}

fs::path ResourcePaths::ResolveExistingGameFile(
    const std::vector<fs::path>& relative_candidates) {
    for (const fs::path& candidate : relative_candidates) {
        const fs::path resolvedPath = ResolveExistingGameFile(candidate);
        if (!resolvedPath.empty()) {
            return resolvedPath;
        }
    }

    return {};
}

fs::path ResourcePaths::GetAudioDirectory() {
    return GetGameRoot() / fs::path("audio");
}

fs::path ResourcePaths::GetImagesDirectory() {
    return GetGameRoot() / fs::path("images");
}

fs::path ResourcePaths::GetFontsDirectory() {
    return GetGameRoot() / fs::path("fonts");
}

fs::path ResourcePaths::GetScenesDirectory() {
    return GetGameRoot() / fs::path("scenes");
}

fs::path ResourcePaths::GetActorTemplatesDirectory() {
    return GetGameRoot() / fs::path("actor_templates");
}

fs::path ResourcePaths::GetComponentTypesDirectory() {
    return GetGameRoot() / fs::path("component_types");
}
