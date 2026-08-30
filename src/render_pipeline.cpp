#include "render_pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <stdexcept>

namespace openaladdin {
namespace {

constexpr int kPlayerVisualOffsetY = 0x100;
constexpr int kActorVisualOffsetX = 3;

void draw_preview_health_hud(
    const GameState& state,
    const std::vector<std::uint8_t>& rom,
    const std::vector<SDL_Color>& palette,
    std::vector<std::uint32_t>& framebuffer,
    int width,
    int height
) {
    const std::uint8_t health = std::min(
        state.player.health, PlayerState::kMaximumHealth);
    if (health == 0 || rom.empty()) return;

    SpriteRenderer::draw_vdp_sprite(
        rom,
        GenesisHealthHudLayout::kDigitRomBase
            + health * GenesisHealthHudLayout::kDigitRomStride,
        1,
        1,
        palette,
        framebuffer,
        width,
        height,
        GenesisHealthHudLayout::kPreviewDigitScreenX,
        GenesisHealthHudLayout::kPreviewDigitScreenY,
        3
    );
}

int actor_palette_line(const ActorState& actor) {
    return static_cast<int>((actor.sprite_attribute >> 13) & 0x03);
}

}  // namespace

void RenderPipeline::reset() {
    width_ = 0;
    height_ = 0;
    framebuffer_.clear();
}

void RenderPipeline::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("render pipeline dimensions must be positive");
    }
    width_ = width;
    height_ = height;
    framebuffer_.assign(
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_),
        0
    );
}

bool RenderPipeline::render(
    const GameState& state,
    GenesisRenderModel& render_model,
    const SpriteDatabase& sprites,
    const PlayerRenderState& player,
    const std::vector<std::uint8_t>& rom
) {

    // The native renderer consumes the same WORLD_CAMERA origin as terrain
    // and actors. The old player-centered calculation made rendering drift
    // independently from Genesis gameplay coordinates.
    if (width_ <= 0 || height_ <= 0
        || framebuffer_.size() != static_cast<std::size_t>(width_ * height_)) {
        return false;
    }
    const int camera_render_x = std::clamp(
        state.camera.x,
        0,
        std::max(0, render_model.preview_background_width() - width_));
    const int camera_render_y = std::clamp(
        state.camera.y,
        0,
        std::max(0, render_model.preview_background_height() - height_));

    if (render_model.loaded()) {
        render_model.sync_checkpoint_health_hud(state.player.health);
        render_model.render(framebuffer_, width_, height_);
    } else {
        render_model.render_preview_background(
            framebuffer_, width_, height_, state);

    // Static preview SAT entries are owned by the Genesis render model. Keep
    // them as VDP sprites rather than folding them into a background bitmap
    // so their Genesis colour-zero transparency remains observable.
    if (!rom.empty()) {
        for (const GenesisPreviewSprite& sprite : render_model.preview_sprites()) {
            if (sprite.tile_address
                == GenesisHealthHudLayout::kDigitRomBase
                    + 3 * GenesisHealthHudLayout::kDigitRomStride) {
                continue;
            }
            SpriteRenderer::draw_vdp_sprite(
                rom,
                sprite.tile_address,
                sprite.width_tiles,
                sprite.height_tiles,
                sprites.palette(),
                framebuffer_,
                width_,
                height_,
                sprite.screen_x,
                sprite.screen_y,
                sprite.palette_line
            );
        }
        draw_preview_health_hud(
            state,
            rom,
            sprites.palette(),
            framebuffer_,
            width_,
            height_
        );
    }

    int player_frame_index = player.sprite_frame;
    if (player_frame_index < 0 || !rom.empty()) {
        player_frame_index = sprites.frame_index_for_address(
            static_cast<int>(player.frame_pointer)
        );
    }
    if (player_frame_index < 0) {
        player_frame_index = SpriteDatabase::kIdleFrame;
    }
    const SpriteFrame& player_frame = sprites.frame(player_frame_index);

    // Actor animation is state-owned by the native actor table, but its
    // visual output still has to be submitted to the same framebuffer as the
    // player. Actor frame pointers use the same extracted Chopper frame
    // database. Their extracted records retain preview palette line 0,
    // while the runtime Genesis SAT selects enemy palette line 2.
    // Slot zero is mirrored from PlayerState in the live engine and is drawn
    // separately below. Snapshot fixtures use the same convention.
    for (std::size_t slot = 1; slot < state.actors.size(); ++slot) {
        const ActorState& actor = state.actors[slot];
        if (actor.type == 0 || actor.frame_ptr == 0) {
            continue;
        }
        const int actor_frame_index = sprites.frame_index_for_address(
            static_cast<int>(actor.frame_ptr)
        );
        if (actor_frame_index < 0) {
            // Some terminal/resource records intentionally have frame
            // pointers that are not visual Chopper frames. They remain part
            // of gameplay state but have no native bitmap to submit.
            continue;
        }
        const SpriteFrame& actor_frame = sprites.frame(actor_frame_index);
        SpriteRenderer::draw(
            actor_frame,
            sprites.palette(),
            framebuffer_,
            width_,
            height_,
            static_cast<int>(actor.x) + kActorVisualOffsetX - camera_render_x,
            static_cast<int>(actor.y) - kPlayerVisualOffsetY - camera_render_y,
            actor.facing_x_flip != 0,
            actor.facing_y_flip != 0,
            actor_palette_line(actor)
        );
    }

    SpriteRenderer::draw(
        player_frame,
        sprites.palette(),
        framebuffer_,
        width_,
        height_,
        (state.camera.x + state.player.x) - camera_render_x,
        (state.camera.y + state.player.y - kPlayerVisualOffsetY) - camera_render_y,
        player.facing_left,
        false,
        SpriteDatabase::kPlayerPaletteLine
    );
    }

    return true;
}

void RenderPipeline::write_framebuffer_ppm(const std::string& path) const {
    if (width_ <= 0 || height_ <= 0
        || framebuffer_.size()
            != static_cast<std::size_t>(width_ * height_)) {
        throw std::runtime_error("native framebuffer is not initialized");
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open framebuffer output: " + path);
    }
    file << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    for (const std::uint32_t pixel : framebuffer_) {
        file.put(static_cast<char>(pixel & 0xFF));
        file.put(static_cast<char>((pixel >> 8) & 0xFF));
        file.put(static_cast<char>((pixel >> 16) & 0xFF));
    }
    if (!file) {
        throw std::runtime_error("cannot write framebuffer output: " + path);
    }
}
}  // namespace openaladdin
