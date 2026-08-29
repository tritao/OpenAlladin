#include "sdl_render_backend.hpp"

#include <stdexcept>
#include <string>

namespace openaladdin {

SdlRenderBackend::~SdlRenderBackend() {
    release_texture();
}

void SdlRenderBackend::release_texture() {
    // Engine instances can outlive SDL_Quit in the command-line frontend.
    // SDL no longer owns a live video device at that point, so discard the
    // host handle without calling into the shut-down subsystem.
    if (texture_ != nullptr && SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_DestroyTexture(texture_);
    }
    texture_ = nullptr;
    renderer_ = nullptr;
    width_ = 0;
    height_ = 0;
}

void SdlRenderBackend::present(
    SDL_Renderer* renderer,
    const std::vector<std::uint32_t>& framebuffer,
    int width,
    int height
) {
    if (renderer == nullptr) {
        throw std::invalid_argument("SDL render backend requires a renderer");
    }
    if (width <= 0 || height <= 0
        || framebuffer.size() != static_cast<std::size_t>(width * height)) {
        throw std::invalid_argument("SDL render backend received an invalid framebuffer");
    }

    if (texture_ == nullptr || renderer_ != renderer
        || width_ != width || height_ != height) {
        release_texture();
        texture_ = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
        );
        if (texture_ == nullptr) {
            throw std::runtime_error(
                std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        }
        renderer_ = renderer;
        width_ = width;
        height_ = height;
    }

    if (SDL_UpdateTexture(
            texture_,
            nullptr,
            framebuffer.data(),
            width * static_cast<int>(sizeof(std::uint32_t))) != 0) {
        throw std::runtime_error(
            std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
    }
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

}  // namespace openaladdin
