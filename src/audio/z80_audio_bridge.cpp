#include "audio/z80_audio_bridge.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace openaladdin::audio {
namespace {

// Z80 ROM $1168, little-endian. These are the normalized YM FNUM values
// selected by the driver's twelve-step note interpolation path.
constexpr std::array<std::uint16_t, 12> kYmFnumTable{
    0x0284, 0x02AA, 0x02D3, 0x02FE,
    0x032B, 0x035B, 0x038E, 0x03C5,
    0x03FE, 0x043B, 0x047B, 0x04BF,
};
// Z80 ROM $1182, little-endian. The PSG path subtracts $21 from the
// transformed note and uses the result as a direct index into this table.
constexpr std::uint8_t kPsgFirstNote = 0x21;
constexpr std::array<std::uint16_t, 64> kPsgPeriodTable{
    0x03F9, 0x03C0, 0x038A, 0x0357, 0x0327, 0x02FA, 0x02CF, 0x02A7,
    0x0281, 0x025D, 0x023B, 0x021B, 0x01FC, 0x01E0, 0x01C5, 0x01AC,
    0x0194, 0x017D, 0x0168, 0x0153, 0x0140, 0x012E, 0x011D, 0x010D,
    0x00FE, 0x00F0, 0x00E2, 0x00D6, 0x00CA, 0x00BE, 0x00B4, 0x00AA,
    0x00A0, 0x0097, 0x008F, 0x0087, 0x007F, 0x0078, 0x0071, 0x006B,
    0x0065, 0x005F, 0x005A, 0x0055, 0x0050, 0x004C, 0x0047, 0x0043,
    0x0040, 0x003C, 0x0039, 0x0035, 0x0032, 0x002F, 0x002D, 0x002A,
    0x0028, 0x0026, 0x0023, 0x0021, 0x0020, 0x001E, 0x001C, 0x001C,
};
// Z80 driver route records at $1791/$17BC. Values are the hardware channel
// selectors, not dense native voice indices: $04/$05/$06 select YM port 2.
constexpr std::array<std::uint8_t, 6> kYmChannelOrder{0, 1, 4, 5, 6, 2};

std::uint8_t ym_port(std::uint8_t hardware_channel) {
    return hardware_channel >= 4 ? 2 : 0;
}

std::uint8_t ym_channel(std::uint8_t hardware_channel) {
    return hardware_channel >= 4
        ? static_cast<std::uint8_t>(hardware_channel - 4)
        : hardware_channel;
}

}  // namespace

Z80AudioBridge::Z80AudioBridge(Bus bus)
    : bus_(std::move(bus)) {
}

void Z80AudioBridge::reset() {
    for (const std::uint8_t hardware_channel : kYmChannelOrder) {
        key_off_ym(hardware_channel);
    }
    for (std::size_t voice = 0; voice < 4; ++voice) {
        mute_psg(voice);
    }
    ym_keyed_.fill(false);
    ym_channel_in_use_.fill(false);
    has_ym_channel_for_stream_.fill(false);
    ym_channel_for_stream_.fill(0);
    has_ym_patch_.fill(false);
    for (auto& patch : ym_patches_) {
        patch.fill(0);
    }
}

void Z80AudioBridge::handle(const Z80SoundDriver::SoundEvent& event) {
    const bool use_psg = event.output == Z80SoundDriver::Output::Psg
        || (event.output == Z80SoundDriver::Output::Unknown
            && event.channel >= kYmVoiceCount);
    if (event.kind == Z80SoundDriver::SoundEvent::Kind::Note) {
        if (use_psg) {
            handle_psg_note(event.channel % 4, event.opcode);
        } else if (event.channel < kStreamChannelCount) {
            handle_ym_note(event.channel, event.opcode);
        }
        return;
    }

    if (event.opcode == 0x61 && event.channel < kStreamChannelCount
        && event.output != Z80SoundDriver::Output::Psg
        && event.has_patch_state && is_ym_patch(event.patch_state)) {
        ym_patches_[event.channel] = event.patch_state;
        has_ym_patch_[event.channel] = true;
        return;
    }

    if (event.opcode == 0x60) {
        if (use_psg) {
            mute_psg(event.channel % 4);
        } else if (event.channel < kStreamChannelCount) {
            release_ym_channel(event.channel);
        }
    }
}

