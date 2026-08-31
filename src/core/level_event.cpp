#include "core/level_event.hpp"

#include "core/actor.hpp"
#include "core/interaction.hpp"
#include "core/ram.hpp"
#include "core/rom.hpp"
#include "core/trace.hpp"

#include <optional>

namespace openaladdin::core {
namespace {

constexpr std::size_t kLevelEventCommandDispatchTable = 0x000020C0;
constexpr std::size_t kLevelEventRecordSize = 6;

constexpr RamAddress kHandlerType05 = 0x001B7634;
constexpr RamAddress kHandlerSound5D = 0x001B7840;
constexpr RamAddress kHandlerType7D = 0x001B766C;
constexpr RamAddress kHandlerType7C = 0x001B760A;
constexpr RamAddress kHandlerPlayerPresentation = 0x001B781A;
constexpr RamAddress kHandlerType2FMovement = 0x001B76D4;
constexpr RamAddress kHandlerType20 = 0x001B77F0;
constexpr RamAddress kHandlerType40 = 0x001B7758;
constexpr RamAddress kHandlerType3AMovement = 0x001B77A4;
constexpr RamAddress kHandlerType2A = 0x001B7738;
constexpr RamAddress kHandlerType46ModeReady = 0x001B7706;
constexpr RamAddress kHandlerType2F = 0x001B76AA;
constexpr RamAddress kHandlerSceneTransition = 0x001B5B3A;
constexpr RamAddress kHandlerPresentationState = 0x001B5B32;
constexpr RamAddress kHandlerType84VariantA = 0x001B5AAA;
constexpr RamAddress kHandlerType3AFromCounter = 0x001B58DA;
constexpr RamAddress kHandlerType34 = 0x001B592A;
constexpr RamAddress kHandlerType54Pair = 0x001B5976;
constexpr RamAddress kHandlerType46SceneReady = 0x001B59DE;
constexpr RamAddress kHandlerType84Pair = 0x001B5A4C;
constexpr RamAddress kHandlerType84VariantB = 0x001B5AD2;
constexpr RamAddress kHandlerType84VariantC = 0x001B5B02;
constexpr RamAddress kHandlerNoOp = 0x001B58D8;
constexpr RamAddress kHandlerType42 = 0x001B5A1A;

constexpr RamAddress kTemplateType05 = 0x001B7A94;
constexpr RamAddress kTemplateType7D = 0x001B81D8;
constexpr RamAddress kTemplateType7C = 0x001B819C;
constexpr RamAddress kTemplateType2F = 0x001B7BD4;
constexpr RamAddress kTemplateType20 = 0x001B7C10;
constexpr RamAddress kTemplateBaseZero = 0x001B79B8;
constexpr RamAddress kTemplateType2A = 0x001B7AF8;
constexpr RamAddress kTemplateType46 = 0x001B79CC;
constexpr RamAddress kTemplateType84LevelEntry = 0x001B8138;
constexpr RamAddress kTemplateType42 = 0x001B814C;
constexpr RamAddress kTemplateType54 = 0x001B8160;

constexpr RamAddress kAnimationPlayerLevelEvent = 0x001258D2;
constexpr RamAddress kAnimationType3A = 0x00122BD8;
constexpr RamAddress kAnimationType34 = 0x00122C1E;
constexpr RamAddress kAnimationType40 = 0x00122C12;
constexpr RamAddress kAnimationType84A = 0x0012585C;
constexpr RamAddress kAnimationType84B = 0x00125864;
constexpr RamAddress kAnimationType84C = 0x0012586C;
constexpr RamAddress kMovementType2F = 0x00121082;
constexpr RamAddress kMovementType3A40 = 0x00121034;
constexpr RamAddress kMovementType84A = 0x00120FA0;
constexpr RamAddress kMovementType84B = 0x00120F8A;
constexpr RamAddress kMovementType84VariantB = 0x00120FB4;
constexpr RamAddress kMovementType46 = 0x00120FFE;
constexpr RamAddress kMovementType34 = 0x001217B4;

struct EventActorSpec {
    ActorAllocationPool pool = ActorAllocationPool::CommonForward;
    RamAddress template_address = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint32_t animation = 0;
    std::uint32_t movement = 0;
    std::uint8_t type = 0;
    bool override_type = false;
    std::uint8_t movement_flags = 0;
    bool override_movement_flags = false;
    std::uint8_t resource_count = 0;
    bool override_resource_count = false;
    std::uint16_t sprite_attribute = 0;
    bool override_sprite_attribute = false;
    bool face_from_x = false;
};

std::optional<std::size_t> spawn_event_actor(
    CoreRuntime& core,
    const EventActorSpec& spec
) {
    const auto slot = actor_find_free_slot(core.ram, spec.pool);
    if (!slot || !actor_initialize_from_template(
            core, *slot, spec.template_address)) {
        return std::nullopt;
    }

    const ActorView actor = actor_view(core.ram, *slot);
    actor_write16(actor, kActorXOffset, spec.x);
    actor_write16(actor, kActorYOffset, spec.y);
    if (spec.animation != 0) {
        actor_write32(actor, kActorAnimationPcOffset, spec.animation);
    }
    if (spec.movement != 0) {
        actor_write32(actor, kActorMovementPcOffset, spec.movement);
    }
    if (spec.override_type) {
        actor_write8(actor, kActorTypeOffset, spec.type);
    }
    if (spec.override_movement_flags) {
        actor_write8(actor, kActorMovementFlagsOffset, spec.movement_flags);
    }
    if (spec.override_resource_count) {
        actor_write8(actor, kActorResourceCountOffset, spec.resource_count);
    }
    if (spec.override_sprite_attribute) {
        actor_write16(actor, 0x1E, spec.sprite_attribute);
    }
    if (spec.face_from_x) {
        actor_write8(actor, kActorFacingXOffset,
                     spec.x > 0x008CU ? 0xFF : 0);
    }
    return slot;
}

std::uint16_t event_relative_x(const GenesisRam& ram) {
    return static_cast<std::uint16_t>(
        read16(ram, kWorldCameraX) + 0x0140U);
}

std::uint16_t event_relative_y(
    const GenesisRam& ram,
    std::uint16_t event_y
) {
    return static_cast<std::uint16_t>(
        read16(ram, kWorldCameraY) + event_y + 0x0100U);
}

void execute_event_handler(
    CoreRuntime& core,
    const LevelEventDispatchResult& event,
    std::optional<std::uint8_t> event_byte = std::nullopt
) {
    const std::uint16_t direct_x = event.arg0;
    const std::uint16_t direct_y = event.arg1;
    const std::uint16_t relative_x = event_relative_x(core.ram);

    switch (event.handler) {
    case kHandlerType05:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType05,
            direct_x, direct_y, 0, 0, 0, false, 0, false,
            0, false, 0, false, true});
        break;
    case kHandlerType7D:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::GameplayForward, kTemplateType7D,
            direct_x, direct_y, 0, 0, 0, false, 0, false,
            0, false, 0x4000, true, true});
        break;
    case kHandlerType7C:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType7C,
            direct_x, direct_y, 0, 0, 0, false, 0, false,
            0, false, 0, false, true});
        break;
    case kHandlerPlayerPresentation:
        write8(core.ram, kPlayerInteractionAnimationGate, 0xFF);
        actor_write32(actor_view(core.ram, 0), kActorAnimationPcOffset,
                      kAnimationPlayerLevelEvent);
        actor_write8(actor_view(core.ram, 0), kActorAnimationTimerOffset, 0);
        write8(core.ram, kSceneScriptCountdown, 0xC8);
        write8(core.ram, kScenePresentationLatch, 0xFF);
        break;
    case kHandlerType2FMovement:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType2F,
            direct_x, direct_y, 0, kMovementType2F, 0, false, 0, false,
            0, false, 0, false, true});
        break;
    case kHandlerType20:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType20,
            direct_x, direct_y, 0, 0, 0, false, 0, false,
            0, false, 0, false, true});
        break;
    case kHandlerType40:
        if (read16(core.ram, kInteractionCounterDigits) != 0x3939) {
            (void)spawn_event_actor(core, EventActorSpec{
                ActorAllocationPool::CommonForward, kTemplateBaseZero,
                direct_x, direct_y, kAnimationType40, kMovementType3A40,
                0x40, true, 1, true, 0, true, 0, false, false});
        }
        break;
    case kHandlerType3AMovement:
        if (read16(core.ram, kInteractionCounterSecondaryDigits) != 0x3939) {
            (void)spawn_event_actor(core, EventActorSpec{
                ActorAllocationPool::CommonForward, kTemplateBaseZero,
                direct_x, direct_y, kAnimationType3A, kMovementType3A40,
                0x3A, true, 1, true, 1, true, 0, false, false});
        }
        break;
    case kHandlerType2A:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType2A,
            direct_x, direct_y});
        break;
    case kHandlerType46ModeReady:
        if (read8(core.ram, kGameDifficultyCounter) != '9') {
            (void)spawn_event_actor(core, EventActorSpec{
                ActorAllocationPool::CommonForward, kTemplateType46,
                direct_x, direct_y, 0, kMovementType46});
        }
        break;
    case kHandlerType2F:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType2F,
            direct_x, direct_y, 0, 0, 0, false, 0, false,
            0, false, 0, false, true});
        break;
    case kHandlerSceneTransition:
        write8(core.ram, kSceneScriptCountdown, 1);
        write8(core.ram, kSceneScriptWait, 0xFF);
        break;
    case kHandlerType84VariantA:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::GameplayForward, kTemplateType84LevelEntry,
            0x018E, 0x0164, kAnimationType84C});
        break;
    case kHandlerType3AFromCounter:
        if (event_byte
            && read16(core.ram, kInteractionCounterSecondaryDigits) != 0x3939) {
            (void)spawn_event_actor(core, EventActorSpec{
                ActorAllocationPool::CommonForward, kTemplateBaseZero,
                relative_x, event_relative_y(core.ram, *event_byte),
                kAnimationType3A, 0,
                0x3A, true, 0, false, 1, true, 0, false, true});
        }
        break;
    case kHandlerType34:
        if (!event_byte) break;
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateBaseZero,
            relative_x, event_relative_y(core.ram, *event_byte),
            kAnimationType34, kMovementType34,
            0x34, true, 6, true, 6, true, 0, false, true});
        break;
    case kHandlerType54Pair:
        if (!event_byte) break;
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType54,
            relative_x, event_relative_y(core.ram, *event_byte)});
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType54,
            static_cast<std::uint16_t>(relative_x + 0x20U),
            event_relative_y(core.ram, *event_byte)});
        break;
    case kHandlerType46SceneReady:
        if (event_byte && read8(core.ram, kGameDifficultyCounter) != '9') {
            (void)spawn_event_actor(core, EventActorSpec{
                ActorAllocationPool::CommonForward, kTemplateType46,
                relative_x, event_relative_y(core.ram, *event_byte)});
        }
        break;
    case kHandlerType84Pair:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::GameplayForward, kTemplateType84LevelEntry,
            0x018E, 0x0164, kAnimationType84A, kMovementType84A});
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::GameplayForward, kTemplateType84LevelEntry,
            0x018E, 0x0178, kAnimationType84B, kMovementType84B});
        break;
    case kHandlerType84VariantB:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::GameplayForward, kTemplateType84LevelEntry,
            0x018E, 0x0164, kAnimationType84A, kMovementType84VariantB});
        break;
    case kHandlerType84VariantC:
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::GameplayForward, kTemplateType84LevelEntry,
            0x018E, 0x0164, kAnimationType84B, kMovementType84B});
        break;
    case kHandlerType42:
        if (!event_byte) break;
        (void)spawn_event_actor(core, EventActorSpec{
            ActorAllocationPool::CommonForward, kTemplateType42,
            relative_x, event_relative_y(core.ram, *event_byte)});
        break;
    case kHandlerPresentationState:
        if (event_byte) write8(core.ram, kLevelEventPresentationState, *event_byte);
        break;
    case kHandlerSound5D:
    case kHandlerNoOp:
        // Audio and direct RTS entries are host/no-op boundaries.
        break;
    default:
        break;
    }
}

}  // namespace

