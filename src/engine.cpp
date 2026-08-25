#include "engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace openaladdin {
namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 224;
constexpr int kPlayerHeight = 24;
constexpr int kPlayerWidth = 16;
constexpr int kTerrainVisualOffsetY = 0xF0;

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path);
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("cannot size " + path);
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

std::string ppm_token(std::istream& input) {
    while (true) {
        int ch = input.peek();
        if (ch == '#') {
            std::string ignored;
            std::getline(input, ignored);
        } else if (ch != EOF && std::isspace(static_cast<unsigned char>(ch))) {
            input.get();
        } else {
            break;
        }
    }
    std::string token;
    while (true) {
        int ch = input.peek();
        if (ch == EOF || std::isspace(static_cast<unsigned char>(ch)) || ch == '#') {
            break;
        }
        token.push_back(static_cast<char>(input.get()));
    }
    return token;
}

struct PpmImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

PpmImage read_ppm(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path);
    }
    if (ppm_token(file) != "P6") {
        throw std::runtime_error(path + " is not a binary PPM (P6)");
    }
    const int width = std::stoi(ppm_token(file));
    const int height = std::stoi(ppm_token(file));
    const int max_value = std::stoi(ppm_token(file));
    if (width <= 0 || height <= 0 || max_value != 255) {
        throw std::runtime_error("unsupported PPM dimensions or color depth in " + path);
    }
    // ppm_token leaves the delimiter after max_value in the stream. Consume
    // it before reading the packed RGB payload.
    file.get();
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    file.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (file.gcount() != static_cast<std::streamsize>(rgb.size())) {
        throw std::runtime_error("truncated PPM payload in " + path);
    }
    PpmImage result{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t source = 0, destination = 0; source < rgb.size(); source += 3, destination += 4) {
        result.rgba[destination + 0] = rgb[source + 0];
        result.rgba[destination + 1] = rgb[source + 1];
        result.rgba[destination + 2] = rgb[source + 2];
        result.rgba[destination + 3] = 255;
    }
    return result;
}

std::vector<std::uint16_t> read_be_words(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() % 2 != 0) {
        throw std::runtime_error("terrain map has an odd byte count");
    }
    std::vector<std::uint16_t> words(bytes.size() / 2);
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = static_cast<std::uint16_t>(bytes[i * 2] << 8 | bytes[i * 2 + 1]);
    }
    return words;
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

}  // namespace

void Level::load(const std::string& asset_root) {
    const auto background = read_ppm(asset_root + "/background.ppm");
    background_width_ = background.width;
    background_height_ = background.height;
    background_rgba_ = background.rgba;

    const auto map_bytes = read_file(asset_root + "/raw/map.bin");
    terrain_words_ = read_be_words(map_bytes);
    if (terrain_words_.size() != static_cast<std::size_t>(map_width_ * map_height_)) {
        throw std::runtime_error("level-01 map is not 300x45 terrain words");
    }

    floor_data_ = read_file(asset_root + "/raw/floor.bin");
    const auto palette_bytes = read_file(asset_root + "/raw/palette.bin");
    if (palette_bytes.size() < 32) {
        throw std::runtime_error("level palette is too short");
    }
    palette_.clear();
    palette_.reserve(palette_bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < palette_bytes.size(); i += 2) {
        const std::uint16_t word = static_cast<std::uint16_t>(palette_bytes[i] << 8 | palette_bytes[i + 1]);
        const auto channel = [](std::uint16_t value, int shift) {
            return static_cast<std::uint8_t>(((value >> shift) & 7) * 255 / 7);
        };
        palette_.push_back(SDL_Color{channel(word, 1), channel(word, 5), channel(word, 9), 255});
    }
}

std::uint8_t Level::terrain_behavior(int column, int row) const {
    if (column < 0 || column >= map_width_ || row < 0 || row >= map_height_) {
        return 0xFF;
    }
    const std::uint16_t terrain_word = terrain_words_[static_cast<std::size_t>(row * map_width_ + column)];
    const std::size_t behavior_index = 2 + (terrain_word >> 1);
    if (behavior_index >= floor_data_.size()) {
        return 0xFF;
    }
    return floor_data_[behavior_index];
}

void Engine::load(const std::string& asset_root) {
    level_.load(asset_root);
    framebuffer_.resize(static_cast<std::size_t>(kScreenWidth * kScreenHeight));
    reset();
}

void Engine::reset() {
    player_ = PlayerState{};
    player_.x = level_.start_x();
    player_.y = level_.start_y();
    player_.grounded = true;
    player_.terrain_behavior = 0;
    frame_ = 0;
    quit_ = false;
}

