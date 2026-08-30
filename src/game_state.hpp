#pragma once

#include "actor.hpp"
#include "animation.hpp"
#include "level.hpp"
#include "scene.hpp"

#include <cstdint>

namespace openaladdin {

struct CameraState {
    // WORLD_CAMERA_X/Y: the actual Genesis world origin. PLAYER_X/Y remain
    // local to this origin; their sum is the world position.
    int x = 0;
    int y = 464;

    // CAMERA_REFERENCE_X/Y and CAMERA_SCROLL_X/Y are the state used by the
    // original camera limit and tile-update path. Keeping them explicit is
    // important: camera x/y alone is not sufficient to resume a trace.
    int reference_x = 0;
    int reference_y = 464;
    int scroll_x = 0;
    int scroll_y = 0;

    // PLAYER_X/Y are followed toward these local thresholds. The ROM changes
    // them for movement, jump, and transition modes.
    int horizontal_threshold = 103;
    int vertical_threshold = 416;

    int level_width = 4800;
    int level_height = 720;
    // SCENE_VDP_UPDATE_FLAG gates fixed event dispatch from the player
    // interaction selector.
    int vdp_update = 1;
    // 0xFF7E0A..0xFF7E10 are startup/VDP alignment fields written by
    // Player_CameraAlign (0x001B0490), not live aliases of x/y.
    int pixel_x = 0;
    int pixel_y = 0;
    int tile_x = 0;
    int tile_y = 0;

    int update_delay = 0;
    int special_mode = 0;
    int scene_state = 1;
    bool scroll_left_pending = false;
    bool scroll_right_pending = false;
    bool scroll_up_pending = false;
    bool scroll_down_pending = false;
};

struct PlayerState {
    static constexpr std::uint8_t kDefaultHealth = 3;
    static constexpr std::uint8_t kMaximumHealth = 3;
    // Keep the gameplay resource and its ROM response lockout together so
    // one flame cannot drain multiple points while the same response is active.
    std::uint8_t health = kDefaultHealth;
    std::uint8_t hurt_cooldown = 0;

    // These are the same local coordinates and 8.8 velocity fields recovered
    // from the Genesis RAM map (PLAYER_X/Y, PLAYER_VX/VY).
    int x = 0;
    int y = 0;
    std::int16_t vx = 0;
    std::int16_t vy = 0;
    // Explicit inertial ground state. The ROM keeps the brake stream active
    // while the release impulse decays; selector dispatch must use this state
    // rather than re-testing the numeric velocity at the animation boundary.
    bool ground_braking = false;
    bool grounded = false;
    std::uint8_t terrain_behavior = 0;
    std::uint8_t terrain_query_result = 0x7F;
    std::uint8_t terrain_push_right = 0;
    std::uint8_t terrain_push_left = 0;
    std::uint8_t terrain_push_up = 0;
    std::uint8_t terrain_push_down = 0;
    std::int16_t terrain_horizontal_response = 0;
    std::uint8_t terrain_response_active = 0;
    std::uint8_t terrain_vertical_stop = 0;
    std::uint8_t terrain_landing_state = 0;
    // The ROM keeps two distinct fields here: FFF0A4 is the surface-mode
    // word toggled by handler 0x47, while FFF0C2 is that handler's one-shot
    // latch. Keeping them separate matters for terrain cells that are
    // revisited on consecutive frames.
    std::uint16_t terrain_surface_mode = 0;
    std::uint8_t terrain_surface_latch = 0;
    std::uint8_t terrain_stop_left_motion = 0;
    std::uint8_t terrain_stop_right_motion = 0;
    std::uint8_t terrain_stop_upward_motion = 0;
    std::uint8_t terrain_left_inner_probe = 0;
    std::uint8_t terrain_left_outer_probe = 0;
    std::uint8_t terrain_right_inner_probe = 0;
    std::uint8_t terrain_right_outer_probe = 0;
    std::uint8_t terrain_response_timer_state = 0;
    // FFF0BF counts the initial active-response jump phase. The ROM applies
    // an extra -0x6C vertical impulse until this counter reaches ten.
    std::uint8_t terrain_jump_response_counter = 0;
    // FFF0EB counts the terrain-bounce animation phase independently from
    // the launch-response counter above.
    std::uint8_t terrain_bounce_animation_state = 0;
    std::uint8_t terrain_transition_countdown = 0;
    std::uint8_t terrain_query_state_a = 0;
    std::uint8_t terrain_query_state_b = 0;
    std::uint8_t terrain_state = 0;
    std::uint8_t terrain_response_latch = 0;
    // FFF0D0 is asserted by the connector prepass while a vertical query is
    // actively moving the player. It is separate from TERRAIN_QUERY_STATE_A:
    // the resolver clears the latter every pass, while the movement response
    // publishes this gate for the following state-machine calls.
    std::uint8_t terrain_transition_gate = 0;
    std::uint8_t terrain_terminal_transition = 0;
    // Selector bytes that are not yet folded into the native terrain model
    // still travel with the checkpoint. This keeps response/action stream
    // selection reproducible instead of reconstructing it from a pose.
    AnimationSelectorState animation_selector{};
    std::uint8_t attack_timer = 0;
};

struct FrameState {
    // Host frame labels remain separate from the ROM phase counter. Both are
    // semantic scheduler state and must survive a checkpoint boundary.
    int number = 0;
    std::uint8_t phase = 0;
};

struct RandomState {
    // FUN_001B3032 is shared by terrain responses and animation F0 branches.
    std::uint32_t value = 0;
};

// Interaction response state is distinct from InteractionRuntimeState, which
// contains host-side deferred calls. These bytes are the Genesis fields used
// by response-target convergence and indirect target-state dispatch.
struct InteractionState {
    std::uint8_t target_current = 0;    // FFF0EC
    std::uint8_t response_current = 0;  // FFEFFA
    std::uint8_t response_pending = 0;  // FFEFFB
    // Collision response latches consumed by interaction spawn gates.
    std::uint8_t type3e_response_latch = 0;  // FFF177
    std::uint8_t type3f_response_latch = 0;  // FFF178
};

// Genesis-semantic runtime state. Services, rendering resources, trace
// buffers, and deferred host scheduling flags intentionally remain outside
// this aggregate while the runtime is migrated incrementally.
struct GameState {
    FrameState frame{};
    PlayerState player{};
    CameraState camera{};
    SceneRuntimeState scene{};
    InteractionMap interactions{};
    InteractionState interaction_state{};
    ActorSystem actors{};
    RandomState random{};
    GameRamStore ram{};
};

}  // namespace openaladdin
