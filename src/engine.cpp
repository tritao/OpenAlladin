#include "engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace openaladdin {
namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 224;
constexpr int kTerrainVisualOffsetY = 0xF0;
// The player frame origin is one 16-pixel tile above the terrain query
// origin. The ROM keeps these coordinate systems distinct: terrain probes use
// WORLD_Y - 0xF0, while the VDP sprite origin uses WORLD_Y - 0x100.
constexpr int kPlayerVisualOffsetY = 0x100;
constexpr int kTerrainContourRomOffset = 0x2FD2;
constexpr int kTerrainContourRomSize = 0x1000;
constexpr std::uint32_t kTerrainNoOpHandler = 0x001B65BE;
constexpr std::uint8_t kActorGuardType = 0x0A;
constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint8_t kTerrainSpawnActorType = 0x8C;
constexpr std::uint32_t kTerrainSpawnAnimationStream = 0x00124408;
constexpr std::uint32_t kPlayerSwordAnimationStream = 0x0012271A;
constexpr std::uint32_t kActorDeathAnimationStream = 0x00122FA2;
constexpr std::uint32_t kActorSwordDeathAnimationStream = 0x00122DD8;
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
// the native slice makes the behavior -> handler decision data-driven and
// avoids treating an arbitrary behavior-byte range as solid terrain.
constexpr std::array<std::uint32_t, 0x48> kTerrainHandlers = {
    0x001B65BE, 0x001B5492, 0x001B5492, 0x001B5492,
    0x001B5492, 0x001B549C, 0x001B549C, 0x001B549C,
    0x001B65BE, 0x001B54F4, 0x001B5320, 0x001B54F4,
    0x001B54F4, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B5450, 0x001B65BE, 0x001B56F4, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B5458, 0x001B5460,
    0x001B5468, 0x001B65BE, 0x001B65BE, 0x001B575C,
    0x001B5764, 0x001B576C, 0x001B65BE, 0x001B5774,
    0x001B5318, 0x001B65BE, 0x001B54D8, 0x001B54D8,
    0x001B54D2, 0x001B54E0, 0x001B65BE, 0x001B54A6,
    0x001B55E8, 0x001B557E, 0x001B55D8, 0x001B5502,
    0x001B65BE, 0x001B56B6, 0x001B65BE, 0x001B65BE,
    0x001B537A, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B536C, 0x001B53A2, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B5470
};

std::uint32_t terrain_handler(std::uint8_t behavior) {
    return behavior < kTerrainHandlers.size() ? kTerrainHandlers[behavior] : kTerrainNoOpHandler;
}

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

