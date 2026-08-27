# Level 01 transfer-capable mechanism graph

Recorded 2026-08-27 against ROM SHA-256
`8199d016f7bb88ea73b635dcc072c126b40f01c662707ed3f67d865fd86c0ab6`.

## Result

The only remaining *specific, Level 01-present, statically connected, actor-driven launch candidate* is:

```text
Level 01 selector 0x87
  -> interaction handler 0x001B74D6
  -> allocator 0x001B525E
  -> template 0x001B7A30 (actor type 0x01)
  -> actor collision table 0x001CBE
  -> 0x001AFD84
  -> PLAYER_Y = actor_y - WORLD_CAMERA_Y
     PLAYER_VY = -0x0800
```

It has not been observed naturally in MAME: the natural replay has not reached the selector-`0x87` handler or collided with the resulting Type-`0x01` actor. This is therefore a focused runtime candidate, not evidence that the actor is the exit mechanism.

The strongest raw ROM capability is instead the player movement VM at `0x001ADE36`, which can add signed movement-stream deltas directly to `PLAYER_Y`. Level 01's player actor is Type `0x83`, but its `movement_pc` is zero and no natural Level 01 resource chain installs a nonzero movement stream. It is dormant capacity, not a live candidate.

The direct terrain launch families are structurally eliminated for Level 01: the decoded Level 01 terrain contains no behavior `0x27`, `0x29`, or `0x2D` cells, and the dynamic terrain/resource paths do not install them at runtime. The exit predicate and scene loader are downstream placement machinery; they become relevant only after the player reaches the boundary corridor.

## Graph

```text
scene state / Level 01 table
  0x001AA484 Level_LoadFromSceneState
    -> 0x001B3434 / 0x001B3818 terrain behavior resource -> 0x00FFAE84
    -> Level 01 map / row pointers -> 0x001B1E38 Terrain_ResolvePlayerCell
      -> 0x004554 terrain handler table
        -> 0x22/0x23: 0x001B54D8 -> query state -> 0x001A986E connector step
        -> 0x24:     0x001B54D2 -> query state -> 0x001A986E connector step
        -> 0x25:     0x001B54E0 -> terrain state -> local response/landing
        -> 0x27:     0x001B54A6 -> PLAYER_Y -= 0x50       [absent in Level 01]
        -> 0x29:     0x001B557E -> PLAYER_VY = -0x500     [absent]
        -> 0x2D:     0x001B56B6 -> PLAYER_VY = +0x200     [absent]

 interaction rows -> 0x00FFAE87 -> row processor / allocator
   selector 0x87 -> 0x001B74D6 -> 0x001B525E
     -> template 0x001B7A30 / actor type 0x01
       -> collision table 0x001CBE -> 0x001AFD84
         -> actor-relative PLAYER_Y and PLAYER_VY=-0x800 [live candidate]
   selector 0x74 -> 0x001B670C -> template 0x001B7E54 / type 0x65
     -> 0x001AFBF4 -> PLAYER_VY=-0x500 [observed local bounce]
   handhold actor type 0x69/0x6A -> 0x001AF978
     -> local PLAYER_Y alignment / type 0x6B [observed handhold]

 player actor type 0x83 + movement_pc != 0
   -> 0x001ADE36 MovementVM_TickActors
     -> signed stream delta -> PLAYER_X / PLAYER_Y [dormant Level 01 capacity]

 Level 01 boundary
   -> 0x001B5B4A: PLAYER_WORLD_X > 0x1287 && PLAYER_WORLD_Y < 0x01D6
     -> SCENE_SCRIPT_COUNTDOWN=0xFF
     -> 0x001A8E3E SceneScript_AdvanceState
     -> 0x001AA484 Level_LoadFromSceneState
       -> table-selected initial PLAYER_X/PLAYER_Y [scripted placement]
   -> 0x001B6406 exit callback / common transition helper
```

## Direct writer census

### Ordinary integration and jump

| Writer | Effect | Classification |
|---|---|---|
| `0x001A9716` `Player_HandleJumpAndVerticalState` | Normal `PLAYER_VY=-0x0200`; terrain-state branch can set `+0x0400`; active jump changes VY by `-0x006C` | Ordinary jump arc |
| `0x001A9B90` `Player_IntegrateMotion` | Integrates the signed high byte of VY into `PLAYER_Y`; acceleration is `±0x003C` | Ordinary integration |
| `0x001A9C22`, `0x001A9C54` | The two `PLAYER_Y += high-byte(VY)` integration sites | Ordinary integration |
| `0x001A9C28`, `0x001A9C5A`, `0x001A9764` | VY acceleration/active-jump updates | Ordinary integration |

