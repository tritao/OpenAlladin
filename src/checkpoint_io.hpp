#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace openaladdin::checkpoint {

// Checkpoints are deliberately little-endian and independent of native
// object layout. They are debugging artifacts, not C++ memory images.
class Writer {
public:
    explicit Writer(std::ostream& output) : output_(output) {}

    void u8(std::uint8_t value) { put(&value, sizeof(value)); }

    void u16(std::uint16_t value) {
        const std::uint8_t bytes[] = {
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8),
        };
        put(bytes, sizeof(bytes));
    }

    void u32(std::uint32_t value) {
        const std::uint8_t bytes[] = {
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 24),
        };
        put(bytes, sizeof(bytes));
    }

    void i16(std::int16_t value) {
        u16(static_cast<std::uint16_t>(value));
    }

    void i32(std::int32_t value) {
        u32(static_cast<std::uint32_t>(value));
    }

    void boolean(bool value) { u8(value ? 1 : 0); }

    void bytes(const std::uint8_t* data, std::size_t size) {
        if (size != 0) put(data, size);
    }

    void byte_vector(const std::vector<std::uint8_t>& data) {
        u32(static_cast<std::uint32_t>(data.size()));
        bytes(data.data(), data.size());
    }

private:
    void put(const void* data, std::size_t size) {
        output_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!output_) throw std::runtime_error("cannot write OpenAladdin checkpoint");
    }

    std::ostream& output_;
};

class Reader {
public:
    explicit Reader(std::istream& input) : input_(input) {}

    bool has_more() {
        return input_.peek() != std::char_traits<char>::eof();
    }

    std::uint8_t u8() {
        std::uint8_t value = 0;
        get(&value, sizeof(value));
        return value;
    }

    std::uint16_t u16() {
        const std::uint8_t bytes[2] = {u8(), u8()};
        return static_cast<std::uint16_t>(bytes[0])
            | static_cast<std::uint16_t>(bytes[1] << 8);
    }

    std::uint32_t u32() {
        const std::uint8_t bytes[4] = {u8(), u8(), u8(), u8()};
        return static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1]) << 8)
            | (static_cast<std::uint32_t>(bytes[2]) << 16)
            | (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }

    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    bool boolean() {
        const std::uint8_t value = u8();
        if (value > 1) throw std::runtime_error("invalid boolean in OpenAladdin checkpoint");
        return value != 0;
    }

    void bytes(std::uint8_t* data, std::size_t size) {
        if (size != 0) get(data, size);
    }

    std::vector<std::uint8_t> byte_vector(std::size_t maximum_size) {
        const std::uint32_t size = u32();
        if (size > maximum_size) {
            throw std::runtime_error("oversized vector in OpenAladdin checkpoint");
        }
        std::vector<std::uint8_t> result(size);
        bytes(result.data(), result.size());
        return result;
    }

private:
    void get(void* data, std::size_t size) {
        input_.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        if (!input_) throw std::runtime_error("truncated OpenAladdin checkpoint");
    }

    std::istream& input_;
};

inline void magic(Writer& writer, const char* value, std::size_t size) {
    writer.bytes(reinterpret_cast<const std::uint8_t*>(value), size);
}

inline void expect_magic(Reader& reader, const char* expected, std::size_t size) {
    std::string actual(size, '\0');
    reader.bytes(reinterpret_cast<std::uint8_t*>(actual.data()), size);
    if (actual != std::string(expected, size)) {
        throw std::runtime_error("invalid OpenAladdin checkpoint signature");
    }
}

}  // namespace openaladdin::checkpoint
