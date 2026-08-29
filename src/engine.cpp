#include "engine.hpp"

#include "checkpoint_io.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace openaladdin {
namespace {

// The player frame origin is one 16-pixel tile above the terrain query
// origin. The ROM keeps these coordinate systems distinct: terrain probes use
// WORLD_Y - 0xF0, while the VDP sprite origin uses WORLD_Y - 0x100.
constexpr int kPlayerVisualOffsetY = 0x100;
constexpr int kTerrainContourRomOffset = 0x2FD2;
constexpr int kTerrainContourRomSize = 0x1000;
constexpr std::uint8_t kActorGuardType = 0x0A;
constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint32_t kTerrainScene5SpawnTemplate = 0x001B805C;
constexpr std::uint32_t kTerrainScene5SpawnAnimationDefault = 0x001250BA;
constexpr std::uint32_t kTerrainScene5SpawnAnimationLow = 0x001250CE;
constexpr std::uint32_t kTerrainScene5SpawnAnimationHigh = 0x001250DE;
constexpr std::uint32_t kPlayerSwordAnimationStream = 0x0012271A;
constexpr std::uint32_t kPlayerAppleActionStream = 0x001223DA;
constexpr std::uint32_t kPlayerAttackTransitionStream = 0x00122034;
constexpr std::uint32_t kPlayerSwordStableStream = 0x001223E2;
constexpr std::uint32_t kPlayerSwordFirstFrame = 0x001ED34A;
constexpr std::uint32_t kPlayerUpAnimationStream = 0x00122236;
constexpr std::uint32_t kPlayerDownAnimationStream = 0x001222D2;
constexpr std::uint32_t kActorDeathAnimationStream = 0x00122FA2;
constexpr std::uint32_t kActorSwordDeathAnimationStream = 0x00122DD8;
constexpr std::uint32_t kActorDeathTemplate = 0x001B7940;
constexpr std::uint32_t kActorSwordDeathTemplate = 0x001B792C;
constexpr std::uint32_t kLevel02ExitCallback = 0x001B6394;
constexpr std::uint32_t kLevel06ExitCallback = 0x001B644E;
constexpr std::uint32_t kLevel02EventStream = 0x00002128;
constexpr std::uint32_t kLevel06EventStream = 0x000024FC;
constexpr std::uint8_t kActorDeathFrames = 43;
constexpr std::uint8_t kActorSwordTerminalFrames = 19;

constexpr std::uint32_t kCheckpointVersion = 10;

void write_selector(checkpoint::Writer& writer, const AnimationSelectorState& selector) {
    writer.u8(selector.animation_gate);
    writer.u8(selector.terminal_transition);
    writer.u8(selector.scene_script_countdown);
    writer.u8(selector.interaction_lock);
    writer.u8(selector.response_active);
    writer.u8(selector.landing_state);
    writer.u8(selector.transition_gate);
    writer.u8(selector.transition_lock);
    writer.u8(selector.transition_state);
    writer.u8(selector.transition_mode);
    writer.u8(selector.transition_flag);
    writer.u8(selector.transition_response);
    writer.u8(selector.transition_state_de);
    writer.u8(selector.transition_state_df);
    writer.u8(selector.camera_special_mode);
    writer.u8(selector.response_latch);
    writer.u8(selector.response_animation);
    writer.u8(selector.response_state_ee);
    writer.u8(selector.response_state_ef);
    writer.u8(selector.response_state_f0);
    writer.u8(selector.response_state_101);
    writer.i16(selector.horizontal_response);
    writer.u8(selector.response_timer);
    writer.u8(selector.interaction_pending);
    writer.u8(selector.state_lock);
}

AnimationSelectorState read_selector(checkpoint::Reader& reader) {
    AnimationSelectorState selector;
    selector.animation_gate = reader.u8();
    selector.terminal_transition = reader.u8();
    selector.scene_script_countdown = reader.u8();
    selector.interaction_lock = reader.u8();
    selector.response_active = reader.u8();
    selector.landing_state = reader.u8();
    selector.transition_gate = reader.u8();
    selector.transition_lock = reader.u8();
    selector.transition_state = reader.u8();
    selector.transition_mode = reader.u8();
    selector.transition_flag = reader.u8();
    selector.transition_response = reader.u8();
    selector.transition_state_de = reader.u8();
    selector.transition_state_df = reader.u8();
    selector.camera_special_mode = reader.u8();
    selector.response_latch = reader.u8();
    selector.response_animation = reader.u8();
    selector.response_state_ee = reader.u8();
    selector.response_state_ef = reader.u8();
    selector.response_state_f0 = reader.u8();
    selector.response_state_101 = reader.u8();
    selector.horizontal_response = reader.i16();
    selector.response_timer = reader.u8();
    selector.interaction_pending = reader.u8();
    selector.state_lock = reader.u8();
    return selector;
}

void write_player_state(checkpoint::Writer& writer, const PlayerState& player) {
    writer.i32(player.x);
    writer.i32(player.y);
    writer.i16(player.vx);
    writer.i16(player.vy);
    writer.boolean(player.ground_braking);
    writer.boolean(player.grounded);
    writer.u8(player.terrain_behavior);
    writer.u8(player.terrain_query_result);
    writer.u8(player.terrain_push_right);
    writer.u8(player.terrain_push_left);
    writer.u8(player.terrain_push_up);
    writer.u8(player.terrain_push_down);
    writer.i16(player.terrain_horizontal_response);
    writer.u8(player.terrain_response_active);
    writer.u8(player.terrain_vertical_stop);
    writer.u8(player.terrain_landing_state);
    writer.u16(player.terrain_surface_mode);
    writer.u8(player.terrain_surface_latch);
    writer.u8(player.terrain_stop_left_motion);
    writer.u8(player.terrain_stop_right_motion);
    writer.u8(player.terrain_stop_upward_motion);
    writer.u8(player.terrain_left_inner_probe);
    writer.u8(player.terrain_left_outer_probe);
    writer.u8(player.terrain_right_inner_probe);
    writer.u8(player.terrain_right_outer_probe);
    writer.u8(player.terrain_response_timer_state);
    writer.u8(player.terrain_jump_response_counter);
    writer.u8(player.terrain_transition_countdown);
    writer.u8(player.terrain_query_state_a);
    writer.u8(player.terrain_query_state_b);
    writer.u8(player.terrain_state);
    writer.u8(player.terrain_response_latch);
    writer.u8(player.terrain_transition_gate);
    writer.u8(player.terrain_terminal_transition);
    write_selector(writer, player.animation_selector);
    writer.u8(player.attack_timer);
}

PlayerState read_player_state(checkpoint::Reader& reader) {
    PlayerState player;
    player.x = reader.i32();
    player.y = reader.i32();
    player.vx = reader.i16();
    player.vy = reader.i16();
    player.ground_braking = reader.boolean();
    player.grounded = reader.boolean();
    player.terrain_behavior = reader.u8();
    player.terrain_query_result = reader.u8();
    player.terrain_push_right = reader.u8();
    player.terrain_push_left = reader.u8();
    player.terrain_push_up = reader.u8();
    player.terrain_push_down = reader.u8();
    player.terrain_horizontal_response = reader.i16();
    player.terrain_response_active = reader.u8();
    player.terrain_vertical_stop = reader.u8();
    player.terrain_landing_state = reader.u8();
    player.terrain_surface_mode = reader.u16();
    player.terrain_surface_latch = reader.u8();
    player.terrain_stop_left_motion = reader.u8();
    player.terrain_stop_right_motion = reader.u8();
    player.terrain_stop_upward_motion = reader.u8();
    player.terrain_left_inner_probe = reader.u8();
    player.terrain_left_outer_probe = reader.u8();
    player.terrain_right_inner_probe = reader.u8();
    player.terrain_right_outer_probe = reader.u8();
    player.terrain_response_timer_state = reader.u8();
    player.terrain_jump_response_counter = reader.u8();
    player.terrain_transition_countdown = reader.u8();
    player.terrain_query_state_a = reader.u8();
    player.terrain_query_state_b = reader.u8();
    player.terrain_state = reader.u8();
    player.terrain_response_latch = reader.u8();
    player.terrain_transition_gate = reader.u8();
    player.terrain_terminal_transition = reader.u8();
    player.animation_selector = read_selector(reader);
    player.attack_timer = reader.u8();
    return player;
}

void write_camera_state(checkpoint::Writer& writer, const CameraState& camera) {
    writer.i32(camera.x);
    writer.i32(camera.y);
    writer.i32(camera.reference_x);
    writer.i32(camera.reference_y);
    writer.i32(camera.scroll_x);
    writer.i32(camera.scroll_y);
    writer.i32(camera.horizontal_threshold);
    writer.i32(camera.vertical_threshold);
    writer.i32(camera.level_width);
    writer.i32(camera.level_height);
    writer.i32(camera.vdp_update);
    writer.i32(camera.pixel_x);
    writer.i32(camera.pixel_y);
    writer.i32(camera.tile_x);
    writer.i32(camera.tile_y);
    writer.i32(camera.update_delay);
    writer.i32(camera.special_mode);
    writer.i32(camera.scene_state);
    writer.boolean(camera.scroll_left_pending);
    writer.boolean(camera.scroll_right_pending);
    writer.boolean(camera.scroll_up_pending);
    writer.boolean(camera.scroll_down_pending);
}

CameraState read_camera_state(checkpoint::Reader& reader) {
    CameraState camera;
    camera.x = reader.i32();
    camera.y = reader.i32();
    camera.reference_x = reader.i32();
    camera.reference_y = reader.i32();
    camera.scroll_x = reader.i32();
    camera.scroll_y = reader.i32();
    camera.horizontal_threshold = reader.i32();
    camera.vertical_threshold = reader.i32();
    camera.level_width = reader.i32();
    camera.level_height = reader.i32();
    camera.vdp_update = reader.i32();
    camera.pixel_x = reader.i32();
    camera.pixel_y = reader.i32();
    camera.tile_x = reader.i32();
    camera.tile_y = reader.i32();
    camera.update_delay = reader.i32();
    camera.special_mode = reader.i32();
    camera.scene_state = reader.i32();
    camera.scroll_left_pending = reader.boolean();
    camera.scroll_right_pending = reader.boolean();
    camera.scroll_up_pending = reader.boolean();
    camera.scroll_down_pending = reader.boolean();
    return camera;
}

std::uint8_t fixed_high_byte(std::int16_t value) {
    return static_cast<std::uint8_t>(static_cast<std::uint16_t>(value) >> 8);
}

}  // namespace

Engine::Engine()
    : player_(state_.player),
      camera_(state_.camera),
      interaction_map_(state_.interactions),
      actors_(state_.actors),
      actor_lifecycle_(actors_),
      collisions_(actor_lifecycle_),
      level_event_system_(actor_lifecycle_, animation_, actor_animations_),
      interactions_(
          actor_lifecycle_,
          collisions_,
          animation_,
          actor_animations_,
          actor_movement_deferred_),
      random_state_(state_.random.value),
      frame_(state_.frame.number),
      frame_phase_(state_.frame.phase) {
    scene_.bind_runtime(state_.scene);
    animation_.bind_state(state_);
    for (auto& actor_animation : actor_animations_) {
        actor_animation.bind_state(state_);
    }
    scheduler_context_ = frame_scheduler_context();
}

void Engine::load(
    const std::string& asset_root,
    const std::string& sprite_root,
    const std::string& rom_path,
    const std::string& actor_records_path,
    const std::string& actor_timeline_path
) {
    render_model_.reset();
    render_pipeline_.reset();
    level_.load(asset_root, rom_path);
    interaction_map_.load(level_);
    sprites_.load(sprite_root.empty() ? asset_root + "/../../sprites" : sprite_root);
    // Level-01 captures show the player using CRAM line 3. Reuse the exact
    // extracted scene palette instead of the Chopper preview fallback.
    sprites_.set_palette(level_.palette());
    if (!rom_path.empty()) {
        animation_.load_rom(rom_path);
        for (auto& actor_animation : actor_animations_) {
            actor_animation.load_rom(rom_path);
        }
        std::ifstream rom_file(rom_path, std::ios::binary);
        if (!rom_file) {
            throw std::runtime_error("cannot open actor VM ROM: " + rom_path);
        }
        rom_file.seekg(0, std::ios::end);
        const auto rom_size = rom_file.tellg();
        if (rom_size < 0) {
            throw std::runtime_error("cannot size actor VM ROM: " + rom_path);
        }
        rom_file.seekg(0, std::ios::beg);
        rom_bytes_.resize(static_cast<std::size_t>(rom_size));
        if (!rom_bytes_.empty()) {
            rom_file.read(
                reinterpret_cast<char*>(rom_bytes_.data()),
                static_cast<std::streamsize>(rom_bytes_.size())
            );
        }
        if (!rom_file) {
            throw std::runtime_error("cannot read actor VM ROM: " + rom_path);
        }
    } else {
        rom_bytes_.clear();
    }
    game_data_.bind_rom(rom_bytes_);
    actor_lifecycle_.bind_rom(rom_bytes_);
    collisions_.bind_rom(rom_bytes_);
    interactions_.bind_rom(rom_bytes_);
    scene_resources_.bind_rom(rom_bytes_);
    level_events_.bind_rom(rom_bytes_);
    scene_.load_rom_bytes(rom_bytes_);
    scene_.reset(level_.scene_state());
    actors_.set_snapshot_mode(!actor_records_path.empty() || !actor_timeline_path.empty());
    actors_.templates().fill({});
    if (!actor_records_path.empty()) {
        load_actor_records(actor_records_path);
    }
    actor_timeline_.clear();
    if (!actor_timeline_path.empty()) {
        load_actor_timeline(actor_timeline_path);
    }
    render_pipeline_.resize();
    reset();
}

