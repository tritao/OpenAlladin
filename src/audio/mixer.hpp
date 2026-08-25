#pragma once

#include <cstddef>
#include <cstdint>

#include "audio/sn76489.hpp"
#include "audio/ym2612.hpp"

namespace openaladdin::audio {

// Small hardware mixer used by the eventual SDL audio callback. The chip
// cores remain independently clocked; this class only rate-converts them to
// the callback rate and applies the Genesis-era half-gain when summing FM and
// PSG output.
class Mixer {
public:
    static constexpr std::uint32_t kDefaultOutputRate = 48'000;

    explicit Mixer(std::uint32_t output_rate = kDefaultOutputRate);

    void reset();
    void write_psg(std::uint8_t data);
    void write_ym2612(std::uint8_t port, std::uint8_t data);

    // Render interleaved signed 16-bit stereo at the configured callback
    // rate.
    void render(std::int16_t* interleaved_stereo, std::size_t frames);

    // Render at a rate supplied by an audio device that negotiated a different
    // frequency than requested.
    void render(std::int16_t* interleaved_stereo,
                std::size_t frames,
                std::uint32_t output_rate);

    [[nodiscard]] std::uint32_t output_rate() const noexcept {
        return output_rate_;
    }

private:
    std::uint32_t output_rate_;
    SegaPsg psg_;
    Ym2612 ym2612_;
};

}  // namespace openaladdin::audio