int Engine::visual_x() const {
    return level_.camera_x() + player_.x;
}

int Engine::visual_y() const {
    // The terrain resolver indexes rows from WORLD_CAMERA_Y + PLAYER_Y - 0xF0.
    return level_.camera_y() + player_.y - kTerrainVisualOffsetY;
}

int Engine::support_row(int visual_y_position, int visual_x_position) const {
    const int column = (visual_x_position + 16) >> 4;
    const int first_row = std::max(0, visual_y_position >> 4);
    for (int row = first_row; row <= first_row + 3 && row < level_.map_height(); ++row) {
        const std::uint8_t behavior = level_.terrain_behavior(column, row);
        // Zero is the empty behavior in the recovered resolver. Values below
        // E0 are the surface/interaction family; E0+ are non-solid entries.
        if (behavior != 0 && behavior < 0xE0) {
            return row;
        }
    }
    return -1;
}

void Engine::integrate_motion() {
    // This follows Player_IntegrateMotion at 0x001A9B90: move by the signed
    // high byte of each 8.8 velocity, then damp by 0x28/0x3C.
    if (player_.vx != 0) {
        if (player_.vx < 0) {
            if (player_.x < 0x14 || static_cast<std::uint16_t>(-player_.vx) < 0x28) {
                player_.vx = 0;
            } else {
                player_.x += static_cast<std::int8_t>(fixed_high_byte(player_.vx));
                player_.vx = static_cast<std::int16_t>(player_.vx + 0x28);
            }
        } else if (player_.vx < 0x28) {
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
        if (player_.y < 0x14 || magnitude <= 0x3B) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x3C);
        } else {
            player_.y += static_cast<std::int8_t>(fixed_high_byte(player_.vy));
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x3C);
        }
    } else if (player_.vy > 0x3B) {
        player_.y += static_cast<std::int8_t>(fixed_high_byte(player_.vy));
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x3C);
    } else {
        player_.vy = 0;
    }
}

void Engine::resolve_terrain() {
    const int current_visual_y = visual_y();
    const int current_visual_x = visual_x();
    const int row = support_row(current_visual_y, current_visual_x);
    if (row >= 0 && player_.vy >= 0) {
        const int surface_y = row * 16;
        if (current_visual_y >= surface_y - 2 && current_visual_y <= surface_y + 12) {
            const int column = (current_visual_x + 16) >> 4;
            const std::uint8_t behavior = level_.terrain_behavior(column, row);
            player_.y = surface_y + kTerrainVisualOffsetY - level_.camera_y();
            player_.vy = 0;
            player_.vx = 0;
            player_.grounded = true;
            apply_terrain_behavior(behavior, surface_y);
            return;
        }
    }

    player_.grounded = false;
    player_.terrain_behavior = 0;
    // The terrain miss path applies gravity before the next call to the
    // recovered integrator. The observed implementation caps it at 0x800.
    if (player_.vy < 0x800) {
        player_.vy = static_cast<std::int16_t>(std::min<int>(0x800, player_.vy + 0x78));
    }

    if (row >= 0 && player_.vy > 0) {
        const int surface_y = row * 16;
        if (current_visual_y >= surface_y - 2 && current_visual_y <= surface_y + 20) {
            const int column = (current_visual_x + 16) >> 4;
            const std::uint8_t behavior = level_.terrain_behavior(column, row);
            player_.y = surface_y + kTerrainVisualOffsetY - level_.camera_y();
            player_.vy = 0;
            player_.vx = 0;
            player_.grounded = true;
            apply_terrain_behavior(behavior, surface_y);
        }
    }
}

void Engine::apply_terrain_behavior(std::uint8_t behavior, int surface_y) {
    if (behavior == 0 || behavior >= 0xE0) {
        player_.terrain_behavior = 0;
        return;
    }

    const bool entered_behavior = player_.terrain_behavior != behavior;
    player_.terrain_behavior = behavior;
    if (!entered_behavior) {
        return;
    }

    // These values are the behavior indices resolved through the ROM table at
    // 0x004554. The corresponding handlers are recovered in the RE notes:
    // 0x29=launch, 0x2B=stop/align, 0x2D=bounce, 0x30=landing, 0x41=left.
    switch (behavior) {
    case 0x29:  // TerrainHandler_LaunchPlayerBlock (0x001B557E)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(-0x500);
        player_.grounded = false;
        break;
    case 0x2B:  // TerrainHandler_StopAndAlignPlayer (0x001B5502)
        player_.vx = 0;
        player_.vy = 0;
        player_.grounded = true;
        player_.y = surface_y + kTerrainVisualOffsetY - level_.camera_y();
        break;
    case 0x2D:  // TerrainHandler_BouncePlayerBlock (0x001B56B6)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(0x200);
        player_.grounded = false;
        break;
    case 0x30:  // TerrainHandler_LandingResponseBlock (0x001B537A)
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x7C);
        player_.grounded = false;
        break;
    case 0x41:  // TerrainHandler_HorizontalResponseBlock (0x001B53A2)
        player_.x = std::max(0x14, player_.x - 8);
        player_.vx = 0;
        break;
    default:
        break;
    }
}

