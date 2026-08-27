#include "animation.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace openaladdin {
namespace {

// Player_ProcessInteractionState dispatches this fixed event through the
// shared 68K-to-Z80 audio path at 0x001AE5A6.
constexpr std::uint8_t kInteractionEventSoundId = 0x31;

constexpr std::uint32_t kIdleStream = 0x00121D9A;
constexpr std::uint32_t kRunStream = 0x00122006;
constexpr std::uint32_t kBrakeStream = 0x001232E0;
constexpr std::uint32_t kJumpStream = 0x001221B0;
constexpr std::uint32_t kJumpTimedStream = 0x0012214E;
constexpr std::uint32_t kLandingStream = 0x00121F84;
constexpr std::uint32_t kResponseStream = 0x00121FA6;
constexpr std::uint32_t kResponseRecoveryStream = 0x00121FA0;
constexpr std::uint32_t kResponseStreamEnd = 0x00121FD4;
constexpr std::uint32_t kInteractionStopStream = 0x001226CE;
constexpr std::uint32_t kInteractionSpecialStream = 0x001226B2;

const PlayerAnimationVm::Clip kIdleClip{
    kIdleStream,
    {{201, 34}, {282, 2}, {283, 4}, {284, 60},
     {283, 2}, {285, 4}, {286, 4}, {287, 43}},
    true,
    0,
};

const PlayerAnimationVm::Clip kRunClip{
    kRunStream,
    {{201, 4}, {202, 4}, {203, 4}, {204, 4},
     {205, 4}, {206, 2}, {207, 4}, {208, 4},
     {209, 4}, {210, 4}, {211, 2}, {212, 4},
     {213, 4}, {214, 4}, {205, 4}, {206, 2},
     {207, 4}, {208, 4}, {209, 4}, {210, 4},
     {211, 2}, {212, 4}, {213, 4}, {214, 4}},
    true,
    14,
};

const PlayerAnimationVm::Clip kBrakeClip{
    kBrakeStream,
    {{233, 4}, {234, 4}, {235, 4}, {236, 4}, {237, 4},
     {238, 4}, {316, 4}, {317, 2}, {318, 4}, {319, 4}},
    false,
    0,
};

const PlayerAnimationVm::Clip kJumpClip{
    kJumpStream,
    {{161, 4}, {162, 4}, {163, 2}, {164, 2}, {165, 6}},
    false,
    0,
};

const PlayerAnimationVm::Clip kLandingClip{
    kLandingStream,
    {{171, 6}, {161, 6}},
    false,
    0,
};

std::vector<std::uint8_t> read_binary(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path);
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path);
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    if (!result.empty()) file.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!file) throw std::runtime_error("cannot read " + path);
    return result;
}

bool is_command(std::uint8_t value) { return value >= 0xEA && value <= 0xFE; }

bool is_response_root(std::uint32_t stream_entry) {
    return stream_entry == kResponseStream || stream_entry == kResponseRecoveryStream;
}

bool is_response_stream_cursor(std::uint32_t cursor) {
    // 0x121FA0 is the short recovery stream and 0x121FA6 is the extended
    // response/hurt loop. The latter advances through 0x121FD2 before the
    // next player stream begins at 0x121FD4, so a checkpoint can legitimately
    // contain any even cursor in this interval rather than only a root.
    // The bounce-pad callback writes the first data cursor (0x1221B8)
    // directly into the player record instead of writing the 0x121FA6 root.
    // Treat that observable cursor as part of the same response stream so a
    // state handoff does not get reclassified as an action/locomotion stream.
    return (cursor >= kResponseRecoveryStream && cursor < kResponseStreamEnd)
        || cursor == 0x001221B8;
}

std::uint16_t as_u16(int value) { return static_cast<std::uint16_t>(value & 0xFFFF); }

}  // namespace

const PlayerAnimationVm::Clip& PlayerAnimationVm::clip(SpritePose pose) {
    switch (pose) {
    case SpritePose::Idle: return kIdleClip;
    case SpritePose::Run: return kRunClip;
    case SpritePose::Brake: return kBrakeClip;
    case SpritePose::Jump: return kJumpClip;
    case SpritePose::Landing: return kLandingClip;
    }
    throw std::runtime_error("unknown player animation pose");
}

void PlayerAnimationVm::load_rom(const std::string& path) {
    rom_ = read_binary(path);
    if (rom_.size() < 0x20000) throw std::runtime_error("animation ROM is too small: " + path);
    rom_mode_ = true;
    reset();
}

void PlayerAnimationVm::reset() {
    pose_ = SpritePose::Idle;
    step_ = 0;
    timer_ = rom_mode_ ? 0 : clip(pose_).steps.front().duration;
    facing_left_ = false;
    horizontal_direction_ = HorizontalDirection::None;
    stream_kind_ = AnimationStreamKind::Locomotion;
    animation_pc_ = rom_mode_ ? kIdleStream : 0;
    frame_pointer_ = 0;
    stream_entry_ = rom_mode_ ? kIdleStream : clip(pose_).stream_entry;
    return_pc_ = 0;
    // The engine supplies the shared ROM RNG through AnimationContext. Keep
    // the standalone VM deterministic when it is used without an engine.
    random_value_ = 0x00;
    actor_tick_ = false;
    animation_phase_delay_ = 0;
    pending_animation_pc_ = 0;
    force_tick_after_service_ = false;
    force_tick_next_update_ = false;
    force_tick_without_phase_ = false;
    defer_tick_next_update_ = false;
    tracking_memory_writes_ = false;
    spawn_requests_.clear();
    sound_requests_.clear();
    update_count_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = false;
    memory_.fill(0);
    memory_write_flags_.fill(0);
    actor_.fill(0);
    actor_[0] = 1;
}

void PlayerAnimationVm::select(SpritePose pose) {
    pose_ = pose;
    step_ = 0;
    timer_ = clip(pose_).steps.front().duration;
}

