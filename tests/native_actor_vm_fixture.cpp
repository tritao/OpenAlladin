#include "animation.hpp"
#include "game_state.hpp"
#include "movement.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// This runner is intentionally outside Engine. It reproduces the small
// debugger-injected actor-table captures by composing the reusable ROM VMs;
// it is not a second gameplay scheduler.
namespace {

using openaladdin::ActorState;
using openaladdin::AnimationContext;
using openaladdin::GameState;
using openaladdin::HorizontalDirection;
using openaladdin::MovementContext;
using openaladdin::MovementVm;
using openaladdin::PlayerAnimationVm;

struct Options {
    std::string rom;
    std::string actors;
    std::string state_output;
    int frames = 0;
};

std::vector<std::uint8_t> read_binary(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open ROM: " + path);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

std::uint32_t parse_unsigned(const std::string& value, const std::string& label) {
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed, 0);
    if (consumed != value.size()) {
        throw std::runtime_error("invalid " + label + ": " + value);
    }
    return static_cast<std::uint32_t>(parsed);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--rom" || argument == "--actor-records"
             || argument == "--state-output")
            && index + 1 < argc) {
            const std::string value = argv[++index];
            if (argument == "--rom") options.rom = value;
            else if (argument == "--actor-records") options.actors = value;
            else options.state_output = value;
        } else if (argument == "--frames" && index + 1 < argc) {
            options.frames = std::stoi(argv[++index]);
        } else {
            throw std::runtime_error("unknown or incomplete option: " + argument);
        }
    }
    if (options.rom.empty() || options.actors.empty() || options.state_output.empty()) {
        throw std::runtime_error(
            "usage: native_actor_vm_fixture --rom FILE --actor-records FILE "
            "--frames N --state-output FILE"
        );
    }
    if (options.frames < 0) throw std::runtime_error("--frames must be nonnegative");
    return options;
}

void load_actor_records(
    const std::string& path,
    std::array<ActorState, 32>& actors
) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open actor fixture: " + path);

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::istringstream row(line);
        std::string slot_text;
        if (!(row >> slot_text)) continue;
        if (!slot_text.empty() && slot_text.front() == '#') continue;

        std::string fields[16];
        std::string extra;
        int field_count = 0;
        while (field_count < 16 && (row >> fields[field_count])) {
            ++field_count;
        }
        if (field_count != 16 || (row >> extra)) {
            throw std::runtime_error(
                "invalid actor fixture record at " + path + ":"
                + std::to_string(line_number));
        }
        const std::uint32_t slot_value = parse_unsigned(slot_text, "actor slot");
        if (slot_value >= actors.size()) {
            throw std::runtime_error(
                "actor fixture slot is outside the 32-record table at " + path + ":"
                + std::to_string(line_number));
        }

        ActorState& actor = actors[slot_value];
        actor.type = static_cast<std::uint8_t>(parse_unsigned(fields[0], "actor type"));
        actor.x = static_cast<std::uint16_t>(parse_unsigned(fields[1], "actor x"));
        actor.y = static_cast<std::uint16_t>(parse_unsigned(fields[2], "actor y"));
        actor.movement_pc = parse_unsigned(fields[3], "movement PC");
        actor.frame_ptr = parse_unsigned(fields[4], "frame pointer");
        actor.animation_pc = parse_unsigned(fields[5], "animation PC");
        actor.flags = static_cast<std::uint8_t>(parse_unsigned(fields[6], "actor flags"));
        if (field_count > 7) {
            actor.facing_x_flip = static_cast<std::uint8_t>(
                parse_unsigned(fields[7], "horizontal facing"));
        }
        if (field_count > 8) {
            actor.facing_y_flip = static_cast<std::uint8_t>(
                parse_unsigned(fields[8], "vertical facing"));
        }
        if (field_count > 9) {
            actor.movement_command_timer = static_cast<std::uint8_t>(
                parse_unsigned(fields[9], "movement command timer"));
        }
        if (field_count > 10) {
            actor.movement_loop_pc = parse_unsigned(fields[10], "movement loop PC");
        }
        if (field_count > 11) {
            actor.movement_loop_timer = static_cast<std::uint8_t>(
                parse_unsigned(fields[11], "movement loop timer"));
        }
        if (field_count > 12) {
            actor.movement_return_pc = parse_unsigned(fields[12], "movement return PC");
        }
        if (field_count > 13) {
            actor.movement_word_18 = static_cast<std::int16_t>(
                parse_unsigned(fields[13], "movement word 18")
            );
        }
        if (field_count > 14) {
            actor.movement_word_1a = static_cast<std::int16_t>(
                parse_unsigned(fields[14], "movement word 1A")
            );
        }
        if (field_count > 15) {
            actor.sprite_attribute = static_cast<std::uint16_t>(
                parse_unsigned(fields[15], "sprite attribute")
            );
        }
    }
}