void Level::load(const std::string& asset_root, const std::string& rom_path) {
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
    contour_table_.clear();
    if (!rom_path.empty()) {
        const auto rom = read_file(rom_path);
        if (rom.size() >= static_cast<std::size_t>(kTerrainContourRomOffset + kTerrainContourRomSize)) {
            contour_table_.assign(
                rom.begin() + kTerrainContourRomOffset,
                rom.begin() + kTerrainContourRomOffset + kTerrainContourRomSize
            );
        }
    }
    const auto palette_bytes = read_file(asset_root + "/raw/palette.bin");
    if (palette_bytes.size() < 32) {
        throw std::runtime_error("level palette is too short");
    }
    palette_.clear();
    palette_.reserve(palette_bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < palette_bytes.size(); i += 2) {
        const std::uint16_t word = static_cast<std::uint16_t>(palette_bytes[i] << 8 | palette_bytes[i + 1]);
        const auto channel = [](std::uint16_t value, int shift) {
            static constexpr std::uint8_t levels[8] = {
                0, 52, 87, 116, 144, 172, 206, 255,
            };
            return levels[(value >> shift) & 7];
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

Level::TerrainCell Level::resolve_player_cell(int world_x, int world_y) const {
    TerrainCell cell;
    const int terrain_y = world_y - kTerrainVisualOffsetY;
    if (terrain_y < 0 || terrain_y >= map_height_ * 16) {
        return cell;
    }

    const int row = terrain_y >> 4;
    const int column = (world_x + 16) >> 4;
    if (column < 0 || column >= map_width_ || row < 0 || row >= map_height_) {
        return cell;
    }

    const std::uint16_t word = terrain_words_[static_cast<std::size_t>(row * map_width_ + column)];
    const std::size_t behavior_index = 2 + (word >> 1);
    if (behavior_index >= floor_data_.size()) {
        return cell;
    }

    cell.valid = true;
    cell.column = column;
    cell.row = row;
    cell.terrain_word = word;
    cell.behavior = floor_data_[behavior_index];
    cell.handler = terrain_handler(cell.behavior);
    return cell;
}

Level::TerrainQuery Level::query_player(int world_x, int world_y) const {
    TerrainQuery query;
    query.resolver = resolve_player_cell(world_x, world_y);
    return query;
}

Level::TerrainCollisionFlags Level::query_player_collision(
    int world_x,
    int world_y,
    std::uint8_t landing_state
) const {
    TerrainCollisionFlags flags;

    // Player_TerrainCollisionProbe uses WORLD_CAMERA_Y + PLAYER_Y - 0x110
    // as a pixel-space row selector and WORLD_CAMERA_X + PLAYER_X as the
    // column selector. The ROM's row pointers are equivalent to these
    // fixed-size level-01 indices.
    const int collision_y = world_y - 0x110;
    const int row = collision_y >> 4;
    const int column = world_x >> 4;
    if (collision_y < 0 || row < 0 || row >= map_height_ - 3
        || column < 0 || column + 4 >= map_width_) {
        return flags;
    }

    const auto blocking = [this](int test_column, int test_row) {
        if (test_column < 0 || test_column >= map_width_
            || test_row < 0 || test_row >= map_height_) {
            return false;
        }
        // The collision probe deliberately uses a different interpretation
        // from Terrain_ResolvePlayerCell: bytes >= 0xE0 are blocking geometry,
        // while ordinary floor behavior 0x11 remains traversable here.
        return terrain_behavior(test_column, test_row) >= 0xE0;
    };

    // Left side: the ROM first requires the two cells under the player's
    // left column to be traversable, then records the two adjacent probes and
    // the deeper wall condition. Only the terminal stop bit is consumed by
    // the player motion path, but the complete condition is retained here.
    const bool left_passable = !blocking(column, row) && !blocking(column, row + 1);
    if (left_passable) {
        flags.left_inner = blocking(column - 1, row + 1);
        flags.left_outer = blocking(column - 2, row + 1);
        flags.stop_left = blocking(column, row + 2)
            || (landing_state == 0 && blocking(column, row + 3));
    } else {
        flags.stop_left = true;
    }

    // The ROM restores the base pointer and advances it by four bytes before
    // the right-side pass. That is two terrain words, not one: the right
    // group begins at column + 2 and its inner/outer probes are +3/+4.
    const bool right_passable = !blocking(column + 2, row) && !blocking(column + 2, row + 1);
    if (right_passable) {
        flags.right_inner = blocking(column + 3, row + 1);
        flags.right_outer = blocking(column + 4, row + 1);
        flags.stop_right = blocking(column + 2, row + 2)
            || (landing_state == 0 && blocking(column + 2, row + 3));
    } else {
        flags.stop_right = true;
    }

    // FFF0CB is the ceiling bit: the word immediately to the right of the
    // base pointer at the probe row must be blocking. The ROM bounds-checks
    // this pointer before testing it; the column bounds above provide the
    // same protection for the fixed level map.
    flags.stop_upward = blocking(column + 1, row);
    return flags;
}

Level::TerrainContour Level::query_player_contour(
    int world_x,
    int world_y,
    std::uint16_t surface_mode
) const {
    TerrainContour result;
    if (contour_table_.size() < static_cast<std::size_t>(kTerrainContourRomSize)) {
        return result;
    }

    // Player_FloorContour bounds its source lookup with:
    //   (WORLD_CAMERA_Y + PLAYER_Y) - 0x100
    //   < LEVEL_HEIGHT_PIXELS - 0x20
    // FF98C4 is one 16-pixel row ahead of the resolver's FF9884 table, so
    // the first candidate is the same row selected by the 0xF0 resolver.
    const int lookup_y = world_y - 0x100;
    if (lookup_y < 0 || lookup_y >= map_height_ * 16 - 0x20) {
        return result;
    }

    const int base_row = (world_y - kTerrainVisualOffsetY) >> 4;
    const int column = (world_x + 16) >> 4;
    if (base_row < 0 || base_row + 2 >= map_height_
        || column < 0 || column >= map_width_) {
        return result;
    }

    const int x_fraction = world_x & 0x0F;
    for (int candidate = 0; candidate < 3; ++candidate) {
        const int row = base_row + candidate;
        const std::uint16_t word = terrain_words_[static_cast<std::size_t>(row * map_width_ + column)];
        const std::size_t floor_index = static_cast<std::size_t>(word >> 1)
            + static_cast<std::size_t>(surface_mode);
        if (floor_index >= floor_data_.size()) {
            continue;
        }

        const std::uint8_t floor_type = floor_data_[floor_index];
        const int fraction = candidate == 2 ? 2 : x_fraction;
        const std::size_t contour_index = static_cast<std::size_t>(floor_type) * 16
            + static_cast<std::size_t>(fraction);
        if (contour_index >= contour_table_.size()) {
            continue;
        }

        const std::uint8_t contour = static_cast<std::uint8_t>(contour_table_[contour_index] & 0x3F);
        if (contour == 0) {
            continue;
        }

        const int candidate_y = world_y - 16 + candidate * 16;
        result.valid = true;
        result.row = row;
        result.column = column;
        result.target_world_y = (candidate_y & ~0x0F) + contour - 1;
        result.floor_type = floor_type;
        result.contour = contour;
        return result;
    }

    return result;
}

void Engine::load(
    const std::string& asset_root,
    const std::string& sprite_root,
    const std::string& rom_path,
    const std::string& actor_records_path,
    const std::string& actor_timeline_path
) {
    level_.load(asset_root, rom_path);
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
    actor_templates_.fill({});
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
        std::string flags;
        std::string facing_x_flip;
        std::string facing_y_flip;
        std::string movement_command_timer;
        const int slot = std::stoi(first, nullptr, 0);
        if (!(row >> type >> x >> y >> movement_pc >> frame_ptr >> animation_pc >> flags)
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
        actor.facing_x_flip = (row >> facing_x_flip)
            ? static_cast<std::uint8_t>(parse(facing_x_flip))
            : 0;
        actor.facing_y_flip = (row >> facing_y_flip)
            ? static_cast<std::uint8_t>(parse(facing_y_flip))
            : 0;
        actor.movement_command_timer = (row >> movement_command_timer)
            ? static_cast<std::uint8_t>(parse(movement_command_timer))
            : 0;
        actor.movement_loop_pc = (row >> movement_loop_pc)
            ? static_cast<std::uint32_t>(parse(movement_loop_pc))
            : 0;
        actor.movement_loop_timer = (row >> movement_loop_timer)
            ? static_cast<std::uint8_t>(parse(movement_loop_timer))
            : 0;
        actor.movement_return_pc = (row >> movement_return_pc)
            ? static_cast<std::uint32_t>(parse(movement_return_pc))
            : 0;
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

void Engine::update_actor_movement() {
    if (rom_bytes_.empty()) return;

    const auto read_u8 = [&](std::uint32_t address) -> std::uint8_t {
        if (address >= rom_bytes_.size()) return 0;
        return rom_bytes_[address];
    };
    const auto read_u32 = [&](std::uint32_t address) -> std::uint32_t {
        if (address + 3 >= rom_bytes_.size()) return 0;
        return (static_cast<std::uint32_t>(read_u8(address)) << 24)
            | (static_cast<std::uint32_t>(read_u8(address + 1)) << 16)
            | (static_cast<std::uint32_t>(read_u8(address + 2)) << 8)
            | read_u8(address + 3);
    };
    const auto read_u16 = [&](std::uint32_t address) -> std::uint16_t {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(read_u8(address)) << 8)
            | read_u8(address + 1));
    };
    const auto write_actor_value = [](ActorState& actor, std::uint16_t offset, int size, std::uint32_t value) {
        if (size == 1) {
            const auto byte = static_cast<std::uint8_t>(value);
            switch (offset) {
            case 0x00: actor.type = byte; break;
            case 0x09: actor.facing_x_flip = byte; break;
            case 0x12: actor.movement_loop_timer = byte; break;
            case 0x35: actor.facing_y_flip = byte; break;
            case 0x36: actor.movement_command_timer = byte; break;
            case 0x3C: actor.flags = byte; break;
            default: break;
            }
        } else if (size == 2) {
            const auto word = static_cast<std::uint16_t>(value);
            switch (offset) {
            case 0x02: actor.x = word; break;
            case 0x04: actor.y = word; break;
            default: break;
            }
        } else {
            switch (offset) {
            case 0x0A: actor.movement_pc = value; break;
            case 0x0E: actor.movement_loop_pc = value; break;
            case 0x14: actor.frame_ptr = value; break;
            case 0x20: actor.animation_pc = value; break;
            case 0x38: actor.movement_return_pc = value; break;
            default: break;
            }
        }
    };

    for (ActorState& actor : actors_) {
        if (actor.type == 0 || actor.terminal_timer != 0 || actor.movement_pc == 0) {
            continue;
        }

        std::uint32_t cursor = actor.movement_pc;
        if (cursor + 1 >= rom_bytes_.size()) continue;
        int delta_x = static_cast<std::int8_t>(read_u8(cursor));
        int delta_y = static_cast<std::int8_t>(read_u8(cursor + 1));
        if (actor.facing_x_flip != 0) delta_x = -delta_x;
        if (actor.facing_y_flip != 0) delta_y = -delta_y;
        actor.x = static_cast<std::uint16_t>(static_cast<int>(actor.x) + delta_x);
        actor.y = static_cast<std::uint16_t>(static_cast<int>(actor.y) + delta_y);
        cursor += 2;

        // The ROM applies the signed delta on every tick. The command timer
        // gates only the command dispatch that follows it, so a delayed
        // stream continues moving along the current step each frame.
        if (actor.movement_command_timer != 0) {
            --actor.movement_command_timer;
            continue;
        }

        bool cursor_committed = false;
        for (int command_count = 0; command_count < 32; ++command_count) {
            const std::uint8_t opcode = read_u8(cursor);
            if (opcode < 0x80 || opcode > 0x94) break;

            switch (opcode) {
            case 0x80:  // Movement_Jump: absolute long target.
                cursor = read_u32(cursor + 2);
                if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                    actor.movement_pc = cursor;
                    cursor_committed = true;
                    break;
                }
                continue;
            case 0x81:  // Movement_ToggleFacing.
                if (read_u8(cursor + 1) != 0) {
                    actor.facing_y_flip = static_cast<std::uint8_t>(actor.facing_y_flip ^ 0xFF);
                } else {
                    actor.facing_x_flip = static_cast<std::uint8_t>(actor.facing_x_flip ^ 0xFF);
                }
                cursor += 2;
                continue;
            case 0x82:  // Movement_ClearCursor.
                if (read_u8(cursor + 1) == 0) {
                    actor.movement_pc = 0;
                    cursor_committed = true;
                    break;
                }
                actor.animation_pc = 0;
                cursor += 2;
                continue;
            case 0x83: {  // Movement_WriteActorOrRamValue.
                const std::uint8_t operand = read_u8(cursor + 1);
                const int size = (operand & 0x0F) == 1 || (operand & 0x0F) == 2 ? 2 : 4;
                const std::uint16_t offset = read_u16(cursor + 2);
                const std::uint32_t value = size == 2
                    ? read_u16(cursor + 4)
                    : read_u32(cursor + 4);
                // Absolute work-RAM writes need a RAM model. Actor-relative
                // writes are safe to mirror now and cover the confirmed
                // movement streams that update animation/state cursors.
                if ((operand & 0x10) != 0) {
                    write_actor_value(actor, offset, size, value);
                }
                cursor += static_cast<std::uint32_t>(4 + size);
                continue;
            }
            case 0x84:  // Movement_SetCommandTimer.
                // In movement mode the shared handler uses the high bit to
                // select the per-frame command timer. Without it, the
                // command initializes the separate loop cursor/timer pair.
                if ((read_u8(cursor + 1) & 0x80) != 0) {
                    actor.movement_command_timer = static_cast<std::uint8_t>(read_u8(cursor + 1) & 0x7F);
                    actor.movement_pc = cursor + 2;
                    cursor_committed = true;
                    break;
                } else {
                    actor.movement_loop_timer = read_u8(cursor + 1);
                    actor.movement_loop_pc = cursor + 2;
                }
                // A loop timer is setup inline with the current movement
                // step. The interpreter continues through the rest of that
                // step, so a following arithmetic command is consumed now
                // rather than being mistaken for the next signed delta.
                cursor += 2;
                continue;
            case 0x85:  // Movement_RewindAfterTimer.
                // The original handler consumes one loop count and the
                // interpreter rewinds to the saved loop cursor whenever the
                // counter was non-zero on entry. This includes the final
                // decrement to zero; the next pass then falls through.
                if (actor.movement_loop_timer != 0) {
                    --actor.movement_loop_timer;
                    actor.movement_pc = actor.movement_loop_pc;
                    cursor_committed = true;
                    break;
                }
                cursor += 2;
                continue;
            case 0x87: {  // Movement_AddSignedActorOffset.
                const int delta = static_cast<std::int16_t>(
                    (static_cast<std::uint16_t>(read_u8(cursor + 2)) << 8)
                    | read_u8(cursor + 3));
                if (read_u8(cursor + 1) == 0) {
                    const int mirrored = actor.facing_x_flip != 0 ? -delta : delta;
                    actor.x = static_cast<std::uint16_t>(static_cast<int>(actor.x) + mirrored);
                } else {
                    const int mirrored = actor.facing_y_flip != 0 ? -delta : delta;
                    actor.y = static_cast<std::uint16_t>(static_cast<int>(actor.y) + mirrored);
                }
                cursor += 4;
                continue;
            }
            case 0x90: {  // Movement_AddOrSubtractActorWord.
                const std::uint8_t mode = read_u8(cursor + 1);
                const std::uint16_t offset = read_u16(cursor + 2);
                const auto value = static_cast<std::int16_t>(read_u16(cursor + 4));
                const std::uint8_t width = static_cast<std::uint8_t>(mode & 0x3F);

                // The confirmed sword stream uses actor-relative word
                // arithmetic (mode 0x42, offset +0x1A). Keep the other
                // address/width variants consumed but inert until their RAM
                // targets are identified from a trace.
                if ((mode & 0x40) != 0 && width == 2) {
                    std::int16_t* target = nullptr;
                    if (offset == 0x18) target = &actor.movement_word_18;
                    if (offset == 0x1A) target = &actor.movement_word_1a;
                    if (target != nullptr) {
                        if ((mode & 0x80) != 0) {
                            *target = static_cast<std::int16_t>(*target - value);
                        } else {
                            *target = static_cast<std::int16_t>(*target + value);
                        }
                    }
                }
                cursor += 6;
                continue;
            }
            case 0x8C:  // Movement_ClearActor.
                if (read_u8(cursor + 1) == 0 && (actor.flags & 0x04) == 0) {
                    actor = ActorState{};
                    cursor_committed = true;
                    break;
                }
                cursor += 2;
                continue;
            case 0x8D:  // Movement_FacePlayer.
                actor.facing_x_flip = player_world_x() < static_cast<int>(actor.x) ? 0xFF : 0;
                cursor += 2;
                continue;
            case 0x8E:  // Movement_SelectDynamicStream.
                // FUN_001AD150 returns the default actor stream in the
                // no-player-state path used by the controlled probe. The
                // remaining global-state selector branches need their own
                // RAM fixtures before they can be mirrored safely.
                actor.movement_pc = 0x00121AD8;
                cursor_committed = true;
                break;
            case 0x91:  // Movement_PushParameter; no positional effect yet.
                cursor += 6;
                continue;
            case 0x92: {  // Movement_CallOrReturn.
                const std::uint8_t operand = read_u8(cursor + 1);
                if ((operand & 0x80) != 0) {
                    cursor = actor.movement_return_pc;
                } else {
                    actor.movement_return_pc = cursor + 6;
                    cursor = read_u32(cursor + 2);
                }
                if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                    actor.movement_pc = cursor;
                    cursor_committed = true;
                    break;
                }
                continue;
            }
            case 0x93: {  // Movement_PlayerWithinX.
                const int threshold = read_u8(cursor + 1) == 0xFF
                    ? 0x140
                    : read_u8(cursor + 1);
                const int distance = std::abs(player_world_x() - static_cast<int>(actor.x));
                if (distance <= threshold) {
                    cursor = read_u32(cursor + 2);
                    if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                        actor.movement_pc = cursor;
                        cursor_committed = true;
                        break;
                    }
                    continue;
                }
                // A failed condition retries the same step on the next tick.
                cursor_committed = true;
                break;
            }
            case 0x94: {  // Movement_PlayerWithinY.
                const int threshold = read_u8(cursor + 1);
                const int distance = std::abs(player_world_y() - static_cast<int>(actor.y));
                if (distance <= threshold) {
                    cursor = read_u32(cursor + 2);
                    if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                        actor.movement_pc = cursor;
                        cursor_committed = true;
                        break;
                    }
                    continue;
                }
                // The original Y condition falls through when the player is
                // outside the threshold; the inline jump after the command
                // handles the alternate path.
                cursor += 6;
                continue;
            }
            default:
                // Other conditional, RAM-write, spawn, and velocity commands
                // are intentionally not guessed. Leave the cursor at the command
                // so a trace can identify the next missing handler.
                actor.movement_pc = cursor;
                cursor_committed = true;
                break;
            }
            break;
        }
        if (!cursor_committed) {
            actor.movement_pc = cursor;
        }
    }
}

