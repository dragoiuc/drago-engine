#include "AudioDB.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "AudioHelper.h"
#include "ResourcePaths.h"

namespace fs = std::filesystem;

std::unordered_map<std::string, Mix_Chunk*> AudioDB::audioCache;
namespace {
    bool g_audio_initialized = false;
}

static fs::path ResolveAudioPath(const std::string& clipName) {
    return ResourcePaths::ResolveExistingGameFile(std::vector<fs::path>{
        fs::path("audio") / fs::path(clipName + ".wav"),
        fs::path("audio") / fs::path(clipName + ".ogg")
        });
}

void AudioDB::Init() {
    if (g_audio_initialized) {
        return;
    }

    AudioHelper::Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    AudioHelper::Mix_AllocateChannels(50);
    g_audio_initialized = true;
}

void AudioDB::Shutdown() {
    if (!g_audio_initialized) {
        return;
    }

    for (auto& entry : audioCache) {
        if (entry.second != nullptr) {
            Mix_FreeChunk(entry.second);
        }
    }
    audioCache.clear();

    AudioHelper::Mix_CloseAudio();
    g_audio_initialized = false;
}

bool AudioDB::HasClip(const std::string& clipName) {
    return !ResolveAudioPath(clipName).empty();
}

Mix_Chunk* AudioDB::LoadClip(const std::string& clipName) {
    auto found = audioCache.find(clipName);
    if (found != audioCache.end()) {
        return found->second;
    }

    const fs::path audioPath = ResolveAudioPath(clipName);
    if (audioPath.empty()) {
        std::cout << "error: failed to play audio clip " << clipName;
        std::exit(0);
    }

    Mix_Chunk* chunk = AudioHelper::Mix_LoadWAV(audioPath.string().c_str());
    if (chunk == nullptr) {
        std::cout << "error: failed to play audio clip " << clipName;
        std::exit(0);
    }

    audioCache[clipName] = chunk;
    return chunk;
}

void AudioDB::PlayChannel(int channel, const std::string& clipName, int loops) {
    Mix_Chunk* chunk = LoadClip(clipName);
    AudioHelper::Mix_PlayChannel(channel, chunk, loops);
}

void AudioDB::HaltChannel(int channel) {
    AudioHelper::Mix_HaltChannel(channel);
}

void AudioDB::SetVolume(int channel, int volume) {
    AudioHelper::Mix_Volume(channel, volume);
}
