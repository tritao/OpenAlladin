#pragma once

#include "sprites.hpp"

#include <array>
#include <cstdint>
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
    int player_x = 0;
    int player_y = 0;
    int world_x = 0;
    int world_y = 0;
    std::int16_t player_vx = 0;
    std::int16_t player_vy = 0;
    bool grounded = false;
    std::uint8_t terrain_response_timer_state = 0;
    std::uint8_t terrain_behavior = 0;          // FFF0C3
    std::uint16_t camera_vertical_threshold = 0; // FF7E00
    std::uint8_t scene_vdp_update_flag = 0;     // FFF57D
    AnimationSelectorState selector{};
    // The ROM animation F0 command calls the same shared PRNG as the terrain
    // response code. Engine owns the state; every VM sees the same sequence.
    std::uint32_t* random_state = nullptr;
};

struct ActorAnimationState {
    std::uint8_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint32_t movement_pc = 0;
    // Live actor record byte +0x07, shared by the terrain pass and movement
    // VM flag tests.
    std::uint8_t runtime_field_07 = 0;
    // The shared actor VM's FA/90 arithmetic commands address the two
    // signed accumulator words at record offsets +0x18 and +0x1A.
    std::int16_t movement_word_18 = 0;
    std::int16_t movement_word_1a = 0;
    std::uint8_t facing_x_flip = 0;
    std::uint8_t facing_y_flip = 0;
    std::uint8_t flags = 0;
    // Actor record byte +0x3D is a callback/state value used by proximity
    // streams (not the public flags byte at +0x3C).
    std::uint8_t interaction_state = 0;
    std::uint32_t animation_pc = 0;
    std::uint32_t frame_ptr = 0;
    std::uint8_t animation_timer = 0;
};

// A decoded animation-VM F5 request. The VM owns the bytecode cursor, while
// Engine owns the 32 live actor records and performs the actual allocation.
struct AnimationSpawnRequest {
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
    // The player uses the same template for the physical apple action and
    // for other mode-3 effects. Preserve the originating stream so Engine
    // can apply the apple-specific lifecycle only to the former.
    bool apple_action = false;
    // Actor-originated linked F5 modes (5/6) store a back-reference in the
    // compact Genesis records. The host uses the slot index to mirror the
    // corresponding parent-flag cleanup when the child retires.
    int source_actor_slot = -1;
};

// PlayerAnimationVm is the player-facing slice of the original common actor
// animation VM at 0x001AC784. With a ROM loaded it executes the original
// frame-reference stream and command bytecode directly. The small Clip table
// remains as a ROM-less unit-test fallback for tools that only exercise the
// animation API.
class PlayerAnimationVm {
public:
    struct Clip {
        std::uint32_t stream_entry;
        std::vector<AnimationStep> steps;
        bool loop;
        std::size_t loop_start;
    };

