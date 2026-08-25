#include "audio/mixer.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using openaladdin::audio::Mixer;

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

}  // namespace

int main() {
    bool rejected_zero_rate = false;
    try {
        Mixer invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero_rate = true;
    }
    assert(rejected_zero_rate);

    Mixer mixer;
    assert(mixer.output_rate() == Mixer::kDefaultOutputRate);

    // Enable a loud zero-period PSG tone and the YM2612 DAC simultaneously.
    mixer.write_psg(0x90);
    mixer.write_psg(0x80);
    mixer.write_ym2612(0, 0x2b);
    mixer.write_ym2612(1, 0x80);
    mixer.write_ym2612(0, 0x2a);
    mixer.write_ym2612(1, 0xff);

    std::array<std::int16_t, 256 * 2> mixed{};
    mixer.render(mixed.data(), mixed.size() / 2);
    assert_stereo(mixed);
    assert(has_signal(mixed));

    // The mixer must accept the rate negotiated by a future SDL device.
    std::array<std::int16_t, 64> negotiated{};
    mixer.render(negotiated.data(), negotiated.size() / 2, 44'100);
    assert_stereo(negotiated);
    assert(has_signal(negotiated));

    mixer.reset();
    std::array<std::int16_t, 32> reset_output{};
    mixer.render(reset_output.data(), reset_output.size() / 2);
    assert_stereo(reset_output);

    bool rejected_zero_render_rate = false;
    try {
        mixer.render(reset_output.data(), 1, 0);
    } catch (const std::invalid_argument&) {
        rejected_zero_render_rate = true;
    }
    assert(rejected_zero_render_rate);

    std::cout << "mixer: ok\n";
    return 0;
}
