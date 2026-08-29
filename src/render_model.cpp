#include "render_model.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace openaladdin {
namespace {

std::uint32_t rgba(
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a = 255
) {
    return static_cast<std::uint32_t>(r)
        | (static_cast<std::uint32_t>(g) << 8)
        | (static_cast<std::uint32_t>(b) << 16)
        | (static_cast<std::uint32_t>(a) << 24);
}

}  // namespace

void GenesisRenderModel::reset() {
    loaded_ = false;
    vram_.clear();
    vsram_.clear();
    checkpoint_palette_.clear();
    registers_.fill(0);
    plane_a_ = {};
    plane_b_ = {};
    scroll_ = {};
    palette_.fill({});
    sprites_ = {};
    scene_tile_writes_.clear();
}

void GenesisRenderModel::load_checkpoint(
    std::vector<std::uint8_t> vram,
    std::vector<std::uint8_t> vsram,
    std::vector<GenesisColor> palette,
    std::array<std::uint8_t, 32> registers,
    bool loaded
) {
    vram_ = std::move(vram);
    vsram_ = std::move(vsram);
    checkpoint_palette_ = std::move(palette);
    registers_ = registers;
    loaded_ = loaded;
    scene_tile_writes_.clear();
    refresh_views();
}

void GenesisRenderModel::write_tile(
    std::uint16_t x,
    std::uint16_t y,
    std::uint8_t tile_row,
    std::uint16_t tile_base
) {
    scene_tile_writes_.push_back({x, y, tile_row, tile_base});
}

void GenesisRenderModel::clear_c000() {
    if (loaded_ && vram_.size() >= 0xE000) {
        std::fill(vram_.begin() + 0xC000, vram_.begin() + 0xE000, 0);
        refresh_views();
    }
}

void GenesisRenderModel::prepare_frame_and_palette() {
    // The recovered command currently clears C000 before the palette service;
    // palette progression will become a typed operation as its source banks
    // are promoted into GameData.
    clear_c000();
}

std::uint16_t GenesisRenderModel::word(int address) const {
    if (address < 0 || address + 1 >= static_cast<int>(vram_.size())) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(vram_[static_cast<std::size_t>(address)]) << 8)
        | vram_[static_cast<std::size_t>(address + 1)]
    );
}

void GenesisRenderModel::refresh_views() {
    const int plane_width_tiles = (registers_[16] & 0x03) == 1 ? 64 : 32;
    const int plane_height_tiles = ((registers_[16] >> 4) & 0x03) == 1 ? 64 : 32;
    plane_a_ = GenesisPlaneState{
        static_cast<std::uint16_t>((registers_[2] & 0x38) << 10),
        plane_width_tiles,
        plane_height_tiles,
    };
    plane_b_ = GenesisPlaneState{
        static_cast<std::uint16_t>((registers_[4] & 0x07) << 13),
        plane_width_tiles,
        plane_height_tiles,
    };
    scroll_.horizontal_base = static_cast<std::uint16_t>((registers_[13] & 0x3F) << 10);
    scroll_.horizontal_mode = registers_[11] & 0x03;
    const auto vsram_word = [this](int index) {
        const int address = index * 2;
        if (address < 0 || address + 1 >= static_cast<int>(vsram_.size())) return 0;
        return static_cast<int>(
            (static_cast<std::uint16_t>(vsram_[static_cast<std::size_t>(address)]) << 8)
            | vsram_[static_cast<std::size_t>(address + 1)]
        ) & 0x03FF;
    };
    scroll_.vertical_a = vsram_word(0);
    scroll_.vertical_b = vsram_word(1);

    palette_.fill({});
    for (std::size_t index = 0; index < std::min<std::size_t>(64, checkpoint_palette_.size()); ++index) {
        palette_[index] = checkpoint_palette_[index];
    }

    sprites_ = {};
    sprites_.vram_base = static_cast<std::uint16_t>((registers_[5] & 0x7F) << 9);
    for (std::size_t index = 0; index < sprites_.records.size(); ++index) {
        const int address = sprites_.vram_base + static_cast<int>(index) * 8;
        sprites_.records[index] = GenesisSpriteRecord{
            word(address),
            word(address + 2),
            word(address + 4),
            word(address + 6),
        };
    }
}

