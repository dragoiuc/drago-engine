#pragma once

#include <SDL2/SDL.h>

class Engine;

class ImGuiLayer
{
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void Initialize(SDL_Window* window, SDL_Renderer* renderer);
    void Shutdown();

    void ProcessEvent(const SDL_Event& event);
    void BeginFrame();
    bool BuildDefaultOverlay(const Engine& engine);
    void Render(SDL_Renderer* renderer);

    bool WantsMouseCapture() const;
    bool WantsKeyboardCapture() const;
    bool IsVisible() const;

private:
    bool initialized = false;
    bool overlay_visible = false;
    bool show_demo_window = false;
};
