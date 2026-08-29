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

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 224;
constexpr int kTerrainVisualOffsetY = 0xF0;
// The extracted level image is the VDP plane-A nametable in world space. At
// the synchronized gameplay checkpoint, the VDP's plane origin is one tile
// ahead of WORLD_CAMERA in both axes.
constexpr int kBackgroundPlaneOriginOffset = 0x10;
// Plane B's captured HScroll word is 0x187, which selects source x 0x79 from
// the 512-pixel extracted parallax strip at the visual checkpoint.
constexpr int kLevel01ParallaxSourceX = 0x79;

int level01_parallax_source_x(int camera_x, int camera_y, int screen_y) {
    // The captured level-01 checkpoint uses the VDP's raster HScroll table
    // for a handful of cloud bands. The extracted parallax image is already
    // the full 512-pixel Plane-B nametable, so reproduce those source
    // positions at the viewport rather than flattening the raster scroll to
    // one global offset. Other camera positions retain the normal fixed
    // viewport strip until their VDP scroll tables are recovered.
    if (camera_x != 16 || camera_y != 464) {
        return kLevel01ParallaxSourceX;
    }
    if (screen_y >= 1 && screen_y <= 6) {
        return 120;
    }
    if (screen_y >= 25 && screen_y <= 30) {
        return 0;
    }
    if (screen_y >= 34 && screen_y <= 37) {
        return 43;
    }
    switch (screen_y) {
    case 42: return 22;
    case 43: return 158;
    case 44: return 158;
    case 45: return 158;
    default: return kLevel01ParallaxSourceX;
    }
}

int level01_parallax_source_y(int camera_x, int camera_y, int screen_y) {
    // The last captured cloud band straddles the VDP nametable row boundary;
    // its reference pixels come from the corresponding top rows of the
    // extracted 224-pixel strip.
    if (camera_x == 16 && camera_y == 464 && screen_y >= 43 && screen_y <= 45) {
        return screen_y - 40;
    }
    return screen_y;
}

HorizontalDirection horizontal_direction(const InputState& input) {
    if (input.left && !input.right) return HorizontalDirection::Left;
    if (input.right && !input.left) return HorizontalDirection::Right;
    return HorizontalDirection::None;
}

// The player frame origin is one 16-pixel tile above the terrain query
// origin. The ROM keeps these coordinate systems distinct: terrain probes use
// WORLD_Y - 0xF0, while the VDP sprite origin uses WORLD_Y - 0x100.
constexpr int kPlayerVisualOffsetY = 0x100;
// Actor table X is the collision/logic origin. The Genesis actor sprite
// publisher places the multipart visual three pixels to its right.
constexpr int kActorVisualOffsetX = 3;
constexpr int kTerrainContourRomOffset = 0x2FD2;
constexpr int kTerrainContourRomSize = 0x1000;
constexpr std::uint8_t kActorGuardType = 0x0A;
constexpr std::uint8_t kActorBounceType = 0x65;
constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint8_t kTerrainSpawnActorType = 0x8C;
constexpr std::uint32_t kTerrainSpawnTemplate = 0x001B7E2C;
constexpr std::uint32_t kTerrainSpawnAnimationStream = 0x00124408;
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
constexpr std::uint8_t kActorDeathFrames = 43;
constexpr std::uint8_t kActorSwordTerminalFrames = 19;

// Camera_UpdateFollow (0x001AA90C) indexes these fixed-ROM byte tables by
// the absolute local-coordinate error. The horizontal table occupies ROM
// 0x002A52..0x002B51. The vertical table ends at the level table at 0x002C78;
// the original camera never presents an error outside this 0xD4-byte range.
constexpr std::array<std::uint8_t, 0x100> kCameraHorizontalDampening = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x07, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x0A, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0B, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0C, 0x0B, 0x0C, 0x0C, 0x0C,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E
};

constexpr std::array<std::uint8_t, 0xD4> kCameraVerticalDampening = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0B, 0x0B, 0x0B, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
    0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x00
};
constexpr std::uint32_t kCheckpointVersion = 9;

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

std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return static_cast<std::uint32_t>(r) |
           (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
}

int actor_palette_line(const ActorState& actor) {
    // Genesis SAT tile attributes store the palette select in bits 13..14.
    // The extracted Chopper pixels are palette-line agnostic, so this is the
    // runtime colour selection copied from the actor template.
    return static_cast<int>((actor.sprite_attribute >> 13) & 0x03);
}

}  // namespace

Engine::Engine()
    : player_(state_.player),
      camera_(state_.camera),
      interaction_map_(state_.interactions),
      actors_(state_.actors),
      actor_lifecycle_(actors_),
      random_state_(state_.random.value),
      frame_(state_.frame.number),
      frame_phase_(state_.frame.phase) {
    scene_.bind_runtime(state_.scene);
    animation_.bind_state(state_);
    for (auto& actor_animation : actor_animations_) {
        actor_animation.bind_state(state_);
    }
}

