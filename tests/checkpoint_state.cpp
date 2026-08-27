#include "engine.hpp"

#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

openaladdin::InputState input_for_frame(int frame) {
    openaladdin::InputState input;
    input.right = frame < 220;
    input.left = frame >= 300 && frame < 460;
    input.jump_pressed = frame == 30 || frame == 360;
    input.attack_pressed = frame == 90 || frame == 520;
    input.apple_pressed = frame == 180 || frame == 620;
    return input;
}

std::string checkpoint(const openaladdin::Engine& engine) {
    std::ostringstream output(std::ios::out | std::ios::binary);
    engine.write_checkpoint(output);
    return output.str();
}

}  // namespace

int main() {
    constexpr char kAssets[] = "build/assets/levels/level01";
    constexpr char kSprites[] = "build/assets/sprites";
    constexpr char kRom[] = "rom/Disneys_Aladdin_U_p1.bin";
    constexpr int kCheckpointFrame = 500;
    constexpr int kEndFrame = 1000;

    auto uninterrupted = std::make_unique<openaladdin::Engine>();
    uninterrupted->load(kAssets, kSprites, kRom);

    for (int frame = 0; frame < kCheckpointFrame; ++frame) {
        uninterrupted->update(input_for_frame(frame));
    }
    const std::string saved = checkpoint(*uninterrupted);
    // The checkpoint contains the per-VM 64 KiB RAM images and should not
    // silently collapse to a visual/semantic pose snapshot.
    assert(saved.size() > 0x100000);

    auto restored = std::make_unique<openaladdin::Engine>();
    restored->load(kAssets, kSprites, kRom);
    std::istringstream input(saved, std::ios::in | std::ios::binary);
    restored->read_checkpoint(input);
    assert(checkpoint(*uninterrupted) == checkpoint(*restored));

    for (int frame = kCheckpointFrame; frame < kEndFrame; ++frame) {
        const auto input_state = input_for_frame(frame);
        uninterrupted->update(input_state);
        restored->update(input_state);
        assert(uninterrupted->frame() == restored->frame());
        assert(checkpoint(*uninterrupted) == checkpoint(*restored));
    }
    return 0;
}
