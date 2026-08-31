#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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
constexpr RamAddress kActorRenderXOffset = 0x00FFF080;
constexpr RamAddress kActorRenderYOffset = 0x00FFF082;
constexpr RamAddress kCameraScrollRenderOffset = 0x00FFF09E;
constexpr RamAddress kGlobalPrngState = 0x00FF7DEA;
constexpr RamAddress kActorVmCommandContinuation = 0x00FF7D9A;
constexpr RamAddress kActorVmCursorClearContinuation = 0x00FF7D9E;
constexpr RamAddress kActorVmMovementPass = 0x00FF7DA2;
constexpr RamAddress kInteractionRowPointer = 0x00FF7DAC;
constexpr RamAddress kInteractionHandlerX = 0x00FF7DB0;
constexpr RamAddress kInteractionHandlerY = 0x00FF7DB2;
constexpr RamAddress kInteractionRowStride = 0x00FF7DB4;
constexpr RamAddress kLevelWidthTiles = 0x00FF7DB6;
constexpr RamAddress kLevelWidthPixels = 0x00FF7DB8;
constexpr RamAddress kLevelHeightTiles = 0x00FF7DBA;
constexpr RamAddress kLevelHeightPixels = 0x00FF7DBC;
constexpr RamAddress kLevelBackgroundBlockSource = 0x00FF7DBE;
constexpr RamAddress kLevelCameraScrollCallback = 0x00FF7DA4;
constexpr RamAddress kLevelFrameCallback = 0x00FF7DC2;
constexpr RamAddress kTerrainRowPointerTable = 0x00FF9884;
constexpr RamAddress kPlayerTerrainQueryResult = 0x00FFF156;
constexpr RamAddress kTerrainQueryInputRaw = 0x00FFF155;
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
constexpr RamAddress kPlayerTerrainSurfaceMode = 0x00FFF0A4;
constexpr RamAddress kPlayerTerrainSurfaceLatch = 0x00FFF0C2;
constexpr RamAddress kPlayerTerrainPushDownState = 0x00FFF0DE;
constexpr RamAddress kPlayerTerrainPushUpState = 0x00FFF0DF;
constexpr RamAddress kPlayerActionResponseField = 0x00FFF0D8;
constexpr RamAddress kPlayerActionResponseStateB = 0x00FFF0D9;
constexpr RamAddress kPlayerActionAnimationState = 0x00FFF0DA;
constexpr RamAddress kPlayerInteractionMode = 0x00FFF0CD;
constexpr RamAddress kPlayerInteractionPending = 0x00FFEFFF;
constexpr RamAddress kPlayerInteractionAnimationGate = 0x00FFF0E7;
constexpr RamAddress kPlayerInteractionLock = 0x00FFF0F2;
constexpr RamAddress kPlayerTransitionLock = 0x00FFF0DB;
constexpr RamAddress kPlayerTransitionGate = 0x00FFF114;
constexpr RamAddress kPlayerTerminalTransition = 0x00FFF0E6;
constexpr RamAddress kPlayerCollisionResponseSuppress = 0x00FFF0F5;
constexpr RamAddress kPlayerCollisionCurrentActorType = 0x00FFF0F6;
constexpr RamAddress kActorCollisionEventFlag = 0x00FFF100;
constexpr RamAddress kPlayerTerrainResponsePassTimer = 0x00FFF0EE;
constexpr RamAddress kPlayerTerrainResponseLeft = 0x00FFF0EF;
constexpr RamAddress kPlayerTerrainResponseRight = 0x00FFF0F0;
constexpr RamAddress kPlayerInteractionMarker = 0x00FFF0D3;
constexpr RamAddress kPlayerInteractionResponse = 0x00FFF0D4;
constexpr RamAddress kPlayerTerrainBounceAnimationState = 0x00FFF0EB;
constexpr RamAddress kLevel08EventPhase = 0x00FFF084;
constexpr RamAddress kLevel08EventCounterHigh = 0x00FFF086;
constexpr RamAddress kLevel08EventCounterLow = 0x00FFF088;
constexpr RamAddress kLevel08VdpRecordOffset = 0x00FFF08A;
constexpr RamAddress kLevel08VdpScrollOffset = 0x00FFF0A2;
constexpr RamAddress kLevelTimer = 0x00FFF103;
constexpr RamAddress kSceneScriptCountdown = 0x00FFF0E9;
constexpr RamAddress kPlayerInteractionCounter = 0x00FFF003;
constexpr RamAddress kInteractionCounterDigits = 0x00FFEFE0;
constexpr RamAddress kInteractionCounterSecondaryDigits = 0x00FFEFE2;
constexpr RamAddress kLevelEventPresentationState = 0x00FFF10B;
constexpr RamAddress kSceneScriptWait = 0x00FFF005;
constexpr RamAddress kScenePresentationLatch = 0x00FFF570;
constexpr RamAddress kLevelEventTick = 0x00FFF10C;
constexpr RamAddress kLevel08EventCommandCursor = 0x00FFF12E;
constexpr RamAddress kLevelEventScriptCursor = 0x00FFF132;
constexpr RamAddress kPlayerInteractionType1ALatch = 0x00FFF10E;
constexpr RamAddress kPlayerInteractionType1BLatch = 0x00FFF10F;
constexpr RamAddress kPlayerInteractionType1CLatch = 0x00FFF110;
constexpr RamAddress kInteractionSpawnXOffset = 0x00FFF150;
constexpr RamAddress kInteractionSpawnYOffset = 0x00FFF152;
constexpr RamAddress kInteractionRuntimeTable = 0x00FFAE87;
constexpr RamAddress kInteractionResponseFlag = 0x00FFF104;
constexpr RamAddress kInteractionType1FSpawnGate = 0x00FFF105;
constexpr RamAddress kInteractionType1ESpawnGate = 0x00FFF107;
constexpr RamAddress kInteractionType5FReadyGate = 0x00FFF11C;
constexpr RamAddress kInteractionType12PresentationLatch = 0x00FFF123;
constexpr RamAddress kCameraSpecialMode = 0x00FFF173;
constexpr RamAddress kCameraUpdateDelay = 0x00FFF167;
constexpr RamAddress kCameraScrollApplyGate = 0x00FFF174;
constexpr RamAddress kTerrainStopLeftMotion = 0x00FFF0C5;
constexpr RamAddress kTerrainStopRightMotion = 0x00FFF0C8;
constexpr RamAddress kTerrainStopUpwardMotion = 0x00FFF0CB;
constexpr RamAddress kSceneScriptCursor = 0x00FFF572;
constexpr RamAddress kSceneScriptData = 0x00FFF576;
constexpr RamAddress kSceneTableIndex = 0x00FFF57A;
constexpr RamAddress kSceneScriptPending = 0x00FFF57C;
constexpr RamAddress kSceneVdpUpdateFlag = 0x00FFF57D;
constexpr RamAddress kSceneVdpClearFlag = 0x00FFF57E;
constexpr RamAddress kSceneTransitionEvent = 0x00FFF57F;
constexpr RamAddress kSceneResourceVdpStreamPtr = 0x00FFF140;
constexpr RamAddress kSceneResourceVdpStreamEnd = 0x00FFF148;
constexpr RamAddress kSceneResourceVdpStreamOffset = 0x00FFF14C;
constexpr RamAddress kVdpCommandAddressLatch = 0x00FF8880;
constexpr RamAddress kSceneResourceTileBase = 0x00FFEFF0;
constexpr RamAddress kSceneResourcePresentationScratch = 0x00FFEFFC;
constexpr RamAddress kSceneResourceActorRecordCursor = 0x00FF7282;
constexpr RamAddress kSceneResourceActorSpawnGate = 0x00FFF0FF;
constexpr RamAddress kSceneResourceC000Source = 0x00FF7294;
constexpr RamAddress kVdpTilePlaneOrder = 0x00FFF165;
constexpr RamAddress kSceneResourceMode = 0x00FFF15A;
constexpr RamAddress kSceneResourceActorResource = 0x00FF7286;
constexpr RamAddress kSceneResourceActorXAdvance = 0x00FF728A;

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
constexpr std::size_t kActorResourceCountOffset = 0x29;
constexpr std::size_t kActorResourcePointerOffset = 0x2A;
constexpr std::size_t kActorSpriteVramBaseOffset = 0x2E;
constexpr std::size_t kActorTerrainResponseOffset = 0x3D;
constexpr std::size_t kActorFacingYOffset = 0x35;
constexpr std::size_t kActorMovementCommandTimerOffset = 0x36;
constexpr std::size_t kActorAnimationTimerOffset = 0x37;
constexpr std::size_t kActorMovementReturnPcOffset = 0x38;
constexpr std::size_t kActorFlagsOffset = 0x3C;
constexpr std::size_t kActorLinkedRecordPointerOffset = 0x3E;

constexpr RamAddress kActorResourceBitmapBase = 0x00FFF008;
constexpr std::size_t kActorResourceBitmapSize = 0x74;
constexpr RamAddress kActorSpriteVramBaseTable = 0x0011F500;

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
std::optional<std::size_t> actor_slot_for_address(RamAddress address);

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
