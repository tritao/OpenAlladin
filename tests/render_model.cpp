#include "render_model.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace openaladdin;

    std::vector<std::uint8_t> vram(GenesisRenderModel::kVramSize, 0);
    std::vector<std::uint8_t> vsram(GenesisRenderModel::kVsramSize, 0);
    std::vector<GenesisColor> palette(64);
    palette[2] = GenesisColor{1, 2, 3, 255};
    std::array<std::uint8_t, 32> registers{};
    registers[2] = 0x38;
    registers[4] = 0x07;
    registers[5] = 0x01;
    registers[7] = 2;
    registers[11] = 2;
    registers[13] = 0x3F;
    registers[16] = 0x11;
    vram[0xBFFF] = 1;
    vram[0xC000] = 2;
    vram[0xDFFF] = 3;
    vram[0xE000] = 4;

    const int sat_base = 0x200;
    vram[sat_base + 2] = 0x80;
    vram[sat_base + 3] = 0x01;
    vram[sat_base + 4] = 0x20;
    vram[sat_base + 5] = 0x02;
    vram[sat_base + 6] = 0x01;
    vram[sat_base + 7] = 0x40;
    vsram[0] = 0x01;
    vsram[1] = 0x20;
    vsram[2] = 0x02;
    vsram[3] = 0x40;

    GenesisRenderModel model;
    model.load_checkpoint(vram, vsram, palette, registers);
    assert(model.loaded());
    assert(model.plane_a().vram_base == 0xE000);
    assert(model.plane_b().vram_base == 0xE000);
    assert(model.plane_a().width_tiles == 64);
    assert(model.plane_a().height_tiles == 64);
    assert(model.scroll().horizontal_base == 0xFC00);
    assert(model.scroll().horizontal_mode == 2);
    assert(model.scroll().vertical_a == 0x0120);
    assert(model.scroll().vertical_b == 0x0240);
    assert(model.palette()[2].r == 1);
    assert(model.sprites().vram_base == 0x0200);
    assert(model.sprites().records[0].size_link == 0x8001);
    assert(model.sprites().records[0].tile == 0x2002);
    assert(model.sprites().records[0].x == 0x0140);

    model.write_tile(4, 7, 3, 0x4000);
    assert(model.scene_tile_writes().size() == 1);
    assert(model.scene_tile_writes()[0][0] == 4);
    assert(model.scene_tile_writes()[0][1] == 7);
    assert(model.scene_tile_writes()[0][2] == 3);
    assert(model.scene_tile_writes()[0][3] == 0x4000);

    model.clear_c000();
    assert(model.checkpoint_vram()[0xBFFF] == 1);
    assert(model.checkpoint_vram()[0xC000] == 0);
    assert(model.checkpoint_vram()[0xDFFF] == 0);
    assert(model.checkpoint_vram()[0xE000] == 4);

    std::vector<std::uint32_t> framebuffer(320 * 224, 0);
    model.render(framebuffer, 320, 224);
    assert(framebuffer.front() == 0xFF030201U);

    model.reset();
    assert(!model.loaded());
    assert(model.scene_tile_writes().empty());
    assert(model.checkpoint_vram().empty());

    std::vector<GenesisColor> preview_palette(64);
    model.load_preview(
        320,
        224,
        {},
        320,
        224,
        {},
        preview_palette,
        true
    );
    assert(model.preview_background_width() == 320);
    assert(model.preview_background_height() == 224);
    assert(model.preview_sprites().size() == 16);
    assert(model.preview_sprites().front().tile_address == 0x11EDE0);
    assert(model.preview_sprites().back().palette_line == 3);

    model.reset();
    assert(model.preview_sprites().empty());

    return 0;
}
