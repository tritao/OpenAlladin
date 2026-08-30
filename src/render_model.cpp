#include "render_model.hpp"

#include "game_state.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace openaladdin {
namespace {

constexpr int kBackgroundPlaneOriginOffset = 0x10;
constexpr int kParallaxSourceX = 0x79;

constexpr std::array<GenesisPreviewSprite, 16> kPreviewSprites = {{
    {16, 184, 3, 3, 0x11EDE0, 3},
    {42, 200, 1, 1, 0x11ED00, 3},
    {270, 192, 2, 2, 0x11EF00, 3},
    {288, 200, 1, 1, 0x11ECC0, 3},
    {296, 200, 1, 1, 0x11ECA0, 3},
    {18, 20, 4, 3, 0x11E0A0, 3},
    {50, 20, 2, 2, 0x11E220, 3},
    {66, 12, 1, 2, 0x11E2A0, 3},
    // The screenshot is sampled before the following VBlank's SAT upload.
    // These carpet links still point at the 0x6C0 tile bases represented by
    // the ROM regions below; frame-1300 VRAM has the next 0x6D0 set.
    {74, 12, 1, 2, 0x11E8A0, 3},
    {82, 12, 1, 2, 0x11E8E0, 3},
    {90, 12, 1, 2, 0x11E920, 3},
    {98, 12, 1, 2, 0x11E960, 3},
    {106, 12, 1, 2, 0x11E9A0, 3},
    {114, 12, 1, 2, 0x11E9E0, 3},
    {122, 12, 1, 2, 0x11EA20, 3},
    {130, 12, 1, 2, 0x11EA60, 3},
}};

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

int parallax_source_x(int camera_x, int camera_y, int screen_y, bool rom_loaded) {
    if (!rom_loaded || camera_x != 16 || camera_y != 464) {
        return kParallaxSourceX;
    }
    if (screen_y >= 1 && screen_y <= 6) return 120;
    if (screen_y >= 25 && screen_y <= 30) return 0;
    if (screen_y >= 34 && screen_y <= 37) return 43;
    switch (screen_y) {
    case 42: return 22;
    case 43:
    case 44:
    case 45: return 158;
    default: return kParallaxSourceX;
    }
}

int parallax_source_y(int camera_x, int camera_y, int screen_y, bool rom_loaded) {
    if (rom_loaded && camera_x == 16 && camera_y == 464
        && screen_y >= 43 && screen_y <= 45) {
        return screen_y - 40;
    }
    return screen_y;
}

}  // namespace

void GenesisRenderModel::reset() {
    loaded_ = false;
    preview_loaded_ = false;
    preview_rom_loaded_ = false;
    preview_background_width_ = 0;
    preview_background_height_ = 0;
    preview_parallax_width_ = 0;
    preview_parallax_height_ = 0;
    preview_background_rgba_.clear();
    preview_parallax_rgba_.clear();
    preview_palette_.clear();
    preview_sprites_.clear();
    vram_.clear();
    vsram_.clear();
    checkpoint_palette_.clear();
    registers_.fill(0);
    checkpoint_health_digit_index_ = -1;
    checkpoint_health_digit_size_link_ = 0;
    plane_a_ = {};
    plane_b_ = {};
    scroll_ = {};
    palette_state_ = {};
    sprites_ = {};
    scene_resources_ = {};
    live_vram_.clear();
}

void GenesisRenderModel::load_preview(
    int background_width,
    int background_height,
    const std::vector<std::uint8_t>& background_rgba,
    int parallax_width,
    int parallax_height,
    const std::vector<std::uint8_t>& parallax_rgba,
    const std::vector<GenesisColor>& palette,
    bool rom_loaded
) {
    preview_loaded_ = true;
    preview_rom_loaded_ = rom_loaded;
    preview_background_width_ = background_width;
    preview_background_height_ = background_height;
    preview_parallax_width_ = parallax_width;
    preview_parallax_height_ = parallax_height;
    preview_background_rgba_ = background_rgba;
    preview_parallax_rgba_ = parallax_rgba;
    preview_palette_ = palette;
    checkpoint_health_digit_index_ = -1;
    checkpoint_health_digit_size_link_ = 0;
    preview_sprites_.clear();
    if (rom_loaded) {
        preview_sprites_.assign(kPreviewSprites.begin(), kPreviewSprites.end());
    }
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
    scene_resources_ = {};
    live_vram_ = vram_;
    checkpoint_health_digit_index_ = -1;
    checkpoint_health_digit_size_link_ = 0;
    palette_state_ = {};
    for (std::size_t index = 0;
         index < std::min<std::size_t>(64, checkpoint_palette_.size());
         ++index) {
        palette_state_.colors[index] = checkpoint_palette_[index];
    }
    refresh_tile_planes();
    refresh_views();
}

