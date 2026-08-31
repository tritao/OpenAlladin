#include "animation.hpp"

#include "checkpoint_io.hpp"
#include "game_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace openaladdin {
namespace {

class RamContextScope {
public:
    explicit RamContextScope(GameRamView& ram) : ram_(ram) {}
    ~RamContextScope() { ram_.clear_context(); }

private:
    GameRamView& ram_;
};

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
constexpr std::uint32_t kAppleActionStream = 0x001223DA;
constexpr std::uint32_t kAppleTerrainTimerStream = 0x001225A2;
constexpr std::uint32_t kAirborneActionStream = 0x0012262A;
constexpr std::uint32_t kAppleFirstHeldCursor = 0x001223FA;
constexpr std::uint32_t kAppleFirstCursorBoundary = 0x00122438;
constexpr std::uint32_t kAppleSecondCursorBoundary = 0x0012245C;
constexpr std::uint32_t kAppleFirstFrame = 0x001ED422;
constexpr std::uint32_t kAppleSecondFrame = 0x001ED470;

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

void write_spawn_command(checkpoint::Writer& writer, const F5Command& command) {
    writer.boolean(command.valid);
    writer.u8(command.mode);
    writer.u32(command.template_address);
    writer.u8(static_cast<std::uint8_t>(command.offset_x));
    writer.u8(static_cast<std::uint8_t>(command.offset_y));
    writer.u32(command.animation_override);
    writer.u32(command.movement_override);
    writer.i32(command.source_world_x);
    writer.i32(command.source_world_y);
    writer.u8(command.source_facing_x_flip);
    writer.u8(command.source_facing_y_flip);
    writer.boolean(command.apple_action);
    // Retain the former request's source-slot wire field for checkpoint
    // compatibility. The source is now supplied by AnimationServices.
    writer.i32(-1);
}

F5Command read_spawn_command(checkpoint::Reader& reader) {
    F5Command command;
    command.valid = reader.boolean();
    command.mode = reader.u8();
    command.template_address = reader.u32();
    command.offset_x = static_cast<std::int8_t>(reader.u8());
    command.offset_y = static_cast<std::int8_t>(reader.u8());
    command.animation_override = reader.u32();
    command.movement_override = reader.u32();
    command.source_world_x = reader.i32();
    command.source_world_y = reader.i32();
    command.source_facing_x_flip = reader.u8();
    command.source_facing_y_flip = reader.u8();
    command.apple_action = reader.boolean();
    (void)reader.i32();
    return command;
}

}  // namespace

PlayerAnimationVm::PlayerAnimationVm() {
    ram_.bind_actor_record(actor_);
}

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
    // The shared ROM RNG lives in GameState. Keep the byte result local to the
    // interpreter because it is a command result, not another RAM image.
    random_value_ = 0x00;
    actor_tick_ = false;
    actor_service_boundary_ = ActorServiceBoundary::None;
    tracking_memory_writes_ = false;
    ram_.set_write_tracking(false);
    active_command_pc_ = 0;
    writer_pcs_.clear();
    unhandled_spawn_commands_.clear();
    deferred_spawn_command_.reset();
    actor_retired_ = false;
    sound_requests_.clear();
    update_count_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = false;
    ram_.reset();
    ram_.bind_actor_record(actor_);
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
        && (context->response_timer_override
                ? *context->response_timer_override != 0
                : read_memory8(0xFFF0CC) != 0)
        ? kJumpTimedStream
        : clip(pose).stream_entry;
    animation_pc_ = stream_entry_;
    timer_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = pose == SpritePose::Landing;
    if (execute_now) tick_rom({}, nullptr);
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
    return ram_.read8(address);
}

std::uint16_t PlayerAnimationVm::read_memory16(std::uint32_t address) const {
    return ram_.read16(address);
}

std::uint32_t PlayerAnimationVm::read_memory32(std::uint32_t address) const {
    return ram_.read32(address);
}

void PlayerAnimationVm::write_memory8(std::uint32_t address, std::uint8_t value) {
    if (writer_trace_enabled_ && tracking_memory_writes_ && active_command_pc_ != 0
        && (writer_pcs_.empty() || writer_pcs_.back() != active_command_pc_)) {
        writer_pcs_.push_back(active_command_pc_);
    }
    ram_.write8(address, value);
}

void PlayerAnimationVm::write_memory16(std::uint32_t address, std::uint16_t value) {
    ram_.write16(address, value);
}

void PlayerAnimationVm::write_memory32(std::uint32_t address, std::uint32_t value) {
    ram_.write32(address, value);
}

std::uint16_t PlayerAnimationVm::advance_random() {
    auto* random_state = ram_.random_state();
    if (random_state == nullptr) return 0;

    *random_state = *random_state * 13U + 7U;
    const std::uint32_t state = *random_state;
    // TerrainScene5RandomStep returns the low word after folding the new
    // state's high word into it. Keep the complete result for callbacks that
    // consume D7w, while retaining the byte used by F0's threshold branch.
    const std::uint16_t folded = static_cast<std::uint16_t>(
        (state & 0xFFFFU) ^ (state >> 16));
    random_value_ = static_cast<std::uint8_t>(folded);
    return folded;
}

