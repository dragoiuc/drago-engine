#include "ImageDB.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "DB.h"
#include "Helper.h"
#include "ResourcePaths.h"

namespace fs = std::filesystem;

SDL_Renderer* ImageDB::renderer = nullptr;
std::unordered_map<std::string, ImageDB::ImageInfo> ImageDB::imageCache;
ImageDB::ImageInfo ImageDB::default_particle_image;
std::vector<ImageDB::SceneDrawRequest> ImageDB::scene_draw_requests;
std::vector<ImageDB::UIDrawRequest> ImageDB::ui_draw_requests;
std::vector<ImageDB::PixelDrawRequest> ImageDB::pixel_draw_requests;
bool ImageDB::scene_draw_requests_sorted = true;
bool ImageDB::ui_draw_requests_sorted = true;
int ImageDB::last_scene_sorting_order = 0;
int ImageDB::last_ui_sorting_order = 0;

static fs::path ResolveImagePath(const std::string& imageName) {
    return ResourcePaths::ResolveExistingGameFile(std::vector<fs::path>{
        fs::path("images") / fs::path(imageName),
        fs::path("images") / fs::path(imageName + ".png")
        });
}

static int ClampColor(float value) {
    return std::clamp(static_cast<int>(value), 0, 255);
}

static void ApplyTextureMods(SDL_Texture* texture, int r, int g, int b, int a) {
    SDL_SetTextureColorMod(
        texture,
        static_cast<Uint8>(r),
        static_cast<Uint8>(g),
        static_cast<Uint8>(b));
    SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(a));
}

static void ResetTextureMods(SDL_Texture* texture) {
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
}

static bool NeedsTextureMods(int r, int g, int b, int a) {
    return r != 255 || g != 255 || b != 255 || a != 255;
}

void ImageDB::Shutdown() {
    for (auto& entry : imageCache) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    imageCache.clear();

    if (default_particle_image.texture != nullptr) {
        SDL_DestroyTexture(default_particle_image.texture);
    }
    default_particle_image = ImageInfo{};

    scene_draw_requests.clear();
    ui_draw_requests.clear();
    pixel_draw_requests.clear();
    scene_draw_requests_sorted = true;
    ui_draw_requests_sorted = true;
    last_scene_sorting_order = 0;
    last_ui_sorting_order = 0;
}

void ImageDB::Init(SDL_Renderer* rendererPtr) {
    renderer = rendererPtr;
}

void ImageDB::BeginFrame() {
    scene_draw_requests.clear();
    ui_draw_requests.clear();
    pixel_draw_requests.clear();
    scene_draw_requests_sorted = true;
    ui_draw_requests_sorted = true;
    last_scene_sorting_order = 0;
    last_ui_sorting_order = 0;
}

void ImageDB::LoadAll(const std::vector<std::string>& imageNames) {
    for (const std::string& imageName : imageNames) {
        GetImage(imageName);
    }
}

SDL_Texture* ImageDB::GetImage(const std::string& imageName) {
    return GetImageInfo(imageName).texture;
}

const ImageDB::ImageInfo& ImageDB::GetImageInfo(const std::string& imageName) {
    auto found = imageCache.find(imageName);
    if (found != imageCache.end()) {
        return found->second;
    }

    const fs::path imagePath = ResolveImagePath(imageName);
    if (imagePath.empty()) {
        std::cout << "error: missing image " << imageName;
        std::exit(0);
    }

    SDL_Texture* texture = IMG_LoadTexture(renderer, imagePath.string().c_str());
    if (texture == nullptr) {
        std::cout << "error: missing image " << imageName;
        std::exit(0);
    }

    float width = 0.0f;
    float height = 0.0f;
    Helper::SDL_QueryTexture(texture, &width, &height);

    imageCache[imageName] = ImageInfo{ texture, width, height };
    return imageCache[imageName];
}

const ImageDB::ImageInfo& ImageDB::GetDefaultParticleImageInfo() {
    if (default_particle_image.texture != nullptr || renderer == nullptr) {
        return default_particle_image;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        std::cout << "error: failed to create default particle texture";
        std::exit(0);
    }

    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 255, 255, 255, 255));

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);

    if (texture == nullptr) {
        std::cout << "error: failed to create default particle texture";
        std::exit(0);
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    default_particle_image = ImageInfo{ texture, 8.0f, 8.0f };
    return default_particle_image;
}

void ImageDB::DrawUI(const std::string& image_name, float x, float y) {
    DrawUIEx(image_name, x, y, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f);
}

