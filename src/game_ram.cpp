#include "game_ram.hpp"

#include "animation.hpp"
#include "game_state.hpp"

namespace openaladdin {
namespace {

std::uint16_t as_u16(int value) {
    return static_cast<std::uint16_t>(value);
}

std::int16_t as_i16(std::uint16_t value) {
    return static_cast<std::int16_t>(value);
}

}  // namespace

void GameRamView::bind_state(GameState& state) {
    state_ = &state;
    store_ = &state.ram;
}

void GameRamView::bind_context(const AnimationContext& context) {
    context_ = &context;
    if (context.state != nullptr) {
        state_ = context.state;
        store_ = &context.state->ram;
    }
    // A command write is an actual state mutation. It must be visible to
    // subsequent commands even though the context itself is an input view.
    // Overlay bytes provide that read-after-write behavior without mutating a
    // caller-owned AnimationContext.
    context_overrides_.clear();
}

std::uint32_t* GameRamView::random_state() {
    return state_ == nullptr ? nullptr : &state_->random.value;
}

void GameRamView::clear_context() {
    context_ = nullptr;
    context_overrides_.clear();
}

void GameRamView::reset() {
    // The sparse backing belongs to GameState and is cleared only when the
    // game itself resets. Resetting one VM must not erase another VM's RAM.
    context_overrides_.clear();
    pending_writes_.clear();
    tracking_writes_ = false;
    context_ = nullptr;
}

bool GameRamView::is_typed_address(RamAddress address) {
    switch (address) {
    case 0xFF7DFA: case 0xFF7DFB:
    case 0xFF7DFC: case 0xFF7DFD:
    case 0xFF7E00: case 0xFF7E01:
    case 0xFF7E02: case 0xFF7E03:
    case 0xFF7E04: case 0xFF7E05:
    case 0xFF7E1E:
    case 0xFF7E25:
    case 0xFF7E3C:
    case 0xFF7E3F:
    case 0xFF7E28:
    case 0xFF7E58: case 0xFF7E59:
    case 0xFF7E5A: case 0xFF7E5B:
    case 0xFFF07C: case 0xFFF07D:
    case 0xFFF07E: case 0xFFF07F:
    case 0xFFF0B0: case 0xFFF0B1:
    case 0xFFF0BE:
    case 0xFFF0C0:
    case 0xFFF0C1:
    case 0xFFF0C3:
    case 0xFFF0CC:
    case 0xFFF0CD:
    case 0xFFF0D0:
    case 0xFFF0D2:
    case 0xFFF0D4:
    case 0xFFF0D7:
    case 0xFFF0DB:
    case 0xFFF0DE:
    case 0xFFF0DF:
    case 0xFFF0E6:
    case 0xFFF0E7:
    case 0xFFF0E9:
    case 0xFFF0EB:
    case 0xFFF0ED:
    case 0xFFF0EE:
    case 0xFFF0EF:
    case 0xFFF0F0:
    case 0xFFF0F2:
    case 0xFFF101:
    case 0xFFF115:
    case 0xFFF114:
    case 0xFFF15A:
    case 0xFFF11F:
    case 0xFFF173:
    case 0xFFF177: case 0xFFF178:
    case 0xFFF57D:
    case 0xFFF0EC:
    case 0xFFEFFA:
    case 0xFFEFFB:
    case 0xFFEFFF:
    case 0xFFEFE0: case 0xFFEFE1:
    case 0xFFEFE2: case 0xFFEFE3:
        return true;
    default:
        return false;
    }
}

std::uint8_t GameRamView::read_typed8(RamAddress address, bool& handled) const {
    handled = is_typed_address(address) && state_ != nullptr;
    if (!handled) return 0;

    const auto& player = state_->player;
    const auto& selector = player.animation_selector;
    const auto selector_u8 = [&](std::uint8_t AnimationSelectorState::*member) {
        return selector.*member;
    };
    const auto context_u8 = [&](std::uint8_t AnimationSelectorState::*member) {
        return selector_u8(member);
    };

    switch (address) {
    case 0xFF7DFA: return static_cast<std::uint8_t>(as_u16(
        player.x) >> 8);
    case 0xFF7DFB: return static_cast<std::uint8_t>(as_u16(
        player.x));
    case 0xFF7DFC: return static_cast<std::uint8_t>(as_u16(
        player.y) >> 8);
    case 0xFF7DFD: return static_cast<std::uint8_t>(as_u16(
        player.y));
    case 0xFF7E00: {
        const int threshold = state_->camera.vertical_threshold;
        return static_cast<std::uint8_t>(as_u16(threshold) >> 8);
    }
    case 0xFF7E01: {
        const int threshold = state_->camera.vertical_threshold;
        return static_cast<std::uint8_t>(as_u16(threshold));
    }
    case 0xFF7E02: {
        const int world_x = state_->camera.x + player.x;
        return static_cast<std::uint8_t>(as_u16(world_x) >> 8);
    }
    case 0xFF7E03: {
        const int world_x = state_->camera.x + player.x;
        return static_cast<std::uint8_t>(as_u16(world_x));
    }
    case 0xFF7E04: {
        const int world_y = state_->camera.y + player.y;
        return static_cast<std::uint8_t>(as_u16(world_y) >> 8);
    }
    case 0xFF7E05: {
        const int world_y = state_->camera.y + player.y;
        return static_cast<std::uint8_t>(as_u16(world_y));
    }
    case 0xFF7E28: return state_->frame.phase;
    case 0xFF7E1E: return state_->frame.vblank_ready_latch;
    case 0xFF7E25: return state_->frame.frame_wait_latch;
    case 0xFF7E58: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        player.vx) >> 8);
    case 0xFF7E59: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        player.vx));
    case 0xFF7E5A: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        context_ != nullptr && context_->player_vy_override
            ? *context_->player_vy_override : player.vy) >> 8);
    case 0xFF7E5B: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        context_ != nullptr && context_->player_vy_override
            ? *context_->player_vy_override : player.vy));
    case 0xFFF07C: return player.terrain_push_right;
    case 0xFFF07D: return player.terrain_push_left;
    case 0xFFF07E: return player.terrain_push_up;
    case 0xFFF07F: return player.terrain_push_down;
    case 0xFFF0B0: return static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(player.terrain_horizontal_response) >> 8);
    case 0xFFF0B1: return static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(player.terrain_horizontal_response));
    case 0xFFF0BE: return player.terrain_response_active;
    case 0xFFF0C0: return player.terrain_vertical_stop;
    case 0xFFF0C1:
        if (context_ != nullptr && context_->landing_state_override) {
            return *context_->landing_state_override;
        }
        return player.terrain_landing_state != 0
            ? player.terrain_landing_state
            : (context_ != nullptr && context_->grounded_override && *context_->grounded_override
                ? 1 : 0);
    case 0xFFF0C3: return player.terrain_behavior;
    case 0xFFF0CC:
        return context_ != nullptr && context_->response_timer_override
            ? *context_->response_timer_override : player.terrain_response_timer_state;
    case 0xFFF0CD: return context_u8(&AnimationSelectorState::transition_mode);
    case 0xFFF0D0: return player.terrain_transition_gate;
    case 0xFFF0D2: return context_u8(&AnimationSelectorState::transition_flag);
    case 0xFFF0D4: return context_u8(&AnimationSelectorState::transition_response);
    case 0xFFF0D7: return context_u8(&AnimationSelectorState::transition_lock);
    case 0xFFF0DB: return context_u8(&AnimationSelectorState::transition_state);
    case 0xFFF0DE: return context_u8(&AnimationSelectorState::transition_state_de);
    case 0xFFF0DF: return context_u8(&AnimationSelectorState::transition_state_df);
    case 0xFFF0E6: return player.terrain_terminal_transition;
    case 0xFFF0E7: return context_u8(&AnimationSelectorState::animation_gate);
    case 0xFFF0E9: return context_u8(&AnimationSelectorState::scene_script_countdown);
    case 0xFFF0EB: return player.terrain_bounce_animation_state;
    case 0xFFF0ED: return context_u8(&AnimationSelectorState::response_animation);
    case 0xFFF0EE: return context_u8(&AnimationSelectorState::response_state_ee);
    case 0xFFF0EF: return context_u8(&AnimationSelectorState::response_state_ef);
    case 0xFFF0F0: return context_u8(&AnimationSelectorState::response_state_f0);
    case 0xFFF0F2:
        return context_ != nullptr && context_->interaction_lock_override
            ? *context_->interaction_lock_override
            : context_u8(&AnimationSelectorState::interaction_lock);
    case 0xFFF101: return context_u8(&AnimationSelectorState::response_state_101);
    case 0xFFF115: return player.terrain_response_latch;
    case 0xFFF114: return state_->scene.resource_completion;
    case 0xFFF15A: return state_->scene.resource_mode;
    case 0xFFF11F: return context_u8(&AnimationSelectorState::state_lock);
    case 0xFFF173: return static_cast<std::uint8_t>(state_->camera.special_mode);
    case 0xFFF177: return state_->interaction_state.type3e_response_latch;
    case 0xFFF178: return state_->interaction_state.type3f_response_latch;
    case 0xFFF57D: return static_cast<std::uint8_t>(state_->camera.vdp_update);
    case 0xFFF0EC: return state_->interaction_state.target_current;
    case 0xFFEFFA: return state_->interaction_state.response_current;
    case 0xFFEFFB: return state_->interaction_state.response_pending;
    case 0xFFEFFF: return context_u8(&AnimationSelectorState::interaction_pending);
    case 0xFF7E3C: return state_->progress.difficulty_counter;
    case 0xFF7E3F: return state_->progress.active_scene_entry_gate;
    case 0xFFEFE0:
        return static_cast<std::uint8_t>(state_->interaction_state.primary_digits >> 8);
    case 0xFFEFE1:
        return static_cast<std::uint8_t>(state_->interaction_state.primary_digits);
    case 0xFFEFE2:
        return static_cast<std::uint8_t>(state_->interaction_state.secondary_digits >> 8);
    case 0xFFEFE3:
        return static_cast<std::uint8_t>(state_->interaction_state.secondary_digits);
    default: return 0;
    }
}