void PlayerAnimationVm::dispatch_callback(
    std::uint32_t callback,
    const AnimationContext& context
) {
    switch (callback) {
    case 0x001ACB5A: // ActorEvent_SetActorField3CBit4
        write_memory8(0x3C, static_cast<std::uint8_t>(read_memory8(0x3C) | 0x10U));
        return;
    case 0x001ACB62: // ActorEvent_ClearActorField3CBit4
        write_memory8(0x3C, static_cast<std::uint8_t>(read_memory8(0x3C) & ~0x10U));
        return;
    case 0x001ACB6A: // ActorEvent_SetActorFlag40
        write_memory8(0x06, static_cast<std::uint8_t>(read_memory8(0x06) | 0x40U));
        return;
    case 0x001ACB72: // ActorEvent_ClearActorFlag40
        write_memory8(0x06, static_cast<std::uint8_t>(read_memory8(0x06) & ~0x40U));
        return;
    case 0x001ACB7A: // ActorEvent_SetFlag20
        write_memory8(0x07, static_cast<std::uint8_t>(read_memory8(0x07) | 0x20U));
        return;
    case 0x001ACB82: // ActorEvent_ClearFlag20
        write_memory8(0x07, static_cast<std::uint8_t>(read_memory8(0x07) & ~0x20U));
        return;
    case 0x001ACB8A: // ActorEvent_SetActorFlagsBit4
        write_memory8(0x06, static_cast<std::uint8_t>(read_memory8(0x06) | 0x10U));
        return;
    case 0x001ACB92: // ActorEvent_ClearActorFlagsBit4
        write_memory8(0x06, static_cast<std::uint8_t>(read_memory8(0x06) & ~0x10U));
        return;
    case 0x001ACBD8: { // ActorEvent_CopyLinkedActorCoordinates
        if (context.state == nullptr) return;
        const std::uint32_t linked_slot = read_memory32(0x3E);
        if (linked_slot >= context.state->actors.size()) return;
        const ActorState& linked = context.state->actors[linked_slot];
        write_memory16(0x02, linked.x);
        write_memory16(0x04, linked.y);
        return;
    }
    case 0x001ACC18: // ActorEvent_SetActorField3CBit5
        write_memory8(0x3C, static_cast<std::uint8_t>(read_memory8(0x3C) | 0x20U));
        return;
    case 0x001ACC20: // ActorEvent_ClearActorField3CBit5
        write_memory8(0x3C, static_cast<std::uint8_t>(read_memory8(0x3C) & ~0x20U));
        return;
    case 0x001ACC28: // ActorEvent_SetActorActiveBit
        write_memory8(0x06, static_cast<std::uint8_t>(read_memory8(0x06) | 0x01U));
        return;
    case 0x001ACC56: // ActorEvent_ClearActorActiveBit
        write_memory8(0x06, static_cast<std::uint8_t>(read_memory8(0x06) & ~0x01U));
        return;
    case 0x001ACC5E: { // ActorEvent_QueueRandomVariantAudio
        const std::uint16_t random = advance_random();
        if (read_memory8(0xFFF57D) != 0) {
            sound_requests_.push_back(static_cast<std::uint8_t>(0x5E + (random & 3U)));
        }
        return;
    }
    case 0x001ACD02: { // ActorEvent_QueueParityAudio
        const std::uint16_t random = advance_random();
        if (read_memory8(0xFFF57D) != 0) {
            sound_requests_.push_back((random & 1U) != 0 ? 0x44 : 0x40);
        }
        return;
    }
    case 0x001ACD5A: { // ActorEvent_ApplyWideRandomOffsets
        const std::uint16_t random_x = advance_random();
        write_memory8(
            0x18,
            static_cast<std::uint8_t>((random_x & 0x0FU) - 7U));
        const std::uint16_t random_y = advance_random();
        write_memory8(
            0x1A,
            static_cast<std::uint8_t>((random_y & 0x0FU) - 0x0FU));
        return;
    }
    case 0x001ACD7E: { // ActorEvent_ApplyNarrowRandomOffsets
        const std::uint16_t random_x = advance_random();
        write_memory8(
            0x18,
            static_cast<std::uint8_t>((random_x & 0x07U) - 4U));
        const std::uint16_t random_y = advance_random();
        write_memory8(
            0x1A,
            static_cast<std::uint8_t>((random_y & 0x07U) - 7U));
        return;
    }
    case 0x001B0360: { // InteractionCounter_DecrementPrimaryDigits
        // FB carries the ROM helper address. Keep the helper's decimal-borrow
        // semantics shared with collision/resource code while addressing the
        // authoritative big-endian ASCII word through GameRamView.
        std::uint16_t digits = read_memory16(0xFFEFE0);
        if (decrement_ascii_decimal(digits)) {
            write_memory16(0xFFEFE0, digits);
        }
        return;
    }
    default:
        // FB parameters are ROM function addresses. Unknown callbacks remain
        // consumed at the command boundary until their RAM contract is
        // recovered; they must not make the interpreter depend on host code.
        return;
    }
}

