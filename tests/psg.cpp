#include "audio/sn76489.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using openaladdin::audio::SegaPsg;

template <std::size_t Size>
void assert_stereo(const std::array<std::int16_t, Size>& samples) {
    for (std::size_t i = 0; i < samples.size() / 2; ++i) {
        assert(samples[i * 2] == samples[i * 2 + 1]);
    }
}

}  // namespace

int main() {
    bool rejected_zero_clock = false;
    try {
        SegaPsg invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero_clock = true;
    }
    assert(rejected_zero_clock);

    SegaPsg psg;
    assert(psg.native_sample_rate() == SegaPsg::kDefaultClockHz / 2);

    // A zero Sega tone period is a real zero period, so this channel toggles
    // on every divided PSG clock. MAME's stream begins with seven settling
    // samples before the first divided clock.
    psg.write(0x90);  // tone 0, maximum volume
    psg.write(0x80);  // tone 0, period low nibble = 0
    std::array<std::int16_t, 32> tone_samples{};
    psg.render_native(tone_samples.data(), tone_samples.size() / 2);
    assert_stereo(tone_samples);
    for (std::size_t i = 0; i < 7; ++i) {
        assert(tone_samples[i * 2] == 0);
    }
    for (std::size_t i = 7; i < 15; ++i) {
        assert(tone_samples[i * 2] < 0);
    }
    assert(tone_samples[15 * 2] == 0);

    // Data bytes use the previously latched tone register and carry six
    // frequency bits. A period of 0x20 must still be high at divided tick 16.
    psg.reset();
    psg.write(0x90);
    psg.write(0x80);
    psg.write(0x02);  // period high bits: 0x02 << 4 = 0x20
    std::array<std::int16_t, 528> latched_samples{};
    psg.render_native(latched_samples.data(), latched_samples.size() / 2);
    assert(latched_samples[7 * 2] < 0);
    assert(latched_samples[127 * 2] < 0);
    assert(latched_samples[255 * 2] < 0);
    assert(latched_samples[263 * 2] == 0);

    // Noise uses the Sega PSG's 16-bit LFSR and eventually produces a high
    // output bit after the reset seed has shifted through the register.
    psg.reset();
    psg.write(0xf0);  // noise, maximum volume
    psg.write(0xe0);  // periodic noise, shortest fixed period
    std::array<std::int16_t, 8192> noise_samples{};
    psg.render_native(noise_samples.data(), noise_samples.size() / 2);
    bool noise_active = false;
    for (std::size_t i = 0; i < noise_samples.size(); i += 2) {
        assert(noise_samples[i] == noise_samples[i + 1]);
        noise_active |= noise_samples[i] < 0;
    }
    assert(noise_active);

    // The arbitrary-rate path advances the same waveform state and emits
    // mono PSG output in interleaved stereo form.
    psg.reset();
    psg.write(0x90);
    psg.write(0x80);
    std::array<std::int16_t, 128> resampled{};
    psg.render(resampled.data(), resampled.size() / 2, 48'000);
    assert_stereo(resampled);
    bool resampled_active = false;
    for (std::size_t i = 0; i < resampled.size(); i += 2) {
        resampled_active |= resampled[i] < 0;
    }
    assert(resampled_active);

    bool rejected_zero_rate = false;
    try {
        psg.render(resampled.data(), 1, 0);
    } catch (const std::invalid_argument&) {
        rejected_zero_rate = true;
    }
    assert(rejected_zero_rate);

    std::cout << "psg: ok\n";
    return 0;
}