LevelEventDispatchResult level_event_dispatch_timed_command(
    CoreRuntime& core,
    CoreTrace* trace
) {
    LevelEventDispatchResult result;
    const RamAddress cursor = read32(core.ram, kLevelEventScriptCursor);
    const std::uint8_t delay = rom_read8(core.rom, cursor);
    if (delay == 0) return result;

    const std::uint8_t tick = static_cast<std::uint8_t>(
        read8(core.ram, kLevelEventTick) + 1U);
    write8(core.ram, kLevelEventTick, tick);
    if (delay >= tick) return result;

    result.dispatched = true;
    result.command = rom_read8(core.rom, cursor + 1);
    result.arg0 = rom_read16(core.rom, cursor + 2);
    result.arg1 = rom_read16(core.rom, cursor + 4);
    write32(core.ram, kLevelEventScriptCursor,
            cursor + kLevelEventRecordSize);
    write8(core.ram, kLevelEventTick, 0);

    // The ROM adds 0x1A to the command byte in an 8-bit register before
    // indexing the 26-entry table. E6 therefore selects entry zero.
    const std::uint8_t table_index = static_cast<std::uint8_t>(
        result.command + 0x1AU);
    result.handler = rom_read32(
        core.rom,
        kLevelEventCommandDispatchTable
            + static_cast<std::size_t>(table_index) * 4);

    execute_event_handler(core, result);

    if (trace != nullptr) {
        trace->level_event_dispatched = true;
        trace->level_event_command = result.command;
        trace->level_event_arg0 = result.arg0;
        trace->level_event_arg1 = result.arg1;
        trace->level_event_handler = result.handler;
    }
    return result;
}

