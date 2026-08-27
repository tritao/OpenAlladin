#pragma once

#include <SDL.h>

#include "animation.hpp"
#include "actor.hpp"
#include "level.hpp"
#include "movement.hpp"
#include "scene.hpp"
#include "sprites.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
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
    // SCENE_VDP_UPDATE_FLAG gates fixed event dispatch from the player
    // interaction selector.
    int vdp_update = 1;
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
    // FFF0BF counts the initial active-response jump phase. The ROM applies
    // an extra -0x6C vertical impulse until this counter reaches ten.
    std::uint8_t terrain_jump_response_counter = 0;
    std::uint8_t terrain_transition_countdown = 0;
    std::uint8_t terrain_query_state_a = 0;
    std::uint8_t terrain_query_state_b = 0;
    std::uint8_t terrain_state = 0;
    std::uint8_t terrain_response_latch = 0;
    // FFF0D0 is asserted by the connector prepass while a vertical query is
    // actively moving the player. It is separate from TERRAIN_QUERY_STATE_A:
    // the resolver clears the latter every pass, while the movement response
    // publishes this gate for the following state-machine calls.
    std::uint8_t terrain_transition_gate = 0;
    std::uint8_t terrain_terminal_transition = 0;
    // Selector bytes that are not yet folded into the native terrain model
    // still travel with the checkpoint. This keeps response/action stream
    // selection reproducible instead of reconstructing it from a pose.
    AnimationSelectorState animation_selector{};
    std::uint8_t attack_timer = 0;
};

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool jump_pressed = false;
    bool attack_pressed = false;
    // Explicit apple/throw action.  The legacy attack_pressed field remains
    // the sword path used by existing fixtures; the ROM's A-button stream
    // is exposed separately so physical controller replays can preserve the
    // distinct action selection.
    bool apple_pressed = false;
};

struct SchedulerPhase {
    std::string name;
    std::uint32_t rom_entry_pc = 0;
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
    void set_checkpoint_terrain_behavior(std::uint8_t behavior);
    void set_checkpoint_terrain_landing_state(std::uint8_t landing_state);
    void set_checkpoint_frame_ptr(int address);
    void set_checkpoint_animation(std::uint32_t animation_pc, int timer);
    void set_checkpoint_animation_phase_delay(int ticks);
    void set_checkpoint_animation_selector(const AnimationSelectorState& selector);
    void set_checkpoint_facing_x_flip(bool facing_x_flip);
    void set_checkpoint_vdp(const std::string& trace_dir, int frame);
    void set_checkpoint_camera(
        int x,
        int y,
        int reference_x,
        int reference_y,
        int scroll_x,
        int scroll_y,
        int scene_state,
        int horizontal_threshold = -1,
        int vertical_threshold = -1,
        int update_delay = -1
    );
    void update(const InputState& input);
    // Return sound IDs emitted by the player and actor animation VMs since
    // the previous call. The caller submits them to the recovered Z80 driver.
    std::vector<std::uint8_t> take_sound_requests();
    void render(SDL_Renderer* renderer);
    // Write the last rendered native framebuffer without SDL window scaling.
    // This is the format used by the visual differential audit tools.
    void write_framebuffer_ppm(const std::string& path) const;
    void write_state(std::ostream& output, const std::string& input_token) const;
    void set_scheduler_trace_enabled(bool enabled);
    void write_scheduler_trace(std::ostream& output, const std::string& input_token) const;
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

