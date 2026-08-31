#include "core/interaction.hpp"

#include "core/trace.hpp"

namespace openaladdin::core {
namespace {

constexpr RamAddress kInteractionHandlerType20 = 0x001B6EB2;
constexpr RamAddress kInteractionHandlerType21 = 0x001B6ED0;
constexpr RamAddress kInteractionHandlerType22 = 0x001B6EEE;
constexpr RamAddress kInteractionHandlerType84Offset = 0x001B65C0;
constexpr RamAddress kInteractionHandlerType84Terminal = 0x001B65D4;
constexpr RamAddress kInteractionHandlerType32 = 0x001B65E0;
constexpr RamAddress kInteractionHandlerType62Gate = 0x001B65F4;
constexpr RamAddress kInteractionHandlerType62Scene = 0x001B65FE;
constexpr RamAddress kInteractionHandlerType64 = 0x001B6622;
constexpr RamAddress kInteractionHandlerType1A = 0x001B6636;
constexpr RamAddress kInteractionHandlerType1B = 0x001B6654;
constexpr RamAddress kInteractionHandlerType1C = 0x001B6672;
constexpr RamAddress kInteractionHandlerType58 = 0x001B668A;
constexpr RamAddress kInteractionHandlerType58Alternate = 0x001B6696;
constexpr RamAddress kInteractionHandlerType55Y0 = 0x001B66AC;
constexpr RamAddress kInteractionHandlerType55Y4 = 0x001B66C0;
constexpr RamAddress kInteractionHandlerType55Y8 = 0x001B66D8;
constexpr RamAddress kInteractionHandlerType55Y12 = 0x001B66F2;
constexpr RamAddress kInteractionHandlerType74Offset = 0x001B670C;
constexpr RamAddress kInteractionHandlerType07 = 0x001B6726;
constexpr RamAddress kInteractionHandlerType8C = 0x001B6732;
constexpr RamAddress kInteractionHandlerType76 = 0x001B673E;
constexpr RamAddress kInteractionHandlerType74 = 0x001B674A;
constexpr RamAddress kInteractionHandlerType84Terrain = 0x001B6756;
constexpr RamAddress kInteractionHandlerType89Pair = 0x001B67C2;
constexpr RamAddress kInteractionHandlerType6A6B = 0x001B6802;
constexpr RamAddress kInteractionHandlerType69 = 0x001B681C;
constexpr RamAddress kInteractionHandlerType6C = 0x001B6836;
constexpr RamAddress kInteractionHandlerType23 = 0x001B6864;
constexpr RamAddress kInteractionHandlerType06 = 0x001B6870;
constexpr RamAddress kInteractionHandlerType2B = 0x001B688A;
constexpr RamAddress kInteractionHandlerType84Fd = 0x001B6896;
constexpr RamAddress kInteractionHandlerType84Fe = 0x001B68B0;
constexpr RamAddress kInteractionHandlerType84Response = 0x001B68CA;
constexpr RamAddress kInteractionHandlerType5F = 0x001B68D6;
constexpr RamAddress kInteractionHandlerType87 = 0x001B6F0C;
constexpr RamAddress kInteractionHandlerUpper12 = 0x001B6E7A;
constexpr RamAddress kInteractionHandlerUpper64 = 0x001B6E86;
constexpr RamAddress kInteractionHandlerUpper63 = 0x001B6E9C;
constexpr RamAddress kInteractionHandlerUpper10 = 0x001B6EA6;

constexpr RamAddress kTemplateType20 = 0x001B7C10;
constexpr RamAddress kTemplateType1E = 0x001B7C24;
constexpr RamAddress kTemplateType1F = 0x001B7C38;
constexpr RamAddress kTemplateType84Offset = 0x001B7D14;
constexpr RamAddress kTemplateType84Terminal = 0x001B82B4;
constexpr RamAddress kTemplateType32 = 0x001B7EF4;
constexpr RamAddress kTemplateType62Default = 0x001B7FD0;
constexpr RamAddress kTemplateType62Scene5 = 0x001B7FE4;
constexpr RamAddress kTemplateType64 = 0x001B7FBC;
constexpr RamAddress kTemplatePresentation = 0x001B7FF8;
constexpr RamAddress kTemplateType58 = 0x001B8084;
constexpr RamAddress kTemplateType55 = 0x001B7A1C;
constexpr RamAddress kTemplateType74 = 0x001B7E68;
constexpr RamAddress kTemplateType07 = 0x001B8070;
constexpr RamAddress kTemplateType8C = 0x001B7E2C;
constexpr RamAddress kTemplateType76 = 0x001B7DB4;
constexpr RamAddress kTemplateType84Terrain = 0x001B7DDC;
constexpr RamAddress kTemplateType84TerrainAlt = 0x001B7DF0;
constexpr RamAddress kTemplateType89 = 0x001B7DA0;
constexpr RamAddress kTemplateType6A6B = 0x001B7D8C;
constexpr RamAddress kTemplateType69 = 0x001B7D78;
constexpr RamAddress kTemplateType23 = 0x001B7AE4;
constexpr RamAddress kTemplateType06 = 0x001B7B34;
constexpr RamAddress kTemplateType2B = 0x001B7B0C;
constexpr RamAddress kTemplateType84FdFe = 0x001B8354;
constexpr RamAddress kTemplateType84Response = 0x001B7C9C;
constexpr RamAddress kTemplateType5F = 0x001B78B4;
constexpr RamAddress kTemplateType87 = 0x001B7C4C;

constexpr RamAddress kAnimationType20 = 0x0012337A;
constexpr RamAddress kAnimationType21 = 0x001235AC;
constexpr RamAddress kAnimationType22 = 0x001238B2;
constexpr RamAddress kAnimationType6C = 0x00124034;
constexpr RamAddress kAnimationType58Alternate = 0x00125348;

std::uint16_t add_words(std::uint16_t first, std::uint16_t second) {
    return static_cast<std::uint16_t>(first + second);
}

std::uint16_t add_signed_word(std::uint16_t value, int offset) {
    return static_cast<std::uint16_t>(
        static_cast<int>(value) + offset);
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
        // D2 indexes the runtime selector table, which is the A2 base used
        // by the ROM helper. The source map row at FF7DAC is only the input
        // to the processor; it is not the byte consumed by a spawn.
        if (interaction_index <= kWorkRamLast - kInteractionRuntimeTable) {
            write8(
                core.ram,
                kInteractionRuntimeTable
                    + static_cast<RamAddress>(interaction_index),
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

std::optional<std::size_t> spawn_with_post_adjustment(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t interaction_index,
    std::uint8_t selector,
    int x_offset,
    int y_offset,
    bool consume_row,
    bool set_type,
    std::uint8_t type,
    bool set_animation,
    RamAddress animation,
    bool set_facing
) {
    const auto slot = interaction_allocate(
        core, pool, template_address, interaction_index, selector,
        consume_row);
    if (!slot) return std::nullopt;

    const ActorView actor = actor_view(core.ram, *slot);
    actor_write16(actor, kActorXOffset,
                  add_signed_word(actor_read16(actor, kActorXOffset),
                                  x_offset));
    actor_write16(actor, kActorYOffset,
                  add_signed_word(actor_read16(actor, kActorYOffset),
                                  y_offset));
    if (set_type) actor_write8(actor, kActorTypeOffset, type);
    if (set_animation) {
        actor_write32(actor, kActorAnimationPcOffset, animation);
    }
    if (set_facing) actor_write8(actor, kActorFacingXOffset, 0xFF);
    return slot;
}

std::uint8_t random_step_low_byte(CoreRuntime& core) {
    const std::uint32_t state = read32(core.ram, kGlobalPrngState);
    const std::uint32_t next = state * 13U + 7U;
    write32(core.ram, kGlobalPrngState, next);
    return static_cast<std::uint8_t>(next);
}

void apply_random_spawn_variation(CoreRuntime& core, std::size_t slot) {
    if (!is_actor_slot(slot)) return;
    const ActorView actor = actor_view(core.ram, slot);
    const std::uint8_t first = random_step_low_byte(core);
    actor_write16(actor, kActorXOffset,
                  add_signed_word(actor_read16(actor, kActorXOffset),
                                  static_cast<int>(first & 7U) - 3));
    const std::uint8_t second = random_step_low_byte(core);
    if ((second & 1U) != 0) {
        actor_write32(actor, kActorAnimationPcOffset, 0x001241FC);
    }
    if ((second & 2U) != 0) {
        actor_write8(actor, kActorFacingXOffset, 0xFF);
    }
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
    } else if (result.dispatch.handler == kInteractionHandlerType84Offset) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::GameplayReverse, kTemplateType84Offset,
            interaction_index, selector, 0, 0x0A, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType84Terminal) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType84Terminal,
            interaction_index, selector, 0, 0, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType32) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType32,
            interaction_index, selector, 0, 0x08, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType62Gate
               || result.dispatch.handler == kInteractionHandlerType62Scene) {
        const bool scene_five = read8(core.ram, kSceneState) == 0x05;
        if (result.dispatch.handler == kInteractionHandlerType62Gate
            && read8(core.ram, kPlayerInteractionType1ALatch) == 0) {
            result.handler_applied = false;
        } else if ((result.dispatch.handler == kInteractionHandlerType62Gate
                    && scene_five)
                   || (result.dispatch.handler == kInteractionHandlerType62Scene
                       && !scene_five)) {
            const RamAddress template_address = scene_five
                ? kTemplateType62Scene5 : kTemplateType62Default;
            result.actor_slot = interaction_allocate_preserve_row(
                core, ActorAllocationPool::CommonForward, template_address,
                interaction_index, selector);
            if (result.actor_slot && scene_five) {
                const ActorView actor = actor_view(
                    core.ram, *result.actor_slot);
                actor_write16(actor, kActorYOffset,
                              add_signed_word(
                                  actor_read16(actor, kActorYOffset), -8));
            }
        }
    } else if (result.dispatch.handler == kInteractionHandlerType64) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType64,
            interaction_index, selector, 0x10, 0, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType1A) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplatePresentation,
            interaction_index, selector, 0x10, 0x10, true,
            true, 0x1A, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType1B) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplatePresentation,
            interaction_index, selector, 0, 0x10, true,
            true, 0x1B, false, 0, true);
    } else if (result.dispatch.handler == kInteractionHandlerType1C) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplatePresentation,
            interaction_index, selector, 0, 0x10, true,
            true, 0x1C, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType58
               || result.dispatch.handler == kInteractionHandlerType58Alternate) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::GameplayReverse, kTemplateType58,
            interaction_index, selector);
        if (result.actor_slot && result.dispatch.handler
                == kInteractionHandlerType58Alternate) {
            const ActorView actor = actor_view(core.ram, *result.actor_slot);
            actor_write32(actor, kActorAnimationPcOffset,
                          kAnimationType58Alternate);
        }
    } else if (result.dispatch.handler == kInteractionHandlerType55Y0
               || result.dispatch.handler == kInteractionHandlerType55Y4
               || result.dispatch.handler == kInteractionHandlerType55Y8
               || result.dispatch.handler == kInteractionHandlerType55Y12) {
        const int y_offset = result.dispatch.handler == kInteractionHandlerType55Y0
            ? 0 : result.dispatch.handler == kInteractionHandlerType55Y4
                ? 4 : result.dispatch.handler == kInteractionHandlerType55Y8
                    ? 8 : 0x0C;
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType55,
            interaction_index, selector, 8, y_offset, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType74Offset) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType74,
            interaction_index, selector, -8, 4, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType07) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonReverse, kTemplateType07,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType8C) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonReverse, kTemplateType8C,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType76) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonForward, kTemplateType76,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType74) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonReverse, kTemplateType74,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType84Terrain) {
        const bool terrain_mode = read8(core.ram, kSceneState) == 0x0B;
        if (!terrain_mode || read8(
                core.ram, kInteractionType12PresentationLatch) == 0) {
            const RamAddress template_address = terrain_mode
                ? kTemplateType84TerrainAlt : kTemplateType84Terrain;
            result.actor_slot = interaction_allocate_and_consume_row(
                core, ActorAllocationPool::GameplayReverse, template_address,
                interaction_index, selector);
        }
    } else if (result.dispatch.handler == kInteractionHandlerType89Pair) {
        const std::uint8_t first = random_step_low_byte(core);
        if (first < 200) {
            result.actor_slot = interaction_allocate_and_consume_row(
                core, ActorAllocationPool::GameplayReverse, kTemplateType89,
                interaction_index, selector);
            if (!result.actor_slot) {
                result.handler_applied = false;
                return result;
            }
            if (result.actor_slot) {
                apply_random_spawn_variation(core, *result.actor_slot);
            }
            const std::uint8_t second = random_step_low_byte(core);
            if (second >= 200) return result;
        }
        const auto second_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::GameplayReverse, kTemplateType89,
            interaction_index, selector);
        if (second_slot) {
            result.actor_slot = second_slot;
            const ActorView actor = actor_view(core.ram, *second_slot);
            actor_write16(actor, kActorYOffset,
                          add_signed_word(
                              actor_read16(actor, kActorYOffset), 8));
            actor_write8(actor, kActorFacingXOffset,
                         static_cast<std::uint8_t>(
                             actor_read8(actor, kActorFacingXOffset)
                                 ^ 0xFFU));
            apply_random_spawn_variation(core, *second_slot);
        } else {
            result.handler_applied = false;
            return result;
        }
    } else if (result.dispatch.handler == kInteractionHandlerType6A6B) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::GameplayReverse, kTemplateType6A6B,
            interaction_index, selector, 8, -1, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType69) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::GameplayReverse, kTemplateType69,
            interaction_index, selector, 8, -1, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType6C) {
        if (read8(core.ram, kActorCollisionEventFlag) == 0) {
            result.actor_slot = spawn_with_post_adjustment(
                core, ActorAllocationPool::GameplayReverse, kTemplateType69,
                interaction_index, selector, 0x0F, -1, true,
                true, 0x6C, true, kAnimationType6C, false);
        }
    } else if (result.dispatch.handler == kInteractionHandlerType23) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonReverse, kTemplateType23,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType06) {
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType06,
            interaction_index, selector, 9, 7, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType2B) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::GameplayReverse, kTemplateType2B,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType84Fd
               || result.dispatch.handler == kInteractionHandlerType84Fe) {
        const bool first_variant = result.dispatch.handler
            == kInteractionHandlerType84Fd;
        result.actor_slot = spawn_with_post_adjustment(
            core, ActorAllocationPool::CommonReverse, kTemplateType84FdFe,
            interaction_index, selector, first_variant ? 0x14 : 0x0B,
            first_variant ? -1 : 6, true,
            false, 0, false, 0, false);
    } else if (result.dispatch.handler == kInteractionHandlerType84Response) {
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::GameplayReverse,
            kTemplateType84Response, interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerType5F) {
        if (read8(core.ram, kInteractionType5FReadyGate) == 0) {
            result.actor_slot = spawn_with_post_adjustment(
                core, ActorAllocationPool::CommonReverse, kTemplateType5F,
                interaction_index, selector, 0x0D, -0x0C, true,
                false, 0, false, 0, false);
        }
    } else if (result.dispatch.handler == kInteractionHandlerType87) {
        write8(core.ram, kInteractionResponseFlag, 0);
        result.actor_slot = interaction_allocate_and_consume_row(
            core, ActorAllocationPool::CommonReverse, kTemplateType87,
            interaction_index, selector);
    } else if (result.dispatch.handler == kInteractionHandlerUpper12
               || result.dispatch.handler == kInteractionHandlerUpper10
               || result.dispatch.handler == kInteractionHandlerUpper63
               || result.dispatch.handler == kInteractionHandlerUpper64) {
        bool allowed = true;
        if (result.dispatch.handler == kInteractionHandlerUpper63) {
            allowed = read8(core.ram, kInteractionType1FSpawnGate) != 0;
        } else if (result.dispatch.handler == kInteractionHandlerUpper64) {
            allowed = read8(core.ram, kInteractionType1ESpawnGate) != 0;
        }
        if (allowed) {
            const RamAddress template_address =
                result.dispatch.handler == kInteractionHandlerUpper10
                    || result.dispatch.handler == kInteractionHandlerUpper63
                ? kTemplateType1F : kTemplateType20;
            result.actor_slot = interaction_allocate_and_consume_row(
                core, ActorAllocationPool::CommonForward, template_address,
                interaction_index, selector);
        }
    }

    result.handler_applied = result.actor_slot.has_value();
    if (trace != nullptr && result.actor_slot) {
        trace->interaction_spawn_slot = *result.actor_slot;
    }
    return result;
}

