#include "actor_terrain.hpp"

#include "actor_lifecycle.hpp"
#include "animation_system.hpp"
#include "game_state.hpp"
#include "level.hpp"

#include <cstddef>

namespace openaladdin {
namespace {

constexpr int kTerrainResourceBase = 0x2FD2;

}  // namespace

void ActorTerrainSystem::update(
    GameState& state,
    const Level& level,
    std::span<const std::uint8_t> rom,
    bool stable_fixture
) {
    if (stable_fixture || rom.empty()) return;

    const auto& words = level.terrain_words();
    const auto& floor = level.floor_data();
    const int player_world_y = state.camera.y + state.player.y;

    // FUN_001ADB5C resolves terrain for non-collision actors whose movement
    // flag bit 0 is set. The common type-0x29 object in the opening refill
    // window has no movement cursor, but its animation publishes a frame
    // pointer and the terrain pass snaps its Y coordinate to the selected
    // class contour on the following VBlank.
    for (std::size_t slot = 0; slot < state.actors.size(); ++slot) {
        ActorState& actor = state.actors[slot];
        if (actor.type == 0
            || ((actor.flags & 0x08) != 0
                && !state.actors.host_meta(slot).spawned_by_apple)
            || ((actor.movement_flags & 0x01) == 0
                && !state.actors.host_meta(slot).spawned_by_apple)
            || actor.frame_ptr == 0
            || actor.movement_word_1a < 0) {
            continue;
        }
        const int level_height_pixels = level.map_height() * 16;
        if (static_cast<int>(actor.y) > level_height_pixels + 0xC8) {
            actor.movement_flags = static_cast<std::uint8_t>(
                (actor.movement_flags & ~0x01U) | 0x40U);
            continue;
        }
        const int row = (static_cast<int>(actor.y) - 0xF0) >> 4;
        const int column = (static_cast<int>(actor.x) + 0x10) >> 4;
        if (row < 0 || row >= level.map_height()
            || column < 0 || column >= level.map_width()) {
            actor.movement_flags = static_cast<std::uint8_t>(
                (actor.movement_flags & ~0x01U) | 0x40U);
            continue;
        }
        unsigned class_value = 0;
        int class_row_offset = 0;
        std::uint8_t interaction_state = 0;
        for (int row_offset = 0; row_offset < 3 && class_value == 0; ++row_offset) {
            const int sample_row = row + row_offset;
            if (sample_row >= level.map_height()) break;
            const std::size_t map_index = static_cast<std::size_t>(
                sample_row * level.map_width() + column);
            const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
            for (std::size_t resource_offset = 0; resource_offset < 2; ++resource_offset) {
                if (resource + resource_offset >= floor.size()) continue;
                const std::uint8_t floor_byte = floor[resource + resource_offset];
                const std::size_t class_address = static_cast<std::size_t>(
                    kTerrainResourceBase + (static_cast<std::size_t>(floor_byte) << 4)
                    + (static_cast<int>(actor.x) & 0x0F));
                if (class_address >= rom.size()) continue;
                const unsigned candidate = rom[class_address] & 0x3F;
                if (candidate == 0) continue;
                class_value = candidate;
                class_row_offset = row_offset;
                if (resource + 2 < floor.size()) {
                    interaction_state = floor[resource + 2];
                }
                break;
            }
        }
        if (state.actors.host_meta(slot).spawned_by_apple && actor.type == 0x80
            && (actor.flags & 0x08) != 0) {
            // The apple flight record is collision-enabled but does not use
            // the generic gravity-only terrain branch. It converts only when
            // the current row (rather than a look-ahead row) contains a
            // solid class, matching the observed impact edge.
            if (class_value != 0 && class_row_offset == 0) {
                const ActorState replacement = lifecycle_.initialize_record(
                    actor,
                    0x001B792C
                );
                (void)lifecycle_.install(slot, replacement);
                // This conversion occurs before the shared actor-animation
                // pass. The replacement is therefore eligible for the next
                // phase-gated service immediately; deferring it one more gate
                // would make the apple's terminal animation one odd frame
                // late (the original ticks it on the very next boundary).
                state.actors.host_meta(slot).spawned_by_apple = false;
            }
            continue;
        }
        if (class_value == 0) {
            // The ROM's terrain probe also inspects the fourth row below the
            // actor for a pending contour. It uses that look-ahead to arm
            // +0x07 bit 4, but does not snap the actor to a contour until the
            // class enters the ordinary three-row path.
            for (int row_offset = 3; row_offset < 4; ++row_offset) {
                const int sample_row = row + row_offset;
                if (sample_row >= level.map_height()) break;
                const std::size_t map_index = static_cast<std::size_t>(
                    sample_row * level.map_width() + column);
                const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
                for (std::size_t resource_offset = 0; resource_offset < 2; ++resource_offset) {
                    if (resource + resource_offset >= floor.size()) continue;
                    const std::uint8_t floor_byte = floor[resource + resource_offset];
                    const std::size_t class_address = static_cast<std::size_t>(
                        kTerrainResourceBase + (static_cast<std::size_t>(floor_byte) << 4)
                        + (static_cast<int>(actor.x) & 0x0F));
                    if (static_cast<int>(actor.y) >= 0x36B
                        && class_address < rom.size()
                        && (rom[class_address] & 0x3F) != 0) {
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
            // In-bounds class-zero terrain follows ROM 1ADE1E: it only arms
            // the vertical accumulator (unless bit 7 suppresses gravity).
            // The bit0->bit6 flag conversion is reserved for the out-of-range
            // path above.
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

    // Collision-enabled type-0x2D records use the same decoded terrain
    // resource to dispatch Actor_HandleType2DInteraction. This converts the
    // later child to the 0x84 template while the earlier child on a flat
    // class-zero cell remains eligible for the player collision pass below.
    for (std::size_t slot = 0; slot < state.actors.size(); ++slot) {
        ActorState& actor = state.actors[slot];
        if (actor.type != 0x2D
            || (actor.flags & 0x08) == 0
            || actor.frame_ptr == 0
            || static_cast<int>(actor.y) > player_world_y + 0xE0) {
            continue;
        }
        const int row = (static_cast<int>(actor.y) - 0xF0) >> 4;
        const int column = (static_cast<int>(actor.x) + 0x10) >> 4;
        if (row < 0 || row >= level.map_height()
            || column < 0 || column >= level.map_width()) {
            continue;
        }
        const std::size_t map_index = static_cast<std::size_t>(
            row * level.map_width() + column);
        const std::size_t resource = static_cast<std::size_t>(words[map_index] >> 1);
        const auto terrain_class = [&](std::size_t resource_byte_offset) {
            if (resource + resource_byte_offset >= floor.size()) return 0U;
            const std::uint8_t resource_byte = floor[resource + resource_byte_offset];
            const std::size_t class_address = static_cast<std::size_t>(
                kTerrainResourceBase + (static_cast<std::size_t>(resource_byte) << 4)
                + (static_cast<int>(actor.x) & 0x0F));
            if (class_address >= rom.size()) return 0U;
            return static_cast<unsigned>(rom[class_address] & 0x3F);
        };
        const bool class_empty = terrain_class(0) == 0 && terrain_class(1) == 0;
        const bool third_byte_is_empty = resource + 2 >= floor.size()
            || floor[resource + 2] < 0xE0;
        if (class_empty && third_byte_is_empty) continue;

        const std::uint32_t animation_pc = actor.animation_pc;
        const bool spawned_by_animation = state.actors.host_meta(slot).spawned_by_animation;
        const ActorState replacement = lifecycle_.initialize_record(
            actor,
            0x001B7E40
        );
        (void)lifecycle_.install(slot, replacement);
        // Most type-0x2D terrain conversions pass through the common
        // 0x001ABECE follow-up, which republishes the facing byte as 0xFF.
        // The Level-01 stream at animation cursor 0x00123EFA takes the direct
        // terrain path instead and keeps the template's zero facing byte.
        actor.facing_x_flip = animation_pc == 0x00123EFA ? 0 : 0xFF;
        animation_.actors().vm(slot).clear_actor_service_boundary();
        animation_.actors().vm(slot).defer_actor_service_on_gate();
        state.actors.host_meta(slot).spawned_by_animation = spawned_by_animation;
    }
}

}  // namespace openaladdin
