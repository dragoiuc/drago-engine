#include "TextDB.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "DB.h"
#include "Helper.h"
#include "ResourcePaths.h"

namespace fs = std::filesystem;

SDL_Renderer* TextDB::renderer = nullptr;
std::string TextDB::default_font_name = "";
std::unordered_map<std::string, std::unordered_map<int, TTF_Font*>> TextDB::fontCache;
std::unordered_map<std::string, TextDB::CachedText> TextDB::textCache;
std::vector<TextDB::TextDrawRequest> TextDB::draw_requests;
namespace {
    bool g_text_initialized = false;
}

static fs::path ResolveFontPath(const std::string& fontName) {
    return ResourcePaths::ResolveExistingGameFile(std::vector<fs::path>{
        fs::path("fonts") / fs::path(fontName),
        fs::path("fonts") / fs::path(fontName + ".ttf")
        });
}

TTF_Font* TextDB::GetFontOrExit(const std::string& font_name, int font_size) {
    auto fontFamilyIt = TextDB::fontCache.find(font_name);
    if (fontFamilyIt != TextDB::fontCache.end()) {
        auto sizedFontIt = fontFamilyIt->second.find(font_size);
        if (sizedFontIt != fontFamilyIt->second.end()) {
            return sizedFontIt->second;
        }
    }

    const fs::path fontPath = ResolveFontPath(font_name);
    if (fontPath.empty()) {
        std::cout << "error: font " << font_name << " missing";
        std::exit(0);
    }

    TTF_Font* font = TTF_OpenFont(fontPath.string().c_str(), font_size);
    if (font == nullptr) {
        std::cout << "error: font " << font_name << " missing";
        std::exit(0);
    }

    TextDB::fontCache[font_name][font_size] = font;
    return font;
}

static std::string BuildTextCacheKey(
    const std::string& text,
    const std::string& font_name,
    int font_size,
    int r,
    int g,
    int b,
    int a) {
    std::ostringstream keyBuilder;
    keyBuilder
        << font_name << '\x1f'
        << font_size << '\x1f'
        << r << '\x1f'
        << g << '\x1f'
        << b << '\x1f'
        << a << '\x1f'
        << text;
    return keyBuilder.str();
}

void TextDB::Init(SDL_Renderer* rendererPtr) {
    renderer = rendererPtr;
    if (!g_text_initialized) {
        TTF_Init();
        g_text_initialized = true;
    }
}

void TextDB::Shutdown() {
    for (auto& familyEntry : fontCache) {
        for (auto& sizeEntry : familyEntry.second) {
            if (sizeEntry.second != nullptr) {
                TTF_CloseFont(sizeEntry.second);
            }
        }
    }
    fontCache.clear();

    for (auto& textEntry : textCache) {
        if (textEntry.second.texture != nullptr) {
            SDL_DestroyTexture(textEntry.second.texture);
        }
    }
    textCache.clear();
    draw_requests.clear();
    default_font_name.clear();

    if (g_text_initialized) {
        TTF_Quit();
        g_text_initialized = false;
    }
}

void TextDB::LoadConfiguredFont(const DB& db) {
    default_font_name = db.font;
    if (!default_font_name.empty()) {
        TextDB::GetFontOrExit(default_font_name, 16);
    }

    if (!db.intro_images.empty() && !db.intro_text.empty() && default_font_name.empty()) {
        std::cout << "error: text render failed. No font configured";
        std::exit(0);
    }
}

void TextDB::DrawText(const std::string& text, float x, float y) {
    if (default_font_name.empty()) {
        return;
    }

    DrawText(text, x, y, default_font_name, 16.0f, 255.0f, 255.0f, 255.0f, 255.0f);
}

void TextDB::DrawText(
    const std::string& text,
    float x,
    float y,
    const std::string& font_name,
    float font_size,
    float r,
    float g,
    float b,
    float a) {
    if (renderer == nullptr || font_name.empty()) {
        return;
    }

    TextDrawRequest request;
    request.text = text;
    request.font_name = font_name;
    request.x = static_cast<int>(x);
    request.y = static_cast<int>(y);
    request.font_size = std::max(1, static_cast<int>(font_size));
    request.r = std::clamp(static_cast<int>(r), 0, 255);
    request.g = std::clamp(static_cast<int>(g), 0, 255);
    request.b = std::clamp(static_cast<int>(b), 0, 255);
    request.a = std::clamp(static_cast<int>(a), 0, 255);
    draw_requests.push_back(std::move(request));
}

void TextDB::BeginFrame() {
    draw_requests.clear();
}

TextDB::CachedText& TextDB::GetCachedText(
    const std::string& text,
    const std::string& font_name,
    int font_size,
    int r,
    int g,
    int b,
    int a) {
    const std::string cacheKey = BuildTextCacheKey(text, font_name, font_size, r, g, b, a);
    auto found = textCache.find(cacheKey);
    if (found != textCache.end()) {
        return found->second;
    }

    TTF_Font* font = TextDB::GetFontOrExit(font_name, font_size);
    SDL_Color color = {
        static_cast<Uint8>(r),
        static_cast<Uint8>(g),
        static_cast<Uint8>(b),
        static_cast<Uint8>(a)
    };

    CachedText cachedText;
    if (!text.empty()) {
        SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color);
        if (surface != nullptr) {
            cachedText.width = surface->w;
            cachedText.height = surface->h;
            cachedText.texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
            if (cachedText.texture != nullptr) {
                SDL_SetTextureBlendMode(cachedText.texture, SDL_BLENDMODE_BLEND);
            }
        }
    }

    auto inserted = textCache.emplace(cacheKey, cachedText);
    return inserted.first->second;
}

void TextDB::RenderQueuedText() {
    if (renderer == nullptr) {
        draw_requests.clear();
        return;
    }

    for (const TextDrawRequest& request : draw_requests) {
        CachedText& cachedText = GetCachedText(
            request.text,
            request.font_name,
            request.font_size,
            request.r,
            request.g,
            request.b,
            request.a);
        if (cachedText.texture == nullptr) {
            continue;
        }

        SDL_FRect dstRect = {
            static_cast<float>(request.x),
            static_cast<float>(request.y),
            static_cast<float>(cachedText.width),
            static_cast<float>(cachedText.height)
        };

        Helper::SDL_RenderCopy(renderer, cachedText.texture, nullptr, &dstRect);
    }

    draw_requests.clear();
}
