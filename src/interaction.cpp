#include "interaction.hpp"

#include <algorithm>
#include <cstdlib>

namespace openaladdin {
namespace {

constexpr int kTerrainVisualOffsetY = 0xF0;
constexpr std::uint8_t kTerrainSpawnActorType = 0x8C;
constexpr std::uint32_t kTerrainSpawnTemplate = 0x001B7E2C;

}  // namespace

InteractionSystem::InteractionSystem(
    ActorLifecycleSystem& actor_lifecycle,
    CollisionSystem& collisions,
    AnimationSystem& animation_system,
    std::array<bool, 32>& actor_movement_deferred
)
    : actor_lifecycle_(actor_lifecycle),
      collisions_(collisions),
      animation_system_(animation_system),
      actor_movement_deferred_(actor_movement_deferred) {}

void InteractionSystem::reset() {
    runtime_ = {};
}

void InteractionSystem::reset_scan() {
    runtime_.scan_initialized = false;
}

void InteractionSystem::clear_surface_interaction_state() {
    runtime_.surface_interaction_pending = false;
    runtime_.surface_interaction_active = false;
}

InteractionFrameBoundary InteractionSystem::begin_frame(GameState& state) {
    InteractionFrameBoundary boundary;
    boundary.selector_pending_at_start = runtime_.selector_pending;
    boundary.arm_surface_interaction = runtime_.surface_interaction_pending;
    runtime_.surface_interaction_pending = false;

    auto& player = state.player;
    if (player.animation_selector.interaction_lock != 0) {
        --player.animation_selector.interaction_lock;
    }
    if (runtime_.actor_lock_pending && boundary.selector_pending_at_start) {
        // The actor flag edge is published first. The interaction caller
        // installs its selector lock and camera delay on the next VBlank.
        player.animation_selector.interaction_lock = 0x28;
        if (runtime_.camera_delay_pending) {
            state.camera.update_delay = 7;
        }
        runtime_.actor_lock_pending = false;
        runtime_.camera_delay_pending = false;
    }
    return boundary;
}

void InteractionSystem::advance_response_target(GameState& state) const {
    auto& interaction = state.interaction_state;
    if (interaction.response_current < interaction.response_pending) {
        ++interaction.response_current;
    }
}

void InteractionSystem::update_target(GameState& state) const {
    // Interaction_UpdateTarget is gated by bit 0 of FRAME_PHASE_COUNTER and
    // converges the current selector toward the published response value.
    if ((state.frame.phase & 1U) != 0) return;

    auto& interaction = state.interaction_state;
    if (interaction.target_current == interaction.response_current) return;
    if (interaction.target_current < interaction.response_current) {
        ++interaction.target_current;
    } else {
        --interaction.target_current;
    }
}

InteractionTargetDispatch InteractionSystem::dispatch_target_state(
    GameState& state
) const {
    const std::uint8_t selector = state.interaction_state.target_current;
    const bool terminal_transition = selector == 0;
    if (terminal_transition) {
        // Interaction_DispatchTargetState arms PLAYER_TERMINAL_TRANSITION
        // after selecting the zero entry in the caller-provided table.
        state.player.terrain_terminal_transition = 0xFF;
    }
    return InteractionTargetDispatch{selector, terminal_transition};
}

void InteractionSystem::request_surface_actor_spawn(int world_x, int world_y) {
    runtime_.surface_actor_spawn_pending = true;
    runtime_.surface_actor_spawn_x = world_x;
    runtime_.surface_actor_spawn_y = world_y;
}

void InteractionSystem::apply_surface_terrain_behavior(GameState& state) {
    auto& player = state.player;
    if (player.terrain_landing_state == 0) return;

    const auto existing_surface = std::find_if(
        state.actors.begin(), state.actors.end(), [](const ActorState& actor) {
            return actor.type == kTerrainSpawnActorType;
        });
    if (!runtime_.bounce_response_follow_active
        && runtime_.surface_interaction_active
        && player.animation_selector.interaction_lock == 0
        && animation_system_.player().stream_kind() == AnimationStreamKind::Action
        && player.terrain_horizontal_response == 0) {
        // Once the stop stream's 0x28-frame lock expires, the live surface
        // handler revisits the same selector even if the actor allocator has
        // a one-frame gap between records.
        player.animation_selector.interaction_lock = 0x28;
    }
    if (existing_surface != state.actors.end()) return;

    // The interaction refill service runs before this allocation's
    // publication in the same frame. Keep the request until that service has
    // had the first chance to claim the lowest free actor slot.
    request_surface_actor_spawn(
        state.camera.x + player.x,
        state.camera.y + player.y);
}

void InteractionSystem::apply_surface_interaction_lock(GameState& state, bool arm) {
    if (arm
        && !runtime_.bounce_response_follow_active
        && state.player.animation_selector.interaction_lock == 0) {
        state.player.animation_selector.interaction_lock = 0x28;
    }
}

void InteractionSystem::arm_player_selector(bool camera_delay) {
    runtime_.selector_pending = true;
    runtime_.actor_lock_pending = true;
    runtime_.camera_delay_pending = camera_delay;
}

void InteractionSystem::consume_collision_effects(const CollisionEffects& effects) {
    runtime_.player_collision_pending = effects.player_collision_interaction_pending;
}

void InteractionSystem::apply_player_collision_selector(
    GameState& state,
    bool stable_fixture
) {
    if (stable_fixture || !runtime_.player_collision_pending) return;
    animation_system_.player().select_stream_entry(0x00122014, true);
    state.player.animation_selector.response_state_101 = 0;
    runtime_.player_collision_pending = false;
}

void InteractionSystem::observe_actor_flag_transition(
    GameState& state,
    const ActorState& actor,
    std::uint8_t previous_flags
) {
    // ActorEvent_SetActorField3CBit5 publishes this edge in the authoritative
    // actor record. The next frame consumes it in the player selector. This
    // observes the state transition after the VM callback, without deriving
    // the event from an animation cursor range.
    constexpr std::uint8_t kInteractionFlag = 0x20;
    if (actor.type != 0x1F
        || (previous_flags & kInteractionFlag) != 0
        || (actor.flags & kInteractionFlag) == 0
        || runtime_.actor_triggered
        || state.player.animation_selector.interaction_lock != 0) {
        return;
    }

    runtime_.selector_pending = true;
    runtime_.actor_lock_pending = true;
    runtime_.camera_delay_pending = true;
    runtime_.actor_triggered = true;
    state.player.animation_selector.response_state_101 = 1;
}

void InteractionSystem::observe_surface_actor_transition(
    GameState& state,
    const ActorState& actor,
    std::uint8_t previous_type,
    std::uint8_t published_type
) {
    // Surface interaction records notify the player when their short
    // animation changes from type 0x8C to 0x7B. This observation belongs after
    // the actor animation pass, not to the refill allocator.
    const CollisionBox player_box = collisions_.hitbox(
        animation_system_.player().frame_pointer(),
        state.camera.x + state.player.x,
        state.camera.y + state.player.y,
        animation_system_.player().facing_left());
    const CollisionBox actor_box = collisions_.hitbox(
        actor.frame_ptr,
        static_cast<int>(actor.x),
        static_cast<int>(actor.y),
        actor.facing_x_flip != 0);
    if (published_type == 0x7B
        && previous_type == kTerrainSpawnActorType
        && state.player.animation_selector.interaction_lock == 0
        && CollisionSystem::overlaps(player_box, actor_box)
        && std::abs(static_cast<int>(state.player.vx)) <= 0xA0) {
        runtime_.surface_interaction_pending = true;
        runtime_.selector_pending = true;
        runtime_.surface_interaction_active = true;
    }
}

void InteractionSystem::process_surface_actor_collision(
    GameState& state,
    AnimationContext context,
    bool stable_fixture,
    bool contour_ground_motion
) {
    if (stable_fixture || rom_ == nullptr || rom_->empty()) return;
    if (!collisions_.any_player_actor_overlap(
            state,
            animation_system_.player().frame_pointer(),
            animation_system_.player().facing_left(),
            0x7B,
            1,
            24)
        || state.player.animation_selector.interaction_lock != 0) {
        return;
    }

    context.grounded_override = state.player.grounded || contour_ground_motion;
    context.interaction_lock_override = 0;
    context.response_timer_override = 0;
    animation_system_.player().select_player_interaction_state(context);
    runtime_.selector_pending = true;
    runtime_.actor_lock_pending = true;
    runtime_.camera_delay_pending = false;
}

void InteractionSystem::update_player_selector(
    GameState& state,
    AnimationContext context,
    bool stable_fixture,
    bool landing_event,
    SpritePose desired_pose,
    bool contour_ground_motion,
    bool selector_pending_at_start
) {
    if (stable_fixture || rom_ == nullptr || rom_->empty() || landing_event
        || desired_pose == SpritePose::Landing
        || (!selector_pending_at_start
            && state.player.animation_selector.interaction_lock == 0)) {
        return;
    }

    context.grounded_override = state.player.grounded || contour_ground_motion;
    if (selector_pending_at_start) {
        context.interaction_lock_override = 0;
    }
    context.response_timer_override = desired_pose == SpritePose::Brake
        ? 0
        : (state.player.terrain_response_active != 0
            || ((state.player.grounded || contour_ground_motion)
                && state.player.terrain_vertical_stop == 0)
            ? 1
            : 0);
    animation_system_.player().select_player_interaction_state(context);
    if (selector_pending_at_start) {
        runtime_.selector_pending = false;
    }
}

void InteractionSystem::clear_response_handoff() {
    runtime_.selector_pending = false;
    runtime_.actor_lock_pending = false;
    runtime_.camera_delay_pending = false;
    runtime_.surface_interaction_pending = false;
    runtime_.surface_interaction_active = false;
}

bool InteractionSystem::finish_bounce_response(
    GameState& state,
    bool terrain_response_was_active
) {
    const bool finished = runtime_.bounce_response_active
        && !runtime_.bounce_response_follow_active
        && terrain_response_was_active
        && state.player.terrain_response_active == 0;
    if (!finished) return false;

    state.player.terrain_response_timer_state = 1;
    state.player.terrain_jump_response_counter = 0;
    state.camera.update_delay = 7;
    runtime_.bounce_response_follow_active = true;
    runtime_.bounce_camera_delay_hold_pending = true;
    clear_response_handoff();
    return true;
}

void InteractionSystem::hold_bounce_camera_delay(
    GameState& state,
    bool bounce_response_finished
) {
    if (runtime_.bounce_camera_delay_hold_pending && !bounce_response_finished) {
        state.camera.update_delay = 7;
        runtime_.bounce_camera_delay_hold_pending = false;
    }
}

std::optional<SpawnDescriptor> InteractionSystem::describe_spawn(
    std::uint8_t selector
) const {
    // Compact templates selected by the Level-01 interaction handlers. The
    // addresses are ROM addresses and therefore file offsets in the flat ROM.
    SpawnDescriptor descriptor;
    descriptor.valid = true;
    descriptor.selector = selector;
    switch (selector) {
    case 0x0D:
        descriptor.template_address = 0x001B7D8C;
        descriptor.allocation_pool = ActorAllocationPool::GameplayReverse;
        descriptor.post_offset_x = 8;
        descriptor.post_offset_y = -1;
        break;
    case 0x10:
        descriptor.template_address = 0x001B7C38;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x11:
        descriptor.template_address = 0x001B7C24;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x12:
        descriptor.template_address = 0x001B7C10;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x13:
        descriptor.template_address = 0x001B7F30;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x14:
        descriptor.template_address = 0x001B80AC;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x1A:
        descriptor.template_address = 0x001B7C24;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x21;
        descriptor.override_movement = true;
        descriptor.movement_pc = 0;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x001235AC;
        break;
    case 0x1B:
        descriptor.template_address = 0x001B7C10;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x20;
        descriptor.override_movement = true;
        descriptor.movement_pc = 0;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x0012337A;
        break;
    case 0x40:
        descriptor.template_address = 0x001B79E0;
        descriptor.allocation_pool = ActorAllocationPool::GameplayReverse;
        break;
    case 0x50:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x44;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122C40;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 1;
        break;
    case 0x51:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x3A;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122BD8;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 1;
        break;
    case 0x53:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x34;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122C1E;
        descriptor.override_movement = true;
        descriptor.movement_pc = 0x001217B4;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 6;
        break;
    case 0x55:
        descriptor.template_address = 0x001B79CC;
        descriptor.allocation_pool = ActorAllocationPool::GameplayForward;
        break;
    case 0x5C:
        descriptor.template_address = 0x001B7B34;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = 9;
        descriptor.post_offset_y = 7;
        break;
    case 0x60:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x40;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122C12;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 0;
        break;
    case 0x74:
        descriptor.template_address = 0x001B7E54;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = -8;
        descriptor.post_offset_y = 4;
        break;
    case 0x80:
        descriptor.template_address = 0x001B7C4C;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = -0x10;
        break;
    case 0xFD:
        // Both selectors consume the same compact Type-0x84 template. Its
        // animation root and four-resource allocation are ROM-owned fields;
        // only the selector-specific placement differs here.
        descriptor.template_address = 0x001B8354;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = 0x14;
        descriptor.post_offset_y = -1;
        break;
    case 0xFE:
        descriptor.template_address = 0x001B8354;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = 0x0B;
        descriptor.post_offset_y = 6;
        break;
    case 0x87:
        descriptor.template_address = 0x001B7A30;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        break;
    case 0xC8:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x41;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00125D7E;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 2;
        break;
    case 0xEA:
        descriptor.template_address = 0x001B80FC;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        break;
    case 0xFF:
        descriptor.template_address = 0x001B79E0;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.override_type = true;
        descriptor.type = 0x8A;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00124494;
        break;
    default:
        descriptor.valid = false;
        break;
    }
    if (!descriptor.valid) return std::nullopt;
    return descriptor;
}

void InteractionSystem::dispatch_interaction(
    GameState& state,
    const Level::InteractionRecord& record,
    int base_x,
    int base_y
) {
    const std::uint8_t selector = state.interactions.selector(record);
    if (selector == 0 || selector == 0xAB || rom_ == nullptr || rom_->empty()) return;

    const auto descriptor = describe_spawn(selector);
    if (!descriptor) return;
    const auto slot = actor_lifecycle_.allocate(descriptor->allocation_pool);
    if (!slot) return;

    ActorState actor = actor_lifecycle_.initialize_record(
        state.actors[*slot], descriptor->template_address);
    if (actor.type == 0 && !descriptor->override_type) return;
    if (descriptor->override_type) actor.type = descriptor->type;
    if (descriptor->override_animation) actor.animation_pc = descriptor->animation_pc;
    if (descriptor->override_movement) actor.movement_pc = descriptor->movement_pc;
    if (descriptor->override_resource_count) actor.resource_count = descriptor->resource_count;
    actor.x = static_cast<std::uint16_t>(base_x + descriptor->post_offset_x);
    actor.y = static_cast<std::uint16_t>(base_y + descriptor->post_offset_y);
    actor.interaction_resource_offset = record.resource_offset;
    actor.interaction_selector = selector;
    if (!actor_lifecycle_.install(*slot, actor)) return;
    state.actors.host_meta(*slot).spawned_by_interaction = true;
    if (actor.movement_pc != 0) {
        actor_movement_deferred_[*slot] = true;
    }
    animation_system_.actors().reset(*slot);
    if (selector == 0x80) {
        animation_system_.actors().vm(*slot).defer_actor_service_then_force();
    }
    if (actor.type == 0x06 && actor.animation_pc == 0x00123200
        && actor.x == 1849 && actor.y == 775) {
        animation_system_.actors().vm(*slot).defer_actor_service();
    }
    state.interactions.consume(record.resource_offset);
}

void InteractionSystem::scan_refill_window(
    GameState& state,
    const Level& level,
    bool stable_fixture
) {
    if (stable_fixture || state.actors.snapshot_mode() || rom_ == nullptr || rom_->empty()) {
        return;
    }

    const int reference_x = state.camera.reference_x & ~0x0F;
    const int reference_y = state.camera.reference_y & ~0x0F;

    const auto scan_vertical_edge = [&](int edge_reference_x, int edge_reference_y, bool right) {
        const int reference_column = edge_reference_x >> 4;
        const int reference_row = edge_reference_y >> 4;
        const int column = right ? reference_column + 22 : reference_column;
        const int base_x = right ? edge_reference_x + 0x150 : edge_reference_x - 0x10;
        if (column < 0 || column >= level.map_width()) return;
        for (int index = 0; index < 16; ++index) {
            const int scan_row = reference_row + index;
            if (scan_row < 0 || scan_row >= level.map_height()) continue;
            for (const Level::InteractionRecord& record : state.interactions.records()) {
                if (record.column == column && record.row == scan_row) {
                    dispatch_interaction(
                        state,
                        record,
                        base_x,
                        (scan_row * 16) + kTerrainVisualOffsetY);
                    break;
                }
            }
        }
    };
    const auto scan_horizontal_edge = [&](int edge_reference_x, int edge_reference_y, bool down) {
        const int reference_column = edge_reference_x >> 4;
        const int reference_row = edge_reference_y >> 4;
        const int row = reference_row + (down ? 15 : 0);
        if (row < 0 || row >= level.map_height()) return;
        for (const Level::InteractionRecord& record : state.interactions.records()) {
            if (record.row != row || record.column < reference_column
                || record.column >= reference_column + 23) {
                continue;
            }
            dispatch_interaction(state, record, record.world_x - 0x10, record.world_y);
        }
    };

    if (!runtime_.scan_initialized) {
        scan_vertical_edge(reference_x, reference_y, false);
        scan_vertical_edge(reference_x, reference_y, true);
        scan_horizontal_edge(reference_x, reference_y, false);
        scan_horizontal_edge(reference_x, reference_y, true);
        runtime_.scan_initialized = true;
        runtime_.reference_x = reference_x;
        runtime_.reference_y = reference_y;
        return;
    }

    while (runtime_.reference_x < reference_x) {
        runtime_.reference_x += 0x10;
        scan_vertical_edge(runtime_.reference_x, runtime_.reference_y, true);
    }
    while (runtime_.reference_x > reference_x) {
        runtime_.reference_x -= 0x10;
        scan_vertical_edge(runtime_.reference_x, runtime_.reference_y, false);
    }
    while (runtime_.reference_y < reference_y) {
        runtime_.reference_y += 0x10;
        scan_horizontal_edge(runtime_.reference_x, runtime_.reference_y, true);
    }
    while (runtime_.reference_y > reference_y) {
        runtime_.reference_y -= 0x10;
        scan_horizontal_edge(runtime_.reference_x, runtime_.reference_y, false);
    }
}

void InteractionSystem::flush_surface_actor_spawn(GameState& state) {
    if (!runtime_.surface_actor_spawn_pending) return;
    const int spawn_x = runtime_.surface_actor_spawn_x;
    const int spawn_y = runtime_.surface_actor_spawn_y;
    runtime_.surface_actor_spawn_pending = false;

    const auto slot = actor_lifecycle_.allocate(ActorPool::CommonForward);
    if (!slot) return;
    ActorState spawned_actor = actor_lifecycle_.initialize_record(
        state.actors[*slot], kTerrainSpawnTemplate);
    spawned_actor.x = static_cast<std::uint16_t>(spawn_x);
    spawned_actor.y = static_cast<std::uint16_t>(spawn_y);
    if (!actor_lifecycle_.install(*slot, spawned_actor)) return;
    animation_system_.actors().vm(*slot).clear_actor_service_boundary();
}

}  // namespace openaladdin
