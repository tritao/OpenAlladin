#include "engine.hpp"

#include <SDL.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string assets = "build/assets/levels/level01";
    int frames = -1;
    bool no_window = false;
    bool demo = false;
};

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
        } else if (argument == "--help") {
            std::cout << "usage: openaladdin [--assets DIR] [--frames N] [--no-window] [--demo]\n";
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

            if (options.demo) {
                // Deterministic run/jump input for headless regression checks.
                input.right = rendered_frames < 100;
                input.jump_pressed = rendered_frames == 30;
            }

            engine.update(input);
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