void GameRamView::write_typed8(RamAddress address, std::uint8_t value, bool& handled) {
    handled = is_typed_address(address) && state_ != nullptr;
    if (!handled) return;

    auto& player = state_->player;
    auto& selector = player.animation_selector;
    const auto update_word = [value](std::uint16_t& target, RamAddress address, RamAddress base) {
        if (address == base) target = static_cast<std::uint16_t>((target & 0x00FF) | (value << 8));
        else target = static_cast<std::uint16_t>((target & 0xFF00) | value);
    };
    const auto update_i16 = [&](std::int16_t& target, RamAddress base) {
        auto word = static_cast<std::uint16_t>(target);
        update_word(word, address, base);
        target = as_i16(word);
    };
    const auto update_selector_u8 = [&](std::uint8_t AnimationSelectorState::*member) {
        selector.*member = value;
    };

    switch (address) {
    case 0xFF7DFA: {
        auto word = as_u16(player.x); update_word(word, address, 0xFF7DFA); player.x = as_i16(word); return;
    }
    case 0xFF7DFB: {
        auto word = as_u16(player.x); update_word(word, address, 0xFF7DFA); player.x = as_i16(word); return;
    }
    case 0xFF7DFC: {
        auto word = as_u16(player.y); update_word(word, address, 0xFF7DFC); player.y = as_i16(word); return;
    }
    case 0xFF7DFD: {
        auto word = as_u16(player.y); update_word(word, address, 0xFF7DFC); player.y = as_i16(word); return;
    }
    case 0xFF7E00: {
        auto word = as_u16(state_->camera.vertical_threshold); update_word(word, address, 0xFF7E00); state_->camera.vertical_threshold = word; return;
    }
    case 0xFF7E01: {
        auto word = as_u16(state_->camera.vertical_threshold); update_word(word, address, 0xFF7E00); state_->camera.vertical_threshold = word; return;
    }
    case 0xFF7E28: state_->frame.phase = value; return;
    case 0xFF7E1E: state_->frame.vblank_ready_latch = value; return;
    case 0xFF7E25: state_->frame.frame_wait_latch = value; return;
    case 0xFF7E3C: state_->progress.difficulty_counter = value; return;
    case 0xFF7E3F: state_->progress.active_scene_entry_gate = value; return;
    case 0xFF7E58: {
        auto word = static_cast<std::uint16_t>(player.vx); update_word(word, address, 0xFF7E58); player.vx = as_i16(word); return;
    }
    case 0xFF7E59: {
        auto word = static_cast<std::uint16_t>(player.vx); update_word(word, address, 0xFF7E58); player.vx = as_i16(word); return;
    }
    case 0xFF7E5A: {
        auto word = static_cast<std::uint16_t>(player.vy); update_word(word, address, 0xFF7E5A); player.vy = as_i16(word); return;
    }
    case 0xFF7E5B: {
        auto word = static_cast<std::uint16_t>(player.vy); update_word(word, address, 0xFF7E5A); player.vy = as_i16(word); return;
    }
    case 0xFFF07C: player.terrain_push_right = value; return;
    case 0xFFF07D: player.terrain_push_left = value; return;
    case 0xFFF07E: player.terrain_push_up = value; return;
    case 0xFFF07F: player.terrain_push_down = value; return;
    case 0xFFF0B0: case 0xFFF0B1:
        update_i16(player.terrain_horizontal_response, 0xFFF0B0);
        selector.horizontal_response = player.terrain_horizontal_response;
        return;
    case 0xFFF0BE:
        player.terrain_response_active = value;
        update_selector_u8(&AnimationSelectorState::response_active);
        return;
    case 0xFFF0C0: player.terrain_vertical_stop = value; return;
    case 0xFFF0C1: player.terrain_landing_state = value; selector.landing_state = value; return;
    case 0xFFF0C3: player.terrain_behavior = value; return;
    case 0xFFF0CC:
        player.terrain_response_timer_state = value;
        update_selector_u8(&AnimationSelectorState::response_timer);
        return;
    case 0xFFF0CD: update_selector_u8(&AnimationSelectorState::transition_mode); return;
    case 0xFFF0D0:
        player.terrain_transition_gate = value;
        update_selector_u8(&AnimationSelectorState::transition_gate);
        return;
    case 0xFFF0D2: update_selector_u8(&AnimationSelectorState::transition_flag); return;
    case 0xFFF0D4: update_selector_u8(&AnimationSelectorState::transition_response); return;
    case 0xFFF0D7: update_selector_u8(&AnimationSelectorState::transition_lock); return;
    case 0xFFF0DB: update_selector_u8(&AnimationSelectorState::transition_state); return;
    case 0xFFF0DE: update_selector_u8(&AnimationSelectorState::transition_state_de); return;
    case 0xFFF0DF: update_selector_u8(&AnimationSelectorState::transition_state_df); return;
    case 0xFFF0E6:
        player.terrain_terminal_transition = value;
        update_selector_u8(&AnimationSelectorState::terminal_transition);
        return;
    case 0xFFF0E7: update_selector_u8(&AnimationSelectorState::animation_gate); return;
    case 0xFFF0E9: update_selector_u8(&AnimationSelectorState::scene_script_countdown); return;
    case 0xFFF0EB: player.terrain_bounce_animation_state = value; return;
    case 0xFFF0ED: update_selector_u8(&AnimationSelectorState::response_animation); return;
    case 0xFFF0EE: update_selector_u8(&AnimationSelectorState::response_state_ee); return;
    case 0xFFF0EF: update_selector_u8(&AnimationSelectorState::response_state_ef); return;
    case 0xFFF0F0: update_selector_u8(&AnimationSelectorState::response_state_f0); return;
    case 0xFFF0F2: update_selector_u8(&AnimationSelectorState::interaction_lock); return;
    case 0xFFF101: update_selector_u8(&AnimationSelectorState::response_state_101); return;
    case 0xFFF115:
        player.terrain_response_latch = value;
        update_selector_u8(&AnimationSelectorState::response_latch);
        return;
    case 0xFFF11F: update_selector_u8(&AnimationSelectorState::state_lock); return;
    case 0xFFF114: state_->scene.resource_completion = value; return;
    case 0xFFF15A: state_->scene.resource_mode = value; return;
    case 0xFFF173:
        state_->camera.special_mode = value;
        update_selector_u8(&AnimationSelectorState::camera_special_mode);
        return;
    case 0xFFF177: state_->interaction_state.type3e_response_latch = value; return;
    case 0xFFF178: state_->interaction_state.type3f_response_latch = value; return;
    case 0xFFF57D: state_->camera.vdp_update = value; return;
    case 0xFFF0EC: state_->interaction_state.target_current = value; return;
    case 0xFFEFFA: state_->interaction_state.response_current = value; return;
    case 0xFFEFFB: state_->interaction_state.response_pending = value; return;
    case 0xFFEFFF: update_selector_u8(&AnimationSelectorState::interaction_pending); return;
    case 0xFFEFE0:
    case 0xFFEFE1:
        update_word(state_->interaction_state.primary_digits, address, 0xFFEFE0);
        return;
    case 0xFFEFE2:
    case 0xFFEFE3:
        update_word(state_->interaction_state.secondary_digits, address, 0xFFEFE2);
        return;
    default: return;
    }
}