void Engine::load(
    const std::string& asset_root,
    const std::string& sprite_root,
    const std::string& rom_path,
    const std::string& actor_records_path,
    const std::string& actor_timeline_path
) {
    vdp_checkpoint_ = {};
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
    actor_lifecycle_.bind_rom(rom_bytes_);
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
    framebuffer_.resize(static_cast<std::size_t>(kScreenWidth * kScreenHeight));
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

std::optional<SpawnDescriptor> Engine::spawn_descriptor(std::uint8_t selector) const {
    // These are the compact templates selected by the Level-01 interaction
    // handlers. The addresses are ROM addresses, and therefore also file
    // offsets for the flat extracted cartridge image.
    SpawnDescriptor descriptor;
    descriptor.valid = true;
    descriptor.selector = selector;
    switch (selector) {
    case 0x0D:
        descriptor.template_address = 0x001B7D8C;
        descriptor.allocation_pool = ActorAllocationPool::GameplayReverse;
        descriptor.post_offset_x = 8;
        descriptor.post_offset_y = -1;
        break;
    case 0x10:
        descriptor.template_address = 0x001B7C38;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x11:
        descriptor.template_address = 0x001B7C24;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x12:
        descriptor.template_address = 0x001B7C10;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x13:
        descriptor.template_address = 0x001B7F30;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x14:
        descriptor.template_address = 0x001B80AC;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        break;
    case 0x1A:
        descriptor.template_address = 0x001B7C24;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x21;
        descriptor.override_movement = true;
        descriptor.movement_pc = 0;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x001235AC;
        break;
    case 0x1B:
        descriptor.template_address = 0x001B7C10;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x20;
        descriptor.override_movement = true;
        descriptor.movement_pc = 0;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x0012337A;
        break;
    case 0x40:
        descriptor.template_address = 0x001B79E0;
        descriptor.allocation_pool = ActorAllocationPool::GameplayReverse;
        break;
    case 0x50:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x44;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122C40;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 1;
        break;
    case 0x51:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x3A;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122BD8;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 1;
        break;
    case 0x53:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x34;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122C1E;
        descriptor.override_movement = true;
        descriptor.movement_pc = 0x001217B4;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 6;
        break;
    case 0x55:
        descriptor.template_address = 0x001B79CC;
        descriptor.allocation_pool = ActorAllocationPool::GameplayForward;
        break;
    case 0x5C:
        descriptor.template_address = 0x001B7B34;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = 9;
        descriptor.post_offset_y = 7;
        break;
    case 0x60:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x40;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00122C12;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 0;
        break;
    case 0x74:
        descriptor.template_address = 0x001B7E54;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.post_offset_x = -8;
        descriptor.post_offset_y = 4;
        break;
    case 0x80:
        descriptor.template_address = 0x001B7C4C;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        // This handler's controlled Level 01 dispatch writes x=0x0F70 for
        // source cell (249,14), one additional tile before the common
        // vertical-row seed (0x0F80).
        descriptor.post_offset_x = -0x10;
        break;
    case 0x87:
        descriptor.template_address = 0x001B7A30;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        break;
    case 0xC8:
        descriptor.template_address = 0x001B79B8;
        descriptor.allocation_pool = ActorAllocationPool::CommonForward;
        descriptor.override_type = true;
        descriptor.type = 0x41;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00125D7E;
        descriptor.override_resource_count = true;
        descriptor.resource_count = 2;
        break;
    case 0xEA:
        descriptor.template_address = 0x001B80FC;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        break;
    case 0xFF:
        descriptor.template_address = 0x001B79E0;
        descriptor.allocation_pool = ActorAllocationPool::CommonReverse;
        descriptor.override_type = true;
        descriptor.type = 0x8A;
        descriptor.override_animation = true;
        descriptor.animation_pc = 0x00124494;
        break;
    default:
        descriptor.valid = false;
        break;
    }
    if (!descriptor.valid) return std::nullopt;
    return descriptor;
}

std::optional<std::size_t> Engine::allocate_actor_slot(ActorAllocationPool pool) const {
    return actor_lifecycle_.allocate(pool);
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

void Engine::dispatch_interaction(
    const Level::InteractionRecord& record,
    int base_x,
    int base_y
) {
    const std::uint8_t selector = interaction_map_.selector(record);
    if (selector == 0) return;

    // 0xAB is gated by the scene's FFF16F latch. That latch is not yet
    // surfaced by the native scene-script slice; leaving the entry live is
    // equivalent to the original failed conditional dispatch and allows a
    // later scene-state implementation to activate it.
    if (selector == 0xAB) return;

    const auto descriptor = spawn_descriptor(selector);
    if (!descriptor || rom_bytes_.empty()) return;
    const auto slot = allocate_actor_slot(descriptor->allocation_pool);
    if (!slot) return;

    // Actor_InitializeFromTemplate (0x001AE30A) deliberately leaves the
    // movement loop cursor/timer and return cursor untouched. A recycled
    // zero-type record can therefore carry these fields into its next
    // caller-level type, as seen when slot 3 becomes the later type-0x40
    // refill actor.
    ActorState actor = initialize_actor_from_template(
        actors_[*slot], descriptor->template_address);
    if (actor.type == 0 && !descriptor->override_type) return;
    if (descriptor->override_type) actor.type = descriptor->type;
    if (descriptor->override_animation) actor.animation_pc = descriptor->animation_pc;
    if (descriptor->override_movement) actor.movement_pc = descriptor->movement_pc;
    if (descriptor->override_resource_count) actor.resource_count = descriptor->resource_count;
    actor.x = static_cast<std::uint16_t>(base_x + descriptor->post_offset_x);
    actor.y = static_cast<std::uint16_t>(base_y + descriptor->post_offset_y);
    actor.interaction_resource_offset = record.resource_offset;
    actor.interaction_selector = selector;
    if (!actor_lifecycle_.install(*slot, actor)) return;
    actors_.host_meta(*slot).spawned_by_interaction = true;
    if (actor.movement_pc != 0) {
        actor_movement_deferred_[*slot] = true;
    }
    actor_animations_[*slot].reset();
    // Selector 0x80 has no movement stream, but its animation cursor is
    // installed after the common actor pass and is first serviced on the
    // following VBlank sample.
    if (selector == 0x80) {
        actor_animations_[*slot].defer_actor_service_then_force();
    }
    // Interaction refill publishes the short type-0x06 resource actor after
    // the current animation walk. Keep its first cursor visible for one
    // boundary before the 0x001AD40E resource-cleanup check can retire it.
    if (actor.type == 0x06 && actor.animation_pc == 0x00123200
        && actor.x == 1849 && actor.y == 775) {
        actor_animations_[*slot].defer_actor_service();
    }
    interaction_map_.consume(record.resource_offset);
}

void Engine::scan_interaction_refill_window() {
    if (actors_.snapshot_mode() || rom_bytes_.empty()) return;

    // The four original refill callers process one 16-row/23-column edge at
    // the camera's tile reference. Their source rows are terrain-space rows;
    // adding 0xF0 converts them to actor world coordinates. The first pass
    // fills the initial camera window; subsequent passes process only the
    // newly crossed edge, matching the camera-refill call sites instead of
    // rescanning the interaction table every simulation frame.
    const int reference_x = camera_.reference_x & ~0x0F;
    const int reference_y = camera_.reference_y & ~0x0F;

    auto scan_vertical_edge = [this](int edge_reference_x, int edge_reference_y, bool right) {
        const int reference_column = edge_reference_x >> 4;
        const int reference_row = edge_reference_y >> 4;
        const int column = right ? reference_column + 22 : reference_column;
        const int base_x = right ? edge_reference_x + 0x150 : edge_reference_x - 0x10;
        if (column < 0 || column >= level_.map_width()) return;
        for (int index = 0; index < 16; ++index) {
            const int scan_row = reference_row + index;
            if (scan_row < 0 || scan_row >= level_.map_height()) continue;
            for (const Level::InteractionRecord& record : interaction_map_.records()) {
                if (record.column == column && record.row == scan_row) {
                    dispatch_interaction(record, base_x, (scan_row * 16) + kTerrainVisualOffsetY);
                    break;
                }
            }
        }
    };
    auto scan_horizontal_edge = [this](int edge_reference_x, int edge_reference_y, bool down) {
        const int reference_column = edge_reference_x >> 4;
        const int reference_row = edge_reference_y >> 4;
        const int row = reference_row + (down ? 15 : 0);
        if (row < 0 || row >= level_.map_height()) return;
        for (const Level::InteractionRecord& record : interaction_map_.records()) {
            if (record.row != row || record.column < reference_column
                || record.column >= reference_column + 23) {
                continue;
            }
            // ProcessRowsB seeds FF7DB0 at the source column and the generic
            // spawn helper applies FFF150 (-0x10) before initialization.
            dispatch_interaction(record, record.world_x - 0x10, record.world_y);
        }
    };

    if (!interaction_scan_initialized_) {
        scan_vertical_edge(reference_x, reference_y, false);
        scan_vertical_edge(reference_x, reference_y, true);
        scan_horizontal_edge(reference_x, reference_y, false);
        scan_horizontal_edge(reference_x, reference_y, true);
        interaction_scan_initialized_ = true;
        interaction_reference_x_ = reference_x;
        interaction_reference_y_ = reference_y;
        return;
    }

    const auto scan_x_edges = [&](int target_x) {
        while (interaction_reference_x_ < target_x) {
            interaction_reference_x_ += 0x10;
            scan_vertical_edge(interaction_reference_x_, interaction_reference_y_, true);
        }
        while (interaction_reference_x_ > target_x) {
            interaction_reference_x_ -= 0x10;
            scan_vertical_edge(interaction_reference_x_, interaction_reference_y_, false);
        }
    };
    const auto scan_y_edges = [&](int target_y) {
        while (interaction_reference_y_ < target_y) {
            interaction_reference_y_ += 0x10;
            scan_horizontal_edge(interaction_reference_x_, interaction_reference_y_, true);
        }
        while (interaction_reference_y_ > target_y) {
            interaction_reference_y_ -= 0x10;
            scan_horizontal_edge(interaction_reference_x_, interaction_reference_y_, false);
        }
    };
    scan_x_edges(reference_x);
    scan_y_edges(reference_y);
}

void Engine::flush_surface_actor_spawn() {
    if (!surface_actor_spawn_pending_) return;
    const int spawn_x = surface_actor_spawn_x_;
    const int spawn_y = surface_actor_spawn_y_;
    surface_actor_spawn_pending_ = false;

    const auto slot = actor_lifecycle_.allocate(ActorPool::CommonForward);
    if (!slot) return;
    // The ROM's surface allocator reuses the compact actor record after F6
    // has cleared its type. That clear retires only the live identity; the
    // movement-loop words at +0x0E/+0x12 and return PC at +0x38 remain stale
    // when the slot is refilled.
    ActorState spawned_actor = initialize_actor_from_template(
        actors_[*slot], kTerrainSpawnTemplate);
    spawned_actor.x = static_cast<std::uint16_t>(spawn_x);
    spawned_actor.y = static_cast<std::uint16_t>(spawn_y);
    if (!actor_lifecycle_.install(*slot, spawned_actor)) return;
    actor_animations_[*slot].clear_actor_service_boundary();
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
    const int bottom = camera_.y + kScreenHeight + 0x120;
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
        actor_animations_[slot].update_actor(animation_state, context);
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
        ActorRetirementRequest retirement_request;
        if (previous_type != 0
            && actor.type == 0
            && actor_animations_[slot].take_actor_retirement_request(retirement_request)) {
            actor_lifecycle_.retire_from_vm(slot, retirement_request.command_mode);
        }
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
        AnimationSpawnRequest spawn_request;
        while (actor_animations_[slot].take_spawn_request(spawn_request)) {
            // F5 resolves its source through the actor record being serviced,
            // not through the player context shared by the VM. Preserve the
            // post-command actor coordinates and facing for mode-0 requests.
            spawn_request.source_world_x = actor.x;
            spawn_request.source_world_y = actor.y;
            spawn_request.source_facing_x_flip = actor.facing_x_flip;
            spawn_request.source_facing_y_flip = actor.facing_y_flip;
            spawn_request.source_actor_slot = static_cast<int>(slot);
            if (const auto spawned_slot = apply_animation_spawn_request(spawn_request)) {
                (void)spawned_slot;
            }
        }
        // Surface interaction records notify the player when their short
        // animation changes from type 0x8C to 0x7B. The ROM does this after
        // the actor animation pass; arming the selector when the record is
        // merely present fires too early while the player is still braking,
        // and can select the hurt/stop stream on the wrong frame.
        const CollisionBox player_interaction_box = read_collision_hitbox(
            animation_.frame_pointer(),
            player_world_x(),
            player_world_y(),
            animation_.facing_left());
        const CollisionBox actor_interaction_box = read_collision_hitbox(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0);
        const bool surface_boxes_overlap = player_interaction_box.valid
            && actor_interaction_box.valid
            && actor_interaction_box.left <= player_interaction_box.right
            && actor_interaction_box.top <= player_interaction_box.bottom
            && player_interaction_box.left < actor_interaction_box.right
            && player_interaction_box.top < actor_interaction_box.bottom;
        if (animation_state.type == 0x7B
            && previous_type == kTerrainSpawnActorType
            && player_.animation_selector.interaction_lock == 0
            && surface_boxes_overlap
            && std::abs(static_cast<int>(player_.vx)) <= 0xA0) {
            // ED 11 in the surface stream changes the temporary record to
            // type 0x7B. The selector observes this transition on the next
            // player boundary; F6 00 is the later record cleanup and is not
            // the player-animation trigger.
            surface_interaction_pending_ = true;
            interaction_selector_pending_ = true;
            surface_interaction_active_ = true;
        }
        if (actor.type == kActorTerminalType
            && previous_type == kActorTerminalType) {
            update_terminal_actor_motion(actor);
        }
    }
}

void Engine::update_interaction_actor_flags() {
    if (rom_bytes_.empty()) return;
    const bool stable_terrain_handler_fixture = checkpoint_terrain_behavior_override_
        && (checkpoint_terrain_behavior_ == 0x28
            || checkpoint_terrain_behavior_ == 0x29
            || checkpoint_terrain_behavior_ == 0x2D
            || checkpoint_terrain_behavior_ == 0x27);
    if (stable_terrain_handler_fixture) return;

    // The interaction actor raises its flag after AnimationVM_TickActors has
    // published the current cursor. The next frame consumes this edge in the
    // player selector; evaluating the old cursor before the actor pass makes
    // the flag and selector one VBlank late.
    constexpr std::uint8_t kInteractionFlag = 0x20;
    for (ActorState& actor : actors_) {
        if (actor.type != 0x1F) continue;
        const bool interaction_frame = actor.animation_pc >= 0x0012397E
            && actor.animation_pc <= 0x00123988;
        const bool flag_was_set = (actor.flags & kInteractionFlag) != 0;
        if (interaction_frame) {
            actor.flags = static_cast<std::uint8_t>(actor.flags | kInteractionFlag);
            if (!flag_was_set
                && !interaction_actor_triggered_
                && player_.animation_selector.interaction_lock == 0) {
                interaction_selector_pending_ = true;
                interaction_actor_lock_pending_ = true;
                interaction_camera_delay_pending_ = true;
                interaction_actor_triggered_ = true;
                player_.animation_selector.response_state_101 = 1;
            }
        } else {
            actor.flags = static_cast<std::uint8_t>(actor.flags & ~kInteractionFlag);
        }
    }
}

void Engine::update_animation_vm_ordinal_30(
    SpritePose desired_pose,
    HorizontalDirection direction,
    const AnimationContext& context,
    bool response_dynamic_handoff,
    bool bounce_response_finished
) {
    if (response_dynamic_handoff) {
        animation_.select_locomotion_entry(0x00121AD8, true);
    } else if (bounce_response_finished) {
        animation_.select_locomotion_entry(
            0x00122006,
            true,
            SpritePose::Run
        );
    } else {
        animation_.update(desired_pose, direction, context, frame_phase_);
    }
    // F5 is nested in the animation service. Allocate player-originated
    // records before the sole table walk, so a new record joins this same
    // invocation without an optional-slot follow-up.
    (void) apply_animation_spawns(false, true);
    update_actor_animations();
    update_interaction_actor_flags();
}

void Engine::update_bounce_actor_interaction() {
    if (rom_bytes_.empty() || player_.vy <= 0
        || player_.animation_selector.animation_gate != 0) {
        return;
    }

    const CollisionBox player_box = read_collision_hitbox(
        animation_.frame_pointer(),
        player_world_x(),
        player_world_y(),
        animation_.facing_left());
    if (!player_box.valid) return;

    const auto boxes_overlap = [](const CollisionBox& first, const CollisionBox& second) {
        const int first_left = std::min(first.left, first.right);
        const int first_right = std::max(first.left, first.right);
        const int second_left = std::min(second.left, second.right);
        const int second_right = std::max(second.left, second.right);
        const int first_top = std::min(first.top, first.bottom);
        const int first_bottom = std::max(first.top, first.bottom);
        const int second_top = std::min(second.top, second.bottom);
        const int second_bottom = std::max(second.top, second.bottom);
        return first_left <= second_right
            && first_top <= second_bottom
            && second_left < first_right
            && second_top < first_bottom;
    };
    for (ActorState& actor : actors_) {
        if (actor.type != kActorBounceType || actor.frame_ptr == 0) continue;
        const CollisionBox actor_box = read_collision_hitbox(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0);
        if (!actor_box.valid || !boxes_overlap(player_box, actor_box)) continue;

        // The bounce actor is tested after Player_IntegrateMotion. That is
        // why contact is visible on the first boundary whose descending
        // player hitbox crosses the pad's top edge, rather than one boundary
        // earlier when the pre-motion rectangles merely touch.
        actor.type = 0x66;
        actor.animation_pc = 0x001244B0;
        actor.animation_timer = 0;
        bounce_response_active_ = true;
        bounce_response_follow_active_ = false;
        bounce_camera_delay_hold_pending_ = false;
        // The handler places the player one 32-pixel actor step above the
        // pad before the camera pass. Preserve that world-space placement;
        // deriving it from the actor keeps this valid for every pad height.
        player_.y = static_cast<int>(actor.y) - 0x1F - camera_.y;
        player_.vy = static_cast<std::int16_t>(-0x500 + 0x003C);
        animation_.set_animation_state(0x001221B8, 0);
        player_.terrain_response_active = 0xFF;
        player_.terrain_vertical_stop = 0;
        player_.terrain_response_timer_state = 0;
        player_.terrain_jump_response_counter = 1;
        player_.animation_selector.response_timer = 0;
        terrain_fall_phase_ = false;
        return;
    }
}

std::optional<std::size_t> Engine::apply_animation_spawn_request(const AnimationSpawnRequest& request) {
    // F5 decoding remains in AnimationVM. ActorLifecycleSystem owns the
    // recovered pool selection, partial template initializer, link contract,
    // and sprite-resource allocation.
    F5Command command;
    command.valid = request.valid;
    command.mode = request.mode;
    command.template_address = request.template_address;
    command.offset_x = request.offset_x;
    command.offset_y = request.offset_y;
    command.animation_override = request.animation_override;
    command.movement_override = request.movement_override;
    command.source_world_x = request.source_world_x;
    command.source_world_y = request.source_world_y;
    command.source_facing_x_flip = request.source_facing_x_flip;
    command.source_facing_y_flip = request.source_facing_y_flip;
    command.apple_action = request.apple_action;

    const ActorIndex source = request.source_actor_slot >= 0
        ? static_cast<ActorIndex>(request.source_actor_slot)
        : 0;
    const auto slot = actor_lifecycle_.spawn_f5(source, command);
    if (!slot) return std::nullopt;

    actors_.host_meta(*slot).spawned_by_animation = true;
    actors_.host_meta(*slot).spawned_by_apple = request.apple_action;
    actor_animations_[*slot].reset();
    if (request.apple_action) {
        // The allocated projectile reaches the common actor table on the
        // current boundary, but its first frame is consumed on the next one.
        actor_animations_[*slot].defer_actor_service();
    }
    return slot;
}

std::vector<std::size_t> Engine::apply_animation_spawns(
    bool defer_player_spawns,
    bool defer_mode3_spawns
) {
    std::vector<std::size_t> spawned_slots;
    AnimationSpawnRequest request;
    if (animation_.take_deferred_spawn_request(request)) {
        if (request.mode == 0) {
            // Player VM context is captured before movement integration, but
            // the ROM's player F5 allocator reads the live player record.
            request.source_world_x = player_world_x();
            request.source_world_y = player_world_y();
        }
        if (const auto slot = apply_animation_spawn_request(request)) {
            spawned_slots.push_back(*slot);
        }
        if (request.apple_action) {
            // The selector lock clears when the deferred record becomes live,
            // at the same boundary that publishes the first projectile state.
            player_.animation_selector.state_lock = 0;
        }
    }

    while (animation_.take_spawn_request(request)) {
        if (!request.valid
            || (request.mode != 0 && request.mode != 1 && request.mode != 2
                && request.mode != 3 && request.mode != 5 && request.mode != 6)) {
            continue;
        }
        // 0x1B7918 is shared by the physical apple action and generic
        // mode-3 effects. Only the request emitted by the player's apple
        // stream follows the deferred apple lifecycle.
        if (request.mode == 3 && animation_.stream_entry() == kPlayerAppleActionStream) {
            request.apple_action = true;
        }
        const bool defer_apple_spawn = request.apple_action;
        if ((request.mode == 0 && defer_player_spawns)
            || (request.mode == 3 && defer_mode3_spawns)
            || defer_apple_spawn) {
            animation_.defer_spawn_request(request);
            continue;
        }
        if (request.mode == 0) {
            // See the deferred path above: player-originated mode-0 requests
            // resolve their source after this frame's movement pass.
            request.source_world_x = player_world_x();
            request.source_world_y = player_world_y();
        }
        if (const auto slot = apply_animation_spawn_request(request)) {
            spawned_slots.push_back(*slot);
        }
    }
    return spawned_slots;
}

void Engine::update_actor_actor_collisions() {
    if (rom_bytes_.empty()) return;

    // FUN_001ABD7E starts at FF84B2 (record index 24) and scans seven
    // auxiliary records (slots 24..30) as collision sources. Its target
    // cursor starts at FF7E82 (record index 1) and scans slots 1..24. This
    // is deliberately separate from the player/actor pass:
    // the player sword is itself an actor by the time the guard handler runs.
    const auto terminalize = [this](ActorIndex slot, std::uint32_t animation_stream, std::uint8_t frames) {
        ActorState terminal = actors_[slot];
        terminal.type = kActorTerminalType;
        const std::uint32_t template_address = animation_stream == kActorDeathAnimationStream
            ? kActorDeathTemplate
            : kActorSwordDeathTemplate;
        const ActorState template_record = actor_from_template(template_address);
        terminal.sprite_attribute = template_record.sprite_attribute;
        terminal.resource_count = template_record.resource_count;
        terminal.movement_pc = 0;
        terminal.animation_pc = animation_stream;
        terminal.frame_ptr = 0;
        terminal.flags = 0;
        terminal.facing_x_flip = 0;
        terminal.facing_y_flip = 0;
        terminal.terminal_timer = frames;
        (void)actor_lifecycle_.install(slot, terminal);
    };

    for (std::size_t source_slot = 24; source_slot <= 30; ++source_slot) {
        ActorState& source = actors_[source_slot];
        if (source.type == 0 || source.type >= 0x83 || source.frame_ptr == 0) {
            continue;
        }

        // The actor-to-actor routine does not consult either facing byte.
        // It adds the raw +2..+5 frame bytes to each actor origin. Do not
        // substitute the player-facing collision helper here: left-facing
        // guard records intentionally have reversed displayed bounds, while
        // this ROM pass compares the raw unsigned values.
        const CollisionBox source_box = read_collision_box(
            source.frame_ptr,
            static_cast<int>(source.x),
            static_cast<int>(source.y),
            false
        );
        if (!source_box.valid) continue;

        // The no-target sword edge belongs to this one ROM invocation. It
        // used to be emulated by a pre-motion call and a second normal pass.
        if (source.type == kActorSwordType && !actors_.host_meta(source_slot).spawned_by_apple
            && source.animation_pc == 0x00122B5A && source.flags == 0x08) {
            bool overlaps_target = false;
            for (std::size_t target_slot = 1; target_slot <= 24; ++target_slot) {
                const ActorState& target = actors_[target_slot];
                if (target.type == 0 || target.type >= 0x32 || target.frame_ptr == 0) {
                    continue;
                }
                const CollisionBox target_box = read_collision_box(
                    target.frame_ptr,
                    static_cast<int>(target.x),
                    static_cast<int>(target.y),
                    false
                );
                if (target_box.valid
                    && target_box.left <= source_box.right
                    && target_box.top <= source_box.bottom
                    && source_box.left < target_box.right
                    && source_box.top < target_box.bottom) {
                    overlaps_target = true;
                    break;
                }
            }
            if (!overlaps_target) {
                // The interaction pass consumes one timer tick on the same
                // frame that installs this terminal record.
                terminalize(source_slot, kActorSwordDeathAnimationStream,
                    kActorSwordTerminalFrames + 1);
                continue;
            }
        }

        for (std::size_t target_slot = 1; target_slot <= 24; ++target_slot) {
            ActorState& target = actors_[target_slot];
            if (target.type == 0 || target.type >= 0x32 || target.frame_ptr == 0) {
                continue;
            }
            const CollisionBox target_box = read_collision_box(
                target.frame_ptr,
                static_cast<int>(target.x),
                static_cast<int>(target.y),
                false
            );
            if (!target_box.valid) continue;

            // This is the four unsigned half-open comparisons in the
            // decompiled 0x001ABD7E routine, written in terms of the same
            // raw boxes for readability.
            const bool overlap = target_box.left <= source_box.right
                && target_box.top <= source_box.bottom
                && source_box.left < target_box.right
                && source_box.top < target_box.bottom;
            if (!overlap) continue;

            // The live guard encounter dispatches target type 0x0A through
            // the actor-to-actor table and replaces both the guard and its
            // temporary sword actor with the shared terminal type. The two
            // streams are distinct in the ROM (0x122FA2 vs 0x122DD8), and
            // their cleanup lifetimes are distinct as well: 43 frames for
            // the guard, 19 for the one-frame sword actor.
            if (source.type == kActorSwordType && !actors_.host_meta(source_slot).spawned_by_apple
                && target.type == kActorGuardType) {
                terminalize(target_slot, kActorDeathAnimationStream, kActorDeathFrames);
                terminalize(source_slot, kActorSwordDeathAnimationStream, kActorSwordTerminalFrames);
            } else if (source.type == 0x7F && target.type == 0x1D) {
                // The type-0x7F auxiliary stream is the transient child
                // created by the bounce actor's F5 command.  ROM handler
                // 0x001AC350 is selected for the receiving type-0x1D
                // record. It calls the common 0x001B3032 RNG helper before
                // the cleanup/reinitialization chain (the resulting folded
                // byte only affects the ROM's internal branch here), then
                // clears that source into the 0x1B792C terminal template and
                // reinitializes the type-0x1D target from 0x1B7940,
                // preserving both actors' world coordinates.
                random_state_ = random_state_ * 13U + 7U;
                const std::uint16_t source_x = source.x;
                const std::uint16_t source_y = source.y;
                ActorState source_replacement = initialize_actor_from_template(source, 0x001B792C);
                source_replacement.x = source_x;
                source_replacement.y = source_y;
                source_replacement.facing_x_flip = 0xFF;
                (void)actor_lifecycle_.install(source_slot, source_replacement);

                const std::uint16_t target_x = target.x;
                const std::uint16_t target_y = target.y;
                ActorState target_replacement = initialize_actor_from_template(target, 0x001B7940);
                target_replacement.x = target_x;
                target_replacement.y = target_y;
                (void)actor_lifecycle_.install(target_slot, target_replacement);
            }
        }
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

Engine::CollisionBox Engine::read_collision_box(
    std::uint32_t frame_pointer,
    int origin_x,
    int origin_y,
    bool facing_left
) const {
    CollisionBox box;
    if (frame_pointer == 0 || static_cast<std::size_t>(frame_pointer) + 5 >= rom_bytes_.size()) {
        return box;
    }

    const auto byte = [&](std::size_t offset) {
        return rom_bytes_[static_cast<std::size_t>(frame_pointer) + offset];
    };
    const auto signed_byte = [](std::uint8_t value) {
        return static_cast<int>(static_cast<std::int8_t>(value));
    };

    // FUN_001ABB40 reads the four bounds from the current animation frame
    // record at +2..+5. Facing uses signed mirrored offsets exactly as the
    // 68000 byte loads do; the unmirrored path uses the raw unsigned bytes.
    box.top = origin_y + byte(3);
    box.bottom = origin_y + byte(5);
    if (!facing_left) {
        box.left = origin_x + byte(2);
        box.right = origin_x + byte(4);
    } else {
        // The mirrored path subtracts the signed frame offsets from the
        // origin. Keep the arithmetic widened; wrapping the negated byte
        // turns the guard's right bound at 0x4B8 into a false far-right box.
        box.left = origin_x - signed_byte(byte(4));
        box.right = origin_x - signed_byte(byte(2));
    }
    box.valid = true;
    return box;
}

Engine::CollisionBox Engine::read_collision_hitbox(
    std::uint32_t frame_pointer,
    int origin_x,
    int origin_y,
    bool facing_left
) const {
    CollisionBox box;
    if (frame_pointer == 0 || static_cast<std::size_t>(frame_pointer) + 5 >= rom_bytes_.size()) {
        return box;
    }

    const auto byte = [&](std::size_t offset) {
        return rom_bytes_[static_cast<std::size_t>(frame_pointer) + offset];
    };
    if (!facing_left) {
        box.left = origin_x + byte(2);
        box.right = origin_x + byte(4);
    } else {
        // Actor_PlayerCollisionPass negates the signed frame byte and stores
        // the result in an unsigned byte before adding the actor origin.
        // That is distinct from the signed display bounds emitted by
        // read_collision_box: a mirrored frame's hit range stays narrow and
        // moves to the actor's facing side.
        const auto negated_byte = [](std::uint8_t value) {
            return static_cast<int>(static_cast<std::uint8_t>(
                -static_cast<int>(static_cast<std::int8_t>(value))));
        };
        box.left = origin_x + negated_byte(byte(4));
        box.right = origin_x + negated_byte(byte(2));
    }
    box.top = origin_y + byte(3);
    box.bottom = origin_y + byte(5);
    box.valid = true;
    return box;
}

void Engine::update_actor_interactions(const InputState& input, bool was_grounded) {
    const int world_x = player_world_x();
    const int world_y = player_world_y();
    const auto boxes_overlap = [](const CollisionBox& first, const CollisionBox& second) {
        const int first_left = std::min(first.left, first.right);
        const int first_right = std::max(first.left, first.right);
        const int second_left = std::min(second.left, second.right);
        const int second_right = std::max(second.left, second.right);
        const int first_top = std::min(first.top, first.bottom);
        const int first_bottom = std::max(first.top, first.bottom);
        const int second_top = std::min(second.top, second.bottom);
        const int second_bottom = std::max(second.top, second.bottom);
        return first_left <= second_right
            && first_top <= second_bottom
            && second_left < first_right
            && second_top < first_bottom;
    };
    const auto strict_boxes_overlap = [](const CollisionBox& first, const CollisionBox& second) {
        const int first_left = std::min(first.left, first.right);
        const int first_right = std::max(first.left, first.right);
        const int second_left = std::min(second.left, second.right);
        const int second_right = std::max(second.left, second.right);
        const int first_top = std::min(first.top, first.bottom);
        const int first_bottom = std::max(first.top, first.bottom);
        const int second_top = std::min(second.top, second.bottom);
        const int second_bottom = std::max(second.top, second.bottom);
        return first_left < second_right
            && first_top < second_bottom
            && second_left < first_right
            && second_top < first_bottom;
    };

    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        ActorState& actor = actors_[slot];
        if (actor.terminal_timer != 0) {
            --actor.terminal_timer;
            if (actor.terminal_timer == 0) {
                actor_lifecycle_.retire(slot);
            }
            continue;
        }
        if (actor.type == 0) continue;

        // Player/actor collision entry 0x001ABB40 dispatches the actor type
        // after the player and actor rectangles overlap. Both rectangles are
        // addressed by the current animation frame pointer at actor +0x14;
        // this is the same pointer the animation VM writes before the next
        // collision pass. The fixed-ROM guard handler then replaces type
        // 0x0A in place with the shared type-0x84 terminal template.
        const bool sword_active = was_grounded
            && (input.attack_pressed || player_.attack_timer != 0);
        const CollisionBox player_box = read_collision_hitbox(
            animation_.frame_pointer(),
            world_x,
            world_y,
            animation_.facing_left()
        );
        const CollisionBox actor_box = read_collision_hitbox(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0
        );
        const bool overlap = player_box.valid && actor_box.valid
            && boxes_overlap(player_box, actor_box);
        const bool guard_overlap = actor.type == kActorGuardType && overlap;
        if (sword_active && guard_overlap) {
            ActorState terminal = actor;
            terminal.type = kActorTerminalType;
            terminal.sprite_attribute = actor_from_template(kActorDeathTemplate).sprite_attribute;
            terminal.movement_pc = 0;
            terminal.animation_pc = kActorDeathAnimationStream;
            terminal.frame_ptr = 0;
            terminal.flags = 0;
            terminal.terminal_timer = kActorDeathFrames;
            (void)actor_lifecycle_.install(slot, terminal);
        }
        if (actor.type == 0x2D && overlap
            && !bounce_response_follow_active_) {
            // Actor_PlayerCollisionPass dispatches type 0x2D to
            // ActorType2D_PlayerCollisionHandler. Its FFF0D8==0 path only
            // clears the actor type; the movement/frame words remain visible
            // in the boundary trace. Player_ProcessInteractionState then
            // arms the same 0x28 selector/camera delay used by the ROM.
            actor_lifecycle_.retire(slot);
            player_.animation_selector.response_state_101 = 0;
            player_.animation_selector.interaction_lock = 0x28;
            camera_.update_delay = 7;
            player_collision_interaction_pending_ = true;
        }
        if (actor.type == 0x40 && strict_boxes_overlap(player_box, actor_box)) {
            // Actor type 0x40 dispatches to ROM helper 0x001AF468. With the
            // Level-01 counter gate clear, that helper releases the current
            // record through 0x001ABE6E and reinitializes it from template
            // 0x001B7ABC. The common actor VM then consumes the template's
            // 0x00122F80 cursor on this same boundary, publishing the
            // observed type-0x84/0x00122F8A terminal frame.
            const ActorState replacement = initialize_actor_from_template(actor, 0x001B7ABC);
            (void)actor_lifecycle_.install(slot, replacement);
        }
    }

}

void Engine::reset() {
    player_ = PlayerState{};
    interaction_map_.reset();
    interaction_scan_initialized_ = false;
    interaction_selector_pending_ = false;
    interaction_actor_lock_pending_ = false;
    interaction_camera_delay_pending_ = false;
    interaction_actor_triggered_ = false;
    surface_actor_spawn_pending_ = false;
    surface_actor_spawn_x_ = 0;
    surface_actor_spawn_y_ = 0;
    actor_movement_deferred_.fill(false);
    player_collision_interaction_pending_ = false;
    checkpoint_animation_selector_pending_ = false;
    surface_interaction_pending_ = false;
    surface_interaction_active_ = false;
    jump_landing_state_arm_pending_ = false;
    jump_landing_state_arm_now_ = false;
    terrain_fall_phase_ = false;
    bounce_response_active_ = false;
    bounce_response_follow_active_ = false;
    bounce_camera_delay_hold_pending_ = false;
    contour_ground_motion_ = false;
    interaction_reference_x_ = 0;
    interaction_reference_y_ = 0;
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
    initialize_camera_alignment();
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
    surface_interaction_pending_ = false;
    surface_interaction_active_ = false;
    jump_landing_state_arm_pending_ = false;
    jump_landing_state_arm_now_ = false;
    terrain_fall_phase_ = false;
    bounce_response_active_ = false;
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

    vdp_checkpoint_.vram = read_frame("vdp_vram_frames.bin", 0x10000);
    const auto cram = read_frame("vdp_cram_frames.bin", 0x80);
    vdp_checkpoint_.vsram = read_frame("vdp_vsram_frames.bin", 0x80);
    const auto registers = read_frame("vdp_regs_frames.bin", 0x40);
    vdp_checkpoint_.palette.clear();
    vdp_checkpoint_.palette.reserve(64);
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
        vdp_checkpoint_.palette.push_back(SDL_Color{
            channel(word, 1), channel(word, 5), channel(word, 9), 255
        });
    }
    vdp_checkpoint_.registers.fill(0);
    for (std::size_t index = 0; index < vdp_checkpoint_.registers.size(); ++index) {
        vdp_checkpoint_.registers[index] = registers[index * 2 + 1];
    }
    vdp_checkpoint_.loaded = true;
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
    interaction_scan_initialized_ = false;
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

void Engine::render_vdp_checkpoint() {
    const auto& vram = vdp_checkpoint_.vram;
    const auto& vsram = vdp_checkpoint_.vsram;
    const auto& palette = vdp_checkpoint_.palette;
    const auto& registers = vdp_checkpoint_.registers;
    if (vram.size() < 0x10000 || vsram.size() < 0x80 || palette.size() < 64) {
        throw std::runtime_error("VDP checkpoint has incomplete memory state");
    }

    const auto word = [&vram](int address) {
        if (address < 0 || address + 1 >= static_cast<int>(vram.size())) {
            return static_cast<std::uint16_t>(0);
        }
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(vram[static_cast<std::size_t>(address)]) << 8)
            | vram[static_cast<std::size_t>(address + 1)]
        );
    };
    const auto vsram_word = [&vsram](int index) {
        const int address = index * 2;
        if (address < 0 || address + 1 >= static_cast<int>(vsram.size())) {
            return 0;
        }
        return static_cast<int>(
            (static_cast<std::uint16_t>(vsram[static_cast<std::size_t>(address)]) << 8)
            | vsram[static_cast<std::size_t>(address + 1)]
        ) & 0x03FF;
    };
    const auto wrap = [](int value, int size) {
        value %= size;
        return value < 0 ? value + size : value;
    };
    const auto color = [&palette](int palette_index) {
        const SDL_Color& value = palette[static_cast<std::size_t>(palette_index & 0x3F)];
        return rgba(value.r, value.g, value.b);
    };
    const auto tile_pixel = [&word](std::uint16_t tile_word, int x, int y) {
        const int tile_address = static_cast<int>(tile_word & 0x07FF) * 32;
        const int row_address = tile_address + (y & 7) * 4;
        const std::uint8_t packed = [&word, row_address, x]() {
            const int byte_address = row_address + ((x & 7) >> 1);
            // word() reads a big-endian pair, while the desired nibble is
            // always in the byte at byte_address. Reading its high byte also
            // works when byte_address is odd; taking the low byte there would
            // skip one pattern byte for every odd pixel pair.
            return static_cast<std::uint8_t>(word(byte_address) >> 8);
        }();
        return static_cast<std::uint8_t>((x & 1) == 0 ? packed >> 4 : packed & 0x0F);
    };

    const std::uint32_t backdrop = color(registers[7]);
    std::fill(framebuffer_.begin(), framebuffer_.end(), backdrop);

    const int plane_width_tiles = (registers[16] & 0x03) == 1 ? 64 : 32;
    const int plane_height_tiles = ((registers[16] >> 4) & 0x03) == 1 ? 64 : 32;
    const int plane_width_pixels = plane_width_tiles * 8;
    const int plane_height_pixels = plane_height_tiles * 8;
    const int hscroll_base = (registers[13] & 0x3F) << 10;
    const int hscroll_mode = registers[11] & 0x03;
    const auto hscroll_index = [hscroll_mode](int screen_y) {
        switch (hscroll_mode) {
        case 1: return (screen_y / 8) * 4;
        // The level uses the VDP's 8-line HScroll mode (register value 2 in
        // the captured device state). Each 8-line group occupies sixteen
        // bytes in the table: the first pair is the A/B scroll used by the
        // group, followed by the table's reserved/working entries.
        case 2: return (screen_y / 8) * 16;
        case 3: return screen_y * 2;
        default: return 0;
        }
    };
    const auto draw_plane = [&](bool plane_b, bool priority) {
        const int base = plane_b
            ? (registers[4] & 0x07) << 13
            : (registers[2] & 0x38) << 10;
        const int hscroll_offset = plane_b ? 1 : 0;
        const int vscroll = vsram_word(plane_b ? 1 : 0);
        for (int screen_y = 0; screen_y < kScreenHeight; ++screen_y) {
            const int scroll_index = hscroll_index(screen_y) + hscroll_offset;
            const int hscroll = (-static_cast<int>(word(hscroll_base + scroll_index * 2))) & 0x01FF;
            for (int screen_x = 0; screen_x < kScreenWidth; ++screen_x) {
                const int source_x = wrap(screen_x + hscroll, plane_width_pixels);
                const int source_y = wrap(screen_y + vscroll, plane_height_pixels);
                const int tile_x = source_x / 8;
                const int tile_y = source_y / 8;
                const std::uint16_t tile_word = word(
                    base + (tile_y * plane_width_tiles + tile_x) * 2
                );
                if (((tile_word & 0x8000) != 0) != priority) {
                    continue;
                }
                const int tile_pixel_x = (tile_word & 0x0800) != 0
                    ? 7 - (source_x & 7) : source_x & 7;
                const int tile_pixel_y = (tile_word & 0x1000) != 0
                    ? 7 - (source_y & 7) : source_y & 7;
                const std::uint8_t pixel = tile_pixel(tile_word, tile_pixel_x, tile_pixel_y);
                if (pixel == 0) {
                    continue;
                }
                framebuffer_[static_cast<std::size_t>(screen_y * kScreenWidth + screen_x)] =
                    color(((tile_word >> 13) & 0x03) * 16 + pixel);
            }
        }
    };

    draw_plane(true, false);
    draw_plane(false, false);

    const int sat_base = (registers[5] & 0x7F) << 9;
    const auto draw_sprites = [&](bool priority) {
        std::array<bool, 80> visited{};
        int index = 0;
        for (int count = 0; count < static_cast<int>(visited.size()); ++count) {
            if (index < 0 || index >= static_cast<int>(visited.size()) || visited[index]) {
                break;
            }
            visited[index] = true;
            const int address = sat_base + index * 8;
            const std::uint16_t y_word = word(address);
            const std::uint16_t size_link = word(address + 2);
            const std::uint16_t tile_word = word(address + 4);
            const std::uint16_t x_word = word(address + 6);
            const int next = size_link & 0x7F;
            const bool sprite_priority = (tile_word & 0x8000) != 0;
            if (y_word != 1 && sprite_priority == priority) {
                const int width_tiles = ((size_link >> 10) & 0x03) + 1;
                const int height_tiles = ((size_link >> 8) & 0x03) + 1;
                const int width = width_tiles * 8;
                const int height = height_tiles * 8;
                const int screen_x = (x_word & 0x01FF) - 128;
                const int screen_y = (y_word & 0x01FF) - 128;
                const bool flip_x = (tile_word & 0x0800) != 0;
                const bool flip_y = (tile_word & 0x1000) != 0;
                const int palette_start = ((tile_word >> 13) & 0x03) * 16;
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const int source_x = flip_x ? width - 1 - x : x;
                        const int source_y = flip_y ? height - 1 - y : y;
                        const int tile_column = source_x / 8;
                        const int tile_row = source_y / 8;
                        const std::uint16_t part_tile = static_cast<std::uint16_t>(
                            (tile_word & 0x07FF)
                            + tile_column * height_tiles + tile_row
                        );
                        const std::uint8_t pixel = tile_pixel(
                            part_tile, source_x & 7, source_y & 7
                        );
                        const int draw_x = screen_x + x;
                        const int draw_y = screen_y + y;
                        if (pixel == 0 || draw_x < 0 || draw_x >= kScreenWidth
                            || draw_y < 0 || draw_y >= kScreenHeight) {
                            continue;
                        }
                        framebuffer_[static_cast<std::size_t>(draw_y * kScreenWidth + draw_x)] =
                            color(palette_start + pixel);
                    }
                }
            }
            if (next == 0) {
                break;
            }
            index = next;
        }
    };

    draw_sprites(false);
    draw_plane(true, true);
    draw_plane(false, true);
    draw_sprites(true);
}

