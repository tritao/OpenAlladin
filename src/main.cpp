#include "engine.hpp"

#include <SDL.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "audio/mixer.hpp"
#include "audio/sdl_audio.hpp"
#include "audio/trace.hpp"
#include "audio/z80_audio_bridge.hpp"
#include "audio/z80_sound_driver.hpp"

namespace {

struct Options {
    std::string assets = "build/assets/levels/level01";
    int level_index = 1;
    bool assets_explicit = false;
    std::string sprites = "build/assets/sprites";
    std::string rom = "rom/Disneys_Aladdin_U_p1.bin";
    // Native Level 01 actors are refilled from the ROM interaction map. The
    // TSV remains available as an explicit compatibility/replay fixture via
    // --actor-records.
    std::string actor_records;
    std::string actor_timeline;
    int frames = -1;
    bool no_window = false;
    bool no_audio = false;
    int sound_id = -1;
    std::string audio_trace;
    bool demo = false;
    bool render_only = false;
    std::string state_output;
    std::string framebuffer_output;
    int framebuffer_frame = -1;
    std::string input_schedule;
    std::string checkpoint_player;
    std::string checkpoint_terrain_behavior;
    std::string checkpoint_terrain_landing_state;
    std::string checkpoint_frame_ptr;
    std::string checkpoint_animation;
    std::string checkpoint_animation_phase_delay;
    std::string checkpoint_animation_selector;
    std::string checkpoint_facing_x_flip;
    std::string checkpoint_vdp;
    int checkpoint_vdp_frame = -1;
    std::string checkpoint_camera;
};

std::vector<std::string> split_schedule(const std::string& schedule) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= schedule.size()) {
        const std::size_t end = schedule.find(',', start);
        std::string item = schedule.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto first = item.find_first_not_of(" \t");
        const auto last = item.find_last_not_of(" \t");
        if (first != std::string::npos) {
            item = item.substr(first, last - first + 1);
        } else {
            item.clear();
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
        for (int i = 0; i < repeat; ++i) {
            result.push_back(item.empty() ? "none" : item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

void apply_scheduled_token(const std::string& token, openaladdin::InputState& input) {
    std::size_t start = 0;
    while (start <= token.size()) {
        const std::size_t end = token.find('+', start);
        const std::string part = token.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "up") {
            input.up = true;
        } else if (part == "down") {
            input.down = true;
        } else if (part == "left") {
            input.left = true;
        } else if (part == "right") {
            input.right = true;
        } else if (part == "a" || part == "attack") {
            input.attack_pressed = true;
        } else if (part == "apple" || part == "throw") {
            input.apple_pressed = true;
        } else if (part == "c" || part == "jump" || part == "space") {
            input.jump_pressed = true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

std::vector<int> parse_checkpoint(const std::string& value) {
    std::vector<int> fields;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        fields.push_back(std::stoi(item));
    }
    if (fields.size() != 4 && fields.size() != 5) {
        throw std::runtime_error("--checkpoint-player expects x,y,vx,vy[,grounded]");
    }
    return fields;
}

std::uint8_t parse_sound_id(const std::string& value) {
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed, 0);
    if (consumed != value.size()
        || parsed >= openaladdin::audio::Z80SoundDriver::kSoundSequenceCount) {
        throw std::runtime_error(
            "--sound-id expects a ROM sound ID from 0 through 0x71");
    }
    return static_cast<std::uint8_t>(parsed);
}

std::string_view sound_command_kind(std::uint8_t sound_id) {
    using Driver = openaladdin::audio::Z80SoundDriver;
    if (sound_id == Driver::kLevel01MusicSoundId) {
        return "MUSIC";
    }
    if (sound_id == Driver::kAnimationSfxSoundId) {
        return "SFX";
    }
    if (sound_id == Driver::kInteractionEventSoundId) {
        return "EVENT";
    }
    return "SOUND";
}

std::vector<int> parse_camera_checkpoint(const std::string& value) {
    std::vector<int> fields;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        fields.push_back(std::stoi(item));
    }
    if (fields.size() != 2 && fields.size() != 7 && fields.size() != 10) {
        throw std::runtime_error(
            "--checkpoint-camera expects x,y[,reference_x,reference_y,scroll_x,scroll_y,scene_state[,horizontal_threshold,vertical_threshold,update_delay]]"
        );
    }
    return fields;
}

openaladdin::AnimationSelectorState parse_animation_selector(const std::string& value) {
    std::vector<int> fields;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        fields.push_back(std::stoi(item));
    }
    if (fields.size() != 25) {
        throw std::runtime_error(
            "--checkpoint-animation-selector expects 25 comma-separated fields"
        );
    }
    openaladdin::AnimationSelectorState selector;
    selector.animation_gate = static_cast<std::uint8_t>(fields[0]);
    selector.terminal_transition = static_cast<std::uint8_t>(fields[1]);
    selector.scene_script_countdown = static_cast<std::uint8_t>(fields[2]);
    selector.interaction_lock = static_cast<std::uint8_t>(fields[3]);
    selector.response_active = static_cast<std::uint8_t>(fields[4]);
    selector.landing_state = static_cast<std::uint8_t>(fields[5]);
    selector.transition_gate = static_cast<std::uint8_t>(fields[6]);
    selector.transition_lock = static_cast<std::uint8_t>(fields[7]);
    selector.transition_state = static_cast<std::uint8_t>(fields[8]);
    selector.transition_mode = static_cast<std::uint8_t>(fields[9]);
    selector.transition_flag = static_cast<std::uint8_t>(fields[10]);
    selector.transition_response = static_cast<std::uint8_t>(fields[11]);
    selector.transition_state_de = static_cast<std::uint8_t>(fields[12]);
    selector.transition_state_df = static_cast<std::uint8_t>(fields[13]);
    selector.camera_special_mode = static_cast<std::uint8_t>(fields[14]);
    selector.response_latch = static_cast<std::uint8_t>(fields[15]);
    selector.response_animation = static_cast<std::uint8_t>(fields[16]);
    selector.response_state_ee = static_cast<std::uint8_t>(fields[17]);
    selector.response_state_ef = static_cast<std::uint8_t>(fields[18]);
    selector.response_state_f0 = static_cast<std::uint8_t>(fields[19]);
    selector.response_state_101 = static_cast<std::uint8_t>(fields[20]);
    selector.horizontal_response = static_cast<std::int16_t>(fields[21]);
    selector.response_timer = static_cast<std::uint8_t>(fields[22]);
    selector.interaction_pending = static_cast<std::uint8_t>(fields[23]);
    selector.state_lock = static_cast<std::uint8_t>(fields[24]);
    return selector;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--assets" && i + 1 < argc) {
            options.assets = argv[++i];
            options.assets_explicit = true;
        } else if (argument == "--level-index" && i + 1 < argc) {
            options.level_index = std::stoi(argv[++i], nullptr, 0);
            if (options.level_index < 0 || options.level_index > 12) {
                throw std::runtime_error("--level-index expects a value from 0 through 12");
            }
        } else if (argument == "--sprites" && i + 1 < argc) {
            options.sprites = argv[++i];
        } else if (argument == "--rom" && i + 1 < argc) {
            options.rom = argv[++i];
        } else if (argument == "--actor-records" && i + 1 < argc) {
            options.actor_records = argv[++i];
        } else if (argument == "--actor-timeline" && i + 1 < argc) {
            options.actor_timeline = argv[++i];
        } else if (argument == "--frames" && i + 1 < argc) {
            options.frames = std::stoi(argv[++i]);
        } else if (argument == "--no-window") {
            options.no_window = true;
        } else if (argument == "--no-audio") {
            options.no_audio = true;
        } else if (argument == "--sound-id" && i + 1 < argc) {
            options.sound_id = parse_sound_id(argv[++i]);
        } else if (argument == "--audio-trace" && i + 1 < argc) {
            options.audio_trace = argv[++i];
        } else if (argument == "--demo") {
            options.demo = true;
        } else if (argument == "--render-checkpoint") {
            options.render_only = true;
        } else if (argument == "--state-output" && i + 1 < argc) {
            options.state_output = argv[++i];
        } else if (argument == "--framebuffer-out" && i + 1 < argc) {
            options.framebuffer_output = argv[++i];
        } else if (argument == "--framebuffer-frame" && i + 1 < argc) {
            options.framebuffer_frame = std::stoi(argv[++i]);
        } else if (argument == "--input-schedule" && i + 1 < argc) {
            options.input_schedule = argv[++i];
        } else if (argument == "--checkpoint-player" && i + 1 < argc) {
            options.checkpoint_player = argv[++i];
        } else if (argument == "--checkpoint-terrain-behavior" && i + 1 < argc) {
            options.checkpoint_terrain_behavior = argv[++i];
        } else if (argument == "--checkpoint-terrain-landing-state" && i + 1 < argc) {
            options.checkpoint_terrain_landing_state = argv[++i];
        } else if (argument == "--checkpoint-frame-ptr" && i + 1 < argc) {
            options.checkpoint_frame_ptr = argv[++i];
        } else if (argument == "--checkpoint-animation" && i + 1 < argc) {
            options.checkpoint_animation = argv[++i];
        } else if (argument == "--checkpoint-animation-phase-delay" && i + 1 < argc) {
            options.checkpoint_animation_phase_delay = argv[++i];
        } else if (argument == "--checkpoint-animation-selector" && i + 1 < argc) {
            options.checkpoint_animation_selector = argv[++i];
        } else if (argument == "--checkpoint-facing-x-flip" && i + 1 < argc) {
            options.checkpoint_facing_x_flip = argv[++i];
        } else if (argument == "--checkpoint-vdp" && i + 2 < argc) {
            options.checkpoint_vdp = argv[++i];
            options.checkpoint_vdp_frame = std::stoi(argv[++i]);
        } else if (argument == "--checkpoint-camera" && i + 1 < argc) {
            options.checkpoint_camera = argv[++i];
        } else if (argument == "--help") {
            std::cout << "usage: openaladdin [--assets DIR] [--level-index N] [--sprites DIR] [--rom FILE] [--actor-records FILE] [--actor-timeline FILE] [--frames N] [--no-window] [--no-audio] [--sound-id ID] [--audio-trace PATH] [--demo] [--render-checkpoint]\n"
                         "       [--state-output PATH] [--framebuffer-out PATH] [--framebuffer-frame N]\n"
                         "       [--input-schedule SCHEDULE]\n"
                         "       [--checkpoint-player X,Y,VX,VY[,GROUNDED]]\n"
                         "       [--checkpoint-terrain-behavior BYTE]\n"
                         "       [--checkpoint-terrain-landing-state BYTE]\n"
                         "       [--checkpoint-frame-ptr ADDRESS]\n"
                         "       [--checkpoint-animation PC,TIMER]\n"
                         "       [--checkpoint-animation-phase-delay TICKS]\n"
                         "       [--checkpoint-animation-selector FIELDS]\n"
                         "       [--checkpoint-facing-x-flip VALUE]\n"
                         "       [--checkpoint-vdp TRACE_DIR FRAME]\n"
                         "       [--checkpoint-camera X,Y[,REFERENCE_X,REFERENCE_Y,SCROLL_X,SCROLL_Y,SCENE_STATE]]\n"
                         "       --sound-id ID selects a ROM sound sequence (default: Level 01 music 0x49)\n"
                         "       --audio-trace PATH writes a deterministic native command/event/bus trace\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + argument);
        }
    }
    if (!options.assets_explicit && options.level_index != 1) {
        options.assets = "build/assets/levels/level";
        if (options.level_index < 10) options.assets += '0';
        options.assets += std::to_string(options.level_index);
    }
    return options;
}

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open audio ROM: " + path);
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("cannot size audio ROM: " + path);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error("cannot read audio ROM: " + path);
    }
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.no_window) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        }
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }

        openaladdin::Engine engine;
        engine.load(options.assets, options.sprites, options.rom, options.actor_records, options.actor_timeline);
        if (!options.checkpoint_terrain_behavior.empty()) {
            engine.set_checkpoint_terrain_behavior(
                static_cast<std::uint8_t>(std::stoul(options.checkpoint_terrain_behavior, nullptr, 0))
            );
        }
        if (!options.checkpoint_player.empty()) {
            const auto checkpoint = parse_checkpoint(options.checkpoint_player);
            engine.set_checkpoint(
                checkpoint[0],
                checkpoint[1],
                static_cast<std::int16_t>(checkpoint[2]),
                static_cast<std::int16_t>(checkpoint[3]),
                checkpoint.size() < 5 || checkpoint[4] != 0
            );
        }
        if (!options.checkpoint_terrain_landing_state.empty()) {
            engine.set_checkpoint_terrain_landing_state(
                static_cast<std::uint8_t>(std::stoul(options.checkpoint_terrain_landing_state, nullptr, 0))
            );
        }
        if (!options.checkpoint_frame_ptr.empty()) {
            engine.set_checkpoint_frame_ptr(std::stoi(options.checkpoint_frame_ptr, nullptr, 0));
        }
        if (!options.checkpoint_animation.empty()) {
            const auto separator = options.checkpoint_animation.find(',');
            if (separator == std::string::npos) {
                throw std::runtime_error("--checkpoint-animation expects PC,TIMER");
            }
            engine.set_checkpoint_animation(
                static_cast<std::uint32_t>(std::stoul(options.checkpoint_animation.substr(0, separator), nullptr, 0)),
                std::stoi(options.checkpoint_animation.substr(separator + 1), nullptr, 0)
            );
        }
        if (!options.checkpoint_animation_phase_delay.empty()) {
            engine.set_checkpoint_animation_phase_delay(
                std::stoi(options.checkpoint_animation_phase_delay, nullptr, 0)
            );
        }
        if (!options.checkpoint_animation_selector.empty()) {
            engine.set_checkpoint_animation_selector(
                parse_animation_selector(options.checkpoint_animation_selector)
            );
        }
        if (!options.checkpoint_facing_x_flip.empty()) {
            engine.set_checkpoint_facing_x_flip(
                std::stoi(options.checkpoint_facing_x_flip, nullptr, 0) != 0
            );
        }
        if (!options.checkpoint_vdp.empty()) {
            engine.set_checkpoint_vdp(options.checkpoint_vdp, options.checkpoint_vdp_frame);
        }
        if (!options.checkpoint_camera.empty()) {
            const auto checkpoint = parse_camera_checkpoint(options.checkpoint_camera);
            engine.set_checkpoint_camera(
                checkpoint[0],
                checkpoint[1],
                checkpoint.size() == 7 ? checkpoint[2] : checkpoint[0],
                checkpoint.size() == 7 ? checkpoint[3] : checkpoint[1],
                checkpoint.size() == 7 ? checkpoint[4] : 0,
                checkpoint.size() == 7 ? checkpoint[5] : 0,
                checkpoint.size() >= 7 ? checkpoint[6] : 1,
                checkpoint.size() == 10 ? checkpoint[7] : -1,
                checkpoint.size() == 10 ? checkpoint[8] : -1,
                checkpoint.size() == 10 ? checkpoint[9] : -1
            );
        }

        std::unique_ptr<openaladdin::audio::AudioTrace> audio_trace;
        if (!options.audio_trace.empty()) {
            const std::filesystem::path trace_path(options.audio_trace);
            if (trace_path.has_parent_path()) {
                std::filesystem::create_directories(trace_path.parent_path());
            }
            audio_trace = std::make_unique<openaladdin::audio::AudioTrace>(
                options.audio_trace);
            audio_trace->begin_frame(0);
        }

        openaladdin::audio::Mixer mixer;
        openaladdin::audio::SdlAudioOutput audio_output(mixer);
        openaladdin::audio::Z80AudioBridge audio_bridge({
            [&audio_output, &audio_trace](std::uint8_t data) {
                if (audio_trace) {
                    audio_trace->record_psg_write(data);
                }
                audio_output.write_psg(data);
            },
            [&audio_output, &audio_trace](std::uint8_t port, std::uint8_t data) {
                if (audio_trace) {
                    audio_trace->record_ym_write(port, data);
                }
                audio_output.write_ym2612(port, data);
            },
        });
        std::unique_ptr<openaladdin::audio::Z80SoundDriver> sound_driver;
        if (!options.no_audio && !options.render_only) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                std::cerr << "openaladdin: audio disabled: " << SDL_GetError() << '\n';
            } else {
                try {
                    audio_output.open();
                    audio_bridge.reset();
                    const auto audio_rom = read_binary_file(options.rom);
                    sound_driver = std::make_unique<openaladdin::audio::Z80SoundDriver>(
                        audio_rom,
                        [&audio_bridge, &audio_trace](const auto& event) {
                            if (audio_trace) {
                                audio_trace->record_event(event);
                            }
                            audio_bridge.handle(event);
                        }
                    );
                    // This is the same four-pointer setup issued by the
                    // original 68K audio initializer before it starts a
                    // sound. The third pointer is the sequence table; the
                    // first one supplies the per-channel patch states.
                    constexpr std::array<std::uint32_t, 4> audio_tables{
                        openaladdin::audio::Z80SoundDriver::kPatchTableBase,
                        0x1BAF46,
                        openaladdin::audio::Z80SoundDriver::kSequenceTableBase,
                        0x1C73CB,
                    };
                    std::array<std::uint8_t, 12> audio_setup{};
                    for (std::size_t index = 0; index < audio_tables.size(); ++index) {
                        audio_setup[index * 3] =
                            static_cast<std::uint8_t>(audio_tables[index]);
                        audio_setup[index * 3 + 1] = static_cast<std::uint8_t>(
                            audio_tables[index] >> 8);
                        audio_setup[index * 3 + 2] = static_cast<std::uint8_t>(
                            audio_tables[index] >> 16);
                    }
                    if (audio_trace) {
                        audio_trace->record_command(
                            0x0B, audio_setup, "INIT", "immediate");
                    }
                    sound_driver->command(0x0B, audio_setup);
                    const std::array<std::uint8_t, 1> selected_sound{
                        options.sound_id >= 0
                            ? static_cast<std::uint8_t>(options.sound_id)
                            : openaladdin::audio::Z80SoundDriver::kLevel01MusicSoundId
                    };
                    if (audio_trace) {
                        audio_trace->record_command(
                            0x10,
                            selected_sound,
                            sound_command_kind(selected_sound[0]),
                            "immediate",
                            selected_sound[0]);
                    }
                    sound_driver->command(0x10, selected_sound);
                } catch (const std::exception& error) {
                    std::cerr << "openaladdin: audio disabled: " << error.what() << '\n';
                    sound_driver.reset();
                    audio_output.close();
                }
            }
        }

        std::ofstream state_file;
        if (!options.state_output.empty()) {
            const std::filesystem::path state_path(options.state_output);
            if (state_path.has_parent_path()) {
                std::filesystem::create_directories(state_path.parent_path());
            }
            state_file.open(state_path);
            if (!state_file) {
                throw std::runtime_error("cannot open state output: " + options.state_output);
            }
            state_file << "{\"type\":\"header\",\"format\":\"openaladdin-frame-state-v3\",\"rom\":\"openaladdin\",\"rom_sha256\":\"\",\"state_boundary\":\"game-loop\",\"sync\":{\"boundary\":\"VBlankInterrupt\",\"state_boundary\":\"game-loop\",\"atomic_fields\":[\"player\",\"camera\",\"terrain\",\"scene\",\"actors\",\"scheduler\"],\"atomic_actor_fields\":[\"type\",\"x\",\"y\",\"movement_flags\",\"runtime_field_07\",\"runtime_field_07_delay\",\"facing_x_flip\",\"facing_y_flip\",\"movement_pc\",\"movement_loop_pc\",\"movement_loop_timer\",\"movement_word_18\",\"frame_ptr\",\"animation_pc\",\"movement_return_pc\",\"flags\",\"interaction_state\",\"terminal_timer\",\"movement_command_timer\",\"animation_timer\",\"animation_defer_ticks\",\"animation_force_next_tick\",\"animation_tick_phase\",\"resource_count\",\"interaction_resource_offset\",\"interaction_selector\",\"spawned_by_interaction\",\"spawned_by_animation\",\"spawned_by_apple\",\"linked_actor_slot\",\"vm_actor_record\"],\"actors_qualified\":true,\"actor_slot_count\":32}}\n";
            engine.write_state(state_file, "none");
        }

        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        if (!options.no_window) {
            window = SDL_CreateWindow(
                "OpenAladdin - level 01 vertical slice",
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                960,
                672,
                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
            );
            if (window == nullptr) {
                throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
            }
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (renderer == nullptr) {
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            }
            if (renderer == nullptr) {
                throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
            }
        } else {
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 224, 32, SDL_PIXELFORMAT_RGBA8888);
            if (surface == nullptr) {
                throw std::runtime_error(std::string("SDL_CreateRGBSurface failed: ") + SDL_GetError());
            }
            renderer = SDL_CreateSoftwareRenderer(surface);
            if (renderer == nullptr) {
                SDL_FreeSurface(surface);
                throw std::runtime_error(std::string("SDL_CreateSoftwareRenderer failed: ") + SDL_GetError());
            }
            SDL_SetRenderTarget(renderer, nullptr);
        }

        bool previous_jump = false;
        bool previous_attack = false;
        const std::vector<std::string> scheduled_inputs = split_schedule(options.input_schedule);
        int rendered_frames = 0;
        bool framebuffer_written = false;
        if (options.render_only) {
            engine.render(renderer);
            if (!options.framebuffer_output.empty()
                && (options.framebuffer_frame < 0 || engine.frame() == options.framebuffer_frame)) {
                const std::filesystem::path framebuffer_path(options.framebuffer_output);
                if (framebuffer_path.has_parent_path()) {
                    std::filesystem::create_directories(framebuffer_path.parent_path());
                }
                engine.write_framebuffer_ppm(options.framebuffer_output);
                framebuffer_written = true;
            }
        } else {
            while (!engine.quit_requested() && (options.frames < 0 || rendered_frames < options.frames)) {
            if (audio_trace) {
                audio_trace->begin_frame(static_cast<std::uint64_t>(rendered_frames));
            }
            openaladdin::InputState input;
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    engine.request_quit();
                }
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                    engine.request_quit();
                }
            }
            const std::uint8_t* keys = SDL_GetKeyboardState(nullptr);
            input.up = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
            input.down = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
            input.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
            input.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
            const bool jump = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_C];
            const bool attack = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_Z];
            input.jump_pressed = jump && !previous_jump;
            input.attack_pressed = attack && !previous_attack;
            previous_jump = jump;
            previous_attack = attack;
            std::string input_token;

            if (!options.input_schedule.empty()) {
                input = openaladdin::InputState{};
                input_token = rendered_frames < static_cast<int>(scheduled_inputs.size())
                    ? scheduled_inputs[static_cast<std::size_t>(rendered_frames)]
                    : "none";
                apply_scheduled_token(input_token, input);
                input.jump_pressed = input.jump_pressed && !previous_jump;
                previous_jump = input.jump_pressed;
            }

            if (options.demo) {
                // Deterministic run/jump input for headless regression checks.
                input.right = rendered_frames < 100;
                input.jump_pressed = rendered_frames == 30;
            }

            if (options.input_schedule.empty()) {
                input_token = "none";
                if (input.jump_pressed) {
                    input_token = "jump";
                } else if (input.attack_pressed) {
                    input_token = "a";
                } else if (input.left && !input.right) {
                    input_token = "left";
                } else if (input.right && !input.left) {
                    input_token = "right";
                }
            }

            engine.update(input);
            const auto sound_requests = engine.take_sound_requests();
            if (sound_driver) {
                try {
                    for (const std::uint8_t sound_id : sound_requests) {
                        const std::array<std::uint8_t, 1> sound_command{sound_id};
                        if (audio_trace) {
                            audio_trace->record_command(
                                0x10,
                                sound_command,
                                sound_command_kind(sound_id),
                                "enqueue",
                                sound_id);
                        }
                        sound_driver->enqueue_command(0x10, sound_command);
                    }
                    audio_bridge.tick();
                    sound_driver->tick();
                } catch (const std::exception& error) {
                    std::cerr << "openaladdin: sound driver stopped: "
                              << error.what() << '\n';
                    sound_driver.reset();
                    audio_output.reset();
                }
            }
            if (state_file) {
                engine.write_state(state_file, input_token);
            }
            engine.render(renderer);
            ++rendered_frames;
            if (!options.framebuffer_output.empty()
                && options.framebuffer_frame >= 0
                && engine.frame() == options.framebuffer_frame) {
                const std::filesystem::path framebuffer_path(options.framebuffer_output);
                if (framebuffer_path.has_parent_path()) {
                    std::filesystem::create_directories(framebuffer_path.parent_path());
                }
                engine.write_framebuffer_ppm(options.framebuffer_output);
                framebuffer_written = true;
            }
            if (options.no_window) {
                // Keep --no-window deterministic and fast for CI/smoke tests.
                SDL_RenderPresent(renderer);
            }
            }
        }

        if (!options.framebuffer_output.empty() && !framebuffer_written) {
            if (options.framebuffer_frame >= 0) {
                throw std::runtime_error(
                    "requested framebuffer frame was not rendered: "
                    + std::to_string(options.framebuffer_frame)
                );
            }
            const std::filesystem::path framebuffer_path(options.framebuffer_output);
            if (framebuffer_path.has_parent_path()) {
                std::filesystem::create_directories(framebuffer_path.parent_path());
            }
            engine.write_framebuffer_ppm(options.framebuffer_output);
        }

        const auto& player = engine.player();
        std::cout << "frames: " << engine.frame()
                  << " player_x: " << player.x
                  << " player_y: " << player.y
                  << " player_vx: " << player.vx
                  << " player_vy: " << player.vy
                  << " grounded: " << (player.grounded ? "yes" : "no")
                  << '\n';

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        audio_output.close();
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "openaladdin: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
