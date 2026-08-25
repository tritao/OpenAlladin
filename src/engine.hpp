#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openaladdin {

struct PlayerState {
    // These are the same local coordinates and 8.8 velocity fields recovered
    // from the Genesis RAM map (PLAYER_X/Y, PLAYER_VX/VY).
    int x = 0;
    int y = 0;
    std::int16_t vx = 0;
    std::int16_t vy = 0;
    bool grounded = false;
    std::uint8_t terrain_behavior = 0;
};

struct InputState {
    bool left = false;
    bool right = false;
    bool jump_pressed = false;
};

class Level {
public:
    void load(const std::string& asset_root);

    int background_width() const { return background_width_; }
    int background_height() const { return background_height_; }
    const std::vector<std::uint8_t>& background_rgba() const { return background_rgba_; }
    const std::vector<std::uint16_t>& terrain_words() const { return terrain_words_; }
    const std::vector<std::uint8_t>& floor_data() const { return floor_data_; }
    const std::vector<SDL_Color>& palette() const { return palette_; }
    int map_width() const { return map_width_; }
    int map_height() const { return map_height_; }
    int start_x() const { return start_x_; }
    int start_y() const { return start_y_; }
    int camera_x() const { return camera_x_; }
    int camera_y() const { return camera_y_; }

    // This is the recovered FF9884/FFAE86 lookup in local, file-backed form.
    std::uint8_t terrain_behavior(int column, int row) const;

private:
    int map_width_ = 300;
    int map_height_ = 45;
    // Level-01 runtime state: local player (103, 416), camera (0, 464).
    // These are the values observed in the loaded MAME state; the extracted
    // level-table fields use the opposite-looking start/offset naming.
    int start_x_ = 103;
    int start_y_ = 416;
    int camera_x_ = 0;
    int camera_y_ = 464;
    int background_width_ = 0;
    int background_height_ = 0;
    std::vector<std::uint8_t> background_rgba_;
    std::vector<std::uint16_t> terrain_words_;
    std::vector<std::uint8_t> floor_data_;
    std::vector<SDL_Color> palette_;
};

class Engine {
public:
    void load(const std::string& asset_root);
    void reset();
    void update(const InputState& input);
    void render(SDL_Renderer* renderer);

    const PlayerState& player() const { return player_; }
    int frame() const { return frame_; }
    bool grounded() const { return player_.grounded; }

private:
    void integrate_motion();
    void resolve_terrain();
    bool horizontal_blocked(int direction) const;
    void apply_ground_movement(const InputState& input);
    void apply_terrain_behavior(std::uint8_t behavior, int surface_y);
    int visual_x() const;
    int visual_y() const;
    int support_row(int visual_y, int visual_x) const;

    Level level_;
    PlayerState player_;
    int frame_ = 0;
    bool quit_ = false;
    std::vector<std::uint32_t> framebuffer_;
    SDL_Texture* texture_ = nullptr;
    int camera_render_x_ = 0;
    int camera_render_y_ = 0;

public:
    bool quit_requested() const { return quit_; }
    void request_quit() { quit_ = true; }
};

}  // namespace openaladdin
