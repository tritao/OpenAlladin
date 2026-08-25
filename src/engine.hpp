#pragma once

#include <SDL.h>

#include "animation.hpp"
#include "sprites.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace openaladdin {

struct CameraState {
    // WORLD_CAMERA_X/Y: the actual Genesis world origin. PLAYER_X/Y remain
    // local to this origin; their sum is the world position.
    int x = 0;
    int y = 464;

    // CAMERA_REFERENCE_X/Y and CAMERA_SCROLL_X/Y are the state used by the
    // original camera limit and tile-update path. Keeping them explicit is
    // important: camera x/y alone is not sufficient to resume a trace.
    int reference_x = 0;
    int reference_y = 464;
    int scroll_x = 0;
    int scroll_y = 0;

    // PLAYER_X/Y are followed toward these local thresholds. The ROM changes
    // them for movement, jump, and transition modes.
    int horizontal_threshold = 103;
    int vertical_threshold = 416;

    int level_width = 4800;
    int level_height = 720;
    // 0xFF7E0A..0xFF7E10 are startup/VDP alignment fields written by
    // Player_CameraAlign (0x001B0490), not live aliases of x/y.
    int pixel_x = 0;
    int pixel_y = 0;
    int tile_x = 0;
    int tile_y = 0;

    int update_delay = 0;
    int special_mode = 0;
    int scene_state = 1;
    bool scroll_left_pending = false;
    bool scroll_right_pending = false;
    bool scroll_up_pending = false;
    bool scroll_down_pending = false;
};

struct PlayerState {
    // These are the same local coordinates and 8.8 velocity fields recovered
    // from the Genesis RAM map (PLAYER_X/Y, PLAYER_VX/VY).
    int x = 0;
    int y = 0;
    std::int16_t vx = 0;
    std::int16_t vy = 0;
    // Explicit inertial ground state. The ROM keeps the brake stream active
    // while the release impulse decays; selector dispatch must use this state
    // rather than re-testing the numeric velocity at the animation boundary.
    bool ground_braking = false;
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
    // The ROM keeps two distinct fields here: FFF0A4 is the surface-mode
    // word toggled by handler 0x47, while FFF0C2 is that handler's one-shot
    // latch. Keeping them separate matters for terrain cells that are
    // revisited on consecutive frames.
    std::uint16_t terrain_surface_mode = 0;
    std::uint8_t terrain_surface_latch = 0;
    std::uint8_t terrain_stop_left_motion = 0;
    std::uint8_t terrain_stop_right_motion = 0;
    std::uint8_t terrain_stop_upward_motion = 0;
    std::uint8_t terrain_left_inner_probe = 0;
    std::uint8_t terrain_left_outer_probe = 0;
    std::uint8_t terrain_right_inner_probe = 0;
    std::uint8_t terrain_right_outer_probe = 0;
    std::uint8_t terrain_response_timer_state = 0;
    std::uint8_t terrain_query_state_a = 0;
    std::uint8_t terrain_query_state_b = 0;
    std::uint8_t terrain_state = 0;
    std::uint8_t terrain_response_latch = 0;
    std::uint8_t terrain_terminal_transition = 0;
    std::uint8_t attack_timer = 0;
};

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool jump_pressed = false;
    bool attack_pressed = false;
};

struct ActorState {
    std::uint8_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t facing_x_flip = 0;
    std::uint8_t facing_y_flip = 0;
    std::uint32_t movement_pc = 0;
    std::uint32_t movement_loop_pc = 0;
    std::uint8_t movement_loop_timer = 0;
    std::uint32_t frame_ptr = 0;
    std::uint32_t animation_pc = 0;
    std::uint32_t movement_return_pc = 0;
    std::uint8_t flags = 0;
    std::uint8_t terminal_timer = 0;
    std::uint8_t movement_command_timer = 0;
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
    };

    struct TerrainCollisionFlags {
        // FFF0C5/FFF0C8/FFF0CB from Player_TerrainCollisionProbe
        // (0x001AD632). These are separate from the resolver result: the
        // original probe uses the collision map's >=0xE0 criterion and runs
        // before Player_IntegrateMotion.
        bool stop_left = false;
        bool stop_right = false;
        bool stop_upward = false;
        bool left_inner = false;
        bool left_outer = false;
        bool right_inner = false;
        bool right_outer = false;
    };

    struct TerrainContour {
        bool valid = false;
        int row = -1;
        int column = -1;
        int target_world_y = 0;
        std::uint8_t floor_type = 0;
        std::uint8_t contour = 0;
    };

    void load(const std::string& asset_root, const std::string& rom_path = {});

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
    int camera_start_x() const { return camera_start_x_; }
    int camera_start_y() const { return camera_start_y_; }
    int camera_threshold_x() const { return camera_threshold_x_; }
    int camera_threshold_y() const { return camera_threshold_y_; }

    // This is the recovered FF9884/FFAE86 lookup in local, file-backed form.
    std::uint8_t terrain_behavior(int column, int row) const;

    // Exact fixed-ROM equivalent of Terrain_ResolvePlayerCell's address math:
    // the resolver selects one 16-pixel row band and one column, then applies
    // the terrain-word -> behavior-table lookup. No nearby-row search occurs.
    TerrainCell resolve_player_cell(int world_x, int world_y) const;

    // Canonical player probe used by the native terrain response.
    TerrainQuery query_player(int world_x, int world_y) const;

    // Exact fixed-ROM equivalent of Player_TerrainCollisionProbe at
    // 0x001AD632. This is not a rectangle scan and does not use behavior
    // handler dispatch. The landing-state byte controls whether the probe
    // performs its extra downward row check.
    TerrainCollisionFlags query_player_collision(
        int world_x,
        int world_y,
        std::uint8_t landing_state
    ) const;

    // Exact fixed-ROM equivalent of Player_FloorContour at 0x001AD7B4.
    // The routine checks the selected row and the next two rows, then uses
    // the ROM contour table to turn a floor type and X fraction into a
    // pixel-accurate target Y.
    TerrainContour query_player_contour(
        int world_x,
        int world_y,
        std::uint16_t surface_mode
    ) const;