std::uint8_t GameRamView::read_sparse8(RamAddress address) const {
    if (store_ == nullptr) return 0;
    const auto found = store_->sparse_memory_.find(address);
    return found == store_->sparse_memory_.end() ? 0 : found->second;
}

std::uint8_t GameRamView::read_actor8(RamAddress address, bool& handled) const {
    handled = actor_state_ != nullptr && address < 0x42;
    if (!handled) return 0;

    const ActorState& actor = *actor_state_;
    const auto byte_u16 = [](std::uint16_t value, RamAddress base, RamAddress address) {
        return static_cast<std::uint8_t>(
            address == base ? value >> 8 : value);
    };
    const auto byte_u32 = [](std::uint32_t value, RamAddress base, RamAddress address) {
        const unsigned shift = static_cast<unsigned>((3 - (address - base)) * 8);
        return static_cast<std::uint8_t>(value >> shift);
    };

    switch (address) {
    case 0x00: return actor.type;
    case 0x02: case 0x03:
        return byte_u16(actor.x, 0x02, address);
    case 0x04: case 0x05:
        return byte_u16(actor.y, 0x04, address);
    case 0x06: return actor.movement_flags;
    case 0x07: return actor.runtime_field_07;
    case 0x08: return actor.runtime_field_07_delay;
    case 0x09: return actor.facing_x_flip;
    case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        return byte_u32(actor.movement_pc, 0x0A, address);
    case 0x0E: case 0x0F: case 0x10: case 0x11:
        return byte_u32(actor.movement_loop_pc, 0x0E, address);
    case 0x12: return actor.movement_loop_timer;
    case 0x14: case 0x15: case 0x16: case 0x17:
        return byte_u32(actor.frame_ptr, 0x14, address);
    case 0x18: case 0x19:
        return byte_u16(static_cast<std::uint16_t>(actor.movement_word_18), 0x18, address);
    case 0x1A: case 0x1B:
        return byte_u16(static_cast<std::uint16_t>(actor.movement_word_1a), 0x1A, address);
    case 0x1E: case 0x1F:
        return byte_u16(actor.sprite_attribute, 0x1E, address);
    case 0x20: case 0x21: case 0x22: case 0x23:
        return byte_u32(actor.animation_pc, 0x20, address);
    case 0x35: return actor.facing_y_flip;
    case 0x36: return actor.movement_command_timer;
    case 0x37: return actor.animation_timer;
    case 0x38: case 0x39: case 0x3A: case 0x3B:
        return byte_u32(actor.movement_return_pc, 0x38, address);
    case 0x3C: return actor.flags;
    case 0x3D: return actor.interaction_state;
    case 0x3E: case 0x3F: case 0x40: case 0x41:
        return byte_u32(static_cast<std::uint32_t>(actor.linked_actor_slot), 0x3E, address);
    default:
        handled = false;
        return 0;
    }
}

