#include "camera.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace openaladdin;

    constexpr std::size_t horizontal_address = 0x2A52;
    constexpr std::size_t vertical_address = 0x2BA4;
    constexpr std::size_t rom_size = 0x2C78;

    std::vector<std::uint8_t> rom(rom_size, 0);
    rom[horizontal_address] = 0xA1;
    rom[horizontal_address + 0x151] = 0xB2;
    rom[vertical_address] = 0xC3;
    rom[vertical_address + 0xD3] = 0xD4;

    GameData data;
    assert(data.camera_horizontal_damping().empty());
    data.bind_rom(rom);
    const auto horizontal = data.camera_horizontal_damping();
    const auto vertical = data.camera_vertical_damping();
    assert(horizontal.size() == 0x152);
    assert(vertical.size() == 0xD4);
    assert(horizontal.front() == 0xA1);
    assert(horizontal.back() == 0xB2);
    assert(vertical.front() == 0xC3);
    assert(vertical.back() == 0xD4);

    Level level;
    CameraSystem camera_system;

    GameState aligned;
    aligned.player.x = 3;
    aligned.player.y = 5;
    aligned.camera.x = 0x12;
    aligned.camera.y = 0x23;
    camera_system.initialize(aligned, level);
    assert(aligned.camera.pixel_x == 5);
    assert(aligned.camera.pixel_y == 8);
    assert(aligned.camera.tile_x == 0x10);
    assert(aligned.camera.tile_y == 0x20);

    GameState rebased;
    rebased.camera.reference_x = 0x20;
    rebased.camera.reference_y = 0x30;
    rebased.camera.scroll_x = 0x10;
    rebased.camera.scroll_y = -0x10;
    rebased.camera.scroll_left_pending = true;
    rebased.camera.scroll_right_pending = true;
    rebased.camera.scroll_up_pending = true;
    rebased.camera.scroll_down_pending = true;
    assert(camera_system.rebase(rebased, level));
    assert(rebased.camera.reference_x == 0x30);
    assert(rebased.camera.reference_y == 0x20);
    assert(rebased.camera.scroll_x == 0);
    assert(rebased.camera.scroll_y == 0);
    assert(!rebased.camera.scroll_left_pending);
    assert(!rebased.camera.scroll_right_pending);
    assert(!rebased.camera.scroll_up_pending);
    assert(!rebased.camera.scroll_down_pending);

    rom[horizontal_address + 0x10] = 2;
    rom[vertical_address + 0x10] = 3;
    GameState followed;
    followed.player.x = 84;
    followed.player.y = 84;
    followed.camera.horizontal_threshold = 100;
    followed.camera.vertical_threshold = 100;
    followed.camera.reference_x = 0x20;
    followed.camera.reference_y = 0x20;
    followed.camera.level_width = 0x1000;
    followed.camera.level_height = 0x1000;
    camera_system.update(followed, level, data);
    assert(followed.player.x == 86);
    assert(followed.player.y == 87);
    assert(followed.camera.x == -2);
    assert(followed.camera.y == 461);
    assert(followed.camera.scroll_x == -2);
    assert(followed.camera.scroll_y == -3);
    assert(followed.camera.scroll_left_pending);
    assert(followed.camera.scroll_up_pending);

    followed.camera.update_delay = 1;
    const int delayed_x = followed.player.x;
    const int delayed_y = followed.player.y;
    camera_system.update(followed, level, data);
    assert(followed.camera.update_delay == 0);
    assert(followed.player.x == delayed_x);
    assert(followed.player.y == delayed_y);

    followed.camera.special_mode = 1;
    camera_system.update(followed, level, data);
    assert(followed.player.x == delayed_x);
    assert(followed.player.y == delayed_y);

    followed.camera.special_mode = 0;
    followed.scene.transition_active = true;
    camera_system.update(followed, level, data);
    assert(followed.player.x == delayed_x);
    assert(followed.player.y == delayed_y);

    return 0;
}