void Z80AudioBridge::write_ym_register(std::uint8_t hardware_channel,
                                       std::uint8_t address,
                                       std::uint8_t data) {
    if (!bus_.write_ym2612) {
        return;
    }
    const std::uint8_t port = ym_port(hardware_channel);
    bus_.write_ym2612(port, address);
    bus_.write_ym2612(static_cast<std::uint8_t>(port + 1), data);
}

void Z80AudioBridge::configure_ym_voice(std::uint8_t hardware_channel) {
    // One simple algorithm-7 patch: all operators are active, with the first
    // three acting as quiet modulators and the fourth as the carrier.
    const std::uint8_t channel = ym_channel(hardware_channel);
    constexpr std::array<std::uint8_t, 4> kOperatorSlots{0, 4, 8, 12};
    for (std::size_t operator_index = 0;
         operator_index < kOperatorSlots.size();
         ++operator_index) {
        const std::uint8_t offset = static_cast<std::uint8_t>(
            kOperatorSlots[operator_index] + channel);
        write_ym_register(hardware_channel,
                          static_cast<std::uint8_t>(0x30 + offset), 0x01);
        write_ym_register(
            hardware_channel,
            static_cast<std::uint8_t>(0x40 + offset),
            operator_index == 3 ? 0x00 : 0x7F);
        write_ym_register(hardware_channel,
                          static_cast<std::uint8_t>(0x50 + offset), 0x1F);
        write_ym_register(hardware_channel,
                          static_cast<std::uint8_t>(0x60 + offset), 0x00);
        write_ym_register(hardware_channel,
                          static_cast<std::uint8_t>(0x70 + offset), 0x00);
        write_ym_register(hardware_channel,
                          static_cast<std::uint8_t>(0x80 + offset), 0x0F);
    }
    write_ym_register(hardware_channel,
                      static_cast<std::uint8_t>(0xB0 + channel), 0x07);
    write_ym_register(hardware_channel,
                      static_cast<std::uint8_t>(0xB4 + channel), 0xC0);
}