bool Engine::horizontal_blocked(int direction) const {
    const int next_visual_x = visual_x() + direction * 3;
    const int edge_x = next_visual_x + (direction > 0 ? 16 : 0);
    const int body_top = visual_y() - kPlayerHeight + 4;
    const int body_bottom = visual_y() - 4;
    for (int sample_y = body_top; sample_y <= body_bottom; sample_y += 8) {
        const int row = sample_y >> 4;
        const int column = edge_x >> 4;
        const std::uint8_t behavior = level_.terrain_behavior(column, row);
        if (behavior != 0 && behavior < 0xE0) {
            return true;
        }
    }
    return false;
}

void Engine::apply_ground_movement(const InputState& input) {
    const int direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    if (direction == 0) {
        player_.vx = 0;
        return;
    }

    // The recovered ground response path uses a three-pixel movement step
    // (DAT_FFF0B0=3) and leaves PLAYER_VX clear. This is distinct from the
    // fixed-point velocity used for airborne motion and terrain launches.
    if (!horizontal_blocked(direction)) {
        player_.x = std::clamp(player_.x + direction * 3, 0x14, 0x130);
    }
    player_.vx = 0;
}

void Engine::update(const InputState& input) {
    const bool was_grounded = player_.grounded;
    if (input.jump_pressed && was_grounded) {
        // Confirmed jump initialization at 0x001A9716.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
    }

    if (was_grounded && player_.grounded) {
        apply_ground_movement(input);
    } else if (input.left && !input.right) {
        player_.vx = player_.vx >= 0 ? static_cast<std::int16_t>(-0x300)
                                     : std::max<std::int16_t>(player_.vx, -0x300);
    } else if (input.right && !input.left) {
        player_.vx = player_.vx <= 0 ? static_cast<std::int16_t>(0x300)
                                     : std::min<std::int16_t>(player_.vx, 0x300);
    }

    integrate_motion();
    resolve_terrain();
    ++frame_;
}

void Engine::render(SDL_Renderer* renderer) {
    if (framebuffer_.size() != static_cast<std::size_t>(kScreenWidth * kScreenHeight)) {
        return;
    }
    camera_render_x_ = std::clamp(visual_x() - kScreenWidth / 2, 0, std::max(0, level_.background_width() - kScreenWidth));
    camera_render_y_ = std::clamp(visual_y() - kScreenHeight / 2, 0, std::max(0, level_.background_height() - kScreenHeight));

    const auto& background = level_.background_rgba();
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const int source_x = camera_render_x_ + x;
            const int source_y = camera_render_y_ + y;
            std::uint32_t pixel = rgba(10, 10, 18);
            if (source_x >= 0 && source_x < level_.background_width() &&
                source_y >= 0 && source_y < level_.background_height()) {
                const std::size_t source = static_cast<std::size_t>((source_y * level_.background_width() + source_x) * 4);
                pixel = rgba(background[source], background[source + 1], background[source + 2]);
            }
            framebuffer_[static_cast<std::size_t>(y * kScreenWidth + x)] = pixel;
        }
    }

    const int player_screen_x = visual_x() - camera_render_x_ - kPlayerWidth / 2;
    const int player_screen_y = visual_y() - camera_render_y_ - kPlayerHeight;
    SDL_Color body = level_.palette().size() > 14 ? level_.palette()[14] : SDL_Color{255, 180, 40, 255};
    SDL_Color outline = level_.palette().size() > 15 ? level_.palette()[15] : SDL_Color{255, 255, 255, 255};
    for (int y = 0; y < kPlayerHeight; ++y) {
        for (int x = 0; x < kPlayerWidth; ++x) {
            const int screen_x = player_screen_x + x;
            const int screen_y = player_screen_y + y;
            if (screen_x < 0 || screen_x >= kScreenWidth || screen_y < 0 || screen_y >= kScreenHeight) {
                continue;
            }
            const bool edge = x == 0 || x == kPlayerWidth - 1 || y == 0 || y == kPlayerHeight - 1;
            const SDL_Color color = edge ? outline : body;
            framebuffer_[static_cast<std::size_t>(screen_y * kScreenWidth + screen_x)] = rgba(color.r, color.g, color.b);
        }
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