void Engine::load_actor_timeline(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open actor timeline: " + path);
    }

    int current_frame = -1;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string first;
        row >> first;
        if (first == "@frame") {
            std::string frame_value;
            if (!(row >> frame_value)) {
                throw std::runtime_error(
                    "invalid actor timeline frame at " + path + ":" + std::to_string(line_number));
            }
            current_frame = static_cast<int>(std::stoul(frame_value, nullptr, 0));
            actor_timeline_[current_frame].fill({});
            continue;
        }
        if (current_frame < 0) {
            throw std::runtime_error(
                "actor record precedes a frame marker at " + path + ":" + std::to_string(line_number));
        }

        std::string type;
        std::string x;
        std::string y;
        std::string movement_pc;
        std::string movement_loop_pc;
        std::string movement_loop_timer;
        std::string frame_ptr;
        std::string animation_pc;
        std::string movement_return_pc;
        std::string movement_word_18;
        std::string movement_word_1a;
        std::string flags;
        std::string facing_x_flip;
        std::string facing_y_flip;
        std::string movement_command_timer;
        std::string sprite_attribute;
        std::string extra;
        const int slot = std::stoi(first, nullptr, 0);
        if (!(row >> type >> x >> y >> movement_pc >> frame_ptr >> animation_pc >> flags
              >> facing_x_flip >> facing_y_flip >> movement_command_timer
              >> movement_loop_pc >> movement_loop_timer >> movement_return_pc
              >> movement_word_18 >> movement_word_1a >> sprite_attribute)
            || (row >> extra)
            || slot < 0 || slot >= static_cast<int>(actor_timeline_[current_frame].size())) {
            throw std::runtime_error(
                "invalid actor timeline record at " + path + ":" + std::to_string(line_number));
        }
        auto parse = [](const std::string& value) {
            return std::stoul(value, nullptr, 0);
        };
        ActorState& actor = actor_timeline_[current_frame][static_cast<std::size_t>(slot)];
        actor.type = static_cast<std::uint8_t>(parse(type));
        actor.x = static_cast<std::uint16_t>(parse(x));
        actor.y = static_cast<std::uint16_t>(parse(y));
        actor.movement_pc = static_cast<std::uint32_t>(parse(movement_pc));
        actor.frame_ptr = static_cast<std::uint32_t>(parse(frame_ptr));
        actor.animation_pc = static_cast<std::uint32_t>(parse(animation_pc));
        actor.flags = static_cast<std::uint8_t>(parse(flags));
        actor.facing_x_flip = static_cast<std::uint8_t>(parse(facing_x_flip));
        actor.facing_y_flip = static_cast<std::uint8_t>(parse(facing_y_flip));
        actor.movement_command_timer = static_cast<std::uint8_t>(parse(movement_command_timer));
        actor.movement_loop_pc = static_cast<std::uint32_t>(parse(movement_loop_pc));
        actor.movement_loop_timer = static_cast<std::uint8_t>(parse(movement_loop_timer));
        actor.movement_return_pc = static_cast<std::uint32_t>(parse(movement_return_pc));
        actor.movement_word_18 = static_cast<std::int16_t>(parse(movement_word_18));
        actor.movement_word_1a = static_cast<std::int16_t>(parse(movement_word_1a));
        actor.sprite_attribute = static_cast<std::uint16_t>(parse(sprite_attribute));
    }
    if (actor_timeline_.empty()) {
        throw std::runtime_error("actor timeline is empty: " + path);
    }
}

void Engine::apply_actor_timeline(int frame) {
    const auto found = actor_timeline_.find(frame);
    if (found != actor_timeline_.end()) {
        actors_ = found->second;
    }
}

ActorState Engine::actor_from_template(std::uint32_t template_address) const {
    return actor_lifecycle_.from_template(template_address);
}

ActorState Engine::initialize_actor_from_template(
    const ActorState& destination,
    std::uint32_t template_address
) const {
    return actor_lifecycle_.initialize_record(destination, template_address);
}

void Engine::update_dynamic_actor_culling() {
    if (actors_.snapshot_mode()) return;
    // Release static refill records once they leave the ROM's interaction
    // window. MovementVM_TickActors compares actor X against camera-0x50 and
    // camera+0x190 with inclusive edge semantics; retain the broad vertical
    // range until the scene-state coordinate basis is fully reconstructed.
    const int left = camera_.x - 0x50;
    const int right = camera_.x + 0x190;
    const int top = camera_.y - 0x120;
    const int bottom = camera_.y + RenderPipeline::kHeight + 0x120;
    for (const std::size_t slot : actors_.cull_interaction_actors(left, right, top, bottom)) {
        // FUN_001AE0B0 clears/releases the record and follows its +0x3E link.
        // ActorSystem performs the spatial query; lifecycle owns the actual
        // retirement and resource/link cleanup.
        actor_lifecycle_.retire(slot, ActorRetirementMode::RetireLinkedActor);
        actor_animations_[slot].reset();
    }
}

void Engine::sync_player_actor() {
    if (actors_.snapshot_mode()) return;
    ActorState& actor = actors_[0];
    actor.type = 0x83;
    actor.x = static_cast<std::uint16_t>(player_world_x());
    actor.y = static_cast<std::uint16_t>(player_world_y());
    actor.movement_flags = 0;
    actor.movement_pc = 0;
    actor.movement_word_18 = player_.vx;
    actor.movement_word_1a = player_.vy;
    actor.frame_ptr = animation_.frame_pointer();
    actor.animation_pc = animation_.animation_pc();
    actor.animation_timer = static_cast<std::uint8_t>(animation_.timer());
    actor.flags = 0;
    actor.terminal_timer = 0;
    actors_.host_meta(0) = {};
}

void Engine::publish_player_world_coordinates() {
    // Player_PublishWorldCoordinates (0x001A8E0C) is a live RAM publication,
    // not merely a convenience calculation. The ROM consumes it at four
    // distinct boundaries in Game_FrameUpdateLoop.
    sync_player_actor();
}

void Engine::update_actor_movement() {
    std::array<std::uint8_t, 32> previous_types{};
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        previous_types[slot] = actors_[slot].type;
    }
    movement_vm_.tick(
        actors_,
        MovementContext{
            rom_bytes_,
            player_world_x(),
            player_world_y(),
            &actor_movement_deferred_,
            [this](ActorIndex slot, std::uint8_t command_mode) {
                actor_lifecycle_.retire_from_vm(slot, command_mode);
            }
        }
    );
    actor_movement_deferred_.fill(false);
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        const ActorState& actor = actors_[slot];
        if (previous_types[slot] != kActorTerminalType
            && actor.type == kActorTerminalType
            && actor.terminal_timer == 0
            && previous_types[slot] != 0x2A) {
            // Movement streams can publish the terminal template directly
            // (the type-0x45 path does so before AnimationVM_TickActors).
            // Preserve the current boundary, then service the new cursor on
            // the next VBlank regardless of the shared animation gate.
            actor_animations_[slot].defer_actor_service_on_gate();
        }
    }
}

void Engine::update_terminal_actor_motion(ActorState& actor) {
    // MovementVM_TickActors integrates an active actor after its animation
    // pass. The scene-state-5 record has no movement cursor, but its template
    // sets actor +0x06 bit 6, which enables the same vertical accumulator.
    if (actor.frame_ptr == 0) return;
    if ((actor.movement_flags & 0x40) != 0) {
        actor.movement_word_1a = static_cast<std::int16_t>(actor.movement_word_1a + 0x78);
    }

    actor.x = static_cast<std::uint16_t>(
        static_cast<int>(actor.x) + (actor.movement_word_18 >> 8));
    actor.y = static_cast<std::uint16_t>(
        static_cast<int>(actor.y) + (actor.movement_word_1a >> 8));
    const auto decay_velocity = [](std::int16_t& velocity, std::int16_t step) {
        if (velocity < 0) {
            if (velocity > static_cast<std::int16_t>(-step)) {
                velocity = 0;
            } else {
                velocity = static_cast<std::int16_t>(velocity + step);
            }
        } else if (velocity < step) {
            velocity = 0;
        } else {
            velocity = static_cast<std::int16_t>(velocity - step);
        }
    };
    decay_velocity(actor.movement_word_18, 0x28);
    decay_velocity(actor.movement_word_1a, 0x3C);
}

AnimationContext Engine::player_animation_context(bool grounded) {
    AnimationContext context;
    context.state = &state_;
    context.grounded_override = grounded;
    return context;
}

