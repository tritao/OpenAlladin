#include "audio/z80_audio_bridge.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace openaladdin::audio {
namespace {

constexpr double kYmClockHz = 7'670'454.0;
constexpr double kPsgClockHz = 3'579'545.0;
constexpr double kSemitone = 1.0594630943592953;

std::uint8_t ym_port(std::size_t voice) {
    return voice >= 3 ? 2 : 0;
}

std::uint8_t ym_channel(std::size_t voice) {
    return static_cast<std::uint8_t>(voice % 3);
}

}  // namespace

Z80AudioBridge::Z80AudioBridge(Bus bus)
    : bus_(std::move(bus)) {
}

void Z80AudioBridge::reset() {
    for (std::size_t voice = 0; voice < kYmVoiceCount; ++voice) {
        key_off_ym(voice);
    }
    for (std::size_t voice = 0; voice < 4; ++voice) {
        mute_psg(voice);
    }
    ym_keyed_.fill(false);
    has_ym_patch_.fill(false);
    for (auto& patch : ym_patches_) {
        patch.fill(0);
    }
}

void Z80AudioBridge::handle(const Z80SoundDriver::SoundEvent& event) {
    if (event.kind == Z80SoundDriver::SoundEvent::Kind::Note) {
        if (event.channel < kYmVoiceCount) {
            handle_ym_note(event.channel, event.opcode);
        } else {
            handle_psg_note(event.channel % 4, event.opcode);
        }
        return;
    }

    if (event.opcode == 0x61 && event.channel < kYmVoiceCount
        && event.has_patch_state && is_ym_patch(event.patch_state)) {
        ym_patches_[event.channel] = event.patch_state;
        has_ym_patch_[event.channel] = true;
        return;
    }

    if (event.opcode == 0x60) {
        if (event.channel < kYmVoiceCount) {
            key_off_ym(event.channel);
        } else {
            mute_psg(event.channel % 4);
        }
    }
}

void Z80AudioBridge::write_ym_register(std::size_t voice,
                                       std::uint8_t address,
                                       std::uint8_t data) {
    if (!bus_.write_ym2612) {
        return;
    }
    const std::uint8_t port = ym_port(voice);
    bus_.write_ym2612(port, address);
    bus_.write_ym2612(static_cast<std::uint8_t>(port + 1), data);
}

void Z80AudioBridge::configure_ym_voice(std::size_t voice) {
    // One simple algorithm-7 patch: all operators are active, with the first
    // three acting as quiet modulators and the fourth as the carrier.
    const std::uint8_t channel = ym_channel(voice);
    constexpr std::array<std::uint8_t, 4> kOperatorSlots{0, 4, 8, 12};
    for (std::size_t operator_index = 0;
         operator_index < kOperatorSlots.size();
         ++operator_index) {
        const std::uint8_t offset = static_cast<std::uint8_t>(
            kOperatorSlots[operator_index] + channel);
        write_ym_register(voice, static_cast<std::uint8_t>(0x30 + offset), 0x01);
        write_ym_register(
            voice,
            static_cast<std::uint8_t>(0x40 + offset),
            operator_index == 3 ? 0x00 : 0x7F);
        write_ym_register(voice, static_cast<std::uint8_t>(0x50 + offset), 0x1F);
        write_ym_register(voice, static_cast<std::uint8_t>(0x60 + offset), 0x00);
        write_ym_register(voice, static_cast<std::uint8_t>(0x70 + offset), 0x00);
        write_ym_register(voice, static_cast<std::uint8_t>(0x80 + offset), 0x0F);
    }
    write_ym_register(voice, static_cast<std::uint8_t>(0xB0 + channel), 0x07);
    write_ym_register(voice, static_cast<std::uint8_t>(0xB4 + channel), 0xC0);
}