void GenesisRenderModel::render(
    std::vector<std::uint32_t>& framebuffer,
    int width,
    int height
) const {
    if (!loaded_ || vram_.size() < kVramSize || vsram_.size() < kVsramSize
        || checkpoint_palette_.size() < 64) {
        throw std::runtime_error("VDP checkpoint has incomplete memory state");
    }
    if (width <= 0 || height <= 0
        || framebuffer.size() != static_cast<std::size_t>(width * height)) {
        throw std::runtime_error("render framebuffer has invalid dimensions");
    }

    const auto wrap = [](int value, int size) {
        value %= size;
        return value < 0 ? value + size : value;
    };
    const auto color = [this](int palette_index) {
        const GenesisColor& value = palette_[static_cast<std::size_t>(palette_index & 0x3F)];
        return rgba(value.r, value.g, value.b);
    };
    const auto tile_pixel = [this](std::uint16_t tile_word, int x, int y) {
        const int tile_address = static_cast<int>(tile_word & 0x07FF) * 32;
        const int row_address = tile_address + (y & 7) * 4;
        const int byte_address = row_address + ((x & 7) >> 1);
        const std::uint8_t packed = static_cast<std::uint8_t>(word(byte_address) >> 8);
        return static_cast<std::uint8_t>((x & 1) == 0 ? packed >> 4 : packed & 0x0F);
    };

    const std::uint32_t backdrop = color(registers_[7]);
    std::fill(framebuffer.begin(), framebuffer.end(), backdrop);

    const auto hscroll_index = [this](int screen_y) {
        switch (scroll_.horizontal_mode) {
        case 1: return (screen_y / 8) * 4;
        case 2: return (screen_y / 8) * 16;
        case 3: return screen_y * 2;
        default: return 0;
        }
    };
    const auto draw_plane = [&](const GenesisPlaneState& plane, bool plane_b, bool priority) {
        const int plane_width_pixels = plane.width_tiles * 8;
        const int plane_height_pixels = plane.height_tiles * 8;
        const int hscroll_offset = plane_b ? 1 : 0;
        const int vscroll = plane_b ? scroll_.vertical_b : scroll_.vertical_a;
        for (int screen_y = 0; screen_y < height; ++screen_y) {
            const int scroll_index = hscroll_index(screen_y) + hscroll_offset;
            const int hscroll = (-static_cast<int>(word(
                scroll_.horizontal_base + scroll_index * 2))) & 0x01FF;
            for (int screen_x = 0; screen_x < width; ++screen_x) {
                const int source_x = wrap(screen_x + hscroll, plane_width_pixels);
                const int source_y = wrap(screen_y + vscroll, plane_height_pixels);
                const int tile_x = source_x / 8;
                const int tile_y = source_y / 8;
                const std::uint16_t tile_word = word(
                    plane.vram_base + (tile_y * plane.width_tiles + tile_x) * 2
                );
                if (((tile_word & 0x8000) != 0) != priority) continue;
                const int tile_pixel_x = (tile_word & 0x0800) != 0
                    ? 7 - (source_x & 7) : source_x & 7;
                const int tile_pixel_y = (tile_word & 0x1000) != 0
                    ? 7 - (source_y & 7) : source_y & 7;
                const std::uint8_t pixel = tile_pixel(tile_word, tile_pixel_x, tile_pixel_y);
                if (pixel == 0) continue;
                framebuffer[static_cast<std::size_t>(screen_y * width + screen_x)] =
                    color(((tile_word >> 13) & 0x03) * 16 + pixel);
            }
        }
    };

    draw_plane(plane_b_, true, false);
    draw_plane(plane_a_, false, false);

    const auto draw_sprites = [&](bool priority) {
        std::array<bool, 80> visited{};
        int index = 0;
        for (int count = 0; count < static_cast<int>(visited.size()); ++count) {
            if (index < 0 || index >= static_cast<int>(visited.size()) || visited[index]) break;
            visited[index] = true;
            const GenesisSpriteRecord& sprite = sprites_.records[static_cast<std::size_t>(index)];
            const int next = sprite.size_link & 0x7F;
            const bool sprite_priority = (sprite.tile & 0x8000) != 0;
            if (sprite.y != 1 && sprite_priority == priority) {
                const int width_tiles = ((sprite.size_link >> 10) & 0x03) + 1;
                const int height_tiles = ((sprite.size_link >> 8) & 0x03) + 1;
                const int sprite_width = width_tiles * 8;
                const int sprite_height = height_tiles * 8;
                const int screen_x = (sprite.x & 0x01FF) - 128;
                const int screen_y = (sprite.y & 0x01FF) - 128;
                const bool flip_x = (sprite.tile & 0x0800) != 0;
                const bool flip_y = (sprite.tile & 0x1000) != 0;
                const int palette_start = ((sprite.tile >> 13) & 0x03) * 16;
                for (int y = 0; y < sprite_height; ++y) {
                    for (int x = 0; x < sprite_width; ++x) {
                        const int source_x = flip_x ? sprite_width - 1 - x : x;
                        const int source_y = flip_y ? sprite_height - 1 - y : y;
                        const int tile_column = source_x / 8;
                        const int tile_row = source_y / 8;
                        const std::uint16_t part_tile = static_cast<std::uint16_t>(
                            (sprite.tile & 0x07FF) + tile_column * height_tiles + tile_row
                        );
                        const std::uint8_t pixel = tile_pixel(
                            part_tile, source_x & 7, source_y & 7
                        );
                        const int draw_x = screen_x + x;
                        const int draw_y = screen_y + y;
                        if (pixel == 0 || draw_x < 0 || draw_x >= width
                            || draw_y < 0 || draw_y >= height) continue;
                        framebuffer[static_cast<std::size_t>(draw_y * width + draw_x)] =
                            color(palette_start + pixel);
                    }
                }
            }
            if (next == 0) break;
            index = next;
        }
    };

    draw_sprites(false);
    draw_plane(plane_b_, true, true);
    draw_plane(plane_a_, false, true);
    draw_sprites(true);
}

}  // namespace openaladdin
