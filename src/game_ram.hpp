#pragma once

#include <array>
#include <cstdint>
#include <map>

namespace openaladdin {

struct AnimationContext;
struct ActorState;
struct GameState;

using RamAddress = std::uint32_t;

// Address-based VM commands still use the original Genesis addresses, but
// this view resolves recovered addresses to typed runtime state. Unclassified
// addresses use a sparse byte store so the animation interpreter can preserve
// ROM behavior without carrying a second 64 KiB RAM image.
class GameRamView {
public:
    GameRamView() = default;

    void bind_state(GameState& state) { state_ = &state; }
    void bind_context(const AnimationContext& context);
    void clear_context();
    std::uint32_t* random_state();
    void bind_actor_record(std::array<std::uint8_t, 0x42>& record) {
        actor_state_ = nullptr;
        actor_record_ = &record;
    }
    // Actor-relative animation commands address the live Genesis actor
    // record. The optional byte record is retained only for unmapped/private
    // offsets and for the legacy checkpoint/diagnostic view.
    void bind_actor(
        ActorState& actor,
        std::array<std::uint8_t, 0x42>& private_bytes
    ) {
        actor_state_ = &actor;
        actor_record_ = &private_bytes;
    }

    void reset();
    void set_write_tracking(bool enabled) { tracking_writes_ = enabled; }

    std::uint8_t read8(RamAddress address) const;
    std::uint16_t read16(RamAddress address) const;
    std::uint32_t read32(RamAddress address) const;
    std::array<std::uint8_t, 0x42> actor_record() const;

    void write8(RamAddress address, std::uint8_t value);
    void write16(RamAddress address, std::uint16_t value);
    void write32(RamAddress address, std::uint32_t value);

    bool take_write(RamAddress address, std::uint8_t& value);

    // These preserve the current OACP checkpoint wire format during the
    // migration. Only the serialization is dense; the live view is sparse.
    void copy_legacy_memory(std::array<std::uint8_t, 0x10000>& memory) const;
    void copy_legacy_write_flags(std::array<std::uint8_t, 0x10000>& flags) const;
    void restore_legacy_memory(
        const std::array<std::uint8_t, 0x10000>& memory,
        const std::array<std::uint8_t, 0x10000>& flags
    );

private:
    static bool is_typed_address(RamAddress address);
    std::uint8_t read_actor8(RamAddress address, bool& handled) const;
    void write_actor8(RamAddress address, std::uint8_t value, bool& handled);
    std::uint8_t read_typed8(RamAddress address, bool& handled) const;
    void write_typed8(RamAddress address, std::uint8_t value, bool& handled);
    std::uint8_t read_sparse8(RamAddress address) const;
    std::array<std::uint8_t, 0x42> actor_record_snapshot() const;

    GameState* state_ = nullptr;
    const AnimationContext* context_ = nullptr;
    ActorState* actor_state_ = nullptr;
    std::array<std::uint8_t, 0x42>* actor_record_ = nullptr;
    std::map<RamAddress, std::uint8_t> sparse_memory_;
    std::map<RamAddress, std::uint8_t> context_overrides_;
    std::map<RamAddress, std::uint8_t> pending_writes_;
    bool tracking_writes_ = false;
};

}  // namespace openaladdin
