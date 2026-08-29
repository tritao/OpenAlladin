#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <vector>

namespace openaladdin {

using ActorIndex = std::size_t;

struct ActorState {
    std::uint8_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t movement_flags = 0;
    std::uint8_t runtime_field_07 = 0;
    std::uint8_t runtime_field_07_delay = 0;
    std::uint8_t facing_x_flip = 0;
    std::uint8_t facing_y_flip = 0;
    std::uint32_t movement_pc = 0;
    std::uint32_t movement_loop_pc = 0;
    std::uint8_t movement_loop_timer = 0;
    std::int16_t movement_word_18 = 0;
    std::int16_t movement_word_1a = 0;
    // Raw actor-table word at +0x1E. This is the Genesis SAT tile
    // attribute base; its palette and priority bits are part of the actor
    // template and must not be inferred from the actor type.
    std::uint16_t sprite_attribute = 0;
    std::uint32_t frame_ptr = 0;
    std::uint32_t animation_pc = 0;
    std::uint32_t movement_return_pc = 0;
    std::uint8_t flags = 0;
    std::uint8_t interaction_state = 0;
    std::uint8_t terminal_timer = 0;
    std::uint8_t movement_command_timer = 0;
    std::uint8_t animation_timer = 0;
    std::uint8_t resource_count = 0;
    std::uint16_t interaction_resource_offset = 0;
    std::uint8_t interaction_selector = 0;
    // This is the Genesis record link at +0x3E. Unlike spawn provenance it is
    // part of the actor's observable runtime state and is therefore kept in
    // the record itself.
    int linked_actor_slot = -1;
};

enum class ActorAllocationPool {
    CommonForward,
    CommonReverse,
    GameplayForward,
    GameplayReverse,
    AuxiliaryForward,
};

using ActorPool = ActorAllocationPool;

struct ActorResourceAllocation {
    std::uint8_t first_slot = 0;
    std::uint8_t slot_count = 0;
    std::uint32_t genesis_vram_base = 0;

    bool valid() const { return slot_count != 0; }
};

// The ROM's logical sprite-resource allocator is independent of SDL/VRAM.
// It is still semantic state: allocation failure retires actors, and the
// allocation is released by the actor lifecycle helpers.
class ActorSpriteResources {
public:
    static constexpr std::size_t kSlotCount = 0x74;
    static constexpr std::uint32_t kSlotBitmapAddress = 0x00FFF008;
    static constexpr std::uint32_t kVramBaseTableAddress = 0x0011F500;
    static constexpr std::uint32_t kVramBaseStart = 0x00008600;

    void reset();
    std::optional<ActorResourceAllocation> allocate(std::uint8_t resource_count);
    void release(const ActorResourceAllocation& allocation);

    const std::array<std::uint8_t, kSlotCount>& slot_bitmap() const {
        return slot_bitmap_;
    }
    void restore_slot_bitmap(const std::array<std::uint8_t, kSlotCount>& bitmap) {
        slot_bitmap_ = bitmap;
    }

private:
    std::array<std::uint8_t, kSlotCount> slot_bitmap_{};
};

// Spawn origin is host bookkeeping used by the current parity scheduler. It
// is deliberately parallel to the ROM-shaped actor records rather than being
// mistaken for an actor field.
struct ActorHostMeta {
    bool spawned_by_interaction = false;
    bool spawned_by_animation = false;
    bool spawned_by_apple = false;
};

struct SpawnDescriptor {
    bool valid = false;
    std::uint8_t selector = 0;
    std::uint32_t template_address = 0;
    ActorAllocationPool allocation_pool = ActorAllocationPool::CommonForward;
    int post_offset_x = 0;
    int post_offset_y = 0;
    bool override_type = false;
    std::uint8_t type = 0;
    bool override_animation = false;
    std::uint32_t animation_pc = 0;
    bool override_movement = false;
    std::uint32_t movement_pc = 0;
    bool override_resource_count = false;
    std::uint8_t resource_count = 0;
};

// Owns the fixed 32-record Genesis actor table and the allocator/culling
// policy shared by interaction and animation producers. It inherits the
// array interface intentionally: existing trace code can still inspect the
// records as a contiguous table while Engine no longer owns their storage.
class ActorSystem : public std::array<ActorState, 32> {
public:
    using Table = std::array<ActorState, 32>;

    ActorSystem& operator=(const Table& table) {
        static_cast<Table&>(*this) = table;
        host_meta_.fill({});
        resource_allocations_.fill(std::nullopt);
        sprite_resources_.reset();
        return *this;
    }

    void begin_frame();
    void reset(const Table& templates, bool snapshot_mode);
    void reset();
    void set_snapshot_mode(bool enabled) { snapshot_mode_ = enabled; }
    bool snapshot_mode() const { return snapshot_mode_; }
    Table& templates() { return templates_; }
    const Table& templates() const { return templates_; }
    std::optional<std::size_t> allocate_actor_slot(ActorAllocationPool pool) const;

    ActorHostMeta& host_meta(ActorIndex slot) { return host_meta_.at(slot); }
    const ActorHostMeta& host_meta(ActorIndex slot) const { return host_meta_.at(slot); }

    std::optional<ActorResourceAllocation> allocate_sprite_resources(
        ActorIndex slot,
        std::uint8_t resource_count
    );
    void release_sprite_resources(ActorIndex slot);
    std::optional<ActorResourceAllocation> resource_allocation(ActorIndex slot) const;
    const ActorSpriteResources& sprite_resources() const { return sprite_resources_; }

    std::vector<std::size_t> cull_interaction_actors(
        int left,
        int right,
        int top,
        int bottom
    );

    bool was_culled_this_frame(std::size_t slot) const;
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

private:
    std::array<bool, 32> culled_this_frame_{};
    std::array<ActorHostMeta, 32> host_meta_{};
    std::array<std::optional<ActorResourceAllocation>, 32> resource_allocations_{};
    ActorSpriteResources sprite_resources_{};
    Table templates_{};
    bool snapshot_mode_ = false;
};

}  // namespace openaladdin
