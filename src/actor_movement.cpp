#include "actor_movement.hpp"

#include "animation_system.hpp"
#include "frame_scheduler.hpp"

#include <array>

namespace openaladdin {
namespace {

constexpr std::uint8_t kActorTerminalType = 0x84;

}  // namespace

void ActorMovementSystem::update(
    GameState& state,
    FrameRuntime& runtime,
    std::span<const std::uint8_t> rom
) {
    std::array<std::uint8_t, 32> previous_types{};
    for (std::size_t slot = 0; slot < state.actors.size(); ++slot) {
        previous_types[slot] = state.actors[slot].type;
    }

    vm_.tick(
        state.actors,
        MovementContext{
            rom,
            state.camera.x + state.player.x,
            state.camera.y + state.player.y,
            &runtime.actor_movement_deferred,
            [this](ActorIndex slot, std::uint8_t command_mode) {
                lifecycle_.retire_from_vm(slot, command_mode);
            }
        }
    );
    runtime.actor_movement_deferred.fill(false);

    for (std::size_t slot = 0; slot < state.actors.size(); ++slot) {
        const ActorState& actor = state.actors[slot];
        if (previous_types[slot] != kActorTerminalType
            && actor.type == kActorTerminalType
            && actor.terminal_timer == 0
            && previous_types[slot] != 0x2A) {
            // Movement streams can publish the terminal template directly
            // before AnimationVM_TickActors. Preserve that boundary, then
            // service the new cursor on the next VBlank regardless of the
            // shared animation gate.
            animation_.actors().vm(slot).defer_actor_service_on_gate();
        }
    }
}

}  // namespace openaladdin
