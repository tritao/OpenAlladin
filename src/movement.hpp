#pragma once

#include "actor.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace openaladdin {

struct MovementContext {
    const std::vector<std::uint8_t>& rom;
    int player_world_x = 0;
    int player_world_y = 0;
    const std::array<bool, 32>* deferred_actors = nullptr;
    std::function<void(ActorIndex, std::uint8_t)> retire_actor;
};

// Shared actor movement interpreter. It owns no actor records and therefore
// remains independently testable against a ROM byte stream and a small actor
// table fixture.
class MovementVm {
public:
    void tick(
        std::array<ActorState, 32>& actors,
        const MovementContext& context
    ) const;
};

}  // namespace openaladdin
