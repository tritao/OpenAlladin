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
constexpr int kTerrainContourRomOffset = 0x2FD2;
constexpr int kTerrainContourRomSize = 0x1000;
constexpr std::uint32_t kTerrainNoOpHandler = 0x001B65BE;
constexpr std::uint8_t kActorGuardType = 0x0A;
constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint8_t kTerrainSpawnActorType = 0x8C;
constexpr std::uint32_t kTerrainSpawnAnimationStream = 0x00124408;
constexpr std::uint32_t kTerrainScene5SpawnTemplate = 0x001B805C;
constexpr std::uint32_t kTerrainScene5SpawnAnimationDefault = 0x001250BA;
constexpr std::uint32_t kTerrainScene5SpawnAnimationLow = 0x001250CE;
constexpr std::uint32_t kTerrainScene5SpawnAnimationHigh = 0x001250DE;
constexpr std::uint32_t kPlayerSwordAnimationStream = 0x0012271A;
constexpr std::uint32_t kPlayerAttackTransitionStream = 0x00122034;
constexpr std::uint32_t kPlayerSwordStableStream = 0x001223E2;
constexpr std::uint32_t kPlayerSwordFirstFrame = 0x001ED34A;
constexpr std::uint32_t kPlayerUpAnimationStream = 0x00122236;
constexpr std::uint32_t kPlayerDownAnimationStream = 0x001222D2;
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

    const std::string parallax_path = asset_root + "/parallax.ppm";
    std::ifstream parallax_file(parallax_path, std::ios::binary);
    if (parallax_file.good()) {
        const auto parallax = read_ppm(parallax_path);
        parallax_width_ = parallax.width;
        parallax_height_ = parallax.height;
        parallax_rgba_ = parallax.rgba;
    } else {
        parallax_width_ = 0;
        parallax_height_ = 0;
        parallax_rgba_.clear();
    }

    const auto map_bytes = read_file(asset_root + "/raw/map.bin");
    terrain_words_ = read_be_words(map_bytes);
    if (terrain_words_.size() != static_cast<std::size_t>(map_width_ * map_height_)) {
        throw std::runtime_error("level-01 map is not 300x45 terrain words");
    }

    floor_data_ = read_file(asset_root + "/raw/floor.bin");
    interaction_records_.clear();
    // Level-01's interaction resource is the fourth byte-oriented table in
    // floor.bin. The map word is a 16-bit resource reference, so both the
    // terrain and interaction lookups share the same (word >> 1) index.
    for (int row = 0; row < map_height_; ++row) {
        for (int column = 0; column < map_width_; ++column) {
            const std::uint16_t terrain_word = terrain_words_[
                static_cast<std::size_t>(row * map_width_ + column)];
            const std::uint16_t resource_offset = static_cast<std::uint16_t>(terrain_word >> 1);
            const std::size_t selector_index = static_cast<std::size_t>(3 + resource_offset);
            if (selector_index >= floor_data_.size() || floor_data_[selector_index] == 0) {
                continue;
            }
            interaction_records_.push_back(InteractionRecord{
                column,
                row,
                terrain_word,
                resource_offset,
                floor_data_[selector_index],
                column * 16,
                row * 16 + kTerrainVisualOffsetY,
            });
        }
    }
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

std::uint8_t Level::interaction_selector(int column, int row) const {
    if (column < 0 || column >= map_width_ || row < 0 || row >= map_height_) {
        return 0;
    }
    const std::uint16_t terrain_word = terrain_words_[
        static_cast<std::size_t>(row * map_width_ + column)];
    const std::size_t selector_index = static_cast<std::size_t>(3 + (terrain_word >> 1));
    return selector_index < floor_data_.size() ? floor_data_[selector_index] : 0;
}

void InteractionMap::load(const Level& level) {
    records_ = level.interaction_records();
    selectors_.assign(level.floor_data().size(), 0);
    for (const Level::InteractionRecord& record : records_) {
        const std::size_t index = static_cast<std::size_t>(3 + record.resource_offset);
        if (index < selectors_.size()) {
            selectors_[index] = record.selector;
        }
    }
}

void InteractionMap::reset() {
    for (const Level::InteractionRecord& record : records_) {
        const std::size_t index = static_cast<std::size_t>(3 + record.resource_offset);
        if (index < selectors_.size()) {
            selectors_[index] = record.selector;
        }
    }
}

std::uint8_t InteractionMap::selector(int column, int row) const {
    for (const Level::InteractionRecord& record : records_) {
        if (record.column == column && record.row == row) {
            return selector(record);
        }
    }
    return 0;
}

std::uint8_t InteractionMap::selector(const Level::InteractionRecord& record) const {
    const std::size_t index = static_cast<std::size_t>(3 + record.resource_offset);
    return index < selectors_.size() ? selectors_[index] : 0;
}

bool InteractionMap::consume(std::uint16_t resource_offset) {
    const std::size_t index = static_cast<std::size_t>(3 + resource_offset);
    if (index >= selectors_.size() || selectors_[index] == 0) {
        return false;
    }
    selectors_[index] = 0;
    return true;
}

std::size_t InteractionMap::active_record_count() const {
    std::size_t count = 0;
    for (const Level::InteractionRecord& record : records_) {
        if (selector(record) != 0) ++count;
    }
    return count;
}

bool Level::is_vdp_transparent(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue
) const {
    // A Genesis plane pixel with color index zero is transparent. The
    // extracted PPMs preserve the selected palette line, so compare against
    // color zero from each of the four palette lines rather than a single
    // RGB key.
    for (int line = 0; line < 4; ++line) {
        const std::size_t index = static_cast<std::size_t>(line * 16);
        if (index < palette_.size()
            && palette_[index].r == red
            && palette_[index].g == green
            && palette_[index].b == blue) {
            return true;
        }
    }
    return false;
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
    actor_snapshot_mode_ = !actor_records_path.empty() || !actor_timeline_path.empty();
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
    auto free_slot = [this](int slot) -> std::optional<std::size_t> {
        if (slot < 0 || slot >= static_cast<int>(actors_.size())) return std::nullopt;
        return actors_[static_cast<std::size_t>(slot)].type == 0
            ? std::optional<std::size_t>(static_cast<std::size_t>(slot))
            : std::nullopt;
    };

    switch (pool) {
    case ActorAllocationPool::CommonForward:
        for (int slot = 3; slot <= 22; ++slot) {
            if (auto found = free_slot(slot)) return found;
        }
        break;
    case ActorAllocationPool::CommonReverse:
        for (int slot = 20; slot >= 1; --slot) {
            if (auto found = free_slot(slot)) return found;
        }
        break;
    case ActorAllocationPool::GameplayForward:
        for (int slot = 1; slot <= 23; ++slot) {
            if (auto found = free_slot(slot)) return found;
        }
        break;
    case ActorAllocationPool::GameplayReverse:
        for (int slot = 23; slot >= 1; --slot) {
            if (auto found = free_slot(slot)) return found;
        }
        break;
    }
    return std::nullopt;
}

