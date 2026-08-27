#include "level.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace openaladdin {
namespace {

constexpr int kTerrainVisualOffsetY = 0xF0;
constexpr int kTerrainContourRomOffset = 0x2FD2;
constexpr int kTerrainContourRomSize = 0x1000;
constexpr std::uint32_t kTerrainNoOpHandler = 0x001B65BE;

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
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B536C, 0x001B53A2, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B5470
};

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

std::uint8_t read_u8(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset >= data.size()) {
        throw std::runtime_error("truncated ROM level table");
    }
    return data[offset];
}

std::uint16_t read_be_u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("truncated ROM level table");
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1]
    );
}

std::uint32_t read_be_u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("truncated ROM level table");
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24)
        | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
        | data[offset + 3];
}

int level_index_from_asset_root(const std::string& asset_root) {
    const std::string name = std::filesystem::path(asset_root).filename().string();
    if (name.size() != 7 || name.compare(0, 5, "level") != 0
        || !std::isdigit(static_cast<unsigned char>(name[5]))
        || !std::isdigit(static_cast<unsigned char>(name[6]))) {
        return 1;
    }
    const int index = (name[5] - '0') * 10 + (name[6] - '0');
    return index < static_cast<int>(LevelTable::kCount) ? index : 1;
}

struct PpmImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

std::string ppm_token(std::istream& input) {
    while (true) {
        const int ch = input.peek();
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
        const int ch = input.peek();
        if (ch == EOF || std::isspace(static_cast<unsigned char>(ch)) || ch == '#') {
            break;
        }
        token.push_back(static_cast<char>(input.get()));
    }
    return token;
}

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
    file.get();
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    file.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (file.gcount() != static_cast<std::streamsize>(rgb.size())) {
        throw std::runtime_error("truncated PPM payload in " + path);
    }
    PpmImage result{
        width,
        height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)
    };
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

std::uint32_t terrain_handler(std::uint8_t behavior) {
    return behavior < kTerrainHandlers.size() ? kTerrainHandlers[behavior] : kTerrainNoOpHandler;
}

}  // namespace

LevelDescriptor LevelTable::descriptor(
    const std::vector<std::uint8_t>& rom,
    std::uint8_t scene_id
) {
    if (scene_id >= kCount) {
        throw std::runtime_error("scene state is outside the ROM level table");
    }
    const std::size_t offset = kRomOffset + static_cast<std::size_t>(scene_id) * kEntrySize;
    if (offset + kEntrySize > rom.size()) {
        throw std::runtime_error("ROM level table does not contain selected scene state");
    }

    LevelDescriptor result;
    result.scene_id = scene_id;
    result.descriptor_rom_offset = static_cast<std::uint32_t>(offset);
    result.player_start = {
        static_cast<int>(read_be_u16(rom, offset + 0x04)),
        static_cast<int>(read_be_u16(rom, offset + 0x06)),
    };
    result.camera_start = {
        static_cast<int>(read_be_u16(rom, offset + 0x00)),
        static_cast<int>(read_be_u16(rom, offset + 0x02)),
    };
    result.map_size = {
        static_cast<int>(read_be_u16(rom, offset + 0x30)),
        static_cast<int>(read_be_u16(rom, offset + 0x32)),
    };
    result.floor = {read_be_u32(rom, offset + 0x08)};
    result.chars = {read_be_u32(rom, offset + 0x0C)};
    result.map = {read_be_u32(rom, offset + 0x10)};
    result.animation = {read_be_u32(rom, offset + 0x14)};
    result.animation_size = read_be_u16(rom, offset + 0x18);
    result.music_id = static_cast<std::uint8_t>(read_be_u16(rom, offset + 0x1A));
    result.parallax = {read_be_u32(rom, offset + 0x1C)};
    result.palette = {read_be_u32(rom, offset + 0x20)};
    result.blocks = {read_be_u32(rom, offset + 0x24)};
    result.exit_function = {read_be_u32(rom, offset + 0x28)};
    result.enter_function = {read_be_u32(rom, offset + 0x2C)};
    result.parallax_function = {read_be_u32(rom, offset + 0x34)};
    result.unused_0 = {read_be_u32(rom, offset + 0x38)};
    result.unused_1 = {read_be_u32(rom, offset + 0x3C)};
    result.background_swap = read_u8(rom, offset + 0x40);
    result.padding = read_u8(rom, offset + 0x41);
    result.from_rom = true;
    return result;
}

