#pragma once

#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class DB;

class ImageDB
{
public:
    static void Shutdown();
    struct ImageInfo {
        SDL_Texture* texture = nullptr;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct SceneDrawRequest {
        SDL_Texture* texture = nullptr;
        float width = 0.0f;
        float height = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        int rotation_degrees = 0;
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        float pivot_x = 0.5f;
        float pivot_y = 0.5f;
        int r = 255;
        int g = 255;
        int b = 255;
        int a = 255;
        int sorting_order = 0;
    };

    struct UIDrawRequest {
        SDL_Texture* texture = nullptr;
        float width = 0.0f;
        float height = 0.0f;
        int x = 0;
        int y = 0;
        int r = 255;
        int g = 255;
        int b = 255;
        int a = 255;
        int sorting_order = 0;
    };

    struct PixelDrawRequest {
        int x = 0;
        int y = 0;
        int r = 255;
        int g = 255;
        int b = 255;
        int a = 255;
    };

    static void Init(SDL_Renderer* renderer);
    static void BeginFrame();
    static void LoadAll(const std::vector<std::string>& imageNames);
    static SDL_Texture* GetImage(const std::string& imageName);
    static const ImageInfo& GetImageInfo(const std::string& imageName);
    static const ImageInfo& GetDefaultParticleImageInfo();
    static void DrawUI(const std::string& image_name, float x, float y);
    static void DrawUIEx(
        const std::string& image_name,
        float x,
        float y,
        float r,
        float g,
        float b,
        float a,
        float sorting_order);
    static void Draw(const std::string& image_name, float x, float y);
    static void DrawEx(
        const std::string& image_name,
        float x,
        float y,
        float rotation_degrees,
        float scale_x,
        float scale_y,
        float pivot_x,
        float pivot_y,
        float r,
        float g,
        float b,
        float a,
        float sorting_order);
    static void DrawEx(
        const ImageInfo& image_info,
        float x,
        float y,
        float rotation_degrees,
        float scale_x,
        float scale_y,
        float pivot_x,
        float pivot_y,
        float r,
        float g,
        float b,
        float a,
        float sorting_order);
    static void QueueDrawEx(
        const ImageInfo& image_info,
        float x,
        float y,
        int rotation_degrees,
        float scale_x,
        float scale_y,
        float pivot_x,
        float pivot_y,
        int r,
        int g,
        int b,
        int a,
        int sorting_order);
    static void DrawPixel(float x, float y, float r, float g, float b, float a);
    static void RenderQueuedImages(const DB& db, const glm::vec2& camera_position);
    static void RenderQueuedPixels();

private:
    static SDL_Renderer* renderer;
    static std::unordered_map<std::string, ImageInfo> imageCache;
    static ImageInfo default_particle_image;
    static std::vector<SceneDrawRequest> scene_draw_requests;
    static std::vector<UIDrawRequest> ui_draw_requests;
    static std::vector<PixelDrawRequest> pixel_draw_requests;
    static bool scene_draw_requests_sorted;
    static bool ui_draw_requests_sorted;
    static int last_scene_sorting_order;
    static int last_ui_sorting_order;
};