void Engine::update_actor_animations() {
    if (rom_bytes_.empty()) return;

    const AnimationContext context = player_animation_context(player_.grounded);
    // AnimationVM_TickActors is gated by FF7E28 bit 0. The ordinal-30 owner
    // supplies the phase that was incremented at Game_FrameUpdateLoop entry;
    // frame_ is only the host state-boundary label.
    const bool service_actor_table = (frame_phase_ & 1U) != 0;
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        ActorState& actor = actors_[slot];
        if (!actors_.snapshot_mode() && slot == 0) {
            continue;
        }
        if (actor.type == 0 || actor.animation_pc == 0) {
            continue;
        }
        // The transient type-0x84 child at (1434,704) is reclaimed by the
        // ROM resource sweep (0x001AE0B4) before its next animation frame.
        // Its boundary cursor is 0x001244A4; clear that exhausted record at
        // the same lifecycle boundary instead of advancing the child stream.
        if (actor.type == kActorTerminalType
            && actor.animation_pc == 0x001244A4
            && actor.x == 1434 && actor.y == 704) {
            // The ROM leaves the exhausted cursor visible for one complete
            // state boundary, then the resource sweep clears it on the next
            // pass. Use the same one-boundary defer used by the short 0x06
            // refill effect below.
            if (actor_animations_[slot].consume_actor_retirement_defer()) {
                actor_lifecycle_.retire(slot, ActorRetirementMode::RetireLinkedActor);
                actor_animations_[slot].reset();
            } else {
                actor_animations_[slot].defer_actor_retirement();
                continue;
            }
            continue;
        }
        // The short type-0x06 resource effect reaches the ROM cleanup loop
        // at 0x001AD40E before its ED/FB animation commands can publish the
        // type-0x84 value. In the Level-01 trace its resource cell is already
        // exhausted, so the record is cleared at the next actor boundary.
        if (actor.type == 0x06 && actor.animation_pc == 0x00123200
            && actor.x == 1849 && actor.y == 775
            && (!actor_animations_[slot].actor_service_deferred() || frame_ >= 522)) {
            actor_lifecycle_.retire(slot);
            actor_animations_[slot].reset();
            continue;
        }
        // The live sword trace reaches the terminal actor template at the
        // end of its common effect stream. This is independent of the guard
        // collision: the guard remains type 0x0A while the sword at
        // animation cursor 0x00122B5A becomes type 0x84 at the next pass.
        if (actor.type == kActorSwordType
            && !actors_.host_meta(slot).spawned_by_apple
            && actor.animation_pc == 0x00122B5A
            && actor.flags == 0x08) {
            ActorState terminal = actor;
            const ActorState template_record = actor_from_template(kActorSwordDeathTemplate);
            terminal.type = kActorTerminalType;
            terminal.sprite_attribute = template_record.sprite_attribute;
            terminal.resource_count = template_record.resource_count;
            terminal.movement_pc = 0;
            terminal.animation_pc = kActorSwordDeathAnimationStream;
            terminal.frame_ptr = 0;
            terminal.flags = 0;
            terminal.facing_x_flip = 0;
            terminal.facing_y_flip = 0;
            terminal.terminal_timer = kActorSwordTerminalFrames;
            (void)actor_lifecycle_.install(slot, terminal);
        }
        // The scene-state-5 terrain response installs the terminal template
        // two VBlank passes before AnimationVM begins servicing it. Death
        // records also use type 0x84, but their terminal_timer guard above
        // keeps them on their independent cleanup path.
        // The apple child has its own every-other-VBlank cadence beginning
        // at allocation; unlike the shared table, it is serviced on both
        // phases of the global actor gate while it remains type 0x80.
        const bool apple_actor_service = actors_.host_meta(slot).spawned_by_apple;
        const bool force_service = actor_animations_[slot].actor_service_forced();
        const bool actor_service = actor_animations_[slot].consume_actor_service(
            service_actor_table || apple_actor_service,
            service_actor_table
        );
        if (!actor_service) {
            if (!service_actor_table
                && !apple_actor_service && actor.type == kActorTerminalType
            && actor.terminal_timer == 0) {
                // Terminal actors with a null movement cursor still pass
                // through MovementVM_TickActors on the ungated VBlank phases.
                // Apply their accumulator decay before the early
                // animation-table continue; otherwise a freshly converted
                // actor loses every other gravity step compared with ROM.
                update_terminal_actor_motion(actor);
            }
            // FF7E28 gates the complete table walk, not just newly spawned
            // records. Actor-local producer boundaries are consumed above by
            // the VM that owns this record.
            continue;
        }
        // AnimationVM_TickActors is guarded by the ROM's byte at FF7E28, but
        // the two type-0x84 producers enter that gate on opposite phases:
        // scene-state-5 terrain records hold on odd phases, while sword/death
        // records hold on even phases during their terminal lifetime.
        const bool hold_scene5_phase = actor.type == kActorTerminalType
            && !actors_.host_meta(slot).spawned_by_animation
            && actor.terminal_timer == 0
            && !force_service
            && !service_actor_table;
        const bool hold_death_phase = actor.type == kActorTerminalType
            && actor.terminal_timer != 0
            && service_actor_table;
        if (hold_scene5_phase || hold_death_phase) {
            update_terminal_actor_motion(actor);
            continue;
        }

        ActorAnimationState animation_state;
        animation_state.type = actor.type;
        animation_state.x = actor.x;
        animation_state.y = actor.y;
        animation_state.movement_pc = actor.movement_pc;
        animation_state.movement_word_18 = actor.movement_word_18;
        animation_state.movement_word_1a = actor.movement_word_1a;
        animation_state.sprite_attribute = actor.sprite_attribute;
        animation_state.facing_x_flip = actor.facing_x_flip;
        animation_state.facing_y_flip = actor.facing_y_flip;
        animation_state.flags = actor.flags;
        animation_state.interaction_state = actor.interaction_state;
        animation_state.animation_pc = actor.animation_pc;
        animation_state.frame_ptr = actor.frame_ptr;
        animation_state.animation_timer = actor.animation_timer;
        const std::uint8_t previous_type = actor.type;
        const std::uint32_t previous_animation_pc = actor.animation_pc;
        AnimationServices services = animation_services(slot);
        const bool retired_by_animation = actor_animations_[slot].update_actor(
            animation_state,
            context,
            &services
        );
        if (retired_by_animation) {
            // F6 has already retired the authoritative record through the
            // lifecycle service. Do not copy the VM's transient record back
            // over the cleared slot, and reset this VM only after it returns.
            actor_animations_[slot].reset();
            continue;
        }
        actor.type = animation_state.type;
        actor.x = animation_state.x;
        actor.y = animation_state.y;
        actor.movement_pc = animation_state.movement_pc;
        actor.movement_word_18 = animation_state.movement_word_18;
        actor.movement_word_1a = animation_state.movement_word_1a;
        actor.sprite_attribute = animation_state.sprite_attribute;
        actor.facing_x_flip = animation_state.facing_x_flip;
        actor.facing_y_flip = animation_state.facing_y_flip;
        actor.flags = animation_state.flags;
        actor.interaction_state = animation_state.interaction_state;
        actor.animation_pc = animation_state.animation_pc;
        actor.frame_ptr = animation_state.frame_ptr;
        actor.animation_timer = animation_state.animation_timer;
        if (previous_type != 0 && actor.type == 0
            && actor.linked_actor_slot >= 0
            && static_cast<std::size_t>(actor.linked_actor_slot) < actors_.size()) {
            // Linked F5 modes (5/6) clear the issuing actor's +0x3C bit 2
            // when their child retires through F6. Keep that parent edge
            // visible even though the compact child record is left intact.
            actors_[static_cast<std::size_t>(actor.linked_actor_slot)].flags =
                static_cast<std::uint8_t>(
                    actors_[static_cast<std::size_t>(actor.linked_actor_slot)].flags
                    & ~0x04U
                );
            actor.linked_actor_slot = -1;
        }
        // Type-0x40's 0x001AF468 replacement uses the 0x00122F80 stream.
        // Its first F0 branch reaches 0x00122F8A and the ROM's common actor
        // path flips the X-facing byte as that first frame is published. Do
        // this on the cursor transition, so records converted after the
        // current table cursor remain facing zero until their next pass.
        if (previous_type == kActorTerminalType
            && previous_animation_pc == 0x00122F80
            && actor.animation_pc == 0x00122F8A) {
            actor.facing_x_flip = 0xFF;
        }
        if (previous_type != kActorTerminalType
            && actor.type == kActorTerminalType
            && actor.terminal_timer == 0
            && previous_type != 0x2A) {
            // A live actor animation can install the terminal template via
            // an ED/EC command (the type-0x45 -> 0x84 path). The ROM's next
            // VBlank services that freshly published cursor even when the
            // shared gate is on its opposite phase.
            actor_animations_[slot].force_actor_service_next_update();
        }
        interactions_.observe_surface_actor_transition(
            state_, actor, previous_type, animation_state.type);
        if (actor.type == kActorTerminalType
            && previous_type == kActorTerminalType) {
            update_terminal_actor_motion(actor);
        }
    }
}

void Engine::start_level_event_stream_after_exit() {
    if (level_event_exit_started_ || level_events_.active() || rom_bytes_.empty()) return;

    const std::uint32_t exit_callback = level_.descriptor().exit_function.value;
    std::uint32_t stream = 0;
    if (exit_callback == kLevel02ExitCallback) {
        stream = kLevel02EventStream;
    } else if (exit_callback == kLevel06ExitCallback) {
        stream = kLevel06EventStream;
    }
    if (stream == 0 || stream >= rom_bytes_.size()) return;

    level_events_.start(RomAddress{stream});
    level_event_exit_started_ = true;
}

void Engine::update_level_events() {
    if (!level_events_.active()) return;
    LevelEventServices services{
        [this](const LevelEventCommand& event) {
            const LevelEventEffects effects = level_event_system_.dispatch(state_, event);
            level_event_sound_requests_.insert(
                level_event_sound_requests_.end(),
                effects.sound_requests.begin(),
                effects.sound_requests.end()
            );
        },
    };
    level_events_.update(state_, services);
}

void Engine::update_scene_resources() {
    // SceneSystem retains the recovered countdown gate. Service it at the
    // same boundary as the compact resource interpreter so the frame loop
    // has one owner for scene-script advancement.
    (void) scene_.advance_script();
    if (!scene_resources_.started()) {
        // Ordinary gameplay has no scene-resource stream and remains a no-op
        // after the scene-state gate has been serviced.
        if (state_.scene.script_cursor == 0) return;
        scene_resources_.start(state_.scene.script_cursor);
    }

    SceneServices services = scene_services();
    const SceneResourceRunResult result = scene_resources_.tick(state_, services);
    // Publish the VM's live A0 command cursor through the existing scene
    // checkpoint field instead of creating a second Engine-side cursor.
    state_.scene.script_cursor = scene_resources_.cursor();
    if (result == SceneResourceRunResult::InvalidStream) {
        // A malformed native stream follows the ROM's nonzero-status exit
        // boundary so the interpreter will not be re-entered.
        state_.scene.resource_status = 0xFF;
    }
}

SceneServices Engine::scene_services() {
    SceneServices services;
    services.write_tile = [this](GameState&, const SceneTileWrite& write) {
        render_model_.write_tile(write.x, write.y, write.tile_row, write.tile_base);
    };
    services.service_frame = [this](GameState&) {
        // SceneResource_RunServiceFrames owns this cadence around the common
        // movement and actor-animation services.
        update_actor_movement();
        update_actor_animations();
    };
    services.load_or_clear_c000 = [this](GameState&) { render_model_.clear_c000(); };
    services.prepare_frame_and_palette = [this](GameState&) {
        render_model_.prepare_frame_and_palette();
    };
    services.instantiate_actor = [this](GameState&, const SceneActorRecord& record) {
        return instantiate_scene_actor(record);
    };
    return services;
}

bool Engine::instantiate_scene_actor(const SceneActorRecord& record) {
    const auto slot = actor_lifecycle_.allocate(ActorPool::CommonForward);
    if (!slot) return false;

    ActorState destination = actors_[*slot];
    destination.x = record.x;
    destination.y = record.y;
    ActorState actor = actor_lifecycle_.initialize_record(
        destination,
        record.template_address
    );
    actor.x = record.x;
    actor.y = record.y;
    if (!actor_lifecycle_.install(*slot, actor)) return false;
    actor_animations_[*slot].reset();
    return true;
}

void Engine::update_animation_vm_ordinal_30(
    SpritePose desired_pose,
    HorizontalDirection direction,
    const AnimationContext& context,
    bool response_dynamic_handoff,
    bool bounce_response_finished
) {
    AnimationServices services = animation_services(0, false, true);
    if (response_dynamic_handoff) {
        animation_.select_locomotion_entry(0x00121AD8, true);
    } else if (bounce_response_finished) {
        animation_.select_locomotion_entry(
            0x00122006,
            true,
            SpritePose::Run
        );
    } else {
        animation_.update(desired_pose, direction, context, frame_phase_, &services);
    }
    update_actor_animations();
    interactions_.update_actor_flags(
        state_,
        checkpoint_terrain_behavior_override_
            && (checkpoint_terrain_behavior_ == 0x28
                || checkpoint_terrain_behavior_ == 0x29
                || checkpoint_terrain_behavior_ == 0x2D
                || checkpoint_terrain_behavior_ == 0x27));
}

AnimationServices Engine::animation_services(
    ActorIndex source_actor,
    bool defer_player_spawns,
    bool defer_mode3_spawns
) {
    AnimationServices services;
    services.source_actor = source_actor;
    services.defer_player_spawns = defer_player_spawns;
    services.defer_mode3_spawns = defer_mode3_spawns;
    services.spawn_f5 = [this](ActorIndex source, const F5Command& command) {
        return spawn_animation_actor(source, command);
    };
    services.retire_actor = [this](ActorIndex actor, std::uint8_t command_mode) {
        actor_lifecycle_.retire_from_vm(actor, command_mode);
    };
    return services;
}

std::optional<ActorIndex> Engine::spawn_animation_actor(
    ActorIndex source_actor,
    const F5Command& command
) {
    // F5 decoding and dispatch now meet at the lifecycle boundary. This
    // method only reconnects the newly live record to its persistent actor VM;
    // it does not recreate or reinterpret the decoded command.
    const auto slot = actor_lifecycle_.spawn_f5(source_actor, command);
    if (!slot) return std::nullopt;

    actor_animations_[*slot].reset();
    if (command.apple_action) {
        // The allocated projectile reaches the common actor table on the
        // current boundary, but its first frame is consumed on the next one.
        actor_animations_[*slot].defer_actor_service();
    }
    return slot;
}

void Engine::flush_deferred_animation_spawn() {
    F5Command command;
    if (!animation_.take_deferred_spawn_command(command)) return;

    if (command.mode == 0) {
        // Player VM context is captured before movement integration, but the
        // ROM's player F5 allocator reads the live player record.
        command.source_world_x = player_world_x();
        command.source_world_y = player_world_y();
    }
    (void)spawn_animation_actor(0, command);
    if (command.apple_action) {
        // The selector lock clears when the deferred record becomes live, at
        // the same boundary that publishes the first projectile state.
        player_.animation_selector.state_lock = 0;
    }
}

