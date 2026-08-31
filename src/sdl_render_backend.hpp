#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openaladdin {

// SDL is deliberately limited to this presentation boundary. Simulation
// and framebuffer composition remain usable without creating a renderer.
class SdlRenderBackend {
public:
    SdlRenderBackend() = default;
    ~SdlRenderBackend();

    SdlRenderBackend(const SdlRenderBackend&) = delete;
    SdlRenderBackend& operator=(const SdlRenderBackend&) = delete;

    void present(
        SDL_Renderer* renderer,
        const std::vector<std::uint32_t>& framebuffer,
        int width,
        int height,
        const std::vector<std::string>& debug_overlay = {}
    );

private:
    void release_texture();

    SDL_Texture* texture_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace openaladdin