These routines are naturally active and observed, but they cannot create a discontinuity by themselves.

### Connector stepping and interaction state

| Writer/consumer | Active state | Result |
|---|---|---|
| `0x001B54D8` | Terrain behavior `0x22/0x23` | Raises connector query state A/B |
| `0x001B54D2` | Terrain behavior `0x24` | Raises connector query state A/B at an endpoint |
| `0x001A986E` | Query state A; vertical stop/response gates clear | `PLAYER_Y += 2` downward, or `PLAYER_Y -= 1/-2` with Up |
| `0x001A99F0` / `0x001A9D98` | Terrain response state/landing flags | Local stop, landing, or response bookkeeping |
| `0x001AE3FC`, `0x001AE47E`, `0x001AE406`, `0x001AE488` | Runtime interaction table at `0x00FFAE87` | Select/allocate actors; no direct player transfer |

The Level 01 rope/connector cells and handhold state are present and naturally observed. Their observed upward effect is only connector stepping or ordinary dismount jumping.

### Local terrain snap/contact

The direct `PLAYER_Y` sites at `0x001A9B80`, `0x001AD94E`, `0x001AD960`, `0x001ADDF4`, and `0x001ADE02` are contour/contact helpers. `0x001AD7B4` (`Player_TerrainLandingResolver`) aligns against the decoded contour resource and zeros VY on contact. Terrain handlers `0x001B5502` (stop/align), `0x001B55E8` (grid snap), and `0x001B53F6` (cadenced landing latch) are likewise local. They require proximity/contact windows and do not move the player across the missing vertical gap.

### Discontinuous terrain transfer

| Terrain behavior | Exact routine | Direct effect | Level 01 status |
|---|---|---|---|
| `0x27` | `0x001B54A6` | `PLAYER_Y -= 0x50`, animation `0x001223D0`, interaction-animation gate set | Zero decoded cells; no dynamic installer observed |
| `0x29` | `0x001B557E` | On inactive response: `PLAYER_VX=-0x400`, `PLAYER_VY=-0x500`, response active | Zero decoded cells; no dynamic installer observed |
| `0x2D` | `0x001B56B6` | `PLAYER_VX=-0x400`, `PLAYER_VY=+0x200`, bounce animation | Zero decoded cells; no dynamic installer observed |

Other direct terrain VY sites are not discontinuous: `0x001B537A` behavior `0x30` applies a small landing response (`VY-=0x7C`), and `0x001B5574` behavior `0x2B` applies a local fallback (`VY+=0x78`). Level 01 has no behavior `0x30`, and its behavior `0x2B` cells are ordinary stop/align contacts.

### Actor-driven launch and placement

| Actor state | Exact routine | Required state/guard | Producer and Level 01 result |
|---|---|---|---|
| Type `0x01` | `0x001AFD84` | `PLAYER_VY >= 0`, interaction-animation gate clear, `abs(PLAYER_Y-(actor_y-cameraY)) < 6` | Selector `0x87` -> Type `0x01` is present and statically connected. Not naturally reached/observed. **Rank 1.** |
| Type `0x65/0x66` | `0x001AFBF4` | Falling overlap, interaction gate clear; converts `65->66` and sets `PLAYER_VY=-0x500` | Selector `0x74`/template `0x001B7E54` is present; lower camel/bounce was naturally observed. Local, not yet connected to exit corridor. **Natural rank 2.** |
| Type `0x69/0x6A/0x6B/0x6C` | `0x001AF978` | Terrain response idle or vertically stopped, actor flag `0x10`, local Y difference `<0x0C` | Handhold state and `6A->6B` transition are naturally observed. It only aligns Y and publishes interaction state. **Natural rank 3.** |
| Type `0x4E` | `0x001AFCD2` | Non-upward player VY, gate clear; actor-facing VY `=-0x900`, Vx `±0x700` | No Level 01 producer or natural state. |
| Type `0x4F` | `0x001AFC4E` | Non-upward player VY, gate clear, actor X overlap | No Level 01 producer. Type `0x4F` in the old audit was a scene-9 loader-matrix observation, not Level 01; that entry is corrected here. |
| Type `0x11/0x12` | `0x001AF110` | Interaction gate set; Vx `+0x600`/`-0x400`, VY `=-0x400` | No Level 01 actor type `0x11/0x12`; Level 01 row selectors `0x11/0x12` produce other types. |
| Type `0x50/0x51` | `0x001AF8F6` | Actor placement/transition gates; writes actor-relative `PLAYER_X/Y`, transition lock/gate, camera threshold | Type `0x50` is a level-table entry-9 producer, not Level 01. |
| Type `0x67/0x68` | `0x001AF740` | Movement flag and vertical window; actor-relative Y placement | Type `0x67` is in another loader matrix; no Level 01 producer. |
| Player actor Type `0x83` with `movement_pc != 0` | `0x001ADE36` | Movement VM enabled; signed movement stream deltas | Level 01 slot 0 is Type `0x83`, but `movement_pc=0`; no natural Level 01 installer. **Not ranked as a natural candidate; strongest dormant raw capability.** |

