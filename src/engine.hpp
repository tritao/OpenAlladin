#pragma once

#include <SDL.h>

#include "animation.hpp"
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

struct ActorState {
    std::uint8_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    // Actor +0x06 controls common motion integration. Bit 6 enables the
    // fixed-point vertical gravity step used by the scene-state-5 type 0x84
    // response, even when its movement stream cursor is null.
    std::uint8_t movement_flags = 0;
    // Actor +0x07 is a transient terrain/collision status byte. The ROM's
    // terrain pass clears bit 4 before testing the actor's floor cell, while
    // the movement VM's 0x88 command can branch on that bit.
    std::uint8_t runtime_field_07 = 0;
    // Native scheduler delay for the terrain pass's +0x07 bit publication;
    // the ROM exposes the bit only after the following movement boundary.
    std::uint8_t runtime_field_07_delay = 0;
    std::uint8_t facing_x_flip = 0;
    std::uint8_t facing_y_flip = 0;
    std::uint32_t movement_pc = 0;
    std::uint32_t movement_loop_pc = 0;
    std::uint8_t movement_loop_timer = 0;
    // The movement VM's actor-relative arithmetic command addresses the
    // 68000 actor record directly. These are the two signed words at +0x18
    // and +0x1A used by the recovered movement streams.
    std::int16_t movement_word_18 = 0;
    std::int16_t movement_word_1a = 0;
    std::uint32_t frame_ptr = 0;
    std::uint32_t animation_pc = 0;
    std::uint32_t movement_return_pc = 0;
    std::uint8_t flags = 0;
    // Actor +0x3D is a runtime callback/state byte consumed by proximity
    // animation streams and survives template reuse of a slot.
    std::uint8_t interaction_state = 0;
    std::uint8_t terminal_timer = 0;
    std::uint8_t movement_command_timer = 0;
    std::uint8_t animation_timer = 0;
    // Some actor producers install a record after the current animation pass;
    // this defers its first shared animation service by one VBlank sample.
    std::uint8_t animation_defer_ticks = 0;
    // Type-0x2D terrain conversion publishes a type-0x84 record on the
    // current pass but the ROM services that new record on the next VBlank
    // even when the common FF7E28 gate is low.
    bool animation_force_next_tick = false;
    // The live sword animation stream is serviced on its first two actor
    // ticks, then on alternating ticks. This is native scheduler state, not
    // a field in the 0x42-byte Genesis actor record.
    std::uint8_t animation_tick_phase = 0;
    // Compact template byte +0x10 copied by Actor_InitializeFromTemplate.
    std::uint8_t resource_count = 0;
    // Interaction sources retain their resource offset so culling/cleanup can
    // remain separate from the one-shot map selector state.
    std::uint16_t interaction_resource_offset = 0;
    std::uint8_t interaction_selector = 0;
    bool spawned_by_interaction = false;
    // F5-created actors use the common animation scheduler immediately. This
    // distinguishes them from the scene-state-5 type-0x84 terminal record,
    // which has its own phase gate and deferred first tick.
    bool spawned_by_animation = false;
    // The physical A-button apple uses the same type-0x80 family as the
    // sword, but its F5 producer publishes the first animation frame in the
    // same boundary as allocation. Keep that cadence distinct from the
    // deferred sword child path.
    bool spawned_by_apple = false;
    // Modes 5/6 of AnimationVM_SpawnOrCopyActor link the new record to the
    // actor that issued the command. This is host-side metadata for the
    // parent flag edge; the 68000 pointer itself is not part of the trace.
    int linked_actor_slot = -1;
};

enum class ActorAllocationPool {
    CommonForward,       // slots 3..22, FUN_001AE262
    CommonReverse,       // slots 20..1, FUN_001AE2AA
    GameplayForward,     // slots 1..24, FUN_001AE27A
    GameplayReverse,     // slots 24..1, FUN_001AE292
};

