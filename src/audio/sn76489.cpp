// Standalone adaptation of the Sega configuration in MAME's sn76496 device.
// MAME's original implementation is BSD-3-Clause licensed.

#include "audio/sn76489.hpp"

#include <algorithm>
#include <stdexcept>

namespace openaladdin::audio {
namespace {

constexpr std::int32_t kMaxOutput = 0x7fff;

}  // namespace

SegaPsg::SegaPsg(std::uint32_t clock_hz) : clock_hz_(clock_hz) {
    if (clock_hz_ == 0) {
        throw std::invalid_argument("SegaPsg clock must be non-zero");
    }
    reset();
}

void SegaPsg::reset() {
    last_register_ = 3;
    for (int i = 0; i < 8; ++i) {
        registers_[i] = 0;
    }
    for (int i = 0; i < 4; ++i) {
        registers_[i * 2 + 1] = 0x0f;
        volume_[i] = 0;
        period_[i] = 0;
        count_[i] = 0;
        output_[i] = 0;
    }

    rng_ = kFeedbackMask;
    output_[3] = static_cast<std::uint8_t>(rng_ & 1U);
    native_clock_ = kClockDivider - 1;
    render_phase_ = 0;

    double output = static_cast<double>(kMaxOutput / 4);
    for (int i = 0; i < 15; ++i) {
        volume_table_[i] = std::min(
            static_cast<std::int32_t>(output), kMaxOutput / 4);
        output /= 1.258925412;  // 2 dB per volume step.
    }
    volume_table_[15] = 0;
    for (int i = 0; i < 4; ++i) {
        volume_[i] = volume_table_[registers_[i * 2 + 1]];
    }
}

void SegaPsg::write(std::uint8_t data) {
    int reg;
    if ((data & 0x80U) != 0) {
        reg = (data & 0x70U) >> 4;
        last_register_ = reg;
        registers_[reg] = static_cast<std::uint16_t>(
            (registers_[reg] & 0x3f0U) | (data & 0x0fU));
    } else {
        reg = last_register_;
    }

    write_register(data, reg);
}

void SegaPsg::write_register(std::uint8_t data, int reg) {
    const int channel = reg >> 1;
    switch (reg) {
    case 0:
    case 2:
    case 4:
        if ((data & 0x80U) == 0) {
            registers_[reg] = static_cast<std::uint16_t>(
                (registers_[reg] & 0x0fU) | ((data & 0x3fU) << 4));
        }
        period_[channel] = registers_[reg];
        if (reg == 4 && (registers_[6] & 0x03U) == 0x03U) {
            period_[3] = period_[2] << 1;
        }
        break;

    case 1:
    case 3:
    case 5:
    case 7:
        volume_[channel] = volume_table_[data & 0x0fU];
        if ((data & 0x80U) == 0) {
            registers_[reg] = static_cast<std::uint16_t>(
                (registers_[reg] & 0x3f0U) | (data & 0x0fU));
        }
        break;

    case 6: {
        if ((data & 0x80U) == 0) {
            registers_[reg] = static_cast<std::uint16_t>(
                (registers_[reg] & 0x3f0U) | (data & 0x0fU));
        }
        const std::uint16_t noise = registers_[6];
        period_[3] = ((noise & 0x03U) == 0x03U)
            ? period_[2] << 1
            : (1U << (5U + (noise & 0x03U)));
        rng_ = kFeedbackMask;
        break;
    }

    default:
        break;
    }
}

void SegaPsg::clock_waveform() {
    for (int i = 0; i < 3; ++i) {
        --count_[i];
        if (count_[i] <= 0) {
            output_[i] ^= 1U;
            count_[i] = static_cast<std::int32_t>(period_[i]);
        }
    }

    --count_[3];
    if (count_[3] <= 0) {
        // In the Sega PSG, bit 2 selects whether the second noise tap is
        // active. This is the same XOR feedback rule used by MAME's segapsg.
        const bool feedback = ((rng_ & kNoiseTap1) != 0)
            != (((rng_ & kNoiseTap2) != 0) && ((registers_[6] & 0x04U) != 0));
        rng_ >>= 1;
        if (feedback) {
            rng_ |= kFeedbackMask;
        }
        output_[3] = static_cast<std::uint8_t>(rng_ & 1U);
        count_[3] = static_cast<std::int32_t>(period_[3]);
    }
}

std::int16_t SegaPsg::mixed_sample() const noexcept {
    std::int32_t output = 0;
    for (int i = 0; i < 4; ++i) {
        if (output_[i] != 0) {
            output += volume_[i];
        }
    }
    output = -output;  // Sega PSG output is inverted in MAME's model.
    output = std::clamp(output,
                        static_cast<std::int32_t>(-kMaxOutput), kMaxOutput);
    return static_cast<std::int16_t>(output);
}

void SegaPsg::render_native(std::int16_t* interleaved_stereo,
                            std::size_t frames) {
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (native_clock_ > 0) {
            --native_clock_;
        } else {
            native_clock_ = kClockDivider - 1;
            clock_waveform();
        }

        const std::int16_t sample = mixed_sample();
        interleaved_stereo[frame * 2] = sample;
        interleaved_stereo[frame * 2 + 1] = sample;
    }
}

void SegaPsg::render(std::int16_t* interleaved_stereo,
                     std::size_t frames,
                     std::uint32_t output_rate) {
    if (output_rate == 0) {
        throw std::invalid_argument("SegaPsg output rate must be non-zero");
    }

    const std::uint64_t waveform_period =
        2ULL * kClockDivider * output_rate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        render_phase_ += clock_hz_;
        while (render_phase_ >= waveform_period) {
            render_phase_ -= waveform_period;
            clock_waveform();
        }

        const std::int16_t sample = mixed_sample();
        interleaved_stereo[frame * 2] = sample;
        interleaved_stereo[frame * 2 + 1] = sample;
    }
}

}  // namespace openaladdin::audio
