#pragma once

#include "game_state.hpp"
#include "render_model.hpp"
#include "sprites.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace openaladdin {

// The visual state needed to submit the player. Gameplay owns the VM and
// actor records; the pipeline only consumes their already-published pose.
struct PlayerRenderState {
    int sprite_frame = -1;
    std::uint32_t frame_pointer = 0;
    bool facing_left = false;
};

// Composes the semantic game/render state into the fixed Genesis viewport.
// This class has no SDL device or texture ownership, so the same output can
// be used by headless framebuffer and visual parity tests.
class RenderPipeline {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 224;

    void reset();
    void resize(int width = kWidth, int height = kHeight);

    bool render(
        const GameState& state,
        const Level& level,
        const GenesisRenderModel& render_model,
        const SpriteDatabase& sprites,
        const PlayerRenderState& player,
        const std::vector<std::uint8_t>& rom
    );

    const std::vector<std::uint32_t>& framebuffer() const { return framebuffer_; }
    void write_framebuffer_ppm(const std::string& path) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint32_t> framebuffer_;
};

}  // namespace openaladdin
