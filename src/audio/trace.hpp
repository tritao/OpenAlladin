#pragma once

#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

#include "audio/z80_sound_driver.hpp"

namespace openaladdin::audio {

// Deterministic native-side trace for comparing the recovered sound path with
// MAME. It records protocol/events and chip-bus writes, never host audio
// samples, so the result is independent of SDL's output device.
class AudioTrace {
public:
    explicit AudioTrace(const std::string& path);
    ~AudioTrace();

    AudioTrace(const AudioTrace&) = delete;
    AudioTrace& operator=(const AudioTrace&) = delete;

    // The native scheduler currently advances the sound driver once per game
    // frame. Keep both names in the trace so a future cycle-accurate driver
    // can change the tick domain without changing the record shape.
    void begin_frame(std::uint64_t frame);

    void record_command(std::uint8_t opcode,
                        std::span<const std::uint8_t> args,
                        std::string_view kind,
                        std::string_view phase,
                        int sound_id = -1);
    void record_event(const Z80SoundDriver::SoundEvent& event);
    void record_psg_write(std::uint8_t data);
    void record_ym_write(std::uint8_t port, std::uint8_t data);

private:
    void begin_record(std::string_view type);
    void end_record();

    std::ofstream output_;
    std::uint64_t frame_ = 0;
    std::uint64_t tick_ = 0;
    std::uint64_t sequence_ = 0;
};

}  // namespace openaladdin::audio
