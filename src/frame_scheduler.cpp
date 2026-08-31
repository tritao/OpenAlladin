#include "frame_scheduler.hpp"

#include "actor_movement.hpp"
#include "actor_terrain.hpp"
#include "level_event.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace openaladdin {
namespace {

// 0x001223E2 is the first data cursor inside the separate apple stream.
constexpr std::uint32_t kPlayerSwordAnimationStream = 0x0012271A;
constexpr std::uint32_t kPlayerSwordAnimationEnd = 0x001227AE;
constexpr std::uint32_t kPlayerSwordQueueCursor = 0x00122796;
constexpr std::uint32_t kActorDeathAnimationStream = 0x00122FA2;
constexpr std::uint32_t kPlayerAppleActionStream = 0x001223DA;
constexpr std::uint8_t kPlayerAppleThrowSoundId = 0x03;
constexpr std::uint32_t kPlayerAppleTerrainTimerStream = 0x001225A2;
constexpr std::uint32_t kPlayerActionTerrainTimerStream = 0x001228AC;
constexpr std::uint32_t kPlayerActionAirborneStream = 0x0012295E;
constexpr std::uint32_t kPlayerAppleTerrainTransitionStream = 0x00122504;
constexpr std::uint32_t kPlayerActionPushDownStream = 0x001227D2;
constexpr std::uint32_t kPlayerActionPushUpStream = 0x00122A10;
constexpr std::uint32_t kPlayerActionTransitionStream = 0x00122AF6;
constexpr std::uint32_t kPlayerActionSpecialCameraStream = 0x001226E4;
constexpr std::uint32_t kPlayerIdleStream = 0x00121D9A;
constexpr std::uint32_t kPlayerLandingAnimationStream = 0x00121F84;
constexpr std::uint32_t kPlayerTerrainBounceRoot = 0x001218A0;
constexpr std::uint32_t kPlayerBounceResponseLandingStream = 0x00121884;
constexpr std::uint32_t kPlayerBounceResponseLandingCursor = 0x00121892;
constexpr std::uint32_t kPlayerTransitionJumpStream = 0x001220B8;
constexpr std::uint32_t kPlayerBounceLandingStream = 0x00121BB8;
constexpr std::uint32_t kPlayerBounceLandingTriggerCursor = 0x0012221E;
constexpr std::uint32_t kPlayerGroundResponseActionCursor = 0x00122190;
constexpr std::uint32_t kPlayerRunStream = 0x00122006;
constexpr std::uint32_t kPlayerUpAnimationStream = 0x00122236;
constexpr std::uint32_t kPlayerDownAnimationStream = 0x001222D2;
constexpr std::uint32_t kLevel02ExitCallback = 0x001B6394;
constexpr std::uint32_t kLevel06ExitCallback = 0x001B644E;
constexpr std::uint32_t kLevel02EventStream = 0x00002128;
constexpr std::uint32_t kLevel06EventStream = 0x000024FC;
HorizontalDirection horizontal_direction(const InputState& input) {
    if (input.left && !input.right) return HorizontalDirection::Left;
    if (input.right && !input.left) return HorizontalDirection::Right;
    return HorizontalDirection::None;
}

}  // namespace

