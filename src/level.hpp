#pragma once

#include <SDL.h>

#include <cstdint>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace openaladdin {

// Small value types used by the ROM-facing scene/level metadata.  A ROM
// address is deliberately not represented as a native pointer: it identifies
// the original data or callback and remains meaningful after decoding.
struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int width = 0;
    int height = 0;
};

struct RomAddress {
    std::uint32_t value = 0;

    constexpr bool present() const { return value != 0; }
    constexpr explicit operator bool() const { return present(); }
};

struct LevelDescriptor {
    std::uint8_t scene_id = 0;
    Point player_start{};
    Point camera_start{};
    Size map_size{};
    std::uint8_t music_id = 0;

    RomAddress palette{};
    RomAddress floor{};
    RomAddress chars{};
    RomAddress map{};
    RomAddress blocks{};
    RomAddress parallax{};
    RomAddress enter_function{};
    RomAddress exit_function{};

    // Scene 08 uses the animation field for a compact resource. Preserve the
    // remaining loader metadata until its consumers are fully decoded.
    RomAddress animation{};
    std::uint16_t animation_size = 0;
    std::uint8_t background_swap = 0;
    RomAddress parallax_function{};
    RomAddress unused_0{};
    RomAddress unused_1{};
    std::uint8_t padding = 0;
    std::uint32_t descriptor_rom_offset = 0;
    bool from_rom = false;
};

// Decoded resources are intentionally separate from LevelDescriptor.  The
// descriptor retains ROM identity; this object contains data suitable for
// native collision, interaction, and rendering code.
struct LevelResources {
    int background_width = 0;
    int background_height = 0;
    std::vector<std::uint8_t> background_rgba;
    int parallax_width = 0;
    int parallax_height = 0;
    std::vector<std::uint8_t> parallax_rgba;

    std::vector<std::uint16_t> map;
    std::vector<std::uint8_t> floor;
    std::vector<std::uint8_t> chars;
    std::vector<std::uint8_t> blocks;
    std::vector<std::uint8_t> parallax;
    std::vector<std::uint8_t> animation;
    std::vector<std::uint8_t> contour_table;
    std::vector<SDL_Color> palette;
};

class LevelTable {
public:
    static constexpr std::size_t kRomOffset = 0x2C78;
    static constexpr std::size_t kEntrySize = 66;
    static constexpr std::size_t kCount = 13;

    static LevelDescriptor descriptor(
        const std::vector<std::uint8_t>& rom,
        std::uint8_t scene_id
    );

    static std::vector<LevelDescriptor> descriptors(
        const std::vector<std::uint8_t>& rom
    );
};

class Level {
public:
    struct InteractionRecord {
        int column = -1;
        int row = -1;
        std::uint16_t terrain_word = 0;
        std::uint16_t resource_offset = 0;
        std::uint8_t selector = 0;
        int world_x = 0;
        int world_y = 0;
    };

    struct TerrainCell {
        bool valid = false;
        int column = -1;
        int row = -1;
        std::uint16_t terrain_word = 0;
        std::uint8_t behavior = 0;
        std::uint32_t handler = 0;
    };

    struct TerrainQuery {
        TerrainCell resolver;
    };

    struct TerrainCollisionFlags {
        bool stop_left = false;
        bool stop_right = false;
        bool stop_upward = false;
        bool left_inner = false;
        bool left_outer = false;
        bool right_inner = false;
        bool right_outer = false;
    };

    struct TerrainContour {
        bool valid = false;
        int row = -1;
        int column = -1;
        int target_world_y = 0;
        std::uint8_t floor_type = 0;
        std::uint8_t contour = 0;
    };

    void load(const std::string& asset_root, const std::string& rom_path = {});

    const LevelDescriptor& descriptor() const { return descriptor_; }
    const LevelResources& resources() const { return resources_; }

    int background_width() const { return resources_.background_width; }
    int background_height() const { return resources_.background_height; }
    const std::vector<std::uint8_t>& background_rgba() const {
        return resources_.background_rgba;
    }
    int parallax_width() const { return resources_.parallax_width; }
    int parallax_height() const { return resources_.parallax_height; }
    const std::vector<std::uint8_t>& parallax_rgba() const {
        return resources_.parallax_rgba;
    }
    const std::vector<std::uint16_t>& terrain_words() const { return resources_.map; }
    const std::vector<std::uint8_t>& floor_data() const { return resources_.floor; }
    const std::vector<InteractionRecord>& interaction_records() const {
        return interaction_records_;
    }
    const std::vector<SDL_Color>& palette() const { return resources_.palette; }
    bool is_vdp_transparent(std::uint8_t red, std::uint8_t green, std::uint8_t blue) const;
    int map_width() const { return descriptor_.map_size.width; }
    int map_height() const { return descriptor_.map_size.height; }
    int start_x() const { return descriptor_.player_start.x; }
    int start_y() const { return descriptor_.player_start.y; }
    int camera_start_x() const { return descriptor_.camera_start.x; }
    int camera_start_y() const { return descriptor_.camera_start.y; }
    int camera_threshold_x() const { return descriptor_.player_start.x; }
    int camera_threshold_y() const { return descriptor_.player_start.y; }
    int scene_state() const { return descriptor_.scene_id; }

    std::uint8_t terrain_behavior(int column, int row) const;
    std::uint8_t interaction_selector(int column, int row) const;
    TerrainCell resolve_player_cell(int world_x, int world_y) const;
    TerrainQuery query_player(int world_x, int world_y) const;
    TerrainCollisionFlags query_player_collision(
        int world_x,
        int world_y,
        std::uint8_t landing_state
    ) const;
    TerrainContour query_player_contour(
        int world_x,
        int world_y,
        std::uint16_t surface_mode
    ) const;

private:
    LevelDescriptor descriptor_{};
    LevelResources resources_{};
    std::vector<InteractionRecord> interaction_records_;
};

// The interaction selector bytes are mutable runtime state, even though the
// decoded LevelResources are immutable after loading.
class InteractionMap {
public:
    void load(const Level& level);
    void reset();

    std::uint8_t selector(int column, int row) const;
    std::uint8_t selector(const Level::InteractionRecord& record) const;
    bool consume(std::uint16_t resource_offset);
    const std::vector<Level::InteractionRecord>& records() const { return records_; }
    std::size_t active_record_count() const;
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

private:
    std::vector<Level::InteractionRecord> records_;
    std::vector<std::uint8_t> selectors_;
};

}  // namespace openaladdin
