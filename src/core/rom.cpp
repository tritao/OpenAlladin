#include "core/rom.hpp"

namespace openaladdin::core {

std::uint8_t rom_read8(RomView rom, std::size_t offset) {
    if (!rom_is_bound(rom) || offset >= rom.size) return 0;
    return rom.bytes[offset];
}

std::uint16_t rom_read16(RomView rom, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(rom_read8(rom, offset)) << 8)
        | static_cast<std::uint16_t>(rom_read8(rom, offset + 1)));
}

std::uint32_t rom_read32(RomView rom, std::size_t offset) {
    return (static_cast<std::uint32_t>(rom_read16(rom, offset)) << 16)
        | static_cast<std::uint32_t>(rom_read16(rom, offset + 2));
}

}  // namespace openaladdin::core