void PlayerAnimationVm::select_rom_stream(
    SpritePose pose,
    bool execute_now,
    const AnimationContext* context
) {
    pose_ = pose;
    stream_kind_ = AnimationStreamKind::Locomotion;
    // Player_HandleJumpAndVerticalState selects the alternate jump stream
    // when the terrain response timer is already armed. This is common after
    // a grounded run and is distinct from the ordinary 0x1221B0 jump root.
    stream_entry_ = pose == SpritePose::Jump
        && context != nullptr
        && (context->terrain_response_timer_state != 0
            || context->selector.response_timer != 0)
        ? kJumpTimedStream
        : clip(pose).stream_entry;
    animation_pc_ = stream_entry_;
    timer_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = pose == SpritePose::Landing;
    if (execute_now) tick_rom({});
}

std::uint8_t PlayerAnimationVm::read_rom8(std::uint32_t address) const {
    if (address >= rom_.size()) throw std::runtime_error("animation VM read outside ROM");
    return rom_[address];
}

std::uint16_t PlayerAnimationVm::read_rom16(std::uint32_t address) const {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(read_rom8(address)) << 8) | read_rom8(address + 1));
}

std::uint32_t PlayerAnimationVm::read_rom32(std::uint32_t address) const {
    return (static_cast<std::uint32_t>(read_rom8(address)) << 24)
        | (static_cast<std::uint32_t>(read_rom8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(read_rom8(address + 2)) << 8)
        | read_rom8(address + 3);
}

std::uint8_t PlayerAnimationVm::read_memory8(std::uint32_t address) const {
    if (address < actor_.size()) return actor_[address];
    if (address >= 0xFF0000 && address <= 0xFFFFFF) return memory_[address - 0xFF0000];
    return 0;
}

std::uint16_t PlayerAnimationVm::read_memory16(std::uint32_t address) const {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(read_memory8(address)) << 8) | read_memory8(address + 1));
}

