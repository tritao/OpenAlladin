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
    uninterrupted->state().interaction_state.target_current = 7;
    uninterrupted->state().interaction_state.response_current = 3;
    uninterrupted->state().interaction_state.response_pending = 9;
    uninterrupted->state().interaction_state.type3e_response_latch = 0xFF;
    uninterrupted->state().interaction_state.type3f_response_latch = 0x7F;
    uninterrupted->state().interaction_state.primary_digits = 0x3037;
    uninterrupted->state().interaction_state.secondary_digits = 0x3132;
    uninterrupted->state().progress.difficulty_counter = '6';
    uninterrupted->state().progress.active_scene_entry_gate = 4;
    uninterrupted->state().scene.resource_completion = 0xFF;
    uninterrupted->state().scene.resource_mode = 0x14;
    uninterrupted->state().player.terrain_bounce_animation_state = 0x28;
    uninterrupted->state().frame.vblank_ready_latch = 0xFF;
    uninterrupted->state().frame.frame_wait_latch = 0x01;
    const std::string saved = checkpoint(*uninterrupted);
    // The checkpoint contains the per-VM 64 KiB RAM images and should not
    // silently collapse to a visual/semantic pose snapshot.
    assert(saved.size() > 0x100000);

    auto restored = std::make_unique<openaladdin::Engine>();
    restored->load(kAssets, kSprites, kRom);
    std::istringstream input(saved, std::ios::in | std::ios::binary);
    restored->read_checkpoint(input);
    assert(checkpoint(*uninterrupted) == checkpoint(*restored));
    assert(restored->state().interaction_state.target_current == 7);
    assert(restored->state().interaction_state.response_current == 3);
    assert(restored->state().interaction_state.response_pending == 9);
    assert(restored->state().interaction_state.type3e_response_latch == 0xFF);
    assert(restored->state().interaction_state.type3f_response_latch == 0x7F);
    assert(restored->state().interaction_state.primary_digits == 0x3037);
    assert(restored->state().interaction_state.secondary_digits == 0x3132);
    assert(restored->state().progress.difficulty_counter == '6');
    assert(restored->state().progress.active_scene_entry_gate == 4);
    assert(restored->state().scene.resource_completion == 0xFF);
    assert(restored->state().scene.resource_mode == 0x14);
    assert(restored->state().player.terrain_bounce_animation_state == 0x28);
    assert(restored->state().frame.vblank_ready_latch == 0xFF);
    assert(restored->state().frame.frame_wait_latch == 0x01);

    for (int frame = kCheckpointFrame; frame < kEndFrame; ++frame) {
        const auto input_state = input_for_frame(frame);
        uninterrupted->update(input_state);
        restored->update(input_state);
        assert(uninterrupted->frame() == restored->frame());
        assert(checkpoint(*uninterrupted) == checkpoint(*restored));
    }

    // Exercise the separate timed level-event cursor/tick state while a
    // Level 02 exit stream is active. This catches checkpoints that preserve
    // ordinary actor state but restart the event stream from its first byte.
    auto event_uninterrupted = std::make_unique<openaladdin::Engine>();
    event_uninterrupted->load(
        "build/assets/levels/level02",
        kSprites,
        kRom
    );
    event_uninterrupted->set_checkpoint(150, 1000, 0, 0, false);
    event_uninterrupted->update({});
    const std::string event_saved = checkpoint(*event_uninterrupted);

    auto event_restored = std::make_unique<openaladdin::Engine>();
    event_restored->load(
        "build/assets/levels/level02",
        kSprites,
        kRom
    );
    std::istringstream event_input(event_saved, std::ios::in | std::ios::binary);
    event_restored->read_checkpoint(event_input);
    assert(checkpoint(*event_uninterrupted) == checkpoint(*event_restored));
    for (int frame = 0; frame < 4; ++frame) {
        event_uninterrupted->update({});
        event_restored->update({});
        assert(checkpoint(*event_uninterrupted) == checkpoint(*event_restored));
    }
    return 0;
}
