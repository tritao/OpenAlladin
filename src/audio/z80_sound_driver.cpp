#include "audio/z80_sound_driver.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

namespace openaladdin::audio {
namespace {

constexpr std::size_t kUnknownCommand = std::numeric_limits<std::size_t>::max();

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes,
                       std::size_t address) {
    if (address > bytes.size() || bytes.size() - address < 2) {
        throw std::out_of_range("Z80 sound sequence pointer is outside the ROM");
    }
    return static_cast<std::uint16_t>(bytes[address])
        | (static_cast<std::uint16_t>(bytes[address + 1]) << 8);
}

}  // namespace

Z80SoundDriver::Z80SoundDriver(std::span<const std::uint8_t> rom,
                               EventHandler event_handler)
    : rom_(rom.begin(), rom.end()),
      event_handler_(std::move(event_handler)) {
    if (rom_.size() <= kSequenceTableBase) {
        throw std::invalid_argument(
            "Z80 sound ROM must contain the recovered sequence table");
    }
}

void Z80SoundDriver::reset() {
    queue_read_ = 0;
    queue_write_ = 0;
    queue_size_ = 0;
    queue_command_count_ = 0;
    sequence_table_base_ = kSequenceTableBase;
    channels_ = {};
}

std::size_t Z80SoundDriver::command_argument_count(
    std::uint8_t opcode) noexcept {
    // These lengths are the handlers whose argument reads are recovered in
    // z80-driver.yml/disassembly. Keeping unknown commands out of the queue
    // prevents a malformed command from desynchronizing all later writes.
    switch (opcode) {
    case 0x0B:
        return 12;
    case 0x0C:
    case 0x0D:
    case 0x16:
        return 0;
    case 0x0E:
    case 0x14:
    case 0x1A:
    case 0x1B:
    case 0x1F:
        return 2;
    case 0x10:
    case 0x12:
    case 0x1C:
    case 0x1D:
    case 0x20:
        return 1;
    case 0x17:
        return 3;
    default:
        return kUnknownCommand;
    }
}

void Z80SoundDriver::command(std::uint8_t opcode,
                             std::span<const std::uint8_t> args) {
    const std::size_t expected = command_argument_count(opcode);
    if (expected == kUnknownCommand) {
        throw std::invalid_argument("unsupported Z80 sound command");
    }
    if (args.size() != expected) {
        throw std::invalid_argument("wrong Z80 sound command argument count");
    }
    process_command(opcode, args);
}

void Z80SoundDriver::enqueue_command(std::uint8_t opcode,
                                     std::span<const std::uint8_t> args) {
    const std::size_t expected = command_argument_count(opcode);
    if (expected == kUnknownCommand) {
        throw std::invalid_argument("unsupported Z80 sound command");
    }
    if (args.size() != expected) {
        throw std::invalid_argument("wrong Z80 sound command argument count");
    }

    const std::size_t packet_size = args.size() + 1;
    if (packet_size > kCommandQueueCapacity - 1
        || queue_size_ > kCommandQueueCapacity - 1 - packet_size) {
        throw std::overflow_error("Z80 sound command queue is full");
    }

    const auto push = [this](std::uint8_t value) {
        command_queue_[queue_write_] = value;
        queue_write_ = (queue_write_ + 1) % kCommandQueueCapacity;
        ++queue_size_;
    };
    push(opcode);
    for (const std::uint8_t arg : args) {
        push(arg);
    }
    ++queue_command_count_;
}

void Z80SoundDriver::tick() {
    if (queue_size_ != 0) {
        const auto pop = [this]() {
            const std::uint8_t value = command_queue_[queue_read_];
            queue_read_ = (queue_read_ + 1) % kCommandQueueCapacity;
            --queue_size_;
            return value;
        };

        const std::uint8_t opcode = pop();
        const std::size_t argument_count = command_argument_count(opcode);
        if (argument_count == kUnknownCommand
            || argument_count > queue_size_) {
            throw std::runtime_error("Z80 sound command queue packet is invalid");
        }
        std::array<std::uint8_t, 12> args{};
        for (std::size_t i = 0; i < argument_count; ++i) {
            args[i] = pop();
        }
        --queue_command_count_;
        process_command(opcode, std::span<const std::uint8_t>(
            args.data(), argument_count));
    }

    for (std::size_t index = 0; index < channels_.size(); ++index) {
        process_channel(index);
    }
}

const Z80SoundDriver::ChannelState& Z80SoundDriver::channel(
    std::size_t index) const {
    if (index >= channels_.size()) {
        throw std::out_of_range("Z80 sound channel index is outside the driver");
    }
    return channels_[index];
}

void Z80SoundDriver::process_command(std::uint8_t opcode,
                                     std::span<const std::uint8_t> args) {
    switch (opcode) {
    case 0x0B:
        sequence_table_base_ = static_cast<std::uint32_t>(args[6])
            | (static_cast<std::uint32_t>(args[7]) << 8)
            | (static_cast<std::uint32_t>(args[8]) << 16);
        if (sequence_table_base_ >= rom_.size()) {
            throw std::out_of_range(
                "Z80 sound command selected a sequence table outside the ROM");
        }
        break;
    case 0x10:
        start_sound(args[0]);
        break;
    case 0x12:
        stop_sound(args[0]);
        break;
    case 0x16:
        for (ChannelState& channel : channels_) {
            channel.active = false;
        }
        break;
    case 0x17:
        // This command changes the original driver's per-track flags. The
        // stream/event layer does not synthesize those flags yet, but it does
        // consume the complete packet so subsequent commands stay aligned.
        break;
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x14:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1F:
    case 0x20:
        // State handlers not needed by the sequence decoder are deliberately
        // retained as consumed no-ops until their hardware side effects are
        // recovered. They are safe to pass through the mailbox model.
        break;
    default:
        throw std::invalid_argument("unsupported Z80 sound command");
    }
}

