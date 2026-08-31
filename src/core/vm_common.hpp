#pragma once

#include "core/ram.hpp"
#include "core/rom.hpp"

#include <cstddef>
#include <cstdint>

namespace openaladdin::core {

struct VmRunResult {
    bool completed = false;
    bool stopped = false;
    std::uint16_t command_count = 0;
    std::uint32_t cursor = 0;
    std::uint8_t blocked_opcode = 0;
};

// Animation ED and Movement 83 are one shared ROM handler with different
// opcode labels. Keeping the address, operand-width, and write operation in
// one helper prevents the two interpreters from growing apart.
RamAddress vm_target_address(
    std::size_t actor_slot,
    std::uint8_t mode,
    std::uint16_t offset
);

std::uint8_t vm_value_width(std::uint8_t mode);
std::uint8_t vm_encoded_operand_size(std::uint8_t mode);
std::uint32_t vm_read_encoded_value(
    RomView rom,
    std::uint32_t cursor,
    std::uint8_t mode
);

std::uint32_t vm_apply_encoded_write(
    GenesisRam& ram,
    RomView rom,
    std::size_t actor_slot,
    std::uint32_t cursor
);

}  // namespace openaladdin::core
