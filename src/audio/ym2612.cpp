#include "audio/ym2612.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "ymfm_opn.h"

namespace openaladdin::audio {
namespace {

constexpr std::size_t kGenerationBatch = 256;

std::int16_t clamp_sample(std::int32_t sample) {
    return static_cast<std::int16_t>(std::clamp(sample, -32768, 32767));
}

}  // namespace

class Ym2612::Impl final : public ymfm::ymfm_interface {
public:
    explicit Impl(std::uint32_t clock_hz)
        : chip_(*this), native_sample_rate_(chip_.sample_rate(clock_hz)) {
        if (clock_hz == 0 || native_sample_rate_ == 0) {
            throw std::invalid_argument("Ym2612 clock must be non-zero");
        }
        reset();
    }

    void reset() {
        chip_.reset();

        // ymfm's YM2612 reset resets the FM engine. Clear the DAC registers as
        // well so this wrapper's reset has the expected silent-chip semantics.
        chip_.write(0, 0x2b);
        chip_.write(1, 0x00);
        chip_.write(0, 0x2a);
        chip_.write(1, 0x80);

        source_phase_ = 0;
        last_left_ = 0;
        last_right_ = 0;
    }

    void write(std::uint8_t port, std::uint8_t data) {
        chip_.write(port, data);
    }

    [[nodiscard]] std::uint8_t status() {
        return chip_.read_status();
    }

    [[nodiscard]] std::uint32_t native_sample_rate() const noexcept {
        return native_sample_rate_;
    }

    void render_native(std::int16_t* output, std::size_t frames) {
        std::array<ymfm::ym2612::output_data, kGenerationBatch> generated{};
        while (frames != 0) {
            const std::size_t count = std::min(frames, kGenerationBatch);
            chip_.generate(generated.data(), static_cast<std::uint32_t>(count));
            for (std::size_t frame = 0; frame < count; ++frame) {
                last_left_ = clamp_sample(generated[frame].data[0]);
                last_right_ = clamp_sample(generated[frame].data[1]);
                output[frame * 2] = last_left_;
                output[frame * 2 + 1] = last_right_;
            }
            output += count * 2;
            frames -= count;
        }
    }

    void render(std::int16_t* output,
                std::size_t frames,
                std::uint32_t output_rate) {
        if (output_rate == 0) {
            throw std::invalid_argument("Ym2612 output rate must be non-zero");
        }

        std::array<ymfm::ym2612::output_data, kGenerationBatch> generated{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            source_phase_ += native_sample_rate_;
            const std::size_t source_frames = source_phase_ / output_rate;
            source_phase_ %= output_rate;
            if (source_frames != 0) {
                advance(source_frames, generated);
            }
            output[frame * 2] = last_left_;
            output[frame * 2 + 1] = last_right_;
        }
    }

private:
    using GeneratedBuffer =
        std::array<ymfm::ym2612::output_data, kGenerationBatch>;

    void advance(std::size_t frames, GeneratedBuffer& generated) {
        while (frames != 0) {
            const std::size_t count = std::min(frames, kGenerationBatch);
            chip_.generate(generated.data(), static_cast<std::uint32_t>(count));
            last_left_ = clamp_sample(generated[count - 1].data[0]);
            last_right_ = clamp_sample(generated[count - 1].data[1]);
            frames -= count;
        }
    }

    ymfm::ym2612 chip_;
    std::uint32_t native_sample_rate_;
    std::uint64_t source_phase_ = 0;
    std::int16_t last_left_ = 0;
    std::int16_t last_right_ = 0;
};

Ym2612::Ym2612(std::uint32_t clock_hz)
    : impl_(std::make_unique<Impl>(clock_hz)),
      native_sample_rate_(impl_->native_sample_rate()) {
}

Ym2612::~Ym2612() = default;

void Ym2612::reset() {
    impl_->reset();
}

void Ym2612::write(std::uint8_t port, std::uint8_t data) {
    if (port > 3) {
        throw std::out_of_range("Ym2612 port must be in the range 0..3");
    }
    impl_->write(port, data);
}

std::uint8_t Ym2612::status() {
    return impl_->status();
}

void Ym2612::render_native(std::int16_t* interleaved_stereo,
                           std::size_t frames) {
    impl_->render_native(interleaved_stereo, frames);
}

void Ym2612::render(std::int16_t* interleaved_stereo,
                    std::size_t frames,
                    std::uint32_t output_rate) {
    impl_->render(interleaved_stereo, frames, output_rate);
}

}  // namespace openaladdin::audio
