#include "audio/ym2612.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using openaladdin::audio::Ym2612;

template <std::size_t Size>
bool has_signal(const std::array<std::int16_t, Size>& samples) {
    for (const std::int16_t sample : samples) {
        if (sample != 0) {
            return true;
        }
    }
    return false;
}

template <std::size_t Size>
void assert_stereo(const std::array<std::int16_t, Size>& samples) {
    for (std::size_t i = 0; i < samples.size() / 2; ++i) {
        assert(samples[i * 2] == samples[i * 2 + 1]);
    }
}

void enable_dac(Ym2612& chip) {
    chip.write(0, 0x2b);
    chip.write(1, 0x80);
    chip.write(0, 0x2a);
    chip.write(1, 0xff);
}

}  // namespace

int main() {
    Ym2612 chip;
    assert(chip.native_sample_rate() > 0);

    std::array<std::int16_t, 32> baseline{};
    chip.render_native(baseline.data(), baseline.size() / 2);

    // The DAC path gives a compact deterministic smoke test for the YM2612
    // port mapping without depending on a long FM envelope warm-up.
    enable_dac(chip);
    std::array<std::int16_t, 64> native{};
    chip.render_native(native.data(), native.size() / 2);
    assert_stereo(native);
    assert(has_signal(native));
    bool dac_changed_output = false;
    for (std::size_t i = 0; i < native.size(); ++i) {
        dac_changed_output |= native[i] != baseline[i % baseline.size()];
    }
    assert(dac_changed_output);

    chip.reset();
    std::array<std::int16_t, 32> reset_output{};
    chip.render_native(reset_output.data(), reset_output.size() / 2);
    assert(reset_output == baseline);

    enable_dac(chip);
    std::array<std::int16_t, 128> resampled{};
    chip.render(resampled.data(), resampled.size() / 2, 48'000);
    assert_stereo(resampled);
    assert(has_signal(resampled));

    const std::array<std::uint8_t, 4> sample{
        0x80, 0xFF, 0x40, 0x80};
    chip.reset();
    chip.play_sample(sample);
    std::array<std::int16_t, 256> sample_output{};
    chip.render(sample_output.data(), sample_output.size() / 2, 48'000);
    assert(has_signal(sample_output));
    chip.stop_sample();

    bool rejected_bad_port = false;
    try {
        chip.write(4, 0);
    } catch (const std::out_of_range&) {
        rejected_bad_port = true;
    }
    assert(rejected_bad_port);

    bool rejected_zero_rate = false;
    try {
        chip.render(resampled.data(), 1, 0);
    } catch (const std::invalid_argument&) {
        rejected_zero_rate = true;
    }
    assert(rejected_zero_rate);

    std::cout << "ym2612: ok\n";
    return 0;
}
