#pragma once

#include <SDL.h>

#include <cstdint>
#include <iosfwd>
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
    std::uint8_t terrain_query_result = 0x7F;
    std::uint8_t terrain_push_right = 0;
    std::uint8_t terrain_push_left = 0;
    std::uint8_t terrain_push_up = 0;
    std::uint8_t terrain_push_down = 0;
    std::int16_t terrain_horizontal_response = 0;
    std::uint8_t terrain_response_active = 0;
    std::uint8_t terrain_vertical_stop = 0;
    std::uint8_t terrain_landing_state = 0;
    std::uint8_t terrain_surface_mode = 0;
    std::uint8_t terrain_stop_left_motion = 0;
    std::uint8_t terrain_stop_right_motion = 0;
    std::uint8_t terrain_stop_upward_motion = 0;
    std::uint8_t terrain_response_timer_state = 0;
    std::uint8_t terrain_query_state_a = 0;
    std::uint8_t terrain_query_state_b = 0;
    std::uint8_t terrain_state = 0;
    std::uint8_t terrain_response_latch = 0;
};

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool jump_pressed = false;
};

class Level {
public:
    struct TerrainCell {
        bool valid = false;
        int column = -1;
        int row = -1;
        std::uint16_t terrain_word = 0;
        std::uint8_t behavior = 0;
        std::uint32_t handler = 0;
    };

    struct TerrainQuery {
        TerrainCell resolver;
        TerrainCell left;
        TerrainCell right;
        TerrainCell up;
        TerrainCell down;

        bool has_resolver_handler() const {
            return resolver.valid && resolver.behavior != 0
                && (resolver.handler != 0x001B65BE || resolver.behavior == 0x11);
        }
        bool side_blocks_left() const {
            return left.valid && left.behavior != 0
                && (left.handler != 0x001B65BE || left.behavior == 0x11);
        }
        bool side_blocks_right() const {
            return right.valid && right.behavior != 0
                && (right.handler != 0x001B65BE || right.behavior == 0x11);
        }
    };

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

    // Exact fixed-ROM equivalent of Terrain_ResolvePlayerCell's address math:
    // the resolver selects one 16-pixel row band and one column, then applies
    // the terrain-word -> behavior-table lookup. No nearby-row search occurs.
    TerrainCell resolve_player_cell(int world_x, int world_y) const;

    // Canonical player probes used by the native terrain response. The
    // resolver probe is the original one; side probes are single-cell probes
    // at the player collision edges rather than a rectangle scan.
    TerrainQuery query_player(int world_x, int world_y) const;

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
    void set_checkpoint(int x, int y, std::int16_t vx, std::int16_t vy, bool grounded);
    void update(const InputState& input);
    void render(SDL_Renderer* renderer);
    void write_state(std::ostream& output, const std::string& input_token) const;

    const PlayerState& player() const { return player_; }
    int frame() const { return frame_; }
    bool grounded() const { return player_.grounded; }

private:
    void integrate_motion();
    void update_terrain_input(const InputState& input);
    void resolve_terrain();
    bool terrain_side_blocked(int direction) const;
    void apply_ground_movement(const InputState& input);
    void apply_terrain_behavior(const Level::TerrainCell& cell);
    int visual_x() const;
    int visual_y() const;

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