struct SpawnDescriptor {
    bool valid = false;
    std::uint8_t selector = 0;
    std::uint32_t template_address = 0;
    ActorAllocationPool allocation_pool = ActorAllocationPool::CommonForward;
    int post_offset_x = 0;
    int post_offset_y = 0;
    bool override_type = false;
    std::uint8_t type = 0;
    bool override_animation = false;
    std::uint32_t animation_pc = 0;
    bool override_movement = false;
    std::uint32_t movement_pc = 0;
    bool override_resource_count = false;
    std::uint8_t resource_count = 0;
};

class Level {
public:
    struct InteractionRecord {
        int column = -1;
        int row = -1;
        std::uint16_t terrain_word = 0;
        std::uint16_t resource_offset = 0;
        std::uint8_t selector = 0;
        int world_x = 0;
        int world_y = 0;
    };

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
    int parallax_width() const { return parallax_width_; }
    int parallax_height() const { return parallax_height_; }
    const std::vector<std::uint8_t>& parallax_rgba() const { return parallax_rgba_; }
    const std::vector<std::uint16_t>& terrain_words() const { return terrain_words_; }
    const std::vector<std::uint8_t>& floor_data() const { return floor_data_; }
    const std::vector<InteractionRecord>& interaction_records() const {
        return interaction_records_;
    }
    const std::vector<SDL_Color>& palette() const { return palette_; }
    bool is_vdp_transparent(std::uint8_t red, std::uint8_t green, std::uint8_t blue) const;
    int map_width() const { return map_width_; }
    int map_height() const { return map_height_; }
    int start_x() const { return start_x_; }
    int start_y() const { return start_y_; }
    int camera_start_x() const { return camera_start_x_; }
    int camera_start_y() const { return camera_start_y_; }
    int camera_threshold_x() const { return camera_threshold_x_; }
    int camera_threshold_y() const { return camera_threshold_y_; }
    int scene_state() const { return scene_state_; }

    // This is the recovered FF9884/FFAE86 lookup in local, file-backed form.
    std::uint8_t terrain_behavior(int column, int row) const;

    // Resource offset 3 is the Level-01 interaction selector table. It is
    // indexed by the same terrain-word resource offset used by the ROM's
    // row scanner: floor[3 + (map_word >> 1)].
    std::uint8_t interaction_selector(int column, int row) const;

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
    // These are configuration only; Engine owns mutable runtime camera state.
    int start_x_ = 103;
    int start_y_ = 416;
    int camera_start_x_ = 0;
    int camera_start_y_ = 464;
    int camera_threshold_x_ = 103;
    int camera_threshold_y_ = 416;
    int scene_state_ = 1;
    int background_width_ = 0;
    int background_height_ = 0;
    std::vector<std::uint8_t> background_rgba_;
    int parallax_width_ = 0;
    int parallax_height_ = 0;
    std::vector<std::uint8_t> parallax_rgba_;
    std::vector<std::uint16_t> terrain_words_;
    std::vector<std::uint8_t> floor_data_;
    std::vector<InteractionRecord> interaction_records_;
    std::vector<std::uint8_t> contour_table_;
    std::vector<SDL_Color> palette_;
};

// The Level-01 interaction table is mutable at runtime. The original clears
// the selector at its resource offset after a successful allocation, which
// makes the map a one-shot source while still allowing failed allocations to
// be retried by a later camera refill pass.
class InteractionMap {
public:
    void load(const Level& level);
    void reset();