void write_actor(std::ostream& output, std::size_t slot, const ActorState& actor) {
    output << (slot == 0 ? "" : ",")
           << "{\"slot\":" << slot
           << ",\"type\":" << static_cast<unsigned>(actor.type)
           << ",\"x\":" << actor.x
           << ",\"y\":" << actor.y
           << ",\"movement_flags\":" << static_cast<unsigned>(actor.movement_flags)
           << ",\"runtime_field_07\":" << static_cast<unsigned>(actor.runtime_field_07)
           << ",\"runtime_field_07_delay\":"
           << static_cast<unsigned>(actor.runtime_field_07_delay)
           << ",\"facing_x_flip\":" << static_cast<unsigned>(actor.facing_x_flip)
           << ",\"facing_y_flip\":" << static_cast<unsigned>(actor.facing_y_flip)
           << ",\"movement_pc\":" << actor.movement_pc
           << ",\"movement_loop_pc\":" << actor.movement_loop_pc
           << ",\"movement_loop_timer\":"
           << static_cast<unsigned>(actor.movement_loop_timer)
           << ",\"movement_word_18\":" << actor.movement_word_18
           << ",\"movement_word_1a\":" << actor.movement_word_1a
           << ",\"sprite_attribute\":" << actor.sprite_attribute
           << ",\"frame_ptr\":" << actor.frame_ptr
           << ",\"animation_pc\":" << actor.animation_pc
           << ",\"movement_return_pc\":" << actor.movement_return_pc
           << ",\"flags\":" << static_cast<unsigned>(actor.flags)
           << ",\"interaction_state\":"
           << static_cast<unsigned>(actor.interaction_state)
           << ",\"terminal_timer\":"
           << static_cast<unsigned>(actor.terminal_timer)
           << ",\"movement_command_timer\":"
           << static_cast<unsigned>(actor.movement_command_timer)
           << ",\"animation_timer\":"
           << static_cast<unsigned>(actor.animation_timer)
           << ",\"resource_count\":"
           << static_cast<unsigned>(actor.resource_count)
           << ",\"interaction_resource_offset\":"
           << actor.interaction_resource_offset
           << ",\"interaction_selector\":"
           << static_cast<unsigned>(actor.interaction_selector)
           // This low-level fixture owns a raw actor table rather than the
           // ActorSystem, so host provenance is intentionally absent.
           << ",\"spawned_by_interaction\":false"
           << ",\"spawned_by_animation\":false"
           << ",\"spawned_by_apple\":false"
           << ",\"linked_actor_slot\":" << actor.linked_actor_slot
           << "}";
}

void write_state(
    std::ostream& output,
    int frame,
    const std::array<ActorState, 32>& actors
) {
    output << "{\"type\":\"state\",\"format\":\"openaladdin-frame-state-v3\",\"frame\":"
           << frame << ",\"actors\":[";
    for (std::size_t slot = 0; slot < actors.size(); ++slot) {
        write_actor(output, slot, actors[slot]);
    }
    output << "]}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const std::vector<std::uint8_t> rom = read_binary(options.rom);
        std::array<ActorState, 32> actors{};
        load_actor_records(options.actors, actors);

        std::array<PlayerAnimationVm, 32> actor_vms{};
        std::array<bool, 32> active_animation_fixtures{};
        std::array<bool, 32> loaded_vms{};
        GameState state;
        state.camera.vdp_update = 1;
        AnimationContext context;
        context.state = &state;
        MovementVm movement_vm;

        std::ofstream output(options.state_output);
        if (!output) throw std::runtime_error("cannot write fixture state: " + options.state_output);
        write_state(output, 0, actors);

        for (int frame = 0; frame < options.frames; ++frame) {
            for (std::size_t slot = 0; slot < actors.size(); ++slot) {
                ActorState& actor = actors[slot];
                if (!active_animation_fixtures[slot]
                    && (actor.type != 0x7D
                        || actor.movement_pc == 0
                        || actor.animation_pc != 0x00125952)) {
                    continue;
                }
                if (actor.type == 0 || actor.animation_pc == 0) {
                    active_animation_fixtures[slot] = false;
                    continue;
                }
                if (!loaded_vms[slot]) {
                    actor_vms[slot].load_rom(options.rom);
                    loaded_vms[slot] = true;
                }
                actor_vms[slot].update_actor(actor, context);
                active_animation_fixtures[slot] = true;
            }
            movement_vm.tick(
                actors,
                MovementContext{rom, 0, 0, nullptr, {}}
            );
            write_state(output, frame + 1, actors);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