void GameRamView::write_actor8(
    RamAddress address,
    std::uint8_t value,
    bool& handled
) {
    handled = actor_state_ != nullptr && address < 0x42;
    if (!handled) return;

    auto& actor = *actor_state_;
    const auto update_u16 = [value](std::uint16_t& target, RamAddress base, RamAddress address) {
        if (address == base) {
            target = static_cast<std::uint16_t>((target & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8));
        } else {
            target = static_cast<std::uint16_t>((target & 0xFF00U) | value);
        }
    };
    const auto update_u32 = [value](std::uint32_t& target, RamAddress base, RamAddress address) {
        const unsigned shift = static_cast<unsigned>((3 - (address - base)) * 8);
        const std::uint32_t mask = 0xFFU << shift;
        target = (target & ~mask) | (static_cast<std::uint32_t>(value) << shift);
    };
    const auto update_i16 = [&](std::int16_t& target, RamAddress base) {
        auto value_u16 = static_cast<std::uint16_t>(target);
        update_u16(value_u16, base, address);
        target = static_cast<std::int16_t>(value_u16);
    };

    switch (address) {
    case 0x00: actor.type = value; return;
    case 0x02: case 0x03: update_u16(actor.x, 0x02, address); return;
    case 0x04: case 0x05: update_u16(actor.y, 0x04, address); return;
    case 0x06: actor.movement_flags = value; return;
    case 0x07: actor.runtime_field_07 = value; return;
    case 0x08: actor.runtime_field_07_delay = value; return;
    case 0x09: actor.facing_x_flip = value; return;
    case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        update_u32(actor.movement_pc, 0x0A, address); return;
    case 0x0E: case 0x0F: case 0x10: case 0x11:
        update_u32(actor.movement_loop_pc, 0x0E, address); return;
    case 0x12: actor.movement_loop_timer = value; return;
    case 0x14: case 0x15: case 0x16: case 0x17:
        update_u32(actor.frame_ptr, 0x14, address); return;
    case 0x18: case 0x19: update_i16(actor.movement_word_18, 0x18); return;
    case 0x1A: case 0x1B: update_i16(actor.movement_word_1a, 0x1A); return;
    case 0x1E: case 0x1F: update_u16(actor.sprite_attribute, 0x1E, address); return;
    case 0x20: case 0x21: case 0x22: case 0x23:
        update_u32(actor.animation_pc, 0x20, address); return;
    case 0x35: actor.facing_y_flip = value; return;
    case 0x36: actor.movement_command_timer = value; return;
    case 0x37: actor.animation_timer = value; return;
    case 0x38: case 0x39: case 0x3A: case 0x3B:
        update_u32(actor.movement_return_pc, 0x38, address); return;
    case 0x3C: actor.flags = value; return;
    case 0x3D: actor.interaction_state = value; return;
    case 0x3E: case 0x3F: case 0x40: case 0x41: {
        auto linked = static_cast<std::uint32_t>(actor.linked_actor_slot);
        update_u32(linked, 0x3E, address);
        actor.linked_actor_slot = static_cast<std::int32_t>(linked);
        return;
    }
    default:
        handled = false;
        return;
    }
}