void ImageDB::DrawUIEx(
    const std::string& image_name,
    float x,
    float y,
    float r,
    float g,
    float b,
    float a,
    float sorting_order) {
    const ImageInfo& imageInfo = GetImageInfo(image_name);

    UIDrawRequest request;
    request.texture = imageInfo.texture;
    request.width = imageInfo.width;
    request.height = imageInfo.height;
    request.x = static_cast<int>(x);
    request.y = static_cast<int>(y);
    request.r = ClampColor(r);
    request.g = ClampColor(g);
    request.b = ClampColor(b);
    request.a = ClampColor(a);
    request.sorting_order = static_cast<int>(sorting_order);
    if (!ui_draw_requests.empty() && request.sorting_order < last_ui_sorting_order) {
        ui_draw_requests_sorted = false;
    }
    last_ui_sorting_order = request.sorting_order;
    ui_draw_requests.push_back(request);
}

void ImageDB::Draw(const std::string& image_name, float x, float y) {
    DrawEx(
        image_name,
        x,
        y,
        0.0f,
        1.0f,
        1.0f,
        0.5f,
        0.5f,
        255.0f,
        255.0f,
        255.0f,
        255.0f,
        0.0f);
}

void ImageDB::DrawEx(
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
    float sorting_order) {
    DrawEx(
        GetImageInfo(image_name),
        x,
        y,
        rotation_degrees,
        scale_x,
        scale_y,
        pivot_x,
        pivot_y,
        r,
        g,
        b,
        a,
        sorting_order);
}

void ImageDB::DrawEx(
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
    float sorting_order) {
    QueueDrawEx(
        image_info,
        x,
        y,
        static_cast<int>(rotation_degrees),
        scale_x,
        scale_y,
        pivot_x,
        pivot_y,
        ClampColor(r),
        ClampColor(g),
        ClampColor(b),
        ClampColor(a),
        static_cast<int>(sorting_order));
}

void ImageDB::QueueDrawEx(
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
    int sorting_order) {
    SceneDrawRequest request;
    request.texture = image_info.texture;
    request.width = image_info.width;
    request.height = image_info.height;
    request.x = x;
    request.y = y;
    request.rotation_degrees = rotation_degrees;
    request.scale_x = scale_x;
    request.scale_y = scale_y;
    request.pivot_x = pivot_x;
    request.pivot_y = pivot_y;
    request.r = r;
    request.g = g;
    request.b = b;
    request.a = a;
    request.sorting_order = sorting_order;
    if (!scene_draw_requests.empty() && request.sorting_order < last_scene_sorting_order) {
        scene_draw_requests_sorted = false;
    }
    last_scene_sorting_order = request.sorting_order;
    scene_draw_requests.push_back(request);
}

void ImageDB::DrawPixel(float x, float y, float r, float g, float b, float a) {
    PixelDrawRequest request;
    request.x = static_cast<int>(x);
    request.y = static_cast<int>(y);
    request.r = ClampColor(r);
    request.g = ClampColor(g);
    request.b = ClampColor(b);
    request.a = ClampColor(a);
    pixel_draw_requests.push_back(request);
}

