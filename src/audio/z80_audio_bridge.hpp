#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include "audio/z80_sound_driver.hpp"

namespace openaladdin::audio {

// Minimal hardware-facing bridge for the recovered stream events. The native
// driver annotates events with their selected output path; synthetic events
// without that metadata retain a deterministic channel-number fallback.
class Z80AudioBridge {
public:
    struct Bus {
        std::function<void(std::uint8_t)> write_psg;
        std::function<void(std::uint8_t, std::uint8_t)> write_ym2612;
    };

    explicit Z80AudioBridge(Bus bus);

    void reset();
    // Advance the short-lived YM voice leases used by the original driver.
    // Call once per sound-driver tick before dispatching that tick's events.
    void tick();
    void handle(const Z80SoundDriver::SoundEvent& event);

private:
    static constexpr std::size_t kYmVoiceCount = 6;
    static constexpr std::size_t kYmHardwareChannelCount = 7;
    static constexpr std::size_t kStreamChannelCount =
        Z80SoundDriver::kChannelCount;

    void write_ym_register(std::uint8_t hardware_channel,
                           std::uint8_t address,
                           std::uint8_t data);
    void write_ym_global_register(std::uint8_t address,
                                  std::uint8_t data);
    void configure_ym_voice(std::uint8_t hardware_channel);
    void configure_ym_patch(std::uint8_t hardware_channel,
                            const Z80SoundDriver::PatchState& patch_state);
    void handle_ym_note(std::size_t stream_channel,
                        std::uint8_t note,
                        std::int16_t operand_b);
    void handle_psg_note(std::size_t voice, std::uint8_t note);
    void key_off_ym(std::uint8_t hardware_channel);
    void mute_psg(std::size_t voice);
    std::uint8_t allocate_ym_channel(std::size_t stream_channel);
    void release_ym_channel(std::size_t stream_channel);
    static std::uint16_t ym_voice_lifetime(std::int16_t operand_b) noexcept;

    static std::pair<std::uint8_t, std::uint16_t> ym_frequency(
        std::uint8_t note);
    static std::uint16_t psg_period(std::uint8_t note);
    static bool is_ym_patch(const Z80SoundDriver::PatchState& patch_state);

    Bus bus_;
    std::array<bool, kYmHardwareChannelCount> ym_keyed_{};
    std::array<bool, kYmHardwareChannelCount> ym_channel_in_use_{};
    std::array<std::uint8_t, kYmHardwareChannelCount> ym_stream_for_channel_{};
    std::array<std::uint16_t, kYmHardwareChannelCount> ym_release_timer_{};
    std::array<bool, kYmHardwareChannelCount> ym_voice_has_stream_{};
    std::array<bool, kStreamChannelCount> has_ym_patch_{};
    std::array<Z80SoundDriver::PatchState, kStreamChannelCount> ym_patches_{};
};

}  // namespace openaladdin::audio
