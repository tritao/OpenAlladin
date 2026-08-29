#pragma once

#include "actor_lifecycle.hpp"
#include "animation_system.hpp"
#include "camera.hpp"
#include "collision.hpp"
#include "game_data.hpp"
#include "game_state.hpp"
#include "interaction.hpp"
#include "level.hpp"
#include "player_terrain.hpp"
#include "scene.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openaladdin {

class ActorMovementSystem;
class PlayerMotionSystem;
class TerrainBehaviorSystem;

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool jump_pressed = false;
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
    using InitializeActor = std::function<ActorState(
        const ActorState&,
        std::uint32_t
    )>;
    using ApplyTimeline = std::function<void(int)>;
    using RecordPhase = std::function<void(const char*, std::uint32_t)>;

    struct Context {
        GameState* state = nullptr;
        Level* level = nullptr;
        GameData* game_data = nullptr;
        ActorSystem* actors = nullptr;
        ActorLifecycleSystem* actor_lifecycle = nullptr;
        CollisionSystem* collisions = nullptr;
        SceneSystem* scene = nullptr;
        InteractionSystem* interactions = nullptr;
        CameraSystem* camera_system = nullptr;
        AnimationSystem* animation_system = nullptr;
        PlayerMotionSystem* player_motion = nullptr;
        ActorMovementSystem* actor_movement = nullptr;
        PlayerTerrainSystem* terrain = nullptr;
        TerrainBehaviorSystem* terrain_behavior = nullptr;
        const std::vector<std::uint8_t>* rom_bytes = nullptr;
        std::vector<std::uint8_t>* level_event_sound_requests = nullptr;
        FrameRuntime* runtime = nullptr;

        NoArg clear_scheduler_trace;
        NoArg flush_deferred_animation_spawn;
        NoArg update_dynamic_actor_culling;
        NoArg update_level_events;
        NoArg start_level_event_stream_after_exit;
        NoArg update_scene_resources;
        UpdateAnimation update_animation_vm_ordinal_30;
        NoArg publish_player_world_coordinates;
        NoArg sync_player_actor;
        PlayerAnimationContext player_animation_context;
        InitializeActor initialize_actor_from_template;
        ApplyTimeline apply_actor_timeline;
        std::function<int()> player_world_x;
        std::function<int()> player_world_y;
        RecordPhase record_scheduler_phase;
        NoArg collect_scheduler_writer_pcs;
    };

    void update(const InputState& input, Context& context) const;
};

}  // namespace openaladdin
