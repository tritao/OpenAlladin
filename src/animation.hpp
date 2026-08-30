#pragma once

#include "actor_lifecycle.hpp"
#include "game_ram.hpp"
#include "sprites.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace openaladdin {

struct AnimationStep {
    int sprite_frame = 0;
    int duration = 1;
};

// Horizontal input is separate from the retained facing state: no input
// leaves the current facing unchanged, while either exclusive direction
// updates it for the next rendered frame.
enum class HorizontalDirection {
    None,
    Left,
    Right,
};

// A stream selected by gameplay is not necessarily the same thing as the
// locomotion pose currently preferred by physics. Keeping this distinction
// explicit prevents a response/action cursor from being mistaken for idle
// merely because the player has no horizontal input.
enum class AnimationStreamKind {
    Locomotion,
    Response,
    Action,
};

// The global RAM inputs consumed by Player_ProcessInteractionState at
// 0x001AE4F8. Keep these separate from the VM's bytecode scratch memory:
// FFF0CC is cleared by the caller immediately before some selector calls,
// while the animation streams also inspect it as a running-state latch.
struct AnimationSelectorState {
    std::uint8_t animation_gate = 0;            // FFF0E7
    std::uint8_t terminal_transition = 0;       // FFF0E6
    std::uint8_t scene_script_countdown = 0;    // FFF0E9
    std::uint8_t interaction_lock = 0;          // FFF0F2
    std::uint8_t response_active = 0;           // FFF0BE
    std::uint8_t landing_state = 0;             // FFF0C1
    std::uint8_t transition_gate = 0;           // FFF0D0
    std::uint8_t transition_lock = 0;           // FFF0D7
    std::uint8_t transition_state = 0;          // FFF0DB
    std::uint8_t transition_mode = 0;           // FFF0CD
    std::uint8_t transition_flag = 0;           // FFF0D2
    std::uint8_t transition_response = 0;       // FFF0D4
    std::uint8_t transition_state_de = 0;       // FFF0DE
    std::uint8_t transition_state_df = 0;       // FFF0DF
    std::uint8_t camera_special_mode = 0;       // FFF173
    std::uint8_t response_latch = 0;             // FFF115
    std::uint8_t response_animation = 0;         // FFF0ED
    std::uint8_t response_state_ee = 0;         // FFF0EE
    std::uint8_t response_state_ef = 0;         // FFF0EF
    std::uint8_t response_state_f0 = 0;         // FFF0F0
    std::uint8_t response_state_101 = 0;        // FFF101
    std::int16_t horizontal_response = 0;       // FFF0B0
    std::uint8_t response_timer = 0;            // FFF0CC
    std::uint8_t interaction_pending = 0;       // FFEFFF
    std::uint8_t state_lock = 0;                // FFF11F
};

struct AnimationContext {
    // Animation commands consume the live Genesis fields through GameRamView.
    // This is deliberately a non-owning link, not a snapshot of player RAM.
    GameState* state = nullptr;

    // These are call-site inputs for boundaries where the original caller has
    // not yet committed a field to GameState. They are transient invocation
    // parameters, never a second semantic state representation.
    std::optional<bool> grounded_override;
    std::optional<std::int16_t> player_vy_override;
    std::optional<std::uint8_t> landing_state_override;
    std::optional<std::uint8_t> interaction_lock_override;
    std::optional<std::uint8_t> response_timer_override;
};

// Animation commands are interpreted in the VM, but their effects belong to
// the owning gameplay service. The callbacks keep the interpreter independent
// of Engine while allowing F5/F6 to act on the authoritative actor table at
// the command boundary.
struct AnimationServices {
    using SpawnF5 = std::function<std::optional<ActorIndex>(
        ActorIndex,
        const F5Command&
    )>;
    using RetireActor = std::function<void(ActorIndex, std::uint8_t)>;

    ActorIndex source_actor = 0;
    bool defer_player_spawns = false;
    bool defer_mode3_spawns = false;
    SpawnF5 spawn_f5;
    RetireActor retire_actor;
};

// PlayerAnimationVm is the player-facing slice of the original common actor
// animation VM at 0x001AC784. With a ROM loaded it executes the original
// frame-reference stream and command bytecode directly. The small Clip table
// remains as a ROM-less unit-test fallback for tools that only exercise the
// animation API.
class PlayerAnimationVm {
public:
    PlayerAnimationVm();