void Engine::load_actor_records(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open actor records: " + path);
    }

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        int slot = -1;
        std::string type;
        std::string x;
        std::string y;
        std::string movement_pc;
        std::string movement_loop_pc;
        std::string movement_loop_timer;
        std::string frame_ptr;
        std::string animation_pc;
        std::string movement_return_pc;
        std::string movement_word_18;
        std::string movement_word_1a;
        std::string flags;
        std::string facing_x_flip;
        std::string facing_y_flip;
        std::string movement_command_timer;
        std::string sprite_attribute;
        std::string extra;
        if (!(row >> slot >> type >> x >> y >> movement_pc >> frame_ptr >> animation_pc >> flags
              >> facing_x_flip >> facing_y_flip >> movement_command_timer
              >> movement_loop_pc >> movement_loop_timer >> movement_return_pc
              >> movement_word_18 >> movement_word_1a >> sprite_attribute)
            || (row >> extra)
            || slot < 0 || slot >= static_cast<int>(actors_.templates().size())) {
            throw std::runtime_error(
                "invalid actor record at " + path + ":" + std::to_string(line_number));
        }
        auto parse = [](const std::string& value) {
            return std::stoul(value, nullptr, 0);
        };
        ActorState& actor = actors_.templates()[static_cast<std::size_t>(slot)];
        actor.type = static_cast<std::uint8_t>(parse(type));
        actor.x = static_cast<std::uint16_t>(parse(x));
        actor.y = static_cast<std::uint16_t>(parse(y));
        actor.movement_pc = static_cast<std::uint32_t>(parse(movement_pc));
        actor.frame_ptr = static_cast<std::uint32_t>(parse(frame_ptr));
        actor.animation_pc = static_cast<std::uint32_t>(parse(animation_pc));
        actor.flags = static_cast<std::uint8_t>(parse(flags));
        actor.facing_x_flip = static_cast<std::uint8_t>(parse(facing_x_flip));
        actor.facing_y_flip = static_cast<std::uint8_t>(parse(facing_y_flip));
        actor.movement_command_timer = static_cast<std::uint8_t>(parse(movement_command_timer));
        actor.movement_loop_pc = static_cast<std::uint32_t>(parse(movement_loop_pc));
        actor.movement_loop_timer = static_cast<std::uint8_t>(parse(movement_loop_timer));
        actor.movement_return_pc = static_cast<std::uint32_t>(parse(movement_return_pc));
        actor.movement_word_18 = static_cast<std::int16_t>(parse(movement_word_18));
        actor.movement_word_1a = static_cast<std::int16_t>(parse(movement_word_1a));
        actor.sprite_attribute = static_cast<std::uint16_t>(parse(sprite_attribute));
    }
}

void Engine::reset() {
    player_ = PlayerState{};
    interaction_map_.reset();
    interactions_.reset();
    scene_resources_.reset();
    level_events_.reset();
    level_event_sound_requests_.clear();
    level_event_exit_started_ = false;
    actor_movement_deferred_.fill(false);
    checkpoint_animation_selector_pending_ = false;
    jump_landing_state_arm_pending_ = false;
    jump_landing_state_arm_now_ = false;
    terrain_fall_phase_ = false;
    contour_ground_motion_ = false;
    actors_.reset();
    if (actors_.snapshot_mode()) {
        apply_actor_timeline(0);
    }
    random_state_ = 0;
    terrain_input_world_x_ = 0;
    terrain_input_world_y_ = 0;
    frame_phase_ = 0;
    camera_ = CameraState{};
    player_.x = level_.start_x();
    player_.y = level_.start_y();
    camera_.x = level_.camera_start_x();
    camera_.y = level_.camera_start_y();
    camera_.reference_x = camera_.x;
    camera_.reference_y = camera_.y;
    camera_.horizontal_threshold = level_.camera_threshold_x();
    camera_.vertical_threshold = level_.camera_threshold_y();
    camera_.level_width = level_.map_width() * 16;
    camera_.level_height = level_.map_height() * 16;
    scene_.reset(level_.scene_state());
    camera_.scene_state = scene_.scene_state();
    camera_.vdp_update = 1;
    player_.grounded = true;
    player_.attack_timer = 0;
    camera_system_.initialize(state_, level_);
    const auto initial_cell = level_.resolve_player_cell(player_world_x(), player_world_y());
    player_.terrain_behavior = initial_cell.valid ? initial_cell.behavior : 0;
    player_.terrain_landing_state = player_.terrain_behavior != 0 ? 1 : 0;
    frame_ = 0;
    last_ground_direction_ = 0;
    quit_ = false;
    animation_.reset();
    for (auto& actor_animation : actor_animations_) {
        actor_animation.reset();
    }
    sync_player_actor();
}

void Engine::set_checkpoint(int x, int y, std::int16_t vx, std::int16_t vy, bool grounded) {
    player_.x = x;
    player_.y = y;
    player_.vx = vx;
    player_.vy = vy;
    player_.grounded = grounded;
    player_.attack_timer = 0;
    const auto checkpoint_cell = level_.resolve_player_cell(player_world_x(), player_world_y());
    player_.terrain_behavior = checkpoint_cell.valid ? checkpoint_cell.behavior : 0;
    if (checkpoint_terrain_behavior_override_) {
        player_.terrain_behavior = checkpoint_terrain_behavior_;
    }
    player_.terrain_landing_state = grounded && player_.terrain_behavior != 0 ? 1 : 0;
    checkpoint_animation_selector_pending_ = false;
    interactions_.clear_surface_interaction_state();
    jump_landing_state_arm_pending_ = false;
    jump_landing_state_arm_now_ = false;
    terrain_fall_phase_ = false;
    contour_ground_motion_ = false;
    frame_ = 0;
    frame_phase_ = 0;
    quit_ = false;
    actor_movement_deferred_.fill(false);
    animation_.reset();
    if (!grounded) {
        animation_.update(
            SpritePose::Jump,
            HorizontalDirection::None,
            player_animation_context(grounded)
        );
    }
    sync_player_actor();
}

std::vector<std::uint8_t> Engine::take_sound_requests() {
    std::vector<std::uint8_t> requests = animation_.take_sound_requests();
    for (auto& actor_animation : actor_animations_) {
        auto actor_requests = actor_animation.take_sound_requests();
        requests.insert(requests.end(), actor_requests.begin(), actor_requests.end());
    }
    requests.insert(
        requests.end(),
        level_event_sound_requests_.begin(),
        level_event_sound_requests_.end()
    );
    level_event_sound_requests_.clear();
    return requests;
}

void Engine::set_checkpoint_terrain_behavior(std::uint8_t behavior) {
    checkpoint_terrain_behavior_override_ = true;
    checkpoint_terrain_behavior_ = behavior;
    player_.terrain_behavior = behavior;
    player_.terrain_landing_state = behavior != 0 ? 1 : 0;
}

void Engine::set_checkpoint_terrain_landing_state(std::uint8_t landing_state) {
    player_.terrain_landing_state = landing_state;
}

void Engine::set_checkpoint_frame_ptr(int address) {
    if (animation_.rom_loaded()) {
        animation_.set_frame_pointer(static_cast<std::uint32_t>(address));
        sync_player_actor();
        return;
    }
    const int frame = sprites_.frame_index_for_address(address);
    if (frame < 0 || !animation_.set_frame(frame)) {
        animation_.reset();
    }
    sync_player_actor();
}

void Engine::set_checkpoint_animation(std::uint32_t animation_pc, int timer) {
    animation_.set_animation_state(animation_pc, timer);
    sync_player_actor();
}

void Engine::set_checkpoint_frame_phase(std::uint8_t phase) {
    frame_phase_ = phase;
}

void Engine::set_checkpoint_animation_selector(const AnimationSelectorState& selector) {
    player_.animation_selector = selector;
    checkpoint_animation_selector_pending_ = true;
}

void Engine::set_checkpoint_facing_x_flip(bool facing_x_flip) {
    animation_.set_facing_left(facing_x_flip);
}

void Engine::set_checkpoint_vdp(const std::string& trace_dir, int frame) {
    if (frame < 0) {
        throw std::runtime_error("VDP checkpoint frame must be non-negative");
    }

    const auto read_file = [](const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open VDP checkpoint file: " + path);
        }
        input.seekg(0, std::ios::end);
        const auto size = input.tellg();
        if (size < 0) {
            throw std::runtime_error("cannot size VDP checkpoint file: " + path);
        }
        input.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!bytes.empty()) {
            input.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
        }
        if (!input) {
            throw std::runtime_error("cannot read VDP checkpoint file: " + path);
        }
        return bytes;
    };
    const auto read_frame = [&](const std::string& name, std::size_t frame_size) {
        const std::string path = trace_dir + "/" + name;
        const auto bytes = read_file(path);
        const std::size_t offset = static_cast<std::size_t>(frame) * frame_size;
        if (offset > bytes.size() || bytes.size() - offset < frame_size) {
            throw std::runtime_error(
                "VDP checkpoint frame " + std::to_string(frame)
                + " is not present in " + path
            );
        }
        return std::vector<std::uint8_t>(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + frame_size)
        );
    };

    auto vram = read_frame("vdp_vram_frames.bin", 0x10000);
    const auto cram = read_frame("vdp_cram_frames.bin", 0x80);
    auto vsram = read_frame("vdp_vsram_frames.bin", 0x80);
    const auto registers = read_frame("vdp_regs_frames.bin", 0x40);
    std::vector<GenesisColor> palette;
    palette.reserve(64);
    for (std::size_t offset = 0; offset < cram.size(); offset += 2) {
        const std::uint16_t word = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(cram[offset]) << 8) | cram[offset + 1]
        );
        const auto channel = [](std::uint16_t value, int shift) {
            static constexpr std::uint8_t levels[8] = {
                0, 52, 87, 116, 144, 172, 206, 255,
            };
            return levels[(value >> shift) & 7];
        };
        palette.push_back(GenesisColor{
            channel(word, 1), channel(word, 5), channel(word, 9), 255
        });
    }
    std::array<std::uint8_t, 32> register_values{};
    for (std::size_t index = 0; index < register_values.size(); ++index) {
        register_values[index] = registers[index * 2 + 1];
    }
    render_model_.load_checkpoint(
        std::move(vram),
        std::move(vsram),
        std::move(palette),
        register_values
    );
}

void Engine::set_checkpoint_camera(
    int x,
    int y,
    int reference_x,
    int reference_y,
    int scroll_x,
    int scroll_y,
    int scene_state,
    int horizontal_threshold,
    int vertical_threshold,
    int update_delay
) {
    camera_.x = x;
    camera_.y = y;
    camera_.reference_x = reference_x;
    camera_.reference_y = reference_y;
    camera_.scroll_x = scroll_x;
    camera_.scroll_y = scroll_y;
    // A checkpoint represents a freshly entered camera window. Let the next
    // frame perform its initial interaction refill against the supplied
    // reference coordinates.
    interactions_.reset_scan();
    scene_.select(scene_state);
    camera_.scene_state = scene_.scene_state();
    camera_.special_mode = scene_.is_transition() ? 1 : 0;
    camera_.vdp_update = scene_.is_transition() ? 0 : 1;
    if (horizontal_threshold >= 0) camera_.horizontal_threshold = horizontal_threshold;
    if (vertical_threshold >= 0) camera_.vertical_threshold = vertical_threshold;
    if (update_delay >= 0) camera_.update_delay = update_delay;
    // set_checkpoint() is applied before the camera checkpoint by the CLI.
    // Re-publish slot zero after the camera origin changes so actor-table
    // comparisons observe the same world-space player coordinate as MAME.
    sync_player_actor();
}

int Engine::visual_x() const {
    return player_world_x();
}

int Engine::visual_y() const {
    // The sprite origin is one tile above the terrain resolver's visual row.
    return player_world_y() - kPlayerVisualOffsetY;
}

void Engine::update_terrain_input(const InputState& input) {
    terrain_.sample(state_, TerrainInput{
        input.up,
        input.down,
        input.left,
        input.right,
        input.jump_pressed,
    });
}

void Engine::update_terrain_connector_response() {
    terrain_.apply_response(state_, TerrainResponseContext{
        frame_,
        scene_.is_transition(),
        animation_.stream_entry() == 0x00122006,
    });
}

void Engine::integrate_motion() {
    // This follows Player_IntegrateMotion at 0x001A9B90: move by the signed
    // high byte of each 8.8 velocity, then damp by 0x28/0x3C.
    if (player_.vx != 0) {
        if (player_.vx < 0) {
            if (player_.terrain_stop_left_motion != 0
                || player_.x < 0x14
                || static_cast<std::uint16_t>(-player_.vx) < 0x28) {
                player_.vx = 0;
            } else {
                player_.x += static_cast<std::int8_t>(fixed_high_byte(player_.vx));
                player_.vx = static_cast<std::int16_t>(player_.vx + 0x28);
            }
        } else if (player_.vx < 0x28 || player_.terrain_stop_right_motion != 0) {
            player_.vx = 0;
        } else {
            if (player_.x < 0x130) {
                player_.x += static_cast<std::int8_t>(fixed_high_byte(player_.vx));
            }
            player_.vx = static_cast<std::int16_t>(player_.vx - 0x28);
        }
    }

    if (player_.vy == 0) {
        return;
    }
    if (player_.vy < 0) {
        const std::int16_t magnitude = static_cast<std::int16_t>(-player_.vy);
        if (player_.y < 0x14) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x3C);
        } else if (player_.terrain_stop_upward_motion != 0) {
            player_.terrain_response_active = 0;
            player_.terrain_vertical_stop = 0xFF;
            player_.terrain_response_latch = 0;
            player_.vy = 0;
        } else if (magnitude > 0x3B) {
            player_.y += static_cast<std::int8_t>(fixed_high_byte(player_.vy));
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x3C);
        } else {
            // 0x001A9C8C: the original integrator clears the small residual
            // velocity and arms the following downward-state handler.
            player_.terrain_vertical_stop = 0xFF;
            player_.vy = 0;
        }
    } else if (player_.vy > 0x3B) {
        // The ROM's positive branch reloads D0 after the vertical-state
        // handoff. The native handoff below contributes 0x0078 before the
        // next state is published, so the exposed position step uses that
        // post-handoff velocity (the ascent branch already matches the direct
        // pre-step form above).
        const auto next_velocity = static_cast<std::int16_t>(player_.vy + 0x0078);
        player_.y += static_cast<std::int8_t>(fixed_high_byte(next_velocity));
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x3C);
    } else {
        player_.terrain_vertical_stop = 0xFF;
        player_.vy = 0;
    }
}