The remaining direct actor `PLAYER_Y` handlers (`0x001AF590`, `0x001AF5F0`, `0x001AF638`, `0x001AF6AC`, `0x001AF79E`, `0x001AF81C`, `0x001AF894`, `0x001AF9F6`, `0x001AFA84`, and `0x001AFB36`) are actor-relative near-contact alignment families. They have small-difference/overlap guards, do not assign a launch VY, and are local placement rather than transfer mechanisms.

The direct actor VY inventory is therefore closed by `0x001AF14E` (Types `0x11/0x12`), `0x001AFC22` (Types `0x65/0x66`), `0x001AFC7A` (Type `0x4F`), `0x001AFCF8` (Type `0x4E`), and `0x001AFDBC` (Type `0x01`), plus the ordinary and terrain sites listed above. ROM writes at `0x001AE630`, `0x001AE642`, and `0x001AED38` are in unreferenced/orphan regions with no dispatch-table or caller edge; they are not runtime mechanisms.

### Scripted and scene placement

| Routine | Active state/producer | Level 01 result |
|---|---|---|
| `0x001B5B4A` `Level01_EnterRoutine` (write at `0x001B5B5E`) | `PLAYER_WORLD_X > 0x1287` and `PLAYER_WORLD_Y < 0x01D6` | Predicate is present and was evaluated naturally, but no natural route has satisfied it; controlled boundary fixture armed `SCENE_SCRIPT_COUNTDOWN=0xFF` at `0x00FFF0E9`. |
| `0x001A8E3E` `SceneScript_AdvanceState` | Nonzero scene countdown/script terminator | Controlled follow-on wrote `SCENE_STATE=0x03`; no natural Level 01 completion observed. |
| `0x001B3B96` `SceneTable_SelectNextState` | Scene-table index and ROM transition table | Can select another scene state, including table data for state `0x08`; no natural Level 01 producer observed. No direct code write of `SCENE_STATE=0x08` exists. |
| `0x001AA484` `Level_LoadFromSceneState` | Selected `SCENE_STATE`; level table at `0x002C78` | Copies initial world/camera/player coordinates and decodes resources. This is genuine discontinuous placement between scenes, but downstream of the missing exit event. |
| `0x001B6406` `Level01_ExitRoutine` | Level-table entry-1 exit callback | Writes VDP control and invokes common transition code; it is not an upstream vertical bridge. |

`0x001A8F0C` terminal/death transition handling and `0x001B1F28` scene/terrain setup are also in the scene graph, but their player-motion effect is to clear/reset VY while rebuilding state. They do not provide an upward transfer and are not candidates.

## Producer and reachability answers

“Present” means the state/resource exists in the Level 01 decoded map, interaction table, or observed actor table. “Statically reachable” means there is an unbroken ROM/resource edge from a natural Level 01 map or interaction record to the required state; it does not mean the clean replay has already reached the triggering coordinate.

| Mechanism | Exact transfer routine | Required state/behavior | Producer present? | Static natural-chain reachability? | Natural MAME observation? |
|---|---|---|---|---|---|
| Normal jump/integration | `0x001A9716` -> `0x001A9B90` | Jump input/gates; ordinary VY arc | Yes | Yes | Yes; ordinary |
| Connector stepping | `0x001A986E` | Terrain `0x22/0x23/0x24` query state | Yes | Yes | Yes; rope/connector |
| Local terrain snap/contact | `0x001AD7B4`, `0x001A99F0` | Contour/contact resource and local collision window | Yes | Yes | Yes |
| Terrain `0x27` step | `0x001B54A6` | Behavior `0x27` cell | No | No | No |
| Terrain `0x29` launch | `0x001B557E` | Behavior `0x29` cell, inactive response | No | No | No |
| Terrain `0x2D` bounce | `0x001B56B6` | Behavior `0x2D` cell | No | No | No |
| Type `0x65` actor launch | `0x001AFBF4` | Type `0x65/66`, falling overlap | Yes | Yes | Yes; local lower bounce |
| Type `0x01` actor launch | `0x001AFD84` | Selector `0x87` -> Type `0x01`, near-Y collision | Yes | Yes | No; focused candidate |
| Type `0x83` movement-stream placement | `0x001ADE36` | Player actor `0x83`, nonzero `movement_pc` | Player type yes; stream no | No | No |
| Type `0x4E/4F`, `0x11/12`, `0x67/68` launches | `0x001AFCD2`, `0x001AFC4E`, `0x001AF110`, `0x001AF740` | Corresponding actor types and guards | No | No | No |
| Type `0x50/51` placement | `0x001AF8F6` | Global placement actor and transition gate | No; entry-9 only | No | No |
| Natural exit gate | `0x001B5B4A` | Level 01 world-position predicate | Yes | Yes | Predicate evaluated; completion no |
| Script/scene placement | `0x001A8E3E` -> `0x001AA484` | Countdown/script/table-selected scene state | Code/table yes; natural producer no | Downstream only | Controlled only |