std::vector<LevelDescriptor> LevelTable::descriptors(
    const std::vector<std::uint8_t>& rom
) {
    std::vector<LevelDescriptor> result;
    result.reserve(kCount);
    for (std::uint8_t scene = 0; scene < kCount; ++scene) {
        result.push_back(descriptor(rom, scene));
    }
    return result;
}

void Level::load(const std::string& asset_root, const std::string& rom_path) {
    const auto selected_scene = static_cast<std::uint8_t>(level_index_from_asset_root(asset_root));
    descriptor_ = {};
    descriptor_.scene_id = selected_scene;
    descriptor_.player_start = {103, 416};
    descriptor_.camera_start = {0, 464};
    descriptor_.map_size = {300, 45};

    std::vector<std::uint8_t> rom;
    if (!rom_path.empty()) {
        rom = read_file(rom_path);
        descriptor_ = LevelTable::descriptor(rom, selected_scene);
    }

    resources_ = {};
    const auto background = read_ppm(asset_root + "/background.ppm");
    resources_.background_width = background.width;
    resources_.background_height = background.height;
    resources_.background_rgba = background.rgba;

    if (rom_path.empty()) {
        if (resources_.background_width % 16 != 0 || resources_.background_height % 16 != 0) {
            throw std::runtime_error("level background dimensions are not 16-pixel aligned");
        }
        descriptor_.map_size = {
            resources_.background_width / 16,
            resources_.background_height / 16,
        };
    }

    const std::string parallax_path = asset_root + "/parallax.ppm";
    std::ifstream parallax_file(parallax_path, std::ios::binary);
    if (parallax_file.good()) {
        const auto parallax = read_ppm(parallax_path);
        resources_.parallax_width = parallax.width;
        resources_.parallax_height = parallax.height;
        resources_.parallax_rgba = parallax.rgba;
    }

    resources_.map = read_be_words(read_file(asset_root + "/raw/map.bin"));
    if (resources_.map.size() != static_cast<std::size_t>(map_width() * map_height())) {
        throw std::runtime_error("selected level map dimensions do not match its terrain words");
    }
    resources_.floor = read_file(asset_root + "/raw/floor.bin");

    const auto load_optional = [&asset_root](const std::string& name) {
        const std::string path = asset_root + "/raw/" + name;
        std::ifstream file(path, std::ios::binary);
        if (!file.good()) return std::vector<std::uint8_t>{};
        return read_file(path);
    };
    resources_.chars = load_optional("chars.bin");
    resources_.blocks = load_optional("blocks.bin");
    resources_.parallax = load_optional("parallax.bin");
    resources_.animation = load_optional("animation.bin");

    interaction_records_.clear();
    for (int row = 0; row < map_height(); ++row) {
        for (int column = 0; column < map_width(); ++column) {
            const std::uint16_t terrain_word = resources_.map[
                static_cast<std::size_t>(row * map_width() + column)];
            const std::uint16_t resource_offset = static_cast<std::uint16_t>(terrain_word >> 1);
            const std::size_t selector_index = static_cast<std::size_t>(3 + resource_offset);
            if (selector_index >= resources_.floor.size() || resources_.floor[selector_index] == 0) {
                continue;
            }
            interaction_records_.push_back(InteractionRecord{
                column,
                row,
                terrain_word,
                resource_offset,
                resources_.floor[selector_index],
                column * 16,
                row * 16 + kTerrainVisualOffsetY,
            });
        }
    }

    if (rom.size() >= static_cast<std::size_t>(kTerrainContourRomOffset + kTerrainContourRomSize)) {
        resources_.contour_table.assign(
            rom.begin() + kTerrainContourRomOffset,
            rom.begin() + kTerrainContourRomOffset + kTerrainContourRomSize
        );
    }

    const auto palette_bytes = read_file(asset_root + "/raw/palette.bin");
    if (palette_bytes.size() < 32) {
        throw std::runtime_error("level palette is too short");
    }
    resources_.palette.reserve(palette_bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < palette_bytes.size(); i += 2) {
        const std::uint16_t word = static_cast<std::uint16_t>(palette_bytes[i] << 8 | palette_bytes[i + 1]);
        const auto channel = [](std::uint16_t value, int shift) {
            static constexpr std::uint8_t levels[8] = {0, 52, 87, 116, 144, 172, 206, 255};
            return levels[(value >> shift) & 7];
        };
        resources_.palette.push_back(SDL_Color{channel(word, 1), channel(word, 5), channel(word, 9), 255});
    }
}

