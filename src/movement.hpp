#pragma once

#include "actor.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace openaladdin {

class GameRamView;

struct MovementContext {
    std::span<const std::uint8_t> rom;
    int player_world_x = 0;
    int player_world_y = 0;
    const std::array<bool, 32>* deferred_actors = nullptr;
    std::function<void(ActorIndex, std::uint8_t)> retire_actor;
    // ActorVM commands use the same address-based memory contract as the
    // animation VM. The owner keeps this view alive across ticks so absolute
    // writes have normal Genesis RAM lifetime.
    GameRamView* ram = nullptr;
};

// Shared actor movement interpreter. It owns no actor records and therefore
// remains independently testable against a ROM byte stream and a small actor
// table fixture.
class MovementVm {
public:
    // Integrates the actor's 8.8 fixed-point velocity accumulator. This is
    // the common movement step used by both the normal movement stream and
    // animation-held actor records.
    static void integrate_actor(ActorState& actor);

    void tick(
        std::array<ActorState, 32>& actors,
        const MovementContext& context
    ) const;
};

}  // namespace openaladdin