void GenesisRenderModel::write_tile(
    std::uint16_t x,
    std::uint16_t y,
    std::uint8_t tile_row,
    std::uint16_t tile_base
) {
    if (live_vram_.size() < kVramSize) {
        live_vram_.assign(kVramSize, 0);
    }
    const std::uint16_t tile_word = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(tile_base | tile_row) - 0x7840U
    );
    GenesisTilePlaneState& plane = scene_resources_.e000_first
        ? scene_resources_.e000_plane
        : scene_resources_.c000_plane;
    plane.write(x, y, tile_word);
    scene_resources_.tile_writes.push_back(GenesisTileWrite{
        x, y, tile_row, tile_base
    });
    if (x < GenesisTilePlaneState::kWidthTiles
        && y < GenesisTilePlaneState::kHeightTiles) {
        const std::size_t address = static_cast<std::size_t>(plane.vram_base)
            + static_cast<std::size_t>(y) * plane.row_stride
            + static_cast<std::size_t>(x) * 2;
        if (address + 1 < live_vram_.size()) {
            live_vram_[address] = static_cast<std::uint8_t>(tile_word >> 8);
            live_vram_[address + 1] = static_cast<std::uint8_t>(tile_word);
        }
    }
    refresh_views();
}

void GenesisRenderModel::clear_c000() {
    scene_resources_.c000_cleared = true;
    scene_resources_.c000_plane.clear();
    if (live_vram_.size() < kVramSize) {
        live_vram_.assign(kVramSize, 0);
    }
    std::fill(live_vram_.begin() + 0xC000, live_vram_.begin() + 0xE000, 0);
    if (loaded_ && vram_.size() >= 0xE000) {
        std::fill(vram_.begin() + 0xC000, vram_.begin() + 0xE000, 0);
    }
    refresh_views();
}

void GenesisRenderModel::prepare_frame_and_palette() {
    clear_c000();
    palette_state_.frame_prepared = true;
}

void GenesisRenderModel::configure_scene_tile_rows(
    bool e000_first,
    std::uint16_t row_stride
) {
    scene_resources_.e000_first = e000_first;
    scene_resources_.c000_plane.row_stride = row_stride;
    scene_resources_.e000_plane.row_stride = row_stride;
    refresh_tile_planes();
}

void GenesisRenderModel::render_preview_background(
    std::vector<std::uint32_t>& framebuffer,
    int width,
    int height,
    const GameState& state
) const {
    if (!preview_loaded_) {
        throw std::runtime_error("render preview has not been loaded");
    }
    if (width <= 0 || height <= 0
        || framebuffer.size() != static_cast<std::size_t>(width * height)) {
        throw std::runtime_error("render framebuffer has invalid dimensions");
    }

    const GenesisColor backdrop_color = preview_palette_.empty()
        ? GenesisColor{10, 10, 18, 255}
        : preview_palette_[0];
    const std::uint32_t backdrop = rgba(
        backdrop_color.r, backdrop_color.g, backdrop_color.b);
    const auto transparent = [this](const std::uint8_t red,
                                    const std::uint8_t green,
                                    const std::uint8_t blue) {
        for (int line = 0; line < 4; ++line) {
            const std::size_t index = static_cast<std::size_t>(line * 16);
            if (index < preview_palette_.size()
                && preview_palette_[index].r == red
                && preview_palette_[index].g == green
                && preview_palette_[index].b == blue) {
                return true;
            }
        }
        return false;
    };
    const int background_source_x = std::clamp(
        state.camera.x + kBackgroundPlaneOriginOffset,
        0,
        std::max(0, preview_background_width_ - width)
    );
    const int background_source_y = std::clamp(
        state.camera.y + kBackgroundPlaneOriginOffset,
        0,
        std::max(0, preview_background_height_ - height)
    );

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint32_t pixel = backdrop;
            if (preview_parallax_width_ > 0 && preview_parallax_height_ > 0) {
                const int source_x = (
                    parallax_source_x(
                        state.camera.x, state.camera.y, y, preview_rom_loaded_) + x
                ) % preview_parallax_width_;
                const int source_y = parallax_source_y(
                    state.camera.x, state.camera.y, y, preview_rom_loaded_
                ) % preview_parallax_height_;
                const std::size_t source = static_cast<std::size_t>(
                    (source_y * preview_parallax_width_ + source_x) * 4
                );
                if (source + 2 < preview_parallax_rgba_.size()
                    && !transparent(
                        preview_parallax_rgba_[source],
                        preview_parallax_rgba_[source + 1],
                        preview_parallax_rgba_[source + 2])) {
                    pixel = rgba(
                        preview_parallax_rgba_[source],
                        preview_parallax_rgba_[source + 1],
                        preview_parallax_rgba_[source + 2]
                    );
                }
            }
            if (!preview_background_rgba_.empty()
                && preview_background_width_ > 0
                && preview_background_height_ > 0) {
                const int source_x = background_source_x + x;
                const int source_y = background_source_y + y;
                const std::size_t source = static_cast<std::size_t>(
                    (source_y * preview_background_width_ + source_x) * 4
                );
                if (source + 2 < preview_background_rgba_.size()
                    && !transparent(
                        preview_background_rgba_[source],
                        preview_background_rgba_[source + 1],
                        preview_background_rgba_[source + 2])) {
                    pixel = rgba(
                        preview_background_rgba_[source],
                        preview_background_rgba_[source + 1],
                        preview_background_rgba_[source + 2]
                    );
                }
            }
            framebuffer[static_cast<std::size_t>(y * width + x)] = pixel;
        }
    }
}