private:
    int map_width_ = 300;
    int map_height_ = 45;
    // Level-01 fixed-ROM startup fields: camera (0, 464), local player
    // (103, 416). They are configuration only; Engine owns mutable runtime
    // camera state.
    int start_x_ = 103;
    int start_y_ = 416;
    int camera_start_x_ = 0;
    int camera_start_y_ = 464;
    int camera_threshold_x_ = 103;
    int camera_threshold_y_ = 416;
    int background_width_ = 0;
    int background_height_ = 0;
    std::vector<std::uint8_t> background_rgba_;
    std::vector<std::uint16_t> terrain_words_;
    std::vector<std::uint8_t> floor_data_;
    std::vector<std::uint8_t> contour_table_;
    std::vector<SDL_Color> palette_;
};

class Engine {
public:
    void load(
        const std::string& asset_root,
        const std::string& sprite_root = {},
        const std::string& rom_path = {},
        const std::string& actor_records_path = {},
        const std::string& actor_timeline_path = {}
    );
    void reset();
    void set_checkpoint(int x, int y, std::int16_t vx, std::int16_t vy, bool grounded);
    void set_checkpoint_frame_ptr(int address);
    void set_checkpoint_animation(std::uint32_t animation_pc, int timer);
    void set_checkpoint_camera(int x, int y, int reference_x, int reference_y, int scroll_x, int scroll_y, int scene_state);
    void update(const InputState& input);
    void render(SDL_Renderer* renderer);
    void write_state(std::ostream& output, const std::string& input_token) const;

    const PlayerState& player() const { return player_; }
    const CameraState& camera() const { return camera_; }
    int player_world_x() const { return camera_.x + player_.x; }
    int player_world_y() const { return camera_.y + player_.y; }
    int frame() const { return frame_; }
    bool grounded() const { return player_.grounded; }
    const std::array<ActorState, 32>& actors() const { return actors_; }

private:
    struct CollisionBox {
        bool valid = false;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    void integrate_motion();
    void update_terrain_input(const InputState& input);
    void apply_floor_contour();
    void resolve_terrain(int previous_world_y);
    void update_camera();
    void initialize_camera_alignment();
    bool rebase_camera_reference();
    void update_state08(const InputState& input);
    void apply_ground_movement(const InputState& input);
    void apply_terrain_behavior(const Level::TerrainCell& cell);
    void load_actor_records(const std::string& path);
    void load_actor_timeline(const std::string& path);
    void apply_actor_timeline(int frame);
    void update_actor_movement();
    void update_actor_interactions(const InputState& input, bool was_grounded);
    CollisionBox read_collision_box(
        std::uint32_t frame_pointer,
        int origin_x,
        int origin_y,
        bool facing_left
    ) const;
    SpritePose sprite_pose() const;
    int visual_x() const;
    int visual_y() const;

    Level level_;
    PlayerState player_;
    CameraState camera_;
    SpriteDatabase sprites_;
    PlayerAnimationVm animation_;
    std::array<ActorState, 32> actor_templates_{};
    std::array<ActorState, 32> actors_{};
    std::map<int, std::array<ActorState, 32>> actor_timeline_;
    std::vector<std::uint8_t> rom_bytes_;
    int frame_ = 0;
    int last_ground_direction_ = 0;
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