InteractionRowPassResult interaction_process_rows(
    CoreRuntime& core,
    InteractionRowProfile profile,
    CoreTrace* trace
) {
    InteractionRowPassResult result;
    const bool rows_a = profile == InteractionRowProfile::A
        || profile == InteractionRowProfile::ACore;
    const bool core_profile = profile == InteractionRowProfile::ACore
        || profile == InteractionRowProfile::BCore;
    const std::size_t row_count = rows_a ? 16 : 23;
    const RamAddress pointer = read32(core.ram, kInteractionRowPointer);
    const RamAddress pointer_step = rows_a
        ? static_cast<RamAddress>(read16(core.ram, kInteractionRowStride)) : 2;

    write16(core.ram, kInteractionSpawnXOffset,
            rows_a && !core_profile ? 0xFFF0 :
                (!rows_a ? 0xFFF0 : 0x0150));
    write16(core.ram, kInteractionSpawnYOffset,
            core_profile && !rows_a ? 0x01E0 : 0x00F0);
    const std::uint16_t reference_x = read16(core.ram, kCameraReferenceX);
    const std::uint16_t reference_y = read16(core.ram, kCameraReferenceY);
    const std::uint16_t aligned_x = reference_x & 0xFFF0U;
    const std::uint16_t aligned_y = reference_y & 0xFFF0U;
    write16(core.ram, kInteractionHandlerX, aligned_x);
    write16(core.ram, kInteractionHandlerY, aligned_y);

    std::uint16_t coordinate = rows_a ? aligned_y : aligned_x;
    for (std::size_t row = 0; row < row_count; ++row) {
        const RamAddress row_address = pointer
            + static_cast<RamAddress>(row * pointer_step);
        const std::uint16_t row_word = read16(core.ram, row_address);
        const std::uint16_t interaction_index = row_word >> 1;
        const std::uint8_t selector = read8(
            core.ram,
            kInteractionRuntimeTable
                + static_cast<RamAddress>(interaction_index));
        if (selector != 0) {
            ++result.selector_count;
            if (rows_a) {
                write16(core.ram, kInteractionHandlerY, coordinate);
            } else {
                write16(core.ram, kInteractionHandlerX, coordinate);
            }
            InteractionSpawnResult spawn = interaction_spawn_dispatch(
                core, interaction_index, selector, trace);
            if (spawn.handler_applied) ++result.spawn_count;
        }
        coordinate = static_cast<std::uint16_t>(coordinate + 0x10);
        ++result.rows_visited;
    }
    return result;
}

InteractionRowPassResult interaction_process_rows_a(
    CoreRuntime& core,
    CoreTrace* trace
) {
    return interaction_process_rows(core, InteractionRowProfile::A, trace);
}

InteractionRowPassResult interaction_process_rows_a_core(
    CoreRuntime& core,
    CoreTrace* trace
) {
    return interaction_process_rows(core, InteractionRowProfile::ACore, trace);
}

InteractionRowPassResult interaction_process_rows_b(
    CoreRuntime& core,
    CoreTrace* trace
) {
    return interaction_process_rows(core, InteractionRowProfile::B, trace);
}

InteractionRowPassResult interaction_process_rows_b_core(
    CoreRuntime& core,
    CoreTrace* trace
) {
    return interaction_process_rows(core, InteractionRowProfile::BCore, trace);
}

}  // namespace openaladdin::core
