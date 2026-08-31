#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace openaladdin::audio {

// Standalone YM2612 (OPN2) wrapper for the Mega Drive/Genesis FM chip.
// Register ports follow the hardware layout: 0/1 are the low address/data
// pair and 2/3 are the high address/data pair.
class Ym2612 {
public:
    static constexpr std::uint32_t kDefaultClockHz = 7'670'454;
    // The recovered Z80 sample service transfers about 176 DAC bytes per
    // NTSC video frame (176 * 60 Hz). Samples in the ROM are unsigned 8-bit
    // YM2612 DAC values and are therefore played at this rate.
    static constexpr std::uint32_t kDacSampleRate = 10'560;

    explicit Ym2612(std::uint32_t clock_hz = kDefaultClockHz);
    ~Ym2612();

    Ym2612(const Ym2612&) = delete;
    Ym2612& operator=(const Ym2612&) = delete;

    void reset();
    void write(std::uint8_t port, std::uint8_t data);
    void play_sample(std::span<const std::uint8_t> samples,
                     std::uint32_t sample_rate = kDacSampleRate);
    void stop_sample();
    [[nodiscard]] std::uint8_t status();

    // Render at ymfm's native chip sample rate. Output is interleaved stereo
    // and is clamped to signed 16-bit samples.
    void render_native(std::int16_t* interleaved_stereo, std::size_t frames);

    // Render at an arbitrary output rate using a fractional source clock and
    // held native samples. This keeps the chip clock independent of SDL's
    // chosen device rate; a higher quality mixer can replace this later.
    void render(std::int16_t* interleaved_stereo,
                std::size_t frames,
                std::uint32_t output_rate);

    [[nodiscard]] std::uint32_t native_sample_rate() const noexcept {
        return native_sample_rate_;
    }

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
    std::uint32_t native_sample_rate_ = 0;
};

}  // namespace openaladdin::audio