std::uint32_t PlayerAnimationVm::read_memory32(std::uint32_t address) const {
    return (static_cast<std::uint32_t>(read_memory8(address)) << 24)
        | (static_cast<std::uint32_t>(read_memory8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(read_memory8(address + 2)) << 8)
        | read_memory8(address + 3);
}

void PlayerAnimationVm::write_memory8(std::uint32_t address, std::uint8_t value) {
    if (address < actor_.size()) actor_[address] = value;
    else if (address >= 0xFF0000 && address <= 0xFFFFFF) {
        const auto offset = address - 0xFF0000;
        memory_[offset] = value;
        if (tracking_memory_writes_) memory_write_flags_[offset] = 1;
    }
}

void PlayerAnimationVm::write_memory16(std::uint32_t address, std::uint16_t value) {
    write_memory8(address, static_cast<std::uint8_t>(value >> 8));
    write_memory8(address + 1, static_cast<std::uint8_t>(value));
}

void PlayerAnimationVm::write_memory32(std::uint32_t address, std::uint32_t value) {
    write_memory8(address, static_cast<std::uint8_t>(value >> 24));
    write_memory8(address + 1, static_cast<std::uint8_t>(value >> 16));
    write_memory8(address + 2, static_cast<std::uint8_t>(value >> 8));
    write_memory8(address + 3, static_cast<std::uint8_t>(value));
}

void PlayerAnimationVm::sync_selector_context(
    const AnimationSelectorState& selector,
    bool grounded
) {
    // These bytes are inputs to the player stream selector, not VM-local
    // scratch. Synchronize the complete selector surface at every actor
    // boundary so a previous response cannot leak into a later idle/run
    // decision. The fallback values retain the historical checkpoint
    // behavior for callers that only provide the basic physics fields.
    write_memory8(0xFFF0E7, selector.animation_gate);
    write_memory8(0xFFF0E6, selector.terminal_transition);
    write_memory8(0xFFF0E9, selector.scene_script_countdown);
    write_memory8(0xFFF0F2, selector.interaction_lock);
    write_memory8(0xFFF0BE, selector.response_active);
    write_memory8(0xFFF0C1, selector.landing_state != 0
        ? selector.landing_state
        : grounded ? 1 : 0);
    write_memory8(0xFFF0D0, selector.transition_gate);
    write_memory8(0xFFF0D7, selector.transition_lock);
    write_memory8(0xFFF0DB, selector.transition_state);
    write_memory8(0xFFF0CD, selector.transition_mode);
    write_memory8(0xFFF0D2, selector.transition_flag);
    write_memory8(0xFFF0D4, selector.transition_response);
    write_memory8(0xFFF0DE, selector.transition_state_de);
    write_memory8(0xFFF0DF, selector.transition_state_df);
    write_memory8(0xFFF173, selector.camera_special_mode);
    write_memory8(0xFFF115, selector.response_latch);
    write_memory8(0xFFF0ED, selector.response_animation);
    write_memory8(0xFFF0EE, selector.response_state_ee);
    write_memory8(0xFFF0EF, selector.response_state_ef);
    write_memory8(0xFFF0F0, selector.response_state_f0);
    write_memory8(0xFFF101, selector.response_state_101);
    write_memory16(0xFFF0B0, as_u16(selector.horizontal_response));
    write_memory8(0xFFF0CC, selector.response_timer);
    write_memory8(0xFFEFFF, selector.interaction_pending);
    write_memory8(0xFFF11F, selector.state_lock);
}

void PlayerAnimationVm::sync_context(const AnimationContext& context) {
    write_memory16(0xFF7DFA, as_u16(context.player_x));
    write_memory16(0xFF7DFC, as_u16(context.player_y));
    write_memory16(0xFF7E02, as_u16(context.world_x));
    write_memory16(0xFF7E04, as_u16(context.world_y));
    write_memory16(0xFF7E58, as_u16(context.player_vx));
    write_memory16(0xFF7E5A, as_u16(context.player_vy));
    write_memory16(0xFF7E00, context.camera_vertical_threshold);
    // The player record's actor-relative movement words are inputs to F2/F9
    // branches in the jump streams.  The byte at +0x1A is the integral/high
    // byte of the vertical word, not an independent VM scratch byte.
    write_memory16(0x18, as_u16(context.player_vx));
    write_memory16(0x1A, as_u16(context.player_vy));
    write_memory8(0xFFF0C3, context.terrain_behavior);
    sync_selector_context(context.selector, context.grounded);
    write_memory8(0xFF7E28, 1);
    write_memory16(2, as_u16(context.player_x));
    write_memory16(4, as_u16(context.player_y));
    write_memory8(9, facing_left_ ? 0xFF : 0);
}

void PlayerAnimationVm::sync_actor_context(
    const ActorAnimationState& actor,
    const AnimationContext& context
) {
    write_memory16(0xFF7DFA, as_u16(context.player_x));
    write_memory16(0xFF7DFC, as_u16(context.player_y));
    write_memory16(0xFF7E02, as_u16(context.world_x));
    write_memory16(0xFF7E04, as_u16(context.world_y));
    write_memory16(0xFF7E58, as_u16(context.player_vx));
    write_memory16(0xFF7E5A, as_u16(context.player_vy));
    write_memory8(0xFFF0C3, context.terrain_behavior);
    sync_selector_context(context.selector, context.grounded);
    write_memory8(0xFF7E28, actor.type);
    write_memory8(0x07, actor.runtime_field_07);
    write_memory32(0x0A, actor.movement_pc);
    write_memory16(2, actor.x);
    write_memory16(4, actor.y);
    write_memory16(0x18, as_u16(actor.movement_word_18));
    write_memory16(0x1A, as_u16(actor.movement_word_1a));
    write_memory8(9, actor.facing_x_flip);
    write_memory8(0x35, actor.facing_y_flip);
    write_memory8(0x3C, actor.flags);
    write_memory8(0x3D, actor.interaction_state);
    write_memory8(0x37, actor.animation_timer);
}

std::uint32_t PlayerAnimationVm::dynamic_stream(const AnimationContext& context) const {
    // Player branch of AnimationVM_SelectState (0x001AD150). Landing is
    // selected by the recovered native physics boundary; the remaining tests
    // preserve the original global-state routing for future states.
    if (pose_ == SpritePose::Landing) return kIdleStream;
    if (read_memory8(0xFFF0D7) != 0) return 0x00121964;
    if (read_memory8(0xFFF173) != 0) return context.grounded ? kRunStream : 0x00121C28;
    if (read_memory8(0xFFF115) != 0) return 0x00125E72;
    if (read_memory8(0xFFF0CD) != 0) return 0x00121964;
    if (read_memory8(0xFFF0DB) != 0) return 0x0012181A;
    if (read_memory8(0xFFF0D0) != 0) return 0x00121828;
    if (read_memory8(0xFFF0D2) != 0) return 0x00121C62;
    if (read_memory8(0xFFF0C1) != 0) {
        if (read_memory8(0xFFF0DE) != 0) return 0x0012231E;
        if (read_memory8(0xFFF0DF) != 0) return 0x00122298;
        if (read_memory8(0xFFF0ED) != 0) return 0x00121FA6;
        if (read_memory16(0xFFF0B0) == 1 || read_memory16(0xFFF0B0) == 2) return kRunStream;
        return kIdleStream;
    }
    return 0x00121AD8;
}

bool PlayerAnimationVm::compare_command(std::uint32_t& cursor) {
    const std::uint8_t flags = read_rom8(cursor + 1);
    const std::uint8_t operation = flags & 0x70;
    const bool actor_relative = (flags & 0x80) != 0;
    const std::uint8_t width = flags & 0x07;
    const std::uint16_t offset = read_rom16(cursor + 2);
    const std::uint32_t address = actor_relative ? offset : 0xFF0000U + offset;
    cursor += 4;
    std::uint32_t value;
    // The byte-compare handler still fetches a 16-bit stream operand and
    // compares its low byte (the ROM's MOVE.W is visible at 0x1ACAD2).
    if (width == 1) { value = read_rom16(cursor); cursor += 2; }
    else if (width == 2) { value = read_rom16(cursor); cursor += 2; }
    else { value = read_rom32(cursor); cursor += 4; }
    const std::uint32_t current = width == 1 ? read_memory8(address)
        : width == 2 ? read_memory16(address) : read_memory32(address);
    bool branch = false;
    switch (operation) {
    case 0x10: branch = value == current; break;
    case 0x20: branch = value != current; break;
    case 0x30: branch = value >= current; break;
    default: branch = value < current; break;
    }
    const std::uint32_t target = read_rom32(cursor);
    cursor = branch ? target : cursor + 4;
    return true;
}

bool PlayerAnimationVm::flag_command(std::uint32_t& cursor) {
    const std::uint8_t flags = read_rom8(cursor + 1);
    const int bit = flags & 7;
    const bool actor_relative = (flags & 0x80) != 0;
    const std::uint16_t offset = read_rom16(cursor + 2);
    const std::uint32_t address = actor_relative ? offset : 0xFF0000U + offset;
    const bool set = (read_memory8(address) & (1U << bit)) != 0;
    // F2's bit 6 selects the two opposite tests in the ROM handler:
    // bit 6 set branches when the tested bit is set; bit 6 clear branches
    // when it is clear.
    const bool branch = (flags & 0x40) != 0 ? set : !set;
    const std::uint32_t target = read_rom32(cursor + 4);
    cursor = branch ? target : cursor + 8;
    return true;
}

bool PlayerAnimationVm::command(std::uint8_t opcode, std::uint32_t& cursor, const AnimationContext& context) {
    switch (opcode) {
    case 0xEA: cursor = read_rom32(cursor + 2); return false;
    case 0xEB:
        if (read_rom8(cursor + 1) == 0) actor_[9] ^= 0xFF;
        else actor_[0x35] ^= 0xFF;
        cursor += 2; return false;
    case 0xEC:
        cursor += 2;
        if (read_rom8(cursor - 1) != 0) {
            animation_pc_ = 0;
            cursor = 0;
            stream_kind_ = AnimationStreamKind::Locomotion;
        }
        return true;
    case 0xED: {
        const std::uint8_t mode = read_rom8(cursor + 1);
        const std::uint16_t offset = read_rom16(cursor + 2);
        const std::uint32_t address = (mode & 0x10) != 0 ? offset : 0xFF0000U + offset;
        cursor += 4;
        if ((mode & 0x0F) == 1) { write_memory8(address, static_cast<std::uint8_t>(read_rom16(cursor))); cursor += 2; }
        else if ((mode & 0x0F) == 2) { write_memory16(address, read_rom16(cursor)); cursor += 2; }
        else { write_memory32(address, read_rom32(cursor)); cursor += 4; }
        return false;
    }
    case 0xEE: {
        const std::uint8_t value = read_rom8(cursor + 1);
        cursor += 2;
        if ((value & 0x80) != 0) { timer_ = value & 0x7F; actor_[0x37] = timer_; return true; }
        actor_[0x28] = value;
        return_pc_ = cursor;
        return false;
    }
    case 0xEF:
        cursor += 2;
        if (actor_[0x28] != 0) { --actor_[0x28]; cursor = return_pc_; }
        return false;
    case 0xF0: {
        const std::uint8_t threshold = read_rom8(cursor + 1);
        if (context.random_state != nullptr) {
            *context.random_state = *context.random_state * 13U + 7U;
            const std::uint32_t state = *context.random_state;
            // 0x1B3032 folds the high and low words into D7; F0 compares the
            // low byte of that folded 16-bit value against its threshold.
            random_value_ = static_cast<std::uint8_t>(state ^ (state >> 16));
        }
        cursor += 2;
        cursor = random_value_ < threshold ? read_rom32(cursor) : cursor + 4;
        return false;
    }
    case 0xF1: {
        const std::uint8_t axis = read_rom8(cursor + 1);
        const auto delta = static_cast<std::int16_t>(read_rom16(cursor + 2));
        if (axis == 0) {
            const int value = static_cast<std::int16_t>(read_memory16(2)) + (actor_[9] ? -delta : delta);
            write_memory16(2, as_u16(value));
        } else {
            const int value = static_cast<std::int16_t>(read_memory16(4)) + (actor_[0x35] ? -delta : delta);
            write_memory16(4, as_u16(value));
        }
        cursor += 4; return false;
    }
    case 0xF2:
        flag_command(cursor);
        return false;
    case 0xF3:
        sound_requests_.push_back(read_rom8(cursor + 1));
        cursor += 2;
        return false;
    case 0xF4: compare_command(cursor); return false;
    case 0xF5: {
        // AnimationVM_SpawnOrCopyActor (0x001AD00E) consumes a fixed 16-byte
        // record. Keep the request lossless here; Engine applies the ROM
        // template to the live actor table after the VM tick.
        AnimationSpawnRequest request;
        request.valid = true;
        request.mode = read_rom8(cursor + 1);
        request.template_address = read_rom32(cursor + 2);
        request.offset_x = static_cast<std::int8_t>(read_rom8(cursor + 6));
        request.offset_y = static_cast<std::int8_t>(read_rom8(cursor + 7));
        request.animation_override = read_rom32(cursor + 8);
        request.movement_override = read_rom32(cursor + 12);
        request.source_world_x = context.world_x;
        request.source_world_y = context.world_y;
        request.source_facing_x_flip = actor_[9];
        request.source_facing_y_flip = actor_[0x35];
        spawn_requests_.push_back(request);
        cursor += 16;
        return false;
    }
    case 0xF6:
        // Actor callback 0 clears the current actor record. Surface actors
        // use F6 00 after their short 0x8C/0x7B animation, and the same
        // callback is used by the short type-0x84 child effect. The player
        // VM must consume the opcode without clearing its own state.
        if (actor_tick_ && read_rom8(cursor + 1) == 0) {
            actor_[0] = 0;
        }
        cursor += 2;
        return false;
    case 0xF7:
        actor_[9] = 0;
        if (read_memory16(0xFF7E02) < read_memory16(2)) actor_[9] = 0xFF;
        cursor += 2; return false;
    case 0xF8:
        cursor = dynamic_stream(context);
        stream_entry_ = cursor;
        // F8 returns control from terrain/action streams to the dynamic
        // locomotion selector. Keep response roots owned by the response
        // state machine; every other dynamic target is a locomotion stream.
        if (stream_kind_ != AnimationStreamKind::Locomotion && !is_response_root(cursor)) {
            stream_kind_ = AnimationStreamKind::Locomotion;
        }
        if (pose_ == SpritePose::Landing) { pose_ = SpritePose::Idle; landing_finished_ = true; }
        return false;
    case 0xF9:
        actor_[0x1A] = static_cast<std::uint8_t>(actor_[0x1A] + read_rom8(cursor + 1));
        actor_[0x18] = static_cast<std::uint8_t>(actor_[0x18] + read_rom8(cursor + 2));
        cursor += 4; return false;
    case 0xFA: {
        const std::uint8_t mode = read_rom8(cursor + 1);
        const std::uint16_t offset = read_rom16(cursor + 2);
        const std::uint32_t address = (mode & 0x40) != 0 ? offset : 0xFF0000U + offset;
        const bool subtract = (mode & 0x80) != 0;
        cursor += 4;
        if ((mode & 7) == 1) { const auto value = read_rom16(cursor); const auto old = read_memory8(address); write_memory8(address, static_cast<std::uint8_t>(subtract ? old - value : old + value)); cursor += 2; }
        else if ((mode & 7) == 2) { const auto value = read_rom16(cursor); const auto old = read_memory16(address); write_memory16(address, static_cast<std::uint16_t>(subtract ? old - value : old + value)); cursor += 2; }
        else { const auto value = read_rom32(cursor); const auto old = read_memory32(address); write_memory32(address, subtract ? old - value : old + value); cursor += 4; }
        return false;
    }
    case 0xFB: {
        const std::uint32_t callback = read_rom32(cursor + 2);
        // FB performs a tail callback by replacing the VM return address.
        // The common 1ACC5E callback is the actor animation's random sound
        // selector; its first operation is the same 13x+7 LCG step used by
        // F0, even though its audio result is outside this state VM.
        if (callback == 0x001ACC5E && context.random_state != nullptr) {
            *context.random_state = *context.random_state * 13U + 7U;
            random_value_ = static_cast<std::uint8_t>(
                *context.random_state ^ (*context.random_state >> 16)
            );
        }
        cursor += 6;
        return false;
    }
    case 0xFC:
        if ((read_rom8(cursor + 1) & 0x80) != 0) cursor = return_pc_;
        else { return_pc_ = cursor + 6; cursor = read_rom32(cursor + 2); }
        return false;
    case 0xFD: {
        int limit = read_rom8(cursor + 1);
        if (limit == 0xFF) limit = 0x140;
        const int distance = std::abs(context.world_x - static_cast<int>(read_memory16(2)));
        cursor += 2;
        // The inline path is the far-away approach behavior; the target path
        // is the near-enough idle behavior used by the interaction actor.
        cursor = limit >= distance ? read_rom32(cursor) : cursor + 4;
        return false;
    }
    case 0xFE: {
        const int limit = read_rom8(cursor + 1);
        const int distance = std::abs(context.world_y - static_cast<int>(read_memory16(4)));
        cursor += 2;
        cursor = limit >= distance ? read_rom32(cursor) : cursor + 4;
        return false;
    }
    default: throw std::runtime_error("unknown animation VM opcode");
    }
}

void PlayerAnimationVm::tick_rom(const AnimationContext& context) {
    if (animation_pc_ == 0 || rom_.empty()) return;
    sync_context(context);
    std::uint32_t cursor = animation_pc_;
    const std::uint16_t reference = read_rom16(cursor);
    cursor += 2;
    frame_pointer_ = read_rom32(reference);
    actor_[0x14] = static_cast<std::uint8_t>(frame_pointer_ >> 24);
    actor_[0x15] = static_cast<std::uint8_t>(frame_pointer_ >> 16);
    actor_[0x16] = static_cast<std::uint8_t>(frame_pointer_ >> 8);
    actor_[0x17] = static_cast<std::uint8_t>(frame_pointer_);
    if (timer_ != 0) { --timer_; actor_[0x37] = timer_; return; }

    tracking_memory_writes_ = true;
    for (int instruction = 0; instruction < 1024; ++instruction) {
        const std::uint8_t opcode = read_rom8(cursor);
        if (!is_command(opcode)) {
            tracking_memory_writes_ = false;
            animation_pc_ = cursor;
            return;
        }
        if (command(opcode, cursor, context)) {
            tracking_memory_writes_ = false;
            animation_pc_ = cursor;
            return;
        }
    }
    tracking_memory_writes_ = false;
    throw std::runtime_error("animation VM command loop exceeded 1024 instructions");
}

void PlayerAnimationVm::tick_actor_rom(
    const AnimationContext& context,
    const ActorAnimationState& actor
) {
    if (animation_pc_ == 0 || rom_.empty()) return;
    sync_actor_context(actor, context);
    std::uint32_t cursor = animation_pc_;
    const std::uint16_t reference = read_rom16(cursor);
    cursor += 2;
    frame_pointer_ = read_rom32(reference);
    actor_[0x14] = static_cast<std::uint8_t>(frame_pointer_ >> 24);
    actor_[0x15] = static_cast<std::uint8_t>(frame_pointer_ >> 16);
    actor_[0x16] = static_cast<std::uint8_t>(frame_pointer_ >> 8);
    actor_[0x17] = static_cast<std::uint8_t>(frame_pointer_);
    // The 0x1E proximity root's near-X branch enters the extended interaction
    // sequence at 0x1237C6. The ROM's command loop consumes that branch in the
    // same actor service; publish its terminal frame cursor directly here.
    if (actor.type == 0x1E
        && animation_pc_ == 0x00123614
        && std::abs(context.world_x - static_cast<int>(actor.x)) <= 0x73) {
        actor_[0x3D] = 0x46;
        animation_pc_ = 0x001237C6;
        return;
    }
    if (timer_ != 0) {
        --timer_;
        actor_[0x37] = timer_;
        return;
    }

    for (int instruction = 0; instruction < 1024; ++instruction) {
        const std::uint8_t opcode = read_rom8(cursor);
        if (!is_command(opcode)) {
            animation_pc_ = cursor;
            return;
        }
        if (command(opcode, cursor, context)) {
            animation_pc_ = cursor;
            return;
        }
    }
    throw std::runtime_error("actor animation VM command loop exceeded 1024 instructions");
}

bool PlayerAnimationVm::set_frame(int sprite_frame) {
    if (rom_mode_) return false;
    for (SpritePose candidate : {SpritePose::Idle, SpritePose::Run, SpritePose::Brake, SpritePose::Jump, SpritePose::Landing}) {
        const Clip& selected = clip(candidate);
        for (std::size_t index = 0; index < selected.steps.size(); ++index) {
            if (selected.steps[index].sprite_frame == sprite_frame) {
                pose_ = candidate;
                step_ = index;
                timer_ = selected.steps[index].duration;
                return true;
            }
        }
    }
    return false;
}

void PlayerAnimationVm::set_frame_pointer(std::uint32_t frame_pointer) {
    if (!rom_mode_) return;
    frame_pointer_ = frame_pointer;
    actor_[0x14] = static_cast<std::uint8_t>(frame_pointer >> 24);
    actor_[0x15] = static_cast<std::uint8_t>(frame_pointer >> 16);
    actor_[0x16] = static_cast<std::uint8_t>(frame_pointer >> 8);
    actor_[0x17] = static_cast<std::uint8_t>(frame_pointer);
}

void PlayerAnimationVm::set_animation_state(std::uint32_t animation_pc, int timer) {
    if (!rom_mode_) return;
    pending_animation_pc_ = 0;
    animation_pc_ = animation_pc;
    if (is_response_stream_cursor(animation_pc)) {
        stream_entry_ = animation_pc < kResponseStream
            ? kResponseRecoveryStream : kResponseStream;
        stream_kind_ = AnimationStreamKind::Response;
    } else if (animation_pc == 0) {
        stream_kind_ = AnimationStreamKind::Locomotion;
    }
    timer_ = std::clamp(timer, 0, 0x7F);
    actor_[0x37] = static_cast<std::uint8_t>(timer_);
    landing_finished_ = false;
}

void PlayerAnimationVm::clear_animation_timer_next_update() {
    if (!rom_mode_) return;
    clear_timer_next_update_ = true;
}

void PlayerAnimationVm::set_animation_phase_delay(int ticks) {
    if (ticks < 0) {
        throw std::runtime_error("animation phase delay must be non-negative");
    }
    animation_phase_delay_ = ticks;
}

void PlayerAnimationVm::republish_stream_root() {
    if (!rom_mode_ || stream_entry_ == 0 || stream_kind_ != AnimationStreamKind::Locomotion) {
        return;
    }
    if (animation_pc_ != stream_entry_) {
        // The VM pass may already have advanced the cursor before the
        // gameplay selector writes the root. Restore that cursor after the
        // one-frame root publication so scheduler phase is not disturbed.
        pending_animation_pc_ = animation_pc_;
    }
    animation_pc_ = stream_entry_;
    const std::uint16_t reference = read_rom16(stream_entry_);
    set_frame_pointer(read_rom32(reference));
}

void PlayerAnimationVm::republish_stream_root_cursor_only() {
    if (!rom_mode_ || stream_entry_ == 0) return;
    pending_animation_pc_ = 0;
    animation_pc_ = stream_entry_;
}

void PlayerAnimationVm::hold_animation_cursor(std::uint32_t cursor) {
    if (!rom_mode_ || cursor == 0) return;
    // Keep the cursor that the next VM service should use separately from
    // the one exposed in this state boundary. The command already ran (and
    // may have queued an F5 request), so re-executing it after the held
    // publication would duplicate the spawned record.
    pending_animation_pc_ = animation_pc_;
    animation_pc_ = cursor;
}

void PlayerAnimationVm::force_tick_next_update_without_phase() {
    if (rom_mode_) force_tick_without_phase_ = true;
}

void PlayerAnimationVm::defer_tick_next_update() {
    if (rom_mode_) defer_tick_next_update_ = true;
}

void PlayerAnimationVm::update_actor(
    ActorAnimationState& actor,
    const AnimationContext& context
) {
    if (!rom_mode_ || actor.animation_pc == 0) return;

    animation_pc_ = actor.animation_pc;
    timer_ = actor.animation_timer;
    actor_[0] = actor.type;
    actor_[0x07] = actor.runtime_field_07;
    actor_[2] = static_cast<std::uint8_t>(actor.x >> 8);
    actor_[3] = static_cast<std::uint8_t>(actor.x);
    actor_[4] = static_cast<std::uint8_t>(actor.y >> 8);
    actor_[5] = static_cast<std::uint8_t>(actor.y);
    actor_[9] = actor.facing_x_flip;
    actor_[0x14] = static_cast<std::uint8_t>(actor.frame_ptr >> 24);
    actor_[0x15] = static_cast<std::uint8_t>(actor.frame_ptr >> 16);
    actor_[0x16] = static_cast<std::uint8_t>(actor.frame_ptr >> 8);
    actor_[0x17] = static_cast<std::uint8_t>(actor.frame_ptr);
    actor_[0x20] = static_cast<std::uint8_t>(actor.animation_pc >> 24);
    actor_[0x21] = static_cast<std::uint8_t>(actor.animation_pc >> 16);
    actor_[0x22] = static_cast<std::uint8_t>(actor.animation_pc >> 8);
    actor_[0x23] = static_cast<std::uint8_t>(actor.animation_pc);
    actor_[0x35] = actor.facing_y_flip;
    actor_[0x37] = actor.animation_timer;
    actor_[0x3C] = actor.flags;

    actor_tick_ = true;
    tick_actor_rom(context, actor);
    actor_tick_ = false;

    actor.type = actor_[0];
    actor.runtime_field_07 = actor_[0x07];
    actor.x = read_memory16(2);
    actor.y = read_memory16(4);
    actor.facing_x_flip = actor_[9];
    actor.facing_y_flip = actor_[0x35];
    actor.flags = actor_[0x3C];
    actor.interaction_state = actor_[0x3D];
    actor.animation_pc = animation_pc_;
    actor.frame_ptr = frame_pointer_;
    actor.animation_timer = actor_[0x37];
    actor.movement_pc = read_memory32(0x0A);
    actor.movement_word_18 = static_cast<std::int16_t>(read_memory16(0x18));
    actor.movement_word_1a = static_cast<std::int16_t>(read_memory16(0x1A));
}

bool PlayerAnimationVm::take_spawn_request(AnimationSpawnRequest& request) {
    if (spawn_requests_.empty()) return false;
    request = spawn_requests_.front();
    spawn_requests_.erase(spawn_requests_.begin());
    return true;
}

std::vector<std::uint8_t> PlayerAnimationVm::take_sound_requests() {
    std::vector<std::uint8_t> requests;
    requests.swap(sound_requests_);
    return requests;
}

bool PlayerAnimationVm::take_memory_write(
    std::uint32_t address,
    std::uint8_t& value
) {
    if (address < 0xFF0000 || address > 0xFFFFFF) return false;
    const auto offset = address - 0xFF0000;
    if (memory_write_flags_[offset] == 0) return false;
    memory_write_flags_[offset] = 0;
    value = memory_[offset];
    return true;
}

void PlayerAnimationVm::select_stream_entry(
    std::uint32_t stream_entry,
    bool publish_frame_pointer,
    bool defer_first_tick,
    bool force_following_tick
) {
    if (!rom_mode_) return;
    stream_kind_ = AnimationStreamKind::Action;
    stream_entry_ = stream_entry;
    animation_pc_ = stream_entry;
    timer_ = 0;
    if (publish_frame_pointer) {
        const std::uint16_t reference = read_rom16(stream_entry);
        set_frame_pointer(read_rom32(reference));
    }
    if (defer_first_tick) {
        // A terrain selector can publish a root immediately before the
        // scheduler's service slot. Move the phase once so this root remains
        // observable for one frame and the following update takes the tick.
        ++update_count_;
    }
    if (force_following_tick) {
        // The terrain selector's first action transition services the new
        // root on the following update and then services one additional
        // command boundary before returning to the alternating cadence.
        force_tick_after_service_ = true;
    }
    landing_finished_ = false;
    landing_reselect_pending_ = false;
}

void PlayerAnimationVm::select_locomotion_stream(
    SpritePose pose,
    const AnimationContext& context
) {
    if (!rom_mode_) return;
    select_rom_stream(pose, false, &context);
}

void PlayerAnimationVm::select_locomotion_entry(
    std::uint32_t stream_entry,
    bool defer_first_tick,
    SpritePose pose
) {
    if (!rom_mode_) return;
    pose_ = pose;
    stream_kind_ = AnimationStreamKind::Locomotion;
    stream_entry_ = stream_entry;
    animation_pc_ = stream_entry;
    timer_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = false;
    if (defer_first_tick) {
        ++update_count_;
    }
}

void PlayerAnimationVm::select_response_stream(std::uint32_t stream_entry, int timer) {
    if (!rom_mode_) return;
    stream_kind_ = AnimationStreamKind::Response;
    stream_entry_ = stream_entry;
    animation_pc_ = stream_entry;
    timer_ = std::clamp(timer, 0, 0x7F);
    actor_[0x37] = static_cast<std::uint8_t>(timer_);
    landing_finished_ = false;
    landing_reselect_pending_ = false;
}

bool PlayerAnimationVm::response_stream_needs_recovery() const {
    if (stream_kind_ != AnimationStreamKind::Response
        || stream_entry_ != kResponseStream) {
        return false;
    }

    // 0x121FA6 is the extended response/hurt stream. It is selected by the
    // terrain state machine and can loop indefinitely, so its exit condition
    // must come from the same RAM gates that selected it. In particular, do
    // not use grounded/no-input as a substitute: those are also true while a
    // response is still active.
    return read_memory8(0xFFF0BE) == 0
        && read_memory8(0xFFF0ED) == 0
        && read_memory8(0xFFF0EE) == 0
        && read_memory8(0xFFF0EF) == 0
        && read_memory8(0xFFF0F0) == 0
        && read_memory8(0xFFF101) == 0
        && read_memory8(0xFFF115) == 0;
}

bool PlayerAnimationVm::select_player_interaction_state(const AnimationContext& context) {
    if (!rom_mode_) return false;

    // This is the control flow of Player_ProcessInteractionState at
    // 0x001AE4F8. The caller-side write that clears FFF0CC is represented by
    // selector.response_timer; it is intentionally not inferred from the
    // VM's bytecode scratch memory because FFF0CC has two distinct roles in
    // the ROM.
    const AnimationSelectorState& state = context.selector;
    if (state.animation_gate != 0
        || state.terminal_transition != 0
        || state.scene_script_countdown != 0
        || state.interaction_lock != 0) {
        return false;
    }

    const bool normal_path = state.response_active == 0
        && state.landing_state != 0
        && state.transition_gate == 0
        && state.transition_lock == 0
        && state.transition_mode == 0
        && state.transition_response == 0;
    if (normal_path) {
        if (state.camera_special_mode != 0) {
            if (stream_entry_ != kInteractionSpecialStream) {
                select_stream_entry(kInteractionSpecialStream);
                timer_ = 0;
            }
            write_memory8(0xFFF0E7, 0xFF);
            write_memory8(0xFFF0E9, 0x32);
            write_memory8(0xFFEFFF, 1);
            return true;
        }
        // The ordinary stop selector is only an idle/run handoff. A brake
        // cursor is already the ROM's selected stopping animation, and
        // replacing it with the interaction stream changes the rendered
        // frame exactly at a wall stop. Likewise, response/action streams
        // must remain owned by their initiating gameplay state.
        const bool response_stop_handoff =
            stream_kind_ == AnimationStreamKind::Response
            && stream_entry_ == kResponseStream
            && state.response_animation != 0;
        const bool can_enter_stop_stream =
            (stream_kind_ == AnimationStreamKind::Locomotion
                && pose_ != SpritePose::Brake
                && (stream_entry_ == kIdleStream || stream_entry_ == kRunStream))
            || response_stop_handoff;
        if (can_enter_stop_stream
            && state.response_timer == 0
            && state.interaction_pending == 0
            && state.state_lock == 0) {
            if (stream_entry_ != kInteractionStopStream) {
                const int current_timer = timer_;
                // The ROM's post-collision selector runs immediately after
                // the common VM pass.  Advancing the scheduler phase here
                // makes the newly selected stop root receive its first
                // service on the very next VBlank (the frame-700 boundary in
                // the captured trace), rather than waiting one extra slot.
                select_stream_entry(kInteractionStopStream);
                // The selector only overwrites FF7E60.  The common animation
                // pass has already published the current frame/timer, so a
                // response-to-stop handoff keeps those fields intact at this
                // boundary and consumes the new stream on the next tick.
                if (response_stop_handoff) {
                    set_animation_state(kInteractionStopStream, current_timer);
                    clear_animation_timer_next_update();
                }
            }
        }
    }

    // Both the selected stop state and the ordinary FFF0CC response path
    // converge at 0x001AE58E. The ROM clears these fields before dispatching
    // the interaction helper at 0x001B03F2.
    write_memory16(0xFFF0B0, 0);
    write_memory8(0xFFF0CC, 0);
    if (context.scene_vdp_update_flag != 0) {
        sound_requests_.push_back(kInteractionEventSoundId);
    }
    return normal_path && state.camera_special_mode == 0
        && state.response_timer == 0
        && state.interaction_pending == 0
        && state.state_lock == 0;
}

bool PlayerAnimationVm::finished() const {
    if (rom_mode_) return landing_finished_;
    const Clip& current = clip(pose_);
    return !current.loop && step_ + 1 == current.steps.size() && timer_ <= 1;
}

void PlayerAnimationVm::update(
    SpritePose desired_pose,
    HorizontalDirection horizontal_direction,
    const AnimationContext& context
) {
    if (pending_animation_pc_ != 0) {
        animation_pc_ = pending_animation_pc_;
        pending_animation_pc_ = 0;
    }
    if (clear_timer_next_update_) {
        timer_ = 0;
        actor_[0x37] = 0;
        clear_timer_next_update_ = false;
    }
    if (horizontal_direction == HorizontalDirection::Left) {
        facing_left_ = true;
    } else if (horizontal_direction == HorizontalDirection::Right) {
        facing_left_ = false;
    }
    if (!rom_mode_) {
        if (desired_pose != pose_) { select(desired_pose); return; }
        const Clip& current = clip(pose_);
        if (timer_ > 1) { --timer_; return; }
        if (step_ + 1 < current.steps.size()) ++step_;
        else if (current.loop) step_ = current.loop_start;
        else { timer_ = 1; return; }
        timer_ = current.steps[step_].duration;
        return;
    }
    // The ROM restarts the run stream when a held direction reverses. The
    // facing byte and the animation cursor are separate actor fields, so
    // updating only facing leaves the native sprite one or more frames ahead
    // of the original immediately after a left-to-right (or right-to-left)
    // reversal.
    if (desired_pose == SpritePose::Run
        && horizontal_direction != HorizontalDirection::None
        && horizontal_direction_ != HorizontalDirection::None
        && horizontal_direction != horizontal_direction_) {
        select_rom_stream(SpritePose::Run, false);
    }
    horizontal_direction_ = horizontal_direction;
    sync_context(context);
    if (context.selector.interaction_lock == 0x28) {
        // Actor flag bit 5 restarts the Genesis run stream. The surface
        // interaction path uses the same lock after selecting the stop
        // stream, so preserve that root when the player is braking at a
        // zero terrain-response boundary.
        if (stream_kind_ == AnimationStreamKind::Locomotion
            && desired_pose == SpritePose::Run) {
            select_rom_stream(SpritePose::Run, false, &context);
        } else if (desired_pose == SpritePose::Brake
                   && context.terrain_response_timer_state == 0
                   && context.selector.horizontal_response == 0) {
            select_stream_entry(kInteractionStopStream);
            // The selector writes the new root after this frame's VM pass.
            // Count the update even though the newly selected stream must
            // not consume its first command until the next frame.
            ++update_count_;
            return;
        }
    }
    if (response_stream_needs_recovery()) {
        select_rom_stream(desired_pose, false, &context);
    } else if (stream_kind_ == AnimationStreamKind::Locomotion
        && (desired_pose != pose_ || animation_pc_ == 0)) {
        select_rom_stream(desired_pose, false, &context);
    }
    if (pose_ == SpritePose::Landing && landing_reselect_pending_ && (update_count_ & 1U) != 0) {
        // The gameplay state selector writes the landing root again on the
        // intervening frame; the VM then consumes its first frame on the next
        // actor tick. This one-frame root write is visible in FF7E60.
        animation_pc_ = kLandingStream;
        landing_reselect_pending_ = false;
    }
    if (defer_tick_next_update_) {
        defer_tick_next_update_ = false;
        // Do not consume scheduler phase: the following VBlank is the
        // service that would otherwise have occurred on this boundary.
        return;
    }
    if (force_tick_without_phase_) {
        force_tick_without_phase_ = false;
        tick_rom(context);
        return;
    }
    if (animation_phase_delay_ > 0) {
        // The VM is serviced on alternating update passes. Consume a phase
        // tick without touching the Genesis-visible timer or cursor.
        if ((update_count_ & 1U) == 0) {
            --animation_phase_delay_;
        }
        ++update_count_;
        return;
    }
    if ((update_count_++ & 1U) == 0) {
        tick_rom(context);
        if (force_tick_after_service_) {
            force_tick_after_service_ = false;
            force_tick_next_update_ = true;
        }
    } else if (force_tick_next_update_) {
        force_tick_next_update_ = false;
        tick_rom(context);
        // The forced service occupies the otherwise idle slot. Preserve the
        // alternating cadence for the update after it.
        ++update_count_;
    }
}

int PlayerAnimationVm::sprite_frame() const {
    return rom_mode_ ? -1 : clip(pose_).steps[step_].sprite_frame;
}

std::uint32_t PlayerAnimationVm::stream_entry() const {
    return rom_mode_ ? stream_entry_ : clip(pose_).stream_entry;
}

}  // namespace openaladdin