    void load_rom(const std::string& path);
    bool rom_loaded() const { return !rom_.empty(); }
    void reset();
    void update(
        SpritePose desired_pose,
        HorizontalDirection horizontal_direction,
        const AnimationContext& context = {}
    );
    bool set_frame(int sprite_frame);
    void set_frame_pointer(std::uint32_t frame_pointer);
    void set_animation_state(std::uint32_t animation_pc, int timer);
    // Some ROM selectors clear FF7E77 on the following VBlank, after
    // publishing the new stream root. Keep that boundary distinct from the
    // stream's scheduler service so the root remains visible for one frame.
    void clear_animation_timer_next_update();
    void set_animation_phase_delay(int ticks);
    // Publish a selector-written stream root after the VM pass. The cursor
    // reached by that pass is restored at the next update boundary.
    void republish_stream_root();
    // Re-expose the action root for one synchronized state boundary after a
    // VM tick without changing the frame pointer or queuing the consumed
    // cursor. This mirrors the player's action-selector writeback path.
    void republish_stream_root_cursor_only();
    // Restore a ROM cursor after its command boundary while retaining the
    // frame pointer already published by that tick. A few action streams
    // expose the pre-command cursor for one VBlank before continuing.
    void hold_animation_cursor(std::uint32_t cursor);
    // Keep the apple action's selector/cursor publication lifecycle with the
    // VM that owns the ROM cursor, rather than with Engine scheduler flags.
    void begin_apple_action_boundary();
    void service_apple_action_boundary();
    // Service one extra ROM tick on the next update without changing the
    // alternating scheduler phase.
    void force_tick_next_update_without_phase();
    // Skip the next scheduled ROM tick while retaining the current cursor.
    // Action-selector boundaries occasionally expose a command result for an
    // additional state before the following animation service.
    void defer_tick_next_update();
    void set_facing_left(bool facing_left) { facing_left_ = facing_left; }
    void update_actor(ActorAnimationState& actor, const AnimationContext& context = {});
    bool take_spawn_request(AnimationSpawnRequest& request);
    void defer_spawn_request(const AnimationSpawnRequest& request);
    bool take_deferred_spawn_request(AnimationSpawnRequest& request);
    const std::optional<AnimationSpawnRequest>& deferred_spawn_request() const {
        return deferred_spawn_request_;
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
        bool defer_first_tick = false,
        bool force_following_tick = false
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
    const std::array<std::uint8_t, 0x42>& actor_record() const { return actor_; }
    int animation_phase_delay() const { return animation_phase_delay_; }
    std::uint32_t pending_animation_pc() const { return pending_animation_pc_; }
    bool force_tick_after_service() const { return force_tick_after_service_; }
    bool force_tick_next_update() const { return force_tick_next_update_; }
    bool force_tick_without_phase() const { return force_tick_without_phase_; }
    bool defer_tick_next_update() const { return defer_tick_next_update_; }
    bool clear_timer_next_update() const { return clear_timer_next_update_; }
    std::uint8_t action_boundary() const {
        return static_cast<std::uint8_t>(action_boundary_);
    }
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
    void tick_rom(const AnimationContext& context);
    void tick_actor_rom(const AnimationContext& context, const ActorAnimationState& actor);
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
    bool command(std::uint8_t opcode, std::uint32_t& cursor, const AnimationContext& context);
    bool compare_command(std::uint32_t& cursor);
    bool flag_command(std::uint32_t& cursor);
    void sync_context(const AnimationContext& context);
    void sync_selector_context(const AnimationSelectorState& selector, bool grounded);
    void sync_actor_context(const ActorAnimationState& actor, const AnimationContext& context);
    bool response_stream_needs_recovery() const;

    SpritePose pose_ = SpritePose::Idle;
    std::size_t step_ = 0;
    int timer_ = 1;
    bool facing_left_ = false;
    HorizontalDirection horizontal_direction_ = HorizontalDirection::None;
    AnimationStreamKind stream_kind_ = AnimationStreamKind::Locomotion;
    bool rom_mode_ = false;
    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, 0x10000> memory_{};
    std::array<std::uint8_t, 0x10000> memory_write_flags_{};
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
    // Native checkpoints restore the Genesis-visible animation timer, while
    // this separate delay restores the VM scheduler phase that is not stored
    // in the captured RAM fields.
    int animation_phase_delay_ = 0;
    std::uint32_t pending_animation_pc_ = 0;
    bool force_tick_after_service_ = false;
    bool force_tick_next_update_ = false;
    bool force_tick_without_phase_ = false;
    bool defer_tick_next_update_ = false;
    bool clear_timer_next_update_ = false;
    enum class ActionBoundary : std::uint8_t {
        None,
        AwaitRootRepublish,
        AwaitFirstCursor,
        AwaitSecondCursor,
    };
    ActionBoundary action_boundary_ = ActionBoundary::None;
    bool tracking_memory_writes_ = false;
    bool writer_trace_enabled_ = false;
    std::uint32_t active_command_pc_ = 0;
    std::vector<std::uint32_t> writer_pcs_;
    std::vector<AnimationSpawnRequest> spawn_requests_{};
    std::optional<AnimationSpawnRequest> deferred_spawn_request_;
    std::vector<std::uint8_t> sound_requests_;
    unsigned update_count_ = 0;
    bool landing_finished_ = false;
    bool landing_reselect_pending_ = false;
};

}  // namespace openaladdin
