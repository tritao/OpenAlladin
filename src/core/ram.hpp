#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace openaladdin::core {

using RamAddress = std::uint32_t;

constexpr RamAddress kWorkRamBase = 0x00FF0000;
constexpr std::size_t kWorkRamSize = 0x10000;
constexpr RamAddress kWorkRamLast = kWorkRamBase + kWorkRamSize - 1;

constexpr RamAddress kPlayerX = 0x00FF7DFA;
constexpr RamAddress kPlayerY = 0x00FF7DFC;
constexpr RamAddress kWorldCameraX = 0x00FF7DF6;
constexpr RamAddress kWorldCameraY = 0x00FF7DF8;
constexpr RamAddress kCameraHorizontalThreshold = 0x00FF7DFE;
constexpr RamAddress kCameraVerticalThreshold = 0x00FF7E00;
constexpr RamAddress kPlayerWorldX = 0x00FF7E02;
constexpr RamAddress kPlayerWorldY = 0x00FF7E04;
constexpr RamAddress kCameraReferenceX = 0x00FF7E06;
constexpr RamAddress kCameraReferenceY = 0x00FF7E08;
constexpr RamAddress kPlayerCameraPixelX = 0x00FF7E0A;
constexpr RamAddress kPlayerCameraPixelY = 0x00FF7E0C;
constexpr RamAddress kCameraTileX = 0x00FF7E0E;
constexpr RamAddress kCameraTileY = 0x00FF7E10;
constexpr RamAddress kCameraScrollDataCursor = 0x00FF7E1A;
constexpr RamAddress kVBlankReadyLatch = 0x00FF7E1E;
constexpr RamAddress kGameDifficultyMode = 0x00FF7E21;
constexpr RamAddress kSceneResourceStatus = 0x00FF7E22;
constexpr RamAddress kSceneResourceError = 0x00FF7E23;
constexpr RamAddress kSceneState = 0x00FF7E26;
constexpr RamAddress kFramePhaseCounter = 0x00FF7E28;
constexpr RamAddress kGameDifficultyCounter = 0x00FF7E3C;
constexpr RamAddress kActiveSceneEntryGate = 0x00FF7E3F;
constexpr RamAddress kActorTableBase = 0x00FF7E40;
constexpr std::size_t kActorRecordSize = 0x42;
constexpr std::size_t kActorSlotCount = 32;
constexpr RamAddress kActorTableEnd =
    kActorTableBase + kActorRecordSize * kActorSlotCount - 1;

constexpr RamAddress kCameraScrollX = 0x00FFF0B2;
constexpr RamAddress kCameraScrollY = 0x00FFF0B4;
constexpr RamAddress kCameraScrollLeftPending = 0x00FFF0B9;
constexpr RamAddress kCameraScrollRightPending = 0x00FFF0BA;
constexpr RamAddress kCameraScrollUpPending = 0x00FFF0BB;
constexpr RamAddress kCameraScrollDownPending = 0x00FFF0BC;
constexpr RamAddress kGlobalPrngState = 0x00FF7DEA;
constexpr RamAddress kActorVmCommandContinuation = 0x00FF7D9A;
constexpr RamAddress kActorVmCursorClearContinuation = 0x00FF7D9E;
constexpr RamAddress kActorVmMovementPass = 0x00FF7DA2;
constexpr RamAddress kPlayerTerrainQueryResult = 0x00FFF156;
constexpr RamAddress kPlayerTerrainPushRight = 0x00FFF07C;
constexpr RamAddress kPlayerTerrainPushLeft = 0x00FFF07D;
constexpr RamAddress kPlayerTerrainPushUp = 0x00FFF07E;
constexpr RamAddress kPlayerTerrainPushDown = 0x00FFF07F;
constexpr RamAddress kPlayerTerrainHorizontalResponse = 0x00FFF0B0;
constexpr RamAddress kPlayerTerrainResponseActive = 0x00FFF0BE;
constexpr RamAddress kPlayerTerrainJumpResponseCounter = 0x00FFF0BF;
constexpr RamAddress kPlayerTerrainVerticalStop = 0x00FFF0C0;
constexpr RamAddress kPlayerTerrainLandingState = 0x00FFF0C1;
constexpr RamAddress kPlayerTerrainBehavior = 0x00FFF0C3;
constexpr RamAddress kPlayerTerrainResponseTimer = 0x00FFF0CC;
constexpr RamAddress kPlayerTerrainState = 0x00FFF0D6;
constexpr RamAddress kPlayerTerrainResponseLatch = 0x00FFF115;
constexpr RamAddress kPlayerTerrainBrakeState = 0x00FFF101;
constexpr RamAddress kSceneScriptCursor = 0x00FFF572;
constexpr RamAddress kSceneScriptData = 0x00FFF576;
constexpr RamAddress kSceneTableIndex = 0x00FFF57A;
constexpr RamAddress kSceneScriptPending = 0x00FFF57C;
constexpr RamAddress kSceneVdpUpdateFlag = 0x00FFF57D;
constexpr RamAddress kSceneVdpClearFlag = 0x00FFF57E;
constexpr RamAddress kSceneTransitionEvent = 0x00FFF57F;