void PlayerAnimationVm::sync_context(const AnimationContext& context) {
    ram_.bind_context(context);
    const GameState* state = context.state;
    const int player_x = state == nullptr ? 0 : state->player.x;
    const int player_y = state == nullptr ? 0 : state->player.y;
    const auto player_vx = state == nullptr ? 0 : state->player.vx;
    const auto player_vy = context.player_vy_override
        ? *context.player_vy_override
        : state == nullptr ? 0 : state->player.vy;
    // The player record's actor-relative movement words are inputs to F2/F9
    // branches in the jump streams. The byte at +0x1A is the integral/high
    // byte of the vertical word, not an independent VM scratch byte.
    write_memory16(0x18, as_u16(player_vx));
    write_memory16(0x1A, as_u16(player_vy));
    write_memory16(2, as_u16(player_x));
    write_memory16(4, as_u16(player_y));
    write_memory8(9, facing_left_ ? 0xFF : 0);
}

std::uint32_t PlayerAnimationVm::dynamic_stream(const AnimationContext& context) const {
    // Player branch of AnimationVM_SelectState (0x001AD150). Landing is
    // selected by the recovered native physics boundary; the remaining tests
    // preserve the original global-state routing for future states.
    // The held-Down transition state has priority over the stale landing pose
    // while its action stream returns through F8. Genesis therefore enters
    // the down cursor at 0x12231E instead of falling back to idle.
    if (pose_ == SpritePose::Landing && read_memory8(0xFFF0DE) == 0) {
        return kIdleStream;
    }
    if (read_memory8(0xFFF0D7) != 0) return 0x00121964;
    if (read_memory8(0xFFF173) != 0) {
        const bool grounded = context.grounded_override
            ? *context.grounded_override : read_memory8(0xFFF0C1) != 0;
        return grounded ? kRunStream : 0x00121C28;
    }
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

bool PlayerAnimationVm::command(
    std::uint8_t opcode,
    std::uint32_t& cursor,
    const AnimationContext& context,
    AnimationServices* services
) {
    active_command_pc_ = cursor;
    switch (opcode) {
    case 0xEA: cursor = read_rom32(cursor + 2); return false;
    case 0xEB:
        if (read_rom8(cursor + 1) == 0) {
            write_memory8(9, static_cast<std::uint8_t>(read_memory8(9) ^ 0xFFU));
        } else {
            write_memory8(0x35, static_cast<std::uint8_t>(read_memory8(0x35) ^ 0xFFU));
        }
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
        if ((value & 0x80) != 0) {
            timer_ = value & 0x7F;
            write_memory8(0x37, static_cast<std::uint8_t>(timer_));
            return true;
        }
        write_memory8(0x28, value);
        return_pc_ = cursor;
        return false;
    }
    case 0xEF:
        cursor += 2;
        if (read_memory8(0x28) != 0) {
            write_memory8(0x28, static_cast<std::uint8_t>(read_memory8(0x28) - 1));
            cursor = return_pc_;
        }
        return false;
    case 0xF0: {
        const std::uint8_t threshold = read_rom8(cursor + 1);
        // 0x1B3032 folds the high and low words into D7; F0 compares the
        // low byte of that folded 16-bit value against its threshold.
        (void)advance_random();
        cursor += 2;
        cursor = random_value_ < threshold ? read_rom32(cursor) : cursor + 4;
        return false;
    }
    case 0xF1: {
        const std::uint8_t axis = read_rom8(cursor + 1);
        const auto delta = static_cast<std::int16_t>(read_rom16(cursor + 2));
        if (axis == 0) {
            const int value = static_cast<std::int16_t>(read_memory16(2))
                + (read_memory8(9) ? -delta : delta);
            write_memory16(2, as_u16(value));
        } else {
            const int value = static_cast<std::int16_t>(read_memory16(4))
                + (read_memory8(0x35) ? -delta : delta);
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
        // record. With services bound, apply the semantic lifecycle effect at
        // this command boundary instead of returning a packet to Engine for a
        // second decode/reconstruction pass.
        F5Command command;
        command.valid = true;
        command.mode = read_rom8(cursor + 1);
        command.template_address = read_rom32(cursor + 2);
        command.offset_x = static_cast<std::int8_t>(read_rom8(cursor + 6));
        command.offset_y = static_cast<std::int8_t>(read_rom8(cursor + 7));
        command.animation_override = read_rom32(cursor + 8);
        command.movement_override = read_rom32(cursor + 12);
        command.source_world_x = static_cast<int>(
            actor_tick_ ? read_memory16(2) : read_memory16(0xFF7E02)
        );
        command.source_world_y = static_cast<int>(
            actor_tick_ ? read_memory16(4) : read_memory16(0xFF7E04)
        );
        command.source_facing_x_flip = read_memory8(9);
        command.source_facing_y_flip = read_memory8(0x35);
        command.apple_action = !actor_tick_
            && (stream_entry_ == kAppleActionStream
                || stream_entry_ == kAppleTerrainTimerStream
                || stream_entry_ == kAirborneActionStream);

        const bool defer = !actor_tick_
            && ((command.mode == 0 && services != nullptr
                    && services->defer_player_spawns)
                || (command.mode == 3 && !command.apple_action
                    && stream_entry_ != kAirborneActionStream
                    && stream_entry_ != kAppleTerrainTimerStream
                    && services != nullptr
                    && services->defer_mode3_spawns));
        if (defer) {
            deferred_spawn_command_ = command;
        } else if (services != nullptr && services->spawn_f5) {
            (void)services->spawn_f5(services->source_actor, command);
        } else {
            // Preserve a useful standalone VM fallback for fixture tools that
            // intentionally do not bind gameplay services.
            unhandled_spawn_commands_.push_back(command);
        }
        cursor += 16;
        return false;
    }
    case 0xF6:
        // Actor callback 0 clears the current actor record. Surface actors
        // use F6 00 after their short 0x8C/0x7B animation, and the same
        // callback is used by the short type-0x84 child effect. The player
        // VM must consume the opcode without clearing its own state.
        if (actor_tick_) {
            const std::uint8_t command_mode = read_rom8(cursor + 1);
            if (services != nullptr && services->retire_actor) {
                services->retire_actor(services->source_actor, command_mode);
                actor_retired_ = true;
            }
            write_memory8(0, 0);
        }
        cursor += 2;
        return false;
    case 0xF7:
        write_memory8(9, 0);
        if (read_memory16(0xFF7E02) < read_memory16(2)) write_memory8(9, 0xFF);
        cursor += 2; return false;
    case 0xF8:
        cursor = dynamic_stream(context);
        stream_entry_ = cursor;
        // F8 returns control from terrain/action streams to the dynamic
        // locomotion selector. Keep response roots owned by the response
        // state machine; every other dynamic target is a locomotion stream.
        if (stream_kind_ != AnimationStreamKind::Locomotion && !is_response_root(cursor)) {
            stream_kind_ = AnimationStreamKind::Locomotion;
            if (cursor == kIdleStream) {
                pose_ = SpritePose::Idle;
            } else if (cursor == kRunStream) {
                pose_ = SpritePose::Run;
            }
        }
        if (pose_ == SpritePose::Landing) {
            pose_ = SpritePose::Idle;
            landing_finished_ = true;
            if (landing_reselect_pending_ == 3) {
                landing_reselect_pending_ = 4;
            }
        }
        return false;
    case 0xF9:
        write_memory8(
            0x1A,
            static_cast<std::uint8_t>(read_memory8(0x1A) + read_rom8(cursor + 1)));
        write_memory8(
            0x18,
            static_cast<std::uint8_t>(read_memory8(0x18) + read_rom8(cursor + 2)));
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
        // The callback itself remains a private VM dispatch so actor-relative
        // writes go through the live GameRamView rather than a mirror record.
        dispatch_callback(callback, context);
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
        const int distance = std::abs(
            static_cast<int>(read_memory16(0xFF7E02)) - static_cast<int>(read_memory16(2)));
        cursor += 2;
        // The inline path is the far-away approach behavior; the target path
        // is the near-enough idle behavior used by the interaction actor.
        cursor = limit >= distance ? read_rom32(cursor) : cursor + 4;
        return false;
    }
    case 0xFE: {
        const int limit = read_rom8(cursor + 1);
        const int distance = std::abs(
            static_cast<int>(read_memory16(0xFF7E04)) - static_cast<int>(read_memory16(4)));
        cursor += 2;
        cursor = limit >= distance ? read_rom32(cursor) : cursor + 4;
        return false;
    }
    default: throw std::runtime_error("unknown animation VM opcode");
    }
}

