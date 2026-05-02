#include "Renderer.h"

#include "DB.h"
#include "Helper.h"

Renderer::~Renderer() {
    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

void Renderer::Initialize(const std::string& title, int width, int height) {
    if (window != nullptr && renderer != nullptr) {
        SDL_SetWindowTitle(window, title.c_str());
        SDL_SetWindowSize(window, width, height);
        return;
    }

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    window = Helper::SDL_CreateWindow(
        title.c_str(),
        100,
        100,
        width,
        height,
        SDL_WINDOW_SHOWN);

    renderer = Helper::SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
}

void Renderer::Initialize(const DB& db) {
    Initialize(db.game_title, db.x_resolution, db.y_resolution);
}

void Renderer::BeginFrame(const DB& db) {
    BeginFrame(
        static_cast<Uint8>(db.clear_color_r),
        static_cast<Uint8>(db.clear_color_g),
        static_cast<Uint8>(db.clear_color_b));
}

void Renderer::BeginFrame(Uint8 clear_r, Uint8 clear_g, Uint8 clear_b) {
    SDL_SetRenderDrawColor(
        renderer,
        clear_r,
        clear_g,
        clear_b,
        255);

    SDL_RenderClear(renderer);
}

void Renderer::Present() const {
    Helper::SDL_RenderPresent(renderer);
}

SDL_Window* Renderer::GetSDLWindow() const {
    return window;
}

SDL_Renderer* Renderer::GetSDLRenderer() const {
    return renderer;
}
