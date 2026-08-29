#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openaladdin {

struct GameState;

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

struct GenesisTileWrite {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t tile_row = 0;
    std::uint16_t tile_base = 0;
};

// Typed state for scene-resource operations that do not yet have a complete
// direct plane-map representation. The recovered VDP row-command table still
// determines the final destination of a tile write, so retain the operation
// until that table is promoted rather than guessing a plane address. C000
// clears and palette preparation are observable semantic operations even when
// no checkpoint-backed VDP memory is loaded.
struct GenesisSceneResourceState {
    std::vector<GenesisTileWrite> tile_writes;
    bool c000_cleared = false;
    bool frame_palette_prepared = false;
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

// A preview sprite is a decoded descriptor for a static Genesis sprite whose
// pattern data is still sourced from the original ROM. It is kept separate
// from GenesisSpriteRecord because preview assets do not have a live SAT
// address when no VDP checkpoint is loaded.
struct GenesisPreviewSprite {
    int screen_x = 0;
    int screen_y = 0;
    int width_tiles = 0;
    int height_tiles = 0;
    int tile_address = 0;
    int palette_line = 0;
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

    // Bind the decoded preview planes used when no VDP checkpoint is
    // available. These are render-model inputs rather than Level lookups so
    // the frontend has one owner for backdrop, parallax, palette and camera
    // sampling policy.
    void load_preview(
        int background_width,
        int background_height,
        const std::vector<std::uint8_t>& background_rgba,
        int parallax_width,
        int parallax_height,
        const std::vector<std::uint8_t>& parallax_rgba,
        const std::vector<GenesisColor>& palette,
        bool rom_loaded
    );

    void load_checkpoint(
        std::vector<std::uint8_t> vram,
        std::vector<std::uint8_t> vsram,
        std::vector<GenesisColor> palette,
        std::array<std::uint8_t, 32> registers,
        bool loaded = true
    );

    bool loaded() const { return loaded_; }

    int preview_background_width() const { return preview_background_width_; }
    int preview_background_height() const { return preview_background_height_; }
    const std::vector<GenesisPreviewSprite>& preview_sprites() const {
        return preview_sprites_;
    }

    void render_preview_background(
        std::vector<std::uint32_t>& framebuffer,
        int width,
        int height,
        const GameState& state
    ) const;

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
    const GenesisSceneResourceState& scene_resources() const {
        return scene_resources_;
    }
    const std::vector<GenesisTileWrite>& scene_tile_writes() const {
        return scene_resources_.tile_writes;
    }
    bool c000_cleared() const { return scene_resources_.c000_cleared; }
    bool frame_palette_prepared() const {
        return scene_resources_.frame_palette_prepared;
    }

private:
    void refresh_views();
    std::uint16_t word(int address) const;

    bool loaded_ = false;
    bool preview_loaded_ = false;
    bool preview_rom_loaded_ = false;
    int preview_background_width_ = 0;
    int preview_background_height_ = 0;
    int preview_parallax_width_ = 0;
    int preview_parallax_height_ = 0;
    std::vector<std::uint8_t> preview_background_rgba_;
    std::vector<std::uint8_t> preview_parallax_rgba_;
    std::vector<GenesisColor> preview_palette_;
    std::vector<GenesisPreviewSprite> preview_sprites_;

    std::vector<std::uint8_t> vram_;
    std::vector<std::uint8_t> vsram_;
    std::vector<GenesisColor> checkpoint_palette_;
    std::array<std::uint8_t, 32> registers_{};

    GenesisPlaneState plane_a_{};
    GenesisPlaneState plane_b_{};
    GenesisScrollState scroll_{};
    std::array<GenesisColor, 64> palette_{};
    GenesisSpriteList sprites_{};

    GenesisSceneResourceState scene_resources_{};
};

}  // namespace openaladdin
