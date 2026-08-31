#include "audio/mixer.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace openaladdin::audio {
namespace {

constexpr std::size_t kRenderBatch = 256;

std::int16_t mix_sample(std::int16_t psg, std::int16_t ym2612) {
    const std::int32_t mixed =
        (static_cast<std::int32_t>(psg) + static_cast<std::int32_t>(ym2612)) / 2;
    return static_cast<std::int16_t>(
        std::clamp(mixed, static_cast<std::int32_t>(-32768), 32767));
}

}  // namespace

Mixer::Mixer(std::uint32_t output_rate)
    : output_rate_(output_rate) {
    if (output_rate_ == 0) {
        throw std::invalid_argument("Mixer output rate must be non-zero");
    }
}

void Mixer::reset() {
    psg_.reset();
    ym2612_.reset();
}

void Mixer::write_psg(std::uint8_t data) {
    psg_.write(data);
}

void Mixer::write_ym2612(std::uint8_t port, std::uint8_t data) {
    ym2612_.write(port, data);
}

void Mixer::play_sample(std::span<const std::uint8_t> samples,
                        std::uint32_t sample_rate) {
    ym2612_.play_sample(samples, sample_rate);
}

void Mixer::stop_sample() {
    ym2612_.stop_sample();
}

void Mixer::render(std::int16_t* interleaved_stereo, std::size_t frames) {
    render(interleaved_stereo, frames, output_rate_);
}

void Mixer::render(std::int16_t* interleaved_stereo,
                   std::size_t frames,
                   std::uint32_t output_rate) {
    if (output_rate == 0) {
        throw std::invalid_argument("Mixer output rate must be non-zero");
    }

    std::array<std::int16_t, kRenderBatch * 2> psg_samples{};
    std::array<std::int16_t, kRenderBatch * 2> ym2612_samples{};
    while (frames != 0) {
        const std::size_t count = std::min(frames, kRenderBatch);
        psg_.render(psg_samples.data(), count, output_rate);
        ym2612_.render(ym2612_samples.data(), count, output_rate);
        for (std::size_t frame = 0; frame < count; ++frame) {
            interleaved_stereo[frame * 2] = mix_sample(
                psg_samples[frame * 2], ym2612_samples[frame * 2]);
            interleaved_stereo[frame * 2 + 1] = mix_sample(
                psg_samples[frame * 2 + 1], ym2612_samples[frame * 2 + 1]);
        }
        interleaved_stereo += count * 2;
        frames -= count;
    }
}

}  // namespace openaladdin::audio
