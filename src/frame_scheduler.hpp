#pragma once

#include "animation_system.hpp"
#include "camera.hpp"
#include "collision.hpp"
#include "game_data.hpp"
#include "game_state.hpp"
#include "interaction.hpp"
#include "level.hpp"
#include "scene_resource.hpp"
#include "player_terrain.hpp"
#include "scene.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openaladdin {

class ActorMovementSystem;
class ActorTerrainSystem;
class LevelEventSystem;
class LevelEventVm;
class PlayerMotionSystem;
class SceneResourceVm;
class TerrainBehaviorSystem;

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    // The ROM distinguishes the jump edge that starts a jump from the
    // controller level remaining asserted during the launch response.
    bool jump_held = false;
    bool jump_pressed = false;
    // Sword input has the same controller-level/edge distinction. Keeping
    // the held state here prevents a scheduled `a*2` replay from becoming
    // two sword selections in the native loop.
    bool attack_held = false;
    bool attack_pressed = false;
    // Explicit apple/throw action. The attack_pressed field remains
    // the sword path used by existing fixtures; the ROM's A-button stream
    // is exposed separately so physical controller replays can preserve the
    // distinct action selection.
    bool apple_pressed = false;
};

struct SchedulerPhase {
    std::string name;
    std::uint32_t rom_entry_pc = 0;
};

// Host-side state that affects frame ordering but is not itself a Genesis
// record. Keeping it together makes the scheduler boundary explicit while
// preserving the existing checkpoint and trace representation.
struct FrameRuntime {
    std::array<bool, 32> actor_movement_deferred{};
    bool checkpoint_animation_selector_pending = false;
    bool jump_landing_state_arm_pending = false;
    bool jump_landing_state_arm_now = false;
    bool terrain_fall_phase = false;
    bool contour_ground_motion = false;
    int terrain_input_world_x = 0;
    int terrain_input_world_y = 0;
    int last_ground_direction = 0;
    bool checkpoint_terrain_behavior_override = false;
    std::uint8_t checkpoint_terrain_behavior = 0;
    bool level_event_exit_started = false;
    bool scheduler_trace_enabled = false;
    std::vector<SchedulerPhase> scheduler_phases;
    std::vector<std::uint32_t> scheduler_writer_pcs;
};

// Owns the recovered frame ordering. Gameplay helpers remain injectable so
// Engine can retain its existing resource, checkpoint, and compatibility
// boundaries while the ordering itself has one owner.
class FrameScheduler {
public:
    using NoArg = std::function<void()>;
    using PlayerAnimationContext = std::function<AnimationContext(bool)>;
    using UpdateAnimation = std::function<void(
        SpritePose,
        HorizontalDirection,
        const AnimationContext&,
        bool,
        bool
    )>;
    using ApplyTimeline = std::function<void(int)>;
    using AppendSoundRequests =
        std::function<void(const std::vector<std::uint8_t>&)>;
    using RecordPhase = std::function<void(const char*, std::uint32_t)>;

    // Operations that cross from the recovered frame ordering into host-side
    // runtime services. Keeping these together leaves Context focused on the
    // semantic systems whose ordering the scheduler owns.
    struct Services {
        NoArg clear_scheduler_trace;
        NoArg flush_deferred_animation_spawn;
        NoArg update_dynamic_actor_culling;
        NoArg update_level_events;
        NoArg update_scene_resources;
        AppendSoundRequests append_sound_requests;
        UpdateAnimation update_animation_vm_ordinal_30;
        NoArg publish_player_world_coordinates;
        NoArg sync_player_actor;
        PlayerAnimationContext player_animation_context;
        ApplyTimeline apply_actor_timeline;
        std::function<int()> player_world_x;
        std::function<int()> player_world_y;
        RecordPhase record_scheduler_phase;
        NoArg collect_scheduler_writer_pcs;
    };

    struct Context {
        GameState* state = nullptr;
        Level* level = nullptr;
        GameData* game_data = nullptr;
        ActorSystem* actors = nullptr;
        CollisionSystem* collisions = nullptr;
        SceneSystem* scene = nullptr;
        InteractionSystem* interactions = nullptr;
        CameraSystem* camera_system = nullptr;
        AnimationSystem* animation_system = nullptr;
        PlayerMotionSystem* player_motion = nullptr;
        ActorMovementSystem* actor_movement = nullptr;
        ActorTerrainSystem* actor_terrain = nullptr;
        PlayerTerrainSystem* terrain = nullptr;
        TerrainBehaviorSystem* terrain_behavior = nullptr;
        LevelEventVm* level_events = nullptr;
        const std::vector<std::uint8_t>* rom_bytes = nullptr;
        FrameRuntime* runtime = nullptr;
        Services* services = nullptr;
    };

    void update(const InputState& input, Context& context) const;
};

}  // namespace openaladdin