void Engine::update_actor_animations() {
    if (rom_bytes_.empty()) return;

    const AnimationContext context{
        player_.x,
        player_.y,
        player_world_x(),
        player_world_y(),
        player_.vx,
        player_.vy,
        player_.grounded,
        player_.terrain_response_timer_state,
    };
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        ActorState& actor = actors_[slot];
        if (actor.type == 0 || actor.animation_pc == 0 || actor.terminal_timer != 0) {
            continue;
        }

        ActorAnimationState animation_state;
        animation_state.type = actor.type;
        animation_state.x = actor.x;
        animation_state.y = actor.y;
        animation_state.facing_x_flip = actor.facing_x_flip;
        animation_state.facing_y_flip = actor.facing_y_flip;
        animation_state.flags = actor.flags;
        animation_state.animation_pc = actor.animation_pc;
        animation_state.frame_ptr = actor.frame_ptr;
        animation_state.animation_timer = actor.animation_timer;
        actor_animations_[slot].update_actor(animation_state, context);

        actor.type = animation_state.type;
        actor.x = animation_state.x;
        actor.y = animation_state.y;
        actor.facing_x_flip = animation_state.facing_x_flip;
        actor.facing_y_flip = animation_state.facing_y_flip;
        actor.flags = animation_state.flags;
        actor.animation_pc = animation_state.animation_pc;
        actor.frame_ptr = animation_state.frame_ptr;
        actor.animation_timer = animation_state.animation_timer;
    }
}