void PlayerAnimationVm::tick_rom(
    const AnimationContext& context,
    AnimationServices* services
) {
    active_command_pc_ = 0;
    if (animation_pc_ == 0 || rom_.empty()) return;
    sync_context(context);
    RamContextScope context_scope(ram_);
    std::uint32_t cursor = animation_pc_;
    const std::uint16_t reference = read_rom16(cursor);
    cursor += 2;
    frame_pointer_ = read_rom32(reference);
    write_memory32(0x14, frame_pointer_);
    if (timer_ != 0) {
        --timer_;
        write_memory8(0x37, static_cast<std::uint8_t>(timer_));
        return;
    }

    tracking_memory_writes_ = true;
    ram_.set_write_tracking(true);
    for (int instruction = 0; instruction < 1024; ++instruction) {
        const std::uint8_t opcode = read_rom8(cursor);
        if (!is_command(opcode)) {
            tracking_memory_writes_ = false;
            ram_.set_write_tracking(false);
            animation_pc_ = cursor;
            return;
        }
        if (command(opcode, cursor, context, services)) {
            tracking_memory_writes_ = false;
            ram_.set_write_tracking(false);
            animation_pc_ = cursor;
            return;
        }
    }
    tracking_memory_writes_ = false;
    ram_.set_write_tracking(false);
    throw std::runtime_error("animation VM command loop exceeded 1024 instructions");
}

