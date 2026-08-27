#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <vector>

namespace openaladdin {

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
    bool spawned_by_interaction = false;
    bool spawned_by_animation = false;
    bool spawned_by_apple = false;
    int linked_actor_slot = -1;
};

enum class ActorAllocationPool {
    CommonForward,
    CommonReverse,
    GameplayForward,
    GameplayReverse,
    AuxiliaryForward,
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
    Table templates_{};
    bool snapshot_mode_ = false;
};

}  // namespace openaladdin