std::uint8_t Level::interaction_selector(int column, int row) const {
    if (column < 0 || column >= map_width() || row < 0 || row >= map_height()) return 0;
    const std::uint16_t terrain_word = resources_.map[static_cast<std::size_t>(row * map_width() + column)];
    const std::size_t selector_index = static_cast<std::size_t>(3 + (terrain_word >> 1));
    return selector_index < resources_.floor.size() ? resources_.floor[selector_index] : 0;
}

void InteractionMap::load(const Level& level) {
    records_ = level.interaction_records();
    selectors_.assign(level.floor_data().size(), 0);
    for (const Level::InteractionRecord& record : records_) {
        const std::size_t index = static_cast<std::size_t>(3 + record.resource_offset);
        if (index < selectors_.size()) selectors_[index] = record.selector;
    }
}

void InteractionMap::reset() {
    for (const Level::InteractionRecord& record : records_) {
        const std::size_t index = static_cast<std::size_t>(3 + record.resource_offset);
        if (index < selectors_.size()) selectors_[index] = record.selector;
    }
}

std::uint8_t InteractionMap::selector(int column, int row) const {
    for (const Level::InteractionRecord& record : records_) {
        if (record.column == column && record.row == row) return selector(record);
    }
    return 0;
}

std::uint8_t InteractionMap::selector(const Level::InteractionRecord& record) const {
    const std::size_t index = static_cast<std::size_t>(3 + record.resource_offset);
    return index < selectors_.size() ? selectors_[index] : 0;
}

bool InteractionMap::consume(std::uint16_t resource_offset) {
    const std::size_t index = static_cast<std::size_t>(3 + resource_offset);
    if (index >= selectors_.size() || selectors_[index] == 0) return false;
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
    for (int line = 0; line < 4; ++line) {
        const std::size_t index = static_cast<std::size_t>(line * 16);
        if (index < resources_.palette.size()
            && resources_.palette[index].r == red
            && resources_.palette[index].g == green
            && resources_.palette[index].b == blue) {
            return true;
        }
    }
    return false;
}

std::uint8_t Level::terrain_behavior(int column, int row) const {
    if (column < 0 || column >= map_width() || row < 0 || row >= map_height()) return 0xFF;
    const std::uint16_t terrain_word = resources_.map[static_cast<std::size_t>(row * map_width() + column)];
    const std::size_t behavior_index = 2 + (terrain_word >> 1);
    return behavior_index < resources_.floor.size() ? resources_.floor[behavior_index] : 0xFF;
}

