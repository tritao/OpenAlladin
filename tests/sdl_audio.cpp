#include "audio/sdl_audio.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    int result = 0;
    try {
        openaladdin::audio::Mixer mixer;
        openaladdin::audio::SdlAudioOutput output(mixer);
        output.open(48'000, 128);
        assert(output.is_open());
        assert(output.sample_rate() > 0);

        output.write_psg(0x90);
        output.write_psg(0x80);
        output.write_ym2612(0, 0x2b);
        output.write_ym2612(1, 0x80);
        output.write_ym2612(0, 0x2a);
        output.write_ym2612(1, 0xff);
        output.reset();
        output.close();
        assert(!output.is_open());
        std::cout << "sdl_audio: ok\n";
    } catch (...) {
        SDL_Quit();
        throw;
    }

    SDL_Quit();
    return result;
}
