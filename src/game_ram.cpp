#include "game_ram.hpp"

#include "animation.hpp"
#include "game_state.hpp"

#include <algorithm>

namespace openaladdin {
namespace {

std::uint16_t as_u16(int value) {
    return static_cast<std::uint16_t>(value);
}

std::int16_t as_i16(std::uint16_t value) {
    return static_cast<std::int16_t>(value);
}

}  // namespace

void GameRamView::bind_context(const AnimationContext& context) {
    context_ = &context;
    // A command write is an actual state mutation. It must be visible to
    // subsequent commands even though the context itself is an input view.
    // Overlay bytes provide that read-after-write behavior without mutating a
    // caller-owned AnimationContext.
    context_overrides_.clear();
}

void GameRamView::clear_context() {
    context_ = nullptr;
    context_overrides_.clear();
}

void GameRamView::reset() {
    sparse_memory_.clear();
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
    case 0xFF7E58: case 0xFF7E59:
    case 0xFF7E5A: case 0xFF7E5B:
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
    case 0xFFF0ED:
    case 0xFFF0EE:
    case 0xFFF0EF:
    case 0xFFF0F0:
    case 0xFFF0F2:
    case 0xFFF101:
    case 0xFFF115:
    case 0xFFF11F:
    case 0xFFF173:
    case 0xFFEFFF:
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
    const auto context_u8 = [&](std::uint8_t AnimationSelectorState::*member) {
        return context_ != nullptr ? context_->selector.*member : selector.*member;
    };
    const auto context_i16 = [&](std::int16_t AnimationSelectorState::*member) {
        return context_ != nullptr ? context_->selector.*member : selector.*member;
    };

    switch (address) {
    case 0xFF7DFA: return static_cast<std::uint8_t>(as_u16(
        context_ != nullptr ? context_->player_x : player.x) >> 8);
    case 0xFF7DFB: return static_cast<std::uint8_t>(as_u16(
        context_ != nullptr ? context_->player_x : player.x));
    case 0xFF7DFC: return static_cast<std::uint8_t>(as_u16(
        context_ != nullptr ? context_->player_y : player.y) >> 8);
    case 0xFF7DFD: return static_cast<std::uint8_t>(as_u16(
        context_ != nullptr ? context_->player_y : player.y));
    case 0xFF7E00: {
        const int threshold = context_ != nullptr
            ? context_->camera_vertical_threshold : state_->camera.vertical_threshold;
        return static_cast<std::uint8_t>(as_u16(threshold) >> 8);
    }
    case 0xFF7E01: {
        const int threshold = context_ != nullptr
            ? context_->camera_vertical_threshold : state_->camera.vertical_threshold;
        return static_cast<std::uint8_t>(as_u16(threshold));
    }
    case 0xFF7E02: {
        const int world_x = context_ != nullptr
            ? context_->world_x : state_->camera.x + player.x;
        return static_cast<std::uint8_t>(as_u16(world_x) >> 8);
    }
    case 0xFF7E03: {
        const int world_x = context_ != nullptr
            ? context_->world_x : state_->camera.x + player.x;
        return static_cast<std::uint8_t>(as_u16(world_x));
    }
    case 0xFF7E04: {
        const int world_y = context_ != nullptr
            ? context_->world_y : state_->camera.y + player.y;
        return static_cast<std::uint8_t>(as_u16(world_y) >> 8);
    }
    case 0xFF7E05: {
        const int world_y = context_ != nullptr
            ? context_->world_y : state_->camera.y + player.y;
        return static_cast<std::uint8_t>(as_u16(world_y));
    }
    case 0xFF7E58: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        context_ != nullptr ? context_->player_vx : player.vx) >> 8);
    case 0xFF7E59: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        context_ != nullptr ? context_->player_vx : player.vx));
    case 0xFF7E5A: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        context_ != nullptr ? context_->player_vy : player.vy) >> 8);
    case 0xFF7E5B: return static_cast<std::uint8_t>(static_cast<std::uint16_t>(
        context_ != nullptr ? context_->player_vy : player.vy));
    case 0xFFF0B0: return static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(context_i16(&AnimationSelectorState::horizontal_response)) >> 8);
    case 0xFFF0B1: return static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(context_i16(&AnimationSelectorState::horizontal_response)));
    case 0xFFF0BE: return context_u8(&AnimationSelectorState::response_active);
    case 0xFFF0C0: return player.terrain_vertical_stop;
    case 0xFFF0C1:
        if (context_ != nullptr) {
            return context_->selector.landing_state != 0
                ? context_->selector.landing_state
                : context_->grounded ? 1 : 0;
        }
        return player.terrain_landing_state;
    case 0xFFF0C3: return context_ != nullptr ? context_->terrain_behavior : player.terrain_behavior;
    case 0xFFF0CC: return context_u8(&AnimationSelectorState::response_timer);
    case 0xFFF0CD: return context_u8(&AnimationSelectorState::transition_mode);
    case 0xFFF0D0: return context_u8(&AnimationSelectorState::transition_gate);
    case 0xFFF0D2: return context_u8(&AnimationSelectorState::transition_flag);
    case 0xFFF0D4: return context_u8(&AnimationSelectorState::transition_response);
    case 0xFFF0D7: return context_u8(&AnimationSelectorState::transition_lock);
    case 0xFFF0DB: return context_u8(&AnimationSelectorState::transition_state);
    case 0xFFF0DE: return context_u8(&AnimationSelectorState::transition_state_de);
    case 0xFFF0DF: return context_u8(&AnimationSelectorState::transition_state_df);
    case 0xFFF0E6: return context_u8(&AnimationSelectorState::terminal_transition);
    case 0xFFF0E7: return context_u8(&AnimationSelectorState::animation_gate);
    case 0xFFF0E9: return context_u8(&AnimationSelectorState::scene_script_countdown);
    case 0xFFF0ED: return context_u8(&AnimationSelectorState::response_animation);
    case 0xFFF0EE: return context_u8(&AnimationSelectorState::response_state_ee);
    case 0xFFF0EF: return context_u8(&AnimationSelectorState::response_state_ef);
    case 0xFFF0F0: return context_u8(&AnimationSelectorState::response_state_f0);
    case 0xFFF0F2: return context_u8(&AnimationSelectorState::interaction_lock);
    case 0xFFF101: return context_u8(&AnimationSelectorState::response_state_101);
    case 0xFFF115: return context_u8(&AnimationSelectorState::response_latch);
    case 0xFFF11F: return context_u8(&AnimationSelectorState::state_lock);
    case 0xFFF173: return context_u8(&AnimationSelectorState::camera_special_mode);
    case 0xFFEFFF: return context_u8(&AnimationSelectorState::interaction_pending);
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
    case 0xFFF0B0: case 0xFFF0B1: update_i16(selector.horizontal_response, 0xFFF0B0); return;
    case 0xFFF0BE: update_selector_u8(&AnimationSelectorState::response_active); return;
    case 0xFFF0C0: player.terrain_vertical_stop = value; return;
    case 0xFFF0C1: player.terrain_landing_state = value; selector.landing_state = value; return;
    case 0xFFF0C3: player.terrain_behavior = value; return;
    case 0xFFF0CC: update_selector_u8(&AnimationSelectorState::response_timer); return;
    case 0xFFF0CD: update_selector_u8(&AnimationSelectorState::transition_mode); return;
    case 0xFFF0D0: player.terrain_transition_gate = value; update_selector_u8(&AnimationSelectorState::transition_gate); return;
    case 0xFFF0D2: update_selector_u8(&AnimationSelectorState::transition_flag); return;
    case 0xFFF0D4: update_selector_u8(&AnimationSelectorState::transition_response); return;
    case 0xFFF0D7: update_selector_u8(&AnimationSelectorState::transition_lock); return;
    case 0xFFF0DB: update_selector_u8(&AnimationSelectorState::transition_state); return;
    case 0xFFF0DE: update_selector_u8(&AnimationSelectorState::transition_state_de); return;
    case 0xFFF0DF: update_selector_u8(&AnimationSelectorState::transition_state_df); return;
    case 0xFFF0E6: update_selector_u8(&AnimationSelectorState::terminal_transition); return;
    case 0xFFF0E7: update_selector_u8(&AnimationSelectorState::animation_gate); return;
    case 0xFFF0E9: update_selector_u8(&AnimationSelectorState::scene_script_countdown); return;
    case 0xFFF0ED: update_selector_u8(&AnimationSelectorState::response_animation); return;
    case 0xFFF0EE: update_selector_u8(&AnimationSelectorState::response_state_ee); return;
    case 0xFFF0EF: update_selector_u8(&AnimationSelectorState::response_state_ef); return;
    case 0xFFF0F0: update_selector_u8(&AnimationSelectorState::response_state_f0); return;
    case 0xFFF0F2: update_selector_u8(&AnimationSelectorState::interaction_lock); return;
    case 0xFFF101: update_selector_u8(&AnimationSelectorState::response_state_101); return;
    case 0xFFF115: update_selector_u8(&AnimationSelectorState::response_latch); return;
    case 0xFFF11F: update_selector_u8(&AnimationSelectorState::state_lock); return;
    case 0xFFF173: update_selector_u8(&AnimationSelectorState::camera_special_mode); return;
    case 0xFFEFFF: update_selector_u8(&AnimationSelectorState::interaction_pending); return;
    default: return;
    }
}

std::uint8_t GameRamView::read_sparse8(RamAddress address) const {
    const auto found = sparse_memory_.find(address);
    return found == sparse_memory_.end() ? 0 : found->second;
}

std::uint8_t GameRamView::read8(RamAddress address) const {
    if (actor_record_ != nullptr && address < actor_record_->size()) {
        return (*actor_record_)[address];
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
    write_typed8(address, value, handled);
    if (!handled) {
        if (actor_record_ != nullptr && address < actor_record_->size()) {
            (*actor_record_)[address] = value;
        } else {
            sparse_memory_[address] = value;
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
    sparse_memory_.clear();
    context_overrides_.clear();
    pending_writes_.clear();
    for (std::size_t offset = 0; offset < memory.size(); ++offset) {
        const RamAddress address = 0xFF0000U + static_cast<RamAddress>(offset);
        // Typed state was restored by GameState's checkpoint section. Do not
        // let the legacy mirror overwrite that authoritative representation.
        if (!is_typed_address(address) && memory[offset] != 0) {
            sparse_memory_[address] = memory[offset];
        }
        if (flags[offset] != 0) pending_writes_[address] = flags[offset];
    }
}

}  // namespace openaladdin
