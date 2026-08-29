#include "render_model.hpp"
#include "scene_resource.hpp"

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
    vram[0xC1C8] = 0x12;
    vram[0xC1C9] = 0x34;
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

    const std::vector<std::uint8_t> write_rom{
        0x01, 0x04,  // x += 4
        0x02, 0x07,  // y += 7
        0x0A,        // tile base 0x4000
        0x23,        // tile row 3
        0x00,        // end
    };
    GameState scene_state;
    SceneResourceVm write_vm;
    write_vm.bind_rom(write_rom);
    write_vm.start(0);
    SceneServices write_services;
    write_services.write_tile = [&model](GameState&, const SceneTileWrite& write) {
        model.write_tile(write.x, write.y, write.tile_row, write.tile_base);
    };
    assert(write_vm.tick(scene_state, write_services)
        == SceneResourceRunResult::Finished);
    assert(model.scene_tile_writes().size() == 1);
    assert(model.scene_tile_writes()[0].x == 4);
    assert(model.scene_tile_writes()[0].y == 7);
    assert(model.scene_tile_writes()[0].tile_row == 3);
    assert(model.scene_tile_writes()[0].tile_base == 0x4000);
    assert(model.scene_resources().c000_plane.vram_base == 0xC000);
    assert(model.scene_resources().c000_plane.row_stride == 0x40);
    assert(model.scene_resources().c000_plane.word(4, 7) == 0xC7C3);
    assert(model.scene_resources().c000_plane.written[7 * 32 + 4]);
    assert(model.checkpoint_vram()[0xC1C8] == 0x12);
    assert(model.checkpoint_vram()[0xC1C9] == 0x34);
    assert(!model.c000_cleared());
    assert(!model.frame_palette_prepared());

    const std::vector<std::uint8_t> prepare_rom{0x0D, 0x0E, 0x00};
    SceneResourceVm prepare_vm;
    prepare_vm.bind_rom(prepare_rom);
    prepare_vm.start(0);
    SceneServices prepare_services;
    prepare_services.load_or_clear_c000 = [&model](GameState&) {
        model.clear_c000();
    };
    prepare_services.prepare_frame_and_palette = [&model](GameState&) {
        model.prepare_frame_and_palette();
    };
    assert(prepare_vm.tick(scene_state, prepare_services)
        == SceneResourceRunResult::Finished);
    assert(model.c000_cleared());
    assert(model.scene_resources().c000_plane.word(4, 7) == 0);
    assert(model.frame_palette_prepared());
    assert(model.checkpoint_vram()[0xBFFF] == 1);
    assert(model.checkpoint_vram()[0xC000] == 0);
    assert(model.checkpoint_vram()[0xDFFF] == 0);
    assert(model.checkpoint_vram()[0xE000] == 4);

    assert(model.c000_cleared());
    assert(model.frame_palette_prepared());
    assert(model.palette_state().colors[2].r == 1);

    model.configure_scene_tile_rows(true);
    model.write_tile(2, 3, 1, 0x2000);
    assert(model.scene_resources().e000_first);
    assert(model.scene_resources().e000_plane.word(2, 3) == 0xA7C1);
    assert(model.scene_resources().c000_plane.word(2, 3) == 0);

    std::vector<std::uint32_t> framebuffer(320 * 224, 0);
    model.render(framebuffer, 320, 224);
    assert(framebuffer.front() == 0xFF030201U);

    model.reset();
    assert(!model.loaded());
    assert(model.scene_tile_writes().empty());
    assert(model.scene_resources().c000_plane.word(2, 3) == 0);
    assert(model.scene_resources().e000_plane.word(2, 3) == 0);
    assert(!model.c000_cleared());
    assert(!model.frame_palette_prepared());
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

    std::vector<std::uint8_t> live_vram(GenesisRenderModel::kVramSize, 0);
    std::vector<std::uint8_t> live_vsram(GenesisRenderModel::kVsramSize, 0);
    std::vector<GenesisColor> live_palette(64);
    live_palette[2] = GenesisColor{1, 2, 3, 255};
    live_palette[33] = GenesisColor{10, 20, 30, 255};
    std::array<std::uint8_t, 32> live_registers{};
    live_registers[2] = 0x30;  // plane A at C000
    live_registers[4] = 0x07;  // plane B at E000
    live_registers[7] = 2;
    for (int address = 0xF800; address < 0xF820; ++address) {
        live_vram[static_cast<std::size_t>(address)] = 0x11;
    }

    GenesisRenderModel live_model;
    live_model.load_checkpoint(
        live_vram, live_vsram, live_palette, live_registers);
    live_model.write_tile(0, 0, 0, 0x4000);
    assert(live_model.checkpoint_vram()[0xC000] == 0);
    assert(live_model.scene_resources().c000_plane.word(0, 0) == 0xC7C0);
    std::vector<std::uint32_t> live_framebuffer(320 * 224, 0);
    live_model.render(live_framebuffer, 320, 224);
    assert(live_framebuffer.front() == 0xFF1E140AU);

    model.reset();
    assert(model.preview_sprites().empty());

    return 0;
}
