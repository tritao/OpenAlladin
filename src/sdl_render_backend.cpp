#include "sdl_render_backend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>

namespace openaladdin {
namespace {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6;
constexpr int kLineAdvance = 8;
constexpr int kOverlayMargin = 3;

// Tiny presentation-only font. Keeping this here avoids adding SDL_ttf as a
// runtime dependency just for a development aid. Rows use the low five bits.
std::array<std::uint8_t, kGlyphHeight> glyph(char character) {
    const char upper = static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
    switch (upper) {
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    case 'C': return {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F};
    case 'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'G': return {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    case 'J': return {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '+': return {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    case '/': return {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
    case ':': return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    case ',': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x08};
    case '=': return {0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '[': return {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E};
    case ']': return {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E};
    case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    case '?': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    default: return {0, 0, 0, 0, 0, 0, 0};
    }
}

void draw_debug_overlay(
    SDL_Renderer* renderer,
    const std::vector<std::string>& lines,
    int native_width,
    int native_height
) {
    if (lines.empty()) return;

    int output_width = native_width;
    int output_height = native_height;
    SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
    const float scale_x = static_cast<float>(output_width) / native_width;
    const float scale_y = static_cast<float>(output_height) / native_height;

    const std::size_t max_line_length = std::min<std::size_t>(
        50,
        std::max_element(
            lines.begin(),
            lines.end(),
            [](const std::string& left, const std::string& right) {
                return left.size() < right.size();
            }
        )->size()
    );
    const int panel_native_width = std::min(
        native_width,
        kOverlayMargin * 2 + static_cast<int>(max_line_length) * kGlyphAdvance
    );
    const int panel_native_height = std::min(
        native_height,
        kOverlayMargin * 2 + static_cast<int>(lines.size()) * kLineAdvance
    );
    const auto pixel_x = [scale_x](int value) {
        return static_cast<int>(std::floor(value * scale_x));
    };
    const auto pixel_y = [scale_y](int value) {
        return static_cast<int>(std::floor(value * scale_y));
    };
    const auto pixel_width = [scale_x](int value) {
        return std::max(1, static_cast<int>(std::ceil(value * scale_x)));
    };
    const auto pixel_height = [scale_y](int value) {
        return std::max(1, static_cast<int>(std::ceil(value * scale_y)));
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 205);
    SDL_Rect panel{
        0,
        0,
        pixel_x(panel_native_width),
        pixel_y(panel_native_height)
    };
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 176, 255, 176, 255);
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const std::string& line = lines[line_index];
        const int native_y = kOverlayMargin + static_cast<int>(line_index) * kLineAdvance;
        for (std::size_t character_index = 0;
             character_index < line.size()
                 && character_index * kGlyphAdvance < max_line_length * kGlyphAdvance;
             ++character_index) {
            const auto rows = glyph(line[character_index]);
            for (int row = 0; row < kGlyphHeight; ++row) {
                for (int column = 0; column < kGlyphWidth; ++column) {
                    if ((rows[static_cast<std::size_t>(row)]
                         & (1U << (kGlyphWidth - 1 - column))) == 0) {
                        continue;
                    }
                    SDL_Rect pixel{
                        pixel_x(kOverlayMargin
                            + static_cast<int>(character_index) * kGlyphAdvance + column),
                        pixel_y(native_y + row),
                        pixel_width(1),
                        pixel_height(1)
                    };
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

}  // namespace

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
    int height,
    const std::vector<std::string>& debug_overlay
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
    draw_debug_overlay(renderer, debug_overlay, width, height);
    SDL_RenderPresent(renderer);
}

}  // namespace openaladdin
