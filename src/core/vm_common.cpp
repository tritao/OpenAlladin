#include "core/vm_common.hpp"

namespace openaladdin::core {

RamAddress vm_target_address(
    std::size_t actor_slot,
    std::uint8_t mode,
    std::uint16_t offset
) {
    if ((mode & 0x10) != 0) {
        return actor_address(actor_slot, offset);
    }
    return kWorkRamBase + offset;
}

std::uint8_t vm_value_width(std::uint8_t mode) {
    const std::uint8_t width = static_cast<std::uint8_t>(mode & 0x0F);
    return width == 1 || width == 2 ? width : 4;
}

std::uint8_t vm_encoded_operand_size(std::uint8_t mode) {
    const std::uint8_t width = vm_value_width(mode);
    return width == 4 ? 4 : 2;
}

std::uint32_t vm_read_encoded_value(
    RomView rom,
    std::uint32_t cursor,
    std::uint8_t mode
) {
    return vm_encoded_operand_size(mode) == 2
        ? rom_read16(rom, cursor)
        : rom_read32(rom, cursor);
}

std::uint32_t vm_apply_encoded_write(
    GenesisRam& ram,
    RomView rom,
    std::size_t actor_slot,
    std::uint32_t cursor
) {
    const std::uint8_t mode = rom_read8(rom, cursor + 1);
    const std::uint8_t width = vm_value_width(mode);
    const std::uint16_t offset = rom_read16(rom, cursor + 2);
    const std::uint32_t value = vm_read_encoded_value(rom, cursor + 4, mode);
    const RamAddress address = vm_target_address(actor_slot, mode, offset);
    if (width == 1) {
        write8(ram, address, static_cast<std::uint8_t>(value));
    } else if (width == 2) {
        write16(ram, address, static_cast<std::uint16_t>(value));
    } else {
        write32(ram, address, value);
    }
    return cursor + 4 + vm_encoded_operand_size(mode);
}

}  // namespace openaladdin::core