## Dynamic installation closure

The only terrain behavior installation path is `0x001AA484` -> `0x001B3434` -> `0x001B3818`, which decodes the Level 01 static behavior source at ROM `0x001434C4` into `0x00FFAE84`. `0x001B1E38` then indexes the decoded behavior and dispatches through the fixed table at `0x004554`. No observed Level 01 runtime writer changes a cell to behavior `0x27`, `0x29`, or `0x2D`.

Interaction rows are a separate dynamic actor path: `0x00FFAE87` -> row processors -> `0x001AE30A` template copy, with F5 child creation through `0x001AD00E`. The complete Level 01 interaction inventory has 175 records and 22 selectors. It contains selector `0x87` (the Type-`0x01` candidate), selector `0x74` (the observed Type-`0x65` bounce), and the handhold family, but no hidden terrain installer and no absent actor launch type such as `0x4E`, `0x4F`, `0x50`, `0x67`, `0x11`, or `0x12`.

The actor terrain pass at `0x001ADB5C` can publish an actor terrain byte such as `0x46`; its Level 01 Type-`0x1E`/movement-stream family produces effects and cleanup, not player Y/VY or a scene state. Behavior selectors `0x1B/0x1C/0x1D/0x1F` and absent interaction selectors `0xAC/0xAD/0xB1` likewise terminate in flags/effects or ordinary actors, with no transfer edge.

## Ranked natural frontier

1. **Selector `0x87` -> Type `0x01` -> `0x001AFD84`.** Only candidate with a direct `PLAYER_Y`/`PLAYER_VY` launch writer, a Level 01 resource producer, and a complete static chain. It is unobserved because the natural route has not reached the source interaction/collision window.
2. **Type `0x65`/`0x66` at `0x001AFBF4`.** Real Level 01 actor launch and natural MAME observation, but the observed producer is the lower local bounce/camel family; no static edge currently connects it to the upper exit corridor.
3. **Type `0x6A`/`0x6B` at `0x001AF978` plus ordinary jump.** Real Level 01 handhold/connector state, naturally observed, but only local alignment and normal dismount motion.
4. **Level 01 exit gate -> scene script -> `0x001AA484`.** Genuine discontinuous scene placement, but downstream: it cannot explain how the player first reaches the exit-height predicate.

The strongest *raw* capability outside that natural ranking is the player Type-`0x83` movement VM at `0x001ADE36`: signed stream deltas write `PLAYER_Y` directly, but the required nonzero `movement_pc` producer is absent from Level 01. It is excluded from the natural ranking. Terrain `0x27/0x29/0x2D` and the absent actor families are structurally eliminated rather than ranked.

## Focused next runtime check

Do not sweep controller inputs broadly. Instrument the existing natural replay at these specific edges:

```text
0x001B74D6 / 0x001B525E  selector-0x87 allocation
0x001AE30A                Type-0x01 template copy
0x001AFD84                Type-0x01 collision entry and its near-Y guard
0x001ADE36                player Type-0x83 movement_pc and signed Y delta
0x001B5B4A                Level 01 boundary predicate
0x001A8E3E / 0x001AA484   scene countdown/state and placement follow-on
```

The first decisive test is whether the natural route can reach selector `0x87` and whether the Type-`0x01` collision guard is satisfied. If those edges remain cold, the static graph has no other Level 01-present direct launch writer to justify an input sweep.

Machine-readable companion: [20260827-level01-transfer-capable-mechanism-graph-v1.json](mame/findings/20260827-level01-transfer-capable-mechanism-graph-v1.json).