ActorState Engine::actor_from_template(std::uint32_t template_address) const {
    ActorState actor;
    const auto read_u8 = [this](std::uint32_t address) -> std::uint8_t {
        return address < rom_bytes_.size() ? rom_bytes_[address] : 0;
    };
    const auto read_u32 = [&read_u8](std::uint32_t address) -> std::uint32_t {
        return (static_cast<std::uint32_t>(read_u8(address)) << 24)
            | (static_cast<std::uint32_t>(read_u8(address + 1)) << 16)
            | (static_cast<std::uint32_t>(read_u8(address + 2)) << 8)
            | read_u8(address + 3);
    };
    if (template_address + 0x12 >= rom_bytes_.size()) return actor;
    actor.type = read_u8(template_address);
    actor.movement_flags = read_u8(template_address + 2);
    actor.facing_x_flip = read_u8(template_address + 5);
    actor.movement_pc = read_u32(template_address + 6);
    actor.animation_pc = read_u32(template_address + 0x0C);
    actor.resource_count = read_u8(template_address + 0x10);
    actor.facing_y_flip = read_u8(template_address + 0x11);
    actor.flags = read_u8(template_address + 0x12);
    return actor;
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

    ActorState actor = actor_from_template(descriptor->template_address);
    if (actor.type == 0 && !descriptor->override_type) return;
    if (descriptor->override_type) actor.type = descriptor->type;
    if (descriptor->override_animation) actor.animation_pc = descriptor->animation_pc;
    if (descriptor->override_movement) actor.movement_pc = descriptor->movement_pc;
    if (descriptor->override_resource_count) actor.resource_count = descriptor->resource_count;
    actor.x = static_cast<std::uint16_t>(base_x + descriptor->post_offset_x);
    actor.y = static_cast<std::uint16_t>(base_y + descriptor->post_offset_y);
    actor.interaction_resource_offset = record.resource_offset;
    actor.interaction_selector = selector;
    actor.spawned_by_interaction = true;
    actors_[*slot] = actor;
    actor_animations_[*slot].reset();
    interaction_map_.consume(record.resource_offset);
}

