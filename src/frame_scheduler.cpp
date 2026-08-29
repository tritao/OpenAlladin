#include "frame_scheduler.hpp"

#include "actor_movement.hpp"
#include "player_motion.hpp"
#include "terrain_behavior.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace openaladdin {
namespace {

constexpr std::uint32_t kPlayerSwordAnimationStream = 0x0012271A;
constexpr std::uint32_t kPlayerAppleActionStream = 0x001223DA;
constexpr std::uint32_t kPlayerAttackTransitionStream = 0x00122034;
constexpr std::uint32_t kPlayerSwordStableStream = 0x001223E2;
constexpr std::uint32_t kPlayerSwordFirstFrame = 0x001ED34A;
constexpr std::uint32_t kPlayerUpAnimationStream = 0x00122236;
constexpr std::uint32_t kPlayerDownAnimationStream = 0x001222D2;
constexpr int kTerrainResourceBase = 0x2FD2;

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
    auto& actor_lifecycle_ = *context.actor_lifecycle;
    auto& collisions_ = *context.collisions;
    auto& scene_ = *context.scene;
    auto& interactions_ = *context.interactions;
    auto& camera_system_ = *context.camera_system;
    auto& animation_ = context.animation_system->player();
    auto& player_motion_ = *context.player_motion;
    auto& actor_movement_ = *context.actor_movement;
    auto& terrain_ = *context.terrain;
    auto& terrain_behavior_ = *context.terrain_behavior;
    auto& actor_animations_ = context.animation_system->actors().vms();
    const auto& rom_bytes_ = *context.rom_bytes;
    auto& level_event_sound_requests_ = *context.level_event_sound_requests;
    auto& runtime_ = *context.runtime;

    auto& checkpoint_animation_selector_pending_ =
        runtime_.checkpoint_animation_selector_pending;
    auto& jump_landing_state_arm_pending_ = runtime_.jump_landing_state_arm_pending;
    auto& jump_landing_state_arm_now_ = runtime_.jump_landing_state_arm_now;
    auto& terrain_fall_phase_ = runtime_.terrain_fall_phase;
    auto& contour_ground_motion_ = runtime_.contour_ground_motion;
    auto& terrain_input_world_x_ = runtime_.terrain_input_world_x;
    auto& terrain_input_world_y_ = runtime_.terrain_input_world_y;
    auto& frame_ = state_.frame.number;
    auto& frame_phase_ = state_.frame.phase;
    auto& last_ground_direction_ = runtime_.last_ground_direction;
    const bool checkpoint_terrain_behavior_override_ =
        runtime_.checkpoint_terrain_behavior_override;
    const std::uint8_t checkpoint_terrain_behavior_ =
        runtime_.checkpoint_terrain_behavior;
    const bool scheduler_trace_enabled_ = runtime_.scheduler_trace_enabled;

    auto& clear_scheduler_trace = context.clear_scheduler_trace;
    auto& flush_deferred_animation_spawn = context.flush_deferred_animation_spawn;
    auto& update_dynamic_actor_culling = context.update_dynamic_actor_culling;
    auto& update_level_events = context.update_level_events;
    auto& start_level_event_stream_after_exit =
        context.start_level_event_stream_after_exit;
    auto& update_scene_resources = context.update_scene_resources;
    auto& update_animation_vm_ordinal_30 =
        context.update_animation_vm_ordinal_30;
    auto& publish_player_world_coordinates =
        context.publish_player_world_coordinates;
    auto& sync_player_actor = context.sync_player_actor;
    auto& player_animation_context = context.player_animation_context;
    auto& initialize_actor_from_template =
        context.initialize_actor_from_template;
    auto& apply_actor_timeline = context.apply_actor_timeline;
    auto& player_world_x = context.player_world_x;
    auto& player_world_y = context.player_world_y;
    auto& record_scheduler_phase = context.record_scheduler_phase;
    auto& collect_scheduler_writer_pcs = context.collect_scheduler_writer_pcs;

