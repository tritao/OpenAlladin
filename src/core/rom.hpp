#pragma once

#include <cstddef>
#include <cstdint>

namespace openaladdin::core {

// The native host owns the ROM storage. The gameplay core only observes this
// immutable view, just as the 68000 observes the cartridge image.
struct RomView {
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
};

constexpr bool rom_is_bound(RomView rom) {
    return rom.bytes != nullptr && rom.size != 0;
}

std::uint8_t rom_read8(RomView rom, std::size_t offset);
std::uint16_t rom_read16(RomView rom, std::size_t offset);
std::uint32_t rom_read32(RomView rom, std::size_t offset);

}  // namespace openaladdin::core
