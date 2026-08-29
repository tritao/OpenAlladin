#include "game_data.hpp"

#include <cstddef>

namespace openaladdin {
namespace {

constexpr std::size_t kCameraHorizontalDampingAddress = 0x2A52;
constexpr std::size_t kCameraHorizontalDampingSize = 0x152;
constexpr std::size_t kCameraVerticalDampingAddress = 0x2BA4;
constexpr std::size_t kCameraVerticalDampingSize = 0x0D4;

std::span<const std::uint8_t> rom_range(
    const std::vector<std::uint8_t>* rom,
    std::size_t address,
    std::size_t size
) {
    if (rom == nullptr || address > rom->size() || size > rom->size() - address) {
        return {};
    }
    return {rom->data() + address, size};
}

}  // namespace

std::span<const std::uint8_t> GameData::camera_horizontal_damping() const {
    return rom_range(
        rom_,
        kCameraHorizontalDampingAddress,
        kCameraHorizontalDampingSize
    );
}

std::span<const std::uint8_t> GameData::camera_vertical_damping() const {
    return rom_range(
        rom_,
        kCameraVerticalDampingAddress,
        kCameraVerticalDampingSize
    );
}

}  // namespace openaladdin