void ImageDB::RenderQueuedImages(const DB& db, const glm::vec2& camera_position) {
    if (renderer == nullptr) {
        scene_draw_requests.clear();
        ui_draw_requests.clear();
        return;
    }

    if (scene_draw_requests.size() > 1 && !scene_draw_requests_sorted) {
        std::stable_sort(
            scene_draw_requests.begin(),
            scene_draw_requests.end(),
            [](const SceneDrawRequest& lhs, const SceneDrawRequest& rhs) {
                return lhs.sorting_order < rhs.sorting_order;
            });
    }

    if (ui_draw_requests.size() > 1 && !ui_draw_requests_sorted) {
        std::stable_sort(
            ui_draw_requests.begin(),
            ui_draw_requests.end(),
            [](const UIDrawRequest& lhs, const UIDrawRequest& rhs) {
                return lhs.sorting_order < rhs.sorting_order;
            });
    }

    if (!scene_draw_requests.empty()) {
        const float zoomFactor = db.zoom_factor > 0.0f ? db.zoom_factor : 1.0f;
        const float screenCenterX = static_cast<float>(db.x_resolution) * 0.5f;
        const float screenCenterY = static_cast<float>(db.y_resolution) * 0.5f;
        const float logicalScreenWidth = static_cast<float>(db.x_resolution) / zoomFactor;
        const float logicalScreenHeight = static_cast<float>(db.y_resolution) / zoomFactor;
        const float logicalScreenCenterX = screenCenterX / zoomFactor;
        const float logicalScreenCenterY = screenCenterY / zoomFactor;
        SDL_Texture* currentModTexture = nullptr;
        int currentModR = 255;
        int currentModG = 255;
        int currentModB = 255;
        int currentModA = 255;
        SDL_RenderSetScale(renderer, zoomFactor, zoomFactor);

        for (const SceneDrawRequest& request : scene_draw_requests) {
            if (request.texture == nullptr || request.a <= 0) {
                continue;
            }

            const float absScaleX = glm::abs(request.scale_x);
            const float absScaleY = glm::abs(request.scale_y);
            const float scaledWidth = request.width * absScaleX;
            const float scaledHeight = request.height * absScaleY;
            if (scaledWidth <= 0.0f || scaledHeight <= 0.0f) {
                continue;
            }
            const float scaledPivotX = request.pivot_x * request.width * absScaleX;
            const float scaledPivotY = request.pivot_y * request.height * absScaleY;

            SDL_FRect dstRect = {
                (request.x - camera_position.x) * 100.0f + logicalScreenCenterX - scaledPivotX,
                (request.y - camera_position.y) * 100.0f + logicalScreenCenterY - scaledPivotY,
                scaledWidth,
                scaledHeight
            };

            if (dstRect.x + dstRect.w < 0.0f ||
                dstRect.y + dstRect.h < 0.0f ||
                dstRect.x > logicalScreenWidth ||
                dstRect.y > logicalScreenHeight) {
                continue;
            }

            SDL_RendererFlip flip = SDL_FLIP_NONE;
            if (request.scale_x < 0.0f) {
                flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_HORIZONTAL);
            }
            if (request.scale_y < 0.0f) {
                flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_VERTICAL);
            }

            if (currentModTexture != request.texture ||
                currentModR != request.r ||
                currentModG != request.g ||
                currentModB != request.b ||
                currentModA != request.a) {
                ApplyTextureMods(request.texture, request.r, request.g, request.b, request.a);
                currentModTexture = request.texture;
                currentModR = request.r;
                currentModG = request.g;
                currentModB = request.b;
                currentModA = request.a;
            }
            if (request.rotation_degrees == 0 && flip == SDL_FLIP_NONE) {
                Helper::SDL_RenderCopy(renderer, request.texture, nullptr, &dstRect);
            }
            else {
                const SDL_FPoint center = {
                    scaledPivotX,
                    scaledPivotY
                };
                Helper::SDL_RenderCopyEx(
                    -1,
                    "",
                    renderer,
                    request.texture,
                    nullptr,
                    &dstRect,
                    static_cast<float>(request.rotation_degrees),
                    &center,
                    flip);
            }
        }

        SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    }

    SDL_Texture* currentUiModTexture = nullptr;
    int currentUiModR = 255;
    int currentUiModG = 255;
    int currentUiModB = 255;
    int currentUiModA = 255;
    for (const UIDrawRequest& request : ui_draw_requests) {
        if (request.texture == nullptr) {
            continue;
        }

        SDL_FRect dstRect = {
            static_cast<float>(request.x),
            static_cast<float>(request.y),
            request.width,
            request.height
        };

        if (currentUiModTexture != request.texture ||
            currentUiModR != request.r ||
            currentUiModG != request.g ||
            currentUiModB != request.b ||
            currentUiModA != request.a) {
            ApplyTextureMods(request.texture, request.r, request.g, request.b, request.a);
            currentUiModTexture = request.texture;
            currentUiModR = request.r;
            currentUiModG = request.g;
            currentUiModB = request.b;
            currentUiModA = request.a;
        }
        Helper::SDL_RenderCopy(renderer, request.texture, nullptr, &dstRect);
    }

    scene_draw_requests.clear();
    ui_draw_requests.clear();
}

void ImageDB::RenderQueuedPixels() {
    if (renderer == nullptr) {
        pixel_draw_requests.clear();
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (const PixelDrawRequest& request : pixel_draw_requests) {
        SDL_SetRenderDrawColor(
            renderer,
            static_cast<Uint8>(request.r),
            static_cast<Uint8>(request.g),
            static_cast<Uint8>(request.b),
            static_cast<Uint8>(request.a));
        SDL_RenderDrawPoint(renderer, request.x, request.y);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    pixel_draw_requests.clear();
}
