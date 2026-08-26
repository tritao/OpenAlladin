#include "sprites.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint32_t>(r)
        | (static_cast<std::uint32_t>(g) << 8)
        | (static_cast<std::uint32_t>(b) << 16)
        | 0xFF000000U;
}

}  // namespace

int main() {
    using namespace openaladdin;

    SpriteDatabase database;
    const std::string sprite_root = std::filesystem::exists("build/assets/sprites")
        ? "build/assets/sprites" : "../build/assets/sprites";
    database.load(sprite_root);
    assert(database.frame_for(SpritePose::Idle).index == SpriteDatabase::kIdleFrame);
    assert(database.frame_for(SpritePose::Run).index == SpriteDatabase::kRunFrame);
    assert(database.frame_for(SpritePose::Jump).index == SpriteDatabase::kJumpFrame);
    assert(database.frame(SpriteDatabase::kIdleFrame).origin_x == 0x80);
    assert(database.frame(SpriteDatabase::kIdleFrame).parts.front().offset_x == -24);
    assert(database.palette().size() == 16);

    std::vector<SDL_Color> palette;
    for (int i = 0; i < 32; ++i) {
        palette.push_back(SDL_Color{
            static_cast<std::uint8_t>(i),
            static_cast<std::uint8_t>(i + 32),
            static_cast<std::uint8_t>(i + 64),
            255,
        });
    }

    SpriteFrame frame;
    frame.origin_x = 0x80;
    frame.origin_y = 0x80;

    SpritePart body;
    body.offset_x = -2;
    body.offset_y = -1;
    body.width = 2;
    body.height = 1;
    body.pixels = {1, 2};
    body.layer = 0;
    frame.parts.push_back(body);

    SpritePart overlay;
    overlay.offset_x = 0;
    overlay.offset_y = 0;
    overlay.width = 1;
    overlay.height = 1;
    overlay.palette_line = 1;
    overlay.pixels = {2};
    overlay.layer = 1;
    frame.parts.push_back(overlay);

    std::vector<std::uint32_t> framebuffer(8 * 8, 0);
    SpriteRenderer::draw(frame, palette, framebuffer, 8, 8, 4, 4);

    // Exact origin and part offset: (-2,-1) lands at (2,3).
    assert(framebuffer[3 * 8 + 2] == rgba(1, 33, 65));
    assert(framebuffer[3 * 8 + 3] == rgba(2, 34, 66));
    // Later multipart layers overwrite earlier pixels, and palette_line 1
    // selects indices 16..31 rather than reusing the first palette line.
    assert(framebuffer[4 * 8 + 4] == rgba(18, 50, 82));

    std::fill(framebuffer.begin(), framebuffer.end(), 0);
    SpriteRenderer::draw(frame, palette, framebuffer, 8, 8, 4, 4, true, false);
    // X flip mirrors the part's placement around the supplied origin and
    // reverses its pixel order.
    assert(framebuffer[3 * 8 + 4] == rgba(2, 34, 66));
    assert(framebuffer[3 * 8 + 5] == rgba(1, 33, 65));
    assert(framebuffer[4 * 8 + 3] == rgba(18, 50, 82));

    std::fill(framebuffer.begin(), framebuffer.end(), 0);
    SpriteRenderer::draw(frame, palette, framebuffer, 8, 8, 4, 4, false, true);
    // Y flip mirrors the top-left part across the world-to-screen origin.
    assert(framebuffer[4 * 8 + 2] == rgba(1, 33, 65));
    assert(framebuffer[4 * 8 + 3] == rgba(2, 34, 66));

    // Genesis VDP sprites use a column-first tile sequence. Four solid
    // tiles make the layout and colour-zero transparency easy to verify
    // without depending on the extracted asset tree.
    std::vector<std::uint8_t> vdp_rom(4 * 32, 0);
    std::fill(vdp_rom.begin() + 0 * 32, vdp_rom.begin() + 1 * 32, 0x11);
    std::fill(vdp_rom.begin() + 1 * 32, vdp_rom.begin() + 2 * 32, 0x22);
    std::fill(vdp_rom.begin() + 2 * 32, vdp_rom.begin() + 3 * 32, 0x33);
    std::fill(vdp_rom.begin() + 3 * 32, vdp_rom.begin() + 4 * 32, 0x44);
    std::vector<std::uint32_t> vdp_framebuffer(16 * 16, 0);
    SpriteRenderer::draw_vdp_sprite(
        vdp_rom,
        0,
        2,
        2,
        palette,
        vdp_framebuffer,
        16,
        16,
        0,
        0,
        0
    );
    // Top-left/bottom-left are tiles 0/1; top-right/bottom-right are 2/3.
    assert(vdp_framebuffer[0 * 16 + 0] == rgba(1, 33, 65));
    assert(vdp_framebuffer[8 * 16 + 0] == rgba(2, 34, 66));
    assert(vdp_framebuffer[0 * 16 + 8] == rgba(3, 35, 67));
    assert(vdp_framebuffer[8 * 16 + 8] == rgba(4, 36, 68));

    return 0;
}