LevelEventDispatchResult level08_event_dispatch_command(
    CoreRuntime& core,
    CoreTrace* trace
) {
    LevelEventDispatchResult result;
    const RamAddress cursor = read32(core.ram, kLevel08EventCommandCursor);

    // Level08_EnterRoutine owns a separate two-byte stream. It advances the
    // cursor before entering the shared handler, and the second byte arrives
    // in D6 rather than in either of the timed dispatcher's payload words.
    result.dispatched = true;
    result.command = rom_read8(core.rom, cursor);
    result.arg1 = rom_read8(core.rom, cursor + 1);
    write32(core.ram, kLevel08EventCommandCursor, cursor + 2);

    const std::uint8_t table_index = static_cast<std::uint8_t>(
        result.command + 0x1AU);
    result.handler = rom_read32(
        core.rom,
        kLevelEventCommandDispatchTable
            + static_cast<std::size_t>(table_index) * 4);

    execute_event_handler(core, result, static_cast<std::uint8_t>(result.arg1));

    if (trace != nullptr) {
        trace->level_event_dispatched = true;
        trace->level_event_command = result.command;
        trace->level_event_arg0 = result.arg0;
        trace->level_event_arg1 = result.arg1;
        trace->level_event_handler = result.handler;
    }
    return result;
}

}  // namespace openaladdin::core
