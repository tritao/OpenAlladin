#include "audio/sdl_audio.hpp"

#include <stdexcept>
#include <string>

namespace openaladdin::audio {

SdlAudioOutput::~SdlAudioOutput() {
    close();
}

void SdlAudioOutput::open(std::uint32_t sample_rate,
                          std::uint16_t buffer_frames) {
    if (sample_rate == 0 || buffer_frames == 0) {
        throw std::invalid_argument("SDL audio format must have non-zero rate and buffer");
    }
    if (device_ != 0) {
        close();
    }

    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(sample_rate);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = buffer_frames;
    desired.callback = &SdlAudioOutput::callback;
    desired.userdata = this;

    device_ = SDL_OpenAudioDevice(
        nullptr,
        0,
        &desired,
        &obtained_,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE
    );
    if (device_ == 0) {
        throw std::runtime_error(
            std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError());
    }
    if (obtained_.format != AUDIO_S16SYS || obtained_.channels != 2
        || obtained_.freq <= 0) {
        close();
        throw std::runtime_error("SDL audio device did not accept S16 stereo");
    }

    SDL_PauseAudioDevice(device_, 0);
}

void SdlAudioOutput::close() noexcept {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    obtained_ = SDL_AudioSpec{};
}

void SdlAudioOutput::reset() {
    if (device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    mixer_->reset();
    if (device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
}

void SdlAudioOutput::write_psg(std::uint8_t data) {
    if (device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    mixer_->write_psg(data);
    if (device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
}

void SdlAudioOutput::write_ym2612(std::uint8_t port, std::uint8_t data) {
    if (device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    mixer_->write_ym2612(port, data);
    if (device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
}

void SdlAudioOutput::play_sample(std::span<const std::uint8_t> samples,
                                 std::uint32_t sample_rate) {
    if (device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    mixer_->play_sample(samples, sample_rate);
    if (device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
}

void SdlAudioOutput::stop_sample() {
    if (device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    mixer_->stop_sample();
    if (device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
}

void SdlAudioOutput::callback(void* userdata, Uint8* stream, int length) {
    auto* output = static_cast<SdlAudioOutput*>(userdata);
    if (output == nullptr || length <= 0) {
        return;
    }

    constexpr int kBytesPerFrame = 2 * static_cast<int>(sizeof(std::int16_t));
    const int frames = length / kBytesPerFrame;
    output->mixer_->render(
        reinterpret_cast<std::int16_t*>(stream),
        static_cast<std::size_t>(frames),
        static_cast<std::uint32_t>(output->obtained_.freq)
    );
    const int written = frames * kBytesPerFrame;
    for (int i = written; i < length; ++i) {
        stream[i] = 0;
    }
}

}  // namespace openaladdin::audio
