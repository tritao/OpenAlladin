#pragma once

#include "sprites.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openaladdin {

struct AnimationStep {
    int sprite_frame = 0;
    int duration = 1;
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
    std::uint8_t transition_mode = 0;           // FFF0CD
    std::uint8_t transition_response = 0;       // FFF0D4
    std::uint8_t camera_special_mode = 0;       // FFF173
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
    AnimationSelectorState selector{};
};

struct ActorAnimationState {
    std::uint8_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t facing_x_flip = 0;
    std::uint8_t facing_y_flip = 0;
    std::uint8_t flags = 0;
    std::uint32_t animation_pc = 0;
    std::uint32_t frame_ptr = 0;
    std::uint8_t animation_timer = 0;
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
        bool face_left_input,
        const AnimationContext& context = {}
    );
    bool set_frame(int sprite_frame);
    void set_frame_pointer(std::uint32_t frame_pointer);
    void set_animation_state(std::uint32_t animation_pc, int timer);
    void update_actor(ActorAnimationState& actor, const AnimationContext& context = {});
    void select_stream_entry(std::uint32_t stream_entry);
    bool finished() const;
    bool select_player_interaction_state(const AnimationContext& context);

    SpritePose pose() const { return pose_; }
    int sprite_frame() const;
    int timer() const { return timer_; }
    bool facing_left() const { return facing_left_; }
    std::uint32_t animation_pc() const { return animation_pc_; }
    std::uint32_t frame_pointer() const { return frame_pointer_; }

    // Original ROM stream entry for the currently selected pose. This is a
    // stream identity, not the live cursor (which is the next VM field to
    // recover once conditional control flow is implemented).
    std::uint32_t stream_entry() const;

private:
    static const Clip& clip(SpritePose pose);
    void select(SpritePose pose);
    void select_rom_stream(SpritePose pose, bool execute_now);
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
    void sync_actor_context(const ActorAnimationState& actor, const AnimationContext& context);

    SpritePose pose_ = SpritePose::Idle;
    std::size_t step_ = 0;
    int timer_ = 1;
    bool facing_left_ = false;
    bool rom_mode_ = false;
    std::vector<std::uint8_t> rom_;
    std::array<std::uint8_t, 0x10000> memory_{};
    std::array<std::uint8_t, 0x42> actor_{};
    std::uint32_t animation_pc_ = 0;
    std::uint32_t frame_pointer_ = 0;
    std::uint32_t stream_entry_ = 0;
    std::uint32_t return_pc_ = 0;
    std::uint8_t random_value_ = 0xFF;
    unsigned update_count_ = 0;
    bool landing_finished_ = false;
    bool landing_reselect_pending_ = false;
};

}  // namespace openaladdin
