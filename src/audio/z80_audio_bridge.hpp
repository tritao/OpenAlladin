#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include "audio/z80_sound_driver.hpp"

namespace openaladdin::audio {

// Minimal hardware-facing bridge for the recovered stream events. FM tracks
// use six YM2612 voices; additional tracks use the four-channel Genesis PSG.
// ROM patch states are supplied by the recovered Z80 stream driver. Synthetic
// events without a patch state retain a small deterministic fallback voice so
// the bridge remains useful in isolation.
class Z80AudioBridge {
public:
    struct Bus {
        std::function<void(std::uint8_t)> write_psg;
        std::function<void(std::uint8_t, std::uint8_t)> write_ym2612;
    };

    explicit Z80AudioBridge(Bus bus);

    void reset();
    void handle(const Z80SoundDriver::SoundEvent& event);

private:
    static constexpr std::size_t kYmVoiceCount = 6;
    static constexpr std::size_t kYmHardwareChannelCount = 7;

    void write_ym_register(std::uint8_t hardware_channel,
                           std::uint8_t address,
                           std::uint8_t data);
    void configure_ym_voice(std::uint8_t hardware_channel);
    void configure_ym_patch(std::uint8_t hardware_channel,
                            const Z80SoundDriver::PatchState& patch_state);
    void handle_ym_note(std::size_t stream_channel, std::uint8_t note);
    void handle_psg_note(std::size_t voice, std::uint8_t note);
    void key_off_ym(std::uint8_t hardware_channel);
    void mute_psg(std::size_t voice);
    std::uint8_t allocate_ym_channel(std::size_t stream_channel);
    void release_ym_channel(std::size_t stream_channel);

    static std::pair<std::uint8_t, std::uint16_t> ym_frequency(
        std::uint8_t note);
    static std::uint16_t psg_period(std::uint8_t note);
    static bool is_ym_patch(const Z80SoundDriver::PatchState& patch_state);

    Bus bus_;
    std::array<bool, kYmHardwareChannelCount> ym_keyed_{};
    std::array<bool, kYmHardwareChannelCount> ym_channel_in_use_{};
    std::array<std::uint8_t, kYmVoiceCount> ym_channel_for_stream_{};
    std::array<bool, kYmVoiceCount> has_ym_channel_for_stream_{};
    std::array<bool, kYmVoiceCount> has_ym_patch_{};
    std::array<Z80SoundDriver::PatchState, kYmVoiceCount> ym_patches_{};
};

}  // namespace openaladdin::audio
