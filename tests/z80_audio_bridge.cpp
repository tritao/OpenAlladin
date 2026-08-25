#include "audio/z80_audio_bridge.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct YmWrite {
    std::uint8_t port;
    std::uint8_t data;
};

}  // namespace

int main() {
    using openaladdin::audio::Z80AudioBridge;
    using openaladdin::audio::Z80SoundDriver;

    std::vector<std::uint8_t> psg;
    std::vector<YmWrite> ym;
    Z80AudioBridge bridge({
        [&psg](std::uint8_t data) { psg.push_back(data); },
        [&ym](std::uint8_t port, std::uint8_t data) {
            ym.push_back({port, data});
        },
    });

    const Z80SoundDriver::SoundEvent ym_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        0,
        24,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(ym_note);
    assert(!ym.empty());
    assert(ym.back().port == 1);
    assert((ym.back().data & 0xF0) == 0xF0);

    const Z80SoundDriver::SoundEvent ym_stop{
        Z80SoundDriver::SoundEvent::Kind::Control,
        0,
        0x60,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(ym_stop);
    assert(ym.size() >= 2);
    assert(ym[ym.size() - 2].port == 0);
    assert(ym[ym.size() - 2].data == 0x28);
    assert(ym.back().port == 1);
    assert(ym.back().data == 0);

    const Z80SoundDriver::SoundEvent psg_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        6,
        36,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(psg_note);
    assert(psg.size() == 3);
    assert((psg[0] & 0x80) != 0);
    assert((psg[0] & 0x60) == 0x40);
    assert(psg[2] == 0xD0);

    const Z80SoundDriver::SoundEvent psg_stop{
        Z80SoundDriver::SoundEvent::Kind::Control,
        6,
        0x60,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(psg_stop);
    assert(psg.back() == 0xDF);

    std::cout << "z80 audio bridge: ok\n";
    return 0;
}