constexpr RamAddress kPlayerVelocityX = kActorTableBase + 0x18;
constexpr RamAddress kPlayerVelocityY = kActorTableBase + 0x1A;
constexpr RamAddress kPlayerFramePointer = kActorTableBase + 0x14;
constexpr RamAddress kPlayerAnimationPc = kActorTableBase + 0x20;
constexpr RamAddress kPlayerAnimationTimer = kActorTableBase + 0x37;
constexpr RamAddress kPlayerFacingXFlip = kActorTableBase + 0x09;
constexpr RamAddress kPlayerFacingYFlip = kActorTableBase + 0x35;
constexpr RamAddress kPlayerMovementPc = kActorTableBase + 0x0A;
constexpr RamAddress kPlayerMovementLoopPc = kActorTableBase + 0x0E;
constexpr RamAddress kPlayerMovementLoopTimer = kActorTableBase + 0x12;
constexpr RamAddress kPlayerMovementCommandTimer = kActorTableBase + 0x36;
constexpr RamAddress kPlayerMovementReturnPc = kActorTableBase + 0x38;
constexpr RamAddress kPlayerFlags = kActorTableBase + 0x3C;

// Offsets are the ROM record contract. Use actor_address(slot, offset) to
// obtain the corresponding absolute Genesis RAM address.
constexpr std::size_t kActorTypeOffset = 0x00;
constexpr std::size_t kActorTimerOffset = 0x01;
constexpr std::size_t kActorXOffset = 0x02;
constexpr std::size_t kActorYOffset = 0x04;
constexpr std::size_t kActorMovementFlagsOffset = 0x06;
constexpr std::size_t kActorRuntimeField07Offset = 0x07;
constexpr std::size_t kActorFacingXOffset = 0x09;
constexpr std::size_t kActorMovementPcOffset = 0x0A;
constexpr std::size_t kActorMovementLoopPcOffset = 0x0E;
constexpr std::size_t kActorMovementLoopTimerOffset = 0x12;
constexpr std::size_t kActorFramePointerOffset = 0x14;
constexpr std::size_t kActorVelocityXOffset = 0x18;
constexpr std::size_t kActorVelocityYOffset = 0x1A;
constexpr std::size_t kActorAnimationPcOffset = 0x20;
constexpr std::size_t kActorAnimationScratchOffset = 0x28;
constexpr std::size_t kActorFacingYOffset = 0x35;
constexpr std::size_t kActorMovementCommandTimerOffset = 0x36;
constexpr std::size_t kActorAnimationTimerOffset = 0x37;
constexpr std::size_t kActorMovementReturnPcOffset = 0x38;
constexpr std::size_t kActorFlagsOffset = 0x3C;
constexpr std::size_t kActorLinkedSlotOffset = 0x3E;

// The gameplay image is the complete 68000 work-RAM address space. No
// semantic field is stored elsewhere; these helpers are only named views.
struct GenesisRam {
    std::array<std::uint8_t, kWorkRamSize> bytes{};
};

constexpr bool is_work_ram_address(RamAddress address) {
    return address >= kWorkRamBase && address <= kWorkRamLast;
}

constexpr std::size_t work_ram_offset(RamAddress address) {
    return static_cast<std::size_t>(address - kWorkRamBase);
}

std::uint8_t read8(const GenesisRam& ram, RamAddress address);
std::uint16_t read16(const GenesisRam& ram, RamAddress address);
std::uint32_t read32(const GenesisRam& ram, RamAddress address);

void write8(GenesisRam& ram, RamAddress address, std::uint8_t value);
void write16(GenesisRam& ram, RamAddress address, std::uint16_t value);
void write32(GenesisRam& ram, RamAddress address, std::uint32_t value);

std::int16_t read_i16(const GenesisRam& ram, RamAddress address);
void write_i16(GenesisRam& ram, RamAddress address, std::int16_t value);

std::uint16_t player_x(const GenesisRam& ram);
std::uint16_t player_y(const GenesisRam& ram);
std::uint16_t player_world_x(const GenesisRam& ram);
std::uint16_t player_world_y(const GenesisRam& ram);
void write_player_x(GenesisRam& ram, std::uint16_t value);
void write_player_y(GenesisRam& ram, std::uint16_t value);

struct ActorView {
    GenesisRam* ram = nullptr;
    std::uint8_t slot = 0;
};

struct ConstActorView {
    const GenesisRam* ram = nullptr;
    std::uint8_t slot = 0;
};

constexpr bool is_actor_slot(std::size_t slot) {
    return slot < kActorSlotCount;
}

constexpr RamAddress actor_address(std::size_t slot, std::size_t offset) {
    return kActorTableBase
        + static_cast<RamAddress>(slot * kActorRecordSize + offset);
}

ActorView actor_view(GenesisRam& ram, std::size_t slot);
ConstActorView actor_view(const GenesisRam& ram, std::size_t slot);

std::uint8_t actor_read8(ConstActorView actor, std::size_t offset);
std::uint16_t actor_read16(ConstActorView actor, std::size_t offset);
std::uint32_t actor_read32(ConstActorView actor, std::size_t offset);
std::uint8_t actor_read8(ActorView actor, std::size_t offset);
std::uint16_t actor_read16(ActorView actor, std::size_t offset);
std::uint32_t actor_read32(ActorView actor, std::size_t offset);
void actor_write8(ActorView actor, std::size_t offset, std::uint8_t value);
void actor_write16(ActorView actor, std::size_t offset, std::uint16_t value);
void actor_write32(ActorView actor, std::size_t offset, std::uint32_t value);

}  // namespace openaladdin::core
