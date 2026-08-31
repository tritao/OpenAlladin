#include "core/ram.hpp"

namespace openaladdin::core {
namespace {

std::uint8_t read_offset8(const GenesisRam& ram, std::size_t offset) {
    return offset < ram.bytes.size() ? ram.bytes[offset] : 0;
}

void write_offset8(GenesisRam& ram, std::size_t offset, std::uint8_t value) {
    if (offset < ram.bytes.size()) ram.bytes[offset] = value;
}

}  // namespace

std::uint8_t read8(const GenesisRam& ram, RamAddress address) {
    return is_work_ram_address(address)
        ? read_offset8(ram, work_ram_offset(address))
        : 0;
}

std::uint16_t read16(const GenesisRam& ram, RamAddress address) {
    return static_cast<std::uint16_t>(read8(ram, address) << 8)
        | static_cast<std::uint16_t>(read8(ram, address + 1));
}

std::uint32_t read32(const GenesisRam& ram, RamAddress address) {
    return (static_cast<std::uint32_t>(read16(ram, address)) << 16)
        | static_cast<std::uint32_t>(read16(ram, address + 2));
}

void write8(GenesisRam& ram, RamAddress address, std::uint8_t value) {
    if (is_work_ram_address(address)) {
        write_offset8(ram, work_ram_offset(address), value);
    }
}

void write16(GenesisRam& ram, RamAddress address, std::uint16_t value) {
    write8(ram, address, static_cast<std::uint8_t>(value >> 8));
    write8(ram, address + 1, static_cast<std::uint8_t>(value));
}

void write32(GenesisRam& ram, RamAddress address, std::uint32_t value) {
    write16(ram, address, static_cast<std::uint16_t>(value >> 16));
    write16(ram, address + 2, static_cast<std::uint16_t>(value));
}

std::int16_t read_i16(const GenesisRam& ram, RamAddress address) {
    return static_cast<std::int16_t>(read16(ram, address));
}

void write_i16(GenesisRam& ram, RamAddress address, std::int16_t value) {
    write16(ram, address, static_cast<std::uint16_t>(value));
}

std::uint16_t player_x(const GenesisRam& ram) {
    return read16(ram, kPlayerX);
}

std::uint16_t player_y(const GenesisRam& ram) {
    return read16(ram, kPlayerY);
}

std::uint16_t player_world_x(const GenesisRam& ram) {
    return read16(ram, kPlayerWorldX);
}

std::uint16_t player_world_y(const GenesisRam& ram) {
    return read16(ram, kPlayerWorldY);
}

void write_player_x(GenesisRam& ram, std::uint16_t value) {
    write16(ram, kPlayerX, value);
}

void write_player_y(GenesisRam& ram, std::uint16_t value) {
    write16(ram, kPlayerY, value);
}

ActorView actor_view(GenesisRam& ram, std::size_t slot) {
    if (!is_actor_slot(slot)) return ActorView{nullptr, 0};
    return ActorView{&ram, static_cast<std::uint8_t>(
        slot)};
}

ConstActorView actor_view(const GenesisRam& ram, std::size_t slot) {
    if (!is_actor_slot(slot)) return ConstActorView{nullptr, 0};
    return ConstActorView{&ram, static_cast<std::uint8_t>(
        slot)};
}

std::uint8_t actor_read8(ConstActorView actor, std::size_t offset) {
    if (actor.ram == nullptr || offset >= kActorRecordSize) return 0;
    return read8(*actor.ram, actor_address(actor.slot, offset));
}

std::uint16_t actor_read16(ConstActorView actor, std::size_t offset) {
    if (actor.ram == nullptr || offset + 1 >= kActorRecordSize) return 0;
    return read16(*actor.ram, actor_address(actor.slot, offset));
}

std::uint32_t actor_read32(ConstActorView actor, std::size_t offset) {
    if (actor.ram == nullptr || offset + 3 >= kActorRecordSize) return 0;
    return read32(*actor.ram, actor_address(actor.slot, offset));
}

std::uint8_t actor_read8(ActorView actor, std::size_t offset) {
    return actor_read8(ConstActorView{actor.ram, actor.slot}, offset);
}

std::uint16_t actor_read16(ActorView actor, std::size_t offset) {
    return actor_read16(ConstActorView{actor.ram, actor.slot}, offset);
}

std::uint32_t actor_read32(ActorView actor, std::size_t offset) {
    return actor_read32(ConstActorView{actor.ram, actor.slot}, offset);
}

void actor_write8(ActorView actor, std::size_t offset, std::uint8_t value) {
    if (actor.ram == nullptr || offset >= kActorRecordSize) return;
    write8(*actor.ram, actor_address(actor.slot, offset), value);
}

void actor_write16(ActorView actor, std::size_t offset, std::uint16_t value) {
    if (actor.ram == nullptr || offset + 1 >= kActorRecordSize) return;
    write16(*actor.ram, actor_address(actor.slot, offset), value);
}

void actor_write32(ActorView actor, std::size_t offset, std::uint32_t value) {
    if (actor.ram == nullptr || offset + 3 >= kActorRecordSize) return;
    write32(*actor.ram, actor_address(actor.slot, offset), value);
}

}  // namespace openaladdin::core
