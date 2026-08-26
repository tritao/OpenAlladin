#include "audio/z80_sound_driver.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using openaladdin::audio::Z80SoundDriver;

struct Fixture {
    std::vector<std::uint8_t> rom;

    Fixture()
        : rom(static_cast<std::size_t>(Z80SoundDriver::kSequenceTableBase)
              + 0x100, 0) {
        constexpr std::size_t base = Z80SoundDriver::kSequenceTableBase;
        constexpr std::size_t header_offset = 0x20;
        constexpr std::size_t stream_offset = 0x40;
        constexpr std::size_t patch_table = base + 0x80;
        constexpr std::size_t patch_data = base + 0xA0;

        // Sound 0 points to a one-track header, whose track points at a
        // stream containing the same operand/control patterns seen in the
        // recovered driver traces.
        rom[base] = static_cast<std::uint8_t>(header_offset);
        rom[base + 1] = 0;
        rom[base + header_offset] = 1;
        rom[base + header_offset + 1] = static_cast<std::uint8_t>(stream_offset);
        rom[base + header_offset + 2] = 0;

        const std::array<std::uint8_t, 10> stream{
            0xC0, 0x6A, 0x4B,  // operand A = 0, control 6A(arg 4B)
            0x6D,              // no-argument control
            0x61, 0x00,        // control 61(arg 00)
            0x98, 0x98, 0x30,  // operand B = -0x618, note 30
            0x60,              // end track
        };
        for (std::size_t i = 0; i < stream.size(); ++i) {
            rom[base + stream_offset + i] = stream[i];
        }

        // A recovered 0x61 patch-state entry: the native decoder copies the
        // 0x27-byte state from table + little-endian offset.
        rom[patch_table] = 0x20;
        rom[patch_table + 1] = 0x00;
        rom[patch_data + 1] = 0x0A;
        rom[patch_data + 3] = 0x34;
        rom[patch_data + 4] = 0xE1;
    }
};

std::array<std::uint8_t, 12> init_args() {
    constexpr std::uint32_t base = Z80SoundDriver::kSequenceTableBase;
    std::array<std::uint8_t, 12> args{};
    constexpr std::uint32_t patch_table = base + 0x80;
    args[0] = static_cast<std::uint8_t>(patch_table);
    args[1] = static_cast<std::uint8_t>(patch_table >> 8);
    args[2] = static_cast<std::uint8_t>(patch_table >> 16);
    args[6] = static_cast<std::uint8_t>(base);
    args[7] = static_cast<std::uint8_t>(base >> 8);
    args[8] = static_cast<std::uint8_t>(base >> 16);
    return args;
}

}  // namespace

int main() {
    Fixture fixture;
    std::vector<Z80SoundDriver::SoundEvent> events;
    Z80SoundDriver driver(fixture.rom, [&events](const auto& event) {
        events.push_back(event);
    });

    const auto setup = init_args();
    driver.command(0x0B, setup);
    const std::array<std::uint8_t, 1> sound_id{0};
    driver.command(0x10, sound_id);
    driver.tick();

    assert(events.size() == 5);
    assert(events[0].opcode == 0x6A);
    assert(events[0].has_control_argument);
    assert(events[0].control_argument == 0x4B);
    assert(events[1].opcode == 0x6D);
    assert(!events[1].has_control_argument);
    assert(events[2].opcode == 0x61);
    assert(events[2].control_argument == 0);
    assert(events[2].has_patch_state);
    assert(events[2].patch_state[1] == 0x0A);
    assert(events[2].patch_state[3] == 0x34);
    assert(events[2].output == Z80SoundDriver::Output::Ym);
    assert(events[3].output == Z80SoundDriver::Output::Ym);
    assert(events[3].kind == Z80SoundDriver::SoundEvent::Kind::Note);
    assert(events[3].opcode == 0x30);
    assert(events[3].operand_b == -0x618);
    assert(events[4].opcode == 0x60);
    assert(!driver.channel(0).active);

    // The mailbox consumes one command per tick, just like the recovered
    // queue consumer, while still allowing the newly started track to run in
    // that same tick.
    driver.reset();
    events.clear();
    driver.enqueue_command(0x0B, setup);
    driver.enqueue_command(0x10, sound_id);
    assert(driver.pending_commands() == 2);
    driver.tick();
    assert(driver.pending_commands() == 1);
    assert(events.empty());
    driver.tick();
    assert(driver.pending_commands() == 0);
    assert(events.size() == 5);

    const std::array<std::uint8_t, 1> stop_id{0};
    driver.command(0x12, stop_id);

    bool rejected_unknown = false;
    try {
        driver.command(0xFF, {});
    } catch (const std::invalid_argument&) {
        rejected_unknown = true;
    }
    assert(rejected_unknown);

    std::cout << "z80 sound driver: ok\n";
    return 0;
}
