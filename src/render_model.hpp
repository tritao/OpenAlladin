#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace openaladdin {

struct GenesisColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct GenesisPlaneState {
    std::uint16_t vram_base = 0;
    int width_tiles = 32;
    int height_tiles = 32;
};

struct GenesisScrollState {
    std::uint16_t horizontal_base = 0;
    std::uint8_t horizontal_mode = 0;
    int vertical_a = 0;
    int vertical_b = 0;
};

struct GenesisSpriteRecord {
    std::uint16_t y = 1;
    std::uint16_t size_link = 0;
    std::uint16_t tile = 0;
    std::uint16_t x = 0;
};

struct GenesisSpriteList {
    std::uint16_t vram_base = 0;
    std::array<GenesisSpriteRecord, 80> records{};
};

// Semantic frontend state for the Genesis VDP. The raw VRAM/VSRAM and
// register arrays remain as a compatibility backing store for existing VDP
// checkpoints; the public views expose the planes, palette, scroll and SAT
// concepts consumed by rendering.
class GenesisRenderModel {
public:
    static constexpr std::size_t kVramSize = 0x10000;
    static constexpr std::size_t kVsramSize = 0x80;

    void reset();

    void load_checkpoint(
        std::vector<std::uint8_t> vram,
        std::vector<std::uint8_t> vsram,
        std::vector<GenesisColor> palette,
        std::array<std::uint8_t, 32> registers,
        bool loaded = true
    );

    bool loaded() const { return loaded_; }

    void write_tile(
        std::uint16_t x,
        std::uint16_t y,
        std::uint8_t tile_row,
        std::uint16_t tile_base
    );
    void clear_c000();
    void prepare_frame_and_palette();

    void render(
        std::vector<std::uint32_t>& framebuffer,
        int width,
        int height
    ) const;

    const GenesisPlaneState& plane_a() const { return plane_a_; }
    const GenesisPlaneState& plane_b() const { return plane_b_; }
    const GenesisScrollState& scroll() const { return scroll_; }
    const std::array<GenesisColor, 64>& palette() const { return palette_; }
    const GenesisSpriteList& sprites() const { return sprites_; }

    const std::vector<std::uint8_t>& checkpoint_vram() const { return vram_; }
    const std::vector<std::uint8_t>& checkpoint_vsram() const { return vsram_; }
    const std::vector<GenesisColor>& checkpoint_palette() const { return checkpoint_palette_; }
    const std::array<std::uint8_t, 32>& checkpoint_registers() const { return registers_; }
    const std::vector<std::array<std::uint16_t, 4>>& scene_tile_writes() const {
        return scene_tile_writes_;
    }

private:
    void refresh_views();
    std::uint16_t word(int address) const;

    bool loaded_ = false;
    std::vector<std::uint8_t> vram_;
    std::vector<std::uint8_t> vsram_;
    std::vector<GenesisColor> checkpoint_palette_;
    std::array<std::uint8_t, 32> registers_{};

    GenesisPlaneState plane_a_{};
    GenesisPlaneState plane_b_{};
    GenesisScrollState scroll_{};
    std::array<GenesisColor, 64> palette_{};
    GenesisSpriteList sprites_{};

    // SceneResourceVm writes are retained as semantic tile operations until
    // the recovered VDP row-command table is promoted to a direct plane map.
    // This makes the callback observable without guessing a destination.
    std::vector<std::array<std::uint16_t, 4>> scene_tile_writes_;
};

}  // namespace openaladdin
