#include "camera.hpp"

#include <algorithm>
#include <cstdlib>

namespace openaladdin {

void CameraSystem::initialize(GameState& state, const Level& level) const {
    (void) level;
    auto& camera = state.camera;
    const auto& player = state.player;
    camera.pixel_x = (camera.x & 0x0F) + player.x;
    camera.pixel_y = (camera.y & 0x0F) + player.y;
    camera.tile_x = camera.x & ~0x0F;
    camera.tile_y = camera.y & ~0x0F;
}

bool CameraSystem::rebase(GameState& state, const Level& level) const {
    (void) level;
    auto& camera = state.camera;
    bool reference_rebased = false;
    // The ROM consumes the accumulated scroll on the next update pass, not
    // immediately after the pass that reaches a 16-pixel boundary. The
    // pending flags are video/update bookkeeping and are cleared at the end
    // of the native frame, so the accumulator is authoritative here.
    if (camera.scroll_x >= 0x10) {
        camera.scroll_x -= 0x10;
        camera.reference_x += 0x10;
        camera.scroll_left_pending = false;
        camera.scroll_right_pending = false;
        reference_rebased = true;
    } else if (camera.scroll_x < -0x0F) {
        camera.scroll_x += 0x10;
        camera.reference_x -= 0x10;
        camera.scroll_left_pending = false;
        camera.scroll_right_pending = false;
        reference_rebased = true;
    }
    if (camera.scroll_y >= 0x10) {
        camera.scroll_y -= 0x10;
        camera.reference_y += 0x10;
        camera.scroll_up_pending = false;
        camera.scroll_down_pending = false;
        reference_rebased = true;
    } else if (camera.scroll_y < -0x0F) {
        camera.scroll_y += 0x10;
        camera.reference_y -= 0x10;
        camera.scroll_up_pending = false;
        camera.scroll_down_pending = false;
        reference_rebased = true;
    }
    // The ROM dispatcher consumes the pending byte every frame. A reference
    // rebase is conditional on crossing 16 pixels, but the marker itself is
    // not sticky.
    camera.scroll_left_pending = false;
    camera.scroll_right_pending = false;
    camera.scroll_up_pending = false;
    camera.scroll_down_pending = false;
    return reference_rebased;
}

void CameraSystem::update(
    GameState& state,
    const Level& level,
    const GameData& data,
    bool suppress_vertical_follow
) const {
    (void) level;
    auto& camera = state.camera;
    auto& player = state.player;

    // 0x001AA8FA delays the follow pass after a player mode/threshold change.
    // The delay is observable in the jump trace: the camera remains still for
    // seven frames after the jump threshold is installed.
    if (camera.update_delay > 0) {
        --camera.update_delay;
        return;
    }
    if (camera.special_mode != 0 || state.scene.transition_active) {
        return;
    }

    const auto horizontal = data.camera_horizontal_damping();
    if (!horizontal.empty()) {
        const int difference = player.x - camera.horizontal_threshold;
        if (difference != 0) {
            const int magnitude = std::abs(difference);
            const int index = std::min(
                magnitude,
                static_cast<int>(horizontal.size() - 1)
            );
            const int delta = horizontal[static_cast<std::size_t>(index)];
            if (delta != 0) {
                if (difference < 0) {
                    if (camera.reference_x >= 0x11) {
                        player.x += delta;
                        camera.x -= delta;
                        camera.scroll_x -= delta;
                        camera.scroll_left_pending = true;
                    }
                } else {
                    const int effective = camera.reference_x + camera.scroll_x + delta;
                    if (effective < camera.level_width - 0x161) {
                        player.x -= delta;
                        camera.x += delta;
                        camera.scroll_x += delta;
                        camera.scroll_right_pending = true;
                    }
                }
            }
        }
    }

    if (suppress_vertical_follow) return;

    const auto vertical = data.camera_vertical_damping();
    if (vertical.empty()) return;

    const int difference = player.y - camera.vertical_threshold;
    if (difference == 0) return;
    const int magnitude = std::abs(difference);
    // The vertical table is immediately followed by the level table in the
    // ROM at 0x2C78. Valid camera errors stop at its final byte.
    const int index = std::min(
        magnitude,
        static_cast<int>(vertical.size() - 1)
    );
    const int delta = vertical[static_cast<std::size_t>(index)];
    if (delta == 0) return;
    if (difference < 0) {
        if (camera.reference_y < 0x11) return;
        player.y += delta;
        camera.y -= delta;
        camera.scroll_y -= delta;
        camera.scroll_up_pending = true;
        return;
    }
    const int effective = camera.reference_y + camera.scroll_y + delta;
    if (effective >= camera.level_height - 0xF1) return;
    player.y -= delta;
    camera.y += delta;
    camera.scroll_y += delta;
    camera.scroll_down_pending = true;
}

}  // namespace openaladdin
