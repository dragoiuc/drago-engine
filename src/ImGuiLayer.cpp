#include "ImGuiLayer.h"

#include "DB.h"
#include "Engine.h"
#include "Helper.h"
#include "Input.h"
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <imgui.h>

ImGuiLayer::~ImGuiLayer() {
    Shutdown();
}

void ImGuiLayer::Initialize(SDL_Window* window, SDL_Renderer* renderer) {
    if (initialized || window == nullptr || renderer == nullptr) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ImGui::DestroyContext();
        return;
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    initialized = true;
}

void ImGuiLayer::Shutdown() {
    if (!initialized) {
        return;
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    initialized = false;
    overlay_visible = false;
    show_demo_window = false;
}

void ImGuiLayer::ProcessEvent(const SDL_Event& event) {
    if (!initialized) {
        return;
    }

    if (event.type == SDL_KEYDOWN &&
        event.key.repeat == 0 &&
        event.key.keysym.scancode == SDL_SCANCODE_F1) {
        overlay_visible = !overlay_visible;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
}

void ImGuiLayer::BeginFrame() {
    if (!initialized) {
        return;
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

bool ImGuiLayer::BuildDefaultOverlay(const Engine& engine) {
    if (!initialized) {
        return false;
    }

    bool returnToLauncherRequested = false;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin(
        "Session Controls",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::Button("Back To Launcher")) {
            returnToLauncherRequested = true;
        }
        ImGui::TextDisabled("Press F1 for debug");
    }
    ImGui::End();

    if (overlay_visible) {
        const DB& db = engine._db;
        const glm::vec2 mousePosition = Input::GetMousePosition();
        const ImGuiIO& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(ImVec2(16.0f, 88.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Engine Debug", &overlay_visible)) {
            ImGui::TextUnformatted("Press F1 to toggle this overlay.");
            ImGui::Separator();
            ImGui::Text("Frame: %d", Helper::GetFrameNumber());
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Text("Scene: %s",
                engine.current_scene_name.empty() ? "<none>" : engine.current_scene_name.c_str());
            ImGui::Text("Actors: %d", static_cast<int>(db.actors.size()));
            ImGui::Text("Resolution: %d x %d", db.x_resolution, db.y_resolution);
            ImGui::Text("Clear Color: %d, %d, %d",
                db.clear_color_r,
                db.clear_color_g,
                db.clear_color_b);
            ImGui::Text("Camera: %.2f, %.2f",
                engine.currentCameraPosition.x,
                engine.currentCameraPosition.y);
            ImGui::Text("Zoom: %.2f", db.zoom_factor);
            ImGui::Text("Mouse: %.1f, %.1f", mousePosition.x, mousePosition.y);
            ImGui::Text("Intro Active: %s", engine.HasActiveIntro() ? "yes" : "no");
            ImGui::Text("Game Over: %s",
                engine.gameOverActive ? (engine.gameOverWon ? "won" : "lost") : "no");

            if (engine.player != nullptr) {
                ImGui::Separator();
                ImGui::Text("Player Health: %d", engine.player->health);
                ImGui::Text("Player Score: %d", engine.player->score);
                ImGui::Text("Player Position: %.2f, %.2f",
                    engine.player->position.x,
                    engine.player->position.y);
            }

            ImGui::Separator();
            ImGui::Checkbox("Show ImGui demo window", &show_demo_window);
        }
        ImGui::End();
    }

    if (show_demo_window) {
        ImGui::ShowDemoWindow(&show_demo_window);
    }

    return returnToLauncherRequested;
}

void ImGuiLayer::Render(SDL_Renderer* renderer) {
    if (!initialized || renderer == nullptr) {
        return;
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}

bool ImGuiLayer::WantsMouseCapture() const {
    return initialized && ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantsKeyboardCapture() const {
    return initialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiLayer::IsVisible() const {
    return overlay_visible;
}