void Z80AudioBridge::configure_ym_patch(
    std::size_t voice,
    const Z80SoundDriver::PatchState& patch_state) {
    // The 0x61 state loader copies a three-byte per-voice prefix followed by
    // the 32-byte YM patch payload. The payload is B0/B4, then four groups of
    // six operator values (MUL, TL, AR, D1R, D2R, SL/RR). The hardware emits
    // operators in the YM slot order 0, 2, 1, 3.
    constexpr std::size_t kPayloadOffset = 3;
    constexpr std::array<std::uint8_t, 4> kOperatorOffsets{0, 8, 4, 12};
    constexpr std::array<std::uint8_t, 4> kPayloadGroups{0, 2, 1, 3};
    constexpr std::array<std::uint8_t, 6> kOperatorRegisters{
        0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    const std::uint8_t channel = ym_channel(voice);
    write_ym_register(voice, static_cast<std::uint8_t>(0xB0 + channel),
                      patch_state[kPayloadOffset]);
    write_ym_register(voice, static_cast<std::uint8_t>(0xB4 + channel),
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
                voice,
                static_cast<std::uint8_t>(
                    kOperatorRegisters[register_index] + offset),
                patch_state[payload_index + register_index]);
        }
        // The ROM format has no ninth per-operator byte; the original driver
        // clears the 0x90-series registers while applying a patch.
        write_ym_register(voice, static_cast<std::uint8_t>(0x90 + offset), 0);
    }
}

void Z80AudioBridge::handle_ym_note(std::size_t voice, std::uint8_t note) {
    if (ym_keyed_[voice]) {
        key_off_ym(voice);
    }

    if (has_ym_patch_[voice]) {
        configure_ym_patch(voice, ym_patches_[voice]);
    } else {
        configure_ym_voice(voice);
    }

    const auto [block, fnum] = ym_frequency(note);
    const std::uint8_t channel = ym_channel(voice);
    write_ym_register(voice, static_cast<std::uint8_t>(0xA0 + channel),
                      static_cast<std::uint8_t>(fnum));
    write_ym_register(
        voice,
        static_cast<std::uint8_t>(0xA4 + channel),
        static_cast<std::uint8_t>((block << 3) | (fnum >> 8)));
    if (bus_.write_ym2612) {
        bus_.write_ym2612(0, 0x28);
        bus_.write_ym2612(
            1,
            static_cast<std::uint8_t>(0xF0 | channel
                                       | (voice >= 3 ? 0x04 : 0x00)));
    }
    ym_keyed_[voice] = true;
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

void Z80AudioBridge::key_off_ym(std::size_t voice) {
    if (bus_.write_ym2612) {
        bus_.write_ym2612(0, 0x28);
        bus_.write_ym2612(
            1,
            static_cast<std::uint8_t>(ym_channel(voice)
                                       | (voice >= 3 ? 0x04 : 0x00)));
    }
    ym_keyed_[voice] = false;
}

void Z80AudioBridge::mute_psg(std::size_t voice) {
    if (bus_.write_psg) {
        bus_.write_psg(static_cast<std::uint8_t>(0x90 | (voice << 5) | 0x0F));
    }
}

std::pair<std::uint8_t, std::uint16_t> Z80AudioBridge::ym_frequency(
    std::uint8_t note) {
    // Treat the stream's 0..95 note range as a chromatic scale starting at
    // MIDI C2. The exact table is part of the pending instrument recovery;
    // this formula keeps the bridge stable and musically useful meanwhile.
    const double frequency = 65.40639132514966
        * std::pow(kSemitone, static_cast<double>(note));
    const double scale = frequency * 144.0 * (1u << 20) / kYmClockHz;
    std::uint8_t block = 0;
    double fnum = scale;
    while (fnum > 2047.0 && block < 7) {
        fnum /= 2.0;
        ++block;
    }
    while (fnum < 1.0 && block > 0) {
        fnum *= 2.0;
        --block;
    }
    return {block, static_cast<std::uint16_t>(std::clamp(
        static_cast<int>(std::lround(fnum)), 1, 2047))};
}

std::uint16_t Z80AudioBridge::psg_period(std::uint8_t note) {
    const double frequency = 65.40639132514966
        * std::pow(kSemitone, static_cast<double>(note));
    const double period = kPsgClockHz / (32.0 * frequency);
    return static_cast<std::uint16_t>(std::clamp(
        static_cast<int>(std::lround(period)), 1, 1023));
}

bool Z80AudioBridge::is_ym_patch(
    const Z80SoundDriver::PatchState& patch_state) {
    // Patch-state records selected by the 0x61 handler use the YM record
    // shape: a 0x0A format byte at offset one, followed by B0/B4 at offset
    // three. Other 0x27-byte states are PSG/sample controls.
    return patch_state[1] == 0x0A && patch_state[2] == 0x00;
}

}  // namespace openaladdin::audio
