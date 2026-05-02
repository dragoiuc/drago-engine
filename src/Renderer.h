#pragma once

#include <SDL2/SDL.h>
#include <string>

class DB;

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Initialize(const std::string& title, int width, int height);
    void Initialize(const DB& db);
    void BeginFrame(const DB& db);
    void BeginFrame(Uint8 clear_r, Uint8 clear_g, Uint8 clear_b);
    void Present() const;

    SDL_Window* GetSDLWindow() const;
    SDL_Renderer* GetSDLRenderer() const;

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
};