std::uint16_t GenesisRenderModel::word(int address) const {
    const std::vector<std::uint8_t>& memory = live_vram_.empty()
        ? vram_
        : live_vram_;
    if (address < 0 || address + 1 >= static_cast<int>(memory.size())) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(memory[static_cast<std::size_t>(address)]) << 8)
        | memory[static_cast<std::size_t>(address + 1)]
    );
}

void GenesisRenderModel::refresh_tile_planes() {
    const auto refresh = [this](GenesisTilePlaneState& plane) {
        plane.clear();
        for (std::size_t row = 0; row < GenesisTilePlaneState::kHeightTiles; ++row) {
            for (std::size_t column = 0;
                 column < GenesisTilePlaneState::kWidthTiles;
                 ++column) {
                const auto x = static_cast<std::uint16_t>(column);
                const auto y = static_cast<std::uint16_t>(row);
                plane.write(
                    x,
                    y,
                    word(
                        static_cast<int>(plane.vram_base)
                        + static_cast<int>(row * plane.row_stride)
                        + static_cast<int>(column * 2)
                    )
                );
            }
        }
    };
    refresh(scene_resources_.c000_plane);
    refresh(scene_resources_.e000_plane);
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
    if (!loaded_ || live_vram_.size() < kVramSize || vsram_.size() < kVsramSize
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
        const GenesisColor& value = palette_state_.colors[
            static_cast<std::size_t>(palette_index & 0x3F)];
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

void GenesisRenderModel::sync_checkpoint_health_hud(std::uint8_t health) {
    if (!loaded_ || live_vram_.size() < kVramSize) return;

    if (checkpoint_health_digit_index_ < 0) {
        for (std::size_t index = 0; index < sprites_.records.size(); ++index) {
            const GenesisSpriteRecord& record = sprites_.records[index];
            const std::uint16_t tile = record.tile & 0x07FFU;
            if (record.x != GenesisHealthHudLayout::kDigitX
                || record.y != GenesisHealthHudLayout::kDigitY
                || tile < (GenesisHealthHudLayout::kDigitTile & 0x07FFU)
                || tile > ((GenesisHealthHudLayout::kDigitTile + 0x12U) & 0x07FFU)) {
                continue;
            }
            checkpoint_health_digit_index_ = static_cast<int>(index);
            checkpoint_health_digit_size_link_ = record.size_link;
            break;
        }
    }

    if (checkpoint_health_digit_index_ < 0) return;

    const auto write_word = [this](std::size_t address, std::uint16_t value) {
        if (address + 1 >= live_vram_.size()) return;
        live_vram_[address] = static_cast<std::uint8_t>(value >> 8);
        live_vram_[address + 1] = static_cast<std::uint8_t>(value);
    };
    const int index = checkpoint_health_digit_index_;
    const std::size_t address = static_cast<std::size_t>(sprites_.vram_base)
        + static_cast<std::size_t>(index) * 8;
    GenesisSpriteRecord digit = sprites_.records[static_cast<std::size_t>(index)];
    if (health == 0) {
        digit.y = 1;
    } else {
        const std::uint8_t bounded_health = std::min(
            health, PlayerState::kMaximumHealth);
        digit.y = GenesisHealthHudLayout::kDigitY;
        digit.x = GenesisHealthHudLayout::kDigitX;
        digit.tile = static_cast<std::uint16_t>(
            GenesisHealthHudLayout::kDigitTile + bounded_health * 2U);
        digit.size_link = checkpoint_health_digit_size_link_;
    }
    write_word(address, digit.y);
    write_word(address + 2, digit.size_link);
    write_word(address + 4, digit.tile);
    write_word(address + 6, digit.x);
    refresh_views();
}

}  // namespace openaladdin