void Engine::apply_animation_spawns() {
    AnimationSpawnRequest request;
    while (animation_.take_spawn_request(request)) {
        // Mode 3 is the player weapon/effect allocation path recovered from
        // the live sword stream. Other F5 modes select linked/common records
        // whose ownership rules are not yet represented by this native table;
        // leaving them queued-free avoids inventing an allocation policy.
        if (!request.valid || request.mode != 3) continue;

        const auto read_u8 = [&](std::uint32_t address) -> std::uint8_t {
            return address < rom_bytes_.size() ? rom_bytes_[address] : 0;
        };
        const auto read_u32 = [&](std::uint32_t address) -> std::uint32_t {
            if (address + 3 >= rom_bytes_.size()) return 0;
            return (static_cast<std::uint32_t>(read_u8(address)) << 24)
                | (static_cast<std::uint32_t>(read_u8(address + 1)) << 16)
                | (static_cast<std::uint32_t>(read_u8(address + 2)) << 8)
                | read_u8(address + 3);
        };
        if (request.template_address + 0x12 >= rom_bytes_.size()) continue;

        // The compact template layout is the one consumed by
        // Actor_InitializeFromTemplate at 0x001AE30A:
        // type +0, movement pointer +6, animation pointer +0xC, resource
        // count +0x10, facing-Y +0x11, flags +0x12.
        ActorState spawned;
        spawned.type = read_u8(request.template_address);
        spawned.movement_pc = read_u32(request.template_address + 6);
        spawned.animation_pc = read_u32(request.template_address + 0x0C);
        spawned.flags = read_u8(request.template_address + 0x12);
        spawned.facing_x_flip = request.source_facing_x_flip;
        spawned.facing_y_flip = request.source_facing_y_flip;
        if (request.animation_override != 0) {
            spawned.animation_pc = request.animation_override;
        }
        if (request.movement_override != 0) {
            spawned.movement_pc = request.movement_override;
        }

        int offset_x = request.offset_x;
        int offset_y = request.offset_y;
        if (request.source_facing_x_flip != 0) offset_x = -offset_x;
        if (request.source_facing_y_flip != 0) offset_y = -offset_y;
        spawned.x = static_cast<std::uint16_t>(request.source_world_x + offset_x);
        spawned.y = static_cast<std::uint16_t>(request.source_world_y + offset_y);

        // Mode 3 allocates from the seven auxiliary records scanned by the
        // actor-to-actor collision pass (0x001ABD7E). The ROM chooses the
        // first free record in this pool.
        for (std::size_t slot = 25; slot < 32; ++slot) {
            if (actors_[slot].type != 0) continue;
            actors_[slot] = spawned;
            actor_animations_[slot].reset();
            break;
        }
    }
}