void PlayerAnimationVm::tick_actor_rom(
    const AnimationContext& context,
    const ActorState& actor,
    AnimationServices* services
) {
    active_command_pc_ = 0;
    if (animation_pc_ == 0 || rom_.empty()) return;
    RamContextScope context_scope(ram_);
    std::uint32_t cursor = animation_pc_;
    const std::uint16_t reference = read_rom16(cursor);
    cursor += 2;
    frame_pointer_ = read_rom32(reference);
    write_memory32(0x14, frame_pointer_);
    // The 0x1E proximity root's near-X branch enters the extended interaction
    // sequence at 0x1237C6. The ROM's command loop consumes that branch in the
    // same actor service; publish its terminal frame cursor directly here.
    if (actor.type == 0x1E
        && animation_pc_ == 0x00123614
        && std::abs(
            static_cast<int>(read_memory16(0xFF7E02)) - static_cast<int>(actor.x)) <= 0x73) {
        write_memory8(0x3D, 0x46);
        animation_pc_ = 0x001237C6;
        return;
    }
    if (timer_ != 0) {
        --timer_;
        write_memory8(0x37, static_cast<std::uint8_t>(timer_));
        return;
    }

    tracking_memory_writes_ = true;
    ram_.set_write_tracking(true);
    for (int instruction = 0; instruction < 1024; ++instruction) {
        const std::uint8_t opcode = read_rom8(cursor);
        if (!is_command(opcode)) {
            tracking_memory_writes_ = false;
            ram_.set_write_tracking(false);
            animation_pc_ = cursor;
            return;
        }
        if (command(opcode, cursor, context, services)) {
            tracking_memory_writes_ = false;
            ram_.set_write_tracking(false);
            animation_pc_ = cursor;
            return;
        }
    }
    tracking_memory_writes_ = false;
    ram_.set_write_tracking(false);
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
    write_memory32(0x14, frame_pointer);
}

void PlayerAnimationVm::set_animation_state(std::uint32_t animation_pc, int timer) {
    if (!rom_mode_) return;
    animation_pc_ = animation_pc;
    if (is_response_stream_cursor(animation_pc)) {
        stream_entry_ = animation_pc < kResponseStream
            ? kResponseRecoveryStream : kResponseStream;
        stream_kind_ = AnimationStreamKind::Response;
    } else if (animation_pc == 0) {
        stream_kind_ = AnimationStreamKind::Locomotion;
    }
    timer_ = std::clamp(timer, 0, 0x7F);
    write_memory8(0x37, static_cast<std::uint8_t>(timer_));
    landing_finished_ = false;
}

void PlayerAnimationVm::select_vertical_band_animation(std::uint32_t world_y) {
    if (!rom_mode_) return;

    // Player_SelectVerticalBandAnimation indexes this table with the
    // two-pixel response coordinate before the terrain response applies its
    // same-frame displacement. Keep the ROM table here rather than deriving
    // the cursor sequence from observed replay frames.
    static constexpr std::array<std::uint32_t, 16> kVerticalAnimationTable{
        0x001218CA, 0x001218BC, 0x001218AE, 0x001218A0,
        0x00121892, 0x00121884, 0x00121876, 0x00121868,
        0x001218CA, 0x001218BC, 0x001218AE, 0x001218A0,
        0x00121892, 0x00121884, 0x00121876, 0x00121868,
    };
    const std::uint8_t band = static_cast<std::uint8_t>((world_y >> 2) & 0x0F);
    facing_left_ = band > 7;
    set_animation_state(kVerticalAnimationTable[band], 0);
    const std::uint16_t reference = read_rom16(animation_pc_);
    set_frame_pointer(read_rom32(reference));
}

void PlayerAnimationVm::clear_animation_timer_next_update() {
    if (!rom_mode_) return;
    clear_timer_next_update_ = true;
}

void PlayerAnimationVm::defer_actor_service() {
    if (!rom_mode_) return;
    actor_service_boundary_ = ActorServiceBoundary::ActorDeferUntilGate;
}

void PlayerAnimationVm::defer_actor_service_then_every_phase() {
    if (!rom_mode_) return;
    actor_service_boundary_ = ActorServiceBoundary::ActorDeferUntilGateThenEveryPhase;
}

void PlayerAnimationVm::defer_actor_service_on_gate() {
    if (rom_mode_) actor_service_boundary_ = ActorServiceBoundary::ActorDeferOnGate;
}

void PlayerAnimationVm::defer_actor_service_then_force() {
    if (rom_mode_) actor_service_boundary_ = ActorServiceBoundary::ActorDeferThenForce;
}

void PlayerAnimationVm::defer_actor_retirement() {
    if (rom_mode_) actor_service_boundary_ = ActorServiceBoundary::ActorRetireNextUpdate;
}

void PlayerAnimationVm::clear_actor_service_boundary() {
    if (rom_mode_) actor_service_boundary_ = ActorServiceBoundary::None;
}

bool PlayerAnimationVm::consume_actor_service(bool scheduler_service, bool defer_gate) {
    switch (actor_service_boundary_) {
    case ActorServiceBoundary::ActorDeferUntilGate:
        if (!defer_gate) return false;
        actor_service_boundary_ = ActorServiceBoundary::None;
        return false;
    case ActorServiceBoundary::ActorDeferUntilGateThenEveryPhase:
        if (!defer_gate) return false;
        // Apple/action children are published before the shared actor pass.
        // The ROM services that child on every subsequent phase until its
        // terrain handoff clears/replaces this boundary. Keep that cadence
        // on the VM instead of inferring it from ActorHostMeta in the loop.
        actor_service_boundary_ = ActorServiceBoundary::ActorServiceEveryPhase;
        return false;
    case ActorServiceBoundary::ActorDeferOnGate:
        if (!defer_gate) return false;
        actor_service_boundary_ = ActorServiceBoundary::None;
        return true;
    case ActorServiceBoundary::ActorDeferThenForce:
        if (!defer_gate) return false;
        actor_service_boundary_ = ActorServiceBoundary::ForceNextUpdate;
        return false;
    case ActorServiceBoundary::ForceNextUpdate:
        actor_service_boundary_ = ActorServiceBoundary::None;
        return true;
    case ActorServiceBoundary::ActorServiceEveryPhase:
        return true;
    default:
        return scheduler_service;
    }
}