std::array<std::uint8_t, 0x42> GameRamView::actor_record_snapshot() const {
    std::array<std::uint8_t, 0x42> result{};
    for (RamAddress address = 0; address < result.size(); ++address) {
        bool handled = false;
        result[address] = read_actor8(address, handled);
        if (handled) continue;
        if (actor_record_ != nullptr) {
            result[address] = (*actor_record_)[address];
        } else {
            result[address] = read_sparse8(address);
        }
    }
    return result;
}

std::array<std::uint8_t, 0x42> GameRamView::actor_record() const {
    return actor_record_snapshot();
}

std::uint8_t GameRamView::read8(RamAddress address) const {
    if (address < 0x42) {
        bool handled = false;
        const auto actor = read_actor8(address, handled);
        if (handled) return actor;
        if (actor_record_ != nullptr) return (*actor_record_)[address];
    }
    const auto override = context_overrides_.find(address);
    if (override != context_overrides_.end()) return override->second;
    bool handled = false;
    const auto typed = read_typed8(address, handled);
    if (handled) return typed;
    return read_sparse8(address);
}

std::uint16_t GameRamView::read16(RamAddress address) const {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(read8(address)) << 8) | read8(address + 1));
}

std::uint32_t GameRamView::read32(RamAddress address) const {
    return (static_cast<std::uint32_t>(read8(address)) << 24)
        | (static_cast<std::uint32_t>(read8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(read8(address + 2)) << 8)
        | read8(address + 3);
}

void GameRamView::write8(RamAddress address, std::uint8_t value) {
    bool handled = false;
    if (address < 0x42) {
        write_actor8(address, value, handled);
    } else {
        write_typed8(address, value, handled);
    }
    if (!handled) {
        if (actor_record_ != nullptr && address < actor_record_->size()) {
            (*actor_record_)[address] = value;
        } else if (store_ != nullptr) {
            store_->sparse_memory_[address] = value;
        }
    } else if (context_ != nullptr) {
        // Keep command-local read-after-write semantics for typed addresses.
        context_overrides_[address] = value;
    }
    if (tracking_writes_) pending_writes_[address] = 1;
}

void GameRamView::write16(RamAddress address, std::uint16_t value) {
    write8(address, static_cast<std::uint8_t>(value >> 8));
    write8(address + 1, static_cast<std::uint8_t>(value));
}

void GameRamView::write32(RamAddress address, std::uint32_t value) {
    write8(address, static_cast<std::uint8_t>(value >> 24));
    write8(address + 1, static_cast<std::uint8_t>(value >> 16));
    write8(address + 2, static_cast<std::uint8_t>(value >> 8));
    write8(address + 3, static_cast<std::uint8_t>(value));
}

bool GameRamView::take_write(RamAddress address, std::uint8_t& value) {
    const auto found = pending_writes_.find(address);
    if (found == pending_writes_.end()) return false;
    pending_writes_.erase(found);
    value = read8(address);
    return true;
}

void GameRamView::copy_legacy_memory(std::array<std::uint8_t, 0x10000>& memory) const {
    for (std::size_t offset = 0; offset < memory.size(); ++offset) {
        memory[offset] = read8(0xFF0000U + static_cast<RamAddress>(offset));
    }
}

void GameRamView::copy_legacy_write_flags(
    std::array<std::uint8_t, 0x10000>& flags
) const {
    flags.fill(0);
    for (const auto& [address, value] : pending_writes_) {
        if (address >= 0xFF0000U && address <= 0xFFFFFFU) {
            flags[address - 0xFF0000U] = value;
        }
    }
}

void GameRamView::restore_legacy_memory(
    const std::array<std::uint8_t, 0x10000>& memory,
    const std::array<std::uint8_t, 0x10000>& flags
) {
    if (store_ != nullptr) store_->sparse_memory_.clear();
    context_overrides_.clear();
    pending_writes_.clear();
    for (std::size_t offset = 0; offset < memory.size(); ++offset) {
        const RamAddress address = 0xFF0000U + static_cast<RamAddress>(offset);
        // Typed state was restored by GameState's checkpoint section. Do not
        // let the legacy mirror overwrite that authoritative representation.
        if (!is_typed_address(address) && memory[offset] != 0 && store_ != nullptr) {
            store_->sparse_memory_[address] = memory[offset];
        }
        if (flags[offset] != 0) pending_writes_[address] = flags[offset];
    }
}

}  // namespace openaladdin
