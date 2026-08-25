#include "animation.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace openaladdin {
namespace {

constexpr std::uint32_t kIdleStream = 0x00121D9A;
constexpr std::uint32_t kRunStream = 0x00122006;
constexpr std::uint32_t kBrakeStream = 0x001232E0;
constexpr std::uint32_t kJumpStream = 0x001221B0;
constexpr std::uint32_t kLandingStream = 0x00121F84;
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
    animation_pc_ = rom_mode_ ? kIdleStream : 0;
    frame_pointer_ = 0;
    stream_entry_ = rom_mode_ ? kIdleStream : clip(pose_).stream_entry;
    return_pc_ = 0;
    // The player traces use the low branch of the F0E0 idle embellishment;
    // keep this deterministic until the shared game RNG is modelled.
    random_value_ = 0x00;
    update_count_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = false;
    memory_.fill(0);
    actor_.fill(0);
    actor_[0] = 1;
}

void PlayerAnimationVm::select(SpritePose pose) {
    pose_ = pose;
    step_ = 0;
    timer_ = clip(pose_).steps.front().duration;
}

void PlayerAnimationVm::select_rom_stream(SpritePose pose, bool execute_now) {
    pose_ = pose;
    stream_entry_ = clip(pose).stream_entry;
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
    else if (address >= 0xFF0000 && address <= 0xFFFFFF) memory_[address - 0xFF0000] = value;
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

void PlayerAnimationVm::sync_context(const AnimationContext& context) {
    write_memory16(0xFF7DFA, as_u16(context.player_x));
    write_memory16(0xFF7DFC, as_u16(context.player_y));
    write_memory16(0xFF7E02, as_u16(context.world_x));
    write_memory16(0xFF7E04, as_u16(context.world_y));
    write_memory16(0xFF7E58, as_u16(context.player_vx));
    write_memory16(0xFF7E5A, as_u16(context.player_vy));
    write_memory8(0xFFF0C1, context.grounded ? 1 : 0);
    // The ground movement path keeps this selector asserted while the
    // running stream is active; the jump handler clears it on the launch
    // boundary. Checkpoint captures do not currently carry this byte, so the
    // grounded predicate supplies the same initial value.
    write_memory8(0xFFF0CC, context.grounded
        ? std::max<std::uint8_t>(context.terrain_response_timer_state, 1)
        : context.terrain_response_timer_state);
    write_memory8(0xFF7E28, 1);
    write_memory16(2, as_u16(context.player_x));
    write_memory16(4, as_u16(context.player_y));
    write_memory8(9, facing_left_ ? 0xFF : 0);
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
        if (read_rom8(cursor - 1) != 0) { animation_pc_ = 0; cursor = 0; }
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
    case 0xF3: cursor += 2; return false;
    case 0xF4: compare_command(cursor); return false;
    case 0xF5: cursor += 16; return false;
    case 0xF6: cursor += 2; return false;
    case 0xF7:
        actor_[9] = 0;
        if (read_memory16(0xFF7E02) < read_memory16(2)) actor_[9] = 0xFF;
        cursor += 2; return false;
    case 0xF8:
        cursor = dynamic_stream(context);
        stream_entry_ = cursor;
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
    case 0xFB: cursor += 6; return false;
    case 0xFC:
        if ((read_rom8(cursor + 1) & 0x80) != 0) cursor = return_pc_;
        else { return_pc_ = cursor + 6; cursor = read_rom32(cursor + 2); }
        return false;
    case 0xFD: {
        int limit = read_rom8(cursor + 1);
        if (limit == 0xFF) limit = 0x140;
        const int distance = std::abs(context.world_x - static_cast<int>(read_memory16(2)));
        cursor += 2;
        cursor = limit >= distance ? cursor + 4 : read_rom32(cursor);
        return false;
    }
    case 0xFE: {
        const int limit = read_rom8(cursor + 1);
        const int distance = std::abs(context.world_y - static_cast<int>(read_memory16(4)));
        cursor += 2;
        cursor = limit >= distance ? cursor + 4 : read_rom32(cursor);
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

    for (int instruction = 0; instruction < 1024; ++instruction) {
        const std::uint8_t opcode = read_rom8(cursor);
        if (!is_command(opcode)) { animation_pc_ = cursor; return; }
        if (command(opcode, cursor, context)) { animation_pc_ = cursor; return; }
    }
    throw std::runtime_error("animation VM command loop exceeded 1024 instructions");
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
    animation_pc_ = animation_pc;
    timer_ = std::clamp(timer, 0, 0x7F);
    actor_[0x37] = static_cast<std::uint8_t>(timer_);
    landing_finished_ = false;
}

void PlayerAnimationVm::select_stream_entry(std::uint32_t stream_entry) {
    if (!rom_mode_) return;
    stream_entry_ = stream_entry;
    animation_pc_ = stream_entry;
    timer_ = 0;
    landing_finished_ = false;
    landing_reselect_pending_ = false;
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
        if (state.response_timer == 0
            && state.interaction_pending == 0
            && state.state_lock == 0) {
            if (stream_entry_ != kInteractionStopStream) {
                select_stream_entry(kInteractionStopStream);
                timer_ = 0;
            }
        }
    }

    // Both the selected stop state and the ordinary FFF0CC response path
    // converge at 0x001AE58E. The ROM clears these fields before dispatching
    // the interaction helper at 0x001B03F2.
    write_memory16(0xFFF0B0, 0);
    write_memory8(0xFFF0CC, 0);
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

void PlayerAnimationVm::update(SpritePose desired_pose, bool face_left_input, const AnimationContext& context) {
    if (face_left_input) facing_left_ = true;
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
    sync_context(context);
    if (desired_pose != pose_) select_rom_stream(desired_pose, false);
    if (pose_ == SpritePose::Landing && landing_reselect_pending_ && (update_count_ & 1U) != 0) {
        // The gameplay state selector writes the landing root again on the
        // intervening frame; the VM then consumes its first frame on the next
        // actor tick. This one-frame root write is visible in FF7E60.
        animation_pc_ = kLandingStream;
        landing_reselect_pending_ = false;
    }
    if ((update_count_++ & 1U) == 0) tick_rom(context);
}

int PlayerAnimationVm::sprite_frame() const {
    return rom_mode_ ? -1 : clip(pose_).steps[step_].sprite_frame;
}

std::uint32_t PlayerAnimationVm::stream_entry() const {
    return rom_mode_ ? stream_entry_ : clip(pose_).stream_entry;
}

}  // namespace openaladdin
