#pragma once

#include <SDL2/SDL.h>
#include <SDL2_ttf/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <vector>

class DB;

class TextDB
{
public:
    static void Shutdown();
    struct CachedText {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
    };

    struct TextDrawRequest {
        std::string text = "";
        std::string font_name = "";
        int x = 0;
        int y = 0;
        int font_size = 16;
        int r = 255;
        int g = 255;
        int b = 255;
        int a = 255;
    };

    static void Init(SDL_Renderer* renderer);
    static void LoadConfiguredFont(const DB& db);
    static void DrawText(const std::string& text, float x, float y);
    static void DrawText(
        const std::string& text,
        float x,
        float y,
        const std::string& font_name,
        float font_size,
        float r,
        float g,
        float b,
        float a);
    static void BeginFrame();
    static void RenderQueuedText();

private:
    static TTF_Font* GetFontOrExit(const std::string& font_name, int font_size);
    static CachedText& GetCachedText(
        const std::string& text,
        const std::string& font_name,
        int font_size,
        int r,
        int g,
        int b,
        int a);
    static SDL_Renderer* renderer;
    static std::string default_font_name;
    static std::unordered_map<std::string, std::unordered_map<int, TTF_Font*>> fontCache;
    static std::unordered_map<std::string, CachedText> textCache;
    static std::vector<TextDrawRequest> draw_requests;
};