void Z80AudioBridge::configure_ym_patch(
    std::uint8_t hardware_channel,
    const Z80SoundDriver::PatchState& patch_state) {
    // The 0x61 state loader copies a three-byte per-voice prefix followed by
    // the 32-byte YM patch payload. The payload is B0/B4, then four groups of
    // six operator values (MUL, TL, AR, D1R, D2R, SL/RR). The hardware emits
    // operators in the YM slot order 0, 2, 1, 3.
    constexpr std::size_t kPayloadOffset = 3;
    constexpr std::array<std::uint8_t, 4> kOperatorOffsets{0, 8, 4, 12};
    constexpr std::array<std::uint8_t, 4> kPayloadGroups{0, 1, 2, 3};
    constexpr std::array<std::uint8_t, 6> kOperatorRegisters{
        0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    const std::uint8_t channel = ym_channel(hardware_channel);
    write_ym_register(hardware_channel,
                      static_cast<std::uint8_t>(0xB0 + channel),
                      patch_state[kPayloadOffset]);
    write_ym_register(hardware_channel,
                      static_cast<std::uint8_t>(0xB4 + channel),
                      patch_state[kPayloadOffset + 1]);

    for (std::size_t operator_index = 0;
         operator_index < kOperatorOffsets.size(); ++operator_index) {
        const std::size_t payload_index = kPayloadOffset + 2
            + kPayloadGroups[operator_index] * kOperatorRegisters.size();
        const std::uint8_t offset = static_cast<std::uint8_t>(
            kOperatorOffsets[operator_index] + channel);
        for (std::size_t register_index = 0;
             register_index < kOperatorRegisters.size(); ++register_index) {
            write_ym_register(
                hardware_channel,
                static_cast<std::uint8_t>(
                    kOperatorRegisters[register_index] + offset),
                patch_state[payload_index + register_index]);
        }
        // The ROM format has no ninth per-operator byte; the original driver
        // clears the 0x90-series registers while applying a patch.
        write_ym_register(hardware_channel,
                          static_cast<std::uint8_t>(0x90 + offset), 0);
    }
}

void Z80AudioBridge::handle_ym_note(std::size_t stream_channel,
                                    std::uint8_t note) {
    const std::uint8_t hardware_channel = allocate_ym_channel(stream_channel);
    if (ym_keyed_[hardware_channel]) {
        key_off_ym(hardware_channel);
    }

    if (has_ym_patch_[stream_channel]) {
        configure_ym_patch(hardware_channel, ym_patches_[stream_channel]);
    } else {
        configure_ym_voice(hardware_channel);
    }

    const auto [block, fnum] = ym_frequency(note);
    const std::uint8_t channel = ym_channel(hardware_channel);
    write_ym_register(hardware_channel,
        static_cast<std::uint8_t>(0xA4 + channel),
        static_cast<std::uint8_t>((block << 3) | (fnum >> 8)));
    write_ym_register(hardware_channel,
                      static_cast<std::uint8_t>(0xA0 + channel),
                      static_cast<std::uint8_t>(fnum));
    if (bus_.write_ym2612) {
        bus_.write_ym2612(0, 0x28);
        bus_.write_ym2612(
            1,
            static_cast<std::uint8_t>(0xF0 | channel
                                       | (hardware_channel >= 4 ? 0x04 : 0x00)));
    }
    ym_keyed_[hardware_channel] = true;
}

void Z80AudioBridge::handle_psg_note(std::size_t voice, std::uint8_t note) {
    if (!bus_.write_psg) {
        return;
    }
    const std::uint16_t period = psg_period(note);
    bus_.write_psg(static_cast<std::uint8_t>(0x80 | (voice << 5)
                                               | (period & 0x0F)));
    bus_.write_psg(static_cast<std::uint8_t>((period >> 4) & 0x3F));
    bus_.write_psg(static_cast<std::uint8_t>(0x90 | (voice << 5)));
}

void Z80AudioBridge::key_off_ym(std::uint8_t hardware_channel) {
    if (bus_.write_ym2612) {
        bus_.write_ym2612(0, 0x28);
        bus_.write_ym2612(
            1,
            static_cast<std::uint8_t>(ym_channel(hardware_channel)
                                       | (hardware_channel >= 4 ? 0x04 : 0x00)));
    }
    ym_keyed_[hardware_channel] = false;
}

std::uint8_t Z80AudioBridge::allocate_ym_channel(std::size_t stream_channel) {
    if (has_ym_channel_for_stream_[stream_channel]) {
        return ym_channel_for_stream_[stream_channel];
    }

    for (const std::uint8_t hardware_channel : kYmChannelOrder) {
        if (!ym_channel_in_use_[hardware_channel]) {
            ym_channel_for_stream_[stream_channel] = hardware_channel;
            has_ym_channel_for_stream_[stream_channel] = true;
            ym_channel_in_use_[hardware_channel] = true;
            return hardware_channel;
        }
    }

    // The original allocator has six FM slots. If a malformed or unusually
    // dense native event stream exceeds that limit, keep the stream audible
    // by reusing its numeric slot deterministically.
    return kYmChannelOrder[stream_channel % kYmChannelOrder.size()];
}

void Z80AudioBridge::release_ym_channel(std::size_t stream_channel) {
    if (!has_ym_channel_for_stream_[stream_channel]) {
        return;
    }
    const std::uint8_t hardware_channel =
        ym_channel_for_stream_[stream_channel];
    key_off_ym(hardware_channel);
    ym_channel_in_use_[hardware_channel] = false;
    has_ym_channel_for_stream_[stream_channel] = false;
}

void Z80AudioBridge::mute_psg(std::size_t voice) {
    if (bus_.write_psg) {
        bus_.write_psg(static_cast<std::uint8_t>(0x90 | (voice << 5) | 0x0F));
    }
}

std::pair<std::uint8_t, std::uint16_t> Z80AudioBridge::ym_frequency(
    std::uint8_t note) {
    const std::uint8_t clamped_note = std::min<std::uint8_t>(note, 0x5F);
    const std::uint8_t block = static_cast<std::uint8_t>(clamped_note / 12);
    const std::size_t table_index = clamped_note % 12;
    return {block, kYmFnumTable[table_index]};
}

std::uint16_t Z80AudioBridge::psg_period(std::uint8_t note) {
    const std::size_t index = std::clamp<std::size_t>(
        note < kPsgFirstNote ? 0 : note - kPsgFirstNote,
        0,
        kPsgPeriodTable.size() - 1);
    return kPsgPeriodTable[index];
}

bool Z80AudioBridge::is_ym_patch(
    const Z80SoundDriver::PatchState& patch_state) {
    // The byte at offset one varies between YM patches. The stable shape is
    // the zero format/control byte at offset two, with the YM payload starting
    // at offset three.
    return patch_state[0] == 0x00 && patch_state[2] == 0x00;
}

}  // namespace openaladdin::audio
