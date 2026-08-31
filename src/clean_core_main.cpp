#include "core/frame.hpp"
#include "core/trace.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string rom = "rom/Disneys_Aladdin_U_p1.bin";
    std::string state_output;
    std::string input_schedule;
    int frames = 0;
};

std::vector<std::string> split_schedule(const std::string& schedule) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= schedule.size()) {
        const std::size_t end = schedule.find(',', start);
        std::string item = schedule.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start);
        const std::size_t first = item.find_first_not_of(" \t");
        const std::size_t last = item.find_last_not_of(" \t");
        if (first == std::string::npos) {
            item.clear();
        } else {
            item = item.substr(first, last - first + 1);
        }

        std::size_t separator = item.find_last_of("*:");
        int repeat = 1;
        if (separator != std::string::npos && separator + 1 < item.size()) {
            const std::string count = item.substr(separator + 1);
            if (count.find_first_not_of("0123456789") == std::string::npos) {
                repeat = std::stoi(count);
                item.resize(separator);
            }
        }
        for (int index = 0; index < repeat; ++index) {
            result.push_back(item.empty() ? "none" : item);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

bool read_binary_file(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "clean-core: cannot open ROM: " << path << '\n';
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        std::cerr << "clean-core: cannot size ROM: " << path << '\n';
        return false;
    }
    bytes.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        std::cerr << "clean-core: cannot read ROM: " << path << '\n';
        bytes.clear();
        return false;
    }
    return true;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--rom" && index + 1 < argc) {
            options.rom = argv[++index];
        } else if (argument == "--state-output" && index + 1 < argc) {
            options.state_output = argv[++index];
        } else if (argument == "--input-schedule" && index + 1 < argc) {
            options.input_schedule = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = std::stoi(argv[++index]);
            if (options.frames < 0) {
                throw std::runtime_error("--frames must not be negative");
            }
        } else if (argument == "--no-window" || argument == "--no-audio") {
            // Accepted for parity with the native replay command. The clean
            // client has no presentation or audio host dependency.
        } else if (argument == "--help") {
            std::cout << "usage: openaladdin_clean_core_client [--rom FILE] [--frames N]"
                         " [--state-output PATH] [--input-schedule SCHEDULE]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + argument);
        }
    }
    return options;
}

void make_parent_directory(const std::string& path) {
    const std::filesystem::path output(path);
    if (output.has_parent_path()) {
        std::filesystem::create_directories(output.parent_path());
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        std::vector<std::uint8_t> rom;
        if (!read_binary_file(options.rom, rom)) return 1;

        openaladdin::core::CoreRuntime core;
        openaladdin::core::bind_rom(
            core,
            openaladdin::core::RomView{rom.data(), rom.size()});
        openaladdin::core::reset(core);

        std::ofstream state;
        if (!options.state_output.empty()) {
            make_parent_directory(options.state_output);
            state.open(options.state_output);
            if (!state) {
                std::cerr << "clean-core: cannot open state output: "
                          << options.state_output << '\n';
                return 1;
            }
        }

        openaladdin::core::CoreTrace trace;
        if (state) {
            openaladdin::core::trace_begin(trace, state, &core);
            openaladdin::core::trace_state(trace, core, 0, "none", true);
        }

        const std::vector<std::string> schedule =
            split_schedule(options.input_schedule);
        for (int frame = 0; frame < options.frames; ++frame) {
            const std::string& token = frame < static_cast<int>(schedule.size())
                ? schedule[static_cast<std::size_t>(frame)]
                : std::string("none");
            openaladdin::core::step_frame(
                core,
                static_cast<std::uint64_t>(frame + 1),
                token,
                state ? &trace : nullptr);
        }
        if (state) state.flush();
        std::cout << "clean-core: trace "
                  << (options.state_output.empty() ? "disabled" : options.state_output)
                  << " (" << options.frames + 1 << " state frame(s))\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "clean-core: " << error.what() << '\n';
        return 1;
    }
}