void Engine::update_actor_actor_collisions() {
    if (rom_bytes_.empty()) return;

    // FUN_001ABD7E scans the seven auxiliary records (slots 25..31) as
    // collision sources and the 24 gameplay records (slots 0..23) as
    // targets. This is deliberately separate from the player/actor pass:
    // the player sword is itself an actor by the time the guard handler runs.
    const auto terminalize = [](ActorState& actor, std::uint32_t animation_stream, std::uint8_t frames) {
        actor.type = kActorTerminalType;
        actor.movement_pc = 0;
        actor.animation_pc = animation_stream;
        actor.frame_ptr = 0;
        actor.flags = 0;
        actor.facing_x_flip = 0;
        actor.facing_y_flip = 0;
        actor.terminal_timer = frames;
    };

    for (std::size_t source_slot = 25; source_slot < 32; ++source_slot) {
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

        for (std::size_t target_slot = 0; target_slot < 24; ++target_slot) {
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
            if (source.type == kActorSwordType && target.type == kActorGuardType) {
                terminalize(target, kActorDeathAnimationStream, kActorDeathFrames);
                terminalize(source, kActorSwordDeathAnimationStream, kActorSwordTerminalFrames);
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
        std::string flags;
        std::string facing_x_flip;
        std::string facing_y_flip;
        std::string movement_command_timer;
        if (!(row >> slot >> type >> x >> y >> movement_pc >> frame_ptr >> animation_pc >> flags)
            || slot < 0 || slot >= static_cast<int>(actor_templates_.size())) {
            throw std::runtime_error(
                "invalid actor record at " + path + ":" + std::to_string(line_number));
        }
        auto parse = [](const std::string& value) {
            return std::stoul(value, nullptr, 0);
        };
        ActorState& actor = actor_templates_[static_cast<std::size_t>(slot)];
        actor.type = static_cast<std::uint8_t>(parse(type));
        actor.x = static_cast<std::uint16_t>(parse(x));
        actor.y = static_cast<std::uint16_t>(parse(y));
        actor.movement_pc = static_cast<std::uint32_t>(parse(movement_pc));
        actor.frame_ptr = static_cast<std::uint32_t>(parse(frame_ptr));
        actor.animation_pc = static_cast<std::uint32_t>(parse(animation_pc));
        actor.flags = static_cast<std::uint8_t>(parse(flags));
        actor.facing_x_flip = (row >> facing_x_flip)
            ? static_cast<std::uint8_t>(parse(facing_x_flip))
            : 0;
        actor.facing_y_flip = (row >> facing_y_flip)
            ? static_cast<std::uint8_t>(parse(facing_y_flip))
            : 0;
        actor.movement_command_timer = (row >> movement_command_timer)
            ? static_cast<std::uint8_t>(parse(movement_command_timer))
            : 0;
        actor.movement_loop_pc = (row >> movement_loop_pc)
            ? static_cast<std::uint32_t>(parse(movement_loop_pc))
            : 0;
        actor.movement_loop_timer = (row >> movement_loop_timer)
            ? static_cast<std::uint8_t>(parse(movement_loop_timer))
            : 0;
        actor.movement_return_pc = (row >> movement_return_pc)
            ? static_cast<std::uint32_t>(parse(movement_return_pc))
            : 0;
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
        box.left = origin_x - signed_byte(byte(4));
        box.right = origin_x - signed_byte(byte(2));
    }
    box.valid = true;
    return box;
}

void Engine::update_actor_interactions(const InputState& input, bool was_grounded) {
    constexpr std::uint8_t kInteractionFlag = 0x20;
    constexpr int kInteractionLeft = 0x0E;
    constexpr int kInteractionRight = 0x10;
    const int world_x = player_world_x();
    const int world_y = player_world_y();

    for (ActorState& actor : actors_) {
        if (actor.terminal_timer != 0) {
            --actor.terminal_timer;
            if (actor.terminal_timer == 0) {
                actor = ActorState{};
            }
            continue;
        }
        if (actor.type == 0) continue;

        // Type 0x1F is the first-level interaction actor observed in the
        // common actor table. Its collision handler raises actor flag bit 5
        // while the player overlaps the actor's horizontal interaction span.
        const bool overlaps_type_1f = actor.type == 0x1F
            && was_grounded
            && input.right && !input.left
            && world_y == actor.y
            && world_x >= static_cast<int>(actor.x) - kInteractionLeft
            && world_x < static_cast<int>(actor.x) + kInteractionRight;
        if (overlaps_type_1f) {
            if ((actor.flags & kInteractionFlag) == 0) {
                actor.flags = static_cast<std::uint8_t>(actor.flags | kInteractionFlag);
                // The selector clears FFF0CC and Player_Update arms a
                // seven-frame camera delay on the following pass. The camera
                // routine consumes one count during this same native update.
                camera_.update_delay = 7;
            }
        } else if ((actor.flags & kInteractionFlag) != 0) {
            actor.flags = static_cast<std::uint8_t>(actor.flags & ~kInteractionFlag);
        }

        // Player/actor collision entry 0x001ABB40 dispatches the actor type
        // after the player and actor rectangles overlap. Both rectangles are
        // addressed by the current animation frame pointer at actor +0x14;
        // this is the same pointer the animation VM writes before the next
        // collision pass. The fixed-ROM guard handler then replaces type
        // 0x0A in place with the shared type-0x84 terminal template.
        const bool sword_active = was_grounded
            && (input.attack_pressed || player_.attack_timer != 0);
        const CollisionBox player_box = read_collision_box(
            animation_.frame_pointer(),
            world_x,
            world_y,
            animation_.facing_left()
        );
        const CollisionBox actor_box = read_collision_box(
            actor.frame_ptr,
            static_cast<int>(actor.x),
            static_cast<int>(actor.y),
            actor.facing_x_flip != 0
        );
        const bool boxes_overlap = player_box.valid && actor_box.valid
            && actor_box.left <= player_box.right
            && actor_box.top <= player_box.bottom
            && player_box.left < actor_box.right
            && player_box.top < actor_box.bottom;
        const bool guard_overlap = actor.type == kActorGuardType && boxes_overlap;
        if (sword_active && guard_overlap) {
            actor.type = kActorTerminalType;
            actor.movement_pc = 0;
            actor.animation_pc = kActorDeathAnimationStream;
            actor.frame_ptr = 0;
            actor.flags = 0;
            actor.terminal_timer = kActorDeathFrames;
        }
    }

    // The actor-to-actor collision pass follows the player/actor dispatch in
    // the frame loop. Keeping it after the legacy direct-player fixture also
    // preserves the existing isolated guard regression while allowing the
    // real type-0x80 sword actor path to drive the same transition.
    update_actor_actor_collisions();
}

void Engine::reset() {
    player_ = PlayerState{};
    actors_ = actor_templates_;
    apply_actor_timeline(0);
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
    camera_.scene_state = 1;
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
    frame_ = 0;
    quit_ = false;
    animation_.reset();
    if (!grounded) {
        animation_.update(
            SpritePose::Jump,
            false,
            AnimationContext{
                player_.x,
                player_.y,
                player_world_x(),
                player_world_y(),
                player_.vx,
                player_.vy,
                grounded,
                player_.terrain_response_timer_state,
            }
        );
    }
}

void Engine::set_checkpoint_terrain_behavior(std::uint8_t behavior) {
    checkpoint_terrain_behavior_override_ = true;
    checkpoint_terrain_behavior_ = behavior;
    player_.terrain_behavior = behavior;
    player_.terrain_landing_state = behavior != 0 ? 1 : 0;
}

void Engine::set_checkpoint_frame_ptr(int address) {
    if (animation_.rom_loaded()) {
        animation_.set_frame_pointer(static_cast<std::uint32_t>(address));
        return;
    }
    const int frame = sprites_.frame_index_for_address(address);
    if (frame < 0 || !animation_.set_frame(frame)) {
        animation_.reset();
    }
}

void Engine::set_checkpoint_animation(std::uint32_t animation_pc, int timer) {
    animation_.set_animation_state(animation_pc, timer);
}

void Engine::set_checkpoint_facing_x_flip(bool facing_x_flip) {
    animation_.set_facing_left(facing_x_flip);
}

void Engine::set_checkpoint_camera(int x, int y, int reference_x, int reference_y, int scroll_x, int scroll_y, int scene_state) {
    camera_.x = x;
    camera_.y = y;
    camera_.reference_x = reference_x;
    camera_.reference_y = reference_y;
    camera_.scroll_x = scroll_x;
    camera_.scroll_y = scroll_y;
    camera_.scene_state = scene_state;
    camera_.special_mode = scene_state == 8 ? 1 : 0;
}

int Engine::visual_x() const {
    return player_world_x();
}

int Engine::visual_y() const {
    // The sprite origin is one tile above the terrain resolver's visual row.
    return player_world_y() - kPlayerVisualOffsetY;
}

SpritePose Engine::sprite_pose() const {
    return animation_.pose();
}

void Engine::update_terrain_input(const InputState& input) {
    // FFF156 is the active-low controller/query byte consumed by the four
    // Terrain_TestQueryFlag helpers. The main loop turns direction bits into
    // FFF07C..FFF07F with SEQ, so a pressed direction is represented by 0xFF.
    player_.terrain_query_result = 0x7F;
    if (input.up) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x01);
    if (input.down) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x02);
    if (input.left) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x04);
    if (input.right) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x08);
    if (input.jump_pressed) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x20);

    player_.terrain_push_right = input.right ? 0xFF : 0;
    player_.terrain_push_left = input.left ? 0xFF : 0;
    player_.terrain_push_up = input.up ? 0xFF : 0;
    player_.terrain_push_down = input.down ? 0xFF : 0;
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
    const Level::TerrainContour contour = level_.query_player_contour(
        player_world_x(), player_world_y(), player_.terrain_surface_mode);
    if (!contour.valid) {
        // Player_FloorContour clears FFF0C1 when all three contour probes are
        // empty. The following state machine then treats the player as
        // airborne and the normal integrator supplies gravity.
        player_.terrain_landing_state = 0;
        player_.grounded = false;
        return;
    }

    // During the upward response phase the ROM deliberately keeps the
    // contour from re-grounding the player on the launch tile. Once the
    // vertical stop is armed, the same contour becomes eligible for landing.
    if ((player_.terrain_response_active != 0 && player_.terrain_vertical_stop == 0)
        || (!player_.grounded && player_.vy < 0 && player_.terrain_vertical_stop == 0)) {
        player_.terrain_landing_state = 0;
        player_.grounded = false;
        return;
    }

    const int target_y = contour.target_world_y - camera_.y;
    const int delta = target_y - player_.y;
    if (delta < -8 || delta > 8) {
        player_.terrain_landing_state = 0;
        player_.grounded = false;
        return;
    }

    // The original writes the contour-adjusted local Y only when the player
    // is within eight pixels of the target. The contour byte itself is the
    // grounded/landing value (flat ground is 1; sloped surfaces retain their
    // ROM value).
    player_.y = target_y;
    player_.vy = 0;
    player_.grounded = true;
    player_.terrain_landing_state = contour.contour;
    player_.terrain_response_active = 0;
    player_.terrain_response_timer_state = 0;
}

