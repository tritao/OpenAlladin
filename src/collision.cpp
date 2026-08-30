#include "collision.hpp"

#include <algorithm>

namespace openaladdin {
namespace {

constexpr std::uint8_t kActorGuardType = 0x0A;
constexpr std::uint8_t kActorBounceType = 0x65;
constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint32_t kActorDeathAnimationStream = 0x00122FA2;
constexpr std::uint32_t kActorSwordDeathAnimationStream = 0x00122DD8;
constexpr std::uint32_t kActorDeathTemplate = 0x001B7940;
constexpr std::uint32_t kActorSwordDeathTemplate = 0x001B792C;
constexpr std::uint8_t kActorDeathFrames = 43;
constexpr std::uint8_t kActorSwordTerminalFrames = 19;
constexpr std::uint32_t kPlayerCollisionHandlerTable = 0x001CBE;
constexpr std::uint32_t kPlayerResponseAnimationStream = 0x00121C62;
constexpr std::uint32_t kType4EActorAnimationStream = 0x00124B1A;
constexpr std::uint32_t kType4FActorAnimationStream = 0x00124B3E;
constexpr std::uint32_t kType12ActorAnimationStream = 0x0012512C;
constexpr std::uint32_t kType4DType12ResponseAnimation = 0x001222C2;
constexpr std::uint32_t kType4DResponseAnimation = 0x00124498;
constexpr std::uint32_t kType3E3FResponseMovementStream = 0x00121618;
constexpr std::uint32_t kType65ActorAnimationStream = 0x001244B0;
constexpr std::uint32_t kType65PlayerAnimationStream = 0x001221B8;

PlayerCollisionHandlerKind player_handler_kind(std::uint8_t actor_type) {
    switch (actor_type) {
    case 0x04:
    case 0x0D:
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28:
    case 0x2B:
        return PlayerCollisionHandlerKind::NoOp;
    case 0x03:
    case 0x06: case 0x0A: case 0x0F:
    case 0x1D: case 0x20: case 0x2A:
        return PlayerCollisionHandlerKind::SharedActorResponse;
    case 0x08: case 0x09:
        return PlayerCollisionHandlerKind::TerrainPushResponse;
    case 0x18: case 0x19:
    case 0x23:
    case 0x2C: case 0x2D: case 0x2F: case 0x30:
    case 0x32: case 0x45: case 0x46: case 0x47: case 0x48:
    case 0x49: case 0x4A: case 0x4B: case 0x4C:
    case 0x54:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7D:
        return PlayerCollisionHandlerKind::InteractionResponse;
    case 0x0C:
    case 0x1A: case 0x1B: case 0x1C:
    case 0x34: case 0x35:
    case 0x43: case 0x44:
    case 0x3E: case 0x3F:
    case 0x4D:
    case 0x62: case 0x63: case 0x64: case 0x65:
    case 0x67: case 0x68: case 0x6A:
        return PlayerCollisionHandlerKind::ActorResponse;
    case 0x50: case 0x51:
    case 0x52: case 0x53:
    case 0x66:
        return PlayerCollisionHandlerKind::PlayerPlacement;
    case 0x11: case 0x12:
    case 0x4E: case 0x4F:
        return PlayerCollisionHandlerKind::PlayerLaunch;
    case 0x6E: case 0x6F: case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
        return PlayerCollisionHandlerKind::TransitionResponse;
    default:
        return PlayerCollisionHandlerKind::Unknown;
    }
}

// Fallback identities keep ROM-less collision fixtures useful. When a full
// ROM is bound, player_collision_handler() replaces these addresses with the
// actual four-byte table entry at 0x001CBE + type * 4.
std::uint32_t known_player_handler_address(std::uint8_t actor_type) {
    switch (actor_type) {
    case 0x03: return 0x001AED86;
    case 0x04: return 0x001AEDA6;
    case 0x06: case 0x0F: return 0x001AE9DA;
    case 0x08: case 0x09: return 0x001AE722;
    case 0x0A: case 0x1D: case 0x20: case 0x2A: return 0x001AE9C6;
    case 0x0C: return 0x001AE9A8;
    case 0x0D: return 0x001AEB7A;
    case 0x11: case 0x12: return 0x001AF110;
    case 0x18: case 0x19: return 0x001AEA48;
    case 0x1A: return 0x001AE9E0;
    case 0x1B: return 0x001AEA00;
    case 0x1C: return 0x001AEA24;
    case 0x23: return 0x001AEECA;
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28:
        return 0x001AEEDE;
    case 0x2B: return 0x001AEBFE;
    case 0x2C: case 0x2D: return 0x001AEE40;
    case 0x2F: return 0x001AEDA8;
    case 0x30: return 0x001AEE18;
    case 0x32: case 0x79: return 0x001AEB7C;
    case 0x34: return 0x001AF4D8;
    case 0x35: case 0x40: return 0x001AF468;
    case 0x3E: return 0x001AF2B0;
    case 0x3F: return 0x001AF2FA;
    case 0x43: return 0x001AE64C;
    case 0x44: return 0x001AEF12;
    case 0x45: return 0x001AEEE0;
    case 0x46: return 0x001AEF5C;
    case 0x47: return 0x001AEFB0;
    case 0x48: return 0x001AEFDC;
    case 0x49: return 0x001AF008;
    case 0x4A: return 0x001AF034;
    case 0x4B: return 0x001AF060;
    case 0x4C: return 0x001AF08C;
    case 0x4D: return 0x001AF0B8;
    case 0x4E: return 0x001AFCD2;
    case 0x4F: return 0x001AFC4E;
    case 0x50: case 0x51: return 0x001AF8F6;
    case 0x52: case 0x53: return 0x001AF79E;
    case 0x54: return 0x001AF7F2;
    case 0x62: case 0x63: return 0x001AF81C;
    case 0x64: return 0x001AF894;
    case 0x65: return 0x001AFBF4;
    case 0x67: case 0x68: return 0x001AF740;
    case 0x6A: return 0x001AF978;
    case 0x6E: case 0x6F: case 0x70: case 0x71: case 0x72: case 0x73:
        return 0x001AFB36;
    case 0x74: case 0x75: return 0x001AFA84;
    case 0x76: case 0x77: return 0x001AF9F6;
    case 0x78: case 0x7A: return 0x001AEBDC;
    case 0x7B: return 0x001AE9D4;
    case 0x7D: return 0x001AEBA4;
    default: return 0;
    }
}

std::uint32_t read_be32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t address
) {
    if (address + 3 >= bytes.size()) return 0;
    return (static_cast<std::uint32_t>(bytes[address]) << 24)
        | (static_cast<std::uint32_t>(bytes[address + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[address + 2]) << 8)
        | bytes[address + 3];
}

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

PlayerCollisionHandlerInfo CollisionSystem::player_collision_handler(
    std::uint8_t actor_type
) const {
    PlayerCollisionHandlerInfo info;
    info.actor_type = actor_type;
    if (actor_type >= 0x7F) return info;

    info.address = known_player_handler_address(actor_type);
    if (rom_ != nullptr) {
        const std::uint32_t table_address = kPlayerCollisionHandlerTable
            + static_cast<std::uint32_t>(actor_type) * 4U;
        const std::uint32_t table_entry = read_be32(*rom_, table_address);
        if (table_address + 3 < rom_->size()) info.address = table_entry;
    }
    info.kind = player_handler_kind(actor_type);
    info.native_implemented = actor_type == kActorGuardType
        || actor_type == 0x2D
        || actor_type == 0x40
        || actor_type == 0x3E
        || actor_type == 0x3F
        || actor_type == kActorBounceType
        || actor_type == 0x11
        || actor_type == 0x12
        || actor_type == 0x4D
        || actor_type == 0x4E
        || actor_type == 0x4F;
    return info;
}

std::vector<PlayerActorCollision> CollisionSystem::detect_player_actor(
    const GameState& state,
    const PlayerCollisionInput& input
) const {
    std::vector<PlayerActorCollision> collisions;
    const CollisionBox player_box = hitbox(
        input.frame_pointer,
        state.camera.x + state.player.x,
        state.camera.y + state.player.y,
        input.facing_left
    );
    if (!player_box.valid) return collisions;

    // Actor_PlayerCollisionPass scans the 24 gameplay records and rejects
    // actor types 0 and 0x7F+ before indexing PLAYER_COLLISION_HANDLER_TABLE.
    for (ActorIndex slot = 1; slot <= 24 && slot < state.actors.size(); ++slot) {
        const ActorState& actor = state.actors[slot];
        if (actor.type == 0 || actor.type >= 0x7F) continue;
        const CollisionBox actor_box = hitbox(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0
        );
        if (!overlaps(player_box, actor_box)) continue;
        collisions.push_back(PlayerActorCollision{slot, player_collision_handler(actor.type)});
    }
    return collisions;
}

CollisionEffects CollisionSystem::dispatch_player_handler(
    GameState& state,
    const PlayerActorCollision& collision,
    const PlayerCollisionInput& input
) {
    CollisionEffects effects;
    if (collision.actor >= state.actors.size()) return effects;
    ActorState& actor = state.actors[collision.actor];

    if (collision.handler.kind == PlayerCollisionHandlerKind::NoOp) return effects;
    if (!collision.handler.native_implemented) {
        effects.unhandled_player_collision_types.push_back(actor.type);
        return effects;
    }

    const CollisionBox player_box = hitbox(
        input.frame_pointer,
        state.camera.x + state.player.x,
        state.camera.y + state.player.y,
        input.facing_left
    );
    const CollisionBox actor_box = hitbox(
        actor.frame_ptr,
        static_cast<int>(actor.x),
        static_cast<int>(actor.y),
        actor.facing_x_flip != 0
    );
    if (!player_box.valid || !actor_box.valid) return effects;

    if (actor.type == kActorGuardType) {
        if (!input.sword_active) return effects;
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
        (void)actor_lifecycle_.install(collision.actor, terminal);
        return effects;
    }

    if (actor.type == 0x2D) {
        if (input.bounce_response_follow_active) return effects;
        // The guard's type-0x2D child is the enemy sword/contact effect. It
        // is still consumed on contact, but it must not cancel a player
        // sword action that is already in flight: doing so retires the child
        // and arms the generic player response before the player's stable
        // sword stream reaches its F5 child-spawn command. At close range
        // that made the attack look completely inert and prevented the
        // player sword from ever entering the actor/actor hit pass.
        actor_lifecycle_.retire(collision.actor);
        if (input.sword_active) return effects;
        // Outside a sword action, the type-0x2D handler retires the actor,
        // then arms the player interaction selector for the scheduler's
        // post-VM handoff.
        state.player.animation_selector.response_state_101 = 0;
        state.player.animation_selector.interaction_lock = 0x28;
        state.camera.update_delay = 7;
        effects.player_collision_interaction_pending = true;
        return effects;
    }

    // Type-0x40 uses the handler's strict secondary rectangle test after the
    // common pass has accepted the candidate.
    if (actor.type == 0x40 && strict_overlaps(player_box, actor_box)) {
        const ActorState replacement = actor_lifecycle_.initialize_record(
            actor,
            0x001B7ABC
        );
        (void)actor_lifecycle_.install(collision.actor, replacement);
        return effects;
    }

    if (actor.type == kActorBounceType) {
        // ActorType65_PlayerCollisionHandler is reached at the post-motion
        // player collision boundary. It only accepts a falling player while
        // the interaction-animation gate is clear.
        if (state.player.vy <= 0
            || state.player.animation_selector.animation_gate != 0) {
            return effects;
        }
        actor.type = 0x66;
        actor.animation_pc = kType65ActorAnimationStream;
        actor.animation_timer = 0;
        state.player.y = static_cast<int>(actor.y) - 0x1F - state.camera.y;
        state.player.vy = static_cast<std::int16_t>(-0x0500 + 0x003C);
        state.player.terrain_response_active = 0xFF;
        state.player.terrain_vertical_stop = 0;
        state.player.terrain_response_timer_state = 0;
        state.player.terrain_jump_response_counter = 1;
        state.player.animation_selector.response_timer = 0;
        effects.player_bounce_response_started = true;
        effects.player_animation_stream = kType65PlayerAnimationStream;
        effects.player_animation_state_immediate = true;
        return effects;
    }

    if (actor.type == 0x3E || actor.type == 0x3F) {
        // ActorType3E/3F_PlayerCollisionHandler mutate the source record in
        // place. The only difference between the two shared handlers is the
        // response latch they publish for the later interaction selectors.
        if (actor.type == 0x3E) {
            state.interaction_state.type3e_response_latch = 0xFF;
        } else {
            state.interaction_state.type3f_response_latch = 0xFF;
        }
        actor.type = kActorTerminalType;
        actor.movement_pc = kType3E3FResponseMovementStream;
        actor.animation_timer = 0;
        state.camera.horizontal_threshold = 0x0070;
        state.camera.vertical_threshold = 0x0190;
        if (state.camera.vdp_update != 0) effects.sound_requests.push_back(0x64);
        return effects;
    }

    if (actor.type == 0x4D) {
        // ActorType4D_PlayerCollisionHandler first searches the gameplay
        // records for the paired type-0x12 actor. The helper leaves the
        // first match selected, so preserve that ordering here.
        ActorState* paired = nullptr;
        for (ActorIndex slot = 1; slot <= 24 && slot < state.actors.size(); ++slot) {
            if (state.actors[slot].type == 0x12) {
                paired = &state.actors[slot];
                break;
            }
        }
        if (paired != nullptr) {
            state.camera.horizontal_threshold = 0x00B0;
            paired->animation_pc = kType4DType12ResponseAnimation;
            paired->animation_timer = 0;

            const std::uint16_t player_world_x = static_cast<std::uint16_t>(
                state.camera.x + state.player.x);
            if (paired->x >= player_world_x) {
                state.player.vx = static_cast<std::int16_t>(state.player.vx + 0x0100);
            } else {
                state.player.vx = static_cast<std::int16_t>(state.player.vx - 0x0100);
            }
        }

        // The source record remains in place but becomes the Type-0x84
        // response actor. Its movement PC and other record fields are left
        // intact; only the fields explicitly cleared by the ROM are reset.
        actor.type = 0x84;
        actor.animation_pc = kType4DResponseAnimation;
        actor.animation_timer = 0;
        actor.movement_word_18 = 0;
        actor.movement_word_1a = 0;
        return effects;
    }

    if (actor.type == 0x11 || actor.type == 0x12) {
        // Player_ProcessInteractionState owns the ungated branch. Type 0x12
        // additionally selects the source actor's interaction response.
        if (!input.interaction_gate) {
            if (actor.type == 0x12) {
                actor.animation_pc = kType12ActorAnimationStream;
                actor.animation_timer = 0;
            }
            return effects;
        }

        const std::uint16_t player_world_x = static_cast<std::uint16_t>(
            state.camera.x + state.player.x);
        // The 68K comparison takes actor_x - player_world_x and the BCC
        // branch selects -0x400 for an actor at or to the right of the
        // player; the other side receives +0x600.
        state.player.vx = actor.x >= player_world_x ? -0x0400 : 0x0600;
        state.player.vy = -0x0400;
        state.player.terrain_response_active = 0xFF;
        state.player.animation_selector.response_active = 0xFF;
        state.player.terrain_vertical_stop = 0;
        state.player.terrain_response_timer_state = 0;
        state.player.animation_selector.response_timer = 0;
        state.camera.horizontal_threshold = 0x00B0;
        state.camera.vertical_threshold = 0x0160;
        effects.player_animation_stream = kPlayerResponseAnimationStream;
        if (state.camera.vdp_update != 0) effects.sound_requests.push_back(0x31);
        return effects;
    }

    if (actor.type == 0x4E) {
        if (state.player.vy < 0
            || state.player.animation_selector.animation_gate != 0) {
            return effects;
        }
        state.player.vx = actor.facing_x_flip != 0 ? -0x0700 : 0x0700;
        state.player.vy = -0x0700;
        state.player.terrain_response_active = 0xFF;
        state.player.animation_selector.response_active = 0xFF;
        state.player.terrain_vertical_stop = 0;
        actor.type = kActorTerminalType;
        actor.animation_pc = kType4EActorAnimationStream;
        actor.animation_timer = 0;
        effects.player_animation_stream = kPlayerResponseAnimationStream;
        state.random.value = state.random.value * 13U + 7U;
        if (state.camera.vdp_update != 0) {
            const std::uint16_t random_value = static_cast<std::uint16_t>(
                state.random.value ^ (state.random.value >> 16));
            effects.sound_requests.push_back(
                (random_value & 1U) != 0 ? 0x47 : 0x46);
        }
        return effects;
    }

    if (actor.type == 0x4F) {
        if (state.player.vy < 0
            || state.player.animation_selector.animation_gate != 0) {
            return effects;
        }
        const int actor_x = static_cast<int>(actor.x);
        const int player_world_x = static_cast<int>(static_cast<std::uint16_t>(
            state.camera.x + state.player.x));
        if (player_world_x > actor_x + 0x18
            || player_world_x <= actor_x - 0x18) {
            return effects;
        }
        state.player.vy = -0x0800;
        state.player.terrain_response_active = 0xFF;
        state.player.animation_selector.response_active = 0xFF;
        state.player.terrain_vertical_stop = 0;
        effects.player_animation_stream = kPlayerResponseAnimationStream;
        actor.type = kActorTerminalType;
        actor.animation_pc = kType4FActorAnimationStream;
        actor.animation_timer = 0;
        if (state.camera.vdp_update != 0) effects.sound_requests.push_back(0x4B);
        return effects;
    }
    return effects;
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

    for (const PlayerActorCollision& collision : detect_player_actor(state, input)) {
        // Type 0x65 is handled at the post-motion boundary below. The
        // ordinary player collision pass must not consume it early.
        if (state.actors[collision.actor].type == kActorBounceType) continue;
        CollisionEffects handler_effects = dispatch_player_handler(
            state,
            collision,
            input
        );
        effects.player_collision_interaction_pending =
            effects.player_collision_interaction_pending
            || handler_effects.player_collision_interaction_pending;
        if (handler_effects.player_animation_stream) {
            effects.player_animation_stream = handler_effects.player_animation_stream;
        }
        effects.sound_requests.insert(
            effects.sound_requests.end(),
            handler_effects.sound_requests.begin(),
            handler_effects.sound_requests.end()
        );
        effects.unhandled_player_collision_types.insert(
            effects.unhandled_player_collision_types.end(),
            handler_effects.unhandled_player_collision_types.begin(),
            handler_effects.unhandled_player_collision_types.end()
        );
    }
    return effects;
}

CollisionEffects CollisionSystem::bounce_player_actor(
    GameState& state,
    const PlayerCollisionInput& input
) {
    CollisionEffects effects;
    if (rom_ == nullptr) return effects;

    // Use the same 1..24 gameplay scan and geometry as the common player
    // collision pass, but dispatch the Type-0x65 handler only after motion
    // integration has published the falling position.
    for (const PlayerActorCollision& collision : detect_player_actor(state, input)) {
        if (collision.actor >= state.actors.size()
            || state.actors[collision.actor].type != kActorBounceType) {
            continue;
        }
        return dispatch_player_handler(state, collision, input);
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
    // FUN_001ABD7E scans auxiliary sources 25..31 and targets records 1..24.
    for (ActorIndex source_slot = 25; source_slot <= 31; ++source_slot) {
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