void Engine::scan_interaction_refill_window() {
    if (actor_snapshot_mode_ || rom_bytes_.empty()) return;

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

void Engine::update_dynamic_actor_culling() {
    if (actor_snapshot_mode_) return;
    const int left = camera_.x - 0x120;
    const int right = camera_.x + kScreenWidth + 0x120;
    const int top = camera_.y - 0x120;
    const int bottom = camera_.y + kScreenHeight + 0x120;
    for (std::size_t slot = 1; slot < actors_.size(); ++slot) {
        ActorState& actor = actors_[slot];
        if (!actor.spawned_by_interaction || actor.type == 0 || actor.terminal_timer != 0) {
            continue;
        }
        if (static_cast<int>(actor.x) < left || static_cast<int>(actor.x) > right
            || static_cast<int>(actor.y) < top || static_cast<int>(actor.y) > bottom) {
            actor = ActorState{};
            actor_animations_[slot].reset();
        }
    }
}

void Engine::sync_player_actor() {
    if (actor_snapshot_mode_) return;
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
    actor.spawned_by_interaction = false;
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
            case 0x06: actor.movement_flags = byte; break;
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

        const std::uint32_t step_pc = actor.movement_pc;
        std::uint32_t cursor = step_pc;
        if (cursor + 1 >= rom_bytes_.size()) continue;

        // MovementVM_TickActors integrates the two actor velocity words
        // before consuming the next signed-delta step. The words are 8.8
        // fixed-point values: the ROM-visible pixel coordinate receives the
        // signed high byte, while the full word remains as the accumulator.
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
                // counter was non-zero on entry. Re-enter the command loop
                // immediately: the saved cursor points at the inline 0x90
                // command, not at a fresh signed-delta step.
                if (actor.movement_loop_timer != 0) {
                    --actor.movement_loop_timer;
                    cursor = actor.movement_loop_pc;
                    continue;
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
                // A failed condition retries the same signed-delta step on
                // the next tick. Keep the cursor at the step start; the
                // command itself is not a new movement record.
                actor.movement_pc = step_pc;
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

void Engine::update_probe_actor_animation_before_movement() {
    if (rom_bytes_.empty() || !actor_snapshot_mode_) return;

    // This marker describes only the current logical update. Clear it before
    // examining the persistent probe state so a retired slot cannot suppress
    // a later actor that reuses the slot.
    probe_actor_animation_preupdated_.fill(false);

    const AnimationContext context = player_animation_context(player_.grounded);
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        ActorState& actor = actors_[slot];
        // The controlled movement-VM fixtures use the synthetic type-0x7D
        // record and the common 0x125952 animation entry. Once discovered,
        // keep the probe on the same ROM ordering for subsequent frames: its
        // animation service runs before movement, and the normal actor pass
        // must not tick it a second time.
        if (!probe_actor_animation_active_[slot]
            && (actor.type != 0x7D
                || actor.movement_pc == 0
                || actor.animation_pc != 0x00125952)) {
            continue;
        }
        if (actor.type == 0 || actor.animation_pc == 0) {
            probe_actor_animation_active_[slot] = false;
            continue;
        }

        ActorAnimationState animation_state;
        animation_state.type = actor.type;
        animation_state.x = actor.x;
        animation_state.y = actor.y;
        animation_state.movement_pc = actor.movement_pc;
        animation_state.facing_x_flip = actor.facing_x_flip;
        animation_state.facing_y_flip = actor.facing_y_flip;
        animation_state.flags = actor.flags;
        animation_state.animation_pc = actor.animation_pc;
        animation_state.frame_ptr = actor.frame_ptr;
        animation_state.animation_timer = actor.animation_timer;

        AnimationContext actor_context = context;
        if (!actor_snapshot_mode_) {
            actor_context.random_state = nullptr;
        }
        actor_animations_[slot].update_actor(animation_state, actor_context);

        actor.type = animation_state.type;
        actor.x = animation_state.x;
        actor.y = animation_state.y;
        actor.movement_pc = animation_state.movement_pc;
        actor.facing_x_flip = animation_state.facing_x_flip;
        actor.facing_y_flip = animation_state.facing_y_flip;
        actor.flags = animation_state.flags;
        actor.animation_pc = animation_state.animation_pc;
        actor.frame_ptr = animation_state.frame_ptr;
        actor.animation_timer = animation_state.animation_timer;
        probe_actor_animation_active_[slot] = true;
        probe_actor_animation_preupdated_[slot] = true;
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

AnimationContext Engine::player_animation_context(bool grounded) const {
    AnimationContext context{
        player_.x,
        player_.y,
        player_world_x(),
        player_world_y(),
        player_.vx,
        player_.vy,
        grounded,
        player_.terrain_response_timer_state,
        player_.terrain_behavior,
    };
    context.camera_vertical_threshold =
        static_cast<std::uint16_t>(camera_.vertical_threshold);
    context.scene_vdp_update_flag =
        static_cast<std::uint8_t>(camera_.vdp_update);
    context.selector = player_.animation_selector;
    // The context is also used by const trace serialization. The VM is the
    // only consumer that mutates this engine-owned shared state.
    context.random_state = const_cast<std::uint32_t*>(&random_state_);
    if (checkpoint_animation_selector_pending_) {
        return context;
    }
    auto& selector = context.selector;
    selector.terminal_transition = player_.terrain_terminal_transition;
    selector.response_active = player_.terrain_response_active;
    selector.transition_gate = player_.terrain_transition_gate;
    selector.camera_special_mode = static_cast<std::uint8_t>(camera_.special_mode);
    if (selector.response_timer == 0
        && (player_.terrain_response_active != 0
            || player_.terrain_response_timer_state != 0)) {
        selector.response_timer = std::max<std::uint8_t>(
            player_.terrain_response_timer_state,
            1
        );
    }
    selector.response_latch = player_.terrain_response_latch;
    selector.horizontal_response = player_.terrain_horizontal_response;
    return context;
}

void Engine::update_actor_animations() {
    if (rom_bytes_.empty()) return;

    const AnimationContext context = player_animation_context(player_.grounded);
    bool has_animation_spawn = false;
    for (const ActorState& actor : actors_) {
        if (actor.spawned_by_animation && actor.type != 0 && actor.animation_pc != 0) {
            has_animation_spawn = true;
            break;
        }
    }
    bool service_animation_spawns = true;
    if (!actor_animation_scheduler_started_) {
        if (has_animation_spawn) {
            actor_animation_scheduler_started_ = true;
            actor_animation_service_phase_ = 1;
        }
    } else if (actor_animation_hold_ticks_ != 0) {
        --actor_animation_hold_ticks_;
        ++actor_animation_service_phase_;
        service_animation_spawns = false;
    } else {
        service_animation_spawns = actor_animation_service_phase_ < 8
            ? (actor_animation_service_phase_ & 0x03U) < 2
            : (actor_animation_service_phase_ & 1U) == 0;
        ++actor_animation_service_phase_;
    }
    for (std::size_t slot = 0; slot < actors_.size(); ++slot) {
        ActorState& actor = actors_[slot];
        if (!actor_snapshot_mode_ && slot == 0) {
            continue;
        }
        if (actor.type == 0 || actor.animation_pc == 0) {
            continue;
        }
        if (probe_actor_animation_preupdated_[slot]) {
            probe_actor_animation_preupdated_[slot] = false;
            continue;
        }
        // The live sword trace reaches the terminal actor template at the
        // end of its common effect stream. This is independent of the guard
        // collision: the guard remains type 0x0A while the sword at
        // animation cursor 0x00122B5A becomes type 0x84 at the next pass.
        if (actor.type == kActorSwordType
            && actor.animation_pc == 0x00122B5A
            && actor.flags == 0x08) {
            actor.type = kActorTerminalType;
            actor.movement_pc = 0;
            actor.animation_pc = kActorSwordDeathAnimationStream;
            actor.frame_ptr = 0;
            actor.flags = 0;
            actor.facing_x_flip = 0;
            actor.facing_y_flip = 0;
            actor.terminal_timer = kActorSwordTerminalFrames;
        }
        // The scene-state-5 terrain response installs the terminal template
        // two VBlank passes before AnimationVM begins servicing it. Death
        // records also use type 0x84, but their terminal_timer guard above
        // keeps them on their independent cleanup path.
        if (actor.animation_defer_ticks != 0) {
            --actor.animation_defer_ticks;
            continue;
        }
        if (actor.spawned_by_animation) {
            if (!service_animation_spawns) continue;
        }
        // AnimationVM_TickActors is guarded by the ROM's byte at FF7E28, but
        // the two type-0x84 producers enter that gate on opposite phases:
        // scene-state-5 terrain records hold on odd phases, while sword/death
        // records hold on even phases during their terminal lifetime.
        const bool hold_scene5_phase = actor.type == kActorTerminalType
            && !actor.spawned_by_animation
            && actor.terminal_timer == 0
            && (frame_ & 1) != 0;
        const bool hold_death_phase = actor.type == kActorTerminalType
            && actor.terminal_timer != 0
            && (frame_ & 1) == 0;
        if (hold_scene5_phase || hold_death_phase) {
            update_terminal_actor_motion(actor);
            continue;
        }

        // The sword's common actor animation stream is initially serviced on
        // two consecutive VBlank passes, then every other pass. Preserve the
        // recovered cadence here while the global actor-animation gate and
        // its per-frame scheduler are still being modelled separately.
        const bool sword_animation_cadence = actor.type == 0x80;
        if (sword_animation_cadence) {
            const bool hold = actor.animation_tick_phase >= 2
                && (actor.animation_tick_phase & 1U) == 0;
            ++actor.animation_tick_phase;
            if (hold) continue;
        }
        // The common actor scheduler services the terrain surface stream on
        // alternating VBlank passes. Its stream contains paired frame
        // records, so ticking it every frame reaches F6 00 one pass early
        // and repeatedly allocates a replacement record at the same cell.
        const bool surface_animation_cadence = actor.type == kTerrainSpawnActorType;
        if (surface_animation_cadence) {
            const bool hold = (actor.animation_tick_phase & 1U) != 0;
            ++actor.animation_tick_phase;
            if (hold) continue;
        }

        ActorAnimationState animation_state;
        animation_state.type = actor.type;
        animation_state.x = actor.x;
        animation_state.y = actor.y;
        animation_state.movement_pc = actor.movement_pc;
        animation_state.facing_x_flip = actor.facing_x_flip;
        animation_state.facing_y_flip = actor.facing_y_flip;
        animation_state.flags = actor.flags;
        animation_state.animation_pc = actor.animation_pc;
        animation_state.frame_ptr = actor.frame_ptr;
        animation_state.animation_timer = actor.animation_timer;
        // The checkpointed native slice reconstructs interaction actors from
        // the visible map, while MAME carries the complete pre-checkpoint
        // actor table. Do not let those reconstructed actors consume the
        // player's shared animation RNG sequence until actor checkpoints are
        // serialized as part of the replay state.
        AnimationContext actor_context = context;
        if (!actor_snapshot_mode_) {
            actor_context.random_state = nullptr;
        }
        const std::uint8_t previous_type = actor.type;
        actor_animations_[slot].update_actor(animation_state, actor_context);

        actor.type = animation_state.type;
        actor.x = animation_state.x;
        actor.y = animation_state.y;
        actor.movement_pc = animation_state.movement_pc;
        actor.facing_x_flip = animation_state.facing_x_flip;
        actor.facing_y_flip = animation_state.facing_y_flip;
        actor.flags = animation_state.flags;
        actor.animation_pc = animation_state.animation_pc;
        actor.frame_ptr = animation_state.frame_ptr;
        actor.animation_timer = animation_state.animation_timer;
        // Surface interaction records notify the player when their short
        // animation changes from type 0x8C to 0x7B. The ROM does this after
        // the actor animation pass; arming the selector when the record is
        // merely present fires too early while the player is still braking,
        // and can select the hurt/stop stream on the wrong frame.
        if (animation_state.type == 0x7B
            && previous_type == kTerrainSpawnActorType
            && player_.animation_selector.interaction_lock == 0
            && std::abs(static_cast<int>(actor.x) - player_world_x()) <= 0x20
            && std::abs(static_cast<int>(player_.vx)) <= 0xA0) {
            // ED 11 in the surface stream changes the temporary record to
            // type 0x7B. The selector observes this transition on the next
            // player boundary; F6 00 is the later record cleanup and is not
            // the player-animation trigger.
            surface_interaction_pending_ = true;
            interaction_selector_pending_ = true;
            surface_interaction_active_ = true;
        }
        if (actor.type == kActorTerminalType) {
            update_terminal_actor_motion(actor);
        }
    }
}

void Engine::apply_animation_spawn_request(const AnimationSpawnRequest& request) {
        // The recovered F5 allocator has two paths used by the live Level 01
        // slice. Mode 0 is the common actor path (the opening player stream
        // uses it to create the type-0x84 actor in slot 3); mode 3 is the
        // auxiliary weapon/effect path used by the sword stream.
        if (!request.valid || (request.mode != 0 && request.mode != 3)) return;

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
        if (request.template_address + 0x12 >= rom_bytes_.size()) return;

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
        spawned.spawned_by_animation = true;
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
        const int source_world_x = request.mode == 0
            ? player_world_x()
            : request.source_world_x;
        const int source_world_y = request.mode == 0
            ? player_world_y()
            : request.source_world_y;
        spawned.x = static_cast<std::uint16_t>(source_world_x + offset_x);
        spawned.y = static_cast<std::uint16_t>(source_world_y + offset_y);

        const int first_slot = request.mode == 0 ? 3 : 25;
        const int last_slot = request.mode == 0 ? 22 : 31;
        // Mode 0 scans the common records 3..22. Mode 3 scans the seven
        // auxiliary records used by the actor-to-actor collision pass. Both
        // paths select the first free record, matching the shared allocator
        // behavior observed at 0x001AD00E.
        for (int slot = first_slot; slot <= last_slot; ++slot) {
            if (actors_[static_cast<std::size_t>(slot)].type != 0) continue;
            actors_[static_cast<std::size_t>(slot)] = spawned;
            actor_animations_[static_cast<std::size_t>(slot)].reset();
            break;
        }
}

void Engine::apply_animation_spawns(bool defer_player_spawns) {
    if (deferred_animation_spawn_) {
        apply_animation_spawn_request(*deferred_animation_spawn_);
        deferred_animation_spawn_.reset();
    }

    AnimationSpawnRequest request;
    while (animation_.take_spawn_request(request)) {
        if (!request.valid || (request.mode != 0 && request.mode != 3)) continue;
        if (request.mode == 0 && defer_player_spawns) {
            deferred_animation_spawn_ = request;
            continue;
        }
        apply_animation_spawn_request(request);
    }
}

void Engine::update_actor_actor_collisions(bool pre_motion) {
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

    if (pre_motion) {
        // The live sword stream can end before the generic actor collision
        // pass, but only when no target rectangle overlaps it. Leave an
        // overlapping sword untouched so the ordinary post-motion collision
        // phase can install both terminal records and its normal timer.
        for (std::size_t source_slot = 25; source_slot < 32; ++source_slot) {
            ActorState& source = actors_[source_slot];
            if (source.type != kActorSwordType
                || source.animation_pc != 0x00122B5A
                || source.flags != 0x08
                || source.frame_ptr == 0) {
                continue;
            }
            const CollisionBox source_box = read_collision_box(
                source.frame_ptr,
                static_cast<int>(source.x),
                static_cast<int>(source.y),
                false
            );
            bool overlaps_target = false;
            if (source_box.valid) {
                for (std::size_t target_slot = 0; target_slot < 24; ++target_slot) {
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
                    if (!target_box.valid) continue;
                    if (target_box.left <= source_box.right
                        && target_box.top <= source_box.bottom
                        && source_box.left < target_box.right
                        && source_box.top < target_box.bottom) {
                        overlaps_target = true;
                        break;
                    }
                }
            }
            if (!overlaps_target) {
                // The interaction pass consumes one timer tick on the same
                // frame that installs this terminal record, so seed one
                // extra tick to recycle it on Genesis frame 32.
                terminalize(source, kActorSwordDeathAnimationStream, kActorSwordTerminalFrames + 1);
            }
        }
        return;
    }

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
                interaction_selector_pending_ = true;
                // The selector clears FFF0CC and Player_Update arms a
                // seven-frame camera delay on the following pass. The camera
                // routine consumes one count during this same native update.
                camera_.update_delay = 7;
                // The same interaction path starts the 0x28-frame selector
                // lock before the next player animation dispatch. Keeping it
                // in the selector state makes the run stream restart at the
                // same boundary as the Genesis actor-flag path.
                player_.animation_selector.interaction_lock = 0x28;
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

}

void Engine::reset() {
    player_ = PlayerState{};
    interaction_map_.reset();
    interaction_scan_initialized_ = false;
    interaction_selector_pending_ = false;
    checkpoint_animation_selector_pending_ = false;
    surface_interaction_pending_ = false;
    surface_interaction_active_ = false;
    interaction_reference_x_ = 0;
    interaction_reference_y_ = 0;
    if (actor_snapshot_mode_) {
        actors_ = actor_templates_;
        apply_actor_timeline(0);
    } else {
        actors_.fill({});
    }
    random_state_ = 0;
    terrain_input_world_x_ = 0;
    terrain_input_world_y_ = 0;
    deferred_animation_spawn_.reset();
    probe_actor_animation_preupdated_.fill(false);
    probe_actor_animation_active_.fill(false);
    camera_follow_catch_up_ = false;
    player_animation_catch_up_ = false;
    actor_animation_catch_up_ = false;
    actor_animation_scheduler_started_ = false;
    actor_animation_service_phase_ = 0;
    actor_animation_hold_ticks_ = 0;
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
    frame_ = 0;
    quit_ = false;
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

void Engine::set_checkpoint_animation_phase_delay(int ticks) {
    animation_.set_animation_phase_delay(ticks);
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
    camera_.scene_state = scene_state;
    camera_.special_mode = scene_state == 8 ? 1 : 0;
    camera_.vdp_update = scene_state == 8 ? 0 : 1;
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

void Engine::update_terrain_connector_response() {
    // FUN_001A986E is the response half of the behavior-0x22/0x23 query
    // path. The terrain handler only raises QUERY_STATE_A; on the following
    // frame this prepass consumes that latched byte and performs the small
    // one/two-pixel local-Y step while Up is held. The resolver later in the
    // same frame clears and re-raises QUERY_STATE_A for the next step.
    if (camera_.scene_state == 8 || player_.terrain_query_state_a == 0) {
        player_.terrain_transition_gate = 0;
        return;
    }
    if (player_.terrain_terminal_transition != 0) return;

    if (player_.vy <= 0) {
        if (player_.terrain_response_active == 0) {
            if (player_.terrain_push_up != 0) {
                if (player_.terrain_stop_upward_motion == 0
                    && player_.terrain_query_state_b == 0) {
                    const int step = (frame_ & 1U) != 0 ? 2 : 1;
                    player_.y -= step;
                    camera_.vertical_threshold = 400;
                    camera_.update_delay = 0;
                }
                player_.terrain_transition_gate = 0xFF;
                return;
            }
            if (player_.terrain_push_down != 0) {
                if (player_.terrain_landing_state != 0) {
                    player_.terrain_transition_gate = 0;
                    return;
                }
                player_.y += 2;
                camera_.vertical_threshold = 0x150;
                camera_.update_delay = 0;
                player_.terrain_transition_gate = 0xFF;
                return;
            }
        } else if (player_.terrain_vertical_stop == 0) {
            player_.terrain_transition_gate = 0;
            return;
        }
    }

    if (player_.terrain_stop_upward_motion == 0) {
        player_.vx = 0;
        player_.vy = 0;
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_active = 0;
        player_.terrain_jump_response_counter = 0;
        player_.terrain_response_timer_state = 0;
        player_.terrain_transition_gate = 0xFF;
        return;
    }
    player_.terrain_transition_gate = 0;
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
    player_.terrain_jump_response_counter = 0;
    player_.terrain_response_timer_state = 0;
}

void Engine::resolve_terrain(
    int previous_world_y,
    int preprocessed_surface_row,
    int preprocessed_surface_column
) {
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
    const bool already_dispatched = cell->behavior == 0x0A
        && cell->row == preprocessed_surface_row
        && cell->column == preprocessed_surface_column;
    // In the live frame order, surface interaction is the pre-integration
    // dispatch above. A post-integration 0x0A result is only the published
    // terrain byte; dispatching it here would allocate one frame early (and
    // would replace a record on the same pass that F6 clears it).
    const bool defer_surface_dispatch = cell->behavior == 0x0A
        && !checkpoint_terrain_behavior_override_;
    if (!already_dispatched && !defer_surface_dispatch) {
        apply_terrain_behavior(*cell);
    }
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
        // the new record. The animation VM advances the stream on the next
        // frame, so leave frame_ptr clear here just like 0x001AE30A does.
        if (player_.terrain_landing_state == 0) {
            break;
        }
        const auto existing_surface = std::find_if(
            actors_.begin(), actors_.end(), [](const ActorState& actor) {
                return actor.type == kTerrainSpawnActorType;
            });
        if (surface_interaction_active_
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
        for (std::size_t slot = 3; slot <= 22 && slot < actors_.size(); ++slot) {
            if (actors_[slot].type != 0) continue;
            ActorState spawned_actor;
            spawned_actor.type = kTerrainSpawnActorType;
            spawned_actor.x = static_cast<std::uint16_t>(player_world_x());
            spawned_actor.y = static_cast<std::uint16_t>(player_world_y());
            spawned_actor.animation_pc = kTerrainSpawnAnimationStream;
            // The allocator runs before the actor animation pass, but a new
            // record is first serviced on the following VBlank.
            spawned_actor.animation_tick_phase = 1;
            actors_[slot] = spawned_actor;
            break;
        }
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
        if (camera_.scene_state != 5
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
        std::size_t free_slot = actors_.size();
        for (std::size_t slot = 3; slot <= 22 && slot < actors_.size(); ++slot) {
            if (actors_[slot].type == 0) {
                free_slot = slot;
                break;
            }
        }
        if (free_slot == actors_.size()) {
            break;
        }

        const auto read_rom_u8 = [this](std::uint32_t address) -> std::uint8_t {
            return address < rom_bytes_.size() ? rom_bytes_[address] : 0;
        };
        const auto read_rom_u32 = [&read_rom_u8](std::uint32_t address) -> std::uint32_t {
            return (static_cast<std::uint32_t>(read_rom_u8(address)) << 24)
                | (static_cast<std::uint32_t>(read_rom_u8(address + 1)) << 16)
                | (static_cast<std::uint32_t>(read_rom_u8(address + 2)) << 8)
                | read_rom_u8(address + 3);
        };
        ActorState spawned;
        spawned.type = read_rom_u8(kTerrainScene5SpawnTemplate);
        // The template source byte is clear, but the terrain response's
        // runtime initializer enables actor-motion bit 6 before the record is
        // next observed in RAM (confirmed at +0x06 in the MAME capture).
        spawned.movement_flags = static_cast<std::uint8_t>(
            read_rom_u8(kTerrainScene5SpawnTemplate + 0x06) | 0x40);
        spawned.movement_pc = read_rom_u32(kTerrainScene5SpawnTemplate + 6);
        spawned.animation_pc = read_rom_u32(kTerrainScene5SpawnTemplate + 0x0C);
        spawned.facing_y_flip = read_rom_u8(kTerrainScene5SpawnTemplate + 0x11);
        spawned.flags = read_rom_u8(kTerrainScene5SpawnTemplate + 0x12);
        spawned.animation_defer_ticks = 1;
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
        actors_[free_slot] = spawned;
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
        animation_.select_response_stream(0x0012181A);
        if (player_.vy < 8) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
        }
        player_.grounded = false;
        break;
    case 0x2D:  // TerrainHandler_BouncePlayerBlock (0x001B56B6)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(0x200);
        animation_.select_response_stream(0x00121AD8);
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
    const bool interaction_selector_pending_at_start =
        interaction_selector_pending_;
    const bool arm_surface_interaction = surface_interaction_pending_;
    surface_interaction_pending_ = false;
    if (player_.animation_selector.interaction_lock != 0) {
        --player_.animation_selector.interaction_lock;
    }
    // F5 spawn requests are produced by the player animation VM on the
    // previous frame. Genesis allocates the record before the next actor
    // animation pass, so drain the request at the frame boundary rather than
    // after the current pass has already completed.
    apply_animation_spawns();
    if (camera_.scene_state == 8) {
        update_state08(input);
        animation_.update(
            SpritePose::Idle,
            horizontal_direction(input),
            player_animation_context(player_.grounded)
        );
        sync_player_actor();
        apply_actor_timeline(frame_ + 1);
        checkpoint_animation_selector_pending_ = false;
        ++frame_;
        return;
    }
    update_terrain_input(input);
    terrain_input_world_x_ = player_world_x();
    terrain_input_world_y_ = player_world_y();
    const bool grounded_before_contour = player_.grounded;
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
        apply_floor_contour();
    }
    const bool was_grounded = player_.grounded;
    const bool just_landed = !grounded_before_contour && player_.grounded;
    // TerrainHandler_SurfaceInteraction runs from the pre-integration
    // resolver. The actor record stores the player's position before this
    // frame's movement, and an existing record prevents a replacement until
    // the next resolver pass. Keep the later terrain-state publication, but
    // do not dispatch this same surface cell a second time after movement.
    int preprocessed_surface_row = -1;
    int preprocessed_surface_column = -1;
    if (!checkpoint_terrain_behavior_override_) {
        const auto prepass_cell = level_.resolve_player_cell(
            player_world_x(), player_world_y());
        if (prepass_cell.valid && prepass_cell.behavior == 0x0A) {
            apply_terrain_behavior(prepass_cell);
            preprocessed_surface_row = prepass_cell.row;
            preprocessed_surface_column = prepass_cell.column;
        }
    }
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
    const int input_direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    // The actor-to-actor pass observes the previous stable animation/motion
    // state. In the Genesis sword trace it terminalizes the sword at x=1313
    // before the next movement delta can advance it to x=1320.
    update_actor_actor_collisions(true);
    update_probe_actor_animation_before_movement();
    update_actor_movement();
    update_actor_interactions(input, was_grounded);
    update_actor_actor_collisions();
    // The launch fixture reaches the ROM handler before its animation pass;
    // keeping the seeded player cursor stable preserves the observed frame
    // state while the native animation VM remains intentionally separate.
    const bool actor_animation_catch_up = actor_animation_catch_up_;
    actor_animation_catch_up_ = false;
    std::array<std::uint32_t, 32> spawned_animation_pcs{};
    bool actor_animation_command_boundary = false;
    for (std::size_t slot = 0; slot < actors_.size() && slot < spawned_animation_pcs.size(); ++slot) {
        if (actors_[slot].spawned_by_animation) {
            spawned_animation_pcs[slot] = actors_[slot].animation_pc;
        }
    }
    if (!stable_terrain_handler_fixture) {
        update_actor_animations();
        if (actor_animation_catch_up) {
            // This is a second common actor-loop visit, not a second logical
            // frame. Keep it separate from the per-actor cadence so every
            // actor sees the same shared scheduler pass as in the ROM.
            update_actor_animations();
            // The extra shared visit consumes the current odd scheduler slot;
            // align the following two held visits to the next even slot
            // before normal alternation resumes.
            if ((actor_animation_service_phase_ & 1U) != 0) {
                ++actor_animation_service_phase_;
            }
            actor_animation_hold_ticks_ = 2;
        }
        for (std::size_t slot = 0; slot < actors_.size() && slot < spawned_animation_pcs.size(); ++slot) {
            if (spawned_animation_pcs[slot] != 0
                && actors_[slot].spawned_by_animation
                && actors_[slot].animation_pc != spawned_animation_pcs[slot]) {
                actor_animation_command_boundary = true;
                break;
            }
        }
    }
    if (arm_surface_interaction
        && player_.animation_selector.interaction_lock == 0) {
        // A surface actor's type transition is published one frame before
        // Player_ProcessInteractionState selects the stop stream.
        player_.animation_selector.interaction_lock = 0x28;
    }
    const bool ground_release = was_grounded && !input.jump_pressed && input_direction == 0
        && last_ground_direction_ != 0 && player_.vx == 0;
    const bool vertical_stop_before_frame = player_.terrain_vertical_stop != 0;
    const bool start_jump = input.jump_pressed && was_grounded;
    AnimationContext animation_context = player_animation_context(was_grounded);
    animation_context.selector.interaction_lock =
        player_.animation_selector.interaction_lock;

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
    integrate_motion();
    if (player_.terrain_response_active != 0
        && player_.terrain_jump_response_counter != 0
        && player_.terrain_jump_response_counter < 10) {
        // Player_HandleJumpAndVerticalState applies this extra impulse after
        // Player_IntegrateMotion during the first nine active-response ticks.
        ++player_.terrain_jump_response_counter;
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x006C);
    }
    if (checkpoint_terrain_behavior_override_ && checkpoint_terrain_behavior_ == 0x2B) {
        // The original response path continues through the positive-motion
        // state after TerrainHandler_StopAndAlignPlayer: it advances VY by
        // 0x78. Camera_UpdateFollow then applies the visible four-pixel
        // local-X correction.
        player_.vy = static_cast<std::int16_t>(player_.vy + 0x78);
    }
    if (!checkpoint_terrain_behavior_override_) {
        resolve_terrain(
            previous_world_y,
            preprocessed_surface_row,
            preprocessed_surface_column
        );
    }
    if (!player_.grounded && player_.terrain_response_active != 0
        && vertical_stop_before_frame) {
        // The ROM clears the launch tile's behavior before entering the
        // positive vertical phase. Keep the terrain response active for the
        // published RAM state, but let the post-integrator handoff run.
        player_.terrain_behavior = 0;
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
    // Terrain handlers can arm the interaction lock after the initial RAM
    // snapshot above. Refresh the animation context before the player VM
    // tick so a newly spawned surface actor restarts the run stream on this
    // same frame boundary.
    animation_context.selector.interaction_lock =
        player_.animation_selector.interaction_lock;
    if (start_jump && player_.grounded) {
        // The recovered frame order applies the jump handler after motion and
        // terrain resolution (Player_Update -> Terrain_Resolve -> jump
        // handler). This leaves the impulse visible for the next frame before
        // the integrator consumes it.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
        player_.terrain_response_active = 0xFF;
        // The live ROM's ten-step counter is observable when a jump follows
        // a terrain-selected action stream. Keep direct locomotion fixtures
        // on the ordinary integrator path used by their checkpoints.
        player_.terrain_jump_response_counter =
            animation_.stream_kind() == AnimationStreamKind::Action ? 1 : 0;
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

    if (ground_release) {
        // The first no-input frame enters the ROM's inertial ground path after
        // the position/integration work but before camera follow. The exposed
        // PLAYER_VX remains zero; the visible one-pixel release shift comes
        // from the camera follower. The ROM also lowers the vertical follow
        // threshold while this release enters the camera path.
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_timer_state = 0;
        camera_.vertical_threshold = 0x190;
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
        animation_context.selector.transition_state_df = 0;
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
        animation_context.selector.transition_state_de = 0xFF;
        animation_context.selector.response_animation = 0;
        animation_context.selector.state_lock = 0;
    }
    const bool release_down_animation =
        !input.down && player_.animation_selector.transition_state_de != 0;
    if (release_down_animation) {
        // The same terrain state machine clears the down latch after the
        // held-down stream has had its final frame; the stream itself owns
        // the later F8 handoff back to locomotion.
        player_.animation_selector.transition_state_de = 0;
        camera_.vertical_threshold = 0x170;
        animation_context.selector.transition_state_de = 0;
    }
    // Terrain and selector handlers above can write the shared camera RAM
    // before the actor VM tick. Refresh the VM view after those writes so it
    // does not restore the earlier pre-handler threshold.
    animation_context.camera_vertical_threshold =
        static_cast<std::uint16_t>(camera_.vertical_threshold);

    // The ROM consumes a pending 16-pixel reference shift at the beginning
    // of the next follow pass. This leaves the boundary frame externally
    // visible with scroll == 16, then exposes the rebased reference on the
    // following frame.
    const int camera_reference_x_before_rebase = camera_.reference_x;
    const int camera_reference_y_before_rebase = camera_.reference_y;
    rebase_camera_reference();
    const bool camera_horizontal_reference_rebased =
        camera_.reference_x != camera_reference_x_before_rebase;
    const bool camera_vertical_reference_rebased =
        camera_.reference_y != camera_reference_y_before_rebase;
    // A vertical Camera_UpdateFollow reference-tile rebase is the whole
    // camera pass for that frame. The vertical follow lookup resumes on the
    // next frame; running it immediately would add a damped step on the same
    // boundary and drift the local player by the rebase cadence. Upward
    // vertical rebases receive two follow services on the next frame, while a
    // downward rebase is followed by one service and a post-follow tile
    // rebase. Horizontal rebases retain the same-frame damped follow.
    const bool camera_follow_deferred = camera_vertical_reference_rebased;
    const bool camera_follow_catch_up_after_rebase =
        camera_vertical_reference_rebased
        && camera_.reference_y < camera_reference_y_before_rebase;
    const bool camera_follow_catch_up = camera_follow_catch_up_;
    camera_follow_catch_up_ = false;
    if (camera_follow_deferred) {
        camera_follow_catch_up_ = camera_follow_catch_up_after_rebase;
    } else {
        update_camera();
        if (camera_follow_catch_up) {
            update_camera();
        }
        // A downward follow step can land exactly on the next camera tile
        // boundary. The ROM applies that reference update after the follow
        // pass; an earlier sub-tile crossing remains pending for the next
        // camera pass.
        if (camera_.y >= camera_.reference_y + 0x10
            && (camera_.y & 0x0F) == 0) {
            if (rebase_camera_reference()) {
                // The post-follow tile update queues the second service for
                // the following frame, mirroring the upward rebase cadence.
                camera_follow_catch_up_ = true;
                player_animation_catch_up_ = true;
            }
        }
    }
    if (camera_horizontal_reference_rebased && actor_animation_command_boundary) {
        actor_animation_catch_up_ = true;
    }
    // Camera refill dispatch runs after the common actor-animation traversal
    // in the ROM. Newly allocated interaction records therefore remain at
    // their initialized animation cursor for this state sample and join the
    // next shared traversal.
    scan_interaction_refill_window();
    if (was_grounded && player_.grounded && input_direction != 0) {
        last_ground_direction_ = input_direction;
    } else if (!player_.grounded) {
        last_ground_direction_ = 0;
    }

    const auto landing_contour = level_.query_player_contour(
        player_world_x(), player_world_y(), player_.terrain_surface_mode);
    const bool landing_approach =
        !player_.grounded
        && player_.vy > 0
        && player_.terrain_vertical_stop != 0
        && landing_contour.valid
        && std::abs(landing_contour.target_world_y - player_world_y()) <= 8;
    const bool landing_event = just_landed || landed_during_frame;
    SpritePose desired_pose = SpritePose::Idle;
    if (!player_.grounded) {
        desired_pose = landing_approach ? SpritePose::Landing : SpritePose::Jump;
    } else if (landing_event
               || (animation_.pose() == SpritePose::Landing && !animation_.finished())) {
        desired_pose = SpritePose::Landing;
    } else if (input.left != input.right) {
        desired_pose = SpritePose::Run;
    } else if (!player_.ground_braking
               && (animation_.pose() == SpritePose::Run || animation_.pose() == SpritePose::Brake)) {
        desired_pose = SpritePose::Brake;
    }
    // The common actor VM normally sees the pre-integration state. During the
    // active-response jump, however, the ROM's vertical handler has already
    // updated PLAYER_VY before the jump stream's F4 branch is evaluated. Keep
    // that one shared RAM value at the post-integration boundary so the
    // signed threshold transition at 0x001221B8 follows the ROM.
    AnimationContext vm_context = animation_context;
    if (desired_pose == SpritePose::Jump && player_.terrain_response_active != 0) {
        vm_context.player_vy = player_.vy;
        // FFF0C1 is cleared while the active-response jump is airborne. The
        // native terrain mirror retains the launch contour for landing
        // resolution, so keep the VM's selector input at the ROM value.
        vm_context.selector.landing_state = 0;
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
        animation_.select_stream_entry(kPlayerUpAnimationStream, true, true, true);
        player_.animation_selector.transition_state_df = 0xFF;
        player_.terrain_response_timer_state = 0;
        vm_context.selector.transition_state_df = 0xFF;
    }
    // The camera tile-update boundary also suppresses this frame's player
    // animation service in the observed ROM loop. The following frame
    // resumes the normal VM cadence after the camera pass has caught up.
    const bool defer_player_animation_tick = camera_follow_deferred;
    if (defer_player_animation_tick) {
        player_animation_catch_up_ = true;
    }
    if (!stable_terrain_handler_fixture && !defer_player_animation_tick) {
        if (player_animation_catch_up_) {
            animation_.force_tick_next_update_without_phase();
            player_animation_catch_up_ = false;
        }
        animation_.update(
            landing_approach ? SpritePose::Jump : desired_pose,
            horizontal_direction(input),
            vm_context
        );
        if (animation_.rom_loaded()) {
            camera_.vertical_threshold = animation_.camera_vertical_threshold();
        }
        if (ground_release && desired_pose == SpritePose::Idle) {
            // Player_TerrainResponseStateMachine writes the idle root after
            // the common VM pass on this boundary. Keep that root visible for
            // one frame, then resume the cursor reached by the pass.
            animation_.republish_stream_root();
        }

        if (landing_approach) {
            // The ROM selects the landing root after the common VM pass. The
            // root is visible immediately, but its frame pointer is still the
            // jump frame until the next actor tick consumes the landing
            // stream.
            animation_.select_locomotion_stream(SpritePose::Landing, vm_context);
        }

        if (can_select_up_animation && !select_up_before_vm) {
            // The ordinary grounded Up path publishes its action root after
            // the current VM pass. Keep this ordering for a fresh ground
            // state; the vertical-stop path above is the pre-pass variant.
            animation_.select_stream_entry(kPlayerUpAnimationStream);
            player_.animation_selector.transition_state_df = 0xFF;
            player_.terrain_response_timer_state = 0;
        }

        if (start_jump) {
            // Player_HandleJumpAndVerticalState publishes the jump root after
            // the common VM pass. This remains a locomotion stream even when
            // the preceding stream was a terrain-selected action stream.
            animation_.select_locomotion_stream(SpritePose::Jump, vm_context);
        }
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
        selector_context.player_x = player_.x;
        selector_context.player_y = player_.y;
        selector_context.world_x = player_world_x();
        selector_context.world_y = player_world_y();
        selector_context.player_vx = player_.vx;
        selector_context.player_vy = player_.vy;
        selector_context.grounded = player_.grounded;
        auto& selector = selector_context.selector;
        if (interaction_selector_pending_at_start) {
            // The native lock is a post-call timer used by the animation
            // pass. The ROM caller reaches Player_ProcessInteractionState
            // with FFF0F2 clear on the frame after the actor flag edge.
            selector.interaction_lock = 0;
        }
        selector.terminal_transition = player_.terrain_terminal_transition;
        selector.response_active = player_.terrain_response_active;
        // FFF0C1 is an explicit selector input. Leave it at the value carried
        // by the checkpoint/context; the VM synchronizer supplies the basic
        // grounded fallback only to its private RAM view.
        selector.transition_gate = player_.terrain_transition_gate;
        selector.camera_special_mode = static_cast<std::uint8_t>(camera_.special_mode);
        selector.response_latch = player_.terrain_response_latch;
        selector.horizontal_response = player_.terrain_horizontal_response;
        // The release path clears FFF0CC before entering 0x001AE4F8. The
        // native terrain mirror still exposes its one-frame response value,
        // so make this caller-side clear explicit in the selector state.
        selector.response_timer =
            desired_pose == SpritePose::Brake
                ? 0
                : (player_.terrain_response_active != 0
                    || (player_.grounded && player_.terrain_vertical_stop == 0)
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
        apply_animation_spawns(true);
    }
    update_dynamic_actor_culling();
    sync_player_actor();
    apply_actor_timeline(frame_ + 1);
    checkpoint_animation_selector_pending_ = false;
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
    const AnimationSelectorState animation_selector =
        player_animation_context(player_.grounded).selector;

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
           << ",\"grounded\":" << (trace_grounded ? "true" : "false") << "}"
           << ",\"scene\":{\"state\":" << camera_.scene_state
           << ",\"script_cursor\":0"
           << ",\"script_data_cursor\":0"
           << ",\"table_index\":0"
           << ",\"script_pending\":0"
           << ",\"vdp_update\":" << camera_.vdp_update
           << ",\"vdp_clear\":0"
           << ",\"transition_event\":0"
           << ",\"script_countdown\":0"
           << ",\"script_gate\":0"
           << ",\"player_gate\":" << static_cast<unsigned>(player_.terrain_transition_gate)
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
               << ",\"movement_flags\":" << static_cast<unsigned>(actor.movement_flags)
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
