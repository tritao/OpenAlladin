#include "collision.hpp"

#include <algorithm>

namespace openaladdin {
namespace {

constexpr std::uint8_t kActorGuardType = 0x0A;
constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint32_t kActorDeathAnimationStream = 0x00122FA2;
constexpr std::uint32_t kActorSwordDeathAnimationStream = 0x00122DD8;
constexpr std::uint32_t kActorDeathTemplate = 0x001B7940;
constexpr std::uint32_t kActorSwordDeathTemplate = 0x001B792C;
constexpr std::uint8_t kActorDeathFrames = 43;
constexpr std::uint8_t kActorSwordTerminalFrames = 19;

int normalized_left(const CollisionBox& box) {
    return std::min(box.left, box.right);
}

int normalized_right(const CollisionBox& box) {
    return std::max(box.left, box.right);
}

int normalized_top(const CollisionBox& box) {
    return std::min(box.top, box.bottom);
}

int normalized_bottom(const CollisionBox& box) {
    return std::max(box.top, box.bottom);
}

}  // namespace

bool CollisionSystem::valid_frame(std::uint32_t frame_pointer) const {
    return rom_ != nullptr
        && frame_pointer != 0
        && static_cast<std::size_t>(frame_pointer) + 5 < rom_->size();
}

CollisionBox CollisionSystem::frame_bounds(
    std::uint32_t frame_pointer,
    int origin_x,
    int origin_y,
    bool facing_left
) const {
    CollisionBox box;
    if (!valid_frame(frame_pointer)) return box;

    const auto byte = [&](std::size_t offset) {
        return (*rom_)[static_cast<std::size_t>(frame_pointer) + offset];
    };
    const auto signed_byte = [](std::uint8_t value) {
        return static_cast<int>(static_cast<std::int8_t>(value));
    };

    // FUN_001ABB40 reads the four bounds from frame +2..+5. The display
    // path mirrors signed X offsets around the actor origin; the unmirrored
    // path uses the raw unsigned bytes.
    box.top = origin_y + byte(3);
    box.bottom = origin_y + byte(5);
    if (!facing_left) {
        box.left = origin_x + byte(2);
        box.right = origin_x + byte(4);
    } else {
        box.left = origin_x - signed_byte(byte(4));
        box.right = origin_x - signed_byte(byte(2));
    }
    box.valid = true;
    return box;
}

CollisionBox CollisionSystem::hitbox(
    std::uint32_t frame_pointer,
    int origin_x,
    int origin_y,
    bool facing_left
) const {
    CollisionBox box;
    if (!valid_frame(frame_pointer)) return box;

    const auto byte = [&](std::size_t offset) {
        return (*rom_)[static_cast<std::size_t>(frame_pointer) + offset];
    };
    if (!facing_left) {
        box.left = origin_x + byte(2);
        box.right = origin_x + byte(4);
    } else {
        // Actor_PlayerCollisionPass negates the signed frame byte and stores
        // it in an unsigned byte before adding the actor origin.
        const auto negated_byte = [](std::uint8_t value) {
            return static_cast<int>(static_cast<std::uint8_t>(
                -static_cast<int>(static_cast<std::int8_t>(value))));
        };
        box.left = origin_x + negated_byte(byte(4));
        box.right = origin_x + negated_byte(byte(2));
    }
    box.top = origin_y + byte(3);
    box.bottom = origin_y + byte(5);
    box.valid = true;
    return box;
}

bool CollisionSystem::overlaps(const CollisionBox& first, const CollisionBox& second) {
    if (!first.valid || !second.valid) return false;
    return normalized_left(first) <= normalized_right(second)
        && normalized_top(first) <= normalized_bottom(second)
        && normalized_left(second) < normalized_right(first)
        && normalized_top(second) < normalized_bottom(first);
}

bool CollisionSystem::strict_overlaps(
    const CollisionBox& first,
    const CollisionBox& second
) {
    if (!first.valid || !second.valid) return false;
    return normalized_left(first) < normalized_right(second)
        && normalized_top(first) < normalized_bottom(second)
        && normalized_left(second) < normalized_right(first)
        && normalized_top(second) < normalized_bottom(first);
}

bool CollisionSystem::player_actor_overlap(
    const GameState& state,
    std::uint32_t player_frame_pointer,
    bool player_facing_left,
    ActorIndex actor
) const {
    if (actor >= state.actors.size()) return false;
    const ActorState& record = state.actors[actor];
    if (record.type == 0 || record.frame_ptr == 0) return false;
    const CollisionBox player_box = hitbox(
        player_frame_pointer,
        state.camera.x + state.player.x,
        state.camera.y + state.player.y,
        player_facing_left
    );
    const CollisionBox actor_box = hitbox(
        record.frame_ptr,
        static_cast<int>(record.x),
        static_cast<int>(record.y),
        record.facing_x_flip != 0
    );
    return overlaps(player_box, actor_box);
}

bool CollisionSystem::any_player_actor_overlap(
    const GameState& state,
    std::uint32_t player_frame_pointer,
    bool player_facing_left,
    std::uint8_t actor_type,
    ActorIndex first_slot,
    ActorIndex last_slot
) const {
    if (first_slot > last_slot) return false;
    for (ActorIndex slot = first_slot; slot <= last_slot; ++slot) {
        if (slot >= state.actors.size()) break;
        const ActorState& actor = state.actors[slot];
        if (actor.type != actor_type) continue;
        if (player_actor_overlap(state, player_frame_pointer, player_facing_left, slot)) {
            return true;
        }
    }
    return false;
}

CollisionEffects CollisionSystem::player_actor(
    GameState& state,
    const PlayerCollisionInput& input
) {
    CollisionEffects effects;
    auto& actors = state.actors;

    // Terminal countdown ownership is part of the player/actor service, but
    // the geometry scan itself is the recovered non-player pool 1..24.
    for (ActorIndex slot = 0; slot < actors.size(); ++slot) {
        ActorState& actor = actors[slot];
        if (actor.terminal_timer == 0) continue;
        --actor.terminal_timer;
        if (actor.terminal_timer == 0) {
            actor_lifecycle_.retire(slot);
        }
    }
    if (rom_ == nullptr) return effects;

    const CollisionBox player_box = hitbox(
        input.frame_pointer,
        state.camera.x + state.player.x,
        state.camera.y + state.player.y,
        input.facing_left
    );
    if (!player_box.valid) return effects;

    for (ActorIndex slot = 1; slot <= 24 && slot < actors.size(); ++slot) {
        ActorState& actor = actors[slot];
        if (actor.type == 0) continue;

        // Player/actor collision entry 0x001ABB40 dispatches the actor type
        // only after both frame hitboxes overlap.
        const CollisionBox actor_box = hitbox(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0
        );
        const bool overlap = overlaps(player_box, actor_box);
        if (input.sword_active && actor.type == kActorGuardType && overlap) {
            ActorState terminal = actor;
            terminal.type = kActorTerminalType;
            const ActorState template_record = actor_lifecycle_.from_template(
                kActorDeathTemplate);
            terminal.sprite_attribute = template_record.sprite_attribute;
            terminal.resource_count = template_record.resource_count;
            terminal.movement_pc = 0;
            terminal.animation_pc = kActorDeathAnimationStream;
            terminal.frame_ptr = 0;
            terminal.flags = 0;
            terminal.facing_x_flip = 0;
            terminal.facing_y_flip = 0;
            terminal.terminal_timer = kActorDeathFrames;
            (void)actor_lifecycle_.install(slot, terminal);
        }
        if (actor.type == 0x2D && overlap
            && !input.bounce_response_follow_active) {
            // The type-0x2D handler retires the actor, then arms the player
            // interaction selector for the scheduler's post-VM handoff.
            actor_lifecycle_.retire(slot);
            state.player.animation_selector.response_state_101 = 0;
            state.player.animation_selector.interaction_lock = 0x28;
            state.camera.update_delay = 7;
            effects.player_collision_interaction_pending = true;
        }
        if (actor.type == 0x40 && strict_overlaps(player_box, actor_box)) {
            const ActorState replacement = actor_lifecycle_.initialize_record(
                actor,
                0x001B7ABC
            );
            (void)actor_lifecycle_.install(slot, replacement);
        }
    }
    return effects;
}

void CollisionSystem::terminalize(
    GameState& state,
    ActorIndex slot,
    std::uint32_t animation_stream,
    std::uint8_t frames
) {
    auto& actor = state.actors[slot];
    ActorState terminal = actor;
    terminal.type = kActorTerminalType;
    const std::uint32_t template_address = animation_stream == kActorDeathAnimationStream
        ? kActorDeathTemplate
        : kActorSwordDeathTemplate;
    const ActorState template_record = actor_lifecycle_.from_template(template_address);
    terminal.sprite_attribute = template_record.sprite_attribute;
    terminal.resource_count = template_record.resource_count;
    terminal.movement_pc = 0;
    terminal.animation_pc = animation_stream;
    terminal.frame_ptr = 0;
    terminal.flags = 0;
    terminal.facing_x_flip = 0;
    terminal.facing_y_flip = 0;
    terminal.terminal_timer = frames;
    (void)actor_lifecycle_.install(slot, terminal);
}

void CollisionSystem::actor_actor(GameState& state) {
    if (rom_ == nullptr) return;

    auto& actors = state.actors;
    // FUN_001ABD7E scans auxiliary sources 24..30 and targets records 1..24.
    for (ActorIndex source_slot = 24; source_slot <= 30; ++source_slot) {
        ActorState& source = actors[source_slot];
        if (source.type == 0 || source.type >= 0x83 || source.frame_ptr == 0) {
            continue;
        }

        // This pass uses raw, unmirrored frame offsets. Its four comparisons
        // are intentionally kept explicit rather than using the player
        // hitbox-facing path.
        const CollisionBox source_box = frame_bounds(
            source.frame_ptr,
            static_cast<int>(source.x),
            static_cast<int>(source.y),
            false
        );
        if (!source_box.valid) continue;

        if (source.type == kActorSwordType
            && !actors.host_meta(source_slot).spawned_by_apple
            && source.animation_pc == 0x00122B5A
            && source.flags == 0x08) {
            bool overlaps_target = false;
            for (ActorIndex target_slot = 1; target_slot <= 24; ++target_slot) {
                const ActorState& target = actors[target_slot];
                if (target.type == 0 || target.type >= 0x32 || target.frame_ptr == 0) {
                    continue;
                }
                const CollisionBox target_box = frame_bounds(
                    target.frame_ptr,
                    static_cast<int>(target.x),
                    static_cast<int>(target.y),
                    false
                );
                if (target_box.valid
                    && target_box.left <= source_box.right
                    && target_box.top <= source_box.bottom
                    && source_box.left < target_box.right
                    && source_box.top < target_box.bottom) {
                    overlaps_target = true;
                    break;
                }
            }
            if (!overlaps_target) {
                terminalize(
                    state,
                    source_slot,
                    kActorSwordDeathAnimationStream,
                    kActorSwordTerminalFrames + 1
                );
                continue;
            }
        }

        for (ActorIndex target_slot = 1; target_slot <= 24; ++target_slot) {
            ActorState& target = actors[target_slot];
            if (target.type == 0 || target.type >= 0x32 || target.frame_ptr == 0) {
                continue;
            }
            const CollisionBox target_box = frame_bounds(
                target.frame_ptr,
                static_cast<int>(target.x),
                static_cast<int>(target.y),
                false
            );
            if (!target_box.valid) continue;

            const bool overlap = target_box.left <= source_box.right
                && target_box.top <= source_box.bottom
                && source_box.left < target_box.right
                && source_box.top < target_box.bottom;
            if (!overlap) continue;

            if (source.type == kActorSwordType
                && !actors.host_meta(source_slot).spawned_by_apple
                && target.type == kActorGuardType) {
                terminalize(state, target_slot, kActorDeathAnimationStream, kActorDeathFrames);
                terminalize(
                    state,
                    source_slot,
                    kActorSwordDeathAnimationStream,
                    kActorSwordTerminalFrames
                );
            } else if (source.type == 0x7F && target.type == 0x1D) {
                state.random.value = state.random.value * 13U + 7U;
                const std::uint16_t source_x = source.x;
                const std::uint16_t source_y = source.y;
                ActorState source_replacement = actor_lifecycle_.initialize_record(
                    source,
                    0x001B792C
                );
                source_replacement.x = source_x;
                source_replacement.y = source_y;
                source_replacement.facing_x_flip = 0xFF;
                (void)actor_lifecycle_.install(source_slot, source_replacement);

                const std::uint16_t target_x = target.x;
                const std::uint16_t target_y = target.y;
                ActorState target_replacement = actor_lifecycle_.initialize_record(
                    target,
                    0x001B7940
                );
                target_replacement.x = target_x;
                target_replacement.y = target_y;
                (void)actor_lifecycle_.install(target_slot, target_replacement);
            }
        }
    }
}

}  // namespace openaladdin