bool PlayerAnimationVm::consume_actor_retirement_defer() {
    if (actor_service_boundary_ != ActorServiceBoundary::ActorRetireNextUpdate) return false;
    actor_service_boundary_ = ActorServiceBoundary::None;
    return true;
}

bool PlayerAnimationVm::actor_service_deferred() const {
    return actor_service_boundary_ == ActorServiceBoundary::ActorDeferUntilGate
        || actor_service_boundary_ == ActorServiceBoundary::ActorDeferUntilGateThenEveryPhase
        || actor_service_boundary_ == ActorServiceBoundary::ActorDeferOnGate
        || actor_service_boundary_ == ActorServiceBoundary::ActorDeferThenForce;
}

bool PlayerAnimationVm::actor_service_forced() const {
    return actor_service_boundary_ == ActorServiceBoundary::ForceNextUpdate;
}

bool PlayerAnimationVm::update_actor(
    ActorState& actor,
    const AnimationContext& context,
    AnimationServices* services
) {
    actor_retired_ = false;
    if (!rom_mode_ || actor.animation_pc == 0) return false;

    ram_.bind_actor(actor, actor_);
    ram_.bind_context(context);
    animation_pc_ = actor.animation_pc;
    timer_ = actor.animation_timer;
    // ActorAnimationVM's FC/return path is stored in the live actor record at
    // +0x38 by the ROM. Rehydrate the VM cursor before the tick and publish it
    // back after the command stream so in-place response transitions retain
    // the same return address.
    return_pc_ = actor.movement_return_pc;

    actor_tick_ = true;
    RamContextScope context_scope(ram_);
    tick_actor_rom(context, actor, services);
    actor_tick_ = false;

    if (actor_retired_) return true;

    actor.animation_pc = animation_pc_;
    actor.frame_ptr = frame_pointer_;
    actor.animation_timer = read_memory8(0x37);
    actor.movement_return_pc = return_pc_;
    return false;
}

bool PlayerAnimationVm::take_spawn_command(F5Command& command) {
    if (unhandled_spawn_commands_.empty()) return false;
    command = unhandled_spawn_commands_.front();
    unhandled_spawn_commands_.erase(unhandled_spawn_commands_.begin());
    return true;
}

void PlayerAnimationVm::defer_spawn_command(const F5Command& command) {
    deferred_spawn_command_ = command;
}

bool PlayerAnimationVm::take_deferred_spawn_command(F5Command& command) {
    if (!deferred_spawn_command_) return false;
    command = *deferred_spawn_command_;
    deferred_spawn_command_.reset();
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
    return ram_.take_write(address, value);
}

void PlayerAnimationVm::select_stream_entry(
    std::uint32_t stream_entry,
    bool publish_frame_pointer,
    bool defer_first_tick
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
    SpritePose pose,
    bool publish_frame_pointer
) {
    if (!rom_mode_) return;
    pose_ = pose;
    stream_kind_ = AnimationStreamKind::Locomotion;
    stream_entry_ = stream_entry;
    animation_pc_ = stream_entry;
    timer_ = 0;
    if (publish_frame_pointer) {
        const std::uint16_t reference = read_rom16(stream_entry);
        set_frame_pointer(read_rom32(reference));
    }
    landing_finished_ = false;
    landing_reselect_pending_ = false;
    if (defer_first_tick) {
        ++update_count_;
    }
}

void PlayerAnimationVm::arm_landing_reselect() {
    if (rom_mode_) landing_reselect_pending_ = 2;
}

void PlayerAnimationVm::clear_landing_idle_hold() {
    if (rom_mode_ && landing_reselect_pending_ == 4) {
        landing_reselect_pending_ = 0;
    }
}