    const PlayerState& player() const { return player_; }
    const CameraState& camera() const { return camera_; }
    int player_world_x() const { return camera_.x + player_.x; }
    int player_world_y() const { return camera_.y + player_.y; }
    int frame() const { return frame_; }
    bool grounded() const { return player_.grounded; }
    const std::array<ActorState, 32>& actors() const { return actors_; }
    const LevelDescriptor& level_descriptor() const { return level_.descriptor(); }

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
    void update_terrain_connector_response();
    void apply_floor_contour();
    void resolve_terrain(int previous_world_y);
    void update_camera(bool suppress_vertical_follow = false);
    void initialize_camera_alignment();
    bool rebase_camera_reference();
    void apply_ground_movement(const InputState& input);
    void apply_terrain_behavior(const Level::TerrainCell& cell);
    void load_actor_records(const std::string& path);
    void load_actor_timeline(const std::string& path);
    void apply_actor_timeline(int frame);
    void update_actor_movement();
    void update_terminal_actor_motion(ActorState& actor);
    void update_actor_animations();
    // ROM ordinal 30 (0x001A8CCE -> 0x001AC784) is the single common
    // animation service. It owns the player VM, player-originated F5
    // allocation, and one gated actor-table traversal.
    void update_animation_vm_ordinal_30(
        SpritePose desired_pose,
        HorizontalDirection horizontal_direction,
        const AnimationContext& context,
        bool response_dynamic_handoff,
        bool bounce_response_finished
    );
    std::vector<std::size_t> apply_animation_spawns(
        bool defer_player_spawns = false,
        bool defer_mode3_spawns = false
    );
    std::optional<std::size_t> apply_animation_spawn_request(const AnimationSpawnRequest& request);
    void scan_interaction_refill_window();
    void dispatch_interaction(const Level::InteractionRecord& record, int base_x, int base_y);
    std::optional<SpawnDescriptor> spawn_descriptor(std::uint8_t selector) const;
    std::optional<std::size_t> allocate_actor_slot(ActorAllocationPool pool) const;
    ActorState actor_from_template(std::uint32_t template_address) const;
    void update_dynamic_actor_culling();
    // FUN_001A8E0C is a real RAM publication, not a cached convenience. The
    // ROM uses it at four distinct causal boundaries in the frame loop.
    void publish_player_world_coordinates();
    void sync_player_actor();
    void update_actor_actor_collisions();
    void record_scheduler_phase(const char* name, std::uint32_t rom_entry_pc = 0);
    void collect_scheduler_writer_pcs();
    void render_vdp_checkpoint();
    void update_actor_interactions(const InputState& input, bool was_grounded);
    AnimationContext player_animation_context(bool grounded) const;
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
    SceneSystem scene_;
    PlayerState player_;
    CameraState camera_;
    SpriteDatabase sprites_;
    PlayerAnimationVm animation_;
    MovementVm movement_vm_;
    std::array<PlayerAnimationVm, 32> actor_animations_{};
    InteractionMap interaction_map_;
    ActorSystem actors_{};
    std::map<int, ActorSystem::Table> actor_timeline_;
    bool interaction_scan_initialized_ = false;
    bool interaction_selector_pending_ = false;
    // Slots retired by the movement VM remain unavailable to refill scans
    // until the next game-loop boundary, matching the ROM's refill-before-cull
    // allocator snapshot even when native camera reconstruction runs later.
    // Type-0x1F actor flags are written after the player's ground step.  The
    // selector lock/camera delay become visible on the following VBlank.
    bool interaction_actor_lock_pending_ = false;
    bool interaction_camera_delay_pending_ = false;
    bool interaction_actor_triggered_ = false;
    // Actor_PlayerCollisionPass invokes the type-0x2D handler on the overlap
    // boundary; the player stream root is published after the common VM pass.
    bool player_collision_interaction_pending_ = false;
    bool checkpoint_animation_selector_pending_ = false;
    bool surface_interaction_pending_ = false;
    bool surface_interaction_active_ = false;
    // The ROM leaves the landing latch visible for the launch boundary,
    // clears it on the first airborne pass, then re-arms it when the jump
    // state enters its falling phase. Keep those two boundaries explicit.
    bool jump_landing_state_arm_pending_ = false;
    bool jump_landing_state_arm_now_ = false;
    bool terrain_fall_phase_ = false;
    // The bounce-pad response leaves the ground-response latch armed after
    // FFF0BE clears, while the player resumes the run stream.
    bool bounce_response_active_ = false;
    bool bounce_response_follow_active_ = false;
    bool bounce_camera_delay_hold_pending_ = false;
    // A sloped contour exposes a non-flat landing byte (and therefore a
    // public grounded=false) while the player remains on the surface. Keep
    // the internal ground-motion path latched across those contour samples.
    bool contour_ground_motion_ = false;
    int interaction_reference_x_ = 0;
    int interaction_reference_y_ = 0;
    // FUN_001B3032 is the shared fixed-ROM PRNG used by terrain responses and
    // animation F0 branches. Keep its state in one place so VM consumers do
    // not silently diverge from the Genesis sequence.
    std::uint32_t random_state_ = 0;
    int terrain_input_world_x_ = 0;
    int terrain_input_world_y_ = 0;
    // Common F5 actors share one AnimationVM_TickActors traversal. Keep its
    // cadence at engine scope so actors spawned later join the same service
    // and hold passes as the already-live records.
    bool checkpoint_terrain_behavior_override_ = false;
    std::uint8_t checkpoint_terrain_behavior_ = 0;
    std::vector<std::uint8_t> rom_bytes_;
    bool scheduler_trace_enabled_ = false;
    std::vector<SchedulerPhase> scheduler_phases_;
    std::vector<std::uint32_t> scheduler_writer_pcs_;
    struct VdpCheckpoint {
        bool loaded = false;
        std::vector<std::uint8_t> vram;
        std::vector<std::uint8_t> vsram;
        std::vector<SDL_Color> palette;
        std::array<std::uint8_t, 32> registers{};
    } vdp_checkpoint_;
    int frame_ = 0;
    // FF7E28 is incremented at Game_FrameUpdateLoop entry. Keep the ROM
    // phase separately from the host frame label so scene/checkpoint
    // boundaries do not silently turn into scheduler gates.
    std::uint8_t frame_phase_ = 0;
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
