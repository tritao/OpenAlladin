#pragma once

#include "actor.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace openaladdin {

// Lossless semantic payload consumed by AnimationVM's F5 command. The VM
// decodes the original 16-byte record; this service owns its effects on the
// actor table and resource allocator.
struct F5Command {
    bool valid = false;
    std::uint8_t mode = 0;
    std::uint32_t template_address = 0;
    std::int8_t offset_x = 0;
    std::int8_t offset_y = 0;
    std::uint32_t animation_override = 0;
    std::uint32_t movement_override = 0;
    int source_world_x = 0;
    int source_world_y = 0;
    std::uint8_t source_facing_x_flip = 0;
    std::uint8_t source_facing_y_flip = 0;
    bool apple_action = false;
};

enum class ActorRetirementMode {
    PreserveLinkedActor,
    RetireLinkedActor,
};

// Owns actor allocation, template initialization, linked-record cleanup, and
// sprite-resource ownership. The records themselves remain in GameState via
// ActorSystem; this object only provides the semantic operations on them.
class ActorLifecycleSystem {
public:
    explicit ActorLifecycleSystem(ActorSystem& actors) : actors_(actors) {}

    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }

    std::optional<ActorIndex> allocate(ActorPool pool) const;
    ActorState from_template(std::uint32_t template_address) const;
    ActorState initialize_record(
        const ActorState& destination,
        std::uint32_t template_address
    ) const;

    // Installs a prepared record and acquires its resource run. A failed
    // allocation leaves the destination retired, matching the ROM allocator's
    // failure boundary.
    bool install(ActorIndex destination, const ActorState& record);

    // Implements the recovered F5 modes 0..6. Mode 4 is the in-place path;
    // modes 5/6 allocate and link a child to the source actor.
    std::optional<ActorIndex> spawn_f5(ActorIndex source, const F5Command& command);

    void release_resources(ActorIndex actor);
    void retire(
        ActorIndex actor,
        ActorRetirementMode mode = ActorRetirementMode::PreserveLinkedActor
    );
    void retire_from_vm(ActorIndex actor, std::uint8_t command_mode);

private:
    std::uint8_t read8(std::uint32_t address) const;
    std::uint16_t read16(std::uint32_t address) const;
    std::uint32_t read32(std::uint32_t address) const;
    void clear_record(ActorIndex actor);

    ActorSystem& actors_;
    const std::vector<std::uint8_t>* rom_ = nullptr;
};

}  // namespace openaladdin