    struct Clip {
        std::uint32_t stream_entry;
        std::vector<AnimationStep> steps;
        bool loop;
        std::size_t loop_start;
    };

    void load_rom(const std::string& path);
    // Bind VM address commands to the single semantic runtime state. The VM
    // remains usable without a binding for ROM-less/unit-test callers.
    void bind_state(GameState& state) { ram_.bind_state(state); }
    bool rom_loaded() const { return !rom_.empty(); }
    void reset();
    void update(
        SpritePose desired_pose,
        HorizontalDirection horizontal_direction,
        const AnimationContext& context = {},
        std::optional<std::uint8_t> scheduler_phase = std::nullopt,
        AnimationServices* services = nullptr
    );
    bool set_frame(int sprite_frame);
    void set_frame_pointer(std::uint32_t frame_pointer);
    void set_animation_state(std::uint32_t animation_pc, int timer);
    // Some ROM selectors clear FF7E77 on the following VBlank, after
    // publishing the new stream root. Keep that boundary distinct from the
    // stream's scheduler service so the root remains visible for one frame.
    void clear_animation_timer_next_update();
    // Actor producers can publish a record immediately before the shared
    // ordinal-30 traversal. Keep that producer-to-VM boundary on the actor's
    // VM rather than adding synthetic fields to the Genesis actor record.
    void defer_actor_service();
    void defer_actor_service_then_every_phase();
    void defer_actor_service_on_gate();
    void defer_actor_service_then_force();
    void defer_actor_retirement();
    void clear_actor_service_boundary();
    bool consume_actor_service(bool scheduler_service, bool defer_gate);
    bool consume_actor_retirement_defer();
    bool actor_service_deferred() const;
    bool actor_service_forced() const;
    void set_facing_left(bool facing_left) { facing_left_ = facing_left; }
    bool update_actor(
        ActorState& actor,
        const AnimationContext& context = {},
        AnimationServices* services = nullptr
    );
    bool take_spawn_command(F5Command& command);
    void defer_spawn_command(const F5Command& command);
    bool take_deferred_spawn_command(F5Command& command);
    const std::optional<F5Command>& deferred_spawn_command() const {
        return deferred_spawn_command_;
    }
    std::vector<std::uint8_t> take_sound_requests();
    // Consume a global-RAM byte written by the player VM during its most
    // recent tick. This keeps ROM-side ED writes visible to Engine-owned
    // terrain state without treating the VM's private scratch image as the
    // authoritative state on frames where no write occurred.
    bool take_memory_write(std::uint32_t address, std::uint8_t& value);
    void select_stream_entry(
        std::uint32_t stream_entry,
        bool publish_frame_pointer = false,
        bool defer_first_tick = false
    );
    void select_locomotion_stream(
        SpritePose pose,
        const AnimationContext& context = {}
    );
    // Publish a dynamically selected locomotion root without consuming its
    // first frame. This mirrors an F8 handoff that occurs at an animation
    // command boundary and remains visible until the next actor tick.
    void select_locomotion_entry(
        std::uint32_t stream_entry,
        bool defer_first_tick = false,
        SpritePose pose = SpritePose::Jump
    );
    void select_response_stream(std::uint32_t stream_entry, int timer = 0);
    bool finished() const;
    bool select_player_interaction_state(const AnimationContext& context);

    SpritePose pose() const { return pose_; }
    int sprite_frame() const;
    int timer() const { return timer_; }
    bool facing_left() const { return facing_left_; }
    AnimationStreamKind stream_kind() const { return stream_kind_; }
    std::uint32_t animation_pc() const { return animation_pc_; }
    std::uint32_t frame_pointer() const { return frame_pointer_; }
    std::uint16_t camera_vertical_threshold() const {
        return read_memory16(0xFF7E00);
    }