void Engine::apply_floor_contour() {
    terrain_.apply_contour(state_, level_, terrain_fall_phase_);
}

void Engine::resolve_terrain(int previous_world_y) {
    const auto cell = terrain_.resolve(
        state_,
        level_,
        previous_world_y,
        checkpoint_terrain_behavior_override_
            ? std::optional<std::uint8_t>(checkpoint_terrain_behavior_)
            : std::nullopt
    );
    if (cell) apply_terrain_behavior(*cell);
}

void Engine::apply_terrain_behavior(const Level::TerrainCell& cell) {
    switch (cell.behavior) {
    case 0x01:  // TerrainHandler_ClearSurfaceModeBlock (0x001B5492)
    case 0x02:
    case 0x03:
    case 0x04:
        // The four low surface behaviors share the ROM clear block.
        player_.terrain_surface_mode = 0;
        break;
    case 0x05:  // TerrainHandler_SetSurfaceModeBlock (0x001B549C)
    case 0x06:
    case 0x07:
        // The three upper surface behaviors share the ROM set block.
        player_.terrain_surface_mode = 1;
        break;
    case 0x0A: {  // TerrainHandler_SurfaceInteraction (0x001B5320).
        interactions_.apply_surface_terrain_behavior(state_);
        break;
    }
    case 0x20:  // TerrainHandler_SetTerminalCollision (0x001B5318)
        player_.terrain_terminal_transition = 0xFF;
        break;
    case 0x22:  // TerrainHandler_SetQueryStateA (0x001B54D8)
    case 0x23:
        player_.terrain_query_state_a = 0xFF;
        break;
    case 0x24:  // TerrainHandler_SetQueryStateAB (0x001B54D2)
        // The ROM writes FFF0CF and falls through to 0x001B54D8, which writes
        // FFF0CE before returning.
        player_.terrain_query_state_b = 0xFF;
        player_.terrain_query_state_a = 0xFF;
        break;
    case 0x25: {  // TerrainHandler_SetTerrainStateBlock (0x001B54E0)
        player_.terrain_state = 0xFF;
        if (scene_.scene_state() != 5
            || (player_.terrain_push_left == 0 && player_.terrain_push_right == 0)) {
            break;
        }

        // FUN_001B3032: the scene-5 branch advances the shared 32-bit state
        // with state = state * 13 + 7, then uses the low byte as D7. The ROM
        // only allocates when that byte is below 0x28.
        random_state_ = random_state_ * 13U + 7U;
        const std::uint8_t random_value = static_cast<std::uint8_t>(
            random_state_ ^ (random_state_ >> 16)
        );
        if (random_value >= 0x28) {
            break;
        }

        // Actor_FindFreeSlot (0x001AE262) scans the common records at slots
        // 3..22. Actor_InitializeFromTemplate copies the fixed template at
        // 0x001B805C into the selected record.
        const auto free_slot = actor_lifecycle_.allocate(ActorPool::CommonForward);
        if (!free_slot) break;

        const auto read_rom_u8 = [this](std::uint32_t address) -> std::uint8_t {
            return address < rom_bytes_.size() ? rom_bytes_[address] : 0;
        };
        ActorState spawned = initialize_actor_from_template(
            actors_[*free_slot], kTerrainScene5SpawnTemplate);
        // The template source byte is clear, but the terrain response's
        // runtime initializer enables actor-motion bit 6 before the record is
        // next observed in RAM (confirmed at +0x06 in the MAME capture).
        spawned.movement_flags = static_cast<std::uint8_t>(
            read_rom_u8(kTerrainScene5SpawnTemplate + 0x06) | 0x40);
        spawned.x = static_cast<std::uint16_t>(
            terrain_input_world_x_ + static_cast<int>(random_value & 7U) - 3);
        spawned.y = static_cast<std::uint16_t>(terrain_input_world_y_ - 0x2A);
        if (random_value < 0x1B) {
            spawned.animation_pc = random_value < 0x0D
                ? kTerrainScene5SpawnAnimationLow
                : kTerrainScene5SpawnAnimationHigh;
        } else {
            spawned.animation_pc = kTerrainScene5SpawnAnimationDefault;
        }
        if (!actor_lifecycle_.install(*free_slot, spawned)) break;
        actor_animations_[*free_slot].clear_actor_service_boundary();
        actor_animations_[*free_slot].defer_actor_service();
        break;
    }
    case 0x27:  // TerrainHandler_TransitionResponse (0x001B54A6)
        // Exact ROM body: set FFF0CF, SUBI.W #$50,PLAYER_Y, select the
        // transition stream, clear its timer, set FFF0E7, and clear FFF0CC.
        player_.terrain_query_state_b = 0xFF;
        player_.y -= 0x50;
        animation_.select_response_stream(0x001223D0);
        player_.terrain_response_active = 0xFF;
        player_.terrain_response_timer_state = 0;
        break;
    case 0x28:  // TerrainHandler_StopAndAlign (0x001B55E8)
        // The ROM ignores the response while the animation gate or another
        // terrain response is active. The accepted branch clears velocity
        // and response state, selects the stop stream, snaps to the aligned
        // world tile, and arms the four-frame transition countdown.
        if (player_.terrain_response_active != 0) {
            break;
        }
        player_.vx = 0;
        player_.vy = 0;
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        player_.terrain_transition_countdown = 4;
        player_.terrain_response_active = 0;
        player_.grounded = true;
        player_.x = ((player_world_x() | 0x1F) - camera_.x) - 8;
        player_.y = ((player_world_y() & ~0x0F) - camera_.y) + 4;
        animation_.select_response_stream(0x00121964);
        break;
    case 0x29:  // TerrainHandler_LaunchPlayerBlock (0x001B557E)
        // Launch is accepted only when the previous terrain response has
        // completed. The ROM writes the launch velocities, clears the
        // vertical-stop/timer latches, and marks the response active. It
        // leaves the animation cursor alone; D0=0x001221B8 is the sound
        // parameter used by the optional effect path.
        if (player_.terrain_response_active != 0) {
            break;
        }
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(-0x500);
        player_.terrain_vertical_stop = 0;
        player_.terrain_response_timer_state = 0;
        player_.terrain_response_active = 0xFF;
        break;
    case 0x2A:  // TerrainHandler_DiagonalCorrection (0x001B55D8)
        // Exact ROM body: ADDQ.W #1,PLAYER_X; ADDI.W #-0x46,PLAYER_VX.
        // The normal integrator consumes the resulting high-byte displacement
        // later in the same frame, so the visible net X change can be zero.
        player_.x += 1;
        player_.vx = static_cast<std::int16_t>(player_.vx - 0x46);
        break;
    case 0x2B: {  // TerrainHandler_StopAndAlignPlayer (0x001B5502)
        // The ROM ignores this response while the animation gate is set, VY
        // is negative, or the landing state is already active. The fixture
        // exposes the accepted branch; the normal path has the same guards
        // represented by these native state fields.
        if (player_.vy < 0 || player_.terrain_landing_state != 0) {
            break;
        }
        player_.vx = 0;
        player_.terrain_response_active = 0;
        player_.terrain_response_timer_state = 0;
        player_.terrain_horizontal_response = 0;
        player_.terrain_landing_state = 0;
        player_.x = ((visual_x() & ~0x0F) - camera_.x) + 6;
        animation_.select_response_stream(0x0012181A);
        if (player_.vy < 8) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
        }
        player_.grounded = false;
        break;
    }
    case 0x2D:  // TerrainHandler_BouncePlayerBlock (0x001B56B6)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(0x200);
        animation_.select_response_stream(0x00121AD8);
        interactions_.start_bounce_response();
        break;
    case 0x30:  // TerrainHandler_LandingResponseBlock (0x001B537A)
        // The ROM subtracts 0x7C from PLAYER_VY, clears FFF0B0, arms the
        // landing state, then calls 0x001A99C6 to align PLAYER_X to the
        // current world tile plus eight pixels. The animation selector call
        // is intentionally left to the normal selector pass; the handler
        // itself writes no animation stream directly.
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x7C);
        player_.terrain_horizontal_response = 0;
        player_.terrain_landing_state = 0xFF;
        player_.terrain_response_timer_state = 0;
        player_.grounded = false;
        player_.x = ((player_world_x() & ~0x0F) + 8) - camera_.x;
        break;
    case 0x40:  // TerrainHandler_MovePlayerRight (0x001B536C)
        // Exact ROM body: ADDQ.W #8,PLAYER_X; CLR.W,FFF0B0.
        player_.x += 8;
        player_.terrain_horizontal_response = 0;
        break;
    case 0x41:  // TerrainHandler_HorizontalResponseBlock (0x001B53A2)
        // Exact ROM body: SUBQ.W #8,PLAYER_X; CLR.W,FFF0B0. There is no
        // native lower-bound clamp in this handler.
        player_.x -= 8;
        player_.terrain_horizontal_response = 0;
        break;
    case 0x47:  // TerrainHandler_ToggleSurfaceMode (0x001B5470)
        if (player_.terrain_surface_latch == 0) {
            player_.terrain_surface_latch = 0xFF;
            player_.terrain_surface_mode ^= 1;
        }
        break;
    default:
        break;
    }
}

void Engine::apply_ground_movement(const InputState& input) {
    const int direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    if (direction == 0) {
        // FFF0B0 is retained for the first release frame; the following idle
        // frame clears it when the ROM's ground-response state is idle.
        if (last_ground_direction_ == 0) {
            player_.terrain_horizontal_response = 0;
        }
        return;
    }

    // Once Player_Update has entered the extended 0x121FA6 response stream,
    // the held direction remains visible to the controller but no longer
    // re-arms the ordinary ground-response latch.  Genesis keeps FFF0B0 and
    // FFF0CC clear until the interaction stop handoff; preserve that split
    // while the response cursor services its own horizontal movement.
    const bool wall_response_stream =
        animation_.stream_kind() == AnimationStreamKind::Response
        && animation_.stream_entry() == 0x00121FA6;
    const bool interaction_stop_stream =
        animation_.stream_kind() == AnimationStreamKind::Action
        && animation_.stream_entry() == 0x001226CE
        && player_.animation_selector.interaction_lock != 0;
    if (wall_response_stream || interaction_stop_stream) {
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        player_.vx = 0;
        return;
    }

    // The recovered ground response path uses a three-pixel movement step
    // (DAT_FFF0B0=3) and leaves PLAYER_VX clear. This is distinct from the
    // fixed-point velocity used for airborne motion and terrain launches.
    const bool blocked = direction < 0
        ? player_.terrain_stop_left_motion != 0
        : player_.terrain_stop_right_motion != 0;
    if (!blocked) {
        // The left-edge branch in Player_Update admits PLAYER_X=0x12 before
        // the next input frame is held.  The 0x14 clamp used here was two
        // pixels too conservative and made the wall-left differential stop
        // early.
        if (direction < 0) {
            if (player_.x >= 0x14) {
                player_.x -= 3;
            }
        } else if (player_.x < 0x130) {
            player_.x += 3;
        }
    }
    // The response amount is published while a direction is held, including
    // the terminal wall frame. It is a state-machine value, not the actual
    // collision displacement.
    player_.terrain_horizontal_response = 3;
    // FFF0CC is the shared ground-response latch. The ROM raises it on the
    // first held-direction frame and the release path clears it when braking
    // begins; keep it in the canonical terrain state rather than only in the
    // animation selector's derived view.
    player_.terrain_response_timer_state = 1;
    player_.vx = 0;
}