void Engine::resolve_terrain(int previous_world_y) {
    Level::TerrainQuery query = level_.query_player(player_world_x(), player_world_y());
    if (checkpoint_terrain_behavior_override_ && query.resolver.valid) {
        query.resolver.behavior = checkpoint_terrain_behavior_;
        query.resolver.handler = terrain_handler(checkpoint_terrain_behavior_);
    }
    const Level::TerrainCell previous_down_probe =
        level_.resolve_player_cell(player_world_x(), previous_world_y + 8);
    const Level::TerrainCell* cell = &query.resolver;
    const bool resolver_has_handler = query.resolver.valid
        && query.resolver.behavior != 0
        && (query.resolver.handler != kTerrainNoOpHandler
            || query.resolver.behavior == 0x11);
    if (player_.vy >= 0 && !resolver_has_handler
        && previous_down_probe.valid && previous_down_probe.behavior != 0
        && (previous_down_probe.handler != kTerrainNoOpHandler
            || previous_down_probe.behavior == 0x11)) {
        // Terrain_ResolvePlayerCell probes the row immediately below the
        // player on the descending path. This catches the floor crossing
        // before PLAYER_Y reaches the floor row itself, matching the ROM's
        // landing snap at the first frame over the surface.
        cell = &previous_down_probe;
    }
    player_.terrain_behavior = cell->valid ? cell->behavior : 0;
    // Terrain_ResolvePlayerCell clears these scratch fields before it looks
    // up the selected behavior. FFF0C2 is deliberately cleared only when
    // the resolved behavior is not 0x47; the 0x47 handler uses it as a
    // one-shot latch on consecutive frames.
    player_.terrain_query_state_a = 0;
    player_.terrain_query_state_b = 0;
    player_.terrain_response_latch = 0;
    player_.terrain_state = 0;
    if (player_.terrain_behavior != 0x47) {
        player_.terrain_surface_latch = 0;
    }

    // The original resolver dispatches the behavior selected by the exact
    // cell. A zero behavior returns without changing motion; it does not
    // search forward for a nearby floor row and it does not add gravity.
    const bool has_handler = cell->valid && cell->behavior != 0
        && (cell->handler != kTerrainNoOpHandler || cell->behavior == 0x11);
    if (!has_handler) {
        // A zero-behavior cell is a no-op for the current terrain state. In
        // particular, the ROM does not turn a grounded player airborne merely
        // because the resolved row has no handler; the landing flag remains
        // latched until an actual vertical transition changes it.
        return;
    }

    // The ROM dispatches the handler on every resolver pass. Individual
    // handlers carry their own guards/latches; dispatching only on behavior
    // transitions loses those semantics for special terrain cells.
    apply_terrain_behavior(*cell);
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
    case 0x0A:  // TerrainHandler_SurfaceInteraction (0x001B5320).
        // The handler first looks for an existing type-0x8C record, then
        // allocates the first free common actor slot (the ROM scans slots
        // 3..22, starting at 0x00FF7F06). It only accepts a non-zero
        // landing/contour result and copies the player's world position into
        // the new record. The animation VM advances the stream on the next
        // frame, so leave frame_ptr clear here just like 0x001AE30A does.
        if (player_.terrain_landing_state == 0) {
            break;
        }
        if (std::any_of(actors_.begin(), actors_.end(), [](const ActorState& actor) {
                return actor.type == kTerrainSpawnActorType;
            })) {
            break;
        }
        for (std::size_t slot = 3; slot <= 22 && slot < actors_.size(); ++slot) {
            if (actors_[slot].type != 0) continue;
            ActorState spawned;
            spawned.type = kTerrainSpawnActorType;
            spawned.x = static_cast<std::uint16_t>(player_world_x());
            spawned.y = static_cast<std::uint16_t>(player_world_y());
            spawned.animation_pc = kTerrainSpawnAnimationStream;
            actors_[slot] = spawned;
            break;
        }
        break;
    case 0x20:  // TerrainHandler_SetTerminalCollision (0x001B5318)
        player_.terrain_terminal_transition = 0xFF;
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
        animation_.set_animation_state(0x00121964, 0);
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
    case 0x2B:  // TerrainHandler_StopAndAlignPlayer (0x001B5502)
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
        animation_.set_animation_state(0x0012181A, 0);
        if (player_.vy < 8) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
        }
        player_.grounded = false;
        break;
    case 0x2D:  // TerrainHandler_BouncePlayerBlock (0x001B56B6)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(0x200);
        animation_.set_animation_state(0x00121AD8, 0);
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
    if (camera_.scroll_left_pending || camera_.scroll_right_pending) {
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
    }
    if (camera_.scroll_up_pending || camera_.scroll_down_pending) {
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

void Engine::update_camera() {
    // 0x001AA8FA delays the follow pass after a player mode/threshold change.
    // The delay is observable in the jump trace: the camera remains still for
    // seven frames after the jump threshold is installed.
    if (camera_.update_delay > 0) {
        --camera_.update_delay;
        return;
    }
    if (camera_.special_mode != 0 || camera_.scene_state == 8) {
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
    vertical_delta();

}

void Engine::update_state08(const InputState& input) {
    // State 08 enters the transition branch at 0x001A9D18 and bypasses the
    // normal physics/camera follower. The controller fallback at 0x001A9C9A
    // moves the local coordinates in eight-pixel steps within these bounds.
    if (input.right && player_.x < 0x130) {
        player_.x += 8;
    }
    if (input.left && player_.x >= 0x10) {
        player_.x -= 8;
    }
    if (input.up && player_.y >= 0x10) {
        player_.y -= 8;
    }
    if (input.down && player_.y < 0x1E0) {
        player_.y += 8;
    }
    player_.grounded = false;
}

void Engine::update(const InputState& input) {
    if (camera_.scene_state == 8) {
        update_state08(input);
        animation_.update(
            SpritePose::Idle,
            input.left && !input.right,
            AnimationContext{
                player_.x,
                player_.y,
                player_world_x(),
                player_world_y(),
                player_.vx,
                player_.vy,
                player_.grounded,
                player_.terrain_response_timer_state,
            }
        );
        apply_actor_timeline(frame_ + 1);
        ++frame_;
        return;
    }
    update_terrain_input(input);
    const bool grounded_before_contour = player_.grounded;
    const bool stable_terrain_handler_fixture = checkpoint_terrain_behavior_override_
        && (checkpoint_terrain_behavior_ == 0x28
            || checkpoint_terrain_behavior_ == 0x29
            || checkpoint_terrain_behavior_ == 0x2D);
    // The normal slice uses the recovered contour pass. Handler fixtures are
    // deliberately staged at the ROM's resolver boundary instead: the
    // original frame calls Terrain_ResolvePlayerCell/handler before the
    // 0x001A9D98 movement integrator, and the fixture supplies the already
    // resolved landing state that the ROM would have in RAM.
    if (!checkpoint_terrain_behavior_override_) {
        apply_floor_contour();
    }
    const bool was_grounded = player_.grounded;
    const bool just_landed = !grounded_before_contour && player_.grounded;
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
    const int input_direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    update_actor_movement();
    update_actor_interactions(input, was_grounded);
    // The launch fixture reaches the ROM handler before its animation pass;
    // keeping the seeded player cursor stable preserves the observed frame
    // state while the native animation VM remains intentionally separate.
    if (!stable_terrain_handler_fixture) {
        update_actor_animations();
    }
    const bool ground_release = was_grounded && !input.jump_pressed && input_direction == 0
        && last_ground_direction_ != 0 && player_.vx == 0;
    const bool vertical_stop_before_frame = player_.terrain_vertical_stop != 0;
    const bool start_jump = input.jump_pressed && was_grounded;
    const AnimationContext animation_context{
        player_.x,
        player_.y,
        player_world_x(),
        player_world_y(),
        player_.vx,
        player_.vy,
        was_grounded,
        player_.terrain_response_timer_state,
    };

    if (was_grounded && player_.grounded) {
        if (input.left != input.right) {
            const int threshold = input.left ? 0xF0 : 0x70;
            if (camera_.horizontal_threshold != threshold) {
                camera_.horizontal_threshold = threshold;
                camera_.update_delay = 7;
            }
        }
        apply_ground_movement(input);
    } else if (input.left && !input.right) {
        player_.vx = player_.vx >= 0 ? static_cast<std::int16_t>(-0x300)
                                     : std::max<std::int16_t>(player_.vx, -0x300);
    } else if (input.right && !input.left) {
        player_.vx = player_.vx <= 0 ? static_cast<std::int16_t>(0x300)
                                     : std::min<std::int16_t>(player_.vx, 0x300);
    }

    const int previous_world_y = player_world_y();
    if (checkpoint_terrain_behavior_override_) {
        resolve_terrain(previous_world_y);
    }
    integrate_motion();
    if (checkpoint_terrain_behavior_override_ && checkpoint_terrain_behavior_ == 0x2B) {
        // The original response path continues through the positive-motion
        // state after TerrainHandler_StopAndAlignPlayer: it advances VY by
        // 0x78. Camera_UpdateFollow then applies the visible four-pixel
        // local-X correction.
        player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
    }
    if (!checkpoint_terrain_behavior_override_) {
        resolve_terrain(previous_world_y);
    }
    if (!player_.grounded && player_.terrain_behavior == 0
        && (vertical_stop_before_frame || player_.terrain_response_timer_state != 0)) {
        // This is the post-integrator jump/vertical-state handoff. It is
        // intentionally after resolve_terrain: the original frame where the
        // residual upward velocity is cleared still exposes VY=0; the next
        // frame starts the positive phase at 0x003C.
        if (player_.terrain_response_timer_state == 0) {
            player_.vy = 0x003C;
            player_.terrain_response_timer_state = 1;
        } else if (player_.vy < 0x800) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x0078);
        }
        // FFF0C0 remains set after the residual-upward stop. The original
        // contour routine uses that latched bit to distinguish the later
        // falling/landing phase while FFF0BE is still active.
    }
    if (start_jump && player_.grounded) {
        // The recovered frame order applies the jump handler after motion and
        // terrain resolution (Player_Update -> Terrain_Resolve -> jump
        // handler). This leaves the impulse visible for the next frame before
        // the integrator consumes it.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
        player_.terrain_response_active = 0xFF;
        player_.terrain_response_timer_state = 0;
        player_.terrain_vertical_stop = 0;
        player_.terrain_landing_state = 0xFF;
        camera_.horizontal_threshold = 0xB0;
        camera_.vertical_threshold = 0x170;
        camera_.update_delay = 7;
    }
    if (player_.ground_braking && player_.vx == 0) {
        player_.ground_braking = false;
    }

    // The ROM updates the follow camera before the tile-update dispatcher
    // consumes a pending 16-pixel reference shift. Keeping the rebase at the
    // end of the frame makes the externally visible state match that order:
    // local movement, damped camera movement, then reference/scroll repair.
    update_camera();
    rebase_camera_reference();
    if (ground_release) {
        // The first no-input frame enters the ROM's inertial ground path after
        // the position/camera work. The exposed velocity is 0x038C (or its
        // signed mirror), then the normal integrator decays it by 0x28.
        player_.vx = static_cast<std::int16_t>(last_ground_direction_ * 0x038C);
        player_.ground_braking = true;
        last_ground_direction_ = 0;
    }
    if (was_grounded && player_.grounded && input_direction != 0) {
        last_ground_direction_ = input_direction;
    } else if (!player_.grounded) {
        last_ground_direction_ = 0;
    }

    const SpritePose desired_pose = !player_.grounded
        ? SpritePose::Jump
        : (just_landed
            ? SpritePose::Landing
            : (animation_.pose() == SpritePose::Landing && !animation_.finished()
                ? SpritePose::Landing
                : (input.left != input.right
                    ? SpritePose::Run
                    : (animation_.pose() == SpritePose::Run || animation_.pose() == SpritePose::Brake
                        ? SpritePose::Brake : SpritePose::Idle))));
    // The original actor VM runs before the player movement update reaches
    // the stable frame boundary. Feed it the pre-integration state so its
    // F4/F2 conditions see the same velocity and landing fields as MAME.
    if (!stable_terrain_handler_fixture) {
        animation_.update(
            desired_pose,
            input.left && !input.right,
            animation_context
        );
    }
    // Player_ProcessInteractionState at 0x001AE4F8 is a RAM-driven stream
    // selector outside the common actor VM. Build its post-physics RAM view
    // after the actor tick so the native path owns the same boundary as the
    // ROM's interaction caller.
    if (!stable_terrain_handler_fixture
        && animation_.rom_loaded() && !just_landed && desired_pose != SpritePose::Landing) {
        AnimationContext selector_context = animation_context;
        selector_context.player_x = player_.x;
        selector_context.player_y = player_.y;
        selector_context.world_x = player_world_x();
        selector_context.world_y = player_world_y();
        selector_context.player_vx = player_.vx;
        selector_context.player_vy = player_.vy;
        selector_context.grounded = player_.grounded;
        auto& selector = selector_context.selector;
        selector.response_active = player_.terrain_response_active;
        selector.landing_state = player_.terrain_landing_state;
        selector.camera_special_mode = static_cast<std::uint8_t>(camera_.special_mode);
        // The release path clears FFF0CC before entering 0x001AE4F8. The
        // native terrain mirror still exposes its one-frame response value,
        // so make this caller-side clear explicit in the selector state.
        selector.response_timer =
            player_.ground_braking
                ? 1
                : desired_pose == SpritePose::Brake
                ? 0
                : std::max<std::uint8_t>(player_.terrain_response_timer_state, 1);
        animation_.select_player_interaction_state(selector_context);
    }
    if (input.attack_pressed && was_grounded && animation_.rom_loaded()) {
        animation_.select_stream_entry(kPlayerSwordAnimationStream);
    }
    apply_animation_spawns();
    apply_actor_timeline(frame_ + 1);
    ++frame_;
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
    // Keep this stream deliberately aligned with re/mame/lua/main.lua. It is
    // intentionally a small, valid subset of the shared schema: scene logic
    // is still a vertical slice, while actor records mirror the captured
    // first-level table and expose the interaction flag used by the camera.
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
    const CollisionBox player_box = read_collision_box(
        animation_.frame_pointer(),
        player_world_x(),
        player_world_y(),
        animation_.facing_left()
    );

    output << "{\"type\":\"state\",\"format\":\"openaladdin-frame-state-v1\""
           << ",\"frame\":" << frame_
           << ",\"input\":\"" << input_token << "\""
           << ",\"player\":{\"x\":" << player_.x
           << ",\"y\":" << player_.y
           << ",\"world_x\":" << player_world_x()
           << ",\"world_y\":" << player_world_y()
           << ",\"vx\":" << player_.vx
           << ",\"vy\":" << player_.vy
           << ",\"animation_pc\":" << animation_.animation_pc()
           << ",\"animation_state\":\""
           << (animation_.pose() == SpritePose::Idle ? "idle"
               : animation_.pose() == SpritePose::Run ? "run"
               : animation_.pose() == SpritePose::Brake ? "brake"
               : animation_.pose() == SpritePose::Jump ? "jump" : "landing")
           << "\""
           << ",\"animation_timer\":" << animation_.timer()
           << ",\"animation_stream_entry\":" << animation_.stream_entry()
           << ",\"sprite_frame\":" << state_sprite_frame
           << ",\"frame_ptr\":" << (animation_.rom_loaded()
               ? animation_.frame_pointer()
               : sprites_.frame(animation_.sprite_frame()).address)
           << ",\"collision_box\":" << collision_box_json(player_box)
           << ",\"facing_left\":" << (animation_.facing_left() ? "true" : "false")
           << ",\"attack_timer\":" << static_cast<unsigned>(player_.attack_timer)
           << ",\"attack_active\":" << (player_.attack_timer != 0 ? "true" : "false")
           << ",\"grounded\":" << (trace_grounded ? "true" : "false") << "}"
           << ",\"scene\":{\"state\":" << camera_.scene_state
           << ",\"script_cursor\":0"
           << ",\"script_data_cursor\":0"
           << ",\"table_index\":0"
           << ",\"script_pending\":0"
           << ",\"vdp_update\":0"
           << ",\"vdp_clear\":0"
           << ",\"transition_event\":0"
           << ",\"script_countdown\":0"
           << ",\"script_gate\":0"
           << ",\"player_gate\":0"
           << ",\"player_lock\":0"
           << ",\"player_countdown\":0"
           << ",\"player_terminal\":" << static_cast<unsigned>(player_.terrain_terminal_transition) << "}"
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
           << ",\"state_08\":" << (camera_.scene_state == 8 ? "true" : "false")
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
           << ",\"actors\":[";
    bool first_actor = true;
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        const ActorState& actor = actors_[slot];
        if (actor.type == 0 && actor.flags == 0) continue;
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
               << ",\"terminal_timer\":" << static_cast<unsigned>(actor.terminal_timer)
               << "}";
    }
    output << "]}\n";
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
