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

    Z80SoundDriver::PatchState patch{};
    patch[1] = 0x0A;
    patch[3] = 0x34;
    patch[4] = 0xE1;
    patch[5] = 0x24;
    patch[6] = 0x20;
    patch[7] = 0x5F;
    patch[8] = 0x0A;
    patch[9] = 0x00;
    patch[10] = 0xB9;
    patch[11] = 0x51;
    patch[12] = 0x16;
    patch[13] = 0x5F;
    patch[14] = 0x0A;
    patch[15] = 0x02;
    patch[16] = 0x7B;
    patch[17] = 0x21;
    patch[18] = 0x0F;
    patch[19] = 0x5F;
    patch[20] = 0x89;
    patch[21] = 0x02;
    patch[22] = 0xAB;
    patch[23] = 0x50;
    patch[24] = 0x0C;
    patch[25] = 0x5F;
    patch[26] = 0x0B;
    patch[27] = 0x02;
    patch[28] = 0x3F;
    Z80SoundDriver::SoundEvent patch_event{};
    patch_event.kind = Z80SoundDriver::SoundEvent::Kind::Control;
    patch_event.channel = 0;
    patch_event.opcode = 0x61;
    patch_event.has_control_argument = true;
    patch_event.has_patch_state = true;
    patch_event.patch_state = patch;
    bridge.handle(patch_event);
    ym.clear();

    bridge.handle(ym_note);
    const auto has_ym_write = [&ym](std::uint8_t address,
                                     std::uint8_t data) {
        for (std::size_t index = 0; index + 1 < ym.size(); index += 2) {
            if (ym[index].port == 0 && ym[index].data == address
                && ym[index + 1].port == 1 && ym[index + 1].data == data) {
                return true;
            }
        }
        return false;
    };
    assert(has_ym_write(0xB0, 0x34));
    assert(has_ym_write(0xB4, 0xE1));
    assert(has_ym_write(0x30, 0x24));
    assert(has_ym_write(0x38, 0x21));
    assert(has_ym_write(0x34, 0x51));
    assert(has_ym_write(0x3C, 0x50));

    ym.clear();
    const Z80SoundDriver::SoundEvent traced_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        0,
        0x2D,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(traced_note);
    assert(has_ym_write(0xA0, 0xD3));
    assert(has_ym_write(0xA4, 0x1A));

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