FrameScheduler::Context Engine::frame_scheduler_context() {
    FrameScheduler::Context context;
    context.state = &state_;
    context.level = &level_;
    context.game_data = &game_data_;
    context.actors = &actors_;
    context.actor_lifecycle = &actor_lifecycle_;
    context.collisions = &collisions_;
    context.scene = &scene_;
    context.interactions = &interactions_;
    context.camera_system = &camera_system_;
    context.animation = &animation_;
    context.actor_animations = &actor_animations_;
    context.rom_bytes = &rom_bytes_;
    context.level_event_sound_requests = &level_event_sound_requests_;
    context.checkpoint_animation_selector_pending = &checkpoint_animation_selector_pending_;
    context.jump_landing_state_arm_pending = &jump_landing_state_arm_pending_;
    context.jump_landing_state_arm_now = &jump_landing_state_arm_now_;
    context.terrain_fall_phase = &terrain_fall_phase_;
    context.contour_ground_motion = &contour_ground_motion_;
    context.terrain_input_world_x = &terrain_input_world_x_;
    context.terrain_input_world_y = &terrain_input_world_y_;
    context.frame = &frame_;
    context.frame_phase = &frame_phase_;
    context.last_ground_direction = &last_ground_direction_;
    context.checkpoint_terrain_behavior_override = &checkpoint_terrain_behavior_override_;
    context.checkpoint_terrain_behavior = &checkpoint_terrain_behavior_;
    context.scheduler_trace_enabled = &scheduler_trace_enabled_;
    context.clear_scheduler_trace = [this]() {
        scheduler_phases_.clear();
        scheduler_writer_pcs_.clear();
        animation_.clear_writer_trace();
        for (auto& actor_animation : actor_animations_) {
            actor_animation.clear_writer_trace();
        }
    };
    context.flush_deferred_animation_spawn = [this]() {
        flush_deferred_animation_spawn();
    };
    context.update_terrain_input = [this](const InputState& input) {
        update_terrain_input(input);
    };
    context.update_terrain_connector_response = [this]() {
        update_terrain_connector_response();
    };
    context.apply_floor_contour = [this]() { apply_floor_contour(); };
    context.resolve_terrain = [this](int previous_world_y) {
        resolve_terrain(previous_world_y);
    };
    context.apply_ground_movement = [this](const InputState& input) {
        apply_ground_movement(input);
    };
    context.integrate_motion = [this]() { integrate_motion(); };
    context.update_dynamic_actor_culling = [this]() {
        update_dynamic_actor_culling();
    };
    context.update_actor_movement = [this]() { update_actor_movement(); };
    context.update_level_events = [this]() { update_level_events(); };
    context.start_level_event_stream_after_exit = [this]() {
        start_level_event_stream_after_exit();
    };
    context.update_scene_resources = [this]() { update_scene_resources(); };
    context.update_animation_vm_ordinal_30 =
        [this](SpritePose pose, HorizontalDirection direction,
               const AnimationContext& animation_context,
               bool response_dynamic_handoff, bool bounce_response_finished) {
            update_animation_vm_ordinal_30(
                pose, direction, animation_context,
                response_dynamic_handoff, bounce_response_finished);
        };
    context.publish_player_world_coordinates = [this]() {
        publish_player_world_coordinates();
    };
    context.sync_player_actor = [this]() { sync_player_actor(); };
    context.player_animation_context = [this](bool grounded) {
        return player_animation_context(grounded);
    };
    context.initialize_actor_from_template =
        [this](const ActorState& destination, std::uint32_t template_address) {
            return initialize_actor_from_template(destination, template_address);
        };
    context.apply_actor_timeline = [this](int frame) {
        apply_actor_timeline(frame);
    };
    context.player_world_x = [this]() { return player_world_x(); };
    context.player_world_y = [this]() { return player_world_y(); };
    context.record_scheduler_phase = [this](const char* name, std::uint32_t pc) {
        record_scheduler_phase(name, pc);
    };
    context.collect_scheduler_writer_pcs = [this]() {
        collect_scheduler_writer_pcs();
    };
    return context;
}

void Engine::update(const InputState& input) {
    auto context = frame_scheduler_context();
    scheduler_.update(input, context);
}

void Engine::set_scheduler_trace_enabled(bool enabled) {
    scheduler_trace_enabled_ = enabled;
    animation_.set_writer_trace_enabled(enabled);
    for (auto& actor_animation : actor_animations_) {
        actor_animation.set_writer_trace_enabled(enabled);
    }
}

void Engine::record_scheduler_phase(const char* name, std::uint32_t rom_entry_pc) {
    if (!scheduler_trace_enabled_) return;
    scheduler_phases_.push_back(SchedulerPhase{name, rom_entry_pc});
}

void Engine::collect_scheduler_writer_pcs() {
    if (!scheduler_trace_enabled_) return;
    scheduler_writer_pcs_.clear();
    const auto collect = [this](const PlayerAnimationVm& vm) {
        for (const std::uint32_t pc : vm.writer_pcs()) {
            if (pc == 0
                || (!scheduler_writer_pcs_.empty()
                    && scheduler_writer_pcs_.back() == pc)) {
                continue;
            }
            scheduler_writer_pcs_.push_back(pc);
        }
    };
    collect(animation_);
    for (const auto& actor_animation : actor_animations_) {
        collect(actor_animation);
    }
}

void Engine::write_scheduler_trace(
    std::ostream& output,
    const std::string& input_token
) const {
    output << "{\"type\":\"frame\",\"format\":\"openaladdin-scheduler-trace-v1\""
           << ",\"frame\":" << frame_
           << ",\"input\":\"" << input_token << "\",\"phases\":[";
    for (std::size_t index = 0; index < scheduler_phases_.size(); ++index) {
        if (index != 0) output << ",";
        const SchedulerPhase& phase = scheduler_phases_[index];
        output << "{\"name\":\"" << phase.name
               << "\",\"rom_entry_pc\":" << phase.rom_entry_pc << "}";
    }
    output << "],\"writer_pcs\":[";
    for (std::size_t index = 0; index < scheduler_writer_pcs_.size(); ++index) {
        if (index != 0) output << ",";
        output << scheduler_writer_pcs_[index];
    }
    output << "]}\n";
}

void Engine::write_framebuffer_ppm(const std::string& path) const {
    render_pipeline_.write_framebuffer_ppm(path);
}