    std::uint8_t selector(int column, int row) const;
    std::uint8_t selector(const Level::InteractionRecord& record) const;
    bool consume(std::uint16_t resource_offset);
    const std::vector<Level::InteractionRecord>& records() const { return records_; }
    std::size_t active_record_count() const;

private:
    std::vector<Level::InteractionRecord> records_;
    std::vector<std::uint8_t> selectors_;
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
    void update_terrain_connector_response();
    void apply_floor_contour();
    void resolve_terrain(
        int previous_world_y,
        int preprocessed_surface_row = -1,
        int preprocessed_surface_column = -1
    );
    void update_camera(bool suppress_vertical_follow = false);
    void initialize_camera_alignment();
    bool rebase_camera_reference();
    void update_state08(const InputState& input);
    void apply_ground_movement(const InputState& input);
    void apply_terrain_behavior(const Level::TerrainCell& cell);
    void load_actor_records(const std::string& path);
    void load_actor_timeline(const std::string& path);
    void apply_actor_timeline(int frame);
    void update_actor_movement();
    void update_probe_actor_animation_before_movement();
    void update_terminal_actor_motion(ActorState& actor);
    void update_actor_animations(std::optional<std::size_t> only_slot = std::nullopt);
    std::vector<std::size_t> apply_animation_spawns(bool defer_player_spawns = false);
    std::optional<std::size_t> apply_animation_spawn_request(const AnimationSpawnRequest& request);
    void scan_interaction_refill_window();
    void dispatch_interaction(const Level::InteractionRecord& record, int base_x, int base_y);
    std::optional<SpawnDescriptor> spawn_descriptor(std::uint8_t selector) const;
    std::optional<std::size_t> allocate_actor_slot(ActorAllocationPool pool) const;
    ActorState actor_from_template(std::uint32_t template_address) const;
    void update_dynamic_actor_culling();
    void sync_player_actor();
    void update_actor_actor_collisions(bool pre_motion = false);
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
    PlayerState player_;
    CameraState camera_;
    SpriteDatabase sprites_;
    PlayerAnimationVm animation_;
    std::array<PlayerAnimationVm, 32> actor_animations_{};
    // Controlled movement probes share a VBlank with the common actor
    // animation pass. The ROM services their animation cursor before
    // movement; these markers prevent the later actor pass from ticking the
    // same slot twice.
    std::array<bool, 32> probe_actor_animation_preupdated_{};
    // Actor records materialized from a deferred apple F5 are serviced once
    // before movement on their first live boundary. Suppress the later
    // table walk from ticking that same record twice.
    std::array<bool, 32> animation_preupdated_this_frame_{};
    // The apple projectile is serviced once at allocation, then exposes one
    // extra held animation boundary before joining the common type-0x80
    // cadence.
    std::array<bool, 32> apple_actor_hold_next_frame_{};
    bool apple_root_republish_pending_ = false;
    bool apple_cursor_hold_1223fa_done_ = false;
    bool apple_cursor_hold_122438_done_ = false;
    bool apple_following_tick_deferred_ = false;
    std::array<bool, 32> probe_actor_animation_active_{};
    InteractionMap interaction_map_;
    std::array<ActorState, 32> actor_templates_{};
    std::array<ActorState, 32> actors_{};
    std::map<int, std::array<ActorState, 32>> actor_timeline_;
    bool actor_snapshot_mode_ = false;
    bool interaction_scan_initialized_ = false;
    bool interaction_selector_pending_ = false;
    // Slots retired by the movement VM remain unavailable to refill scans
    // until the next game-loop boundary, matching the ROM's refill-before-cull
    // allocator snapshot even when native camera reconstruction runs later.
    std::array<bool, 32> actor_slots_culled_this_frame_{};
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
    std::optional<AnimationSpawnRequest> deferred_animation_spawn_;
    // A vertical camera-reference rebase consumes this frame's follow pass;
    // the ROM services the deferred follow twice on the next frame.
    bool camera_follow_catch_up_ = false;
    // The same camera boundary suppresses the current player animation pass;
    // queue the next VM service explicitly at the following boundary.
    bool player_animation_catch_up_ = false;
    // A horizontal camera rebase can share a frame with an actor animation
    // command boundary. The ROM's camera path then reaches the common actor
    // animation loop once more on the following frame.
    bool actor_animation_catch_up_ = false;
    // Common F5 actors share one AnimationVM_TickActors traversal. Keep its
    // cadence at engine scope so actors spawned later join the same service
    // and hold passes as the already-live records.
    bool checkpoint_terrain_behavior_override_ = false;
    std::uint8_t checkpoint_terrain_behavior_ = 0;
    std::vector<std::uint8_t> rom_bytes_;
    struct VdpCheckpoint {
        bool loaded = false;
        std::vector<std::uint8_t> vram;
        std::vector<std::uint8_t> vsram;
        std::vector<SDL_Color> palette;
        std::array<std::uint8_t, 32> registers{};
    } vdp_checkpoint_;
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
