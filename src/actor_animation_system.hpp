#pragma once

#include "actor_lifecycle.hpp"
#include "animation.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <ostream>
#include <vector>

namespace openaladdin {

// Owns the persistent VM instances and the common actor-table animation
// service. Actor records remain authoritative in GameState; the VM array is
// only the interpreter's private cursor/scheduling state.
class ActorAnimationSystem {
public:
    using VmTable = std::array<PlayerAnimationVm, 32>;
    using ObserveTransition = std::function<void(
        GameState&,
        const ActorState&,
        std::uint8_t,
        std::uint8_t
    )>;
    using ObserveActorFlags = std::function<void(
        GameState&,
        const ActorState&,
        std::uint8_t
    )>;

    explicit ActorAnimationSystem(ActorLifecycleSystem& actor_lifecycle)
        : actor_lifecycle_(actor_lifecycle) {}

    void bind_state(GameState& state);
    void load_rom(const std::string& path);
    bool rom_loaded() const { return vms_[0].rom_loaded(); }
    void reset();
    void reset(ActorIndex slot);

    VmTable& vms() { return vms_; }
    const VmTable& vms() const { return vms_; }
    PlayerAnimationVm& vm(ActorIndex slot) { return vms_.at(slot); }
    const PlayerAnimationVm& vm(ActorIndex slot) const { return vms_.at(slot); }

    AnimationServices services(
        ActorIndex source_actor,
        bool defer_player_spawns = false,
        bool defer_mode3_spawns = false
    );
    std::optional<ActorIndex> spawn_f5(
        ActorIndex source_actor,
        const F5Command& command
    );

    void update(
        GameState& state,
        std::uint8_t frame_phase,
        const AnimationContext& context,
        const ObserveTransition& observe_transition,
        const ObserveActorFlags& observe_actor_flags
    );

    std::vector<std::uint8_t> take_sound_requests();
    void set_writer_trace_enabled(bool enabled);
    void clear_writer_trace();
    std::vector<std::uint32_t> writer_pcs() const;
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

private:
    ActorLifecycleSystem& actor_lifecycle_;
    VmTable vms_{};
    GameState* state_ = nullptr;
};

}  // namespace openaladdin