void FrameScheduler::update(const InputState& input, Context& context) const {

    auto& state_ = *context.state;
    auto& level_ = *context.level;
    auto& game_data_ = *context.game_data;
    auto& player_ = state_.player;
    auto& camera_ = state_.camera;
    auto& actors_ = *context.actors;
    auto& collisions_ = *context.collisions;
    auto& scene_ = *context.scene;
    auto& interactions_ = *context.interactions;
    auto& camera_system_ = *context.camera_system;
    auto& animation_ = context.animation_system->player();
    auto& actor_movement_ = *context.actor_movement;
    auto& actor_terrain_ = *context.actor_terrain;
    auto& player_system_ = *context.player_system;
    auto& level_events_ = *context.level_events;
    const auto& rom_bytes_ = *context.rom_bytes;
    auto& runtime_ = *context.runtime;
    auto& services_ = *context.services;

    auto& checkpoint_animation_selector_pending_ =
        runtime_.checkpoint_animation_selector_pending;
    auto& jump_landing_state_arm_pending_ = runtime_.jump_landing_state_arm_pending;
    auto& jump_landing_state_arm_now_ = runtime_.jump_landing_state_arm_now;
    auto& terrain_fall_phase_ = runtime_.terrain_fall_phase;
    auto& contour_ground_motion_ = runtime_.contour_ground_motion;
    auto& terrain_input_world_x_ = runtime_.terrain_input_world_x;
    auto& terrain_input_world_y_ = runtime_.terrain_input_world_y;
    const bool completed_jump_ground_response_run_pending =
        runtime_.completed_jump_ground_response_run_pending;
    runtime_.completed_jump_ground_response_run_pending = false;
    const bool completed_jump_ground_response_latch =
        runtime_.completed_jump_ground_response_latch;
    const bool transition_root_response_retry_pending =
        runtime_.transition_root_response_retry_pending;
    runtime_.transition_root_response_retry_pending = false;
    const bool terrain_response_horizontal_carry =
        runtime_.terrain_response_horizontal_carry;
    runtime_.terrain_response_horizontal_carry = false;
    const bool terrain_action_selected_from_attack_edge =
        runtime_.terrain_action_selected_from_attack_edge;
    runtime_.terrain_action_selected_from_attack_edge = false;
    const bool terrain_action_stream_exit_pending =
        runtime_.terrain_action_stream_exit_pending;
    runtime_.terrain_action_stream_exit_pending = false;
    const bool terrain_action_hold_rearm_pending =
        runtime_.terrain_action_hold_rearm_pending;
    runtime_.terrain_action_hold_rearm_pending = false;
    const bool sword_action_pending = runtime_.sword_action_pending;
    const std::uint8_t sword_action_pending_delay =
        runtime_.sword_action_pending_delay;
    runtime_.sword_action_pending = false;
    runtime_.sword_action_pending_delay = 0;
    auto& frame_ = state_.frame.number;
    auto& frame_phase_ = state_.frame.phase;
    auto& last_ground_direction_ = runtime_.last_ground_direction;
    const bool checkpoint_terrain_behavior_override_ =
        runtime_.checkpoint_terrain_behavior_override;
    const std::uint8_t checkpoint_terrain_behavior_ =
        runtime_.checkpoint_terrain_behavior;
    const bool scheduler_trace_enabled_ = runtime_.scheduler_trace_enabled;

    if (scheduler_trace_enabled_) {
        services_.clear_scheduler_trace();
    }
    services_.record_scheduler_phase("frame_latch", 0x001A8C16);
    // Game_FrameUpdateLoop begins with ADDQ.B #1,$FF7E28. All later gate
    // decisions in this update consume this single recovered ROM phase.
    frame_phase_ = static_cast<std::uint8_t>(frame_phase_ + 1);
    const bool completed_jump_ground_response_latch_for_frame =
        completed_jump_ground_response_latch
        && !(animation_.animation_pc() == 0x0012215A
            && (frame_phase_ & 1U) == 0);
    if (completed_jump_ground_response_run_pending
        && animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == 0x00122014) {
        // The ROM's ground-response selector briefly returns to the dynamic
        // run root on the first frame after a completed jump landing. The
        // following odd animation service then walks that root back into the
        // 0x122014 action cursor. Preserve the two visible boundaries.
        animation_.select_locomotion_entry(
            kPlayerRunStream,
            false,
            SpritePose::Run);
    }
    const std::uint32_t animation_pc_at_frame_start = animation_.animation_pc();
    actors_.begin_frame();
    const bool action_response_exit_candidate =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == 0x00122014
        && animation_.animation_pc() == 0x00122034
        && (frame_phase_ & 1U) != 0
        && player_.animation_selector.response_timer != 0;
    for (ActorState& actor : actors_) {
        if (actor.runtime_field_07_delay == 0) continue;
        --actor.runtime_field_07_delay;
        if (actor.runtime_field_07_delay == 0) {
            actor.runtime_field_07 = static_cast<std::uint8_t>(
                actor.runtime_field_07 | 0x10U);
        }
    }
    const InteractionFrameBoundary interaction_boundary = interactions_.begin_frame(state_);
    const bool interaction_selector_pending_at_start =
        interaction_boundary.selector_pending_at_start;
    const bool terrain_response_was_active =
        player_.terrain_response_active != 0;
    const bool terrain_response_timer_was_armed =
        player_.terrain_response_timer_state != 0;
    const bool terrain_response_timer_was_short =
        player_.terrain_response_timer_state == 1;
    const bool promoted_ground_response_was_active =
        player_.terrain_response_timer_state == 0xFF
        && runtime_.contour_ground_motion;
    const bool bounce_response_was_active =
        interactions_.bounce_response_active();
    const bool jump_response_was_complete =
        player_.terrain_jump_response_counter >= 10;
    const bool timed_jump_response_latch_exit =
        animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == 0x0012214E
        && player_.terrain_response_active != 0
        && player_.terrain_response_timer_state != 0
        && player_.terrain_landing_state != 0
        && camera_.reference_y == 432
        && camera_.y == 424;
    const std::uint8_t vertical_stop_value_at_frame_start =
        player_.terrain_vertical_stop;
    const bool arm_surface_interaction = interaction_boundary.arm_surface_interaction;
    if (player_.animation_selector.state_lock != 0) {
        --player_.animation_selector.state_lock;
    }
    // FUN_001A91C6 is the unconditional input/resource service. A player F5
    // command deferred by the previous animation boundary becomes live here.
    services_.record_scheduler_phase("input_resource", 0x001A91C6);
    services_.flush_deferred_animation_spawn();
    const bool transition_frame = scene_.is_transition();
    if (transition_frame) {
        scene_.update_transition(
            SceneInput{input.up, input.down, input.left, input.right},
            player_.x,
            player_.y,
            player_.grounded
        );
    } else {
    player_system_.sample(state_, TerrainInput{
        input.up,
        input.down,
        input.left,
        input.right,
        input.jump_pressed,
        input.jump_held,
        input.attack_held,
    });
    services_.record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    services_.publish_player_world_coordinates();
    terrain_input_world_x_ = services_.player_world_x();
    terrain_input_world_y_ = services_.player_world_y();
    const bool grounded_before_contour = player_.grounded;
    const bool contour_ground_motion_before = contour_ground_motion_;
    const bool stable_terrain_handler_fixture = checkpoint_terrain_behavior_override_
        && (checkpoint_terrain_behavior_ == 0x28
            || checkpoint_terrain_behavior_ == 0x29
            || checkpoint_terrain_behavior_ == 0x2D
            || checkpoint_terrain_behavior_ == 0x27);
    const bool checkpoint_terrain_handler_fixture =
        checkpoint_terrain_behavior_override_
        && checkpoint_terrain_behavior_ != 0x11;
    const bool preserve_ground_response_timer =
        animation_.stream_kind() == AnimationStreamKind::Action
        && (animation_.stream_entry() == 0x00122014
            || animation_.stream_entry() == kPlayerRunStream)
        && player_.terrain_response_timer_state != 0;
    const bool preserve_ground_response_contour =
        grounded_before_contour
        && player_.terrain_response_timer_state != 0
        && !preserve_ground_response_timer;
    const int camera_update_delay_before_contour = camera_.update_delay;
    // The normal slice uses the recovered contour pass. Handler fixtures are
    // deliberately staged at the ROM's resolver boundary instead: the
    // original frame calls Terrain_ResolvePlayerCell/handler before the
    // 0x001A9D98 movement integrator, and the fixture supplies the already
    // resolved landing state that the ROM would have in RAM.
    if (!stable_terrain_handler_fixture) {
        services_.record_scheduler_phase("terrain_contour", 0x001AD7B4);
        if (!checkpoint_terrain_handler_fixture
            && !preserve_ground_response_contour) {
            player_system_.apply_contour(state_, level_, terrain_fall_phase_);
            if (player_.terrain_response_active == 0
                && terrain_response_timer_was_short
                && (preserve_ground_response_timer
                    || contour_ground_motion_before)) {
                // The contour resolver still publishes the new slope row and
                // Y position here, but the action-response movement pass
                // keeps FFF0CC armed until after its horizontal step.
                // Once the internal contour-ground mode is already live, a
                // directional continuation has demoted the initial FFF0FF
                // latch back to the ordinary value 1; do not promote it
                // again merely because the contour is sampled once more.
                if (player_.terrain_response_timer_state == 0
                    || (player_.terrain_response_timer_state == 0xFF
                        && contour_ground_motion_before)) {
                    player_.terrain_response_timer_state = 1;
                    player_.animation_selector.response_timer = 1;
                    if (contour_ground_motion_before) {
                        camera_.update_delay = camera_update_delay_before_contour;
                    }
                }
            }
        }
    }
    services_.record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    services_.publish_player_world_coordinates();
    // The launch frame itself keeps the prior landing value visible. On the
    // first following pass the contour resolver clears it; one pass later
    // the ROM re-arms FFF0C1 for the falling phase. Apply the delayed arm
    // after contour resolution so the resolver cannot immediately erase it.
    if (jump_landing_state_arm_now_) {
        player_.terrain_landing_state = 0xFF;
        jump_landing_state_arm_now_ = false;
    }
    if (jump_landing_state_arm_pending_) {
        jump_landing_state_arm_now_ = true;
        jump_landing_state_arm_pending_ = false;
    }
    player_.animation_selector.landing_state = player_.terrain_landing_state;
    if (completed_jump_ground_response_latch_for_frame) {
        // Contour sampling can clear the promoted response before the
        // horizontal pass sees it. Genesis keeps FFF0CC promoted through the
        // action-to-run handoff, including the jump that follows it.
        player_.terrain_response_timer_state = 0xFF;
        player_.animation_selector.response_timer = 0xFF;
        if (player_.terrain_response_active == 0) {
            camera_.vertical_threshold = 400;
        }
    }
    if (completed_jump_ground_response_run_pending) {
        // The response selector's run-root boundary also holds the camera
        // settling delay for one additional phase. That keeps the final
        // delayed frame from performing the left-edge follow before the jump
        // stream re-arms the normal threshold.
        camera_.update_delay = 7;
    }
    if (timed_jump_response_latch_exit
        && player_.terrain_landing_state == 0) {
        // The timed jump keeps the response counter active, but its first
        // airborne pass consumes the ground-response latch and horizontal
        // correction. The following counter ticks use the ordinary two-pixel
        // active-response displacement.
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        player_.animation_selector.horizontal_response = 0;
        player_.animation_selector.response_timer = 0;
        player_.animation_selector.landing_state = 0;
    }
    // Non-flat contours are traversed by the grounded movement path even
    // though the ROM's public grounded predicate is false until FFF0C1
    // returns to 1. Keep that distinction explicit for movement and VM
    // selection while preserving the observed state byte.
    bool contour_ground_motion = (grounded_before_contour || contour_ground_motion_)
        && !player_.grounded
        && player_.terrain_landing_state != 0
        && player_.terrain_response_active == 0
        && player_.vy == 0;
    if (promoted_ground_response_was_active
        && player_.terrain_landing_state != 0
        && player_.terrain_response_active == 0
        && player_.vy == 0) {
        // Keep the promoted latch while the internal contour-ground mode is
        // active. A later jump clears that mode before its own landing, so
        // the next ordinary contour snap can clear FFF0CC normally.
        player_.terrain_response_timer_state = 0xFF;
        player_.animation_selector.response_timer = 0xFF;
    }
    contour_ground_motion_ = contour_ground_motion;
    bool was_grounded = player_.grounded || contour_ground_motion;
    const bool just_landed = !grounded_before_contour
        && !contour_ground_motion_before
        && player_.grounded;
    // Terrain_ResolvePlayerCell consumes the world coordinate captured before
    // MovementVM. Its one invocation is placed after the actor terrain and
    // player collision services, and before the player response integrator.
    const int previous_world_y = services_.player_world_y();
    if (player_.attack_timer != 0) {
        --player_.attack_timer;
    }
    if (input.attack_pressed && was_grounded) {
        player_.attack_timer = 10;
    }
    const auto collision = level_.query_player_collision(
        services_.player_world_x(), services_.player_world_y(),
        player_.terrain_landing_state);
    player_.terrain_stop_left_motion = collision.stop_left ? 0xFF : 0;
    player_.terrain_stop_right_motion = collision.stop_right ? 0xFF : 0;
    player_.terrain_stop_upward_motion = collision.stop_upward ? 0xFF : 0;
    player_.terrain_left_inner_probe = collision.left_inner ? 0xFF : 0;
    player_.terrain_left_outer_probe = collision.left_outer ? 0xFF : 0;
    player_.terrain_right_inner_probe = collision.right_inner ? 0xFF : 0;
    player_.terrain_right_outer_probe = collision.right_outer ? 0xFF : 0;
    const bool suppress_transition_root_response =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0
        && input.up
        && (player_.animation_selector.interaction_lock != 0
            || (runtime_.transition_root_cursor_motion_active
                && (animation_.animation_pc() == kPlayerBounceResponseLandingCursor
                    || (animation_.animation_pc() == kPlayerTerrainBounceRoot
                        && (frame_phase_ & 1U) == 0))))
        && !transition_root_response_retry_pending;
    const bool transition_root_up_cursor =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0
        && input.up
        && player_.animation_selector.interaction_lock == 1;
    const bool transition_root_cursor_motion =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && animation_.animation_pc() == kPlayerBounceResponseLandingCursor
        && player_.terrain_transition_gate != 0
        && runtime_.transition_root_cursor_motion_active
        && (frame_phase_ & 1U) != 0;
    const bool transition_root_vertical_band_animation =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0
        && input.up
        && (frame_phase_ & 1U) != 0;
    if (transition_root_vertical_band_animation) {
        animation_.select_vertical_band_animation(
            static_cast<std::uint32_t>(services_.player_world_y()));
    }
    const bool transition_root_rom_response_motion =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0
        && input.up
        && !suppress_transition_root_response
        && animation_.animation_pc() != kPlayerBounceResponseLandingStream
        && animation_.animation_pc() != kPlayerBounceResponseLandingCursor;
    const bool transition_root_release_response =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0
        && !input.up
        && !input.left
        && !input.right
        && player_.animation_selector.interaction_lock == 0
        && animation_.animation_pc() == kPlayerBounceResponseLandingStream;
    player_system_.apply_response(state_, TerrainResponseContext{
        transition_root_rom_response_motion
            ? frame_ + 1
            : frame_,
        scene_.is_transition(),
        animation_.stream_entry() == 0x00122006
            || (animation_.stream_kind() == AnimationStreamKind::Action
                && animation_.stream_entry() == 0x00122014),
        suppress_transition_root_response,
    });
    if (transition_root_release_response) {
        // The first released-Up boundary still consumes the root's final
        // two-pixel upward response before Camera_UpdateFollow settles the
        // cursor. The following neutral frames are camera-only.
        player_.y -= 2;
        camera_.vertical_threshold = 400;
        camera_.update_delay = 0;
    }
    if (suppress_transition_root_response
        && player_.animation_selector.interaction_lock != 0) {
        runtime_.transition_root_response_retry_pending = true;
    }
    if (transition_root_response_retry_pending) {
        // The ROM's second-frame Up retry uses the even-frame contour
        // increment while this native scheduler reaches the response helper
        // one phase earlier. Restore the one-pixel pre-follow difference;
        // Camera_UpdateFollow then supplies the same three-pixel scroll.
        player_.y += 1;
    }
    if (transition_root_up_cursor) {
        // The next held-Up phase advances the transition root's world cursor
        // by two pixels before Camera_UpdateFollow applies its three-pixel
        // upward scroll.
        player_.y -= 2;
    }
    if (transition_root_cursor_motion) {
        // The transition cursor applies the same two-pixel upward world
        // correction on each odd service phase before the camera settles.
        player_.y -= 2;
    }
    if (animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0
        && runtime_.transition_root_cursor_motion_active
        && input.up
        && suppress_transition_root_response
        && player_.animation_selector.interaction_lock == 0
        && !transition_root_response_retry_pending
        && (frame_phase_ & 1U) == 0) {
        // Player_ApplyTerrainResponse uses the one-pixel even-phase branch
        // after the interaction lock expires. The transition-root response
        // suppression above intentionally owns the helper call, so preserve
        // that visible half-step at this scheduler boundary.
        player_.y -= 1;
    }
    if (interactions_.bounce_response_follow_active()
        && player_.terrain_response_active == 0) {
        player_.terrain_response_timer_state = 1;
        if (animation_.stream_entry() == 0x00122006) {
            camera_.vertical_threshold = 400;
        }
    }
    const bool contour_ground_direction_response_start =
        !was_grounded
        && !player_.grounded
        && player_.terrain_response_active == 0
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && player_.terrain_response_timer_state == 0
        && player_.terrain_transition_gate == 0
        && player_.terrain_push_down == 0
        && player_.terrain_push_up == 0
        && player_.terrain_bounce_animation_state == 0x2A
        && input.left != input.right
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerIdleStream
        && animation_.animation_pc() == kPlayerIdleStream;
    if (contour_ground_direction_response_start) {
        // Player_TerrainResponseStateMachine has a private contour-ground
        // path that is independent of the public FFF0C1 grounded byte.  A
        // direction edge on this idle contour handoff selects the run
        // response, arms FFF0CC with -1 (0xFF), and applies the three-pixel
        // local step.  Without this branch native remains visually idle at
        // the Down->left boundary even though Genesis is already walking.
        player_.terrain_horizontal_response = 3;
        player_.terrain_response_timer_state = 0xFF;
        player_.animation_selector.horizontal_response = 3;
        player_.animation_selector.response_timer = 0xFF;
        contour_ground_motion = true;
        contour_ground_motion_ = true;
        was_grounded = true;
        camera_.horizontal_threshold = input.left ? 0xF0 : 0x70;
        camera_.update_delay = 7;
    }
    if (contour_ground_motion
        && input.left != input.right
        && player_.terrain_bounce_animation_state == 0x2A
        && player_.terrain_response_timer_state == 0) {
        // Once the contour-ground mode is live, a later direction edge takes
        // the same ROM branch as the initial idle handoff. In particular it
        // re-arms FFF0CC as -1 (0xFF), rather than the ordinary short value
        // used by public grounded motion.
        player_.terrain_response_timer_state = 0xFF;
        player_.animation_selector.response_timer = 0xFF;
    }
    const int input_direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    // The ROM's movement VM performs its cull before integrating actor
    // deltas. Use the pre-motion actor coordinates and pre-follow camera so
    // edge retirement lines up with the synchronized MAME boundary.
    services_.update_dynamic_actor_culling();
    services_.record_scheduler_phase("movement_vm", 0x001ADE36);
    actor_movement_.update(state_, runtime_, rom_bytes_);
    services_.record_scheduler_phase("actor_terrain_collision", 0x001ADB5C);
    actor_terrain_.update(
        state_,
        level_,
        rom_bytes_,
        stable_terrain_handler_fixture
    );
    services_.record_scheduler_phase("player_actor_interaction", 0x001ABB40);
    // The host attack timer only marks the ten-frame input handoff. The ROM
    // keeps the sword action live through the stable animation stream, where
    // the player hitbox is still active after that timer has expired.
    const std::uint32_t current_animation = animation_.animation_pc();
    const bool sword_stream_active = current_animation >= kPlayerSwordAnimationStream
        && current_animation <= kPlayerSwordAnimationEnd;
    const bool sword_action_was_active =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == kPlayerSwordAnimationStream;
    const bool player_sword_active = input.attack_pressed
        // attack_timer is the selector's ten-frame handoff latch, not the
        // sword hitbox itself. In particular, airborne action streams keep
        // this timer live while the ROM still treats the sword as inactive.
        // The actual sword stream and FFF0D8 below are the hitbox gates.
        || player_.animation_selector.action_response_field != 0
        || animation_.stream_entry() == kPlayerSwordAnimationStream
        || sword_stream_active;
    const CollisionEffects collision_effects = collisions_.player_actor(
        state_,
        PlayerCollisionInput{
            animation_.frame_pointer(),
            animation_.facing_left(),
            player_sword_active,
            interactions_.bounce_response_follow_active(),
            player_.animation_selector.action_response_field != 0
        }
    );
    interactions_.consume_collision_effects(collision_effects);
    if (collision_effects.player_animation_stream) {
        animation_.select_stream_entry(*collision_effects.player_animation_stream);
    }
    if (collision_effects.player_idle_animation_reset) {
        animation_.select_locomotion_entry(
            kPlayerIdleStream,
            false,
            SpritePose::Idle);
        // The collision response owns this boundary. A simultaneous B edge
        // may have armed the host-side timer before the collision pass, but
        // Genesis consumes that edge with the response instead of exposing a
        // sword attack on the next state sample.
        player_.attack_timer = 0;
        player_.animation_selector.interaction_pending = 0;
    }
    services_.append_sound_requests(collision_effects.sound_requests);
    services_.record_scheduler_phase("terrain_resolution", 0x001B1E38);
    const auto terrain_cell = player_system_.resolve(
        state_,
        level_,
        previous_world_y,
        checkpoint_terrain_handler_fixture
            ? std::optional<std::uint8_t>(checkpoint_terrain_behavior_)
            : std::nullopt
    );
    if (terrain_cell) {
        player_system_.apply_behavior(state_, *terrain_cell, runtime_, rom_bytes_);
    }
    const bool terrain_bounce_root_handoff =
        jump_response_was_complete
        && terrain_response_was_active
        && player_.terrain_response_active == 0
        && player_.terrain_landing_state == 0
        && player_.terrain_vertical_stop == 0xFF
        && player_.terrain_behavior == 0x22
        && player_.terrain_transition_gate != 0
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == 0x0012214E;
    if (terrain_bounce_root_handoff) {
        // Terrain_ResolvePlayerCell raises FFF0D0 when the completed timed
        // response leaves the playable contour. The bounce root is published
        // at that same boundary, including its frame-pointer table lookup.
        player_.animation_selector.transition_gate =
            player_.terrain_transition_gate;
        animation_.select_locomotion_entry(
            kPlayerTerrainBounceRoot,
            false,
            SpritePose::Jump,
            true);
        // The ROM's horizontal response pass still consumes the three-pixel
        // correction after the vertical response has been cleared. Preserve
        // that magnitude through update_horizontal; the state is cleared
        // immediately after the movement pass below.
        player_.terrain_horizontal_response = 3;
    }
    const bool bounce_response_landing_event =
        jump_response_was_complete
        && terrain_response_was_active
        && animation_pc_at_frame_start == 0x0012216E
        && player_.terrain_response_active == 0
        && player_.vy == 0
        && player_.terrain_landing_state == 0
        && player_.terrain_bounce_animation_state == 0x2A;
    if (bounce_response_landing_event && input_direction != 0) {
        // The completed response consumes the normal two-pixel airborne
        // step, then applies the ROM's final three-pixel correction before
        // publishing the 0x121884 landing/transition root.
        player_.x += input_direction * 3;
        player_.terrain_transition_gate = 0xFF;
        player_.animation_selector.transition_gate = 0xFF;
        runtime_.transition_root_cursor_motion_active = true;
    }
    // The camera's tile reference is consumed before the actor traversal in
    // the ROM. Rebase it now so the refill edge and the common actor gate see
    // the same newly crossed tile on this VBlank.
    const int camera_reference_x_before_rebase = camera_.reference_x;
    const int camera_reference_y_before_rebase = camera_.reference_y;
    camera_system_.rebase(state_, level_);
    const bool camera_horizontal_reference_rebased =
        camera_.reference_x != camera_reference_x_before_rebase;
    const bool camera_vertical_reference_rebased =
        camera_.reference_y != camera_reference_y_before_rebase;
    if (camera_vertical_reference_rebased) {
        runtime_.transition_root_cursor_motion_active = false;
    }
    const bool camera_reference_moved_up =
        camera_.reference_y < camera_reference_y_before_rebase;
    interactions_.apply_surface_interaction_lock(state_, arm_surface_interaction);
    const std::uint8_t vertical_stop_value_before_frame =
        player_.terrain_vertical_stop;
    const bool vertical_stop_before_frame = vertical_stop_value_before_frame != 0;
    const bool terrain_bounce_root_jump_start =
        input.jump_pressed
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerTerrainBounceRoot
        && player_.terrain_transition_gate != 0;
    const bool bounce_response_landing_root_jump_start =
        input.jump_pressed
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0;
    const bool terrain_bounce_root_camera_rebase =
        camera_horizontal_reference_rebased
        && terrain_response_horizontal_carry
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerTerrainBounceRoot
        && player_.terrain_response_active == 0
        && player_.terrain_transition_gate != 0;
    if (terrain_bounce_root_camera_rebase) {
        // The bounce root's horizontal reference rebase also consumes the
        // two-pixel local correction that Genesis applies on this boundary.
        // It is specific to the root's FFF0D0-gated path; ordinary camera
        // rebases already match without a player-coordinate adjustment.
        player_.x -= 2;
    }
    const bool start_jump = input.jump_pressed
        && (was_grounded
            || terrain_bounce_root_jump_start
            || bounce_response_landing_root_jump_start);
    const bool terrain_bounce_root_jump_followup =
        player_.terrain_response_active != 0
        && (player_.terrain_transition_gate != 0
            || player_.animation_selector.transition_gate != 0)
        && player_.terrain_jump_response_counter != 0
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() != kPlayerTerrainBounceRoot;
    if (terrain_bounce_root_jump_followup) {
        // The bounce root accepts the jump edge on its first boundary, then
        // clears FFF0D0 on the following pass while the new jump response is
        // already active.
        player_.terrain_transition_gate = 0;
        player_.animation_selector.transition_gate = 0;
    }
    AnimationContext animation_context = services_.player_animation_context(was_grounded);

    const bool blocked_right_wall_response = was_grounded
        && (player_.grounded || contour_ground_motion)
        && input.right
        && !input.left
        && player_.terrain_stop_right_motion != 0;
    const bool blocked_left_wall_response = was_grounded
        && (player_.grounded || contour_ground_motion)
        && input.left
        && !input.right
        && player_.terrain_stop_left_motion != 0;
    if (was_grounded && (player_.grounded || contour_ground_motion)) {
        if (input.left != input.right) {
            const int threshold = input.left ? 0xF0 : 0x70;
            if (camera_.horizontal_threshold != threshold) {
                camera_.horizontal_threshold = threshold;
                camera_.update_delay = 7;
            }
        }
    }
    const bool ground_response_was_active =
        was_grounded
        && (player_.grounded || contour_ground_motion)
        && player_.terrain_response_timer_state != 0;
    const bool action_ground_response_start =
        !was_grounded
        && !contour_ground_motion
        && player_.terrain_landing_state == 6
        && player_.terrain_response_active == 0
        && player_.terrain_response_timer_state == 0
        && input.left != input.right
        && animation_.stream_kind() == AnimationStreamKind::Action
        && player_.animation_selector.action_response_field != 0;
    if (action_ground_response_start) {
        // After the response landing, a direction edge exits the held action
        // through the same three-pixel ground-response step as a grounded
        // edge. Arm that step before horizontal motion so this boundary gets
        // the ROM's visible X correction.
        player_.animation_selector.horizontal_response = 3;
        player_.animation_selector.response_timer = 1;
        // Player_Update also installs the walking threshold and gives the
        // camera follow its seven-frame settling delay on this transition.
        // Without this write the native camera follows the response step
        // immediately, cancelling one pixel of local motion each frame and
        // keeping the terrain query on the old slope column.
        camera_.horizontal_threshold = 0x70;
        camera_.update_delay = 7;
        player_.terrain_horizontal_response = 3;
        player_.terrain_response_timer_state = 1;
        player_.animation_selector.action_response_field = 0;
    }
    const bool idle_ground_response_start =
        player_.terrain_response_active == 0
        && player_.terrain_response_timer_state == 0
        && player_.terrain_landing_state == 11
        && input.left != input.right
        && animation_.stream_entry() == kPlayerIdleStream
        && animation_.animation_pc() == 0x00121DC2
        && camera_.vertical_threshold == 400;
    if (idle_ground_response_start) {
        // After the falling response returns through the idle cursor,
        // Player_Update selects the run root before the next motion/camera
        // pass. The direction edge therefore gets its three-pixel step while
        // the camera delay is still armed.
        animation_.select_stream_entry(kPlayerRunStream, true);
        player_.terrain_horizontal_response = 3;
        player_.terrain_response_timer_state = 1;
        player_.animation_selector.horizontal_response = 3;
        player_.animation_selector.response_timer = 1;
        camera_.horizontal_threshold = 0x70;
        camera_.update_delay = 7;
    }
    const bool action_ground_response_motion =
        was_grounded
        && input.left != input.right
        && player_.terrain_response_active == 0
        && player_.terrain_response_timer_state != 0
        && animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == kPlayerRunStream;
    const PlayerMotionResult motion = player_system_.update_horizontal(
        state_,
        runtime_,
        input,
        animation_,
        PlayerMotionInput{
            was_grounded,
            contour_ground_motion,
            (terrain_response_was_active || terrain_response_horizontal_carry)
                && !(animation_.stream_entry() == kPlayerBounceResponseLandingStream
                    && player_.terrain_transition_gate != 0),
            action_ground_response_motion,
            timed_jump_response_latch_exit
                || (animation_.stream_entry() == kPlayerBounceResponseLandingStream
                    && player_.terrain_transition_gate != 0),
        }
    );
    if (terrain_bounce_root_handoff) {
        player_.terrain_horizontal_response = 0;
        player_.animation_selector.horizontal_response = 0;
        player_.animation_selector.response_timer = 0;
        player_.terrain_response_timer_state = 0;
        // The completed response retains the ROM's final two-pixel active
        // displacement in addition to the three-pixel terrain correction.
        // Apply it before world-coordinate publication so camera follow sees
        // the same source position as Genesis.
        player_.x += input_direction * 2;
    }
    const bool ground_release = motion.ground_release;
    const int ground_release_direction = motion.ground_release_direction;
    const bool promoted_ground_response_release =
        promoted_ground_response_was_active
        && contour_ground_motion
        && player_.terrain_response_timer_state == 0
        && input.left == input.right;
    if (promoted_ground_response_release) {
        // Releasing the promoted contour response restores the ordinary
        // vertical camera threshold before Camera_UpdateFollow. The resulting
        // damped upward follow is visible on the first neutral boundary.
        camera_.vertical_threshold = 400;
    }
    const bool ground_response_started =
        ground_response_was_active == false
        && player_.terrain_response_timer_state != 0;
    const bool contour_ground_response_start =
        ground_response_started
        && !was_grounded
        && !contour_ground_motion
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && input.left != input.right
        && terrain_response_timer_was_armed
        && player_.terrain_bounce_animation_state >= 0x2D
        && player_.terrain_landing_state == 9
        && animation_.stream_entry() != 0x00122014
        && animation_.animation_pc() == kPlayerGroundResponseActionCursor;
    const bool contour_landing_direction_start =
        ground_response_started
        && was_grounded
        && contour_ground_motion
        && input.left != input.right
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerLandingAnimationStream;
    const bool grounded_landing_direction_start =
        ground_response_started
        && was_grounded
        && !contour_ground_motion
        && input.left != input.right
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerLandingAnimationStream;
    const bool ground_direction_start =
        was_grounded
        && input.left != input.right
        && last_ground_direction_ == 0
        && !contour_ground_motion_before
        && player_.terrain_response_timer_state != 0
        && !(animation_.stream_kind() == AnimationStreamKind::Action
            && animation_.stream_entry() == 0x00122014);
    const bool contour_ground_direction_start =
        was_grounded
        && input.left != input.right
        && last_ground_direction_ == 0
        && contour_ground_motion_before
        && player_.terrain_landing_state != 0
        && player_.terrain_response_timer_state != 0;
    bool landed_during_frame = false;
    if (checkpoint_terrain_handler_fixture
        && !was_grounded
        && !player_.grounded
        && player_.terrain_response_active != 0
        && player_.terrain_vertical_stop != 0
        && player_.vy >= 0) {
        // The fixture uses the recovered resolver behavior but still needs
        // the ROM's contour landing before the next motion integration. This
        // keeps the row-crossing frame airborne and lands on the following
        // pass, matching the original resolver's ordering.
        const auto contour = level_.query_player_contour(
            services_.player_world_x(), services_.player_world_y(), player_.terrain_surface_mode);
        if (contour.valid) {
            const int target_y = contour.target_world_y - camera_.y;
            if (std::abs(target_y - player_.y) <= 8) {
                player_.y = target_y;
                player_.vy = 0;
                player_.grounded = true;
                player_.terrain_landing_state = contour.contour;
                player_.terrain_response_active = 0;
                player_.terrain_response_timer_state = 0;
                player_.terrain_horizontal_response = 0;
                landed_during_frame = true;
            }
        }
    }
    services_.record_scheduler_phase("player_movement", 0x001A9D98);
    const std::int16_t vertical_velocity_before_integrate = player_.vy;
    player_system_.integrate(state_);
    if (terrain_response_was_active
        && player_.terrain_response_active == 0) {
        // The ROM's post-response horizontal correction remains live for one
        // additional frame after FFF0BE clears. The integrator can be the
        // writer that clears that byte, so carry the edge only after this
        // boundary has completed.
        runtime_.terrain_response_horizontal_carry = true;
    }
    const bool defer_complete_jump_bounce_collision =
        jump_response_was_complete
        && !bounce_response_was_active
        && animation_.stream_entry() == 0x00121AD8
        && animation_.animation_pc() == 0x00121AE0
        && player_.terrain_bounce_animation_state == 0x29;
    if (defer_complete_jump_bounce_collision) {
        // The first overlap after the dynamic fall-root command is observed
        // by Genesis on the following VBlank. Advance the private marker so
        // the same candidate is accepted on that next pass.
        player_.terrain_bounce_animation_state = 0x2A;
    }
    const CollisionEffects bounce_effects = defer_complete_jump_bounce_collision
        ? CollisionEffects{}
        : collisions_.bounce_player_actor(
            state_,
            PlayerCollisionInput{
                animation_.frame_pointer(),
                animation_.facing_left(),
                false,
                interactions_.bounce_response_follow_active(),
                false
            }
        );
    if (bounce_effects.player_bounce_response_started) {
        interactions_.start_bounce_response();
        terrain_fall_phase_ = false;
    }
    if (bounce_effects.player_animation_stream) {
        if (bounce_effects.player_animation_state_immediate) {
            animation_.set_animation_state(*bounce_effects.player_animation_stream, 0);
        } else {
            animation_.select_stream_entry(*bounce_effects.player_animation_stream);
        }
    }
    services_.append_sound_requests(bounce_effects.sound_requests);
    if (player_.terrain_response_active != 0
        && player_.terrain_jump_response_counter != 0
        && player_.terrain_jump_response_counter < 10) {
        // Player_HandleJumpAndVerticalState applies this extra impulse after
        // Player_IntegrateMotion during the first nine active-response ticks.
        ++player_.terrain_jump_response_counter;
        if ((input.jump_held
             || animation_.stream_kind() == AnimationStreamKind::Action)
            && animation_.stream_kind() != AnimationStreamKind::Response) {
            player_.vy = static_cast<std::int16_t>(player_.vy - 0x006C);
        }
    }
    if (checkpoint_terrain_behavior_override_ && checkpoint_terrain_behavior_ == 0x2B) {
        // The original response path continues through the positive-motion
        // state after TerrainHandler_StopAndAlignPlayer: it advances VY by
        // 0x78. Camera_UpdateFollow then applies the visible four-pixel
        // local-X correction.
        player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
    }
    const bool vertical_stop_raised_during_integrate =
        player_.vy == 0
        && player_.terrain_vertical_stop == 0xFF
        && vertical_stop_value_at_frame_start == 0x01;
    if (!player_.grounded && player_.terrain_response_active != 0
        && vertical_stop_before_frame) {
        // The ROM clears the launch tile's behavior before entering the
        // positive vertical phase. Keep the terrain response active for the
        // published RAM state, but let the post-integrator handoff run.
        player_.terrain_behavior = 0;
    }
    // The bounce response clears FFF0BE when its positive fall reaches the
    // handoff boundary.  Genesis immediately arms the ordinary ground
    // response latch and holds Camera_UpdateFollow for seven VBlanks before
    // returning to the run stream.
    const bool bounce_response_finished = interactions_.finish_bounce_response(
        state_, terrain_response_was_active, jump_response_was_complete);
    if (!player_.grounded && player_.terrain_behavior == 0
        && (vertical_stop_before_frame || player_.terrain_response_timer_state != 0)
        && !vertical_stop_raised_during_integrate) {
        // This is the post-integrator jump/vertical-state handoff. It is
        // intentionally after resolve_terrain: the original frame where the
        // residual upward velocity is cleared still exposes VY=0; the next
        // frame starts the positive phase at 0x003C.
        // When the integrator itself raises FFF0C0 while clearing the last
        // upward residual, the ROM publishes that boundary with VY still
        // zero.  The positive phase starts on the following frame; do not
        // consume the newly-written stop in the same pass.
        if (!terrain_fall_phase_
            && grounded_before_contour
            && player_.terrain_landing_state == 0
            && player_.terrain_vertical_stop == 0
            && player_.terrain_response_timer_state != 0) {
            // Walking off the last contour pixel enters the positive phase
            // immediately, even though no explicit vertical-stop byte was
            // raised by the integrator on that boundary.
            player_.vy = 0x003C;
            terrain_fall_phase_ = true;
        } else if (!terrain_fall_phase_ && player_.terrain_vertical_stop == 0xFF
            && player_.terrain_landing_state == 0
            && (vertical_stop_before_frame || player_.vy != 0)) {
            player_.vy = 0x003C;
            terrain_fall_phase_ = true;
        } else if (!terrain_fall_phase_
                   && player_.terrain_response_active != 0
                   && player_.terrain_vertical_stop != 0
                   && player_.vy == 0
                   && vertical_stop_before_frame) {
            // A held jump can reach the upward-stop boundary with the
            // published stop byte already at 1 rather than FF. The ROM
            // still enters its positive phase on the following frame.
            player_.vy = 0x003C;
            terrain_fall_phase_ = true;
        } else if (terrain_fall_phase_) {
            const bool bounce_state_gated =
                (player_.terrain_response_active != 0
                    && player_.terrain_vertical_stop != 0)
                || player_.terrain_transition_gate != 0
                || player_.animation_selector.transition_lock != 0
                || player_.animation_selector.transition_state != 0;
            if (bounce_state_gated && player_.vy < 0x0800) {
                // The recovered helper is entered from the resolver's
                // accepted branches. Keep the existing post-integrator
                // fallback for staged fixtures that intentionally expose a
                // stopped response while retaining the fall-phase latch.
                player_.vy = static_cast<std::int16_t>(player_.vy + 0x0078);
            } else if (!(vertical_velocity_before_integrate > 0
                         && static_cast<int>(vertical_velocity_before_integrate)
                             + 0x0078 >= 0x0800)) {
                const bool bounce_state_advanced =
                    player_system_.advance_bounce_state(state_);
                if (bounce_state_advanced && !jump_response_was_complete) {
                    animation_.select_locomotion_entry(0x00121AD8);
                }
            }
        }
        // FFF0C0 remains set after the residual-upward stop. The original
        // contour routine uses that latched bit to distinguish the later
        // falling/landing phase while FFF0BE is still active.
    }
    // Terrain handlers can arm the interaction lock after the frame's initial
    // state read. The animation VM observes that mutation through GameRamView
    // at its later scheduler boundary.
    if (start_jump
        && (player_.grounded
            || contour_ground_motion
            || terrain_bounce_root_jump_start
            || bounce_response_landing_root_jump_start)) {
        // The recovered frame order applies the jump handler after motion and
        // terrain resolution (Player_Update -> Terrain_Resolve -> jump
        // handler). This leaves the impulse visible for the next frame before
        // the integrator consumes it.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
        contour_ground_motion = false;
        contour_ground_motion_ = false;
        player_.terrain_response_active = 0xFF;
        if (terrain_bounce_root_jump_start
            || bounce_response_landing_root_jump_start) {
            // This is a new vertical response, not a continuation of the
            // completed bounce's positive fall phase.
            terrain_fall_phase_ = false;
        }
        // FFF0BF starts at one for every ordinary jump and advances through
        // the ten-frame launch-response window. The extra -0x6C impulse is
        // conditional on the controller still being held; a one-frame tap
        // keeps the same counter but follows the ordinary arc.
        player_.terrain_jump_response_counter = 1;
        // A jump with a held horizontal direction follows the timed
        // terrain-response stream and retains FFF0CC=1. A neutral C press
        // takes the ordinary jump stream and clears the ground latch.
        if (input.left == input.right) {
            player_.terrain_response_timer_state = 0;
        }
        player_.terrain_vertical_stop = 0;
        // FFF0C1 remains at its grounded value for this launch boundary;
        // the contour pass clears it on the next frame and the falling-phase
        // response re-arms it one frame later.
        jump_landing_state_arm_pending_ = true;
        camera_.horizontal_threshold = 0xB0;
        camera_.vertical_threshold = 0x170;
        camera_.update_delay = 7;
    }
    if (player_.ground_braking && player_.vx == 0) {
        player_.ground_braking = false;
    }

    if (ground_release) {
        // The first no-input frame enters the ROM's inertial ground path after
        // the position/integration work but before camera follow. The common
        // animation pass selects the ROM brake stream on this boundary;
        // camera threshold changes are owned by that stream rather than being
        // inferred from the input edge here.
        player_system_.finish_ground_release(
            state_, runtime_, ground_release_direction);
    }

    const bool release_up_animation =
        !input.up && player_.animation_selector.transition_state_df != 0;
    if (release_up_animation) {
        // This clear belongs to Player_TerrainResponseStateMachine, before
        // Camera_UpdateFollow. The selected action stream remains active.
        player_.animation_selector.transition_state_df = 0;
        camera_.vertical_threshold = 0x170;
    }
    const bool select_down_animation =
        input.down
        && (player_.grounded
            || contour_ground_motion
            || player_.terrain_landing_state != 0)
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && player_.animation_selector.transition_state_de == 0
        && player_.animation_selector.transition_state_df == 0;
    if (select_down_animation) {
        // Player_TerrainResponseStateMachine selects the down stream before
        // the common VM tick. Its root is 0x001222D2; the tick then publishes
        // the first data cursor at 0x001222D4.
        animation_.select_stream_entry(kPlayerDownAnimationStream);
        player_.animation_selector.transition_state_de = 0xFF;
        player_.animation_selector.response_animation = 0;
        player_.animation_selector.state_lock = 0;
    }
    const bool release_down_animation =
        !input.down && player_.animation_selector.transition_state_de != 0;
    if (release_down_animation) {
        // The same terrain state machine clears the down latch after the
        // held-down stream has had its final frame; the stream itself owns
        // the later F8 handoff back to locomotion.
        player_.animation_selector.transition_state_de = 0;
        camera_.vertical_threshold = 0x170;
    }
    // The VM reads these post-handler values directly from GameState through
    // GameRamView. The invocation context remains a lightweight description
    // of this frame's call boundary rather than a copied RAM image.

    // Player_HandleJumpAndVerticalState and the terrain response service have
    // now finished writing local position. Publish the third ROM coordinate
    // boundary before Camera_UpdateFollow consumes it.
    services_.record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    services_.publish_player_world_coordinates();

    // The ROM consumes a pending 16-pixel reference shift before the actor
    // traversal and then runs the damped follow below. This leaves the
    // boundary frame externally visible with scroll == 16, then exposes the
    // rebased reference on the following frame.
    // A downward Camera_UpdateFollow reference-tile rebase suppresses the
    // vertical damped lookup for that frame, but an upward rebase keeps the
    // same-frame vertical lookup. Both directions still service the
    // independent horizontal follow.
    const bool camera_follow_deferred = camera_vertical_reference_rebased;
    const bool post_down_contour_landing =
        !contour_ground_motion_before
        && !player_.grounded
        && player_.terrain_landing_state == 3
        && player_.terrain_bounce_animation_state == 0x2A
        && player_.terrain_response_active == 0
        && player_.vy == 0;
    if (action_response_exit_candidate) {
        // The odd animation service consumes the terminal F4 in the
        // 0x122014 action stream. Its camera-threshold write is visible to
        // the same frame's follow pass, even though the cursor handoff is
        // published by the later animation service.
        player_.animation_selector.response_timer = 1;
        camera_.vertical_threshold = 400;
    }
    if (ground_direction_start || contour_ground_direction_start) {
        // Camera_UpdateFollow arms its short settling delay when the first
        // direction is applied to a stationary grounded player, even when
        // the horizontal threshold already has the requested value. The
        // threshold-change shortcut above misses this recorded boundary. The
        // contour path needs the same edge even though its public grounded
        // byte remains clear.
        camera_.update_delay = 7;
    }
    interactions_.hold_bounce_camera_delay(state_, bounce_response_finished);
    services_.record_scheduler_phase("camera_follow", 0x001AA90C);
    const bool same_frame_vertical_rebase =
        camera_reference_moved_up
        || player_.terrain_jump_response_counter != 0
        || (input.down && player_.terrain_landing_state != 0)
        || post_down_contour_landing;
    if (camera_follow_deferred && !same_frame_vertical_rebase
        && !interactions_.bounce_response_active()) {
        // The reference-tile write occupies this camera pass. The vertical
        // damped lookup resumes on the following VBlank; horizontal follow
        // is still handled by CameraSystem.
        camera_system_.update(state_, level_, game_data_, true);
    } else {
        camera_system_.update(state_, level_, game_data_);
        // A downward follow step can land exactly on the next camera tile
        // boundary. The ROM applies that reference update after the follow
        // pass; an earlier sub-tile crossing remains pending for the next
        // camera pass.
        if (player_.terrain_response_active == 0
            && player_.terrain_jump_response_counter == 0
            && camera_.y >= camera_.reference_y + 0x10
            && (camera_.y & 0x0F) == 0) {
            (void) camera_system_.rebase(state_, level_);
        }
    }
    if (was_grounded && (player_.grounded || contour_ground_motion) && input_direction != 0) {
        last_ground_direction_ = input_direction;
    } else if (input_direction == 0 && contour_ground_motion) {
        // The contour-ground path keeps the public grounded byte clear, but
        // a neutral frame still releases its direction latch. A later held
        // direction must be able to arm the seven-frame camera settling delay
        // again, just as it does for a normally grounded player.
        last_ground_direction_ = 0;
    } else if (!player_.grounded && !contour_ground_motion) {
        last_ground_direction_ = 0;
    }

    const bool response_landing_event =
        terrain_response_was_active
        && player_.terrain_response_active == 0
        && player_.terrain_landing_state != 0
        && player_.vy == 0;
    const bool completed_jump_landing_direction_start =
        response_landing_event
        && jump_response_was_complete
        && animation_pc_at_frame_start == 0x00121AEC
        && input.left != input.right;
    const bool direct_bounce_landing_event =
        (response_landing_event
            && jump_response_was_complete
            && bounce_response_was_active
            && animation_pc_at_frame_start == kPlayerBounceLandingTriggerCursor)
        || (jump_response_was_complete
            && !grounded_before_contour
            && !contour_ground_motion_before
            && !terrain_response_timer_was_armed
            && player_.terrain_response_active == 0
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && player_.terrain_bounce_animation_state >= 0x2D
        && animation_.animation_pc() == kPlayerBounceLandingTriggerCursor);
    const bool contour_landing_run_handoff =
        response_landing_event
        && animation_pc_at_frame_start == 0x0012217C
        && input.left != input.right;
    const bool contour_landing_event =
        !grounded_before_contour
        && !contour_ground_motion_before
        && player_.terrain_landing_state != 0
        && player_.vy == 0;
    const bool generic_contour_landing_event =
        contour_landing_event
        && !direct_bounce_landing_event
        && !contour_ground_response_start;
    const bool landing_event =
        just_landed
        || landed_during_frame
        || response_landing_event
        || generic_contour_landing_event;
    const bool ground_response_ended =
        ground_response_was_active
        && player_.terrain_response_timer_state == 0;
    // A contour response can finish while the public grounded bit was already
    // set. In that case the ordinary just-landed edge is absent, but Genesis
    // still enters the landing stream for one VM pass before accepting a
    // button-edge action on the following frame.
    const bool ground_response_landing =
        (ground_response_ended || landing_event)
        && input.attack_pressed
        && player_.grounded
        && player_.vy == 0
        && player_.terrain_response_active == 0
        && player_.terrain_landing_state != 0;
    const bool preserve_ground_response_run =
        !player_.grounded
        && !contour_ground_motion
        && !interactions_.player_collision_pending()
        && player_.terrain_response_active == 0
        && player_.terrain_response_timer_state != 0
        && animation_.animation_pc() >= 0x00122006
        && animation_.animation_pc() <= 0x001220A6;
    const bool preserve_bounce_response_landing_root =
        animation_.stream_entry() == kPlayerBounceResponseLandingStream
        && player_.terrain_transition_gate != 0;
    SpritePose desired_pose = SpritePose::Idle;
    if (preserve_ground_response_run) {
        // The run/ground response owns the horizontal step even after the
        // contour latch drops. Keep its stream alive while the positive
        // vertical phase starts; switching to the generic jump stream here
        // would stop the camera-follow run sequence one frame too early.
        desired_pose = SpritePose::Run;
        if (!interactions_.bounce_response_follow_active() || bounce_response_finished) {
            player_.animation_selector.response_state_101 = 1;
        }
    } else if (ground_response_landing) {
        desired_pose = SpritePose::Landing;
    } else if (bounce_response_landing_root_jump_start) {
        // The special transition root normally holds an idle presentation,
        // but a C edge transfers ownership to the jump stream on this same
        // boundary. Do not let the root-preservation rule overwrite it.
        desired_pose = SpritePose::Jump;
    } else if (preserve_bounce_response_landing_root) {
        desired_pose = SpritePose::Idle;
    } else if (ground_response_was_active
               && player_.animation_selector.response_state_101 != 0
               && input.left == input.right) {
        // Player_Update consumes FFF101 on the terminal ground-response
        // boundary and selects the brake stream, retaining FFF0B0 until the
        // following inertial-release pass.
        desired_pose = SpritePose::Brake;
        player_.animation_selector.response_state_101 = 0;
    } else if (contour_landing_direction_start
               || grounded_landing_direction_start) {
        // A non-flat landing keeps the public grounded bit clear while its
        // compact landing stream is finishing. The first directional ground
        // response is the ROM's handoff back to the run stream, even though
        // the landing cursor has not reached its terminal record yet. The
        // flat case uses the same landing cursor, but the public grounded
        // byte is already set.
        desired_pose = SpritePose::Run;
    } else if (contour_landing_run_handoff) {
        // This response landing returns directly through the held-direction
        // run root. It is distinct from the ordinary landing stream: the ROM
        // keeps the left-facing threshold armed and exposes 0x122006 on the
        // landing boundary.
        desired_pose = SpritePose::Run;
        camera_.horizontal_threshold = input.left ? 0xF0 : 0x70;
    } else if ((sword_action_pending
                && sword_action_pending_delay != 0
                && animation_.stream_entry() == kPlayerIdleStream)
               || ((animation_.landing_idle_hold()
                    || (!player_.grounded
                        && player_.terrain_landing_state == 6
                        && animation_.stream_entry() == kPlayerIdleStream))
                   && !input.attack_pressed)) {
        // The response landing returns through F8 to idle for one quiet
        // boundary before a later held-button action is accepted. The public
        // grounded bit remains clear on this slope, so the generic airborne
        // fallback would incorrectly restart the jump stream here.
        desired_pose = SpritePose::Idle;
    } else if (!response_landing_event
               && !generic_contour_landing_event
               && !player_.grounded
               && !contour_ground_motion
               && !(animation_.pose() == SpritePose::Landing
                    && !animation_.finished())) {
        // Genesis keeps the jump stream active during the approach frame. It
        // selects the landing stream only after the contour resolver has
        // actually latched grounded state on the following boundary.
        desired_pose = SpritePose::Jump;
    } else if (response_landing_event
               || (landing_event
                   && (animation_.pose() == SpritePose::Jump
                       || post_down_contour_landing))
               || (animation_.pose() == SpritePose::Landing && !animation_.finished())) {
        // The terrain resolver can latch grounded while the active stream is
        // still the run/ground-response program (the opening slope does this
        // at frame 618).  The ROM does not replace that locomotion cursor
        // merely because FFF0C1 changed to a nonzero contour; the landing
        // root is selected only when the preceding player stream is the jump
        // program.  A generic just-landed test would incorrectly jump from
        // 0x0012207E to 0x00121F84 one boundary too early.
        desired_pose = SpritePose::Landing;
    } else if (input.left != input.right) {
        desired_pose = SpritePose::Run;
    } else if (input.attack_pressed || player_.attack_timer != 0) {
        // A sword press does not turn a running player into the brake stream.
        // Preserve the current locomotion pose until the post-VM sword root
        // handoff below, matching MAME's run -> stable-sword boundary.
        desired_pose = animation_.pose() == SpritePose::Run
            ? SpritePose::Run
            : SpritePose::Idle;
    } else if (ground_response_was_active) {
        // Releasing a direction while FFF0CC is armed completes the short
        // ground-response path and returns to idle. It is distinct from the
        // ordinary inertial release, which selects the brake stream.
        desired_pose = SpritePose::Idle;
    } else if (ground_release
               || animation_.pose() == SpritePose::Brake
               || (!player_.ground_braking && animation_.pose() == SpritePose::Run)) {
        desired_pose = SpritePose::Brake;
    }
    // The common actor VM normally sees the pre-integration state. During the
    // active-response jump, however, the ROM's vertical handler has already
    // updated PLAYER_VY before the jump stream's F4 branch is evaluated. Keep
    // that one shared RAM value at the post-integration boundary so the
    // signed threshold transition at 0x001221B8 follows the ROM.
    AnimationContext vm_context = animation_context;
    if (desired_pose == SpritePose::Jump && player_.terrain_response_active != 0) {
        vm_context.player_vy_override = player_.vy;
        // FFF0C1 is cleared while the active-response jump is airborne. The
        // native terrain mirror retains the launch contour for landing
        // resolution, so keep the VM's selector input at the ROM value.
        vm_context.landing_state_override = 0;
    }
    if (ground_response_started
        && was_grounded
        && animation_.stream_kind() == AnimationStreamKind::Action
        && (player_.attack_timer != 0
            || animation_.stream_entry() == kPlayerSwordAnimationStream
            || animation_.stream_entry() == 0x001226CE)) {
        // A direction edge cancels the tail of an attack when it enters the
        // grounded response step. The ROM leaves the new run root visible
        // for the current even scheduler phase, so select it before the
        // common VM pass and let the next odd phase consume its first frame.
        player_.attack_timer = 0;
        player_.animation_selector.interaction_pending = 0;
        if (current_animation != 0x00122722) {
            player_.animation_selector.action_response_field = 0;
        }
        desired_pose = SpritePose::Run;
        animation_.select_locomotion_stream(SpritePose::Run, vm_context);
    }
    if (ground_response_landing
        && animation_.stream_entry() != kPlayerLandingAnimationStream) {
        // Select the landing root before ordinal 30. On the odd phase the VM
        // consumes its first record and publishes 0x00121F92, which is the
        // cursor visible in the Genesis trace at this boundary.
        animation_.select_locomotion_entry(
            kPlayerLandingAnimationStream,
            false,
            SpritePose::Landing);
        // The edge is consumed by the landing handoff. Genesis arms the
        // sword's ten-frame interaction window when the held button selects
        // the sword on the following frame, not on this landing boundary.
        player_.attack_timer = 0;
    }
    if (response_landing_event
        && !direct_bounce_landing_event
        && !contour_landing_run_handoff
        && animation_.stream_entry() != kPlayerLandingAnimationStream) {
        // A terrain response can land directly out of an action stream. The
        // ROM still installs the landing root before ordinal 30; leaving the
        // action cursor in place makes the first landing frame look like a
        // continued airborne action.
        animation_.select_locomotion_entry(
            kPlayerLandingAnimationStream,
            false,
            SpritePose::Landing);
        animation_.arm_landing_reselect();
        player_.attack_timer = 0;
    }
    if (ground_response_ended
        && animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == kPlayerActionTerrainTimerStream
        && player_.attack_timer == 0
        && player_.animation_selector.interaction_pending == 0) {
        // The terrain-timer action's continuation can reach the grounded
        // response cleanup on an even phase, where the common VM does not
        // tick. Player_ProcessInteractionState still publishes the idle
        // root at that boundary, so make the same post-response handoff
        // explicit before the VM service gate.
        animation_.select_locomotion_entry(
            kPlayerIdleStream,
            false,
            SpritePose::Idle);
    }
    // A direct grounded jump publishes its root before the common actor VM
    // pass. The resulting state boundary therefore exposes the first data
    // cursor (0x001221B2), rather than the untouched root, on the launch
    // frame. Action-selected jumps retain their existing post-pass ordering.
    const bool select_jump_before_vm =
        start_jump
        && (animation_.stream_kind() == AnimationStreamKind::Locomotion
            || (animation_.stream_kind() == AnimationStreamKind::Action
                && animation_.stream_entry() == kPlayerRunStream
                && player_.terrain_response_timer_state != 0));
    if (select_jump_before_vm) {
        if (terrain_bounce_root_jump_start
            || bounce_response_landing_root_jump_start) {
            // The jump handler starts from PLAYER_ANIM_TRANSITION_JUMP when
            // FFF0D0 is still set. The common VM then exposes its first data
            // cursor at 0x001220BC on this same frame.
            animation_.select_locomotion_entry(
                kPlayerTransitionJumpStream,
                false,
                SpritePose::Jump);
        } else {
            animation_.select_locomotion_stream(SpritePose::Jump, vm_context);
        }
    }
    // The grounded Up branch at 0x001AA0AE runs before the common VM. It
    // publishes the action root and FFF0DF, then the next animation tick
    // consumes that root.
    const bool can_select_up_animation =
        input.up
        && !input.left
        && !input.right
        && player_.grounded
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && player_.terrain_response_timer_state == 0
        && player_.terrain_transition_gate == 0
        && player_.animation_selector.transition_state_df == 0
        && camera_.special_mode == 0;
    const bool select_up_before_vm =
        can_select_up_animation && player_.terrain_vertical_stop != 0;
    if (select_up_before_vm) {
        animation_.select_stream_entry(kPlayerUpAnimationStream, true, true);
        player_.animation_selector.transition_state_df = 0xFF;
        player_.terrain_response_timer_state = 0;
    }
    const bool apple_terrain_response_followup =
        input.apple_held
        && animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == kPlayerAppleActionStream
        && player_.terrain_response_timer_state != 0;
    if ((input.apple_pressed || apple_terrain_response_followup)
        && (was_grounded
            || player_.terrain_response_active != 0
            || (player_.terrain_landing_state != 0 && player_.vy == 0))
        && state_.interaction_state.can_throw_apple()
        && animation_.rom_loaded()
        && player_.animation_selector.animation_gate == 0
        && player_.animation_selector.interaction_lock == 0
        && player_.animation_selector.transition_gate == 0
        && player_.animation_selector.response_animation == 0
        && player_.animation_selector.transition_lock == 0
        && player_.terrain_transition_countdown == 0
        && (apple_terrain_response_followup
            || player_.animation_selector.state_lock == 0)) {
        // FUN_001A9304 is the A-button selector. It does not always enter the
        // apple stream: its terrain/response priority is evaluated first, so
        // an apple press during FFF0CC=1 enters 0x001225A2 and its F5 command
        // creates the same projectile two frames later.
        std::uint32_t action_stream = 0;
        if (player_.animation_selector.transition_state_de != 0) {
            if (player_.terrain_landing_state != 0) {
                action_stream = 0x00122470;
            }
        } else if (player_.animation_selector.transition_gate != 0
                   || player_.animation_selector.transition_lock != 0) {
            action_stream = kPlayerAppleTerrainTransitionStream;
        } else if (player_.terrain_response_active != 0
                   || player_.terrain_landing_state == 0) {
            action_stream = 0x0012262A;
        } else if (player_.terrain_response_timer_state != 0) {
            action_stream = kPlayerAppleTerrainTimerStream;
        } else {
            action_stream = kPlayerAppleActionStream;
        }
        if (action_stream != 0) {
            animation_.select_stream_entry(action_stream);
            player_.animation_selector.state_lock = 0x0E;
            // The ROM's selector queues OBJECT THROW (0x03) at this same
            // boundary when SCENE_VDP_UPDATE_FLAG is active. The native
            // shortcut above bypasses that selector body, so preserve its
            // audio side effect explicitly.
            if (camera_.vdp_update != 0) {
                services_.append_sound_requests(
                    std::vector<std::uint8_t>{kPlayerAppleThrowSoundId}
                );
            }
        }
    }
    // Player_SelectActionAnimation (0x001A9502) runs before the common actor
    // animation traversal. A sword edge is represented by the controller
    // input here; the ROM's action selector then chooses the presentation
    // stream from the live terrain/interaction fields. In the captured route
    // FFF0CC is still armed, so the selected root is 0x001228AC and the same
    // traversal publishes 0x001228B4.
    const bool collision_selector_action_followup =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == 0x00122014
        && input.attack_held
        && player_.terrain_response_timer_state != 0;
    const bool deferred_landing_action =
        input.attack_held
        && !ground_response_landing
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerLandingAnimationStream;
    const bool terrain_action_stream_busy =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == kPlayerActionTerrainTimerStream
        && animation_.animation_pc() >= 0x001228CC;
    const bool sword_stream_busy =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == kPlayerSwordAnimationStream
        && animation_.animation_pc() >= kPlayerSwordAnimationStream
        && animation_.animation_pc() < kPlayerSwordAnimationEnd;
    const bool action_stream_busy =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() != kPlayerIdleStream;
    bool proximity_actor_terminal = false;
    for (const ActorState& actor : actors_) {
        if ((actor.type == 0x1F && actor.animation_pc >= 0x0012396E)
            || (actor.type == 0x84
                && actor.animation_pc == kActorDeathAnimationStream)) {
            proximity_actor_terminal = true;
            break;
        }
    }
    bool sword_action_rearm_ready = false;
    if (sword_action_pending && input.attack_held) {
        if (sword_action_pending_delay == 0) {
            if (!action_stream_busy) {
                sword_action_rearm_ready = true;
            } else {
                // Keep a queued edge alive until the current action really
                // leaves its stream. The ROM's selector revisits the held
                // button after the short idle handoff, rather than replaying
                // it immediately on the terminal sword cursor.
                runtime_.sword_action_pending = true;
            }
        } else {
            runtime_.sword_action_pending = true;
            runtime_.sword_action_pending_delay =
                static_cast<std::uint8_t>(sword_action_pending_delay - 1);
        }
    }
    if (action_stream_busy && input.attack_pressed) {
        // A new B edge arriving while a sword stream is still live is
        // consumed by the current action, but remains queued if the button is
        // still held when that action hands control back to locomotion. The
        // terminal terrain-action cursor additionally schedules its one-frame
        // idle completion.
        player_.attack_timer = 0;
        if (terrain_action_stream_busy) {
            runtime_.terrain_action_stream_exit_pending = true;
        } else if (!proximity_actor_terminal) {
            runtime_.sword_action_pending = true;
            runtime_.sword_action_pending_delay = 2;
        }
    }
    if (player_.animation_selector.interaction_pending != 0) {
        if (!input.attack_held && !input.attack_pressed) {
            // Terrain_QueryCallbackB clears the action latch when the sword
            // button is released. The animation cursor remains on the idle
            // stream, but the combat window and FFEFFF end immediately.
            player_.attack_timer = 0;
            player_.animation_selector.interaction_pending = 0;
        } else if (player_.attack_timer == 0) {
            // The ROM keeps the terminal sword cursor's interaction window
            // alive for one extra boundary while B remains held. This is
            // visible on the final sword record before the next action edge.
            if (input.attack_held && sword_stream_busy) {
                player_.attack_timer = 1;
            } else {
                player_.animation_selector.interaction_pending = 0;
            }
        } else if (player_.animation_selector.interaction_pending != 1) {
            --player_.animation_selector.interaction_pending;
        }
    } else if (((input.attack_pressed
                   && !sword_action_pending
                   && !ground_response_landing
                   && !action_stream_busy)
               || sword_action_rearm_ready
               || (input.attack_held
                   && (player_.terrain_response_timer_state != 0
                       || terrain_action_hold_rearm_pending)
                   && animation_.stream_kind() == AnimationStreamKind::Locomotion
                   && animation_.animation_pc() != kPlayerRunStream)
               || collision_selector_action_followup
               || deferred_landing_action)
               && player_.animation_selector.animation_gate == 0
               && player_.terrain_terminal_transition == 0
               && animation_.frame_pointer() != 0
               && player_.animation_selector.response_animation == 0
               && player_.animation_selector.transition_state == 0
               && player_.animation_selector.scene_script_countdown == 0
               && !collision_effects.player_idle_animation_reset
               && (player_.animation_selector.transition_lock == 0
                   || player_.terrain_push_right == 0)
               && (player_.terrain_transition_gate == 0
                   || player_.terrain_push_up == 0)) {
        std::uint32_t action_stream = 0;
        if (camera_.special_mode != 0) {
            action_stream = kPlayerActionSpecialCameraStream;
        } else if (player_.animation_selector.transition_lock != 0
                   || player_.terrain_transition_gate != 0) {
            action_stream = kPlayerActionTransitionStream;
        } else if (player_.terrain_response_timer_state != 0) {
            action_stream = player_.terrain_landing_state != 0
                ? kPlayerActionTerrainTimerStream
                : kPlayerActionAirborneStream;
        } else if (player_.terrain_landing_state != 0) {
            action_stream = player_.animation_selector.transition_state_de != 0
                ? kPlayerActionPushDownStream
                : player_.terrain_push_up == 0
                    ? kPlayerSwordAnimationStream
                    : kPlayerActionPushUpStream;
        } else {
            action_stream = kPlayerActionAirborneStream;
        }
        animation_.clear_landing_idle_hold();
        animation_.select_stream_entry(action_stream);
        player_.animation_selector.interaction_pending = 10;
        if (input.attack_pressed) {
            // Airborne action selection reaches this block after the normal
            // grounded input pre-pass, so arm the ROM's ten-frame action
            // timer here as well.
            player_.attack_timer = 10;
        }
        runtime_.terrain_action_selected_from_attack_edge =
            input.attack_pressed
            && action_stream == kPlayerActionTerrainTimerStream;
        if (collision_selector_action_followup) {
            player_.attack_timer = 10;
        }
        if (deferred_landing_action) {
            player_.attack_timer = 10;
        }
    if (input.attack_held
            && !input.attack_pressed
            && (player_.terrain_response_timer_state != 0
                || terrain_action_hold_rearm_pending
                || sword_action_rearm_ready)) {
            player_.attack_timer = 10;
        }
    }
    // A stationary held-B boundary returns the terrain action root to idle;
    // when the player is still walking, the ROM keeps servicing the action
    // stream. The distinction is visible in the corpus at frames 136 and
    // 756 respectively.
    if (!input.attack_pressed
        && !input.left
        && !input.right
        && !collision_selector_action_followup
        && (terrain_action_selected_from_attack_edge
            || (input.attack_held
                && animation_.stream_kind() == AnimationStreamKind::Action
                && animation_.stream_entry() == kPlayerActionTerrainTimerStream))
        && player_.attack_timer != 0
        && player_.animation_selector.interaction_pending != 0
        && animation_.stream_entry() == kPlayerActionTerrainTimerStream) {
        animation_.select_stream_entry(kPlayerIdleStream);
    }
    // Actor_ActorCollisionPass follows the player selectors in the ROM. Its
    // sword terminal edge is handled inside this one source/target scan;
    // there is deliberately no pre-motion companion call.
    services_.record_scheduler_phase("actor_collision", 0x001ABD7E);
    collisions_.actor_actor(state_);
    // Interaction_UpdateTarget runs immediately after the actor collision
    // pass and converges the live Genesis selector on even frame phases.
    interactions_.update_target(state_);
    services_.record_scheduler_phase("level_exit_transition", 0x001A8F0C);
    const bool level_exit_callback = scene_.service_level_exit(
        services_.player_world_y(),
        camera_.level_height,
        player_.terrain_terminal_transition,
        player_.animation_selector.interaction_lock
    );
    if (level_exit_callback
        && !runtime_.level_event_exit_started
        && !level_events_.active()
        && !rom_bytes_.empty()) {
        const std::uint32_t exit_callback = level_.descriptor().exit_function.value;
        std::uint32_t stream = 0;
        if (exit_callback == kLevel02ExitCallback) {
            stream = kLevel02EventStream;
        } else if (exit_callback == kLevel06ExitCallback) {
            stream = kLevel06EventStream;
        }
        if (stream != 0 && stream < rom_bytes_.size()) {
            level_events_.start(RomAddress{stream});
            runtime_.level_event_exit_started = true;
        }
    }
    // 0x001A8F04 is the recovered level callback boundary. Level 02 and 06
    // install their timed stream from the exit callback above, then dispatch
    // one record tick here on the same subsequent callback cadence.
    services_.record_scheduler_phase("empty_return", 0x001A8F04);
    services_.update_level_events();
    // FUN_001B01AC is the late interaction resource service. Its actor refill
    // edge is visible at the same game-loop boundary on either phase of
    // FRAME_PHASE_COUNTER; only the common actor animation table walk is
    // phase-gated.
    services_.record_scheduler_phase("interaction_counter", 0x001B00CA);
    services_.record_scheduler_phase("interaction_resource", 0x001B01AC);
    if (!stable_terrain_handler_fixture) {
    interactions_.scan_refill_window(state_, level_, stable_terrain_handler_fixture);
    }
    interactions_.flush_surface_actor_spawn(state_);
    services_.record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    services_.publish_player_world_coordinates();
    // The camera tile-update boundary does not suppress the common player VM
    // pass: the ROM services animation after the horizontal follow even when
    // the vertical reference tile is rebased. Keep the separate catch-up
    // marker only for the post-follow downward rebase path above.
    services_.record_scheduler_phase("scene_advance", 0x001A8E3E);
    services_.update_scene_resources();
    services_.record_scheduler_phase("animation_vm", 0x001AC784);
    const std::uint32_t animation_pc_before_common_vm =
        animation_.animation_pc();
    const bool action_stream_was_live_before_common_vm =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() != kPlayerIdleStream;
    if (!stable_terrain_handler_fixture) {
        if (transition_root_up_cursor) {
            // The root's F4/F8 sequence publishes this cursor on the same
            // phase that consumes the held-Up retry. A one-tick timer keeps
            // the cursor visible at the game-loop boundary.
            animation_.set_animation_state(
                kPlayerBounceResponseLandingCursor,
                1);
        }
        if (terrain_action_stream_exit_pending) {
            animation_.select_locomotion_entry(
                kPlayerIdleStream,
                false,
                SpritePose::Idle);
            desired_pose = SpritePose::Idle;
            runtime_.terrain_action_hold_rearm_pending = true;
        }
        services_.update_animation_vm_ordinal_30(
            desired_pose,
            horizontal_direction(input),
            vm_context,
            false,
            bounce_response_finished
        );
        if (!input.attack_pressed
            && animation_.stream_kind() == AnimationStreamKind::Locomotion
            && (animation_.stream_entry() == kPlayerIdleStream
                || animation_.stream_entry() == kPlayerRunStream)) {
            // The common ROM animation pass clears FFF0D8 when a sword
            // stream returns through F8 to ordinary locomotion. Native VM
            // command data can re-arm it while consuming the final sword
            // cursor, so publish the post-handoff clear at this boundary.
            player_.animation_selector.action_response_field = 0;
        }
        if (animation_.rom_loaded()) {
            camera_.vertical_threshold = animation_.camera_vertical_threshold();
            std::uint8_t value = 0;
            if (animation_.take_memory_write(0xFFF0C0, value)) {
                player_.terrain_vertical_stop = value;
            }
            if (animation_.take_memory_write(0xFFF0C1, value)) {
                player_.terrain_landing_state = value;
                player_.animation_selector.landing_state = value;
            }
            if (animation_.take_memory_write(0xFFF101, value)) {
                // Some run-stream ED commands re-arm the shared interaction
                // selector after the bounce handoff.  Propagate that tracked
                // ROM write back to the engine-owned selector so the next
                // command boundary sees the same FFF101 latch.
                player_.animation_selector.response_state_101 = value;
            }
        }
        const bool grounded_response_idle_exit =
            ground_response_was_active
            && player_.terrain_response_timer_state == 0
            && !input.attack_pressed
            && !input.attack_held
            && !input.left
            && !input.right
            && player_.animation_selector.interaction_lock != 0
            && animation_.stream_kind() == AnimationStreamKind::Action
            && animation_.stream_entry() == 0x00122014;
        if (direct_bounce_landing_event) {
            // This bounce publishes the compact landing stream after the
            // common VM pass. Its root remains visible for the even boundary;
            // the next odd pass consumes the first frame.
            animation_.select_locomotion_entry(
                kPlayerBounceLandingStream,
                false,
                SpritePose::Idle);
            contour_ground_motion_ = true;
        }
        if (bounce_response_landing_event) {
            animation_.select_locomotion_entry(
                kPlayerBounceResponseLandingStream,
                false,
                SpritePose::Idle,
                true);
        }
        if (transition_root_release_response) {
            // Releasing Up advances the special root to its cursor, where it
            // remains visible during the subsequent camera-only settle.
            animation_.set_animation_state(
                kPlayerBounceResponseLandingCursor,
                0);
        }
        if (generic_contour_landing_event
            && player_.terrain_bounce_animation_state >= 0x2D) {
            // Non-flat landings keep the internal contour-ground predicate
            // live while the landing stream finishes, allowing the next
            // direction edge to enter the run response on the same boundary
            // as Genesis.
            contour_ground_motion_ = true;
        }
        if (((action_response_exit_candidate
              && animation_.animation_pc() == kPlayerIdleStream)
             || grounded_response_idle_exit)
            && player_.terrain_response_timer_state == 0) {
            // F4 branches to the idle root, then the common animation pass
            // exposes the first idle cursor at 0x121DA8.
            player_.terrain_horizontal_response = 0;
            player_.animation_selector.horizontal_response = 0;
            if (grounded_response_idle_exit) {
                // This path has already returned through grounded response
                // cleanup. Publish a locomotion-idle stream so the next held
                // direction can select Run instead of remaining in Action.
                animation_.select_locomotion_entry(
                    kPlayerIdleStream,
                    false,
                    SpritePose::Idle);
            } else {
                animation_.select_stream_entry(kPlayerIdleStream, true);
            }
            animation_.set_animation_state(0x00121DA8, 0);
        }
        interactions_.apply_player_collision_selector(state_, stable_terrain_handler_fixture);

        if (can_select_up_animation && !select_up_before_vm) {
            // The ordinary grounded Up path publishes its action root after
            // the current VM pass. Keep this ordering for a fresh ground
            // state; the vertical-stop path above is the pre-pass variant.
            animation_.select_stream_entry(kPlayerUpAnimationStream);
            player_.animation_selector.transition_state_df = 0xFF;
            player_.terrain_response_timer_state = 0;
        }

        if (start_jump && !select_jump_before_vm) {
            // Player_HandleJumpAndVerticalState publishes the jump root after
            // the common VM pass. This remains a locomotion stream even when
            // the preceding stream was a terrain-selected action stream.
            animation_.select_locomotion_stream(SpritePose::Jump, vm_context);
        }
    }
    const bool sword_ground_response_handoff =
        sword_action_was_active
        && current_animation == 0x00122722
        && ground_response_started
        && input.attack_held
        && player_.attack_timer == 0
        && player_.animation_selector.action_response_field != 0;
    if (sword_ground_response_handoff
        || action_ground_response_start
        || contour_ground_response_start
        || grounded_landing_direction_start
        || completed_jump_landing_direction_start) {
        // The proximity collision tail publishes the grounded response
        // cursor after the sword VM has completed its final frame. It keeps
        // the newly armed horizontal response visible for the next action
        // selector pass; this is the 0x122014 boundary in Genesis.
        animation_.select_stream_entry(0x00122014, true);
        player_.terrain_horizontal_response = 3;
        if (completed_jump_landing_direction_start) {
            player_.terrain_response_timer_state = 0xFF;
            player_.animation_selector.response_timer = 0xFF;
            player_.terrain_transition_gate = 0;
            player_.animation_selector.transition_gate = 0;
        } else if (!contour_ground_response_start) {
            player_.terrain_response_timer_state = 1;
        }
        player_.animation_selector.horizontal_response = 3;
        if (!completed_jump_landing_direction_start
            && !contour_ground_response_start) {
            player_.animation_selector.response_timer = 1;
        }
        player_.animation_selector.action_response_field = 0;
        if (contour_ground_response_start) {
            camera_.horizontal_threshold = input.left ? 0xF0 : 0x70;
            contour_ground_motion_ = true;
        }
    }
    if (completed_jump_landing_direction_start) {
        runtime_.completed_jump_ground_response_latch = true;
        runtime_.completed_jump_ground_response_run_pending = true;
    }
    if (completed_jump_ground_response_latch
        && animation_pc_at_frame_start == 0x0012215A
        && (frame_phase_ & 1U) == 0) {
        runtime_.completed_jump_ground_response_latch = false;
    }
    // Player_Update's horizontal wall branch enters the extended response
    // stream after the common VM tick.  The branch is reached when a held
    // direction first meets the terminal terrain stop; it clears FFF0B0 and
    // FFF0CC, marks FFF0ED, and publishes 0x00121FA6 without consuming its
    // first frame.  Keep this post-pass ordering so the boundary retains the
    // previous locomotion frame pointer just like Genesis.
    const bool wall_response_allowed = animation_.rom_loaded()
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && player_.animation_selector.animation_gate == 0
        && player_.animation_selector.terminal_transition == 0
        && player_.animation_selector.interaction_lock == 0
        && player_.terrain_response_active == 0
        && (blocked_right_wall_response || blocked_left_wall_response);
    if (wall_response_allowed) {
        animation_.select_response_stream(0x00121FA6);
        player_.animation_selector.response_animation = 0xFF;
        player_.animation_selector.response_state_101 = 0;
        player_.animation_selector.horizontal_response = 0;
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        player_.animation_selector.response_timer = 0;
        player_.animation_selector.transition_state_de = 0;
        player_.animation_selector.transition_state_df = 0;
        player_.animation_selector.response_latch = 0;
    }
    // Type 0x7B uses the ROM's 0x001AE9D4 player-collision handler.  The
    // player/actor rectangles are evaluated after the common animation tick;
    // this is why the interaction first becomes visible when the response
    // stream publishes frame 0x001EA062, even though the same actor was
    // already present at the earlier wall-response boundary.
    interactions_.process_surface_actor_collision(
        state_,
        services_.player_animation_context(player_.grounded),
        stable_terrain_handler_fixture,
        contour_ground_motion);
    // Player_ProcessInteractionState at 0x001AE4F8 is a RAM-driven stream
    // selector outside the common actor VM. Build its post-physics RAM view
    // after the actor tick so the native path owns the same boundary as the
    // ROM's interaction caller.
    interactions_.update_player_selector(
        state_,
        animation_context,
        stable_terrain_handler_fixture,
        landing_event,
        desired_pose,
        contour_ground_motion,
        interaction_selector_pending_at_start);
    if (contour_ground_motion_
        && player_.terrain_bounce_animation_state >= 0x2D
        && player_.terrain_landing_state == 9
        && player_.terrain_jump_response_counter == 0
        && player_.vy == 0
        && player_.animation_selector.interaction_lock == 0
        && animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == 0x00122014) {
        // The late contour landing keeps the promoted ground-response latch
        // while the action cursor advances. Selector cleanup may briefly
        // clear FFF0CC on this path, but Genesis republishes the 0xFF latch
        // until the next jump edge consumes it.
        player_.terrain_response_timer_state = 0xFF;
        player_.animation_selector.response_timer = 0xFF;
    }
    if (animation_.stream_entry() == kPlayerAppleTerrainTimerStream
        && animation_pc_before_common_vm == 0x00122622
        && animation_.animation_pc() == 0x00122622
        && player_.animation_selector.state_lock == 0) {
        // The terminal F8 in the terrain-timer action publishes the idle root
        // on the even cleanup boundary and clears the response latch. The
        // next odd VM pass then consumes that root, matching MAME's
        // 0x00121D9A -> 0x00121DA8 handoff.
        player_.terrain_response_timer_state = 0;
        player_.animation_selector.response_timer = 0;
        animation_.select_locomotion_entry(
            kPlayerIdleStream,
            false,
            SpritePose::Idle);
        last_ground_direction_ = 0;
    }
    // Non-combat F5 streams publish their request on the current animation
    // tick and expect the auxiliary actor to be visible in that frame's
    // state record. The live sword action is intentionally deferred to the
    // next frame boundary above while its attack timer is active.
    if (player_.attack_timer == 0
        && animation_.stream_entry() != kPlayerSwordAnimationStream) {
        // Flush the one player-side command whose semantic effect was
        // intentionally held until this post-selector boundary. No actor VM
        // is invoked here.
        services_.flush_deferred_animation_spawn();
    }
        if (terrain_response_was_active
            && player_.terrain_response_active == 0) {
        // The common animation VM may be the final writer that clears the
        // active-response latch. Preserve the one-frame horizontal carry at
        // the completed game-loop boundary in that case as well.
        runtime_.terrain_response_horizontal_carry = true;
    }
    if (action_stream_was_live_before_common_vm
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && animation_.stream_entry() == kPlayerIdleStream) {
        // A button edge arriving on the same boundary as an action's F8
        // return is queued by the ROM and accepted after two idle-service
        // phases. Reuse the existing action-pending latch so the next held-B
        // edge follows the same delayed path as a queued sword action.
        runtime_.sword_action_pending = true;
        runtime_.sword_action_pending_delay = 2;
    }
    }
    if (transition_frame) {
        services_.record_scheduler_phase("scene_advance", 0x001A8E3E);
        services_.update_scene_resources();
        // Scene_EnterTransitionMode owns the transition movement, but the
        // frame loop still reaches its common ordinal-30 animation service.
        // Keeping that service here removes the native early-return path.
        services_.record_scheduler_phase("animation_vm", 0x001AC784);
        services_.update_animation_vm_ordinal_30(
            SpritePose::Idle,
            horizontal_direction(input),
            services_.player_animation_context(player_.grounded),
            false,
            false
        );
    }
    services_.record_scheduler_phase("transition_completion", 0x001AE0F6);
    (void) scene_.transition_completion_ready();
    // Camera scroll publication is presentation-owned in the native build;
    // retaining the ROM boundary in the trace makes that classification
    // explicit without inventing a second camera gameplay pass.
    services_.record_scheduler_phase("camera_scroll_publish", 0x001AAA2A);
    services_.record_scheduler_phase("scene_completion", 0x001B315C);
    (void) scene_.complete_script_to_state1();
    // State serialization needs the final player record after ordinal 30.
    // This mirror is not a gameplay scheduler phase.
    services_.sync_player_actor();
    services_.apply_actor_timeline(frame_ + 1);
    checkpoint_animation_selector_pending_ = false;
    services_.record_scheduler_phase("state_boundary", 0);
    services_.collect_scheduler_writer_pcs();
    ++frame_;
}

}  // namespace openaladdin
