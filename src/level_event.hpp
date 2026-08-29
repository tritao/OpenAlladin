#pragma once

#include "level.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace openaladdin {

struct GameState;

struct LevelEventCommand {
    std::uint8_t delay = 0;
    std::uint8_t command = 0;
    std::uint16_t arg0 = 0;
    std::uint16_t arg1 = 0;
};

// The timed level-event interpreter only decodes the original command
// records. Gameplay, scene, and audio policy remains owned by the service
// callback supplied by the scheduler.
struct LevelEventServices {
    std::function<void(const LevelEventCommand&)> dispatch;
};

class LevelEventVm {
public:
    static constexpr std::size_t kRecordSize = 6;

    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }
    void reset();
    void start(RomAddress stream);
    // Restore the live cursor/tick pair at a checkpoint boundary. The event
    // stream is ROM-owned, so only the address and elapsed byte are runtime
    // state; bind_rom() must have been called before the next update.
    void restore(RomAddress stream, std::uint8_t tick);
    void update(GameState& state, LevelEventServices& services);

    bool active() const { return cursor_.present(); }
    bool faulted() const { return faulted_; }
    RomAddress cursor() const { return cursor_; }
    std::uint8_t tick() const { return tick_; }

private:
    bool can_read(std::size_t count) const;
    std::uint8_t read8(std::size_t offset) const;
    std::uint16_t read16(std::size_t offset) const;
    void disable();

    const std::vector<std::uint8_t>* rom_ = nullptr;
    RomAddress cursor_{};
    std::uint8_t tick_ = 0;
    bool faulted_ = false;
};

}  // namespace openaladdin
