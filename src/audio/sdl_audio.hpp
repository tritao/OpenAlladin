#pragma once

#include <cstdint>

#include <SDL.h>

#include "audio/mixer.hpp"

namespace openaladdin::audio {

// Owns an SDL audio device and feeds its callback from Mixer. Register writes
// are serialized with the callback so the future sound-driver emulation can
// submit hardware writes from the game thread safely.
class SdlAudioOutput {
public:
    explicit SdlAudioOutput(Mixer& mixer) noexcept : mixer_(&mixer) {}
    ~SdlAudioOutput();

    SdlAudioOutput(const SdlAudioOutput&) = delete;
    SdlAudioOutput& operator=(const SdlAudioOutput&) = delete;

    void open(std::uint32_t sample_rate = Mixer::kDefaultOutputRate,
              std::uint16_t buffer_frames = 512);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept {
        return device_ != 0;
    }

    [[nodiscard]] std::uint32_t sample_rate() const noexcept {
        return static_cast<std::uint32_t>(obtained_.freq);
    }

    void reset();
    void write_psg(std::uint8_t data);
    void write_ym2612(std::uint8_t port, std::uint8_t data);

private:
    static void callback(void* userdata, Uint8* stream, int length);

    Mixer* mixer_;
    SDL_AudioDeviceID device_ = 0;
    SDL_AudioSpec obtained_{};
};

}  // namespace openaladdin::audio