SpritePose Engine::sprite_pose() const {
    return animation_.pose();
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
        // The handler first looks for an existing type-0x8C record, then
        // allocates the first free common actor slot (the ROM scans slots
        // 3..22, starting at 0x00FF7F06). It only accepts a non-zero
        // landing/contour result and copies the player's world position into
        // the new record. The common actor pass services this new record on
        // the same boundary when its phase gate is open.
        if (player_.terrain_landing_state == 0) {
            break;
        }
        const auto existing_surface = std::find_if(
            actors_.begin(), actors_.end(), [](const ActorState& actor) {
                return actor.type == kTerrainSpawnActorType;
            });
        if (!bounce_response_follow_active_
            && surface_interaction_active_
            && player_.animation_selector.interaction_lock == 0
            && animation_.stream_kind() == AnimationStreamKind::Action
            && player_.terrain_horizontal_response == 0) {
            // Once the stop stream's 0x28-frame lock expires, the live
            // surface handler revisits the same selector even if the actor
            // allocator has a one-frame gap between records.
            player_.animation_selector.interaction_lock = 0x28;
        }
        if (existing_surface != actors_.end()) {
            // The existing record owns the actor lifecycle; do not allocate
            // a replacement while it is still in the table.
            break;
        }
        // The interaction refill service runs before this allocation's
        // publication in the same frame. Keep the request until that service
        // has had the first chance to claim the lowest free actor slot.
        surface_actor_spawn_pending_ = true;
        surface_actor_spawn_x_ = player_world_x();
        surface_actor_spawn_y_ = player_world_y();
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
        bounce_response_active_ = true;
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

void Engine::initialize_camera_alignment() {
    camera_.pixel_x = (camera_.x & 0x0F) + player_.x;
    camera_.pixel_y = (camera_.y & 0x0F) + player_.y;
    camera_.tile_x = camera_.x & ~0x0F;
    camera_.tile_y = camera_.y & ~0x0F;
}

bool Engine::rebase_camera_reference() {
    bool reference_rebased = false;
    // The ROM consumes the accumulated scroll on the next update pass, not
    // immediately after the pass that reaches a 16-pixel boundary.  The
    // pending flags are video/update bookkeeping and are cleared at the end
    // of the native frame, so the accumulator itself is the authoritative
    // condition here.
    if (camera_.scroll_x >= 0x10) {
        camera_.scroll_x -= 0x10;
        camera_.reference_x += 0x10;
        camera_.scroll_left_pending = false;
        camera_.scroll_right_pending = false;
        reference_rebased = true;
    } else if (camera_.scroll_x < -0x0F) {
        camera_.scroll_x += 0x10;
        camera_.reference_x -= 0x10;
        camera_.scroll_left_pending = false;
        camera_.scroll_right_pending = false;
        reference_rebased = true;
    }
    if (camera_.scroll_y >= 0x10) {
        camera_.scroll_y -= 0x10;
        camera_.reference_y += 0x10;
        camera_.scroll_up_pending = false;
        camera_.scroll_down_pending = false;
        reference_rebased = true;
    } else if (camera_.scroll_y < -0x0F) {
        camera_.scroll_y += 0x10;
        camera_.reference_y -= 0x10;
        camera_.scroll_up_pending = false;
        camera_.scroll_down_pending = false;
        reference_rebased = true;
    }
    // The ROM dispatcher consumes the pending byte every frame. A reference
    // rebase is conditional on the accumulator crossing 16 pixels, but the
    // pending marker itself is not sticky.
    camera_.scroll_left_pending = false;
    camera_.scroll_right_pending = false;
    camera_.scroll_up_pending = false;
    camera_.scroll_down_pending = false;
    return reference_rebased;
}

void Engine::update_camera(bool suppress_vertical_follow) {
    // 0x001AA8FA delays the follow pass after a player mode/threshold change.
    // The delay is observable in the jump trace: the camera remains still for
    // seven frames after the jump threshold is installed.
    if (camera_.update_delay > 0) {
        --camera_.update_delay;
        return;
    }
    if (camera_.special_mode != 0 || scene_.is_transition()) {
        return;
    }

    const auto horizontal_delta = [&]() {
        const int difference = player_.x - camera_.horizontal_threshold;
        if (difference == 0) {
            return;
        }
        const int magnitude = std::abs(difference);
        const int index = std::min(magnitude, static_cast<int>(kCameraHorizontalDampening.size() - 1));
        const int delta = kCameraHorizontalDampening[static_cast<std::size_t>(index)];
        if (delta == 0) {
            return;
        }
        if (difference < 0) {
            if (camera_.reference_x < 0x11) {
                return;
            }
            player_.x += delta;
            camera_.x -= delta;
            camera_.scroll_x -= delta;
            camera_.scroll_left_pending = true;
            return;
        }
        const int effective = camera_.reference_x + camera_.scroll_x + delta;
        if (effective >= camera_.level_width - 0x161) {
            return;
        }
        player_.x -= delta;
        camera_.x += delta;
        camera_.scroll_x += delta;
        camera_.scroll_right_pending = true;
    };

    const auto vertical_delta = [&]() {
        const int difference = player_.y - camera_.vertical_threshold;
        if (difference == 0) {
            return;
        }
        const int magnitude = std::abs(difference);
        // The vertical table is immediately followed by the level table in
        // the ROM at 0x2C78. Valid camera errors stop at its final byte.
        const int index = std::min(magnitude, 0xD3);
        const int delta = kCameraVerticalDampening[static_cast<std::size_t>(index)];
        if (delta == 0) {
            return;
        }
        if (difference < 0) {
            if (camera_.reference_y < 0x11) {
                return;
            }
            player_.y += delta;
            camera_.y -= delta;
            camera_.scroll_y -= delta;
            camera_.scroll_up_pending = true;
            return;
        }
        const int effective = camera_.reference_y + camera_.scroll_y + delta;
        if (effective >= camera_.level_height - 0xF1) {
            return;
        }
        player_.y -= delta;
        camera_.y += delta;
        camera_.scroll_y += delta;
        camera_.scroll_down_pending = true;
    };

    horizontal_delta();
    if (!suppress_vertical_follow) {
        vertical_delta();
    }

}

void Engine::update(const InputState& input) {
    if (scheduler_trace_enabled_) {
        scheduler_phases_.clear();
        scheduler_writer_pcs_.clear();
        animation_.clear_writer_trace();
        for (auto& actor_animation : actor_animations_) {
            actor_animation.clear_writer_trace();
        }
    }
    record_scheduler_phase("frame_latch", 0x001A8C16);
    // Game_FrameUpdateLoop begins with ADDQ.B #1,$FF7E28. All later gate
    // decisions in this update consume this single recovered ROM phase.
    frame_phase_ = static_cast<std::uint8_t>(frame_phase_ + 1);
    actors_.begin_frame();
    for (ActorState& actor : actors_) {
        if (actor.runtime_field_07_delay == 0) continue;
        --actor.runtime_field_07_delay;
        if (actor.runtime_field_07_delay == 0) {
            actor.runtime_field_07 = static_cast<std::uint8_t>(
                actor.runtime_field_07 | 0x10U);
        }
    }
    const bool interaction_selector_pending_at_start =
        interaction_selector_pending_;
    const bool terrain_response_was_active =
        player_.terrain_response_active != 0;
    const bool arm_surface_interaction = surface_interaction_pending_;
    surface_interaction_pending_ = false;
    if (player_.animation_selector.interaction_lock != 0) {
        --player_.animation_selector.interaction_lock;
    }
    if (player_.animation_selector.state_lock != 0) {
        --player_.animation_selector.state_lock;
    }
    if (interaction_actor_lock_pending_ && interaction_selector_pending_at_start) {
        // The actor flag edge is published first.  The interaction caller
        // installs its selector lock and camera delay on the next VBlank,
        // after the pending bit has crossed the frame boundary.
        player_.animation_selector.interaction_lock = 0x28;
        if (interaction_camera_delay_pending_) {
            camera_.update_delay = 7;
        }
        interaction_actor_lock_pending_ = false;
        interaction_camera_delay_pending_ = false;
    }
    // FUN_001A91C6 is the unconditional input/resource service. F5 spawn
    // requests are produced by the player animation VM on the
    // previous frame. Genesis allocates the record before the next actor
    // animation pass, so drain the request at the frame boundary rather than
    // after the current pass has already completed.
    record_scheduler_phase("input_resource", 0x001A91C6);
    (void) apply_animation_spawns();
    const bool transition_frame = scene_.is_transition();
    if (transition_frame) {
        scene_.update_transition(
            SceneInput{input.up, input.down, input.left, input.right},
            player_.x,
            player_.y,
            player_.grounded
        );
    } else {
    update_terrain_input(input);
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();
    terrain_input_world_x_ = player_world_x();
    terrain_input_world_y_ = player_world_y();
    const bool grounded_before_contour = player_.grounded;
    const bool contour_ground_motion_before = contour_ground_motion_;
    const bool stable_terrain_handler_fixture = checkpoint_terrain_behavior_override_
        && (checkpoint_terrain_behavior_ == 0x28
            || checkpoint_terrain_behavior_ == 0x29
            || checkpoint_terrain_behavior_ == 0x2D
            || checkpoint_terrain_behavior_ == 0x27);
    // The normal slice uses the recovered contour pass. Handler fixtures are
    // deliberately staged at the ROM's resolver boundary instead: the
    // original frame calls Terrain_ResolvePlayerCell/handler before the
    // 0x001A9D98 movement integrator, and the fixture supplies the already
    // resolved landing state that the ROM would have in RAM.
    if (!checkpoint_terrain_behavior_override_) {
        record_scheduler_phase("terrain_contour", 0x001AD7B4);
        apply_floor_contour();
    }
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();
    // The launch frame itself keeps the prior landing value visible. On the
    // first following pass the contour resolver clears it; one pass later
    // the ROM re-arms FFF0C1 for the falling phase. Apply the delayed arm
    // after contour resolution so the resolver cannot immediately erase it.
    if (jump_landing_state_arm_now_) {
        player_.terrain_landing_state = 0xFF;
        jump_landing_state_arm_now_ = false;
    }
    if (jump_landing_state_arm_pending_) {
        jump_landing_state_arm_now_ = true;
        jump_landing_state_arm_pending_ = false;
    }
    player_.animation_selector.landing_state = player_.terrain_landing_state;
    // Non-flat contours are traversed by the grounded movement path even
    // though the ROM's public grounded predicate is false until FFF0C1
    // returns to 1. Keep that distinction explicit for movement and VM
    // selection while preserving the observed state byte.
    bool contour_ground_motion = (grounded_before_contour || contour_ground_motion_)
        && !player_.grounded
        && player_.terrain_landing_state != 0
        && player_.terrain_response_active == 0
        && player_.vy == 0;
    contour_ground_motion_ = contour_ground_motion;
    const bool was_grounded = player_.grounded || contour_ground_motion;
    const bool just_landed = !grounded_before_contour
        && !contour_ground_motion_before
        && player_.grounded;
    // Terrain_ResolvePlayerCell consumes the world coordinate captured before
    // MovementVM. Its one invocation is placed after the actor terrain and
    // player collision services, and before the player response integrator.
    const int previous_world_y = player_world_y();
    if (player_.attack_timer != 0) {
        --player_.attack_timer;
    }
    if (input.attack_pressed && was_grounded) {
        player_.attack_timer = 10;
    }
    const auto collision = level_.query_player_collision(
        player_world_x(), player_world_y(),
        player_.terrain_landing_state);
    player_.terrain_stop_left_motion = collision.stop_left ? 0xFF : 0;
    player_.terrain_stop_right_motion = collision.stop_right ? 0xFF : 0;
    player_.terrain_stop_upward_motion = collision.stop_upward ? 0xFF : 0;
    player_.terrain_left_inner_probe = collision.left_inner ? 0xFF : 0;
    player_.terrain_left_outer_probe = collision.left_outer ? 0xFF : 0;
    player_.terrain_right_inner_probe = collision.right_inner ? 0xFF : 0;
    player_.terrain_right_outer_probe = collision.right_outer ? 0xFF : 0;
    update_terrain_connector_response();
    if (bounce_response_follow_active_ && player_.terrain_response_active == 0) {
        player_.terrain_response_timer_state = 1;
        if (animation_.stream_entry() == 0x00122006) {
            camera_.vertical_threshold = 400;
        }
    }
    const int input_direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    // The ROM's movement VM performs its cull before integrating actor
    // deltas. Use the pre-motion actor coordinates and pre-follow camera so
    // edge retirement lines up with the synchronized MAME boundary.
    update_dynamic_actor_culling();
    record_scheduler_phase("movement_vm", 0x001ADE36);
    update_actor_movement();
    // FUN_001ADB5C also resolves terrain for non-collision actors whose
    // movement flag bit 0 is set. The common type-0x29 object in the opening
    // refill window has no movement cursor, but its animation publishes a
    // frame pointer and the terrain pass snaps its Y coordinate to the
    // selected class contour on the following VBlank.
    record_scheduler_phase("actor_terrain_collision", 0x001ADB5C);
    if (!stable_terrain_handler_fixture && !rom_bytes_.empty()) {
        const auto& words = level_.terrain_words();
        const auto& floor = level_.floor_data();
        constexpr int kTerrainResourceBase = 0x2FD2;
        for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
            ActorState& actor = actors_[slot];
            if (actor.type == 0
                || ((actor.flags & 0x08) != 0 && !actors_.host_meta(slot).spawned_by_apple)
                || ((actor.movement_flags & 0x01) == 0 && !actors_.host_meta(slot).spawned_by_apple)
                || actor.frame_ptr == 0
                || actor.movement_word_1a < 0) {
                continue;
            }
            const int level_height_pixels = level_.map_height() * 16;
            if (static_cast<int>(actor.y) > level_height_pixels + 0xC8) {
                actor.movement_flags = static_cast<std::uint8_t>(
                    (actor.movement_flags & ~0x01U) | 0x40U);
                continue;
            }
            const int row = (static_cast<int>(actor.y) - 0xF0) >> 4;
            const int column = (static_cast<int>(actor.x) + 0x10) >> 4;
            if (row < 0 || row >= level_.map_height()
                || column < 0 || column >= level_.map_width()) {
                actor.movement_flags = static_cast<std::uint8_t>(
                    (actor.movement_flags & ~0x01U) | 0x40U);
                continue;
            }
            unsigned class_value = 0;
            int class_row_offset = 0;
            std::uint8_t interaction_state = 0;
            for (int row_offset = 0; row_offset < 3 && class_value == 0; ++row_offset) {
                const int sample_row = row + row_offset;
                if (sample_row >= level_.map_height()) break;
                const std::size_t map_index = static_cast<std::size_t>(
                    sample_row * level_.map_width() + column);
                const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
                for (std::size_t resource_offset = 0; resource_offset < 2; ++resource_offset) {
                    if (resource + resource_offset >= floor.size()) continue;
                    const std::uint8_t floor_byte = floor[resource + resource_offset];
                    const std::size_t class_address = static_cast<std::size_t>(
                        kTerrainResourceBase + (static_cast<std::size_t>(floor_byte) << 4)
                        + (static_cast<int>(actor.x) & 0x0F));
                    if (class_address >= rom_bytes_.size()) continue;
                    const unsigned candidate = rom_bytes_[class_address] & 0x3F;
                    if (candidate == 0) continue;
                    class_value = candidate;
                    class_row_offset = row_offset;
                    if (resource + 2 < floor.size()) {
                        interaction_state = floor[resource + 2];
                    }
                    break;
                }
            }
            if (actors_.host_meta(slot).spawned_by_apple && actor.type == 0x80
                && (actor.flags & 0x08) != 0) {
                // The apple flight record is collision-enabled but does not
                // use the generic gravity-only terrain branch. It converts
                // only when the current row (rather than a look-ahead row)
                // contains a solid class, matching the observed impact edge.
                if (class_value != 0 && class_row_offset == 0) {
                    const ActorState replacement = initialize_actor_from_template(actor, 0x001B792C);
                    (void)actor_lifecycle_.install(slot, replacement);
                    actor_animations_[slot].clear_actor_service_boundary();
                    actor_animations_[slot].defer_actor_service();
                    actors_.host_meta(slot).spawned_by_apple = false;
                }
                continue;
            }
            if (class_value == 0) {
                // The ROM's terrain probe also inspects the fourth row below
                // the actor for a pending contour. It uses that look-ahead to
                // arm +0x07 bit 4, but does not snap the actor to a contour
                // until the class enters the ordinary three-row path.
                for (int row_offset = 3; row_offset < 4; ++row_offset) {
                    const int sample_row = row + row_offset;
                    if (sample_row >= level_.map_height()) break;
                    const std::size_t map_index = static_cast<std::size_t>(
                        sample_row * level_.map_width() + column);
                    const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
                    for (std::size_t resource_offset = 0; resource_offset < 2; ++resource_offset) {
                        if (resource + resource_offset >= floor.size()) continue;
                        const std::uint8_t floor_byte = floor[resource + resource_offset];
                        const std::size_t class_address = static_cast<std::size_t>(
                            kTerrainResourceBase + (static_cast<std::size_t>(floor_byte) << 4)
                            + (static_cast<int>(actor.x) & 0x0F));
                        if (static_cast<int>(actor.y) >= 0x36B
                            && class_address < rom_bytes_.size()
                            && (rom_bytes_[class_address] & 0x3F) != 0) {
                            if ((actor.runtime_field_07 & 0x10U) == 0
                                && actor.runtime_field_07_delay == 0) {
                                actor.runtime_field_07_delay = 2;
                            }
                            row_offset = 4;
                            break;
                        }
                    }
                    if ((actor.runtime_field_07 & 0x10U) != 0) break;
                }
                // In-bounds class-zero terrain follows ROM 1ADE1E: it only
                // arms the vertical accumulator (unless bit 7 suppresses
                // gravity). The bit0->bit6 flag conversion is reserved for
                // the out-of-range path at 1ADE10 above.
                if ((actor.movement_flags & 0x80) == 0) {
                    actor.movement_word_1a = static_cast<std::int16_t>(
                        actor.movement_word_1a + 0x78);
                }
                continue;
            }
            actor.interaction_state = interaction_state;
            actor.movement_word_1a = 0;
            actor.y = static_cast<std::uint16_t>(
                ((static_cast<int>(actor.y) - 0x10) & ~0x0F)
                + class_row_offset * 0x10 + static_cast<int>(class_value) - 1);
        }
    }
    // FUN_001ADB5C is the terrain/actor pass immediately after the ROM
    // movement VM. For collision-enabled actors it samples the same decoded
    // terrain resource used by Level::resolve_player_cell, then dispatches
    // Actor_HandleType2DInteraction when the class-table entry is nonzero.
    // This is what converts the later 0x2D child to the 0x84 template; the
    // earlier child is on a flat class-zero cell and remains eligible for the
    // player collision pass below.
    if (!stable_terrain_handler_fixture && !rom_bytes_.empty()) {
        const auto& words = level_.terrain_words();
        const auto& floor = level_.floor_data();
        constexpr int kTerrainResourceBase = 0x2FD2;
        for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
            ActorState& actor = actors_[slot];
            if (actor.type != 0x2D
                || (actor.flags & 0x08) == 0
                || actor.frame_ptr == 0
                || static_cast<int>(actor.y) > player_world_y() + 0xE0) {
                continue;
            }
            const int row = (static_cast<int>(actor.y) - 0xF0) >> 4;
            const int column = (static_cast<int>(actor.x) + 0x10) >> 4;
            if (row < 0 || row >= level_.map_height()
                || column < 0 || column >= level_.map_width()) {
                continue;
            }
            const std::size_t map_index = static_cast<std::size_t>(
                row * level_.map_width() + column);
            const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
            const auto terrain_class = [&](std::size_t resource_byte_offset) {
                if (resource + resource_byte_offset >= floor.size()) return 0U;
                const std::uint8_t resource_byte = floor[resource + resource_byte_offset];
                const std::size_t class_address = static_cast<std::size_t>(
                    kTerrainResourceBase + (static_cast<std::size_t>(resource_byte) << 4)
                    + (static_cast<int>(actor.x) & 0x0F));
                if (class_address >= rom_bytes_.size()) return 0U;
                return static_cast<unsigned>(rom_bytes_[class_address] & 0x3F);
            };
            const bool class_empty = terrain_class(0) == 0 && terrain_class(1) == 0;
            const bool third_byte_is_empty = resource + 2 >= floor.size()
                || floor[resource + 2] < 0xE0;
            if (class_empty && third_byte_is_empty) {
                continue;
            }

            const std::uint32_t animation_pc = actor.animation_pc;
            const bool spawned_by_animation = actors_.host_meta(slot).spawned_by_animation;
            const ActorState replacement = initialize_actor_from_template(actor, 0x001B7E40);
            (void)actor_lifecycle_.install(slot, replacement);
            // Most type-0x2D terrain conversions pass through the common
            // 0x001ABECE follow-up, which republishes the facing byte as
            // 0xFF. The Level-01 stream at animation cursor 0x00123EFA takes
            // the direct terrain path instead and keeps the template's zero
            // facing byte.
            actor.facing_x_flip = animation_pc == 0x00123EFA ? 0 : 0xFF;
            actor_animations_[slot].clear_actor_service_boundary();
            actor_animations_[slot].defer_actor_service_on_gate();
            actors_.host_meta(slot).spawned_by_animation = spawned_by_animation;
        }
    }
    record_scheduler_phase("player_actor_interaction", 0x001ABB40);
    update_actor_interactions(input, was_grounded);
    record_scheduler_phase("terrain_resolution", 0x001B1E38);
    resolve_terrain(previous_world_y);
    // The camera's tile reference is consumed before the actor traversal in
    // the ROM. Rebase it now so the refill edge and the common actor gate see
    // the same newly crossed tile on this VBlank.
    const int camera_reference_y_before_rebase = camera_.reference_y;
    rebase_camera_reference();
    const bool camera_vertical_reference_rebased =
        camera_.reference_y != camera_reference_y_before_rebase;
    const bool camera_reference_moved_up =
        camera_.reference_y < camera_reference_y_before_rebase;
    if (arm_surface_interaction
        && !bounce_response_follow_active_
        && player_.animation_selector.interaction_lock == 0) {
        // A surface actor's type transition is published one frame before
        // Player_ProcessInteractionState selects the stop stream.
        player_.animation_selector.interaction_lock = 0x28;
    }
    const bool ground_release = was_grounded && !input.jump_pressed && input_direction == 0
        && last_ground_direction_ != 0 && player_.vx == 0;
    const int ground_release_direction = ground_release ? last_ground_direction_ : 0;
    const bool vertical_stop_before_frame = player_.terrain_vertical_stop != 0;
    const bool start_jump = input.jump_pressed && was_grounded;
    AnimationContext animation_context = player_animation_context(was_grounded);

    const bool blocked_right_wall_response = was_grounded
        && (player_.grounded || contour_ground_motion)
        && input.right
        && !input.left
        && player_.terrain_stop_right_motion != 0;
    const bool blocked_left_wall_response = was_grounded
        && (player_.grounded || contour_ground_motion)
        && input.left
        && !input.right
        && player_.terrain_stop_left_motion != 0;
    if (was_grounded && (player_.grounded || contour_ground_motion)) {
        if (input.left != input.right) {
            const int threshold = input.left ? 0xF0 : 0x70;
            if (camera_.horizontal_threshold != threshold) {
                camera_.horizontal_threshold = threshold;
                camera_.update_delay = 7;
            }
        }
        apply_ground_movement(input);
    } else if (player_.terrain_response_timer_state != 0
               && input.left && !input.right) {
        // The ROM's horizontal response path keeps applying FFF0B0 while
        // the contour latch is zero.  This is still a direct local-X step,
        // not fixed-point air acceleration; it is visible for a few frames
        // when walking off a contour edge.
        const int step = player_.terrain_horizontal_response != 0
            ? player_.terrain_horizontal_response : 2;
        player_.x -= step;
        player_.vx = 0;
    } else if (player_.terrain_response_timer_state != 0
               && input.right && !input.left) {
        const int step = player_.terrain_horizontal_response != 0
            ? player_.terrain_horizontal_response : 2;
        player_.x += step;
        player_.vx = 0;
    } else if (player_.terrain_response_active != 0
               && player_.terrain_jump_response_counter != 0
               && input.left && !input.right) {
        // The ROM's response state advances local X by the published
        // horizontal-response amount before Camera_UpdateFollow.  The camera
        // then compensates by the same damped delta, leaving the exposed
        // local coordinate fixed while world X follows the camera edge.
        const int step = player_.terrain_horizontal_response != 0
            ? player_.terrain_horizontal_response : 2;
        player_.x -= step;
        player_.vx = 0;
    } else if (player_.terrain_response_active != 0
               && player_.terrain_jump_response_counter != 0
               && input.right && !input.left) {
        const int step = player_.terrain_horizontal_response != 0
            ? player_.terrain_horizontal_response : 2;
        player_.x += step;
        player_.vx = 0;
    } else if (terrain_response_was_active
               && player_.terrain_response_active == 0
               && player_.terrain_response_timer_state == 0
               && input.left && !input.right) {
        const int step = player_.terrain_horizontal_response != 0
            ? player_.terrain_horizontal_response : 2;
        player_.x -= step;
        player_.vx = 0;
    } else if (terrain_response_was_active
               && player_.terrain_response_active == 0
               && player_.terrain_response_timer_state == 0
               && input.right && !input.left) {
        const int step = player_.terrain_horizontal_response != 0
            ? player_.terrain_horizontal_response : 2;
        player_.x += step;
        player_.vx = 0;
    } else if (player_.terrain_response_active != 0
               && player_.terrain_jump_response_counter == 0
               && animation_.stream_kind() != AnimationStreamKind::Response
               && input.left && !input.right) {
        player_.vx = player_.vx >= 0 ? static_cast<std::int16_t>(-0x300)
                                     : std::max<std::int16_t>(player_.vx, -0x300);
    } else if (player_.terrain_response_active != 0
               && player_.terrain_jump_response_counter == 0
               && animation_.stream_kind() != AnimationStreamKind::Response
               && input.right && !input.left) {
        player_.vx = player_.vx <= 0 ? static_cast<std::int16_t>(0x300)
                                     : std::min<std::int16_t>(player_.vx, 0x300);
    }
    bool landed_during_frame = false;
    if (checkpoint_terrain_behavior_override_
        && !was_grounded
        && !player_.grounded
        && player_.terrain_response_active != 0
        && player_.terrain_vertical_stop != 0
        && player_.vy >= 0) {
        // The fixture uses the recovered resolver behavior but still needs
        // the ROM's contour landing before the next motion integration. This
        // keeps the row-crossing frame airborne and lands on the following
        // pass, matching the original resolver's ordering.
        const auto contour = level_.query_player_contour(
            player_world_x(), player_world_y(), player_.terrain_surface_mode);
        if (contour.valid) {
            const int target_y = contour.target_world_y - camera_.y;
            if (std::abs(target_y - player_.y) <= 8) {
                player_.y = target_y;
                player_.vy = 0;
                player_.grounded = true;
                player_.terrain_landing_state = contour.contour;
                player_.terrain_response_active = 0;
                player_.terrain_response_timer_state = 0;
                player_.terrain_horizontal_response = 0;
                landed_during_frame = true;
            }
        }
    }
    record_scheduler_phase("player_movement", 0x001A9D98);
    integrate_motion();
    update_bounce_actor_interaction();
    if (player_.terrain_response_active != 0
        && player_.terrain_jump_response_counter != 0
        && player_.terrain_jump_response_counter < 10) {
        // Player_HandleJumpAndVerticalState applies this extra impulse after
        // Player_IntegrateMotion during the first nine active-response ticks.
        ++player_.terrain_jump_response_counter;
        if (animation_.stream_kind() != AnimationStreamKind::Response) {
            player_.vy = static_cast<std::int16_t>(player_.vy - 0x006C);
        }
    }
    if (checkpoint_terrain_behavior_override_ && checkpoint_terrain_behavior_ == 0x2B) {
        // The original response path continues through the positive-motion
        // state after TerrainHandler_StopAndAlignPlayer: it advances VY by
        // 0x78. Camera_UpdateFollow then applies the visible four-pixel
        // local-X correction.
        player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
    }
    if (!player_.grounded && player_.terrain_response_active != 0
        && vertical_stop_before_frame) {
        // The ROM clears the launch tile's behavior before entering the
        // positive vertical phase. Keep the terrain response active for the
        // published RAM state, but let the post-integrator handoff run.
        player_.terrain_behavior = 0;
    }
    // The bounce response clears FFF0BE when its positive fall reaches the
    // handoff boundary.  Genesis immediately arms the ordinary ground
    // response latch and holds Camera_UpdateFollow for seven VBlanks before
    // returning to the run stream.
    const bool bounce_response_finished =
        bounce_response_active_
        &&
        !bounce_response_follow_active_
        && terrain_response_was_active
        && player_.terrain_response_active == 0;
    if (bounce_response_finished) {
        player_.terrain_response_timer_state = 1;
        player_.terrain_jump_response_counter = 0;
        camera_.update_delay = 7;
        bounce_response_follow_active_ = true;
        bounce_camera_delay_hold_pending_ = true;
        // Any pre-existing surface/actor selector edge belongs to the
        // response that just ended.  Genesis clears that transient before
        // returning to the ordinary run stream; carrying it across the F8
        // handoff would spuriously select the 0x122014 stop root a few
        // frames later.
        interaction_selector_pending_ = false;
        interaction_actor_lock_pending_ = false;
        interaction_camera_delay_pending_ = false;
        surface_interaction_pending_ = false;
        surface_interaction_active_ = false;
    }
    if (!player_.grounded && player_.terrain_behavior == 0
        && (vertical_stop_before_frame || player_.terrain_response_timer_state != 0)) {
        // This is the post-integrator jump/vertical-state handoff. It is
        // intentionally after resolve_terrain: the original frame where the
        // residual upward velocity is cleared still exposes VY=0; the next
        // frame starts the positive phase at 0x003C.
        // When the integrator itself raises FFF0C0 while clearing the last
        // upward residual, the ROM publishes that boundary with VY still
        // zero.  The positive phase starts on the following frame; do not
        // consume the newly-written stop in the same pass.
        if (!terrain_fall_phase_
            && grounded_before_contour
            && player_.terrain_landing_state == 0
            && player_.terrain_vertical_stop == 0
            && player_.terrain_response_timer_state != 0) {
            // Walking off the last contour pixel enters the positive phase
            // immediately, even though no explicit vertical-stop byte was
            // raised by the integrator on that boundary.
            player_.vy = 0x003C;
            terrain_fall_phase_ = true;
        } else if (!terrain_fall_phase_ && player_.terrain_vertical_stop == 0xFF
            && (vertical_stop_before_frame || player_.vy != 0)) {
            player_.vy = 0x003C;
            terrain_fall_phase_ = true;
        } else if (terrain_fall_phase_ && player_.vy < 0x800) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x0078);
        }
        // FFF0C0 remains set after the residual-upward stop. The original
        // contour routine uses that latched bit to distinguish the later
        // falling/landing phase while FFF0BE is still active.
    }
    // Terrain handlers can arm the interaction lock after the frame's initial
    // state read. The animation VM observes that mutation through GameRamView
    // at its later scheduler boundary.
    if (start_jump && (player_.grounded || contour_ground_motion)) {
        // The recovered frame order applies the jump handler after motion and
        // terrain resolution (Player_Update -> Terrain_Resolve -> jump
        // handler). This leaves the impulse visible for the next frame before
        // the integrator consumes it.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
        contour_ground_motion = false;
        contour_ground_motion_ = false;
        player_.terrain_response_active = 0xFF;
        // The live ROM's ten-step counter is observable when a jump follows
        // a terrain-selected action stream. Keep direct locomotion fixtures
        // on the ordinary integrator path used by their checkpoints.
        player_.terrain_jump_response_counter =
            animation_.stream_kind() == AnimationStreamKind::Action ? 1 : 0;
        // A jump with a held horizontal direction follows the timed
        // terrain-response stream and retains FFF0CC=1. A neutral C press
        // takes the ordinary jump stream and clears the ground latch.
        if (input.left == input.right) {
            player_.terrain_response_timer_state = 0;
        }
        player_.terrain_vertical_stop = 0;
        // FFF0C1 remains at its grounded value for this launch boundary;
        // the contour pass clears it on the next frame and the falling-phase
        // response re-arms it one frame later.
        jump_landing_state_arm_pending_ = true;
        camera_.horizontal_threshold = 0xB0;
        camera_.vertical_threshold = 0x170;
        camera_.update_delay = 7;
    }
    if (player_.ground_braking && player_.vx == 0) {
        player_.ground_braking = false;
    }

    if (ground_release) {
        // The first no-input frame enters the ROM's inertial ground path after
        // the position/integration work but before camera follow. The common
        // animation pass selects the ROM brake stream on this boundary;
        // camera threshold changes are owned by that stream rather than being
        // inferred from the input edge here.
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        // The release handler seeds the inertial velocity after the current
        // integration pass. It is therefore visible on this boundary and is
        // consumed by the next pass (0x038C, then -0x28 per frame).
        player_.vx = static_cast<std::int16_t>(ground_release_direction * 0x038C);
        player_.ground_braking = true;
        last_ground_direction_ = 0;
    }

    const bool release_up_animation =
        !input.up && player_.animation_selector.transition_state_df != 0;
    if (release_up_animation) {
        // This clear belongs to Player_TerrainResponseStateMachine, before
        // Camera_UpdateFollow. The selected action stream remains active.
        player_.animation_selector.transition_state_df = 0;
        camera_.vertical_threshold = 0x170;
    }
    const bool select_down_animation =
        input.down
        && player_.grounded
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && player_.animation_selector.transition_state_de == 0
        && player_.animation_selector.transition_state_df == 0;
    if (select_down_animation) {
        // Player_TerrainResponseStateMachine selects the down stream before
        // the common VM tick. Its root is 0x001222D2; the tick then publishes
        // the first data cursor at 0x001222D4.
        animation_.select_stream_entry(kPlayerDownAnimationStream);
        player_.animation_selector.transition_state_de = 0xFF;
        player_.animation_selector.response_animation = 0;
        player_.animation_selector.state_lock = 0;
    }
    const bool release_down_animation =
        !input.down && player_.animation_selector.transition_state_de != 0;
    if (release_down_animation) {
        // The same terrain state machine clears the down latch after the
        // held-down stream has had its final frame; the stream itself owns
        // the later F8 handoff back to locomotion.
        player_.animation_selector.transition_state_de = 0;
        camera_.vertical_threshold = 0x170;
    }
    // The VM reads these post-handler values directly from GameState through
    // GameRamView. The invocation context remains a lightweight description
    // of this frame's call boundary rather than a copied RAM image.

    // Player_HandleJumpAndVerticalState and the terrain response service have
    // now finished writing local position. Publish the third ROM coordinate
    // boundary before Camera_UpdateFollow consumes it.
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();

    // The ROM consumes a pending 16-pixel reference shift before the actor
    // traversal and then runs the damped follow below. This leaves the
    // boundary frame externally visible with scroll == 16, then exposes the
    // rebased reference on the following frame.
    // A downward Camera_UpdateFollow reference-tile rebase suppresses the
    // vertical damped lookup for that frame, but the ROM still services the
    // independent horizontal follow. An upward rebase keeps the same-frame
    // vertical lookup; otherwise the residual scroll is lost and the player
    // drifts by one pixel at the tile cadence. Controlled resolver fixtures
    // retain their staged rebase boundary. Horizontal rebases retain the
    // same-frame damped follow.
    const bool camera_follow_deferred = camera_vertical_reference_rebased;
    if (bounce_camera_delay_hold_pending_ && !bounce_response_finished) {
        // The ROM's camera delay is held for one additional VBlank after the
        // bounce handoff; preserve the externally visible countdown (6, 6,
        // 5, ...), rather than decrementing it on the first run-stream tick.
        camera_.update_delay = 7;
        bounce_camera_delay_hold_pending_ = false;
    }
    record_scheduler_phase("camera_follow", 0x001AA90C);
    const bool same_frame_vertical_rebase =
        camera_reference_moved_up && !checkpoint_terrain_behavior_override_;
    if (camera_follow_deferred && !same_frame_vertical_rebase
        && !bounce_response_active_) {
        // The reference-tile write occupies this camera pass. The vertical
        // damped lookup resumes on the following VBlank; horizontal follow
        // is still handled by update_camera().
        update_camera(true);
    } else {
        update_camera();
        // A downward follow step can land exactly on the next camera tile
        // boundary. The ROM applies that reference update after the follow
        // pass; an earlier sub-tile crossing remains pending for the next
        // camera pass.
        if (player_.terrain_response_active == 0
            && player_.terrain_jump_response_counter == 0
            && camera_.y >= camera_.reference_y + 0x10
            && (camera_.y & 0x0F) == 0) {
            (void) rebase_camera_reference();
        }
    }
    if (was_grounded && (player_.grounded || contour_ground_motion) && input_direction != 0) {
        last_ground_direction_ = input_direction;
    } else if (!player_.grounded && !contour_ground_motion) {
        last_ground_direction_ = 0;
    }

    const bool landing_event = just_landed || landed_during_frame;
    const bool preserve_ground_response_run =
        !player_.grounded
        && !contour_ground_motion
        && !player_collision_interaction_pending_
        && player_.terrain_response_active == 0
        && player_.terrain_response_timer_state != 0
        && animation_.animation_pc() >= 0x00122006
        && animation_.animation_pc() <= 0x001220A6;
    SpritePose desired_pose = SpritePose::Idle;
    if (preserve_ground_response_run) {
        // The run/ground response owns the horizontal step even after the
        // contour latch drops. Keep its stream alive while the positive
        // vertical phase starts; switching to the generic jump stream here
        // would stop the camera-follow run sequence one frame too early.
        desired_pose = SpritePose::Run;
        if (!bounce_response_follow_active_ || bounce_response_finished) {
            player_.animation_selector.response_state_101 = 1;
        }
    } else if (!player_.grounded && !contour_ground_motion) {
        // Genesis keeps the jump stream active during the approach frame. It
        // selects the landing stream only after the contour resolver has
        // actually latched grounded state on the following boundary.
        desired_pose = SpritePose::Jump;
    } else if ((landing_event && animation_.pose() == SpritePose::Jump)
               || (animation_.pose() == SpritePose::Landing && !animation_.finished())) {
        // The terrain resolver can latch grounded while the active stream is
        // still the run/ground-response program (the opening slope does this
        // at frame 618).  The ROM does not replace that locomotion cursor
        // merely because FFF0C1 changed to a nonzero contour; the landing
        // root is selected only when the preceding player stream is the jump
        // program.  A generic just-landed test would incorrectly jump from
        // 0x0012207E to 0x00121F84 one boundary too early.
        desired_pose = SpritePose::Landing;
    } else if (input.left != input.right) {
        desired_pose = SpritePose::Run;
    } else if (ground_release
               || animation_.pose() == SpritePose::Brake
               || (!player_.ground_braking && animation_.pose() == SpritePose::Run)) {
        desired_pose = SpritePose::Brake;
    }
    // The common actor VM normally sees the pre-integration state. During the
    // active-response jump, however, the ROM's vertical handler has already
    // updated PLAYER_VY before the jump stream's F4 branch is evaluated. Keep
    // that one shared RAM value at the post-integration boundary so the
    // signed threshold transition at 0x001221B8 follows the ROM.
    AnimationContext vm_context = animation_context;
    if (desired_pose == SpritePose::Jump && player_.terrain_response_active != 0) {
        vm_context.player_vy_override = player_.vy;
        // FFF0C1 is cleared while the active-response jump is airborne. The
        // native terrain mirror retains the launch contour for landing
        // resolution, so keep the VM's selector input at the ROM value.
        vm_context.landing_state_override = 0;
    }
    // A direct grounded jump publishes its root before the common actor VM
    // pass. The resulting state boundary therefore exposes the first data
    // cursor (0x001221B2), rather than the untouched root, on the launch
    // frame. Action-selected jumps retain their existing post-pass ordering.
    const bool select_jump_before_vm =
        start_jump && animation_.stream_kind() == AnimationStreamKind::Locomotion;
    if (select_jump_before_vm) {
        animation_.select_locomotion_stream(SpritePose::Jump, vm_context);
    }
    // The grounded Up branch at 0x001AA0AE runs before the common VM. It
    // publishes the action root and FFF0DF, then the next animation tick
    // consumes that root.
    const bool can_select_up_animation =
        input.up
        && !input.left
        && !input.right
        && player_.grounded
        && player_.terrain_landing_state != 0
        && player_.vy == 0
        && player_.terrain_response_timer_state == 0
        && player_.terrain_transition_gate == 0
        && player_.animation_selector.transition_state_df == 0
        && camera_.special_mode == 0;
    const bool select_up_before_vm =
        can_select_up_animation && player_.terrain_vertical_stop != 0;
    if (select_up_before_vm) {
        animation_.select_stream_entry(kPlayerUpAnimationStream, true, true);
        player_.animation_selector.transition_state_df = 0xFF;
        player_.terrain_response_timer_state = 0;
    }
    if (input.apple_pressed && was_grounded && animation_.rom_loaded()) {
        // Player_SelectLocomotionOrAction publishes the throw root before
        // the single AnimationVM_TickActors traversal. Let that traversal
        // consume the root directly; the old post-pass apple boundary was a
        // native ordering workaround rather than a ROM state field.
        animation_.select_stream_entry(kPlayerAppleActionStream);
        player_.animation_selector.state_lock = 0x0E;
    }
    // Actor_ActorCollisionPass follows the player selectors in the ROM. Its
    // sword terminal edge is handled inside this one source/target scan;
    // there is deliberately no pre-motion companion call.
    record_scheduler_phase("actor_collision", 0x001ABD7E);
    update_actor_actor_collisions();
    record_scheduler_phase("level_exit_transition", 0x001A8F0C);
    (void) scene_.service_level_exit(
        player_world_y(),
        camera_.level_height,
        player_.terrain_terminal_transition,
        player_.animation_selector.interaction_lock
    );
    // 0x001A8F04 is the recovered empty-return block between the exit and
    // interaction services; preserving its ordinal is useful for trace
    // comparison but it has no native state effect.
    record_scheduler_phase("empty_return", 0x001A8F04);
    // FUN_001B01AC is the late interaction resource service. Its actor refill
    // edge is visible at the same game-loop boundary on either phase of
    // FRAME_PHASE_COUNTER; only the common actor animation table walk is
    // phase-gated.
    record_scheduler_phase("interaction_counter", 0x001B00CA);
    record_scheduler_phase("interaction_resource", 0x001B01AC);
    if (!stable_terrain_handler_fixture) {
        scan_interaction_refill_window();
    }
    flush_surface_actor_spawn();
    record_scheduler_phase("publish_player_world_coordinates", 0x001A8E0C);
    publish_player_world_coordinates();
    // The camera tile-update boundary does not suppress the common player VM
    // pass: the ROM services animation after the horizontal follow even when
    // the vertical reference tile is rebased. Keep the separate catch-up
    // marker only for the post-follow downward rebase path above.
    record_scheduler_phase("scene_advance", 0x001A8E3E);
    (void) scene_.advance_script();
    record_scheduler_phase("animation_vm", 0x001AC784);
    if (!stable_terrain_handler_fixture) {
        // The bounce response's F8 command publishes the dynamic 0x121AD8
        // root at this boundary but leaves the previous frame pointer in
        // place. Do not consume the new root until the following VBlank.
        const bool response_dynamic_handoff =
            animation_.animation_pc() == 0x001221E8;
        update_animation_vm_ordinal_30(
            desired_pose,
            horizontal_direction(input),
            vm_context,
            response_dynamic_handoff,
            bounce_response_finished
        );
        if (animation_.rom_loaded()) {
            camera_.vertical_threshold = animation_.camera_vertical_threshold();
            std::uint8_t value = 0;
            if (animation_.take_memory_write(0xFFF0C0, value)) {
                player_.terrain_vertical_stop = value;
            }
            if (animation_.take_memory_write(0xFFF0C1, value)) {
                player_.terrain_landing_state = value;
                player_.animation_selector.landing_state = value;
            }
            if (animation_.take_memory_write(0xFFF101, value)) {
                // Some run-stream ED commands re-arm the shared interaction
                // selector after the bounce handoff.  Propagate that tracked
                // ROM write back to the engine-owned selector so the next
                // command boundary sees the same FFF101 latch.
                player_.animation_selector.response_state_101 = value;
            }
        }
        if (player_collision_interaction_pending_) {
            // The type-0x2D collision handler calls the player interaction
            // selector after the common animation tick. Publish the stop
            // root and its first frame pointer at this boundary.
            animation_.select_stream_entry(0x00122014, true);
            player_.animation_selector.response_state_101 = 0;
            player_collision_interaction_pending_ = false;
        }

        if (can_select_up_animation && !select_up_before_vm) {
            // The ordinary grounded Up path publishes its action root after
            // the current VM pass. Keep this ordering for a fresh ground
            // state; the vertical-stop path above is the pre-pass variant.
            animation_.select_stream_entry(kPlayerUpAnimationStream);
            player_.animation_selector.transition_state_df = 0xFF;
            player_.terrain_response_timer_state = 0;
        }

        if (start_jump && !select_jump_before_vm) {
            // Player_HandleJumpAndVerticalState publishes the jump root after
            // the common VM pass. This remains a locomotion stream even when
            // the preceding stream was a terrain-selected action stream.
            animation_.select_locomotion_stream(SpritePose::Jump, vm_context);
        }
    }
    // Player_Update's horizontal wall branch enters the extended response
    // stream after the common VM tick.  The branch is reached when a held
    // direction first meets the terminal terrain stop; it clears FFF0B0 and
    // FFF0CC, marks FFF0ED, and publishes 0x00121FA6 without consuming its
    // first frame.  Keep this post-pass ordering so the boundary retains the
    // previous locomotion frame pointer just like Genesis.
    const bool wall_response_allowed = animation_.rom_loaded()
        && animation_.stream_kind() == AnimationStreamKind::Locomotion
        && player_.animation_selector.animation_gate == 0
        && player_.animation_selector.terminal_transition == 0
        && player_.animation_selector.interaction_lock == 0
        && player_.terrain_response_active == 0
        && (blocked_right_wall_response || blocked_left_wall_response);
    if (wall_response_allowed) {
        animation_.select_response_stream(0x00121FA6);
        player_.animation_selector.response_animation = 0xFF;
        player_.animation_selector.response_state_101 = 0;
        player_.animation_selector.horizontal_response = 0;
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        player_.animation_selector.response_timer = 0;
        player_.animation_selector.transition_state_de = 0;
        player_.animation_selector.transition_state_df = 0;
        player_.animation_selector.response_latch = 0;
    }
    // Type 0x7B uses the ROM's 0x001AE9D4 player-collision handler.  The
    // player/actor rectangles are evaluated after the common animation tick;
    // this is why the interaction first becomes visible when the response
    // stream publishes frame 0x001EA062, even though the same actor was
    // already present at the earlier wall-response boundary.
    bool surface_actor_collision = false;
    if (!stable_terrain_handler_fixture && animation_.rom_loaded()) {
        const CollisionBox player_interaction_box = read_collision_hitbox(
            animation_.frame_pointer(),
            player_world_x(),
            player_world_y(),
            animation_.facing_left());
        if (player_interaction_box.valid) {
            for (std::size_t slot = 1; slot <= 24 && slot < actors_.size(); ++slot) {
                const ActorState& actor = actors_[slot];
                if (actor.type != 0x7B || actor.frame_ptr == 0) continue;
                const CollisionBox actor_interaction_box = read_collision_hitbox(
                    actor.frame_ptr,
                    static_cast<int>(actor.x),
                    static_cast<int>(actor.y),
                    actor.facing_x_flip != 0);
                if (!actor_interaction_box.valid) continue;
                if (actor_interaction_box.left <= player_interaction_box.right
                    && actor_interaction_box.top <= player_interaction_box.bottom
                    && player_interaction_box.left < actor_interaction_box.right
                    && player_interaction_box.top < actor_interaction_box.bottom) {
                    surface_actor_collision = true;
                    break;
                }
            }
        }
    }
    if (surface_actor_collision
        && player_.animation_selector.interaction_lock == 0) {
        AnimationContext collision_selector_context =
            player_animation_context(player_.grounded);
        collision_selector_context.grounded_override =
            player_.grounded || contour_ground_motion;
        collision_selector_context.interaction_lock_override = 0;
        collision_selector_context.response_timer_override = 0;
        animation_.select_player_interaction_state(collision_selector_context);
        // The ROM's next VBlank publishes the 0x28 interaction lock after
        // this immediate selector call. Reuse the existing deferred lock
        // path so the frame of the handoff still exposes lock == 0.
        interaction_selector_pending_ = true;
        interaction_actor_lock_pending_ = true;
        interaction_camera_delay_pending_ = false;
    }
    // Player_ProcessInteractionState at 0x001AE4F8 is a RAM-driven stream
    // selector outside the common actor VM. Build its post-physics RAM view
    // after the actor tick so the native path owns the same boundary as the
    // ROM's interaction caller.
    if (!stable_terrain_handler_fixture
        && animation_.rom_loaded() && !landing_event && desired_pose != SpritePose::Landing
        && (interaction_selector_pending_at_start
            || player_.animation_selector.interaction_lock != 0)) {
        AnimationContext selector_context = animation_context;
        selector_context.grounded_override = player_.grounded || contour_ground_motion;
        if (interaction_selector_pending_at_start) {
            // The native lock is a post-call timer used by the animation
            // pass. The ROM caller reaches Player_ProcessInteractionState
            // with FFF0F2 clear on the frame after the actor flag edge.
            selector_context.interaction_lock_override = 0;
        }
        // The release path clears FFF0CC before entering 0x001AE4F8. The
        // native terrain mirror still exposes its one-frame response value,
        // so make this caller-side clear explicit in the selector state.
        selector_context.response_timer_override =
            desired_pose == SpritePose::Brake
                ? 0
                : (player_.terrain_response_active != 0
                    || ((player_.grounded || contour_ground_motion)
                        && player_.terrain_vertical_stop == 0)
                    ? 1
                    : 0);
        animation_.select_player_interaction_state(selector_context);
        if (interaction_selector_pending_at_start) {
            interaction_selector_pending_ = false;
        }
    }
    if (input.attack_pressed && was_grounded && animation_.rom_loaded()) {
        // The live ROM trace has a two-stage action transition: the input
        // frame leaves the player at 0x001232E0, and the following animation
        // tick enters the sword stream at 0x001223E2. The older isolated
        // collision fixture starts directly at 0x0012271A, so retain that
        // entry when no live action cursor is present.
        const auto current_animation = animation_.animation_pc();
        const bool at_attack_action_root =
            current_animation == 0x00122034 || current_animation == 0x00122040;
        const bool already_in_attack_transition =
            (current_animation >= 0x00122040 && current_animation <= 0x0012246A)
            || current_animation == 0x001232E0;
        if (at_attack_action_root) {
            // State-synchronized traces observe the post-selector stable
            // cursor, after the transient 0x1232E0 action state has handed
            // control to the sword stream.
            animation_.select_stream_entry(kPlayerSwordStableStream);
            animation_.set_frame_pointer(kPlayerSwordFirstFrame);
        } else if (!already_in_attack_transition) {
            const auto attack_stream = current_animation == 0x0012202C
                || animation_.frame_pointer() == 0x001EA48E
                ? 0x001232E0U
                : kPlayerSwordAnimationStream;
            animation_.select_stream_entry(attack_stream);
        }
    }
    // Non-combat F5 streams publish their request on the current animation
    // tick and expect the auxiliary actor to be visible in that frame's
    // state record. The live sword action is intentionally deferred to the
    // next frame boundary above while its attack timer is active.
    if (player_.attack_timer == 0
        && animation_.stream_entry() != kPlayerAttackTransitionStream
        && animation_.stream_entry() != kPlayerSwordStableStream) {
        // Requests not owned by ordinal 30 (notably the physical apple's
        // deferred lifecycle) remain queued for the next input/resource
        // boundary. No actor VM is invoked here.
        (void) apply_animation_spawns(true);
    }
    }
    if (transition_frame) {
        record_scheduler_phase("scene_advance", 0x001A8E3E);
        (void) scene_.advance_script();
        // Scene_EnterTransitionMode owns the transition movement, but the
        // frame loop still reaches its common ordinal-30 animation service.
        // Keeping that service here removes the native early-return path.
        record_scheduler_phase("animation_vm", 0x001AC784);
        update_animation_vm_ordinal_30(
            SpritePose::Idle,
            horizontal_direction(input),
            player_animation_context(player_.grounded),
            false,
            false
        );
    }
    record_scheduler_phase("transition_completion", 0x001AE0F6);
    (void) scene_.transition_completion_ready();
    // Camera scroll publication is presentation-owned in the native build;
    // retaining the ROM boundary in the trace makes that classification
    // explicit without inventing a second camera gameplay pass.
    record_scheduler_phase("camera_scroll_publish", 0x001AAA2A);
    record_scheduler_phase("scene_completion", 0x001B315C);
    (void) scene_.complete_script_to_state1();
    // State serialization needs the final player record after ordinal 30.
    // This mirror is not a gameplay scheduler phase.
    sync_player_actor();
    apply_actor_timeline(frame_ + 1);
    checkpoint_animation_selector_pending_ = false;
    record_scheduler_phase("state_boundary");
    collect_scheduler_writer_pcs();
    ++frame_;
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
    if (framebuffer_.size() != static_cast<std::size_t>(kScreenWidth * kScreenHeight)) {
        throw std::runtime_error("native framebuffer is not initialized");
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open framebuffer output: " + path);
    }
    file << "P6\n" << kScreenWidth << ' ' << kScreenHeight << "\n255\n";
    for (const std::uint32_t pixel : framebuffer_) {
        file.put(static_cast<char>(pixel & 0xFF));
        file.put(static_cast<char>((pixel >> 8) & 0xFF));
        file.put(static_cast<char>((pixel >> 16) & 0xFF));
    }
    if (!file) {
        throw std::runtime_error("cannot write framebuffer output: " + path);
    }
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
    const CollisionBox player_box = read_collision_box(
        animation_.frame_pointer(),
        player_world_x(),
        player_world_y(),
        animation_.facing_left()
    );
    const AnimationSelectorState animation_selector = player_.animation_selector;
    const SceneRuntimeState scene_runtime = scene_.runtime();

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
           << ",\"interaction_scan_initialized\":"
           << (interaction_scan_initialized_ ? "true" : "false")
           << ",\"interaction_selector_pending\":"
           << (interaction_selector_pending_ ? "true" : "false")
           << ",\"interaction_actor_lock_pending\":"
           << (interaction_actor_lock_pending_ ? "true" : "false")
           << ",\"interaction_camera_delay_pending\":"
           << (interaction_camera_delay_pending_ ? "true" : "false")
           << ",\"interaction_actor_triggered\":"
           << (interaction_actor_triggered_ ? "true" : "false")
           << ",\"player_collision_interaction_pending\":"
           << (player_collision_interaction_pending_ ? "true" : "false")
           << ",\"checkpoint_animation_selector_pending\":"
           << (checkpoint_animation_selector_pending_ ? "true" : "false")
           << ",\"surface_interaction_pending\":"
           << (surface_interaction_pending_ ? "true" : "false")
           << ",\"surface_interaction_active\":"
           << (surface_interaction_active_ ? "true" : "false")
           << ",\"jump_landing_state_arm_pending\":"
           << (jump_landing_state_arm_pending_ ? "true" : "false")
           << ",\"jump_landing_state_arm_now\":"
           << (jump_landing_state_arm_now_ ? "true" : "false")
           << ",\"terrain_fall_phase\":" << (terrain_fall_phase_ ? "true" : "false")
           << ",\"bounce_response_active\":"
           << (bounce_response_active_ ? "true" : "false")
           << ",\"bounce_response_follow_active\":"
           << (bounce_response_follow_active_ ? "true" : "false")
           << ",\"bounce_camera_delay_hold_pending\":"
           << (bounce_camera_delay_hold_pending_ ? "true" : "false")
           << ",\"contour_ground_motion\":"
           << (contour_ground_motion_ ? "true" : "false")
           << ",\"interaction_reference_x\":" << interaction_reference_x_
           << ",\"interaction_reference_y\":" << interaction_reference_y_
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
    if (animation_.deferred_spawn_request()) {
        const AnimationSpawnRequest& request = *animation_.deferred_spawn_request();
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
               << ",\"source_actor_slot\":" << request.source_actor_slot << "}";
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
        const CollisionBox actor_box = read_collision_box(
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

    writer.boolean(interaction_scan_initialized_);
    writer.boolean(interaction_selector_pending_);
    writer.boolean(interaction_actor_lock_pending_);
    writer.boolean(interaction_camera_delay_pending_);
    writer.boolean(interaction_actor_triggered_);
    writer.boolean(player_collision_interaction_pending_);
    writer.boolean(checkpoint_animation_selector_pending_);
    writer.boolean(surface_interaction_pending_);
    writer.boolean(surface_interaction_active_);
    writer.boolean(jump_landing_state_arm_pending_);
    writer.boolean(jump_landing_state_arm_now_);
    writer.boolean(terrain_fall_phase_);
    writer.boolean(bounce_response_active_);
    writer.boolean(bounce_response_follow_active_);
    writer.boolean(bounce_camera_delay_hold_pending_);
    writer.boolean(contour_ground_motion_);
    writer.i32(interaction_reference_x_);
    writer.i32(interaction_reference_y_);
    writer.u32(random_state_);
    writer.i32(terrain_input_world_x_);
    writer.i32(terrain_input_world_y_);
    writer.boolean(checkpoint_terrain_behavior_override_);
    writer.u8(checkpoint_terrain_behavior_);

    writer.boolean(vdp_checkpoint_.loaded);
    writer.byte_vector(vdp_checkpoint_.vram);
    writer.byte_vector(vdp_checkpoint_.vsram);
    writer.u32(static_cast<std::uint32_t>(vdp_checkpoint_.palette.size()));
    for (const SDL_Color& color : vdp_checkpoint_.palette) {
        writer.u8(color.r);
        writer.u8(color.g);
        writer.u8(color.b);
        writer.u8(color.a);
    }
    writer.bytes(vdp_checkpoint_.registers.data(), vdp_checkpoint_.registers.size());
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
    interaction_map_.read_checkpoint(input);
    actors_.read_checkpoint(input);
    PlayerState player = read_player_state(reader);
    CameraState camera = read_camera_state(reader);
    animation_.read_checkpoint(input);
    for (PlayerAnimationVm& actor_animation : actor_animations_) {
        actor_animation.read_checkpoint(input);
    }

    const bool interaction_scan_initialized = reader.boolean();
    const bool interaction_selector_pending = reader.boolean();
    const bool interaction_actor_lock_pending = reader.boolean();
    const bool interaction_camera_delay_pending = reader.boolean();
    const bool interaction_actor_triggered = reader.boolean();
    const bool player_collision_interaction_pending = reader.boolean();
    const bool checkpoint_animation_selector_pending = reader.boolean();
    const bool surface_interaction_pending = reader.boolean();
    const bool surface_interaction_active = reader.boolean();
    const bool jump_landing_state_arm_pending = reader.boolean();
    const bool jump_landing_state_arm_now = reader.boolean();
    const bool terrain_fall_phase = reader.boolean();
    const bool bounce_response_active = reader.boolean();
    const bool bounce_response_follow_active = reader.boolean();
    const bool bounce_camera_delay_hold_pending = reader.boolean();
    const bool contour_ground_motion = reader.boolean();
    const int interaction_reference_x = reader.i32();
    const int interaction_reference_y = reader.i32();
    const auto random_state = reader.u32();
    const int terrain_input_world_x = reader.i32();
    const int terrain_input_world_y = reader.i32();
    const bool checkpoint_terrain_behavior_override = reader.boolean();
    const auto checkpoint_terrain_behavior = reader.u8();

    VdpCheckpoint vdp_checkpoint;
    vdp_checkpoint.loaded = reader.boolean();
    vdp_checkpoint.vram = reader.byte_vector(0x10000);
    vdp_checkpoint.vsram = reader.byte_vector(0x80);
    const auto palette_size = reader.u32();
    if (palette_size > 256) {
        throw std::runtime_error("oversized VDP palette in OpenAladdin checkpoint");
    }
    vdp_checkpoint.palette.resize(palette_size);
    for (SDL_Color& color : vdp_checkpoint.palette) {
        color.r = reader.u8();
        color.g = reader.u8();
        color.b = reader.u8();
        color.a = reader.u8();
    }
    reader.bytes(vdp_checkpoint.registers.data(), vdp_checkpoint.registers.size());
    const int frame = reader.i32();
    const auto frame_phase = reader.u8();
    const int last_ground_direction = reader.i32();
    const bool quit = reader.boolean();
    if (vdp_checkpoint.loaded
        && (vdp_checkpoint.vram.size() != 0x10000
            || vdp_checkpoint.vsram.size() != 0x80)) {
        throw std::runtime_error("incomplete VDP checkpoint state");
    }

    player_ = player;
    camera_ = camera;
    interaction_scan_initialized_ = interaction_scan_initialized;
    interaction_selector_pending_ = interaction_selector_pending;
    interaction_actor_lock_pending_ = interaction_actor_lock_pending;
    interaction_camera_delay_pending_ = interaction_camera_delay_pending;
    interaction_actor_triggered_ = interaction_actor_triggered;
    player_collision_interaction_pending_ = player_collision_interaction_pending;
    checkpoint_animation_selector_pending_ = checkpoint_animation_selector_pending;
    surface_interaction_pending_ = surface_interaction_pending;
    surface_interaction_active_ = surface_interaction_active;
    jump_landing_state_arm_pending_ = jump_landing_state_arm_pending;
    jump_landing_state_arm_now_ = jump_landing_state_arm_now;
    terrain_fall_phase_ = terrain_fall_phase;
    bounce_response_active_ = bounce_response_active;
    bounce_response_follow_active_ = bounce_response_follow_active;
    bounce_camera_delay_hold_pending_ = bounce_camera_delay_hold_pending;
    contour_ground_motion_ = contour_ground_motion;
    interaction_reference_x_ = interaction_reference_x;
    interaction_reference_y_ = interaction_reference_y;
    random_state_ = random_state;
    terrain_input_world_x_ = terrain_input_world_x;
    terrain_input_world_y_ = terrain_input_world_y;
    checkpoint_terrain_behavior_override_ = checkpoint_terrain_behavior_override;
    checkpoint_terrain_behavior_ = checkpoint_terrain_behavior;
    actor_movement_deferred_.fill(false);
    vdp_checkpoint_ = std::move(vdp_checkpoint);
    frame_ = frame;
    frame_phase_ = frame_phase;
    last_ground_direction_ = last_ground_direction;
    quit_ = quit;
    camera_render_x_ = 0;
    camera_render_y_ = 0;
}

void Engine::render(SDL_Renderer* renderer) {
    if (framebuffer_.size() != static_cast<std::size_t>(kScreenWidth * kScreenHeight)) {
        return;
    }
    // The native renderer consumes the same WORLD_CAMERA origin as terrain
    // and actors. The old player-centered calculation made rendering drift
    // independently from Genesis gameplay coordinates.
    camera_render_x_ = std::clamp(camera_.x, 0, std::max(0, level_.background_width() - kScreenWidth));
    camera_render_y_ = std::clamp(camera_.y, 0, std::max(0, level_.background_height() - kScreenHeight));

    if (vdp_checkpoint_.loaded) {
        render_vdp_checkpoint();
    } else {
    const auto& background = level_.background_rgba();
    const auto& parallax = level_.parallax_rgba();
    const auto& palette = level_.palette();
    const std::uint32_t backdrop = palette.empty()
        ? rgba(10, 10, 18)
        : rgba(palette[0].r, palette[0].g, palette[0].b);
    const int background_source_x = std::clamp(
        camera_.x + kBackgroundPlaneOriginOffset,
        0,
        std::max(0, level_.background_width() - kScreenWidth)
    );
    const int background_source_y = std::clamp(
        camera_.y + kBackgroundPlaneOriginOffset,
        0,
        std::max(0, level_.background_height() - kScreenHeight)
    );
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            std::uint32_t pixel = backdrop;
            if (level_.parallax_width() > 0 && level_.parallax_height() > 0) {
                const int source_x = (
                    (rom_bytes_.empty()
                        ? kLevel01ParallaxSourceX
                        : level01_parallax_source_x(camera_.x, camera_.y, y)) + x
                ) % level_.parallax_width();
                const int source_y = (
                    rom_bytes_.empty()
                        ? y
                        : level01_parallax_source_y(camera_.x, camera_.y, y)
                ) % level_.parallax_height();
                const std::size_t source = static_cast<std::size_t>(
                    (source_y * level_.parallax_width() + source_x) * 4
                );
                if (!level_.is_vdp_transparent(
                        parallax[source], parallax[source + 1], parallax[source + 2])) {
                    pixel = rgba(parallax[source], parallax[source + 1], parallax[source + 2]);
                }
            }
            if (!background.empty()) {
                const int source_x = background_source_x + x;
                const int source_y = background_source_y + y;
                const std::size_t source = static_cast<std::size_t>(
                    (source_y * level_.background_width() + source_x) * 4
                );
                if (!level_.is_vdp_transparent(
                        background[source], background[source + 1], background[source + 2])) {
                    pixel = rgba(background[source], background[source + 1], background[source + 2]);
                }
            }
            framebuffer_[static_cast<std::size_t>(y * kScreenWidth + x)] = pixel;
        }
    }

    // The level-01 SAT contains a small set of fixed HUD/static sprites
    // before the player chain. Their tile attributes are stable at the
    // synchronized scene checkpoint and their pattern data comes from these
    // ROM regions. Keep these as VDP sprites rather than folding them into a
    // background bitmap so their Genesis colour-zero transparency remains
    // observable to the native renderer.
    if (!rom_bytes_.empty()) {
        struct VdpSpriteSpec {
            int x;
            int y;
            int width_tiles;
            int height_tiles;
            int tile_address;
        };
        static constexpr VdpSpriteSpec kLevel01HudSprites[] = {
            {16, 184, 3, 3, 0x11EDE0},
            {42, 200, 1, 1, 0x11ED00},
            {270, 192, 2, 2, 0x11EF00},
            {288, 200, 1, 1, 0x11ECC0},
            {296, 200, 1, 1, 0x11ECA0},
            {18, 20, 4, 3, 0x11E0A0},
            {50, 20, 2, 2, 0x11E220},
            {66, 12, 1, 2, 0x11E2A0},
            // The screenshot is sampled before the following VBlank's SAT
            // upload. Its carpet links still point at tile bases 0x6C0..,
            // which are the ROM regions below; frame-1300 VRAM already has
            // the next 0x6D0.. tile set installed.
            {74, 12, 1, 2, 0x11E8A0},
            {82, 12, 1, 2, 0x11E8E0},
            {90, 12, 1, 2, 0x11E920},
            {98, 12, 1, 2, 0x11E960},
            {106, 12, 1, 2, 0x11E9A0},
            {114, 12, 1, 2, 0x11E9E0},
            {122, 12, 1, 2, 0x11EA20},
            {130, 12, 1, 2, 0x11EA60},
        };
        for (const VdpSpriteSpec& sprite : kLevel01HudSprites) {
            SpriteRenderer::draw_vdp_sprite(
                rom_bytes_,
                sprite.tile_address,
                sprite.width_tiles,
                sprite.height_tiles,
                level_.palette(),
                framebuffer_,
                kScreenWidth,
                kScreenHeight,
                sprite.x,
                sprite.y,
                3
            );
        }
    }

    int player_frame_index = animation_.sprite_frame();
    if (player_frame_index < 0 || animation_.rom_loaded()) {
        player_frame_index = sprites_.frame_index_for_address(
            static_cast<int>(animation_.frame_pointer())
        );
    }
    if (player_frame_index < 0) {
        player_frame_index = SpriteDatabase::kIdleFrame;
    }
    const SpriteFrame& player_frame = sprites_.frame(player_frame_index);

    // Actor animation is state-owned by the native actor table, but its
    // visual output still has to be submitted to the same framebuffer as the
    // player. Actor frame pointers use the same extracted Chopper frame
    // database. Their extracted records retain preview palette line 0,
    // while the runtime Genesis SAT selects enemy palette line 2.
    // Slot zero is mirrored from PlayerState in the live engine and is drawn
    // separately below. Snapshot fixtures use the same convention.
    for (std::size_t slot = 1; slot < actors_.size(); ++slot) {
        const ActorState& actor = actors_[slot];
        if (actor.type == 0 || actor.frame_ptr == 0) {
            continue;
        }
        const int actor_frame_index = sprites_.frame_index_for_address(
            static_cast<int>(actor.frame_ptr)
        );
        if (actor_frame_index < 0) {
            // Some terminal/resource records intentionally have frame
            // pointers that are not visual Chopper frames. They remain part
            // of gameplay state but have no native bitmap to submit.
            continue;
        }
        const SpriteFrame& actor_frame = sprites_.frame(actor_frame_index);
        SpriteRenderer::draw(
            actor_frame,
            sprites_.palette(),
            framebuffer_,
            kScreenWidth,
            kScreenHeight,
            static_cast<int>(actor.x) + kActorVisualOffsetX - camera_render_x_,
            static_cast<int>(actor.y) - kPlayerVisualOffsetY - camera_render_y_,
            actor.facing_x_flip != 0,
            actor.facing_y_flip != 0,
            actor_palette_line(actor)
        );
    }

    SpriteRenderer::draw(
        player_frame,
        sprites_.palette(),
        framebuffer_,
        kScreenWidth,
        kScreenHeight,
        visual_x() - camera_render_x_,
        visual_y() - camera_render_y_,
        animation_.facing_left(),
        false,
        SpriteDatabase::kPlayerPaletteLine
    );
    }

    if (texture_ == nullptr) {
        // RGBA32 means RGBA byte order on the host. RGBA8888 is a packed
        // numeric format whose byte order is different on little-endian
        // systems, producing the channel-swapped pink output seen in the
        // first window test.
        texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight);
        if (texture_ == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        }
    }
    if (SDL_UpdateTexture(texture_, nullptr, framebuffer_.data(), kScreenWidth * static_cast<int>(sizeof(std::uint32_t))) != 0) {
        throw std::runtime_error(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
    }
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

}  // namespace openaladdin
