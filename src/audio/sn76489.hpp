#pragma once

#include <cstddef>
#include <cstdint>

namespace openaladdin::audio {

// Standalone Sega PSG (the SN76489-compatible PSG in the Mega Drive/Genesis).
// The native render path matches MAME's segapsg device sample timing.
class SegaPsg {
public:
    static constexpr std::uint32_t kDefaultClockHz = 3'579'545;

    explicit SegaPsg(std::uint32_t clock_hz = kDefaultClockHz);

    void reset();
    void write(std::uint8_t data);

    // Render at the chip's native stream rate (clock_hz / 2). Samples are
    // duplicated into interleaved left/right output because the Genesis PSG
    // itself is mono.
    void render_native(std::int16_t* interleaved_stereo, std::size_t frames);

    // Render directly at an arbitrary output rate. This is intended for the
    // eventual SDL mixer path; callers should use one render path consistently
    // for a given instance.
    void render(std::int16_t* interleaved_stereo,
                std::size_t frames,
                std::uint32_t output_rate);

    [[nodiscard]] std::uint32_t native_sample_rate() const noexcept {
        return clock_hz_ / 2;
    }

private:
    static constexpr std::uint32_t kClockDivider = 8;
    static constexpr std::uint32_t kFeedbackMask = 0x8000;
    static constexpr std::uint32_t kNoiseTap1 = 0x01;
    static constexpr std::uint32_t kNoiseTap2 = 0x08;

    void clock_waveform();
    [[nodiscard]] std::int16_t mixed_sample() const noexcept;
    void write_register(std::uint8_t data, int reg);

    std::uint32_t clock_hz_;
    std::uint16_t registers_[8]{};
    int last_register_ = 3;
    std::int32_t volume_[4]{};
    std::int32_t volume_table_[16]{};
    std::uint32_t rng_ = kFeedbackMask;
    std::uint32_t native_clock_ = kClockDivider - 1;
    std::uint32_t period_[4]{};
    std::int32_t count_[4]{};
    std::uint8_t output_[4]{};

    // Fractional waveform-clock accumulator used by render(). It is kept in
    // chip-clock units, so changing output rates does not lose phase.
    std::uint64_t render_phase_ = 0;
};

}  // namespace openaladdin::audio