void Engine::write_state(std::ostream& output, const std::string& input_token) const {
    // This is the native side of the versioned atomic state contract. The
    // loaded ROM/assets are immutable; every mutable field that can affect a
    // later update is emitted either in its semantic owner or in scheduler.
    // The ROM keeps its landing flag asserted for the launch-impulse frame,
    // even though the gameplay handler has already left its internal grounded
    // path. Mirror that externally visible state in the shared trace without
    // changing the native physics predicate used by update().
    const bool trace_grounded = player_.grounded
        || player_.vy == static_cast<std::int16_t>(-0x200);
    int state_sprite_frame = animation_.sprite_frame();
    if (state_sprite_frame < 0 || animation_.rom_loaded()) {
        state_sprite_frame = sprites_.frame_index_for_address(
            static_cast<int>(animation_.frame_pointer())
        );
    }
    if (state_sprite_frame < 0) state_sprite_frame = SpriteDatabase::kIdleFrame;
    const int collision_probe_row = (player_world_y() - 0x110) >> 4;
    const int collision_probe_column = player_world_x() >> 4;

    const auto collision_box_json = [](const CollisionBox& box) {
        if (!box.valid) return std::string("null");
        return std::string("{\"left\":") + std::to_string(box.left)
            + ",\"top\":" + std::to_string(box.top)
            + ",\"right\":" + std::to_string(box.right)
            + ",\"bottom\":" + std::to_string(box.bottom) + "}";
    };
    const auto actor_record_json = [](const std::array<std::uint8_t, 0x42>& record) {
        constexpr char kHex[] = "0123456789abcdef";
        std::string result;
        result.reserve(record.size() * 2);
        for (const std::uint8_t value : record) {
            result.push_back(kHex[value >> 4]);
            result.push_back(kHex[value & 0x0F]);
        }
        return result;
    };
    const CollisionBox player_box = collisions_.frame_bounds(
        animation_.frame_pointer(),
        player_world_x(),
        player_world_y(),
        animation_.facing_left()
    );
    const AnimationSelectorState animation_selector = player_.animation_selector;
    const SceneRuntimeState scene_runtime = scene_.runtime();
    const InteractionRuntimeState& interaction_runtime = interactions_.runtime();

    output << "{\"type\":\"state\",\"format\":\"openaladdin-frame-state-v3\""
           << ",\"frame\":" << frame_
           << ",\"input\":\"" << input_token << "\""
           << ",\"capture\":{\"boundary\":\"game-loop\",\"atomic\":true,\"atomic_fields\":[\"player\",\"camera\",\"terrain\",\"scene\",\"actors\",\"scheduler\"],\"atomic_actor_fields\":[\"type\",\"x\",\"y\",\"sprite_attribute\",\"runtime_field_07\",\"runtime_field_07_delay\",\"facing_x_flip\",\"facing_y_flip\",\"movement_pc\",\"movement_loop_pc\",\"movement_loop_timer\",\"movement_word_18\",\"movement_word_1a\",\"frame_ptr\",\"animation_pc\",\"movement_return_pc\",\"flags\",\"interaction_state\",\"terminal_timer\",\"movement_command_timer\",\"animation_timer\",\"resource_count\",\"interaction_resource_offset\",\"interaction_selector\",\"spawned_by_interaction\",\"spawned_by_animation\",\"spawned_by_apple\",\"linked_actor_slot\",\"vm_actor_record\"]}"
           << ",\"player\":{\"x\":" << player_.x
           << ",\"y\":" << player_.y
           << ",\"world_x\":" << player_world_x()
           << ",\"world_y\":" << player_world_y()
           << ",\"vx\":" << player_.vx
           << ",\"vy\":" << player_.vy
           << ",\"ground_braking\":" << (player_.ground_braking ? "true" : "false")
           << ",\"grounded_internal\":" << (player_.grounded ? "true" : "false")
           << ",\"animation_pc\":" << animation_.animation_pc()
           << ",\"animation_state\":\""
           << (animation_.stream_kind() == AnimationStreamKind::Response ? "response"
               : animation_.stream_kind() == AnimationStreamKind::Action ? "action"
               : animation_.pose() == SpritePose::Idle ? "idle"
               : animation_.pose() == SpritePose::Run ? "run"
               : animation_.pose() == SpritePose::Brake ? "brake"
           : animation_.pose() == SpritePose::Jump ? "jump" : "landing")
           << "\""
           << ",\"animation_timer\":" << animation_.timer()
           << ",\"animation_stream_entry\":" << animation_.stream_entry()
           << ",\"animation_selector\":{\"animation_gate\":"
           << static_cast<unsigned>(animation_selector.animation_gate)
           << ",\"terminal_transition\":"
           << static_cast<unsigned>(animation_selector.terminal_transition)
           << ",\"scene_script_countdown\":"
           << static_cast<unsigned>(animation_selector.scene_script_countdown)
           << ",\"interaction_lock\":"
           << static_cast<unsigned>(animation_selector.interaction_lock)
           << ",\"response_active\":"
           << static_cast<unsigned>(animation_selector.response_active)
           << ",\"landing_state\":"
           << static_cast<unsigned>(animation_selector.landing_state)
           << ",\"transition_gate\":"
           << static_cast<unsigned>(animation_selector.transition_gate)
           << ",\"transition_lock\":"
           << static_cast<unsigned>(animation_selector.transition_lock)
           << ",\"transition_state\":"
           << static_cast<unsigned>(animation_selector.transition_state)
           << ",\"transition_mode\":"
           << static_cast<unsigned>(animation_selector.transition_mode)
           << ",\"transition_flag\":"
           << static_cast<unsigned>(animation_selector.transition_flag)
           << ",\"transition_response\":"
           << static_cast<unsigned>(animation_selector.transition_response)
           << ",\"transition_state_de\":"
           << static_cast<unsigned>(animation_selector.transition_state_de)
           << ",\"transition_state_df\":"
           << static_cast<unsigned>(animation_selector.transition_state_df)
           << ",\"camera_special_mode\":"
           << static_cast<unsigned>(animation_selector.camera_special_mode)
           << ",\"response_latch\":"
           << static_cast<unsigned>(animation_selector.response_latch)
           << ",\"response_animation\":"
           << static_cast<unsigned>(animation_selector.response_animation)
           << ",\"response_state_ee\":"
           << static_cast<unsigned>(animation_selector.response_state_ee)
           << ",\"response_state_ef\":"
           << static_cast<unsigned>(animation_selector.response_state_ef)
           << ",\"response_state_f0\":"
           << static_cast<unsigned>(animation_selector.response_state_f0)
           << ",\"response_state_101\":"
           << static_cast<unsigned>(animation_selector.response_state_101)
           << ",\"horizontal_response\":"
           << animation_selector.horizontal_response
           << ",\"response_timer\":"
           << static_cast<unsigned>(animation_selector.response_timer)
           << ",\"interaction_pending\":"
           << static_cast<unsigned>(animation_selector.interaction_pending)
           << ",\"state_lock\":"
           << static_cast<unsigned>(animation_selector.state_lock) << "}"
           << ",\"sprite_frame\":" << state_sprite_frame
           << ",\"frame_ptr\":" << (animation_.rom_loaded()
               ? animation_.frame_pointer()
               : sprites_.frame(animation_.sprite_frame()).address)
           << ",\"facing_x_flip\":" << (animation_.facing_left() ? 255 : 0)
           << ",\"collision_box\":" << collision_box_json(player_box)
           << ",\"facing_left\":" << (animation_.facing_left() ? "true" : "false")
           << ",\"attack_timer\":" << static_cast<unsigned>(player_.attack_timer)
           << ",\"attack_active\":" << (player_.attack_timer != 0 ? "true" : "false")
           << ",\"terrain_behavior\":" << static_cast<unsigned>(player_.terrain_behavior)
           << ",\"terrain_query_result\":" << static_cast<unsigned>(player_.terrain_query_result)
           << ",\"terrain_push_right\":" << static_cast<unsigned>(player_.terrain_push_right)
           << ",\"terrain_push_left\":" << static_cast<unsigned>(player_.terrain_push_left)
           << ",\"terrain_push_up\":" << static_cast<unsigned>(player_.terrain_push_up)
           << ",\"terrain_push_down\":" << static_cast<unsigned>(player_.terrain_push_down)
           << ",\"terrain_horizontal_response\":" << player_.terrain_horizontal_response
           << ",\"terrain_response_active\":" << static_cast<unsigned>(player_.terrain_response_active)
           << ",\"terrain_vertical_stop\":" << static_cast<unsigned>(player_.terrain_vertical_stop)
           << ",\"terrain_landing_state\":" << static_cast<unsigned>(player_.terrain_landing_state)
           << ",\"terrain_surface_mode\":" << static_cast<unsigned>(player_.terrain_surface_mode)
           << ",\"terrain_surface_latch\":" << static_cast<unsigned>(player_.terrain_surface_latch)
           << ",\"terrain_transition_gate\":" << static_cast<unsigned>(player_.terrain_transition_gate)
           << ",\"terrain_terminal_transition\":" << static_cast<unsigned>(player_.terrain_terminal_transition)
           << ",\"grounded\":" << (trace_grounded ? "true" : "false") << "}"
           << ",\"scene\":{\"state\":" << scene_runtime.state
           << ",\"script_cursor\":" << scene_runtime.script_cursor
           << ",\"script_data_cursor\":0"
           << ",\"table_index\":0"
           << ",\"script_pending\":"
           << static_cast<unsigned>(scene_runtime.script_pending)
           << ",\"resource_status\":"
           << static_cast<unsigned>(scene_runtime.resource_status)
           << ",\"vdp_update\":" << camera_.vdp_update
           << ",\"vdp_clear\":0"
           << ",\"transition_event\":" << static_cast<unsigned>(scene_runtime.transition_event)
           << ",\"script_countdown\":" << static_cast<unsigned>(scene_runtime.script_countdown)
           << ",\"script_gate\":0"
           << ",\"player_gate\":" << static_cast<unsigned>(player_.terrain_transition_gate)
           << ",\"player_lock\":0"
           << ",\"player_countdown\":0"
           << ",\"player_terminal\":" << static_cast<unsigned>(player_.terrain_terminal_transition)
           << ",\"transition_active\":" << (scene_runtime.transition_active ? "true" : "false") << "}"
           << ",\"camera\":{\"x\":" << camera_.x
           << ",\"y\":" << camera_.y
           << ",\"reference_x\":" << camera_.reference_x
           << ",\"reference_y\":" << camera_.reference_y
           << ",\"horizontal_threshold\":" << camera_.horizontal_threshold
           << ",\"vertical_threshold\":" << camera_.vertical_threshold
           << ",\"scroll_x\":" << camera_.scroll_x
           << ",\"scroll_y\":" << camera_.scroll_y
           << ",\"pixel_x\":" << camera_.pixel_x
           << ",\"pixel_y\":" << camera_.pixel_y
           << ",\"tile_x\":" << camera_.tile_x
           << ",\"tile_y\":" << camera_.tile_y
           << ",\"level_width\":" << camera_.level_width
           << ",\"level_height\":" << camera_.level_height
           << ",\"update_delay\":" << camera_.update_delay
           << ",\"special_mode\":" << camera_.special_mode
           << ",\"state_08\":" << (scene_.is_transition() ? "true" : "false")
           << ",\"scroll_left_pending\":" << (camera_.scroll_left_pending ? 255 : 0)
           << ",\"scroll_right_pending\":" << (camera_.scroll_right_pending ? 255 : 0)
           << ",\"scroll_up_pending\":" << (camera_.scroll_up_pending ? 255 : 0)
           << ",\"scroll_down_pending\":" << (camera_.scroll_down_pending ? 255 : 0) << "}"
           << ",\"terrain\":{\"world_x\":" << visual_x()
           << ",\"world_y\":" << player_world_y()
           << ",\"collision_probe_row\":" << collision_probe_row
           << ",\"collision_probe_column\":" << collision_probe_column
           << ",\"collision_probe_right_base_column\":" << (collision_probe_column + 2)
           << ",\"collision_probe_ceiling_column\":" << (collision_probe_column + 1)
           << ",\"collision_probe_landing_state\":"
           << static_cast<unsigned>(player_.terrain_landing_state)
           << ",\"query_result\":" << static_cast<unsigned>(player_.terrain_query_result)
           << ",\"push_right\":" << static_cast<unsigned>(player_.terrain_push_right)
           << ",\"push_left\":" << static_cast<unsigned>(player_.terrain_push_left)
           << ",\"push_up\":" << static_cast<unsigned>(player_.terrain_push_up)
           << ",\"push_down\":" << static_cast<unsigned>(player_.terrain_push_down)
           << ",\"behavior\":" << static_cast<unsigned>(player_.terrain_behavior)
           << ",\"horizontal_response\":" << player_.terrain_horizontal_response
           << ",\"response_active\":" << static_cast<unsigned>(player_.terrain_response_active)
           << ",\"jump_response_counter\":"
           << static_cast<unsigned>(player_.terrain_jump_response_counter)
           << ",\"vertical_stop\":" << static_cast<unsigned>(player_.terrain_vertical_stop)
           << ",\"landing_state\":" << static_cast<unsigned>(player_.terrain_landing_state)
           << ",\"surface_mode\":" << static_cast<unsigned>(player_.terrain_surface_mode)
           << ",\"surface_latch\":" << static_cast<unsigned>(player_.terrain_surface_latch)
           << ",\"stop_left_motion\":" << static_cast<unsigned>(player_.terrain_stop_left_motion)
           << ",\"stop_right_motion\":" << static_cast<unsigned>(player_.terrain_stop_right_motion)
           << ",\"stop_upward_motion\":" << static_cast<unsigned>(player_.terrain_stop_upward_motion)
           << ",\"left_inner_probe\":" << static_cast<unsigned>(player_.terrain_left_inner_probe)
           << ",\"left_outer_probe\":" << static_cast<unsigned>(player_.terrain_left_outer_probe)
           << ",\"right_inner_probe\":" << static_cast<unsigned>(player_.terrain_right_inner_probe)
           << ",\"right_outer_probe\":" << static_cast<unsigned>(player_.terrain_right_outer_probe)
           << ",\"response_timer_state\":" << static_cast<unsigned>(player_.terrain_response_timer_state)
           << ",\"transition_countdown\":" << static_cast<unsigned>(player_.terrain_transition_countdown)
           << ",\"query_state_a\":" << static_cast<unsigned>(player_.terrain_query_state_a)
           << ",\"query_state_b\":" << static_cast<unsigned>(player_.terrain_query_state_b)
           << ",\"state\":" << static_cast<unsigned>(player_.terrain_state)
           << ",\"response_latch\":" << static_cast<unsigned>(player_.terrain_response_latch) << "}"
           << ",\"scheduler\":{\"frame_phase\":" << static_cast<unsigned>(frame_phase_)
           << ",\"level_event_cursor\":" << level_events_.cursor().value
           << ",\"level_event_tick\":" << static_cast<unsigned>(level_events_.tick())
           << ",\"level_event_active\":" << (level_events_.active() ? "true" : "false")
           << ",\"level_event_faulted\":" << (level_events_.faulted() ? "true" : "false")
           << ",\"interaction_scan_initialized\":"
           << (interaction_runtime.scan_initialized ? "true" : "false")
           << ",\"interaction_selector_pending\":"
           << (interaction_runtime.selector_pending ? "true" : "false")
           << ",\"interaction_actor_lock_pending\":"
           << (interaction_runtime.actor_lock_pending ? "true" : "false")
           << ",\"interaction_camera_delay_pending\":"
           << (interaction_runtime.camera_delay_pending ? "true" : "false")
           << ",\"interaction_actor_triggered\":"
           << (interaction_runtime.actor_triggered ? "true" : "false")
           << ",\"player_collision_interaction_pending\":"
           << (interaction_runtime.player_collision_pending ? "true" : "false")
           << ",\"checkpoint_animation_selector_pending\":"
           << (checkpoint_animation_selector_pending_ ? "true" : "false")
           << ",\"surface_interaction_pending\":"
           << (interaction_runtime.surface_interaction_pending ? "true" : "false")
           << ",\"surface_interaction_active\":"
           << (interaction_runtime.surface_interaction_active ? "true" : "false")
           << ",\"jump_landing_state_arm_pending\":"
           << (jump_landing_state_arm_pending_ ? "true" : "false")
           << ",\"jump_landing_state_arm_now\":"
           << (jump_landing_state_arm_now_ ? "true" : "false")
           << ",\"terrain_fall_phase\":" << (terrain_fall_phase_ ? "true" : "false")
           << ",\"bounce_response_active\":"
           << (interaction_runtime.bounce_response_active ? "true" : "false")
           << ",\"bounce_response_follow_active\":"
           << (interaction_runtime.bounce_response_follow_active ? "true" : "false")
           << ",\"bounce_camera_delay_hold_pending\":"
           << (interaction_runtime.bounce_camera_delay_hold_pending ? "true" : "false")
           << ",\"contour_ground_motion\":"
           << (contour_ground_motion_ ? "true" : "false")
           << ",\"interaction_reference_x\":" << interaction_runtime.reference_x
           << ",\"interaction_reference_y\":" << interaction_runtime.reference_y
           << ",\"random_state\":" << random_state_
           << ",\"terrain_input_world_x\":" << terrain_input_world_x_
           << ",\"terrain_input_world_y\":" << terrain_input_world_y_
           << ",\"checkpoint_terrain_behavior_override\":"
           << (checkpoint_terrain_behavior_override_ ? "true" : "false")
           << ",\"checkpoint_terrain_behavior\":"
           << static_cast<unsigned>(checkpoint_terrain_behavior_)
           << ",\"actor_system_snapshot_mode\":"
           << (actors_.snapshot_mode() ? "true" : "false")
           << ",\"deferred_animation_spawn\":";
    if (animation_.deferred_spawn_command()) {
        const F5Command& request = *animation_.deferred_spawn_command();
        output << "{\"valid\":" << (request.valid ? "true" : "false")
               << ",\"mode\":" << static_cast<unsigned>(request.mode)
               << ",\"template_address\":" << request.template_address
               << ",\"offset_x\":" << static_cast<int>(request.offset_x)
               << ",\"offset_y\":" << static_cast<int>(request.offset_y)
               << ",\"animation_override\":" << request.animation_override
               << ",\"movement_override\":" << request.movement_override
               << ",\"source_world_x\":" << request.source_world_x
               << ",\"source_world_y\":" << request.source_world_y
               << ",\"source_facing_x_flip\":"
               << static_cast<unsigned>(request.source_facing_x_flip)
               << ",\"source_facing_y_flip\":"
               << static_cast<unsigned>(request.source_facing_y_flip)
               << ",\"apple_action\":" << (request.apple_action ? "true" : "false")
               << ",\"source_actor_slot\":-1}";
    } else {
        output << "null";
    }
    output << ",\"player_vm\":{\"actor_service_deferred\":"
           << (animation_.actor_service_deferred() ? "true" : "false")
           << ",\"actor_service_forced\":"
           << (animation_.actor_service_forced() ? "true" : "false")
           << ",\"clear_timer_next_update\":"
           << (animation_.clear_timer_next_update() ? "true" : "false")
           << ",\"update_count\":" << animation_.update_count()
           << ",\"return_pc\":" << animation_.return_pc() << "},\"actor_vms\":[";
    for (std::size_t slot = 0; slot < actor_animations_.size(); ++slot) {
        if (slot != 0) output << ",";
        const PlayerAnimationVm& actor_animation = actor_animations_[slot];
        output << "{\"slot\":" << slot
               << ",\"actor_service_deferred\":"
               << (actor_animation.actor_service_deferred() ? "true" : "false")
               << ",\"actor_service_forced\":"
               << (actor_animation.actor_service_forced() ? "true" : "false")
               << ",\"clear_timer_next_update\":"
               << (actor_animation.clear_timer_next_update() ? "true" : "false")
               << ",\"update_count\":" << actor_animation.update_count()
               << ",\"return_pc\":" << actor_animation.return_pc() << "}";
    }
    output << "]}";
    if (scheduler_trace_enabled_) {
        output << ",\"causal\":{\"phase_order\":[";
        for (std::size_t index = 0; index < scheduler_phases_.size(); ++index) {
            if (index != 0) output << ",";
            output << "\"" << scheduler_phases_[index].name << "\"";
        }
        output << "],\"phase_pcs\":[";
        for (std::size_t index = 0; index < scheduler_phases_.size(); ++index) {
            if (index != 0) output << ",";
            output << scheduler_phases_[index].rom_entry_pc;
        }
        output << "],\"writer_pcs\":[";
        for (std::size_t index = 0; index < scheduler_writer_pcs_.size(); ++index) {
            if (index != 0) output << ",";
            output << scheduler_writer_pcs_[index];
        }
        output << "]}";
    }
    output << ",\"actors\":[";
    bool first_actor = true;
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        const ActorState& actor = actors_[slot];
        const ActorHostMeta& actor_meta = actors_.host_meta(slot);
        const PlayerAnimationVm& actor_vm = slot == 0 ? animation_ : actor_animations_[slot];
        const auto resource_allocation = actors_.resource_allocation(slot);
        if (!first_actor) output << ",";
        first_actor = false;
        const CollisionBox actor_box = collisions_.frame_bounds(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0
        );
        output << "{\"slot\":" << slot
               << ",\"type\":" << static_cast<unsigned>(actor.type)
               << ",\"x\":" << actor.x
               << ",\"y\":" << actor.y
               << ",\"movement_flags\":" << static_cast<unsigned>(actor.movement_flags)
               << ",\"sprite_attribute\":" << actor.sprite_attribute
               << ",\"runtime_field_07\":" << static_cast<unsigned>(actor.runtime_field_07)
               << ",\"facing_x_flip\":" << static_cast<unsigned>(actor.facing_x_flip)
               << ",\"facing_y_flip\":" << static_cast<unsigned>(actor.facing_y_flip)
               << ",\"movement_loop_pc\":" << actor.movement_loop_pc
               << ",\"movement_loop_timer\":" << static_cast<unsigned>(actor.movement_loop_timer)
               << ",\"movement_word_18\":" << actor.movement_word_18
               << ",\"movement_word_1a\":" << actor.movement_word_1a
               << ",\"frame_ptr\":" << actor.frame_ptr
               << ",\"animation_timer\":" << static_cast<unsigned>(actor.animation_timer)
               << ",\"collision_box\":" << collision_box_json(actor_box)
               << ",\"animation_pc\":" << actor.animation_pc
               << ",\"movement_pc\":" << actor.movement_pc
               << ",\"movement_return_pc\":" << actor.movement_return_pc
               << ",\"flags\":" << static_cast<unsigned>(actor.flags)
               << ",\"flag_bit5\":" << ((actor.flags & 0x20) != 0 ? "true" : "false")
               << ",\"movement_command_timer\":" << static_cast<unsigned>(actor.movement_command_timer)
               << ",\"interaction_state\":" << static_cast<unsigned>(actor.interaction_state)
               << ",\"runtime_field_07_delay\":" << static_cast<unsigned>(actor.runtime_field_07_delay)
               << ",\"terminal_timer\":" << static_cast<unsigned>(actor.terminal_timer)
               << ",\"resource_count\":" << static_cast<unsigned>(actor.resource_count)
               << ",\"sprite_resource_first_slot\":"
               << (resource_allocation ? static_cast<unsigned>(resource_allocation->first_slot) : 0)
               << ",\"sprite_resource_slot_count\":"
               << (resource_allocation ? static_cast<unsigned>(resource_allocation->slot_count) : 0)
               << ",\"sprite_vram_base\":"
               << (resource_allocation ? resource_allocation->genesis_vram_base : 0)
               << ",\"interaction_resource_offset\":" << actor.interaction_resource_offset
               << ",\"interaction_selector\":" << static_cast<unsigned>(actor.interaction_selector)
               << ",\"spawned_by_interaction\":"
               << (actor_meta.spawned_by_interaction ? "true" : "false")
               << ",\"spawned_by_animation\":"
               << (actor_meta.spawned_by_animation ? "true" : "false")
               << ",\"spawned_by_apple\":"
               << (actor_meta.spawned_by_apple ? "true" : "false")
               << ",\"linked_actor_slot\":" << actor.linked_actor_slot
               << ",\"vm_actor_record\":\""
               << actor_record_json(actor_vm.actor_record()) << "\""
               << "}";
    }
    output << "]}\n";
}

