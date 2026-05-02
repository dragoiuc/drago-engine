#pragma once

#include <string>
#include <unordered_map>

#include "SDL2_mixer/SDL_mixer.h"

class AudioDB
{
public:
    static void Init();
    static void Shutdown();
    static bool HasClip(const std::string& clipName);
    static Mix_Chunk* LoadClip(const std::string& clipName);
    static void PlayChannel(int channel, const std::string& clipName, int loops);
    static void HaltChannel(int channel);
    static void SetVolume(int channel, int volume);

private:
    static std::unordered_map<std::string, Mix_Chunk*> audioCache;
};
