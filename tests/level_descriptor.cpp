#include "level.hpp"

#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main() {
    std::ifstream input("rom/Disneys_Aladdin_U_p1.bin", std::ios::binary);
    assert(input);
    const std::vector<std::uint8_t> rom{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };

    const auto level01 = openaladdin::LevelTable::descriptor(rom, 1);
    assert(level01.scene_id == 1);
    assert(level01.player_start.x == 103);
    assert(level01.player_start.y == 416);
    assert(level01.camera_start.x == 0);
    assert(level01.camera_start.y == 464);
    assert(level01.map_size.width == 300);
    assert(level01.map_size.height == 45);
    assert(level01.music_id == 0x49);
    assert(level01.palette.value == 0x1290D2);
    assert(level01.floor.value == 0x1434C4);
    assert(level01.chars.value == 0x1758E4);
    assert(level01.map.value == 0x195176);
    assert(level01.blocks.value == 0x145B2C);
    assert(level01.parallax.value == 0x12A660);
    assert(level01.enter_function.value == 0x1B5B4A);
    assert(level01.exit_function.value == 0x1B6406);
    assert(level01.parallax_function.value == 0x1AAA88);
    assert(level01.padding == 0);
    assert(level01.descriptor_rom_offset == 0x2CBA);
    assert(level01.from_rom);

    const auto level04 = openaladdin::LevelTable::descriptor(rom, 4);
    assert(level04.background_swap == 1);

    const auto level08 = openaladdin::LevelTable::descriptor(rom, 8);
    assert(level08.animation.value == 0x40B8);
    assert(level08.animation_size == 112);
    assert(level08.music_id == 29);

    const auto all = openaladdin::LevelTable::descriptors(rom);
    assert(all.size() == openaladdin::LevelTable::kCount);
    assert(all[12].exit_function.value == 0x1B6562);

    openaladdin::Level level;
    level.load("build/assets/levels/level01", "rom/Disneys_Aladdin_U_p1.bin");
    assert(level.descriptor().palette.value == 0x1290D2);
    assert(level.resources().map.size() == 300U * 45U);
    assert(!level.resources().floor.empty());
    assert(!level.resources().chars.empty());
    assert(!level.resources().parallax.empty());
    assert(!level.resources().palette.empty());
    return 0;
}
