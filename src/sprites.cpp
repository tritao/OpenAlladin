#include "sprites.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace openaladdin {
namespace {

struct JsonValue {
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    long long number = 0;
    bool boolean = false;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    const JsonValue& at(std::string_view key) const {
        const auto it = object.find(std::string(key));
        if (it == object.end()) {
            throw std::runtime_error("missing JSON field: " + std::string(key));
        }
        return it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue parse() {
        skip_space();
        JsonValue value = parse_value();
        skip_space();
        if (position_ != text_.size()) {
            throw std::runtime_error("trailing data in JSON document");
        }
        return value;
    }

private:
    void skip_space() {
        while (position_ < text_.size()
               && std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    char consume() {
        if (position_ >= text_.size()) {
            throw std::runtime_error("unexpected end of JSON document");
        }
        return text_[position_++];
    }

    void expect(char expected) {
        if (consume() != expected) {
            throw std::runtime_error("invalid JSON punctuation");
        }
    }

    JsonValue parse_value() {
        skip_space();
        if (position_ >= text_.size()) {
            throw std::runtime_error("missing JSON value");
        }
        switch (text_[position_]) {
        case '{': return parse_object();
        case '[': return parse_array();
        case '"': return JsonValue{JsonValue::Type::String, 0, false, parse_string(), {}, {}};
        case 't': return parse_literal("true", JsonValue::Type::Boolean, true);
        case 'f': return parse_literal("false", JsonValue::Type::Boolean, false);
        case 'n': return parse_literal("null", JsonValue::Type::Null, false);
        default: return parse_number();
        }
    }

    JsonValue parse_literal(const char* literal, JsonValue::Type type, bool value) {
        const std::size_t length = std::char_traits<char>::length(literal);
        if (text_.compare(position_, length, literal) != 0) {
            throw std::runtime_error("invalid JSON literal");
        }
        position_ += length;
        JsonValue result;
        result.type = type;
        result.boolean = value;
        return result;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (true) {
            const char ch = consume();
            if (ch == '"') {
                return result;
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            const char escaped = consume();
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
                // Generated manifests contain ASCII strings. Consume a
                // Unicode escape rather than silently desynchronizing the
                // parser if one is introduced later.
                for (int i = 0; i < 4; ++i) {
                    consume();
                }
                result.push_back('?');
                break;
            default:
                throw std::runtime_error("unsupported JSON string escape");
            }
        }
    }

    JsonValue parse_number() {
        const std::size_t start = position_;
        if (text_[position_] == '-') {
            ++position_;
        }
        while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
        // The generated manifest uses integers. Accept a decimal fraction
        // or exponent to keep the parser well-behaved for generic JSON, but
        // reject it when converting to the integer-only runtime structures.
        while (position_ < text_.size()
               && (text_[position_] == '.' || text_[position_] == 'e' || text_[position_] == 'E'
                   || text_[position_] == '+' || text_[position_] == '-')) {
            ++position_;
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }
        const std::string token = text_.substr(start, position_ - start);
        if (token.empty()) {
            throw std::runtime_error("invalid JSON number");
        }
        JsonValue result;
        result.type = JsonValue::Type::Number;
        result.number = std::stoll(token);
        return result;
    }

    JsonValue parse_array() {
        expect('[');
        JsonValue result;
        result.type = JsonValue::Type::Array;
        skip_space();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return result;
        }
        while (true) {
            result.array.push_back(parse_value());
            skip_space();
            const char delimiter = consume();
            if (delimiter == ']') {
                return result;
            }
            if (delimiter != ',') {
                throw std::runtime_error("invalid JSON array delimiter");
            }
        }
    }

    JsonValue parse_object() {
        expect('{');
        JsonValue result;
        result.type = JsonValue::Type::Object;
        skip_space();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return result;
        }
        while (true) {
            skip_space();
            const std::string key = parse_string();
            skip_space();
            expect(':');
            result.object.emplace(key, parse_value());
            skip_space();
            const char delimiter = consume();
            if (delimiter == '}') {
                return result;
            }
            if (delimiter != ',') {
                throw std::runtime_error("invalid JSON object delimiter");
            }
        }
    }

    std::string text_;
    std::size_t position_ = 0;
};

long long integer(const JsonValue& value) {
    if (value.type == JsonValue::Type::Number) {
        return value.number;
    }
    if (value.type == JsonValue::Type::String) {
        std::size_t consumed = 0;
        const long long result = std::stoll(value.string, &consumed, 0);
        if (consumed != value.string.size()) {
            throw std::runtime_error("non-integer JSON string: " + value.string);
        }
        return result;
    }
    throw std::runtime_error("JSON field is not an integer");
}

long long optional_integer(const JsonValue& object, std::string_view key, long long fallback) {
    const auto it = object.object.find(std::string(key));
    return it == object.object.end() ? fallback : integer(it->second);
}

bool optional_boolean(const JsonValue& object, std::string_view key, bool fallback) {
    const auto it = object.object.find(std::string(key));
    if (it == object.object.end()) {
        return fallback;
    }
    if (it->second.type != JsonValue::Type::Boolean) {
        throw std::runtime_error("JSON field is not a boolean");
    }
    return it->second.boolean;
}

std::string read_text(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> read_binary(const std::string& path) {
    const std::string contents = read_text(path);
    return std::vector<std::uint8_t>(contents.begin(), contents.end());
}

SDL_Color genesis_color(std::uint16_t word) {
    const auto channel = [](std::uint16_t value, int shift) {
        return static_cast<std::uint8_t>(((value >> shift) & 7) * 255 / 7);
    };
    return SDL_Color{channel(word, 1), channel(word, 5), channel(word, 9), 255};
}

struct TileSet {
    int width_tiles = 0;
    int height_tiles = 0;
    int tile_bytes = 0;
    std::vector<std::uint8_t> bytes;
};

std::vector<std::uint8_t> decode_part(const TileSet& tile_set, int tile_index, int width, int height) {
    if (width <= 0 || height <= 0 || width % 8 != 0 || height % 8 != 0) {
        throw std::runtime_error("invalid Chopper part dimensions");
    }
    const int tile_width = width / 8;
    const int tile_height = height / 8;
    const std::size_t base = static_cast<std::size_t>(tile_index) * tile_set.tile_bytes;
    const std::size_t required = static_cast<std::size_t>(tile_width * tile_height) * 32;
    if (tile_index < 0 || base + required > tile_set.bytes.size()
        || tile_width != tile_set.width_tiles || tile_height != tile_set.height_tiles) {
        throw std::runtime_error("Chopper part tile index is outside its tile set");
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tile_x = x / 8;
            const int tile_y = y / 8;
            // Chopper stores the tiles in each multipart sprite column-first
            // (all rows of a column precede the next column), rather than
            // the row-major order used by a normal linear tilemap.
            const int tile_index = tile_x * tile_set.height_tiles + tile_y;
            const std::size_t tile_offset = base
                + static_cast<std::size_t>(tile_index * 32);
            const std::size_t row = tile_offset + static_cast<std::size_t>((y & 7) * 4);
            const std::uint8_t packed = tile_set.bytes[row + static_cast<std::size_t>((x & 7) / 2)];
            pixels[static_cast<std::size_t>(y * width + x)] = (x & 1) == 0
                ? static_cast<std::uint8_t>(packed >> 4)
                : static_cast<std::uint8_t>(packed & 0x0F);
        }
    }
    return pixels;
}

std::string resolve_tile_path(const std::string& sprite_root, const std::string& relative) {
    // The extractor writes paths relative to build/assets, while callers
    // naturally pass build/assets/sprites as the database root.
    if (relative.rfind("sprites/", 0) == 0) {
        const std::size_t slash = sprite_root.find_last_of("/\\");
        const std::string parent = slash == std::string::npos ? std::string{} : sprite_root.substr(0, slash);
        return parent + "/" + relative;
    }
    return sprite_root + "/" + relative;
}

std::uint32_t pack_rgba(SDL_Color color) {
    return static_cast<std::uint32_t>(color.r)
        | (static_cast<std::uint32_t>(color.g) << 8)
        | (static_cast<std::uint32_t>(color.b) << 16)
        | (static_cast<std::uint32_t>(color.a) << 24);
}

}  // namespace

void SpriteDatabase::load(const std::string& sprite_root) {
    const JsonValue document = JsonParser(read_text(sprite_root + "/frames.json")).parse();
    const auto& tile_set_values = document.at("tile_sets").object;
    std::map<std::string, TileSet> tile_sets;
    for (const auto& [name, value] : tile_set_values) {
        TileSet tile_set;
        tile_set.width_tiles = static_cast<int>(value.at("width_tiles").number);
        tile_set.height_tiles = static_cast<int>(value.at("height_tiles").number);
        tile_set.tile_bytes = static_cast<int>(value.at("tile_bytes").number);
        const std::string file = value.at("file").string;
        tile_set.bytes = read_binary(resolve_tile_path(sprite_root, file));
        const int count = static_cast<int>(value.at("count").number);
        if (tile_set.tile_bytes <= 0
            || tile_set.bytes.size() != static_cast<std::size_t>(tile_set.tile_bytes * count)) {
            throw std::runtime_error("Chopper tile set has an unexpected size: " + name);
        }
        tile_sets.emplace(name, std::move(tile_set));
    }

    palette_.clear();
    const auto palette_it = document.object.find("default_palette_words");
    if (palette_it != document.object.end()) {
        for (const JsonValue& value : palette_it->second.array) {
            palette_.push_back(genesis_color(static_cast<std::uint16_t>(integer(value))));
        }
    }
    if (palette_.empty()) {
        palette_.resize(16, SDL_Color{0, 0, 0, 255});
    }

    frames_.clear();
    for (const JsonValue& value : document.at("frames").array) {
        SpriteFrame frame;
        frame.index = static_cast<int>(value.at("index").number);
        frame.address = static_cast<int>(integer(value.at("address")));
        frame.origin_x = 0x80;
        frame.origin_y = 0x80;
        const auto origin_it = value.object.find("origin");
        if (origin_it != value.object.end() && origin_it->second.array.size() >= 2) {
            frame.origin_x = static_cast<int>(integer(origin_it->second.array[0]));
            frame.origin_y = static_cast<int>(integer(origin_it->second.array[1]));
        }
        const auto collision_min_it = value.object.find("collision_min_pixels");
        const auto collision_max_it = value.object.find("collision_max_pixels");
        const auto& collision_min = collision_min_it != value.object.end()
            ? collision_min_it->second.array : value.at("collision_min").array;
        const auto& collision_max = collision_max_it != value.object.end()
            ? collision_max_it->second.array : value.at("collision_max").array;
        frame.collision_min_x = static_cast<int>(integer(collision_min[0]));
        frame.collision_min_y = static_cast<int>(integer(collision_min[1]));
        frame.collision_max_x = static_cast<int>(integer(collision_max[0]));
        frame.collision_max_y = static_cast<int>(integer(collision_max[1]));

        int layer = 0;
        for (const JsonValue& part_value : value.at("parts").array) {
            SpritePart part;
            const auto offset_it = part_value.object.find("offset_pixels");
            const auto& offset = offset_it != part_value.object.end()
                ? offset_it->second.array : part_value.at("offset_signed").array;
            part.offset_x = static_cast<int>(integer(offset[0]));
            part.offset_y = static_cast<int>(integer(offset[1]));
            part.tile_index = static_cast<int>(integer(part_value.at("tile_index")));
            part.palette_line = static_cast<int>(optional_integer(part_value, "palette_line", 0));
            part.flip_x = optional_boolean(part_value, "flip_x", false);
            part.flip_y = optional_boolean(part_value, "flip_y", false);
            part.layer = static_cast<int>(optional_integer(part_value, "layer", layer++));
            const auto& info = part_value.at("tile_info");
            part.width = static_cast<int>(info.at("pixel_width").number);
            part.height = static_cast<int>(info.at("pixel_height").number);
            const auto tile_set_it = tile_sets.find(part_value.at("tile_set").string);
            if (tile_set_it == tile_sets.end()) {
                throw std::runtime_error("frame references unknown Chopper tile set");
            }
            part.pixels = decode_part(tile_set_it->second, part.tile_index, part.width, part.height);
            frame.parts.push_back(std::move(part));
        }
        frames_.push_back(std::move(frame));
    }
}

const SpriteFrame& SpriteDatabase::frame(int index) const {
    const auto it = std::find_if(frames_.begin(), frames_.end(), [index](const SpriteFrame& value) {
        return value.index == index;
    });
    if (it == frames_.end()) {
        throw std::runtime_error("sprite frame is not present: " + std::to_string(index));
    }
    return *it;
}

int SpriteDatabase::frame_index_for_address(int address) const {
    for (const SpriteFrame& candidate : frames_) {
        if (candidate.address == address) {
            return candidate.index;
        }
    }
    return -1;
}

const SpriteFrame& SpriteDatabase::frame_for(SpritePose pose) const {
    switch (pose) {
    case SpritePose::Idle: return frame(kIdleFrame);
    case SpritePose::Run: return frame(kRunFrame);
    case SpritePose::Brake: return frame(kBrakeFrame);
    case SpritePose::Jump: return frame(kJumpFrame);
    case SpritePose::Landing: return frame(kLandingFrame);
    }
    throw std::runtime_error("unknown sprite pose");
}

void SpriteRenderer::draw(
    const SpriteFrame& frame,
    const std::vector<SDL_Color>& palette,
    std::vector<std::uint32_t>& framebuffer,
    int framebuffer_width,
    int framebuffer_height,
    int screen_origin_x,
    int screen_origin_y,
    bool flip_x,
    bool flip_y,
    int palette_line_override
) {
    if (framebuffer_width <= 0 || framebuffer_height <= 0
        || framebuffer.size() < static_cast<std::size_t>(framebuffer_width * framebuffer_height)) {
        throw std::runtime_error("sprite framebuffer is too small");
    }
    std::vector<const SpritePart*> parts;
    parts.reserve(frame.parts.size());
    for (const SpritePart& part : frame.parts) {
        parts.push_back(&part);
    }
    std::stable_sort(parts.begin(), parts.end(), [](const SpritePart* left, const SpritePart* right) {
        return left->layer < right->layer;
    });

    for (const SpritePart* part : parts) {
        if (part->width <= 0 || part->height <= 0
            || part->pixels.size() < static_cast<std::size_t>(part->width * part->height)) {
            throw std::runtime_error("sprite part has invalid decoded pixels");
        }
        const bool part_flip_x = flip_x != part->flip_x;
        const bool part_flip_y = flip_y != part->flip_y;
        const int draw_x = screen_origin_x + (flip_x ? -part->offset_x - part->width : part->offset_x);
        const int draw_y = screen_origin_y + (flip_y ? -part->offset_y - part->height : part->offset_y);
        const int palette_start = (palette_line_override >= 0
            ? palette_line_override : part->palette_line) * 16;
        for (int y = 0; y < part->height; ++y) {
            for (int x = 0; x < part->width; ++x) {
                const int screen_x = draw_x + x;
                const int screen_y = draw_y + y;
                if (screen_x < 0 || screen_x >= framebuffer_width
                    || screen_y < 0 || screen_y >= framebuffer_height) {
                    continue;
                }
                const int source_x = part_flip_x ? part->width - 1 - x : x;
                const int source_y = part_flip_y ? part->height - 1 - y : y;
                const std::uint8_t color_index = part->pixels[static_cast<std::size_t>(source_y * part->width + source_x)];
                if (color_index == 0) {
                    continue;
                }
                const int palette_index = palette_start + color_index;
                if (palette_index < 0 || palette_index >= static_cast<int>(palette.size())) {
                    throw std::runtime_error("sprite palette index is outside the loaded palette");
                }
                framebuffer[static_cast<std::size_t>(screen_y * framebuffer_width + screen_x)] = pack_rgba(palette[palette_index]);
            }
        }
    }
}

}  // namespace openaladdin