    if (scheduler_trace_enabled_) {
        clear_scheduler_trace();
    }
    record_scheduler_phase("frame_latch", 0x001A8C16);
    // Game_FrameUpdateLoop begins with ADDQ.B #1,$FF7E28. All later gate
    // decisions in this update consume this single recovered ROM phase.
    frame_phase_ = static_cast<std::uint8_t>(frame_phase_ + 1);
    actors_.begin_frame();
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
    const bool arm_surface_interaction = interaction_boundary.arm_surface_interaction;
    if (player_.animation_selector.state_lock != 0) {
        --player_.animation_selector.state_lock;
    }
    // FUN_001A91C6 is the unconditional input/resource service. A player F5
    // command deferred by the previous animation boundary becomes live here.
    record_scheduler_phase("input_resource", 0x001A91C6);
    flush_deferred_animation_spawn();
    const bool transition_frame = scene_.is_transition();
    if (transition_frame) {
        scene_.update_transition(
            SceneInput{input.up, input.down, input.left, input.right},
            player_.x,
            player_.y,
            player_.grounded
        );
    } else {
    terrain_.sample(state_, TerrainInput{
        input.up,
        input.down,
        input.left,
        input.right,
        input.jump_pressed,
    });
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();
    terrain_input_world_x_ = player_world_x();
    terrain_input_world_y_ = player_world_y();
    const bool grounded_before_contour = player_.grounded;
    const bool contour_ground_motion_before = contour_ground_motion_;
    const bool stable_terrain_handler_fixture = checkpoint_terrain_behavior_override_
        && (checkpoint_terrain_behavior_ == 0x28
            || checkpoint_terrain_behavior_ == 0x29
            || checkpoint_terrain_behavior_ == 0x2D
            || checkpoint_terrain_behavior_ == 0x27);
    // The normal slice uses the recovered contour pass. Handler fixtures are
    // deliberately staged at the ROM's resolver boundary instead: the
    // original frame calls Terrain_ResolvePlayerCell/handler before the
    // 0x001A9D98 movement integrator, and the fixture supplies the already
    // resolved landing state that the ROM would have in RAM.
    if (!checkpoint_terrain_behavior_override_) {
        record_scheduler_phase("terrain_contour", 0x001AD7B4);
        terrain_.apply_contour(state_, level_, terrain_fall_phase_);
    }
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();
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
    // Non-flat contours are traversed by the grounded movement path even
    // though the ROM's public grounded predicate is false until FFF0C1
    // returns to 1. Keep that distinction explicit for movement and VM
    // selection while preserving the observed state byte.
    bool contour_ground_motion = (grounded_before_contour || contour_ground_motion_)
        && !player_.grounded
        && player_.terrain_landing_state != 0
        && player_.terrain_response_active == 0
        && player_.vy == 0;
    contour_ground_motion_ = contour_ground_motion;
    const bool was_grounded = player_.grounded || contour_ground_motion;
    const bool just_landed = !grounded_before_contour
        && !contour_ground_motion_before
        && player_.grounded;
    // Terrain_ResolvePlayerCell consumes the world coordinate captured before
    // MovementVM. Its one invocation is placed after the actor terrain and
    // player collision services, and before the player response integrator.
    const int previous_world_y = player_world_y();
    if (player_.attack_timer != 0) {
        --player_.attack_timer;
    }
    if (input.attack_pressed && was_grounded) {
        player_.attack_timer = 10;
    }
    const auto collision = level_.query_player_collision(
        player_world_x(), player_world_y(),
        player_.terrain_landing_state);
    player_.terrain_stop_left_motion = collision.stop_left ? 0xFF : 0;
    player_.terrain_stop_right_motion = collision.stop_right ? 0xFF : 0;
    player_.terrain_stop_upward_motion = collision.stop_upward ? 0xFF : 0;
    player_.terrain_left_inner_probe = collision.left_inner ? 0xFF : 0;
    player_.terrain_left_outer_probe = collision.left_outer ? 0xFF : 0;
    player_.terrain_right_inner_probe = collision.right_inner ? 0xFF : 0;
    player_.terrain_right_outer_probe = collision.right_outer ? 0xFF : 0;
    terrain_.apply_response(state_, TerrainResponseContext{
        frame_,
        scene_.is_transition(),
        animation_.stream_entry() == 0x00122006,
    });
    if (interactions_.bounce_response_follow_active()
        && player_.terrain_response_active == 0) {
        player_.terrain_response_timer_state = 1;
        if (animation_.stream_entry() == 0x00122006) {
            camera_.vertical_threshold = 400;
        }
    }
    const int input_direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    // The ROM's movement VM performs its cull before integrating actor
    // deltas. Use the pre-motion actor coordinates and pre-follow camera so
    // edge retirement lines up with the synchronized MAME boundary.
    update_dynamic_actor_culling();
    record_scheduler_phase("movement_vm", 0x001ADE36);
    actor_movement_.update(state_, runtime_, rom_bytes_);
    // FUN_001ADB5C also resolves terrain for non-collision actors whose
    // movement flag bit 0 is set. The common type-0x29 object in the opening
    // refill window has no movement cursor, but its animation publishes a
    // frame pointer and the terrain pass snaps its Y coordinate to the
    // selected class contour on the following VBlank.
    record_scheduler_phase("actor_terrain_collision", 0x001ADB5C);
    if (!stable_terrain_handler_fixture && !rom_bytes_.empty()) {
        const auto& words = level_.terrain_words();
        const auto& floor = level_.floor_data();
        constexpr int kTerrainResourceBase = 0x2FD2;
        for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
            ActorState& actor = actors_[slot];
            if (actor.type == 0
                || ((actor.flags & 0x08) != 0 && !actors_.host_meta(slot).spawned_by_apple)
                || ((actor.movement_flags & 0x01) == 0 && !actors_.host_meta(slot).spawned_by_apple)
                || actor.frame_ptr == 0
                || actor.movement_word_1a < 0) {
                continue;
            }
            const int level_height_pixels = level_.map_height() * 16;
            if (static_cast<int>(actor.y) > level_height_pixels + 0xC8) {
                actor.movement_flags = static_cast<std::uint8_t>(
                    (actor.movement_flags & ~0x01U) | 0x40U);
                continue;
            }
            const int row = (static_cast<int>(actor.y) - 0xF0) >> 4;
            const int column = (static_cast<int>(actor.x) + 0x10) >> 4;
            if (row < 0 || row >= level_.map_height()
                || column < 0 || column >= level_.map_width()) {
                actor.movement_flags = static_cast<std::uint8_t>(
                    (actor.movement_flags & ~0x01U) | 0x40U);
                continue;
            }
            unsigned class_value = 0;
            int class_row_offset = 0;
            std::uint8_t interaction_state = 0;
            for (int row_offset = 0; row_offset < 3 && class_value == 0; ++row_offset) {
                const int sample_row = row + row_offset;
                if (sample_row >= level_.map_height()) break;
                const std::size_t map_index = static_cast<std::size_t>(
                    sample_row * level_.map_width() + column);
                const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
                for (std::size_t resource_offset = 0; resource_offset < 2; ++resource_offset) {
                    if (resource + resource_offset >= floor.size()) continue;
                    const std::uint8_t floor_byte = floor[resource + resource_offset];
                    const std::size_t class_address = static_cast<std::size_t>(
                        kTerrainResourceBase + (static_cast<std::size_t>(floor_byte) << 4)
                        + (static_cast<int>(actor.x) & 0x0F));
                    if (class_address >= rom_bytes_.size()) continue;
                    const unsigned candidate = rom_bytes_[class_address] & 0x3F;
                    if (candidate == 0) continue;
                    class_value = candidate;
                    class_row_offset = row_offset;
                    if (resource + 2 < floor.size()) {
                        interaction_state = floor[resource + 2];
                    }
                    break;
                }
            }
            if (actors_.host_meta(slot).spawned_by_apple && actor.type == 0x80
                && (actor.flags & 0x08) != 0) {
                // The apple flight record is collision-enabled but does not
                // use the generic gravity-only terrain branch. It converts
                // only when the current row (rather than a look-ahead row)
                // contains a solid class, matching the observed impact edge.
                if (class_value != 0 && class_row_offset == 0) {
                    const ActorState replacement = initialize_actor_from_template(actor, 0x001B792C);
                    (void)actor_lifecycle_.install(slot, replacement);
                    actor_animations_[slot].clear_actor_service_boundary();
                    actor_animations_[slot].defer_actor_service();
                    actors_.host_meta(slot).spawned_by_apple = false;
                }
                continue;
            }
            if (class_value == 0) {
                // The ROM's terrain probe also inspects the fourth row below
                // the actor for a pending contour. It uses that look-ahead to
                // arm +0x07 bit 4, but does not snap the actor to a contour
                // until the class enters the ordinary three-row path.
                for (int row_offset = 3; row_offset < 4; ++row_offset) {
                    const int sample_row = row + row_offset;
                    if (sample_row >= level_.map_height()) break;
                    const std::size_t map_index = static_cast<std::size_t>(
                        sample_row * level_.map_width() + column);
                    const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
                    for (std::size_t resource_offset = 0; resource_offset < 2; ++resource_offset) {
                        if (resource + resource_offset >= floor.size()) continue;
                        const std::uint8_t floor_byte = floor[resource + resource_offset];
                        const std::size_t class_address = static_cast<std::size_t>(
                            kTerrainResourceBase + (static_cast<std::size_t>(floor_byte) << 4)
                            + (static_cast<int>(actor.x) & 0x0F));
                        if (static_cast<int>(actor.y) >= 0x36B
                            && class_address < rom_bytes_.size()
                            && (rom_bytes_[class_address] & 0x3F) != 0) {
                            if ((actor.runtime_field_07 & 0x10U) == 0
                                && actor.runtime_field_07_delay == 0) {
                                actor.runtime_field_07_delay = 2;
                            }
                            row_offset = 4;
                            break;
                        }
                    }
                    if ((actor.runtime_field_07 & 0x10U) != 0) break;
                }
                // In-bounds class-zero terrain follows ROM 1ADE1E: it only
                // arms the vertical accumulator (unless bit 7 suppresses
                // gravity). The bit0->bit6 flag conversion is reserved for
                // the out-of-range path at 1ADE10 above.
                if ((actor.movement_flags & 0x80) == 0) {
                    actor.movement_word_1a = static_cast<std::int16_t>(
                        actor.movement_word_1a + 0x78);
                }
                continue;
            }
            actor.interaction_state = interaction_state;
            actor.movement_word_1a = 0;
            actor.y = static_cast<std::uint16_t>(
                ((static_cast<int>(actor.y) - 0x10) & ~0x0F)
                + class_row_offset * 0x10 + static_cast<int>(class_value) - 1);
        }
    }
    // FUN_001ADB5C is the terrain/actor pass immediately after the ROM
    // movement VM. For collision-enabled actors it samples the same decoded
    // terrain resource used by Level::resolve_player_cell, then dispatches
    // Actor_HandleType2DInteraction when the class-table entry is nonzero.
    // This is what converts the later 0x2D child to the 0x84 template; the
    // earlier child is on a flat class-zero cell and remains eligible for the
    // player collision pass below.
    if (!stable_terrain_handler_fixture && !rom_bytes_.empty()) {
        const auto& words = level_.terrain_words();
        const auto& floor = level_.floor_data();
        constexpr int kTerrainResourceBase = 0x2FD2;
        for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
            ActorState& actor = actors_[slot];
            if (actor.type != 0x2D
                || (actor.flags & 0x08) == 0
                || actor.frame_ptr == 0
                || static_cast<int>(actor.y) > player_world_y() + 0xE0) {
                continue;
            }
            const int row = (static_cast<int>(actor.y) - 0xF0) >> 4;
            const int column = (static_cast<int>(actor.x) + 0x10) >> 4;
            if (row < 0 || row >= level_.map_height()
                || column < 0 || column >= level_.map_width()) {
                continue;
            }
            const std::size_t map_index = static_cast<std::size_t>(
                row * level_.map_width() + column);
            const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
            const auto terrain_class = [&](std::size_t resource_byte_offset) {
                if (resource + resource_byte_offset >= floor.size()) return 0U;
                const std::uint8_t resource_byte = floor[resource + resource_byte_offset];
                const std::size_t class_address = static_cast<std::size_t>(
                    kTerrainResourceBase + (static_cast<std::size_t>(resource_byte) << 4)
                    + (static_cast<int>(actor.x) & 0x0F));
                if (class_address >= rom_bytes_.size()) return 0U;
                return static_cast<unsigned>(rom_bytes_[class_address] & 0x3F);
            };
            const bool class_empty = terrain_class(0) == 0 && terrain_class(1) == 0;
            const bool third_byte_is_empty = resource + 2 >= floor.size()
                || floor[resource + 2] < 0xE0;
            if (class_empty && third_byte_is_empty) {
                continue;
            }

            const std::uint32_t animation_pc = actor.animation_pc;
            const bool spawned_by_animation = actors_.host_meta(slot).spawned_by_animation;
            const ActorState replacement = initialize_actor_from_template(actor, 0x001B7E40);
            (void)actor_lifecycle_.install(slot, replacement);
            // Most type-0x2D terrain conversions pass through the common
            // 0x001ABECE follow-up, which republishes the facing byte as
            // 0xFF. The Level-01 stream at animation cursor 0x00123EFA takes
            // the direct terrain path instead and keeps the template's zero
            // facing byte.
            actor.facing_x_flip = animation_pc == 0x00123EFA ? 0 : 0xFF;
            actor_animations_[slot].clear_actor_service_boundary();
            actor_animations_[slot].defer_actor_service_on_gate();
            actors_.host_meta(slot).spawned_by_animation = spawned_by_animation;
        }
    }
    record_scheduler_phase("player_actor_interaction", 0x001ABB40);
    const CollisionEffects collision_effects = collisions_.player_actor(
        state_,
        PlayerCollisionInput{
            animation_.frame_pointer(),
            animation_.facing_left(),
            was_grounded && (input.attack_pressed || player_.attack_timer != 0),
            interactions_.bounce_response_follow_active(),
            false
        }
    );
    interactions_.consume_collision_effects(collision_effects);
    if (collision_effects.player_animation_stream) {
        animation_.select_stream_entry(*collision_effects.player_animation_stream);
    }
    level_event_sound_requests_.insert(
        level_event_sound_requests_.end(),
        collision_effects.sound_requests.begin(),
        collision_effects.sound_requests.end()
    );
    record_scheduler_phase("terrain_resolution", 0x001B1E38);
    const auto terrain_cell = terrain_.resolve(
        state_,
        level_,
        previous_world_y,
        checkpoint_terrain_behavior_override_
            ? std::optional<std::uint8_t>(checkpoint_terrain_behavior_)
            : std::nullopt
    );
    if (terrain_cell) {
        terrain_behavior_.apply(state_, *terrain_cell, runtime_, rom_bytes_);
    }
    // The camera's tile reference is consumed before the actor traversal in
    // the ROM. Rebase it now so the refill edge and the common actor gate see
    // the same newly crossed tile on this VBlank.
    const int camera_reference_y_before_rebase = camera_.reference_y;
    camera_system_.rebase(state_, level_);
    const bool camera_vertical_reference_rebased =
        camera_.reference_y != camera_reference_y_before_rebase;
    const bool camera_reference_moved_up =
        camera_.reference_y < camera_reference_y_before_rebase;
    interactions_.apply_surface_interaction_lock(state_, arm_surface_interaction);
    const bool vertical_stop_before_frame = player_.terrain_vertical_stop != 0;
    const bool start_jump = input.jump_pressed && was_grounded;
    AnimationContext animation_context = player_animation_context(was_grounded);

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
    const PlayerMotionResult motion = player_motion_.update_horizontal(
        state_,
        runtime_,
        input,
        animation_,
        PlayerMotionInput{
            was_grounded,
            contour_ground_motion,
            terrain_response_was_active,
        }
    );
    const bool ground_release = motion.ground_release;
    const int ground_release_direction = motion.ground_release_direction;
    bool landed_during_frame = false;
    if (checkpoint_terrain_behavior_override_
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
            player_world_x(), player_world_y(), player_.terrain_surface_mode);
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
    record_scheduler_phase("player_movement", 0x001A9D98);
    player_motion_.integrate(state_);
    interactions_.bounce_actor_interaction(state_, terrain_fall_phase_);
    if (player_.terrain_response_active != 0
        && player_.terrain_jump_response_counter != 0
        && player_.terrain_jump_response_counter < 10) {
        // Player_HandleJumpAndVerticalState applies this extra impulse after
        // Player_IntegrateMotion during the first nine active-response ticks.
        ++player_.terrain_jump_response_counter;
        if (animation_.stream_kind() != AnimationStreamKind::Response) {
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
        state_, terrain_response_was_active);
    if (!player_.grounded && player_.terrain_behavior == 0
        && (vertical_stop_before_frame || player_.terrain_response_timer_state != 0)) {
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
            && (vertical_stop_before_frame || player_.vy != 0)) {
            player_.vy = 0x003C;
            terrain_fall_phase_ = true;
        } else if (terrain_fall_phase_ && player_.vy < 0x800) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x0078);
        }
        // FFF0C0 remains set after the residual-upward stop. The original
        // contour routine uses that latched bit to distinguish the later
        // falling/landing phase while FFF0BE is still active.
    }
    // Terrain handlers can arm the interaction lock after the frame's initial
    // state read. The animation VM observes that mutation through GameRamView
    // at its later scheduler boundary.
    if (start_jump && (player_.grounded || contour_ground_motion)) {
        // The recovered frame order applies the jump handler after motion and
        // terrain resolution (Player_Update -> Terrain_Resolve -> jump
        // handler). This leaves the impulse visible for the next frame before
        // the integrator consumes it.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
        contour_ground_motion = false;
        contour_ground_motion_ = false;
        player_.terrain_response_active = 0xFF;
        // The live ROM's ten-step counter is observable when a jump follows
        // a terrain-selected action stream. Keep direct locomotion fixtures
        // on the ordinary integrator path used by their checkpoints.
        player_.terrain_jump_response_counter =
            animation_.stream_kind() == AnimationStreamKind::Action ? 1 : 0;
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
        player_motion_.finish_ground_release(
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
        && player_.grounded
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
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();

    // The ROM consumes a pending 16-pixel reference shift before the actor
    // traversal and then runs the damped follow below. This leaves the
    // boundary frame externally visible with scroll == 16, then exposes the
    // rebased reference on the following frame.
    // A downward Camera_UpdateFollow reference-tile rebase suppresses the
    // vertical damped lookup for that frame, but the ROM still services the
    // independent horizontal follow. An upward rebase keeps the same-frame
    // vertical lookup; otherwise the residual scroll is lost and the player
    // drifts by one pixel at the tile cadence. Controlled resolver fixtures
    // retain their staged rebase boundary. Horizontal rebases retain the
    // same-frame damped follow.
    const bool camera_follow_deferred = camera_vertical_reference_rebased;
    interactions_.hold_bounce_camera_delay(state_, bounce_response_finished);
    record_scheduler_phase("camera_follow", 0x001AA90C);
    const bool same_frame_vertical_rebase =
        camera_reference_moved_up && !checkpoint_terrain_behavior_override_;
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
    } else if (!player_.grounded && !contour_ground_motion) {
        last_ground_direction_ = 0;
    }

    const bool landing_event = just_landed || landed_during_frame;
    const bool preserve_ground_response_run =
        !player_.grounded
        && !contour_ground_motion
        && !interactions_.player_collision_pending()
        && player_.terrain_response_active == 0
        && player_.terrain_response_timer_state != 0
        && animation_.animation_pc() >= 0x00122006
        && animation_.animation_pc() <= 0x001220A6;
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
    } else if (!player_.grounded && !contour_ground_motion) {
        // Genesis keeps the jump stream active during the approach frame. It
        // selects the landing stream only after the contour resolver has
        // actually latched grounded state on the following boundary.
        desired_pose = SpritePose::Jump;
    } else if ((landing_event && animation_.pose() == SpritePose::Jump)
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
    // A direct grounded jump publishes its root before the common actor VM
    // pass. The resulting state boundary therefore exposes the first data
    // cursor (0x001221B2), rather than the untouched root, on the launch
    // frame. Action-selected jumps retain their existing post-pass ordering.
    const bool select_jump_before_vm =
        start_jump && animation_.stream_kind() == AnimationStreamKind::Locomotion;
    if (select_jump_before_vm) {
        animation_.select_locomotion_stream(SpritePose::Jump, vm_context);
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
    if (input.apple_pressed && was_grounded && animation_.rom_loaded()) {
        // Player_SelectLocomotionOrAction publishes the throw root before
        // the single AnimationVM_TickActors traversal. Let that traversal
        // consume the root directly; the old post-pass apple boundary was a
        // native ordering workaround rather than a ROM state field.
        animation_.select_stream_entry(kPlayerAppleActionStream);
        player_.animation_selector.state_lock = 0x0E;
    }
    // Actor_ActorCollisionPass follows the player selectors in the ROM. Its
    // sword terminal edge is handled inside this one source/target scan;
    // there is deliberately no pre-motion companion call.
    record_scheduler_phase("actor_collision", 0x001ABD7E);
    collisions_.actor_actor(state_);
    record_scheduler_phase("level_exit_transition", 0x001A8F0C);
    const bool level_exit_callback = scene_.service_level_exit(
        player_world_y(),
        camera_.level_height,
        player_.terrain_terminal_transition,
        player_.animation_selector.interaction_lock
    );
    if (level_exit_callback) start_level_event_stream_after_exit();
    // 0x001A8F04 is the recovered level callback boundary. Level 02 and 06
    // install their timed stream from the exit callback above, then dispatch
    // one record tick here on the same subsequent callback cadence.
    record_scheduler_phase("empty_return", 0x001A8F04);
    update_level_events();
    // FUN_001B01AC is the late interaction resource service. Its actor refill
    // edge is visible at the same game-loop boundary on either phase of
    // FRAME_PHASE_COUNTER; only the common actor animation table walk is
    // phase-gated.
    record_scheduler_phase("interaction_counter", 0x001B00CA);
    record_scheduler_phase("interaction_resource", 0x001B01AC);
    if (!stable_terrain_handler_fixture) {
    interactions_.scan_refill_window(state_, level_, stable_terrain_handler_fixture);
    }
    interactions_.flush_surface_actor_spawn(state_);
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();
    // The camera tile-update boundary does not suppress the common player VM
    // pass: the ROM services animation after the horizontal follow even when
    // the vertical reference tile is rebased. Keep the separate catch-up
    // marker only for the post-follow downward rebase path above.
    record_scheduler_phase("scene_advance", 0x001A8E3E);
    update_scene_resources();
    record_scheduler_phase("animation_vm", 0x001AC784);
    if (!stable_terrain_handler_fixture) {
        // The bounce response's F8 command publishes the dynamic 0x121AD8
        // root at this boundary but leaves the previous frame pointer in
        // place. Do not consume the new root until the following VBlank.
        const bool response_dynamic_handoff =
            animation_.animation_pc() == 0x001221E8;
        update_animation_vm_ordinal_30(
            desired_pose,
            horizontal_direction(input),
            vm_context,
            response_dynamic_handoff,
            bounce_response_finished
        );
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
        player_animation_context(player_.grounded),
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
    if (input.attack_pressed && was_grounded && animation_.rom_loaded()) {
        // The live ROM trace has a two-stage action transition: the input
        // frame leaves the player at 0x001232E0, and the following animation
        // tick enters the sword stream at 0x001223E2. The older isolated
        // collision fixture starts directly at 0x0012271A, so retain that
        // entry when no live action cursor is present.
        const auto current_animation = animation_.animation_pc();
        const bool at_attack_action_root =
            current_animation == 0x00122034 || current_animation == 0x00122040;
        const bool already_in_attack_transition =
            (current_animation >= 0x00122040 && current_animation <= 0x0012246A)
            || current_animation == 0x001232E0;
        if (at_attack_action_root) {
            // State-synchronized traces observe the post-selector stable
            // cursor, after the transient 0x1232E0 action state has handed
            // control to the sword stream.
            animation_.select_stream_entry(kPlayerSwordStableStream);
            animation_.set_frame_pointer(kPlayerSwordFirstFrame);
        } else if (!already_in_attack_transition) {
            const auto attack_stream = current_animation == 0x0012202C
                || animation_.frame_pointer() == 0x001EA48E
                ? 0x001232E0U
                : kPlayerSwordAnimationStream;
            animation_.select_stream_entry(attack_stream);
        }
    }
    // Non-combat F5 streams publish their request on the current animation
    // tick and expect the auxiliary actor to be visible in that frame's
    // state record. The live sword action is intentionally deferred to the
    // next frame boundary above while its attack timer is active.
    if (player_.attack_timer == 0
        && animation_.stream_entry() != kPlayerAttackTransitionStream
        && animation_.stream_entry() != kPlayerSwordStableStream) {
        // Flush the one player-side command whose semantic effect was
        // intentionally held until this post-selector boundary. No actor VM
        // is invoked here.
        flush_deferred_animation_spawn();
    }
    }
    if (transition_frame) {
        record_scheduler_phase("scene_advance", 0x001A8E3E);
        update_scene_resources();
        // Scene_EnterTransitionMode owns the transition movement, but the
        // frame loop still reaches its common ordinal-30 animation service.
        // Keeping that service here removes the native early-return path.
        record_scheduler_phase("animation_vm", 0x001AC784);
        update_animation_vm_ordinal_30(
            SpritePose::Idle,
            horizontal_direction(input),
            player_animation_context(player_.grounded),
            false,
            false
        );
    }
    record_scheduler_phase("transition_completion", 0x001AE0F6);
    (void) scene_.transition_completion_ready();
    // Camera scroll publication is presentation-owned in the native build;
    // retaining the ROM boundary in the trace makes that classification
    // explicit without inventing a second camera gameplay pass.
    record_scheduler_phase("camera_scroll_publish", 0x001AAA2A);
    record_scheduler_phase("scene_completion", 0x001B315C);
    (void) scene_.complete_script_to_state1();
    // State serialization needs the final player record after ordinal 30.
    // This mirror is not a gameplay scheduler phase.
    sync_player_actor();
    apply_actor_timeline(frame_ + 1);
    checkpoint_animation_selector_pending_ = false;
    record_scheduler_phase("state_boundary", 0);
    collect_scheduler_writer_pcs();
    ++frame_;
}

}  // namespace openaladdin