void Z80SoundDriver::start_sound(std::uint8_t sound_id) {
    const std::size_t pointer_address =
        static_cast<std::size_t>(sequence_table_base_) + sound_id * 2;
    const std::uint32_t header_offset = read_u16(rom_, pointer_address);
    const std::size_t header_address =
        static_cast<std::size_t>(sequence_table_base_) + header_offset;
    if (header_address >= rom_.size() || rom_.size() - header_address < 0x21) {
        throw std::out_of_range("Z80 sound header is outside the ROM");
    }

    const std::uint8_t track_count = rom_[header_address];
    if (track_count > kChannelCount) {
        throw std::runtime_error("Z80 sound header has too many tracks");
    }

    std::size_t next_channel = 0;
    for (std::uint8_t track = 0; track < track_count; ++track) {
        while (next_channel < channels_.size() && channels_[next_channel].active) {
            ++next_channel;
        }
        if (next_channel == channels_.size()) {
            break;
        }

        const std::size_t track_offset_address = header_address + 1 + track * 2;
        const std::uint32_t stream_offset = read_u16(rom_, track_offset_address);
        const std::size_t stream_address =
            static_cast<std::size_t>(sequence_table_base_) + stream_offset;
        if (stream_address >= rom_.size()) {
            throw std::out_of_range("Z80 sound track is outside the ROM");
        }

        channels_[next_channel] = ChannelState{
            true,
            sound_id,
            track,
            static_cast<std::uint32_t>(stream_address),
            0,
            0,
            0,
        };
        ++next_channel;
    }
}

void Z80SoundDriver::stop_sound(std::uint8_t sound_id) {
    for (ChannelState& channel : channels_) {
        if (channel.active && channel.sound_id == sound_id) {
            channel.active = false;
        }
    }
}

void Z80SoundDriver::process_channel(std::size_t index) {
    ChannelState& channel = channels_[index];
    if (!channel.active) {
        return;
    }
    if (channel.event_timer != 0) {
        --channel.event_timer;
        return;
    }

    // The original routine loops while operand A is zero, allowing control
    // events and zero-duration notes to share a tick. The guard keeps a
    // malformed stream from hanging the native loop forever.
    constexpr std::size_t kMaxEventsPerTick = 256;
    for (std::size_t event_count = 0;
         event_count < kMaxEventsPerTick && channel.active;
         ++event_count) {
        const SoundEvent event = read_event(index);
        if (event_handler_) {
            event_handler_(event);
        }

        if (!channel.active) {
            break;
        }

        const std::int32_t operand = channel.operand_a;
        if (operand != 0) {
            const std::int32_t duration = operand < 0 ? -operand : operand;
            channel.event_timer = static_cast<std::uint16_t>(
                std::min(duration, static_cast<std::int32_t>(
                    std::numeric_limits<std::uint16_t>::max())));
            break;
        }
    }
}

Z80SoundDriver::SoundEvent Z80SoundDriver::read_event(std::size_t index) {
    ChannelState& channel = channels_[index];
    const std::uint32_t stream_address = channel.stream_cursor;
    std::uint8_t opcode = read_stream_byte(channel);

    while (opcode >= 0x80) {
        std::uint64_t value = opcode & 0x3F;
        const bool operand_a = opcode >= 0xC0;
        while (true) {
            const std::uint8_t next = read_stream_byte(channel);
            if ((next & 0xC0) == 0xC0) {
                value = (value << 6) | (next & 0x3F);
                continue;
            }
            if (operand_a) {
                channel.operand_a = decode_signed_operand(value);
            } else {
                channel.operand_b = decode_signed_operand(value);
            }
            opcode = next;
            break;
        }
    }

    SoundEvent event{
        opcode < 0x60 ? SoundEvent::Kind::Note : SoundEvent::Kind::Control,
        static_cast<std::uint8_t>(index),
        opcode,
        stream_address,
        channel.operand_a,
        channel.operand_b,
        false,
        0,
    };

    if (opcode == 0x60) {
        channel.active = false;
    } else if (control_has_argument(opcode)) {
        event.has_control_argument = true;
        event.control_argument = read_stream_byte(channel);
    }

    return event;
}

std::uint8_t Z80SoundDriver::read_stream_byte(ChannelState& channel) {
    if (channel.stream_cursor >= rom_.size()) {
        throw std::out_of_range("Z80 sound stream reached the end of the ROM");
    }
    return rom_[channel.stream_cursor++];
}

std::int16_t Z80SoundDriver::decode_signed_operand(std::uint64_t value) noexcept {
    const std::uint16_t encoded = static_cast<std::uint16_t>(0U - value);
    return std::bit_cast<std::int16_t>(encoded);
}

bool Z80SoundDriver::control_has_argument(std::uint8_t opcode) noexcept {
    switch (opcode) {
    case 0x61:
    case 0x62:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6C:
    case 0x6E:
    case 0x6F:
    case 0x70:
    case 0x71:
        return true;
    default:
        return false;
    }
}

}  // namespace openaladdin::audio