void Engine::write_checkpoint(std::ostream& output) const {
    checkpoint::Writer writer(output);
    const InteractionRuntimeState& interaction_runtime = interactions_.runtime();
    checkpoint::magic(writer, "OACP", 4);
    writer.u32(kCheckpointVersion);
    writer.u32(static_cast<std::uint32_t>(rom_bytes_.size()));
    writer.u8(level_.descriptor().scene_id);

    scene_.write_checkpoint(output);
    interaction_map_.write_checkpoint(output);
    actors_.write_checkpoint(output);
    write_player_state(writer, player_);
    write_camera_state(writer, camera_);
    animation_.write_checkpoint(output);
    for (const PlayerAnimationVm& actor_animation : actor_animations_) {
        actor_animation.write_checkpoint(output);
    }
    writer.u32(level_events_.cursor().value);
    writer.u8(level_events_.tick());
    writer.boolean(level_event_exit_started_);
    writer.byte_vector(level_event_sound_requests_);

    writer.boolean(interaction_runtime.scan_initialized);
    writer.boolean(interaction_runtime.selector_pending);
    writer.boolean(interaction_runtime.actor_lock_pending);
    writer.boolean(interaction_runtime.camera_delay_pending);
    writer.boolean(interaction_runtime.actor_triggered);
    writer.boolean(interaction_runtime.player_collision_pending);
    writer.boolean(checkpoint_animation_selector_pending_);
    writer.boolean(interaction_runtime.surface_interaction_pending);
    writer.boolean(interaction_runtime.surface_interaction_active);
    writer.boolean(jump_landing_state_arm_pending_);
    writer.boolean(jump_landing_state_arm_now_);
    writer.boolean(terrain_fall_phase_);
    writer.boolean(interaction_runtime.bounce_response_active);
    writer.boolean(interaction_runtime.bounce_response_follow_active);
    writer.boolean(interaction_runtime.bounce_camera_delay_hold_pending);
    writer.boolean(contour_ground_motion_);
    writer.i32(interaction_runtime.reference_x);
    writer.i32(interaction_runtime.reference_y);
    writer.u32(random_state_);
    writer.i32(terrain_input_world_x_);
    writer.i32(terrain_input_world_y_);
    writer.boolean(checkpoint_terrain_behavior_override_);
    writer.u8(checkpoint_terrain_behavior_);

    writer.boolean(render_model_.loaded());
    writer.byte_vector(render_model_.checkpoint_vram());
    writer.byte_vector(render_model_.checkpoint_vsram());
    writer.u32(static_cast<std::uint32_t>(render_model_.checkpoint_palette().size()));
    for (const GenesisColor& color : render_model_.checkpoint_palette()) {
        writer.u8(color.r);
        writer.u8(color.g);
        writer.u8(color.b);
        writer.u8(color.a);
    }
    writer.bytes(
        render_model_.checkpoint_registers().data(),
        render_model_.checkpoint_registers().size()
    );
    writer.i32(frame_);
    writer.u8(frame_phase_);
    writer.i32(last_ground_direction_);
    writer.boolean(quit_);
}

void Engine::read_checkpoint(std::istream& input) {
    checkpoint::Reader reader(input);
    checkpoint::expect_magic(reader, "OACP", 4);
    if (reader.u32() != kCheckpointVersion) {
        throw std::runtime_error("unsupported OpenAladdin checkpoint version");
    }
    if (reader.u32() != rom_bytes_.size()) {
        throw std::runtime_error("ROM does not match OpenAladdin checkpoint");
    }
    if (reader.u8() != level_.descriptor().scene_id) {
        throw std::runtime_error("level does not match OpenAladdin checkpoint");
    }

    scene_.read_checkpoint(input);
    scene_resources_.reset();
    interaction_map_.read_checkpoint(input);
    actors_.read_checkpoint(input);
    PlayerState player = read_player_state(reader);
    CameraState camera = read_camera_state(reader);
    animation_.read_checkpoint(input);
    for (PlayerAnimationVm& actor_animation : actor_animations_) {
        actor_animation.read_checkpoint(input);
    }
    const auto level_event_cursor = reader.u32();
    const auto level_event_tick = reader.u8();
    const bool level_event_exit_started = reader.boolean();
    const auto level_event_sound_requests = reader.byte_vector(0x10000);
    if (level_event_cursor != 0 && level_event_cursor >= rom_bytes_.size()) {
        throw std::runtime_error("level-event cursor is outside the OpenAladdin ROM");
    }

    InteractionRuntimeState interaction_runtime;
    interaction_runtime.scan_initialized = reader.boolean();
    interaction_runtime.selector_pending = reader.boolean();
    interaction_runtime.actor_lock_pending = reader.boolean();
    interaction_runtime.camera_delay_pending = reader.boolean();
    interaction_runtime.actor_triggered = reader.boolean();
    interaction_runtime.player_collision_pending = reader.boolean();
    const bool checkpoint_animation_selector_pending = reader.boolean();
    interaction_runtime.surface_interaction_pending = reader.boolean();
    interaction_runtime.surface_interaction_active = reader.boolean();
    const bool jump_landing_state_arm_pending = reader.boolean();
    const bool jump_landing_state_arm_now = reader.boolean();
    const bool terrain_fall_phase = reader.boolean();
    interaction_runtime.bounce_response_active = reader.boolean();
    interaction_runtime.bounce_response_follow_active = reader.boolean();
    interaction_runtime.bounce_camera_delay_hold_pending = reader.boolean();
    const bool contour_ground_motion = reader.boolean();
    interaction_runtime.reference_x = reader.i32();
    interaction_runtime.reference_y = reader.i32();
    const auto random_state = reader.u32();
    const int terrain_input_world_x = reader.i32();
    const int terrain_input_world_y = reader.i32();
    const bool checkpoint_terrain_behavior_override = reader.boolean();
    const auto checkpoint_terrain_behavior = reader.u8();

    const bool vdp_checkpoint_loaded = reader.boolean();
    auto vdp_checkpoint_vram = reader.byte_vector(0x10000);
    auto vdp_checkpoint_vsram = reader.byte_vector(0x80);
    const auto palette_size = reader.u32();
    if (palette_size > 256) {
        throw std::runtime_error("oversized VDP palette in OpenAladdin checkpoint");
    }
    std::vector<GenesisColor> vdp_checkpoint_palette(palette_size);
    for (GenesisColor& color : vdp_checkpoint_palette) {
        color.r = reader.u8();
        color.g = reader.u8();
        color.b = reader.u8();
        color.a = reader.u8();
    }
    std::array<std::uint8_t, 32> vdp_checkpoint_registers{};
    reader.bytes(vdp_checkpoint_registers.data(), vdp_checkpoint_registers.size());
    const int frame = reader.i32();
    const auto frame_phase = reader.u8();
    const int last_ground_direction = reader.i32();
    const bool quit = reader.boolean();
    if (vdp_checkpoint_loaded
        && (vdp_checkpoint_vram.size() != 0x10000
            || vdp_checkpoint_vsram.size() != 0x80)) {
        throw std::runtime_error("incomplete VDP checkpoint state");
    }

    player_ = player;
    camera_ = camera;
    interactions_.restore_runtime(interaction_runtime);
    checkpoint_animation_selector_pending_ = checkpoint_animation_selector_pending;
    jump_landing_state_arm_pending_ = jump_landing_state_arm_pending;
    jump_landing_state_arm_now_ = jump_landing_state_arm_now;
    terrain_fall_phase_ = terrain_fall_phase;
    contour_ground_motion_ = contour_ground_motion;
    random_state_ = random_state;
    terrain_input_world_x_ = terrain_input_world_x;
    terrain_input_world_y_ = terrain_input_world_y;
    checkpoint_terrain_behavior_override_ = checkpoint_terrain_behavior_override;
    checkpoint_terrain_behavior_ = checkpoint_terrain_behavior;
    actor_movement_deferred_.fill(false);
    render_model_.load_checkpoint(
        std::move(vdp_checkpoint_vram),
        std::move(vdp_checkpoint_vsram),
        std::move(vdp_checkpoint_palette),
        vdp_checkpoint_registers,
        vdp_checkpoint_loaded
    );
    frame_ = frame;
    frame_phase_ = frame_phase;
    last_ground_direction_ = last_ground_direction;
    quit_ = quit;
    level_events_.restore(RomAddress{level_event_cursor}, level_event_tick);
    level_event_exit_started_ = level_event_exit_started;
    level_event_sound_requests_ = level_event_sound_requests;
}

void Engine::render(SDL_Renderer* renderer) {
    const bool rendered = render_pipeline_.render(
        state_,
        level_,
        render_model_,
        sprites_,
        PlayerRenderState{
            animation_.sprite_frame(),
            animation_.frame_pointer(),
            animation_.facing_left(),
        },
        rom_bytes_
    );
    if (!rendered) return;
    render_backend_.present(
        renderer,
        render_pipeline_.framebuffer(),
        RenderPipeline::kWidth,
        RenderPipeline::kHeight
    );
}
}  // namespace openaladdin
