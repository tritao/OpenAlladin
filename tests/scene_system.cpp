#include "scene.hpp"

#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main() {
    openaladdin::SceneSystem scene;
    scene.reset(8);
    int x = 0x20;
    int y = 0x20;
    bool grounded = true;
    const bool handled = scene.update_transition(
        openaladdin::SceneInput{true, false, false, true}, x, y, grounded);
    assert(handled);
    assert(x == 0x28);
    assert(y == 0x18);
    assert(!grounded);

    scene.select(1);
    assert(scene.scene_state() == 1);
    assert(!scene.is_transition());
    assert(!scene.update_transition(
        openaladdin::SceneInput{true, false, false, true}, x, y, grounded));

    std::ifstream input("rom/Disneys_Aladdin_U_p1.bin", std::ios::binary);
    assert(input);
    const std::vector<std::uint8_t> rom{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    scene.load_rom_bytes(rom);
    scene.select(1);
    assert(scene.current_descriptor() != nullptr);
    assert(scene.current_descriptor()->palette.value == 0x1290D2);
    return 0;
}
