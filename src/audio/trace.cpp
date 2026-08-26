#include "audio/trace.hpp"

#include <stdexcept>

namespace openaladdin::audio {

AudioTrace::AudioTrace(const std::string& path)
    : output_(path) {
    if (!output_) {
        throw std::runtime_error("cannot open native audio trace: " + path);
    }
    output_
        << R"({"type":"header","format":"openaladdin-native-audio-trace-v1","frame_domain":"game_frame","tick_domain":"one_sound_tick_per_game_frame"})"
        << '\n';
}

AudioTrace::~AudioTrace() {
    if (output_) {
        output_.flush();
    }
}

void AudioTrace::begin_frame(std::uint64_t frame) {
    frame_ = frame;
    tick_ = frame;
}

void AudioTrace::begin_record(std::string_view type) {
    output_ << "{\"type\":\"" << type
            << "\",\"frame\":" << frame_
            << ",\"tick\":" << tick_
            << ",\"sequence\":" << sequence_++;
}

void AudioTrace::end_record() {
    output_ << "}\n";
}

void AudioTrace::record_command(std::uint8_t opcode,
                                std::span<const std::uint8_t> args,
                                std::string_view kind,
                                std::string_view phase,
                                int sound_id) {
    begin_record("audio_command");
    output_ << ",\"source\":\"native\",\"kind\":\"" << kind
            << "\",\"phase\":\"" << phase
            << "\",\"opcode\":" << static_cast<unsigned>(opcode)
            << ",\"sound_id\":";
    if (sound_id < 0) {
        output_ << "null";
    } else {
        output_ << sound_id;
    }
    output_ << ",\"args\":[";
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index != 0) {
            output_ << ',';
        }
        output_ << static_cast<unsigned>(args[index]);
    }
    output_ << ']';
    end_record();
}

void AudioTrace::record_event(const Z80SoundDriver::SoundEvent& event) {
    const auto kind = event.kind == Z80SoundDriver::SoundEvent::Kind::Note
        ? "note"
        : "control";
    const auto output = [&event] {
        switch (event.output) {
        case Z80SoundDriver::Output::Ym:
            return "ym";
        case Z80SoundDriver::Output::Psg:
            return "psg";
        case Z80SoundDriver::Output::Unknown:
            return "unknown";
        }
        return "unknown";
    }();

    begin_record("driver_event");
    output_ << ",\"source\":\"native\",\"kind\":\"" << kind
            << "\",\"output\":\"" << output
            << "\",\"channel\":" << static_cast<unsigned>(event.channel)
            << ",\"opcode\":" << static_cast<unsigned>(event.opcode)
            << ",\"stream_address\":" << event.stream_address
            << ",\"operand_a\":" << event.operand_a
            << ",\"operand_b\":" << event.operand_b
            << ",\"has_control_argument\":"
            << (event.has_control_argument ? "true" : "false")
            << ",\"control_argument\":";
    if (event.has_control_argument) {
        output_ << static_cast<unsigned>(event.control_argument);
    } else {
        output_ << "null";
    }
    output_ << ",\"has_patch_state\":"
            << (event.has_patch_state ? "true" : "false");
    if (event.has_patch_state) {
        output_ << ",\"patch_state\":[";
        for (std::size_t index = 0; index < event.patch_state.size(); ++index) {
            if (index != 0) {
                output_ << ',';
            }
            output_ << static_cast<unsigned>(event.patch_state[index]);
        }
        output_ << ']';
    }
    end_record();
}

void AudioTrace::record_psg_write(std::uint8_t data) {
    begin_record("audio_write");
    output_ << ",\"source\":\"z80\",\"kind\":\"psg\",\"data\":"
            << static_cast<unsigned>(data)
            << ",\"byte\":" << static_cast<unsigned>(data);
    end_record();
}

void AudioTrace::record_ym_write(std::uint8_t port, std::uint8_t data) {
    begin_record("audio_write");
    output_ << ",\"source\":\"z80\",\"kind\":\"ym2612\",\"port\":"
            << static_cast<unsigned>(port)
            << ",\"data\":" << static_cast<unsigned>(data)
            << ",\"byte\":" << static_cast<unsigned>(data);
    end_record();
}

}  // namespace openaladdin::audio
