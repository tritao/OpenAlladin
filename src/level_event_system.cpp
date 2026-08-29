#include "level_event_system.hpp"

#include "game_state.hpp"

namespace openaladdin {
namespace {

constexpr std::uint32_t kLevelEventTemplateType05 = 0x001B7A94;
constexpr std::uint32_t kLevelEventTemplateType2A = 0x001B7AF8;
constexpr std::uint32_t kLevelEventTemplateType2F = 0x001B7BD4;
constexpr std::uint32_t kLevelEventTemplateBaseZero = 0x001B79B8;
constexpr std::uint32_t kLevelEventTemplateType46 = 0x001B79CC;
constexpr std::uint32_t kLevelEventTemplateType7C = 0x001B819C;
constexpr std::uint32_t kLevelEventTemplateType7D = 0x001B81D8;
constexpr std::uint32_t kLevelEventTemplateType84 = 0x001B8138;
constexpr std::uint32_t kLevelEventTemplateType42 = 0x001B814C;
constexpr std::uint32_t kLevelEventTemplateType54 = 0x001B8160;
constexpr std::uint32_t kLevelEventTemplateUpperType20 = 0x001B7C10;
constexpr std::uint32_t kLevelEventAnimationType2F = 0x00123A68;
constexpr std::uint32_t kLevelEventAnimationType3A = 0x00122BD8;
constexpr std::uint32_t kLevelEventAnimationType34 = 0x00122C1E;
constexpr std::uint32_t kLevelEventAnimationType40 = 0x00122C12;
constexpr std::uint32_t kLevelEventAnimationType84A = 0x0012585C;
constexpr std::uint32_t kLevelEventAnimationType84B = 0x00125864;
constexpr std::uint32_t kLevelEventAnimationType84C = 0x0012586C;
constexpr std::uint32_t kLevelEventMovementType2F = 0x00121082;
constexpr std::uint32_t kLevelEventMovementType3A = 0x00121034;
constexpr std::uint32_t kLevelEventMovementType40 = 0x00121034;
constexpr std::uint32_t kLevelEventMovementType84A = 0x00120FA0;
constexpr std::uint32_t kLevelEventMovementType84B = 0x00120F8A;
constexpr std::uint32_t kLevelEventMovementType84C = 0x00120FB4;
constexpr std::uint32_t kLevelEventMovementType46 = 0x00120FFE;

}  // namespace

bool LevelEventSystem::spawn_actor(
    GameState& state,
    std::uint32_t template_address,
    std::uint16_t x,
    std::uint16_t y,
    std::uint32_t animation_override,
    std::uint32_t movement_override,
    bool override_type,
    std::uint8_t type,
    bool override_movement_flags,
    std::uint8_t movement_flags,
    bool override_sprite_attribute,
    std::uint16_t sprite_attribute,
    bool set_facing_from_x
) {
    const auto slot = actor_lifecycle_.allocate(ActorPool::CommonForward);
    if (!slot) return false;

    ActorState destination = state.actors[*slot];
    destination.x = x;
    destination.y = y;
    ActorState actor = actor_lifecycle_.initialize_record(destination, template_address);
    actor.x = x;
    actor.y = y;
    if (animation_override != 0) actor.animation_pc = animation_override;
    if (movement_override != 0) actor.movement_pc = movement_override;
    if (override_type) actor.type = type;
    if (override_movement_flags) actor.movement_flags = movement_flags;
    if (override_sprite_attribute) actor.sprite_attribute = sprite_attribute;
    if (set_facing_from_x) {
        actor.facing_x_flip = x >= 0x8D ? 0xFF : 0;
    }
    if (!actor_lifecycle_.install(*slot, actor)) return false;
    animation_system_.actors().reset(*slot);
    return true;
}

LevelEventEffects LevelEventSystem::dispatch(
    GameState& state,
    const LevelEventCommand& event
) {
    LevelEventEffects effects;

    // The command handlers use D1/D2 as direct actor coordinates. The small
    // set of level-entry handlers that use D6 instead are relative to the
    // live player record; the callback does not publish another semantic
    // coordinate, so the native player world position is the corresponding
    // source here.
    const auto relative_x = [&state]() {
        return static_cast<std::uint16_t>(state.camera.x + state.player.x + 0x140);
    };
    const auto relative_y = [&state]() {
        return static_cast<std::uint16_t>(state.camera.y + state.player.y + 0x100);
    };
    const auto direct_x = event.arg0;
    const auto direct_y = event.arg1;
    const auto spawn_direct = [this, &state, direct_x, direct_y](
        std::uint32_t template_address,
        std::uint32_t animation_override = 0,
        std::uint32_t movement_override = 0,
        bool override_type = false,
        std::uint8_t type = 0,
        bool override_movement_flags = false,
        std::uint8_t movement_flags = 0,
        bool override_sprite_attribute = false,
        std::uint16_t sprite_attribute = 0,
        bool set_facing_from_x = false
    ) {
        return spawn_actor(
            state,
            template_address,
            direct_x,
            direct_y,
            animation_override,
            movement_override,
            override_type,
            type,
            override_movement_flags,
            movement_flags,
            override_sprite_attribute,
            sprite_attribute,
            set_facing_from_x
        );
    };

    switch (event.command) {
    case 0xE6: // LevelEvent_SpawnType05WithPaletteRefresh
        (void)spawn_direct(kLevelEventTemplateType05, 0, 0, false, 0, false, 0, false, 0, true);
        break;
    case 0xE7: // LevelEvent_QueueSound5D
        if (state.camera.vdp_update != 0) effects.sound_requests.push_back(0x5D);
        break;
    case 0xE8: // LevelEvent_SpawnType7DWithPaletteRefresh
        (void)spawn_direct(
            kLevelEventTemplateType7D,
            0,
            0,
            false,
            0,
            false,
            0,
            true,
            0x4000,
            true
        );
        break;
    case 0xE9: // LevelEvent_SpawnType7C
        (void)spawn_direct(kLevelEventTemplateType7C, 0, 0, false, 0, false, 0, false, 0, true);
        break;
    case 0xEA: // LevelEvent_ArmPlayerPresentation
        state.player.animation_selector.animation_gate = 0xFF;
        state.player.animation_selector.scene_script_countdown = 0xC8;
        state.scene.script_countdown = 0xC8;
        animation_system_.player().select_stream_entry(0x001258D2);
        animation_system_.player().clear_animation_timer_next_update();
        break;
    case 0xEB: // LevelEvent_SpawnType2FWithMovement
        (void)spawn_direct(
            kLevelEventTemplateType2F,
            0,
            kLevelEventMovementType2F,
            false,
            0,
            false,
            0,
            false,
            0,
            true
        );
        break;
    case 0xEC: // LevelEvent_SpawnType20
        (void)spawn_direct(kLevelEventTemplateUpperType20, 0, 0, false, 0, false, 0, false, 0, true);
        break;
    case 0xED: // LevelEvent_SpawnType3AWithMovement
        (void)spawn_direct(
            kLevelEventTemplateBaseZero,
            kLevelEventAnimationType3A,
            kLevelEventMovementType3A,
            true,
            0x3A,
            true,
            1
        );
        break;
    case 0xEE: // LevelEvent_SpawnType40
        (void)spawn_direct(
            kLevelEventTemplateBaseZero,
            kLevelEventAnimationType40,
            kLevelEventMovementType40,
            true,
            0x40,
            true,
            1
        );
        break;
    case 0xEF: // LevelEvent_SpawnType2A
        (void)spawn_direct(kLevelEventTemplateType2A);
        break;
    case 0xF0: // LevelEvent_SpawnType46WhenModeReady
        // The ROM gate is GAME_DIFFICULTY_COUNTER (FF7E3C), which is not
        // currently represented in GameState. Do not substitute the
        // unrelated scene-resource status byte here.
        (void)spawn_direct(kLevelEventTemplateType46, 0, kLevelEventMovementType46);
        break;
    case 0xF1: // LevelEvent_SpawnType2F
        (void)spawn_direct(kLevelEventTemplateType2F, 0, 0, false, 0, false, 0, false, 0, true);
        break;
    case 0xF2: // LevelEvent_ArmSceneScriptTransition
        state.player.animation_selector.scene_script_countdown = 1;
        state.scene.script_countdown = 1;
        break;
    case 0xF3: // LevelEvent_SetPresentationStateByte
        // FFF10B is not yet part of the typed state surface. Keep this
        // command decoded and owned by this boundary until its consumer is
        // extracted from the presentation path.
        break;
    case 0xF4: // LevelEvent_SpawnType84VariantA
        (void)spawn_actor(
            state,
            kLevelEventTemplateType84,
            relative_x(),
            relative_y(),
            kLevelEventAnimationType84C
        );
        break;
    case 0xF5: // LevelEvent_SpawnType3AFromCounter
        (void)spawn_actor(
            state,
            kLevelEventTemplateBaseZero,
            relative_x(),
            relative_y(),
            kLevelEventAnimationType3A,
            0,
            true,
            0x3A
        );
        break;
    case 0xF6: // LevelEvent_SpawnType34
        (void)spawn_actor(
            state,
            kLevelEventTemplateBaseZero,
            relative_x(),
            relative_y(),
            kLevelEventAnimationType34,
            0x001217B4,
            true,
            0x34,
            true,
            6
        );
        break;
    case 0xF7: // LevelEvent_SpawnType54Pair
        (void)spawn_actor(state, kLevelEventTemplateType54, relative_x(), relative_y());
        (void)spawn_actor(
            state,
            kLevelEventTemplateType54,
            static_cast<std::uint16_t>(relative_x() + 0x20),
            relative_y()
        );
        break;
    case 0xF8: // LevelEvent_SpawnType46WhenSceneReady
        (void)spawn_actor(
            state,
            kLevelEventTemplateType46,
            relative_x(),
            relative_y()
        );
        break;
    case 0xF9: // LevelEvent_SpawnType84Pair
        (void)spawn_actor(
            state,
            kLevelEventTemplateType84,
            0x018E,
            0x0164,
            kLevelEventAnimationType84A,
            kLevelEventMovementType84A
        );
        (void)spawn_actor(
            state,
            kLevelEventTemplateType84,
            0x018E,
            0x0178,
            kLevelEventAnimationType84B,
            kLevelEventMovementType84B
        );
        break;
    case 0xFA: // LevelEvent_SpawnType84VariantB
        (void)spawn_actor(
            state,
            kLevelEventTemplateType84,
            0x018E,
            0x0164,
            kLevelEventAnimationType84A
        );
        break;
    case 0xFB: // LevelEvent_SpawnType84VariantC
        (void)spawn_actor(
            state,
            kLevelEventTemplateType84,
            0x018E,
            0x0164,
            kLevelEventAnimationType84B,
            kLevelEventMovementType84B
        );
        break;
    case 0xFC:
    case 0xFD:
    case 0xFF:
        break;
    case 0xFE: // LevelEvent_SpawnType42
        (void)spawn_actor(state, kLevelEventTemplateType42, relative_x(), relative_y());
        break;
    default:
        // The ROM dispatch table is limited to E6..FF. Unknown commands are
        // intentionally ignored here after the VM has decoded the record;
        // the VM itself remains responsible for stream framing/faults.
        break;
    }

    return effects;
}

}  // namespace openaladdin
