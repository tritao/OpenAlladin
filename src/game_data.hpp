#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace openaladdin {

// Provides typed views over immutable ROM data. The views retain ROM identity
// without copying tables into native subsystem state.
class GameData {
public:
    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }

    std::span<const std::uint8_t> camera_horizontal_damping() const;
    std::span<const std::uint8_t> camera_vertical_damping() const;

private:
    const std::vector<std::uint8_t>* rom_ = nullptr;
};

}  // namespace openaladdin
