#include "level_event.hpp"

#include "game_state.hpp"

namespace openaladdin {

void LevelEventVm::reset() {
    cursor_ = {};
    tick_ = 0;
    faulted_ = false;
}

void LevelEventVm::start(RomAddress stream) {
    cursor_ = stream;
    tick_ = 0;
    faulted_ = false;
}

void LevelEventVm::restore(RomAddress stream, std::uint8_t tick) {
    cursor_ = stream;
    tick_ = stream.present() ? tick : 0;
    faulted_ = false;
}

bool LevelEventVm::can_read(std::size_t count) const {
    if (rom_ == nullptr) return false;
    const auto offset = static_cast<std::size_t>(cursor_.value);
    return offset <= rom_->size() && count <= rom_->size() - offset;
}

std::uint8_t LevelEventVm::read8(std::size_t offset) const {
    return (*rom_)[static_cast<std::size_t>(cursor_.value) + offset];
}

std::uint16_t LevelEventVm::read16(std::size_t offset) const {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(read8(offset)) << 8)
        | read8(offset + 1));
}

void LevelEventVm::disable() {
    cursor_ = {};
    tick_ = 0;
}

void LevelEventVm::update(GameState& state, LevelEventServices& services) {
    (void) state;
    if (!active()) return;

    if (!can_read(1)) {
        faulted_ = true;
        disable();
        return;
    }

    const std::uint8_t delay = read8(0);
    if (delay == 0) {
        disable();
        return;
    }

    // This is an 8-bit Genesis RAM counter. Deliberately retain byte
    // overflow rather than widening the comparison to native int semantics.
    tick_ = static_cast<std::uint8_t>(tick_ + 1U);
    if (tick_ <= delay) return;

    if (!can_read(kRecordSize)) {
        faulted_ = true;
        disable();
        return;
    }

    const LevelEventCommand event{
        delay,
        read8(1),
        read16(2),
        read16(4),
    };
    cursor_.value += static_cast<std::uint32_t>(kRecordSize);
    tick_ = 0;
    if (services.dispatch) services.dispatch(event);
}

}  // namespace openaladdin