    // Original ROM stream entry for the currently selected pose. This is a
    // stream identity, not the live cursor (which is the next VM field to
    // recover once conditional control flow is implemented).
    std::uint32_t stream_entry() const;
    std::array<std::uint8_t, 0x42> actor_record() const {
        return ram_.actor_record();
    }
    bool clear_timer_next_update() const { return clear_timer_next_update_; }
    unsigned update_count() const { return update_count_; }
    std::uint32_t return_pc() const { return return_pc_; }
    void set_writer_trace_enabled(bool enabled) { writer_trace_enabled_ = enabled; }
    void clear_writer_trace() { writer_pcs_.clear(); }
    const std::vector<std::uint32_t>& writer_pcs() const { return writer_pcs_; }
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

private:
    static const Clip& clip(SpritePose pose);
    void select(SpritePose pose);
    void select_rom_stream(
        SpritePose pose,
        bool execute_now,
        const AnimationContext* context = nullptr
    );
    void tick_rom(const AnimationContext& context, AnimationServices* services);
    void tick_actor_rom(
        const AnimationContext& context,
        const ActorState& actor,
        AnimationServices* services
    );
    std::uint32_t dynamic_stream(const AnimationContext& context) const;
    std::uint8_t read_rom8(std::uint32_t address) const;
    std::uint16_t read_rom16(std::uint32_t address) const;
    std::uint32_t read_rom32(std::uint32_t address) const;
    std::uint8_t read_memory8(std::uint32_t address) const;
    std::uint16_t read_memory16(std::uint32_t address) const;
    std::uint32_t read_memory32(std::uint32_t address) const;
    void write_memory8(std::uint32_t address, std::uint8_t value);
    void write_memory16(std::uint32_t address, std::uint16_t value);
    void write_memory32(std::uint32_t address, std::uint32_t value);
    std::uint16_t advance_random();
    void dispatch_callback(
        std::uint32_t callback,
        const AnimationContext& context
    );
    bool command(
        std::uint8_t opcode,
        std::uint32_t& cursor,
        const AnimationContext& context,
        AnimationServices* services
    );
    bool compare_command(std::uint32_t& cursor);
    bool flag_command(std::uint32_t& cursor);
    void sync_context(const AnimationContext& context);
    bool response_stream_needs_recovery() const;

    SpritePose pose_ = SpritePose::Idle;
    std::size_t step_ = 0;
    int timer_ = 1;
    bool facing_left_ = false;
    HorizontalDirection horizontal_direction_ = HorizontalDirection::None;
    AnimationStreamKind stream_kind_ = AnimationStreamKind::Locomotion;
    bool rom_mode_ = false;
    std::vector<std::uint8_t> rom_;
    GameRamView ram_;
    std::array<std::uint8_t, 0x42> actor_{};
    std::uint32_t animation_pc_ = 0;
    std::uint32_t frame_pointer_ = 0;
    std::uint32_t stream_entry_ = 0;
    std::uint32_t return_pc_ = 0;
    std::uint8_t random_value_ = 0xFF;
    // F6 is an actor callback in the common VM. Player streams can also
    // contain the opcode as data, so only actor ticks may apply its cleanup
    // side effect.
    bool actor_tick_ = false;
    // Actor producers can publish a record immediately before the shared
    // ordinal-30 traversal. Keep those producer-to-VM gates on actor VMs;
    // the player cursor itself is serviced directly by the ROM phase.
    enum class ActorServiceBoundary : std::uint8_t {
        None,
        ForceNextUpdate,
        ActorDeferUntilGate,
        ActorDeferUntilGateThenEveryPhase,
        ActorDeferOnGate,
        ActorDeferThenForce,
        ActorRetireNextUpdate,
        ActorServiceEveryPhase,
    };
    ActorServiceBoundary actor_service_boundary_ = ActorServiceBoundary::None;
    bool clear_timer_next_update_ = false;
    bool tracking_memory_writes_ = false;
    bool writer_trace_enabled_ = false;
    std::uint32_t active_command_pc_ = 0;
    std::vector<std::uint32_t> writer_pcs_;
    // Only service-less callers retain decoded commands locally. The Engine
    // always supplies AnimationServices, so its normal path never queues a
    // decoded F5 request for later reinterpretation.
    std::vector<F5Command> unhandled_spawn_commands_{};
    std::optional<F5Command> deferred_spawn_command_;
    bool actor_retired_ = false;
    std::vector<std::uint8_t> sound_requests_;
    unsigned update_count_ = 0;
    bool landing_finished_ = false;
    bool landing_reselect_pending_ = false;
};

}  // namespace openaladdin
