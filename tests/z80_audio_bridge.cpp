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
    assert(ym[0].port == 0);
    assert(ym[0].data == 0x22);
    assert(ym[1].port == 1);
    assert(ym[1].data == 0x0A);
    assert(has_ym_write(0xB0, 0x34));
    assert(has_ym_write(0xB4, 0xE1));
    assert(has_ym_write(0x30, 0x24));
    assert(has_ym_write(0x38, 0x51));
    assert(has_ym_write(0x34, 0x21));
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
    assert(has_ym_write(0xA1, 0x3B));
    assert(has_ym_write(0xA5, 0x1C));
    assert(ym[ym.size() - 6].port == 0);
    assert(ym[ym.size() - 6].data == 0xA5);
    assert(ym[ym.size() - 4].port == 0);
    assert(ym[ym.size() - 4].data == 0xA1);

    bridge.reset();
    ym.clear();
    psg.clear();
    const Z80SoundDriver::SoundEvent second_stream_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        2,
        0x2D,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(ym_note);
    bridge.handle(second_stream_note);
    assert(ym.back().port == 1);
    assert(ym.back().data == 0xF1);

    bridge.reset();
    ym.clear();
    psg.clear();
    auto leased_note = ym_note;
    leased_note.operand_b = -2;
    bridge.handle(leased_note);
    ym.clear();
    auto overlapping_note = second_stream_note;
    overlapping_note.operand_b = 0;
    bridge.handle(overlapping_note);
    assert(ym.back().port == 1);
    assert(ym.back().data == 0xF1);
    ym.clear();
    bridge.tick();
    assert(ym.empty());
    bridge.tick();
    assert(ym.size() >= 2);
    assert(ym[ym.size() - 2].port == 0);
    assert(ym[ym.size() - 2].data == 0x28);
    assert(ym.back().port == 1);
    assert(ym.back().data == 0x00);

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
    assert(psg[0] == 0xC7);  // ROM $1182[$24 - $21] = $0357
    assert(psg[1] == 0x35);
    assert(psg[2] == 0xD0);

    // Stream metadata takes precedence over the legacy channel-number
    // fallback: a PSG track may occupy a native channel normally used for YM.
    psg.clear();
    Z80SoundDriver::SoundEvent classified_psg_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        0,
        36,
        0,
        0,
        0,
        false,
        0,
    };
    classified_psg_note.output = Z80SoundDriver::Output::Psg;
    bridge.handle(classified_psg_note);
    assert(psg.size() == 3);
    assert(psg[0] == 0x87);

    psg.clear();
    const Z80SoundDriver::SoundEvent psg_low_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        6,
        0x21,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(psg_low_note);
    assert(psg[0] == 0xC9);
    assert(psg[1] == 0x3F);

    psg.clear();
    const Z80SoundDriver::SoundEvent psg_high_note{
        Z80SoundDriver::SoundEvent::Kind::Note,
        6,
        0x60,
        0,
        0,
        0,
        false,
        0,
    };
    bridge.handle(psg_high_note);
    assert(psg[0] == 0xCC);
    assert(psg[1] == 0x01);

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

    // The real animation SFX (ROM sound 0x4C) is a type-3 PSG noise track,
    // not a pitched tone on the stream-numbered voice. Its patch is [03,07,
    // 1F,...], and the recovered driver writes the tone-2 clock, noise setup,
    // and the short volume attack below.
    psg.clear();
    Z80SoundDriver::SoundEvent psg_patch{};
    psg_patch.kind = Z80SoundDriver::SoundEvent::Kind::Control;
    psg_patch.channel = 0;
    psg_patch.opcode = 0x61;
    psg_patch.has_control_argument = true;
    psg_patch.control_argument = 0x69;
    psg_patch.has_patch_state = true;
    psg_patch.patch_state[0] = 0x03;
    psg_patch.patch_state[1] = 0x07;
    psg_patch.patch_state[2] = 0x1F;
    psg_patch.output = Z80SoundDriver::Output::Psg;
    bridge.handle(psg_patch);

    Z80SoundDriver::SoundEvent noise_note{};
    noise_note.kind = Z80SoundDriver::SoundEvent::Kind::Note;
    noise_note.channel = 0;
    noise_note.opcode = 0x44;
    noise_note.output = Z80SoundDriver::Output::Psg;
    bridge.handle(noise_note);
    assert((psg == std::vector<std::uint8_t>{
        0xC7, 0x08, 0xE7, 0xDF, 0xFE}));

    for (const std::uint8_t volume : {
             static_cast<std::uint8_t>(0xFC),
             static_cast<std::uint8_t>(0xFA),
             static_cast<std::uint8_t>(0xF8),
             static_cast<std::uint8_t>(0xF6),
             static_cast<std::uint8_t>(0xF4),
             static_cast<std::uint8_t>(0xF2),
             static_cast<std::uint8_t>(0xF0),
             static_cast<std::uint8_t>(0xF0),
             static_cast<std::uint8_t>(0xF0)}) {
        bridge.tick();
        assert(psg.back() == volume);
    }

    psg.clear();
    Z80SoundDriver::SoundEvent noise_stop{};
    noise_stop.kind = Z80SoundDriver::SoundEvent::Kind::Control;
    noise_stop.channel = 0;
    noise_stop.opcode = 0x60;
    noise_stop.output = Z80SoundDriver::Output::Psg;
    bridge.handle(noise_stop);
    assert((psg == std::vector<std::uint8_t>{0xDF, 0xFF}));

    std::cout << "z80 audio bridge: ok\n";
    return 0;
}