Level::TerrainCell Level::resolve_player_cell(int world_x, int world_y) const {
    TerrainCell cell;
    const int terrain_y = world_y - kTerrainVisualOffsetY;
    if (terrain_y < 0 || terrain_y >= map_height() * 16) return cell;
    const int row = terrain_y >> 4;
    const int column = (world_x + 16) >> 4;
    if (column < 0 || column >= map_width() || row < 0 || row >= map_height()) return cell;
    const std::uint16_t word = resources_.map[static_cast<std::size_t>(row * map_width() + column)];
    const std::size_t behavior_index = 2 + (word >> 1);
    if (behavior_index >= resources_.floor.size()) return cell;
    cell.valid = true;
    cell.column = column;
    cell.row = row;
    cell.terrain_word = word;
    cell.behavior = resources_.floor[behavior_index];
    cell.handler = terrain_handler(cell.behavior);
    return cell;
}

Level::TerrainQuery Level::query_player(int world_x, int world_y) const {
    return TerrainQuery{resolve_player_cell(world_x, world_y)};
}

Level::TerrainCollisionFlags Level::query_player_collision(
    int world_x,
    int world_y,
    std::uint8_t landing_state
) const {
    TerrainCollisionFlags flags;
    const int collision_y = world_y - 0x110;
    const int row = collision_y >> 4;
    const int column = world_x >> 4;
    if (collision_y < 0 || row < 0 || row >= map_height() - 3
        || column < 0 || column + 4 >= map_width()) return flags;

    const auto blocking = [this](int test_column, int test_row) {
        if (test_column < 0 || test_column >= map_width()
            || test_row < 0 || test_row >= map_height()) return false;
        return terrain_behavior(test_column, test_row) >= 0xE0;
    };
    const bool left_passable = !blocking(column, row) && !blocking(column, row + 1);
    if (left_passable) {
        flags.left_inner = blocking(column - 1, row + 1);
        flags.left_outer = blocking(column - 2, row + 1);
        flags.stop_left = blocking(column, row + 2)
            || (landing_state == 0 && blocking(column, row + 3));
    } else {
        flags.stop_left = true;
    }

    const bool right_passable = !blocking(column + 2, row) && !blocking(column + 2, row + 1);
    if (right_passable) {
        flags.right_inner = blocking(column + 3, row + 1);
        flags.right_outer = blocking(column + 4, row + 1);
        flags.stop_right = blocking(column + 2, row + 2)
            || (landing_state == 0 && blocking(column + 2, row + 3));
    } else {
        flags.stop_right = true;
    }
    flags.stop_upward = blocking(column + 1, row);
    return flags;
}

Level::TerrainContour Level::query_player_contour(
    int world_x,
    int world_y,
    std::uint16_t surface_mode
) const {
    TerrainContour result;
    if (resources_.contour_table.size() < static_cast<std::size_t>(kTerrainContourRomSize)) return result;
    const int lookup_y = world_y - 0x100;
    if (lookup_y < 0 || lookup_y >= map_height() * 16 - 0x20) return result;
    const int base_row = (world_y - kTerrainVisualOffsetY) >> 4;
    const int column = (world_x + 16) >> 4;
    if (base_row < 0 || base_row + 2 >= map_height()
        || column < 0 || column >= map_width()) return result;

    const int x_fraction = world_x & 0x0F;
    for (int candidate = 0; candidate < 3; ++candidate) {
        const int row = base_row + candidate;
        const std::uint16_t word = resources_.map[static_cast<std::size_t>(row * map_width() + column)];
        const std::size_t floor_index = static_cast<std::size_t>(word >> 1)
            + static_cast<std::size_t>(surface_mode);
        if (floor_index >= resources_.floor.size()) continue;
        const std::uint8_t floor_type = resources_.floor[floor_index];
        const int fraction = candidate == 2 ? 2 : x_fraction;
        const std::size_t contour_index = static_cast<std::size_t>(floor_type) * 16
            + static_cast<std::size_t>(fraction);
        if (contour_index >= resources_.contour_table.size()) continue;
        const std::uint8_t contour = static_cast<std::uint8_t>(resources_.contour_table[contour_index] & 0x3F);
        if (contour == 0) continue;
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

}  // namespace openaladdin