void PlayerAnimationVm::select_response_stream(std::uint32_t stream_entry, int timer) {
    if (!rom_mode_) return;
    stream_kind_ = AnimationStreamKind::Response;
    stream_entry_ = stream_entry;
    animation_pc_ = stream_entry;
    timer_ = std::clamp(timer, 0, 0x7F);
    write_memory8(0x37, static_cast<std::uint8_t>(timer_));
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

    sync_context(context);
    RamContextScope context_scope(ram_);

    // This is the control flow of Player_ProcessInteractionState at
    // 0x001AE4F8. All selector inputs are read from the bound GameState view;
    // the context only supplies an explicit caller-side override when a
    // selector is invoked between two native state boundaries.
    if (read_memory8(0xFFF0E7) != 0
        || read_memory8(0xFFF0E6) != 0
        || read_memory8(0xFFF0E9) != 0
        || read_memory8(0xFFF0F2) != 0) {
        return false;
    }

    const bool normal_path = read_memory8(0xFFF0BE) == 0
        && read_memory8(0xFFF0C1) != 0
        && read_memory8(0xFFF0D0) == 0
        && read_memory8(0xFFF0D7) == 0
        && read_memory8(0xFFF0CD) == 0
        && read_memory8(0xFFF0D4) == 0;
    const bool response_timer_was_clear = read_memory8(0xFFF0CC) == 0;
    if (normal_path) {
        if (read_memory8(0xFFF173) != 0) {
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
            && read_memory8(0xFFF0ED) != 0;
        const bool can_enter_stop_stream =
            (stream_kind_ == AnimationStreamKind::Locomotion
                && pose_ != SpritePose::Brake
                && (stream_entry_ == kIdleStream || stream_entry_ == kRunStream))
            || response_stop_handoff;
        if (can_enter_stop_stream
            && read_memory8(0xFFF0CC) == 0
            && read_memory8(0xFFEFFF) == 0
            && read_memory8(0xFFF11F) == 0) {
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
    if (read_memory8(0xFFF57D) != 0) {
        sound_requests_.push_back(kInteractionEventSoundId);
    }
    return normal_path && read_memory8(0xFFF173) == 0
        && response_timer_was_clear
        && read_memory8(0xFFEFFF) == 0
        && read_memory8(0xFFF11F) == 0;
}

bool PlayerAnimationVm::finished() const {
    if (rom_mode_) return landing_finished_;
    const Clip& current = clip(pose_);
    return !current.loop && step_ + 1 == current.steps.size() && timer_ <= 1;
}

void PlayerAnimationVm::update(
    SpritePose desired_pose,
    HorizontalDirection horizontal_direction,
    const AnimationContext& context,
    std::optional<std::uint8_t> scheduler_phase,
    AnimationServices* services
) {
    if (clear_timer_next_update_) {
        timer_ = 0;
        write_memory8(0x37, 0);
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
    RamContextScope context_scope(ram_);
    if (read_memory8(0xFFF0F2) == 0x28) {
        // Actor flag bit 5 restarts the Genesis run stream. The surface
        // interaction path uses the same lock after selecting the stop
        // stream, so preserve that root when the player is braking at a
        // zero terrain-response boundary.
        if (stream_kind_ == AnimationStreamKind::Locomotion
            && desired_pose == SpritePose::Run) {
            select_rom_stream(SpritePose::Run, false, &context);
        } else if (desired_pose == SpritePose::Brake
                   && read_memory8(0xFFF0CC) == 0
                   && read_memory16(0xFFF0B0) == 0) {
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
    const bool scheduler_service = scheduler_phase
        ? ((*scheduler_phase & 1U) != 0)
        : ((update_count_ & 1U) == 0);
    if (pose_ == SpritePose::Landing
        && landing_reselect_pending_ != 0
        && (landing_reselect_pending_ == 2
            || animation_pc_ == kLandingStream)
        && !scheduler_service) {
        // The gameplay state selector writes the landing root again on the
        // intervening frame; the VM then consumes its first frame on the next
        // actor tick. This one-frame root write is visible in FF7E60.
        animation_pc_ = kLandingStream;
        landing_reselect_pending_ = landing_reselect_pending_ == 2 ? 3 : 0;
    }
    if (scheduler_phase) {
        if (scheduler_service) tick_rom(context, services);
        return;
    }
    if ((update_count_++ & 1U) == 0) {
        tick_rom(context, services);
    }
}

int PlayerAnimationVm::sprite_frame() const {
    return rom_mode_ ? -1 : clip(pose_).steps[step_].sprite_frame;
}

std::uint32_t PlayerAnimationVm::stream_entry() const {
    return rom_mode_ ? stream_entry_ : clip(pose_).stream_entry;
}

void PlayerAnimationVm::write_checkpoint(std::ostream& output) const {
    checkpoint::Writer writer(output);
    writer.u8(static_cast<std::uint8_t>(pose_));
    writer.u32(static_cast<std::uint32_t>(step_));
    writer.i32(timer_);
    writer.boolean(facing_left_);
    writer.u8(static_cast<std::uint8_t>(horizontal_direction_));
    writer.u8(static_cast<std::uint8_t>(stream_kind_));
    writer.boolean(rom_mode_);
    std::array<std::uint8_t, 0x10000> memory{};
    std::array<std::uint8_t, 0x10000> memory_write_flags{};
    ram_.copy_legacy_memory(memory);
    ram_.copy_legacy_write_flags(memory_write_flags);
    writer.bytes(memory.data(), memory.size());
    writer.bytes(memory_write_flags.data(), memory_write_flags.size());
    writer.bytes(actor_.data(), actor_.size());
    writer.u32(animation_pc_);
    writer.u32(frame_pointer_);
    writer.u32(stream_entry_);
    writer.u32(return_pc_);
    writer.u8(random_value_);
    writer.boolean(actor_tick_);
    // The former deferred retirement packet is no longer used by the bound
    // runtime; retain its empty wire slot for checkpoint compatibility.
    writer.boolean(false);
    writer.u8(static_cast<std::uint8_t>(actor_service_boundary_));
    writer.boolean(clear_timer_next_update_);
    writer.boolean(tracking_memory_writes_);
    writer.u32(static_cast<std::uint32_t>(unhandled_spawn_commands_.size()));
    for (const F5Command& command : unhandled_spawn_commands_) {
        write_spawn_command(writer, command);
    }
    writer.boolean(deferred_spawn_command_.has_value());
    if (deferred_spawn_command_) write_spawn_command(writer, *deferred_spawn_command_);
    writer.byte_vector(sound_requests_);
    writer.u32(update_count_);
    writer.boolean(landing_finished_);
    writer.u8(landing_reselect_pending_);
}

void PlayerAnimationVm::read_checkpoint(std::istream& input) {
    checkpoint::Reader reader(input);
    const auto pose = reader.u8();
    const auto step = reader.u32();
    const auto timer = reader.i32();
    const bool facing_left = reader.boolean();
    const auto horizontal_direction = reader.u8();
    const auto stream_kind = reader.u8();
    const bool rom_mode = reader.boolean();
    if (pose > static_cast<std::uint8_t>(SpritePose::Landing)
        || horizontal_direction > static_cast<std::uint8_t>(HorizontalDirection::Right)
        || stream_kind > static_cast<std::uint8_t>(AnimationStreamKind::Action)) {
        throw std::runtime_error("invalid animation VM enum in OpenAladdin checkpoint");
    }
    if (rom_mode != rom_mode_) {
        throw std::runtime_error("animation VM ROM mode does not match OpenAladdin checkpoint");
    }
    std::array<std::uint8_t, 0x10000> memory{};
    std::array<std::uint8_t, 0x10000> memory_write_flags{};
    std::array<std::uint8_t, 0x42> actor{};
    reader.bytes(memory.data(), memory.size());
    reader.bytes(memory_write_flags.data(), memory_write_flags.size());
    reader.bytes(actor.data(), actor.size());
    const auto animation_pc = reader.u32();
    const auto frame_pointer = reader.u32();
    const auto stream_entry = reader.u32();
    const auto return_pc = reader.u32();
    const auto random_value = reader.u8();
    const bool actor_tick = reader.boolean();
    const bool has_actor_retirement_request = reader.boolean();
    if (has_actor_retirement_request) (void)reader.u8();
    const auto actor_service_boundary = reader.u8();
    const bool clear_timer_next_update = reader.boolean();
    const bool tracking_memory_writes = reader.boolean();
    const auto spawn_count = reader.u32();
    if (spawn_count > 4096) {
        throw std::runtime_error("oversized animation spawn queue in OpenAladdin checkpoint");
    }
    std::vector<F5Command> unhandled_spawn_commands;
    unhandled_spawn_commands.reserve(spawn_count);
    for (std::uint32_t index = 0; index < spawn_count; ++index) {
        unhandled_spawn_commands.push_back(read_spawn_command(reader));
    }
    const bool has_deferred_spawn_command = reader.boolean();
    std::optional<F5Command> deferred_spawn_command;
    if (has_deferred_spawn_command) {
        deferred_spawn_command = read_spawn_command(reader);
    }
    auto sound_requests = reader.byte_vector(4096);
    const auto update_count = reader.u32();
    const bool landing_finished = reader.boolean();
    const auto landing_reselect_pending = reader.u8();
    if (landing_reselect_pending > 4) {
        throw std::runtime_error("invalid landing reselect mode in OpenAladdin checkpoint");
    }
    if (!rom_mode_ && step >= clip(static_cast<SpritePose>(pose)).steps.size()) {
        throw std::runtime_error("invalid animation VM clip step in OpenAladdin checkpoint");
    }
    if (timer < 0) {
        throw std::runtime_error("invalid animation VM timer in OpenAladdin checkpoint");
    }
    if (actor_service_boundary > static_cast<std::uint8_t>(ActorServiceBoundary::ActorServiceEveryPhase)) {
        throw std::runtime_error("invalid animation service boundary in OpenAladdin checkpoint");
    }

    pose_ = static_cast<SpritePose>(pose);
    step_ = step;
    timer_ = timer;
    facing_left_ = facing_left;
    horizontal_direction_ = static_cast<HorizontalDirection>(horizontal_direction);
    stream_kind_ = static_cast<AnimationStreamKind>(stream_kind);
    ram_.restore_legacy_memory(memory, memory_write_flags);
    actor_ = actor;
    animation_pc_ = animation_pc;
    frame_pointer_ = frame_pointer;
    stream_entry_ = stream_entry;
    return_pc_ = return_pc;
    random_value_ = random_value;
    actor_tick_ = actor_tick;
    actor_service_boundary_ = static_cast<ActorServiceBoundary>(actor_service_boundary);
    clear_timer_next_update_ = clear_timer_next_update;
    tracking_memory_writes_ = tracking_memory_writes;
    ram_.set_write_tracking(tracking_memory_writes_);
    unhandled_spawn_commands_ = std::move(unhandled_spawn_commands);
    deferred_spawn_command_ = std::move(deferred_spawn_command);
    actor_retired_ = false;
    sound_requests_ = std::move(sound_requests);
    update_count_ = update_count;
    landing_finished_ = landing_finished;
    landing_reselect_pending_ = landing_reselect_pending;
}

}  // namespace openaladdin
