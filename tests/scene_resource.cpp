#include "scene_resource.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void test_tile_commands_and_service_hooks() {
    // The second stream at 0x40 exercises the auxiliary A4 pointer without
    // depending on a production scene resource whose higher-level presentation
    // owner is not wired yet.
    std::vector<std::uint8_t> rom(0x50, 0);
    std::size_t cursor = 0;
    const auto put = [&](std::uint8_t value) { rom[cursor++] = value; };
    put(0x01); put(0xFE);                       // signed X advance (-2)
    put(0x02); put(0xFE);                       // signed Y advance (-2)
    put(0x01); put(0x02);                       // return X to zero
    put(0x02); put(0x02);                       // return Y to zero
    put(0x08);                                  // tile base 0x0000
    put(0x03); put(2); put(0x20);              // two horizontal tiles
    put(0x04); put(2); put(0x21);              // two vertical tiles
    put(0x05); put(2); put(2); put(0x22);      // two-by-two rectangle
    put(0x07);                                // next row, x = 0
    put(0x0B); put(0x23);                      // tile base 0x6000
    put(0x09); put(0x24);                      // tile base 0x2000
    put(0x0A); put(0x25);                      // tile base 0x4000
    put(0x08); put(0x26);                      // tile base 0x0000
    put(0x0C); put(0x00); put(0x00); put(0x48); // load auxiliary A4 pointer
    put(0x06); put(2);                         // two service frames
    put(0x0D); put(0x0E);                      // resource and palette hooks
    put(0x0F);                                 // actor template, x, y
    put(0x00); put(0x1B); put(0x7E); put(0x2C); // template address
    put(0x01); put(0x23);                       // x
    put(0x00); put(0x45);                       // y
    put(0x00);                                 // end
    rom[0x40] = 0x0C; rom[0x41] = 0x00; rom[0x42] = 0x00; rom[0x43] = 0x48;
    rom[0x44] = 0x20; rom[0x45] = 0x00;

    openaladdin::GameState state;
    openaladdin::SceneResourceVm vm;
    vm.bind_rom(rom);
    vm.start(0);

    std::vector<openaladdin::SceneTileWrite> writes;
    int service_frames = 0;
    int c000_calls = 0;
    int palette_calls = 0;
    openaladdin::SceneActorRecord actor;
    openaladdin::SceneServices services;
    services.write_tile = [&](openaladdin::GameState&, const openaladdin::SceneTileWrite& write) {
        writes.push_back(write);
    };
    services.service_frame = [&](openaladdin::GameState&) { ++service_frames; };
    services.load_or_clear_c000 = [&](openaladdin::GameState&) { ++c000_calls; };
    services.prepare_frame_and_palette = [&](openaladdin::GameState&) { ++palette_calls; };
    services.instantiate_actor = [&](openaladdin::GameState&, const openaladdin::SceneActorRecord& record) {
        actor = record;
        return true;
    };

    assert(vm.tick(state, services) == openaladdin::SceneResourceRunResult::Finished);
    assert(vm.finished());
    assert(!vm.faulted());
    assert(writes.size() == 12);
    assert(writes[0].x == 0 && writes[0].y == 0 && writes[0].tile_row == 0);
    assert(writes[1].x == 1 && writes[1].y == 0 && writes[1].tile_row == 0);
    assert(writes[2].x == 2 && writes[2].y == 0 && writes[2].tile_row == 1);
    assert(writes[3].x == 2 && writes[3].y == 1 && writes[3].tile_row == 1);
    assert(writes[4].x == 2 && writes[4].y == 2 && writes[4].tile_row == 2);
    assert(writes[5].x == 3 && writes[5].y == 2 && writes[5].tile_row == 2);
    assert(writes[6].x == 2 && writes[6].y == 3 && writes[6].tile_row == 2);
    assert(writes[7].x == 3 && writes[7].y == 3 && writes[7].tile_row == 2);
    assert(writes[8].x == 0 && writes[8].y == 5 && writes[8].tile_row == 3);
    assert(writes[8].tile_base == 0x6000);
    assert(writes[9].x == 1 && writes[9].y == 5 && writes[9].tile_row == 4);
    assert(writes[9].tile_base == 0x2000);
    assert(writes[10].x == 2 && writes[10].y == 5 && writes[10].tile_row == 5);
    assert(writes[10].tile_base == 0x4000);
    assert(writes[11].x == 3 && writes[11].y == 5 && writes[11].tile_row == 6);
    assert(writes[11].tile_base == 0x0000);
    assert(service_frames == 2);
    assert(c000_calls == 1);
    assert(palette_calls == 1);
    assert(actor.template_address == 0x001B7E2C);
    assert(actor.x == 0x0123);
    assert(actor.y == 0x0045);

    // The pointer loader updates A4, not the command cursor.
    vm.start(0x40);
    assert(vm.tick(state, services) == openaladdin::SceneResourceRunResult::Finished);
    assert(writes.back().x == 0 && writes.back().tile_row == 0);
    assert(vm.cursor() == 0x46);
    assert(vm.stream_pointer() == 0x48);
}

void test_status_and_malformed_stream_boundaries() {
    openaladdin::GameState state;
    std::vector<std::uint8_t> rom{0x06, 0x03, 0x00};
    openaladdin::SceneResourceVm vm;
    vm.bind_rom(rom);
    vm.start(0);
    int frames = 0;
    openaladdin::SceneServices services;
    services.service_frame = [&](openaladdin::GameState& current) {
        ++frames;
        if (frames == 2) current.scene.resource_status = 1;
    };
    assert(vm.tick(state, services) == openaladdin::SceneResourceRunResult::StatusChanged);
    assert(frames == 2);
    assert(!vm.finished());

    state.scene.resource_status = 0;
    rom = {0x01, 0x01};
    vm.bind_rom(rom);
    vm.start(0);
    assert(vm.tick(state, services, 1) == openaladdin::SceneResourceRunResult::BudgetExhausted);
    assert(!vm.faulted());
    assert(vm.tick(state, services) == openaladdin::SceneResourceRunResult::InvalidStream);
    assert(vm.faulted());
}

}  // namespace

int main() {
    test_tile_commands_and_service_hooks();
    test_status_and_malformed_stream_boundaries();
    return 0;
}
