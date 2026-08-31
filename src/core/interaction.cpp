#include "core/interaction.hpp"

#include "core/trace.hpp"

namespace openaladdin::core {
namespace {

constexpr RamAddress kInteractionHandlerType20 = 0x001B6EB2;
constexpr RamAddress kInteractionHandlerType21 = 0x001B6ED0;
constexpr RamAddress kInteractionHandlerType22 = 0x001B6EEE;

constexpr RamAddress kTemplateType20 = 0x001B7C10;
constexpr RamAddress kTemplateType1E = 0x001B7C24;
constexpr RamAddress kTemplateType1F = 0x001B7C38;

constexpr RamAddress kAnimationType20 = 0x0012337A;
constexpr RamAddress kAnimationType21 = 0x001235AC;
constexpr RamAddress kAnimationType22 = 0x001238B2;

std::uint16_t add_words(std::uint16_t first, std::uint16_t second) {
    return static_cast<std::uint16_t>(first + second);
}

std::optional<std::size_t> interaction_allocate(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t interaction_index,
    std::uint8_t selector,
    bool consume_row
) {
    const auto slot = actor_find_free_slot(core.ram, pool);
    if (!slot || !actor_initialize_from_template(
            core, *slot, template_address)) {
        return std::nullopt;
    }

    const ActorView actor = actor_view(core.ram, *slot);
    actor_write16(actor, 0x32, interaction_index);
    actor_write8(actor, 0x34, selector);
    actor_write16(
        actor,
        kActorXOffset,
        add_words(read16(core.ram, kInteractionHandlerX),
                  read16(core.ram, kInteractionSpawnXOffset)));
    actor_write16(
        actor,
        kActorYOffset,
        add_words(read16(core.ram, kInteractionHandlerY),
                  read16(core.ram, kInteractionSpawnYOffset)));

    if (consume_row) {
        const RamAddress row = read32(core.ram, kInteractionRowPointer);
        if (is_work_ram_address(row)
            && interaction_index <= kWorkRamLast - row) {
            write8(
                core.ram,
                row + static_cast<RamAddress>(interaction_index),
                0);
        }
    }
    return slot;
}

bool set_type_and_animation(
    CoreRuntime& core,
    std::size_t slot,
    std::uint8_t type,
    RamAddress animation
) {
    if (!is_actor_slot(slot)) return false;
    const ActorView actor = actor_view(core.ram, slot);
    actor_write8(actor, kActorTypeOffset, type);
    actor_write32(actor, kActorAnimationPcOffset, animation);
    return true;
}

}  // namespace

InteractionDispatch interaction_dispatch(
    const CoreRuntime& core,
    std::uint8_t selector
) {
    InteractionDispatch dispatch;
    dispatch.selector = selector;
    dispatch.in_range = selector < kInteractionHandlerCount;
    if (!dispatch.in_range || !rom_is_bound(core.rom)) return dispatch;
    dispatch.handler = rom_read32(
        core.rom,
        kInteractionHandlerTable
            + static_cast<RamAddress>(selector) * 4);
    dispatch.no_op = dispatch.handler == 0;
    return dispatch;
}

std::optional<std::size_t> interaction_allocate_and_consume_row(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t interaction_index,
    std::uint8_t selector
) {
    return interaction_allocate(
        core, pool, template_address, interaction_index, selector, true);
}

std::optional<std::size_t> interaction_allocate_preserve_row(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t interaction_index,
    std::uint8_t selector
) {
    return interaction_allocate(
        core, pool, template_address, interaction_index, selector, false);
}

InteractionSpawnResult interaction_spawn_dispatch(
    CoreRuntime& core,
    std::uint16_t interaction_index,
    std::uint8_t selector,
    CoreTrace* trace
) {
    InteractionSpawnResult result;
    result.dispatch = interaction_dispatch(core, selector);
    if (trace != nullptr) {
        trace->interaction_selector = selector;
        trace->interaction_handler = result.dispatch.handler;
        trace->interaction_index = interaction_index;
        trace->interaction_spawn_slot = 0;
    }
    if (!result.dispatch.in_range || result.dispatch.no_op) return result;

    if (result.dispatch.handler == kInteractionHandlerType20) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonForward, kTemplateType20,
            interaction_index, selector);
        if (result.actor_slot) {
            set_type_and_animation(
                core, *result.actor_slot, 0x20, kAnimationType20);
        }
    } else if (result.dispatch.handler == kInteractionHandlerType21) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonForward, kTemplateType1E,
            interaction_index, selector);
        if (result.actor_slot) {
            set_type_and_animation(
                core, *result.actor_slot, 0x21, kAnimationType21);
        }
    } else if (result.dispatch.handler == kInteractionHandlerType22) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonForward, kTemplateType1F,
            interaction_index, selector);
        if (result.actor_slot) {
            const ActorView actor = actor_view(core.ram, *result.actor_slot);
            actor_write32(actor, kActorMovementPcOffset, 0);
            set_type_and_animation(
                core, *result.actor_slot, 0x22, kAnimationType22);
        }
    }

    result.handler_applied = result.actor_slot.has_value();
    if (trace != nullptr && result.actor_slot) {
        trace->interaction_spawn_slot = *result.actor_slot;
    }
    return result;
}

}  // namespace openaladdin::core
