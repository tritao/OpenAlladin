#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openaladdin {

enum class SpritePose {
    Idle,
    Run,
    Brake,
    Jump,
    Landing,
};

struct SpritePart {
    // Coordinates are pixels relative to the Chopper frame origin (0x80,
    // 0x80 in the on-cartridge byte representation).
    int offset_x = 0;
    int offset_y = 0;
    int width = 0;
    int height = 0;
    int tile_index = 0;
    int palette_line = 0;
    bool flip_x = false;
    bool flip_y = false;
    int layer = 0;

    // Decoded Genesis 4bpp indices, row-major, with zero as transparent.
    std::vector<std::uint8_t> pixels;
};

struct SpriteFrame {
    int index = -1;
    int address = 0;
    int origin_x = 0x80;
    int origin_y = 0x80;
    int collision_min_x = 0;
    int collision_min_y = 0;
    int collision_max_x = 0;
    int collision_max_y = 0;
    std::vector<SpritePart> parts;
};

class SpriteDatabase {
public:
    // Loads the generated Chopper frames.json and its sibling SEG tile sets.
    void load(const std::string& sprite_root);

    const SpriteFrame& frame(int index) const;
    int frame_index_for_address(int address) const;
    const SpriteFrame& frame_for(SpritePose pose) const;
    const std::vector<SDL_Color>& palette() const { return palette_; }
    void set_palette(const std::vector<SDL_Color>& palette) { palette_ = palette; }

    // Representative frame IDs from the recovered player streams. The
    // animation VM selects among these records at runtime.
    static constexpr int kIdleFrame = 201;
    static constexpr int kRunFrame = 202;
    static constexpr int kBrakeFrame = 233;
    static constexpr int kJumpFrame = 161;
    static constexpr int kLandingFrame = 171;
    // The level-01 gameplay checkpoint uses CRAM entries 0x30..0x3F,
    // palette line 3. The frame metadata keeps its local indices; rendering
    // applies this runtime line to the player multipart frame.
    static constexpr int kPlayerPaletteLine = 3;

private:
    std::vector<SpriteFrame> frames_;
    std::vector<SDL_Color> palette_;
};

class SpriteRenderer {
public:
    // Draw one decoded multipart frame into a packed RGBA framebuffer. The
    // supplied screen origin is the world point transformed by the caller;
    // all Chopper part offsets are applied from that exact origin.
    static void draw(
        const SpriteFrame& frame,
        const std::vector<SDL_Color>& palette,
        std::vector<std::uint32_t>& framebuffer,
        int framebuffer_width,
        int framebuffer_height,
        int screen_origin_x,
        int screen_origin_y,
        bool flip_x = false,
        bool flip_y = false,
        int palette_line_override = -1
    );
};

}  // namespace openaladdin
