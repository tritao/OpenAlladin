#include "engine.hpp"

#include <SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string assets = "build/assets/levels/level01";
    int frames = -1;
    bool no_window = false;
    bool demo = false;
    std::string state_output;
    std::string input_schedule;
    std::string checkpoint_player;
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
        if (part == "left") {
            input.left = true;
        } else if (part == "right") {
            input.right = true;
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

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--assets" && i + 1 < argc) {
            options.assets = argv[++i];
        } else if (argument == "--frames" && i + 1 < argc) {
            options.frames = std::stoi(argv[++i]);
        } else if (argument == "--no-window") {
            options.no_window = true;
        } else if (argument == "--demo") {
            options.demo = true;
        } else if (argument == "--state-output" && i + 1 < argc) {
            options.state_output = argv[++i];
        } else if (argument == "--input-schedule" && i + 1 < argc) {
            options.input_schedule = argv[++i];
        } else if (argument == "--checkpoint-player" && i + 1 < argc) {
            options.checkpoint_player = argv[++i];
        } else if (argument == "--help") {
            std::cout << "usage: openaladdin [--assets DIR] [--frames N] [--no-window] [--demo]\n"
                         "       [--state-output PATH] [--input-schedule SCHEDULE]\n"
                         "       [--checkpoint-player X,Y,VX,VY[,GROUNDED]]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + argument);
        }
    }
    return options;
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
        engine.load(options.assets);
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
            state_file << "{\"type\":\"header\",\"format\":\"openaladdin-frame-state-v1\",\"rom\":\"openaladdin\",\"rom_sha256\":\"\"}\n";
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
        const std::vector<std::string> scheduled_inputs = split_schedule(options.input_schedule);
        int rendered_frames = 0;
        while (!engine.quit_requested() && (options.frames < 0 || rendered_frames < options.frames)) {
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
            input.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
            input.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
            const bool jump = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_C];
            input.jump_pressed = jump && !previous_jump;
            previous_jump = jump;
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
                } else if (input.left && !input.right) {
                    input_token = "left";
                } else if (input.right && !input.left) {
                    input_token = "right";
                }
            }

            engine.update(input);
            if (state_file) {
                engine.write_state(state_file, input_token);
            }
            engine.render(renderer);
            ++rendered_frames;
            if (options.no_window) {
                // Keep --no-window deterministic and fast for CI/smoke tests.
                SDL_RenderPresent(renderer);
            }
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
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "openaladdin: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
