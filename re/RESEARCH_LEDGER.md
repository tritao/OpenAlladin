# OpenAladdin reverse-engineering ledger

This file is the index for the reproducible reverse-engineering record. It is
deliberately separate from implementation notes: a claim belongs here only
when a tracked source, a replayable campaign, or a deterministic analysis
artifact supports it.

## Baseline

Captured on 2026-08-26.

| Item | Value |
| --- | --- |
| Repository | `master` |
| ROM | `rom/Disneys_Aladdin_U_p1.bin` |
| ROM SHA-256 | `8199d016f7bb88ea73b635dcc072c126b40f01c662707ed3f67d865fd86c0ab6` |
| MAME submodule | `c04a5be3a8c3761744df29bebedcf5c71e6a71af` |
| SDL submodule | `be51a15ba2079e6449a169e8b8ddb559c798b15d` |
| Ghidra submodule | `8b4c91d4d5bd1549622bfbade0df199585b98365` |
| Runtime build | `cmake --build build -j20` |
| Test status at capture | CTest: 16/16 passing |

The repository history is itself part of the record. Do not rewrite the
reverse-engineering commits or replace the ROM/emulator baseline without
adding a new dated baseline section.

## Recording contract

Every significant experiment should leave all of the following:

1. A tracked campaign or finding manifest under `re/mame/`.
2. The exact ROM hash, emulator revision, and harness commit in that manifest.
3. A replayable input schedule or loaded-state provenance.
4. Named MAME checkpoints for meaningful events, with state files retained in
   the ignored `build/re/` tree.
5. A short conclusion that labels each result as verified, negative evidence,
   or an unresolved hypothesis.
6. A commit containing the manifest/finding and any code or symbol changes.

Large traces and MAME `.sta` files remain ignored build artifacts. The tracked
manifest must point to them and `campaign.py verify` must pass before a
campaign is considered recorded. If a trace is regenerated, retain the same
input and state provenance and record the new output directory rather than
silently overwriting the old evidence.

For direct harness runs, use `genie/mame/run.sh`; it records the
ROM identity in the trace header and supports deterministic frame schedules,
saved-state loading, actor/RAM/VDP capture, and named checkpoints. For native
regressions, keep the corresponding MAME trace and the comparison command in
the finding or commit message.

## Verified evidence index

### Runtime and emulator

- `49f95cf` made direct MAME traces self-identify with the ROM hash.
- Relative saved-state paths are normalized by `run.sh`; the load-state smoke
  test is recorded in the platform-chain investigation.
- The normal trace path is headless, with Xvfb available through `MAME_XVFB=1`.

### Gameplay and actor behavior

- `d96db08`: fresh-boot level-01 baseline.
- `6c84fde`: fresh natural route progression through the opening route,
  including the upper ledge, lower chamber, wall, and handhold probes.
- `fb5100d`: bounce/handhold timing family; tested timings did not produce a
  transfer to the upper route.
- `183a082`: exact player-attack differential checkpoint; native and MAME
  state traces match for 47 frames from the synchronized checkpoint.
- `477f5c1`: apparent pre-wall platform chain classified as guard-sword child
  (`0x2D`) and oscillating interaction actor (`0x34`), not a traversable
  moving-platform transfer.
- `20260826-level01-object-trace-frontier-v1`: upper-platform actor
  initialization and animation trace. It records `0x7F` as a transient
  spawn/clear effect and `0x2A` as a falling/resetting movement-stream actor;
  the `0x1E` player-proximity actor remains unresolved.
- `20260826-level01-recording-high-walkway-v1`: current-baseline replay of the
  retained high-platform route. The `0x1D` guard is sword-cleared and the
  upper walkway is reached, but its right edge falls back to the lower band;
  the current replay has a small coordinate drift from the older trace.
- `20260826-level01-recording-frontier-v1`: current-harness replay of the
  lower-gap → handhold → upper-platform → wall-rope chain, with 22 verified
  named checkpoints. The associated frontier matrix records the direct wall,
  upper-rope, upper-platform, and spring negative branches.
- `20260826-level01-fresh-route-canonical-v2`: fresh level-entry replay with
  debugger synchronization disabled, four segments and 27 verified named
  checkpoints through the handhold, upper rope, upper-left ledge, and guard.
  This is the current canonical route frontier; it remains in scene state
  `0x01` and does not yet prove the transfer to the retained high walkway.
- `20260826-level01-fresh-route-high-transfer-v1`: fresh handhold replay that
  reaches the earlier `0x22` rope at X≈1560, transfers to the Y=530 high
  walkway, clears the upper guard, and records the right-edge fall. This
  converts the retained high-walkway state into a verified natural-route
  continuation.
- `20260826-level01-recording-fresh-v2`: clean power-on replay of the same
  route in one continuous no-sync segment. It records 16 named checkpoints
  from power-on through the upper-edge frontier, with the ROM and harness
  provenance captured at the current commit.
- `20260826-level01-canonical-recording-v3`: current-state clean replay and
  loaded-state continuation. It records the power-on route, lower-tower
  reversal/crossing, a guarded far-floor branch, and the positive far vertical
  rope approach/climb at world X≈4728. The route remains in scene state `0x01`.
- `re/mame/findings/20260826-level01-canonical-recording-v3.json`: separates
  verified positive route evidence from guard/reset and horizontal-line
  negative branches and records the next upper-transfer frontier.
- `20260826-level01-canonical-recording-v4`: provenance-complete fresh power-on
  replay under the current commit. Its 2,297 state frames exactly match the
  corresponding v3 fields, and its own loaded-state chain reaches the far rope
  at world `(4728,671)` with 63 verified checkpoints before the new transfer
  experiments.
- `re/mame/findings/20260826-level01-canonical-recording-v4.json`: records the
  far horizontal-line dismount, natural lower-handhold `0x6A→0x6B` collision,
  continuous launch apex at `Y=561`, and sword-assisted guard-window negative
  evidence. None reaches scene state `0x08`.
- `20260826-level01-canonical-recording-v5`: clean recording pass started from
  a fresh power-on and replayed the entire v4 route through the far vertical
  rope using only v5-owned loaded-state checkpoints. The power-on trace matches
  v4 across all 2,297 state frames; all seven continuation segments also match
  their v4 counterparts frame-for-frame, giving the project a new provenance
  baseline without discarding earlier evidence.
- `re/mame/findings/20260826-level01-canonical-recording-v5.json`: records the
  63 named v5 checkpoints and significant route events, ending at world
  `(4728,671)` on terrain behavior `0x24` with `SCENE_STATE=0x01`.
- `20260826-level01-canonical-recording-v6`: clean power-on baseline and
  self-contained replay chain extending v5. It adds a precisely timed Type1E
  sword opening at the lower wall (`0x1E→0x84` at frame 64) and a valid
  jump-left continuation to a stable lower-band frontier at world `(2846,912)`.
- `re/mame/findings/20260826-level01-canonical-recording-v6.json`: records the
  Type1E handler context, successful wall crossing, type-0x8A object observed
  at `(2720,768)`, and negative ordinary-control probes from the new frontier.
- The v6 route now also records the far-rope upper transfer. An untimed jump
  reaches `(4492,628)` but dies in the upper actor cluster; holding sword
  during the jump carries the player through it to a stable frontier at
  `(4372,628)`, still in `SCENE_STATE=0x01`.
- The v6 frontier archive now records six controller families from the
  behavior-`0x24` endpoint at `(4728,671)`. Direct Up/horizontal input leaves
  the player fixed; a fresh jump peaks at `(4728,604)` and returns to the
  behavior-`0x25` band or the lower floor. This endpoint is therefore the
  upper stop of the connector, not an untested climb continuation.
- A five-branch probe of the visible x≈4688 feature records the nearby type
  `0x40` marker and its C-triggered transient `0x40→0x84` response. The
  marker does not attach the player, launch him, or write a scene gate; all
  branches remain in `SCENE_STATE=0x01`.
- The v5 campaign is now extended with a fresh horizontal-line segment, the
  natural far-juggler defeat, and a timed lower-platform sweep. The juggler
  transition is checkpointed at frame 99 (`0x0A→0x84`); continued left movement
  reaches the gap band at world `(4121,912)`, and three C-jump pulses reach a
  stable lower checkpoint near `(3296,912)` without yet opening the upper route.
- `re/mame/findings/20260826-level01-actor-collision-decomp-v1.json` records
  the static `0x001AC458` actor-to-actor handler and its natural confirmation in
  the v5 juggler-defeat trace. Apple timing branches remain retained as
  negative evidence because their projectile collision band misses the juggler.
- `20260826-level01-tower-negative-v1`: loaded-state recording from the clean
  upper-edge checkpoint through the lower tower. It verifies the 0x47 surface
  mode/latch behavior, identifies the resident 0x43 lamp and 0x20 enemy, and
  records the tower as a terminal lower/death-band branch rather than a route
  connector.
- `20260826-level01-tower-support-exploration-v1`: phase-1 controlled-frontier
  recording layered on the clean route. It verifies the lower support contour,
  bounds the empty lower-to-rope gap, re-records the known 0x22 rope climb with
  checkpoints, and preserves the natural edge timing as negative evidence.

### Terrain/connector decompilation

- `re/mame/findings/level01-tower-frontier-decomp-v1.json` records the corrected
  Ghidra target mapping. Behavior 0x47 dispatches to `0x001B5470`, where it
  toggles `TERRAIN_SURFACE_MODE` and arms `TERRAIN_SURFACE_LATCH`; behavior
  0x22/0x23 dispatches to `0x001B54D8`, which raises `TERRAIN_QUERY_STATE_A`.
  Neither handler itself creates a missing actor or scene transition.
- `re/mame/findings/20260826-level01-far-transfer-decomp-v1.json` records the
  far-band collision/terrain decompilation. The 0x6A handhold handler confirms
  the observed 0x6A→0x6B interaction, the 0x0A guard path flows through the
  shared collision block and interaction-state recovery, and 0x1AF468 clears
  and reinitializes an actor after an optional interaction publish. The 0x29
  launch and 0x2D bounce handlers are decoded. The type-0x2D player-collision
  entry at `0x001AEE40` is a transient actor cleanup/spawn path, distinct from
  the terrain behavior 0x2D bounce handler at `0x001B56B6`. The raw type-0x40
  probe shows that 0x1AF468 takes its counter/cleanup branch, not the
  interaction-row publish branch, and replaces the actor with type 0x84. None
  of the 17 targets writes `SCENE_STATE=0x08`; the v4 natural-transfer
  frontier therefore remains the correct replay baseline.
- `re/mame/findings/20260826-level01-actor-collision-decomp-v1.json` confirms
  the receiving type-0x0A actor collision path at `0x001AC458`: it clears the
  source, installs the type-0x84 terminal template, and does not write
  `SCENE_STATE`.
- `re/mame/findings/20260826-terrain-dispatch-table-correction-v1.json`
  corrects the terrain dispatch extent to 256 four-byte entries. The newly
  decoded behavior `0x70` handler is terminal/death bookkeeping, behavior
  `0x88` is a landing latch, and neither behavior occurs in the Level 01 map.
- `re/mame/findings/20260826-level01-uncovered-interactions-v1.json` closes
  the unobserved selectors `0xAC`, `0xAD`, and `0xB1`. Their terrain-gated
  handlers spawn Type `0x3A`, Type `0x40`, or a generic object respectively;
  none writes a scene state, and none is present in the extracted Level 01
  interaction table.
- The query/gate leafs at `0x001B3208` and `0x001B323A..0x001B324E` are now
  named from their exact bit-test bodies. `VBlank_SceneResourceGate` tests
  bit 5 of `0x00FFF155`; the three configured terrain callback
  implementations test bits 4/4/5 of `0x00FFF155`/`0x00FFF156` and return
  condition codes to their indirect callers. They do not write terrain,
  player, actor, or scene state.
- `re/mame/findings/20260826-level01-unclassified-actor-collision-v1.json`
  classifies the remaining actor-collision table family: types `0x2B`,
  `0x2D`, `0x2E`, `0x2F`, `0x30`, and `0x31` are transient cleanup, event, or
  type-`0x84` replacement paths. They contain no player launch, connector, or
  scene-state write; the observed Level 01 type-`0x2D` path is therefore not
  the missing vertical transfer.
- `re/mame/findings/20260826-level01-observed-unknown-player-collision-v1.json`
  closes the remaining observed player-collision targets for types `0x15`,
  `0x36`, and `0x3A`. They select interaction/effect responses or replace an
  actor record, with no player velocity/coordinate injection and no scene gate.

### Scene and exit work

### Upstream transfer writer graph

- `re/ghidra/targets/level01-upstream-transfer-writers-targets.json` and
  `re/mame/findings/20260827-level01-upstream-transfer-writer-graph-v1.json`
  record the backward closure from every material Level 01 player-Y/player-VY,
  terrain-response, connector, interaction, actor-placement, and scene-gate
  writer. The x≈2112 chain is behavior `0x24/0x22` through
  `0x001B54D2`, with nearby selector `0x5C` Type-0x06 and selector `0x60`
  Type-0x40 resources. The x≈2720 chain has the same connector handlers plus
  selector `0x51` Type-0x3A, selector `0x10` Type-0x1F, and selector `0x40`
  Type-0x43 resources. Their movement streams are zero or horizontal-only;
  their animation streams and collision handlers do not write player Y/VY or
  a scene gate. The row-10 selector `0x87` path likewise creates stationary
  Type-0x01/Type-0x84 resources.
- The writer graph classifies behavior `0x27` (`PLAYER_Y -= 0x50`) and
  behaviors `0x29/0x2D` (large velocity writes) as the only direct terrain
  transfer-capable families in the closure. Level 01 contains zero decoded
  behavior-`0x27` cells, and neither local connector chain reaches the
  launch/bounce handlers. Type-0x65/Type-0x6A actor placement is live but is
  already bounded to the observed local bounce/handhold families. This closes
  the requested upstream records as transfer producers without another MAME
  input matrix; runtime tracing resumes only after static work identifies a
  different producer or installation path.

### Transfer reachability closure

- `re/mame/findings/20260827-level01-transfer-reachability-closure-v1.json`
  supersedes the provisional Type-0x65/Type-0x6A conclusion above. The
  terrain path is now closed: `Terrain_ResolvePlayerCell` at `0x001B1E38`
  is called from `0x001A8C8C`, selects a byte from the load-time terrain table
  at `0x00FFAE86`, and dispatches through `0x004554`. Level 01 has zero
  decoded behavior-`0x29` or behavior-`0x2D` cells, and no gameplay writer
  installs either behavior. The dynamic map mutations clear map work-RAM
  words; they do not rewrite the behavior table.
- The same closure finds four direct Level 01 producers for the actor-side
  transfer families. Selector `0x74` at map cell `(100,41)` dispatches through
  `0x004324 -> 0x001B670C -> 0x001B525E -> 0x001AE2AA` and initializes
  template `0x001B7E54` as Type-0x65 in slot 20. Selector `0x0D` at cells
  `(104,29)`, `(259,31)`, and `(265,30)` dispatches through
  `0x004188 -> 0x001B6802 -> 0x001B5256 -> 0x001AE292` and initializes
  template `0x001B7D8C` as Type-0x6A in slot 24. The early selector-0x0D
  producer is directly trace-validated, and the corrected natural far-floor
  trace now directly catches both remote records: `0x6148/0x30A4` at MAME
  frame `0x0EF0` in slot 24 and `0x6060/0x3030` at `0x0F10` in slot 23.
- Actor collision dispatch is through `0x001ABB40` and table `0x001CBE`:
  Type-0x65/0x66 reaches `0x001AFBF4`, which can set `PLAYER_VY=-0x0500`,
  while Type-0x69..0x6C reaches `0x001AF978`, which can place `PLAYER_Y`
  at actor height. No player/interaction state redirects the terrain resolver
  to another behavior family; those state bytes gate consequences before or
  after the lookup. The producer work is closed; the next runtime work is a
  single targeted contact-window trace for the spawned remote actors, not
  another connector-top input matrix.

### Complete Level 01 transfer-capable mechanism graph

- `re/LEVEL01_TRANSFER_MECHANISM_GRAPH.md` and
  `re/mame/findings/20260827-level01-transfer-capable-mechanism-graph-v1.json`
  close the ROM writer graph across ordinary integration/jump, connector
  stepping, local terrain snap/contact, discontinuous terrain handlers,
  actor-driven placement, dynamic resource installation, and scripted scene
  placement. The only remaining Level 01-present actor launch candidate with
  a complete static producer edge is selector `0x87` -> Type `0x01` ->
  `0x001AFD84`, whose near-contact path writes actor-relative `PLAYER_Y` and
  `PLAYER_VY=-0x0800`. It is not yet observed naturally because the clean
  replay has zero selector-`0x87` handler hits and zero Type-`0x01` samples.
- The player movement VM at `0x001ADE36` is the strongest raw ROM capability:
  a Type-`0x83` player actor with nonzero `movement_pc` receives signed stream
  deltas directly into `PLAYER_Y`. Level 01 has the player actor type but its
  `movement_pc` is zero, and no natural Level 01 resource chain installs a
  stream, so this is dormant capacity rather than a live producer.
- Terrain behaviors `0x27`, `0x29`, and `0x2D` are structurally eliminated:
  their handlers at `0x001B54A6`, `0x001B557E`, and `0x001B56B6` are genuine
  discontinuous/launch writers, but Level 01 has zero decoded cells for all
  three and the dynamic terrain path does not install them. Type-`0x65`
  bounce and Type-`0x6A` handhold states remain real, naturally observed,
  local families; neither currently connects to the exit corridor.
- This graph corrects two scope errors in earlier notes. The old Type-`0x01`
  child-chain conclusion applies to its stationary Type-`0x84` child, not the
  parent collision handler. The old motion-audit `0x4F` Level 01 observation
  belongs to the scene-9 loader matrix, not Level 01. The exit predicate at
  `0x001B5B4A` and scene loader at `0x001AA484` are genuine downstream
  discontinuous placement machinery, but cannot explain the upstream rise to
  the predicate. The predicate's exact countdown store is at `0x001B5B5E`
  (`0x001B5B66` is the adjacent countdown branch). No broad input sweep is justified before the focused
  selector-`0x87`/Type-`0x01` runtime trace.

### Player animation and terrain timing

- `re/mame/findings/20260827-level01-f5-actor-boundary-v1.json`: the opening
  player-stream `F5` request initializes slot 3 and publishes its first actor
  animation frame in the same MAME VBlank. Native mode-0 allocation remains a
  known one-boundary gap for the live actor boot; a later interaction-refill
  actor follows a different cadence, so the broader scheduler is still
  unresolved.
- `re/mame/findings/20260827-player-jump-animation-boundary-v1.json`: the
  ROM-backed player jump replay now matches MAME for 157 synchronized frames,
  including run-stream response-latch branching, ordinary-vs-timed jump root
  selection, delayed landing-latch timing, vertical-stop writes, and landing
  animation ordering. `tests/player_jump_regression.py` runs this comparison
  as a CTest differential gate.
- `re/mame/findings/20260827-player-camera-follow-boundary-v1.json`: vertical
  camera reference rebases still perform horizontal follow and the player VM
  on the same boundary, while deferring only vertical damping; the following
  frame receives one ordinary follow service rather than a queued double step.
  Native player/camera fields now match the actor-boot replay through aligned
  frame 394; frame 395 is coupled to the unresolved actor-cleanup scheduler.
- `re/mame/findings/20260827-level01-actor-lifecycle-v1.json`: the actor
  mismatch at aligned frame 395 is a real type-0x2D player-collision lifecycle,
  not a scene transition. Slot 3 enters `0x001AEE40`, then clears its type and
  owned resources through `0x001AE372`; Level 01 remains `SCENE_STATE=0x01`.
- `re/mame/findings/20260827-level01-actor-refill-vm-v1.json`: the first
  interaction-refill actor is allocated in slot 8 at MAME frame `0x536` through
  `0x001B5270`, while the common animation gate crosses to `0x91` at
  `0x001A8C1E`. At MAME frame `0x537`, stream `0x00122C12` executes `EC 01`
  at `0x001AC86E` with `A1=0x00FF8050` (slot 8); this clears only record
  `+0x20`, the animation cursor, while preserving type `0x40` and frame pointer
  `0x001F84A4`. Static decoding confirms that `AnimationVM_TickActors` requires
  the gate low bit and a nonzero actor animation pointer, so this is animation
  state clearing rather than F6 actor retirement, and the opening player F5
  deferral rule cannot yet be generalized to interaction-refill actors. The
  same slot is later reused at MAME frames `0x5DA` and `0x6A5`: caller
  `0x001B737C` writes type `0x40`/stream `0x00122C12`, while caller
  `0x001B72E6` writes type `0x34`/stream `0x00122C1E`/movement
  `0x001217B4` after the generic `0x001B79B8` initializer. Slot reuse is
  therefore classified at the caller's post-initializer writes, not by the
  zero-type template alone. The initializer also leaves actor `+0x0E`
  (movement-loop cursor), its timer, and the return cursor untouched; native
  template reuse preserves those stale fields before applying the caller's
  overrides.

### Scheduler static reconstruction

- `re/ghidra/targets/scheduler-targets.json` and
  `re/scheduler/frame_phases.yml` record the first dedicated static scheduler
  pass. Ghidra recovers 37 direct call sites in
  `Game_FrameUpdateLoop` (`0x001A8C16`), beginning with the increment of the
  `0x00FF7E28` frame-phase counter and ending at
  `SceneScript_CompleteToState1` (`0x001B315C`). The ledger records each call
  site, callee, RAM gates, actor range, pre/post mutation summary, and whether
  another direct invocation exists in the same loop.
- The recovered gameplay body contains one direct
  `AnimationVM_TickActors` call at `0x001A8CCE`, after scene-script advance;
  it does not contain a second direct animation-VM pass before
  `MovementVM_TickActors`. Native now records one `animation_vm` phase for the
  normal actor/player service at that recovered entry; the snapshot-only
  pre-motion probe and spawn/cursor follow-ups remain explicitly temporary
  compatibility paths rather than accepted ROM scheduler phases.
- `VBlankInterrupt` (`0x001B246E`) is tracked as a separate interrupt path,
  gated by `SCENE_RESOURCE_ERROR`, with its own resource/query callback
  services. This is static/decompiled evidence only; no dynamic scheduler or
  parity claim is promoted until the instrumentation provenance is committed.
- `re/mame/campaigns/20260827-scheduler-static-dynamic-v1.json` records the
  first provenance-complete scheduler call-site replay after the focused
  instrumentation was committed at `4414e16`. The Level 01 route observes
  7,747 call-site events: 209 complete repetitions of all 37 recovered direct
  calls, with zero sequence errors and a final capture ending at ordinal 14.
- The same campaign confirms that ordinal 30 (`0x001A8CCE` → `0x001AC784`) is
  the only direct animation-VM call in the gameplay body. It records 210
  gameplay writes to `FRAME_PHASE_COUNTER` from `0x001A8C1E` and 1,396 VBlank
  boundaries at `0x001B2470`. `FRAME_WAIT_LATCH` has no gameplay writer in
  this route; its non-reset writer remains the pre-gameplay `0x001AA3B0` path.
- This validates the direct static call sequence for the tested route, but it
  does not promote the snapshot-only probe or identify indirect service
  callers. Those remain provisional pending a transition/resource campaign.
- `re/scheduler/native_update_mapping.yml` is the row-by-row comparison of all
  37 ROM calls against `Engine::update()`. It classifies 19 rows as exact or
  inlined, 11 as ordering/split mismatches, 6 as presentation-only, and 1 as
  unknown. Its ordinal-30 row now maps the normal native actor/player
  traversal to one service and keeps the probe, spawn, catch-up, cursor,
  defer, and force state tied to individual regression evidence and removal
  conditions; none was deleted speculatively.

- `re/mame/campaigns/20260827-frame-wait-lifecycle-v1.json` resolves the
  previously ambiguous `FRAME_WAIT_LATCH` role. Static ROM inspection finds
  the initialization clear at `0x001AA3A8` in `FUN_001AA344` (the debugger
  reports post-instruction PC `0x001AA3B0`), plus the transition-mode set/clear
  pair at `0x001B2DF4` and `0x001B2E02` in `Scene_EnterTransitionMode`.
- `0x001B249E` does not wait for `FRAME_WAIT_LATCH` to become nonzero. It
  tests that byte only to decide whether to perform the Z80 bus handshake,
  then waits for `VBLANK_READY_LATCH` (`0x00FF7E1E`) written by
  `VBlankInterrupt`. The clean-boot trace observes two frame-wait writes and
  1,396 VBlank boundaries, with no gameplay writer. A replay from the
  available transition checkpoint remained in the wait helper and did not
  reach the transition writer pair, so those writers remain statically
  decompiled but not dynamically reached.

- `re/mame/campaigns/20260827-level01-transition-resource-lifecycle-v1.json`
  and its finding record the next transition/resource coverage pass. The
  upper-frontier checkpoint with the proven local-coordinate predicate reaches
  the Level 01 boundary writer (`0x001B5B5E -> FFF0E9=FF`), then ordinal 29
  enters `SceneScript_AdvanceState` and its `0x001B0078 -> 0x001B0D70`
  resource prelude. The latter executes 300 VBlank-paced service iterations
  through `0x001B28AE -> 0x001B249E`; the nested `0x001B16E0` lifecycle then
  remains active at `0x001B1842`/`0x001B1878` for the 1600-frame replay. No
  script cursor/state writer, `Scene_EnterTransitionMode`, or transition
  `FRAME_WAIT_LATCH` writer fires. This is a restartability/resource-context
  limitation, not evidence that the natural route cannot transition.

- `re/mame/findings/20260827-level01-type34-reuse-native-v1.json`: the later
  selector-0x53 reuse now applies the caller's type-0x34/animation-0x00122C1E/
  movement-0x001217B4 fields directly after the generic initializer. Native
  slot 8 matches the MAME actor fields from aligned frame 458 through its
  type-zero retirement at frame 597; no synthetic first-tick defer is needed.
- `re/mame/findings/20260827-level01-upper-pair-extended-interaction-v1.json`:
  a lossless decode corrects the earlier Type-0x1E extended-stream alignment.
  `ED` is at `0x001237CA` and writes actor `+0x0A = 0x0012046C`; the repeated
  `FB` records are at `0x001237E4`, `0x001237F8`, and `0x0012380E`, each
  queuing the random/audio callback at `0x001ACC5E`. The target movement
  stream is an actor-local animation handoff and movement stop, while the
  callback only selects event IDs `0x5E..0x61`; neither path writes player
  coordinates, the scene countdown, or scene state. Natural reachability of
  this branch remains the only open question for the upper pair.
- `re/mame/findings/20260827-level01-upper-pair-extended-interaction-runtime-v1.json`:
  an explicit loaded-state `left*60,up*120,none*120` replay reaches the ordinary
  Type-0x1E actor but records zero entries for the corrected extended cursor,
  movement `0x0012046C`, interaction state `0x46`, and callback `0x001ACC5E`.
  The scene remains `0x01`; this closes the tested input family and moves the
  unresolved producer question to the common actor-terrain collision pass.
- `re/mame/findings/20260827-level01-actor-interaction-state-producer-v1.json`:
  the common actor terrain/collision pass writes `+0x3D` from the decoded terrain
  resource's third byte (`0xFFAE84 + (map_word >> 1) + 2`). Level 01's behavior
  `0x46` prerequisite exists in 23 cells across lower rows 41 and 43, not at the
  tested upper pair; the remaining branch search is now a finite lower-band route
  and contact trace.
- `re/mame/findings/20260827-player-slope-grounded-boundary-v1.json`: a fresh
  Level 01 actor-boot replay shows non-flat contour bytes (`0x0F` through
  `0x03`) publishing `grounded=false` while retaining ground-motion and run
  animation behavior. The native contour latch reproduces the six-frame
  grounded/landing sequence and preserves the verified 157/257-frame jump
  fixtures; the later camera-rebase differential remains separately open.

- `re/mame/findings/level-transition-findings.json`: controlled scene-state
  transition proof and table/exit-gate work.
- `re/mame/findings/natural-exit-findings.json`: controller-only natural route
  currently reaches approximately world `x=2176`, `y=912` and does not reach
  the exit; the controlled boundary write path is known.
- `re/symbols/functions.yml`: level-01 enter/exit routines at `0x1B5B4A` and
  `0x1B6406`.

## Current frontier

The next search is not a new project bootstrap. It is a targeted level-01
geometry/object investigation. The extracted level package is available at
`build/assets/levels/level01/` and is derived from the ROM table entry at
`0x2CBA`:

- map: 300 x 45 terrain words, ROM source `0x195176`;
- floor/behavior data: ROM source `0x1434C4`;
- background block table: ROM source `0x145B2C`;
- enter routine: `0x1B5B4A`;
- exit routine: `0x1B6406`.

The immediate question is now narrower: what natural interaction or terrain
response transfers the verified far vertical rope around world `x=4728`,
`y=671` to the upper platform/exit corridor above `y=470`? The v4 campaign
records the horizontal-line dismount and the lower handhold/guard branches;
the controlled support route is not being treated as proof of the intended
route. The next investigation should use the saved v4 apex and guard-window
states to decode the far-band object/collision path, then resume controller-only
replay and save a checkpoint for every launch, spring, handhold, rope
attachment, dismount, reset, or scene gate.

The v6 continuation adds a second replayable frontier: after the Type1E sword
opening, a jump-left branch reaches world `(2846,912)` and stops against a
boundary while a type-0x43 object at `(2720,768)` becomes type `0x8A`.
Ordinary movement, jump, sword, apple, and direction-change probes remain
negative there. The next trace should instrument the terrain response path at
`0x001A9D98` and `0x001B1E38` while replaying that saved frontier, then test
whether the boundary is geometry, a hidden connector, or an unobserved
interaction gate.

The active main-route frontier remains the sword-cleared upper corridor at
`(4372,628)`, reached from the far rope with
`c+b+left*20,b+left*160,none*60`. The local behavior-`0x24` endpoint and
x≈4688 marker branches are now bounded, so the next productive step is to
trace the remaining upper-route actor/launch condition in the ROM and target
its initializer or collision handler. Repeating the same direct Up/rope
inputs is no longer an open question.

The initializer trace is now recorded in
`re/mame/findings/20260826-level01-upper-activation-v1.json`. Replaying
`right*158,up*120,none*42` from the v6 far-rope checkpoint shows the two
upper actors being materialized in the same emulated frame through
`0x001AE30C`, returning through the level-object dispatcher at `0x001B5270`:
slot 4 receives template `0x001B7C24` (Type `0x1E`) and slot 5 receives
template `0x001B7C10` (initial Type `0x1D`, observed at runtime as Type
`0x20`). They appear around world `(4512,464)` and `(4688,464)` while the
player remains at `(4684,628)` and
`SCENE_STATE` remains `0x01`. This is the next concrete actor interaction
target, not evidence of a climb or scene transition.

The companion edge trace identifies the interaction source. At the same
absolute MAME frame `0x1353`, the interaction-row caller `0x001AE4EA`
dispatches to `0x001B6E90` and `0x001B6EB2`. The former selects template
`0x001B7C24` directly; the adjacent gate-test bytes at
`0x001B6E86` are bypassed by this table target. The latter enters the same
level-object spawn entry with `0x001B7C10`, then assigns animation
`0x0012337A` and runtime type `0x20`. The three candidate gate bytes remain
zero throughout the RAM capture. The Type-`0x1E` animation stream's
Y-proximity threshold is `0x40`, so it is not the upper climb trigger at the
observed 164-pixel vertical separation. The upper feature is therefore an
interaction-row actor setup, and the next target is the Type-`0x1E`/Type-`0x20`
collision or interaction response after setup.

The paired no-Up control (`right*158,none*82`) remains at the same x≈4684
frontier without creating either actor and dispatches no interaction-table
edge. The actor pair is therefore conditioned on the Up-triggered interaction
path, not merely on reaching the horizontal coordinate.

The row-boundary trace is now recorded in the same upper-activation campaign.
At `0x001AE4E4` immediately before the indirect call at `0x001AE4E8`, the
second interaction-row processor has consumed row word `0x5658` at
`0x00FF2306`, producing runtime index `0x2B2C` and interaction byte `0x11`,
which resolves through handler-table entry `0x004198` to `0x001B6E90`. The
next active row is word `0x5668` at `0x00FF231C`, index `0x2B34`, byte `0x1B`,
and handler-table entry `0x0041C0` to `0x001B6EB2`. This turns the actor-pair
correlation into a concrete level-row/resource observation while preserving
the full register context in the trace artifact.

The response pass is also now classified. Static handler-table decoding maps
runtime Type `0x20` to `0x001AC350`, alongside Type `0x1E` at `0x001AC318`
and the player Type-`0x1E` handler at `0x001AE796`. From the activated pair
checkpoint, a neutral 180-frame replay executes the actor-collision pass but
enters none of those three handlers. A corrected one-frame C-button jump
(`none*1,c*1,none*178`) likewise leaves the player at `(4684,628)` with the
landing state clear and produces no collision-handler entry. The pair is
therefore still above the reachable collision band from this checkpoint; the
next route search should target the level geometry/connector that can place the
player near y≈466 rather than repeat direct collision probes.

The first collision reachability probe from the activated pair
(`c+left*20,left*70,none*90`) falls into the lower band instead of reaching
the actors above. The pair clears by relative frame 34, and the player lands
around `(4476,880)` with `SCENE_STATE=0x01`; this is negative reachability
evidence, not a collision result.

The current checkout now has its own reproducibility baseline in
`re/mame/campaigns/20260826-level01-reproducibility-v1.json`. It replays the
clean power-on route to the established upper-edge frontier with the same
input schedule as `recording-fresh-v2`, and `compare_state.py` reports an
exact match for all 2,297 frame records. The replay carries the current
repository commit, MAME submodule commit, ROM hash, sixteen named checkpoints,
and the complete frame-level state stream. This is a new provenance root for
future branches; no historical campaign was replaced or discarded.

The continuation is recorded in
`re/mame/campaigns/20260826-level01-reproducibility-extension-v1.json` with 160
verified checkpoints. It is a current-checkout replay chain through the lower
tower, far-floor guard, far vertical connector, upper-band dismount, Type-1E
wall opening, and upper interaction-row setup. The sword-assisted route to
`(4372,628)` is explicitly recorded before the Up-triggered pair appears at
`(4512,466)` and `(4688,466)`. A same-input attempt from the lower rope
endpoint is retained as a negative provenance branch rather than being
mistaken for the actor-pair setup. The two response probes enter none of the
three candidate collision handlers and leave `SCENE_STATE=0x01`.

The new opt-in terrain breakpoint context in `re/mame/lua/watches.lua` records
the resolver's live map word, floor byte, contour lookup, world coordinates,
and terrain flags at `0x001AD87E`, `0x001AD886`, and `0x001AD904`. The tower
crossing trace shows behavior `0x47` at the staircase/tower surface and lower
band responses afterward, but no scene-gate write.

The focused tower connector audit is recorded in
`re/mame/campaigns/20260826-level01-tower-connector-audit-v1.json`. From an
exact `(2715,770)` lamp-window checkpoint, eight Up/jump/sword/delayed-wall
timing families all remain in scene state `0x01`; the best delayed jump only
reaches world Y=`719`. The upper Type-1F actor is at Y=`658` with collision top
`739`, while the player collision top is `868`. The player dispatch table also
identifies runtime Type-0x20's handler at `0x001AE9C6`; a neutral and corrected
one-frame-C audit from the activated upper pair enters none of
`0x001AC318`, `0x001AC350`, `0x001AE796`, or `0x001AE9C6`. This closes the
direct lamp/collision hypothesis and moves the search back to the behavior-47
surface's object/connector source.

The upper-frontier direct-input search is now archived in three new campaign
manifests and a consolidated finding:
`20260826-level01-upper-frontier-search-v1` records twelve 360-frame timing
families from the verified upper-rope checkpoint;
`20260826-level01-upper-platform-jump-sweep-v1` records eleven fixed-position
ordinary-C probes across the behavior-25 band; and
`20260826-level01-upper-actor-jump-v1` records eleven directional-C and C+B
probes at the upper Type-1E/Type-20 actor line. Together they preserve 34
branches and 204 named checkpoints, with input schedules checked against the
captured frame streams. Every branch remains in `SCENE_STATE=0x01`; ordinary
C does not launch from the behavior-25 surface, and no tested branch reaches
the upper actor collision band or enters the candidate collision handlers.
This closes the current direct jump-timing family. The next investigation is
static/runtime decoding of the interaction-row and level-object records rather
than more timing variants from the same frontier.

The corresponding instruction-level dispatch replay is recorded in
`re/mame/campaigns/20260826-level01-upper-dispatch-trace-v1.json`. At the
activation frame, interaction rows `0x5658` and `0x5668` resolve to
`0x001B6E90` and `0x001B6EB2`; both enter the generic allocator at
`0x001B5266`, return through `0x001B5270`, and create the Type-1E/Type-20
pair at y≈466. The trace does not enter `0x001B535A` or the scene-actor
load/spawn targets, and the full replay remains in `SCENE_STATE=0x01`.
This closes the upper pair's dispatch path as a connector hypothesis. The
remaining work is the actual behavior-22/24 connector or a different route
branch that can bring the player into the pair's vertical interaction band.

The tower control matrix is recorded in
`re/mame/campaigns/20260826-level01-tower-control-matrix-v1.json` with eight
same-checkpoint branches and 32 named checkpoints. Neutral, attack, jump,
and Down variants remain on the behavior-0x47 lower-tower surface. Up-left
retreats to the previous lower band; Up-right exposes a real lower
continuation at world `(3039,912)`; and Down+lateral input settles near
`(2755,818)`. None reaches `SCENE_STATE=0x08` or writes the watched exit
gates. The finding is preserved in
`re/mame/findings/20260826-level01-tower-control-matrix-v1.json`.

The lower continuation is separately recorded in
`re/mame/campaigns/20260826-level01-lower-continuation-v1.json`. Sustained
rightward and jump-assisted branches traverse to the far-right lower edge
around `(4748,874)`, while the return branch reaches the known lower solid
boundary near `(2847,912)`. This is ordinary level traversal, not the exit:
the static exit predicate still requires world Y below `470`. The complete
negative/frontier result is in
`re/mame/findings/20260826-level01-lower-continuation-v1.json`.

The earlier `lamp-approach` checkpoint was also reopened in
`re/mame/campaigns/20260826-level01-tower-type43-interaction-v1.json`. It
starts with the actual type-0x43 object at `(2720,768)` before its local
conversion to type `0x8A`. Seven controller branches preserve this
conversion and the indirect edge trace to `0x001AE64C`, but no branch enters a
scene transition. This closes the remaining direct lamp interaction
hypothesis without deleting the reusable pre-conversion checkpoint; see
`re/mame/findings/20260826-level01-tower-type43-interaction-v1.json`.

The first upper opening alignment matrix is recorded in
`re/mame/campaigns/20260826-level01-upper-actor-alignment-v1.json`. Its six
same-checkpoint branches were approximate probes near the dynamic Type-0x1E
and Type-0x20 actors, not exact-coordinate tests. The authoritative exact
matrix is recorded in
`re/mame/campaigns/20260826-level01-upper-exact-opening-alignment-v2.json`.
It reaches Type-1E X=`4512` with `right*73` and Type-20 X=`4688` with
`right*160`, then tests Up, C, directional, and attack variants. No branch
enters the player collision handlers or scene state `0x08`; C falls normally
where applicable, while Up does not transfer from the behavior-0x25 band.
The complete exact result is in
`re/mame/findings/20260826-level01-upper-exact-opening-alignment-v2.json`.

The exact pair-y reachability control is recorded in
`re/mame/campaigns/20260826-level01-upper-pair-y-alignment-v1.json`. After
normal Up activation, the harness placed `PLAYER_Y` into the pair's y≈466
band at frame 200. The Type-1E proximity animation then cleared the pair;
neutral, Up, and attack branches produced no player/actor collision handler,
scene write, or exit gate. This is controlled evidence only, but it closes
the pair as the direct vertical connector. The nearby row-15 special-cell
probe is recorded in
`re/mame/campaigns/20260826-level01-upper-x4412-up-matrix-v1.json`; six
Up/C+Up/directional branches near x≈4412 likewise remain negative.

The consolidated route-geometry audit is recorded in
`re/mame/campaigns/20260826-level01-route-geometry-audit-v1.json`, with the
full result in
`re/mame/findings/20260826-level01-route-geometry-audit-v1.json`. It preserves
the replayable high-walkway drop, exact lower-edge rope-alignment attempts,
far-rope-top dismounts, shaft climb variants, lower-floor jump attempts, and
the previously missing plain-C jump near the row-10 platform. The new static
special-cell target also decompiles behavior `0x80/0x81` at `0x001B65BE` as a
no-op; behaviors `0x85/0x86` only set/clear the contour flag. These branches
remain in scene state `0x01` and do not reach the exit height, closing the
special-cell and direct-dismount hypotheses while preserving all checkpoints.

The static connector inventory is now recorded in
`re/mame/findings/20260826-level01-static-connector-inventory-v1.json`. The
extracted 300x45 level-01 terrain map contains four distinct vertical
connector bands: columns 98, 132, 170, and 296, with behavior `0x22` bodies
and behavior `0x24` upper endpoints. The controlled tower connector at
X≈2720 is spatially separate from the reachable behavior-`0x47` tower surface;
the static map therefore does not support a hidden connector-cell explanation
for the lower-tower gap. The remaining bridge must be a player response,
interaction/resource event, or a different route branch.

The current-harness boundary test is recorded in
`re/mame/campaigns/20260826-level01-exit-predicate-probe-v1.json`. A local
coordinate probe at world `(4748,460)` arms `0xFFF0E9=0xFF` from
`0x001B5B5E`, but the extended current-frontier run stays in scene state
`0x01` with script cursor `0x4082`. This is boundary-write evidence only; the
authoritative follow-on scene-state write remains the older controlled proof in
`20260826-level01-transition-v1`.

The natural lower-tower response trace is recorded in
`re/mame/campaigns/20260826-level01-natural-tower-response-trace-v1.json` and
summarized in
`re/mame/findings/20260826-level01-natural-tower-response-trace-v1.json`.
From the natural lamp-window checkpoint, `up+right*60,right*60,none*120`
reaches `(3039,912)` on the ordinary lower floor. The only unusual terrain
event is a short behavior-`0x2B` window at `(2902,849..885)` that sets
`PLAYER_TRANSITION_LOCK` for eight frames. Interaction rows `0x5610` and
`0x6C78` then create lower-band Type-0x20/Type-0x1E actors; no connector
handler, scene-resource rebuild, or exit gate is reached.

The second connector natural trace is recorded in
`re/mame/campaigns/20260826-level01-connector2112-trace-v1.json` and
`re/mame/findings/20260826-level01-connector2112-trace-v1.json`. From the
replayed lower-chamber checkpoint, `c+left*30,up+left*90,left*60,none*60`
reaches the static column-132 connector at X=`2104`: the player climbs from
Y=`846` to the upper stop at Y=`711`, and the requested handler at
`0x001B54D8` is hit 214 times (observed debugger PC `0x001B54DA`). The nearby
Type-0x36 actor is not a direct transition trigger in this branch;
`SCENE_STATE` stays `0x01`, and no write reaches `0xFFF0DC`, `0xFFF0E6`, or
`0xFFF0E9`. This closes the recorded left/up/left timing family at the
connector's upper stop while preserving its checkpoints for dismount tests.

The endpoint matrix is recorded in
`re/mame/campaigns/20260826-level01-connector2112-upper-stop-v1.json` and
`re/mame/findings/20260826-level01-connector2112-upper-stop-v1.json`.
Continuing Up reaches the actual behavior-`0x24` endpoint at `(2104,654)`;
C+Left returns to `(1946,690)`, C+Right falls to `(2176,912)`, and neutral C
returns to the endpoint. All four branches remain in scene state `0x01` and
write none of the scene exit gates, closing this connector as a local loop for
the tested dismount family.

The targeted resource-record decompilation is summarized in
`re/mame/findings/20260826-level01-resource-record-dispatch-v1.json`. The
resource helpers at `0x1B58F4` and `0x1B5938` create camera-relative Type-0x3A
and Type-0x34 actors; the `0x1B5320/0x1B53E0` paths allocate at the current
player position; and `0x1B54E0` is a scene-05-specific terrain-state block.
None installs a hidden behavior-0x22 connector or writes a scene state. The
X≈2720 gap is therefore still a dynamic behavior-0x47 response/interaction
question, not an unresolved static resource table entry.

The indirect edge trace for the natural behavior-0x47 branch is recorded in
`re/mame/campaigns/20260826-level01-behavior47-collision-edge-trace-v1.json`
and `re/mame/findings/20260826-level01-behavior47-collision-edge-trace-v1.json`.
From the tower-lamp checkpoint, `up+right*60,none*60` reaches the resident
Type-0x20 actor at approximately `(2804,818)`, then follows the shared
player-collision path through the transient Type-0x2D/Type-0x84 cleanup chain
before settling on the ordinary lower floor. The indirect breakpoint table
reaches the interaction and collision helpers but reaches neither the
behavior-0x29 launch target `0x1B557E` nor the behavior-0x2D bounce target
`0x1B56B6`; `SCENE_STATE` remains `0x01`. This closes the resident Type-0x20
contact family as a route explanation and makes the launch/bounce families and
the unresolved Type-0x1E actor the next natural targets.

The plain-C edge trace is recorded in
`re/mame/campaigns/20260826-level01-tower-plain-c-edge-trace-v1.json` and
`re/mame/findings/20260826-level01-tower-plain-c-edge-trace-v1.json`.
From the same lamp-window checkpoint, `c*1,up*120,none*119` only aligns the
player from X=`2715` to X=`2718`; it produces no jump, launch, bounce, or
scene-gate write across 240 frames. The launch target `0x001B557E` and bounce
target `0x001B56B6` are both absent. This closes the plain-C timing family at
this checkpoint and moves the investigation back to static scene-writer
coverage and the unresolved Type-0x1E interaction family.

The scene-writer coverage is recorded in
`re/mame/findings/20260826-level01-scene-writer-coverage-v1.json`. The
recovered writer set separates the level-01 boundary predicate at
`0x001B5B4A` (which only arms `FFF0E9=FF`) from the script writer at
`0x001A8ED2` and the table-state writer at `0x001B3F0A`. The initial script
record range contains no `0x08` state operand; the recovered `SCENE_STATE=08`
source is the scene-table entry at index 1, ROM `0x004B0E`. The controlled
boundary proof writes state `0x03` with table index `0`, while the controlled
state-08 selector writes `0x08` only after table index `1` is selected. The
remaining natural question is therefore how normal level progression reaches
that selector/index state.

The current-checkout boundary follow-up is recorded in
`re/mame/campaigns/20260826-level01-scene-writer-controlled-current-v1.json`
and `re/mame/findings/20260826-level01-scene-writer-controlled-current-v1.json`.
Using the current upper-frontier checkpoint and local fields that produce
world `(4748,460)`, native watchpoints reproduce
`0x001B5B5E -> FFF0E9=FF`, but no writer reaches `SCENE_STATE`,
`SCENE_SCRIPT_CURSOR`, or `SCENE_TABLE_INDEX` within 500 frames. This is a
current-fixture negative follow-on result; the older controlled state-03
campaign remains the authoritative observed script-writer path.

The scene-table index call path is recorded in
`re/mame/findings/20260826-level01-scene-table-index-call-path-v1.json`.
Static decompilation shows that `SCENE_TABLE_INDEX` is incremented only by
the scene-table selection path inside `Menu_RunOptionsAndSelectNextState`,
reached from the pending-script terminator in
`SceneScript_CompleteToState1`. The level-01 boundary instead arms the
countdown and takes the script writer to state `0x03` with table index `0`.
The state-`0x08` table entry is the second selector-cycle result at index `1`,
so it remains a separate scene-table/resource proof rather than the direct
natural level-01 exit target.

The broader startup/options role of the former `SceneTable_SelectNextState`
label at `0x001B3B96` is now recorded in
`re/mame/findings/20260828-options-menu-scene-table-v1.json`. The routine
initializes and services the options screen, consumes `MENU_SELECTION_INDEX`
for the difficulty/music/sound-test/exit branches, and only then performs the
scene-table publication. Its three small coordinate helpers at `0x001B43C4`,
`0x001B43E0`, and `0x001B43FC` are named by their exact marker-position
contracts; the menu actor's higher-level art identity remains open.

The menu media subservices are recorded in
`re/mame/findings/20260828-menu-soundtest-credits-v1.json`. The sound-test
screen at `0x001B4436` owns the indexed records beginning at ROM `0x12675E`:
`SOUND_TEST_ENTRY_PTR` advances in `0x10`-byte steps, the first byte queues the
selected audio command, and `SOUND_TEST_INPUT_REPEAT_TIMER` debounces the
navigation actions. Its redraw and previous/next/play helpers are now named
separately. The `0x001B4666` branch is the credits roll, consuming the
command/text stream at `0x127E8C` while servicing the actor and VDP frame
pipeline until its terminator.

The adjacent compact scene-resource loader variants are recorded in
`re/mame/findings/20260828-scene-resource-loader-variants-v1.json`.
`0x001B4896`, `0x001B48C4`, and `0x001B48F2` load exact two-resource VRAM
pairs, while `0x001B496C` loads the common base resource only. Their names
include the published palette source so each wrapper is distinguishable
without assigning an unproven scene or asset identity; the active palette and
C000 source pointers are now canonical RAM fields.

The remaining compact C000 loaders are recorded in
`re/mame/findings/20260828-scene-resource-c000-loaders-v1.json`. The five
wrappers at `0x001B498A`, `0x001B49B2`, `0x001B4A02`, `0x001B4A2A`, and
`0x001B4A52` each publish a distinct compressed C000 source, decompress it,
and prepare a frame from explicit palette pointers. They are source-qualified
until the scene table or runtime evidence establishes higher-level asset
identities.

The setup-time VRAM loader wrappers are recorded in
`re/mame/findings/20260828-scene-resource-setup-loaders-v1.json`.
`0x001B477C` loads a three-region VRAM set, `0x001B47AC` loads the E000
resource used by the scene setup path, and `0x001B47D0` loads the C000/base
pair. These names retain exact payload and destination evidence without
guessing which menu or scene art consumes each resource.

The menu input/control presentation helpers are recorded in
`re/mame/findings/20260828-menu-input-audio-v1.json`. The controller-release
predicate at `0x001B3548` is used by title/menu waits, the three-row renderer
at `0x001B4410` consumes the selected control-layout record beginning at
`0x004036`, and `0x001B329E` sends the fixed selection-change audio command
`0x06`. `MENU_CONTROL_LAYOUT_PTR` is now canonicalized alongside the existing
callback slots.

The hidden menu pattern path is recorded in
`re/mame/findings/20260828-menu-pattern-wish-v1.json`. The options-loop
consumer at `0x001B0BBE` advances the active two-byte input pattern and, on
the terminal record, sends audio command `0x02`, calls the wish-prompt
presentation at `0x001B3324`, clears VRAM, and enters transition mode. The
prompt stream at `0x126570` contains the David Perry question followed by
the ROM's level-choice strings; the later choice handling remains a separate
investigation.

The countdown-producer static audit is recorded in
`re/mame/findings/20260826-level01-countdown-producer-static-v1.json`. The
actor-collision table entries for receiving types `0x10`, `0x11`, and `0x13`
resolve to `0x001AC2BC`, `0x001AC4E8`, and `0x001AC1D0`; the first arms
`SCENE_SCRIPT_COUNTDOWN=0x20`, the second arms `0x14` after its timed cleanup,
and the third has no direct scene-countdown or scene-state assignment. The
known Level 01 runtime inventory contains none of those actor types.

The narrow runtime confirmation is recorded in
`re/mame/campaigns/20260826-level01-countdown-producer-audit-v1.json`.
Controller-only guard and juggler branches both complete with no write to
`0xFFF0E9`, no nonzero scene countdown, and no occurrence of types `0x10`,
`0x11`, or `0x13`. An initial actor-field watch attempt exited early because
the optional actor trace expands into a large debugger watch set; the clean
rerun uses only the single countdown watch and is the authoritative runtime
result. These generic combat producers are therefore not the current missing
Level 01 boundary path.

The next static/runtime correlation is recorded in
`re/mame/findings/20260826-level01-surface-mode-consumer-v1.json` and
`re/mame/campaigns/20260826-level01-surface-mode-audit-v1.json`. The landing
resolver at `0x001AD7B4` adds `TERRAIN_SURFACE_MODE` to the decoded terrain
resource base at `0x00FFAE84`: mode 0 uses `0x00FFAE84`, while mode 1 uses
`0x00FFAE85`. A natural frontier replay reaches behavior `0x47` at harness
frame 31, writes `FFF0C2=FF`, toggles `FFF0A4=1`, and the resolver breakpoint
observes the one-byte base shift. The Level 01 resource is selected at
`0x001B3434` from the record at `0x002C78 + 0x42 * level` and decoded by
`0x001B3818` into `0x00FFAE84`. This establishes the tower as a terrain
resource-mode switch, not a connector or scene writer. It also corrects the
older `0x001ADB34` label: that address is an empty return; the distinct
routine at `0x001ADB36` is not the landing resolver's direct callback.

The corrected Level 01 interaction inventory is recorded in
`re/mame/findings/20260826-level01-interaction-inventory-v1.json`. The live
interaction table is read at `0x00FFAE87`, which is resource offset 3 plus
`(map_word >> 1)`; the earlier offset-2 scan had conflated terrain behavior
bytes `0x80/0x81` with interaction selectors. Under the corrected decode the
300x45 map contains 175 interaction records and 22 unique selectors. The
runtime row processors, entered through wrappers at `0x001AE3FC` and
`0x001AE47E` and implemented at `0x001AE406` and `0x001AE488`, consume the upper
row-14 records at
map cells `(283,14)` and `(294,14)`, selectors `0x11` and `0x1B`, producing
the previously observed Type-0x1E/Type-0x20 pair. The only Level 01 `0x80`
interaction record is a separate actor-spawn path at `(249,14)`; row 15 has
no interaction selectors. Neither direct handler writes `SCENE_STATE=0x08` or
the scene countdown. The remaining high-value test is controlled activation
of the actual `0x80` record followed by tracing the actor it creates.

The concrete interaction dispatch bodies are now named separately from their
short directional wrappers: `InteractionTable_ProcessRowsA_Core` at
`0x001AE406` handles the 16-entry horizontal refill window, while
`InteractionTable_ProcessRowsB_Core` at `0x001AE488` handles the 23-entry
vertical refill window. Both decode `0x00FFAE87` selectors through the common
handler table with camera-relative coordinates.

The transfer-delay matrix is recorded in
`re/mame/findings/20260826-level01-superjump-delay-matrix-v1.json` and
`re/mame/campaigns/20260826-level01-canonical-recording-v11.json`. Ten complete
traces vary the second hold-jump delay from 18 through 40 frames. Every branch
remains in scene state `0x01`; the common peak is world `(4316,561)` at frame
219, and no branch arms `FFF0E9` or reaches the upper row-14 transfer. This
closes that specific timing family as a negative route result.

The controlled activation attempt for the sole Level 01 interaction selector
`0x80` is recorded in
`re/mame/findings/20260826-level01-actual-80-activation-v1.json`. The first
fixed-cell runs confirmed the runtime byte at `0x00FFD99F` but did not observe
`0x001B6F0C`; they also stayed inside the camera-refill deadband. The targeted
caller decompilation is recorded in
`re/mame/findings/20260826-level01-interaction-dispatch-callers-v1.json` and
shows that these row processors are refill-triggered, not unconditional
per-frame scans. A follow-up controlled refill run then observed the edge
`0x001AE46E -> 0x001B6F0C` at machine frame `0x118F`, proving the selector's
handler is live. The reproducible probe
`build/re/diagnostics/actual80-breakpoint-pointer-v2` then redirected
`INTERACTION_ROW_POINTER` (`FF7DAC`) to `FF22C2`, whose source word is the exact
target `0x5630`, and observed handler entry at machine frame `0x04DE` with
selector `0x80`. This is now an exact-record controlled dispatch proof, while
the natural Level 01 route and natural activation at `(3984,464)` remain
unresolved.

The next experiment is therefore to preserve a natural camera scroll across
the `(249,14)` window and trace the handler's generic spawn plus `FFF104` effect.

That side-effect trace is recorded in
`re/mame/findings/20260826-level01-actual-80-spawn-side-effects-v1.json`.
At machine frame `0x04DE`, selector `0x80` writes `FFF104=0`, enters the shared
spawn routine at `0x001B525E`, and initializes actor slot 20 (`FF8368`) as
runtime type `0x87` at `(3952,464)`. This confirms that the record creates an
actor rather than advancing the scene; the remaining Level 01 objective is
still a natural traversal to the exit predicate and subsequent scene change.

The natural camera-scroll audit is recorded in
`re/mame/findings/20260826-level01-natural-80-camera-scroll-v1.json` and
`re/mame/campaigns/20260826-level01-natural-80-camera-scroll-v1.json`. From the
clean far-floor checkpoint, the controller-only replay crosses world X=3984
at frames 166-167 while the player remains on the lower floor at Y=912. The
target row-14 actor position at Y=464 is never materialized; the selector
observer sees no 0x80 dispatch, the FFF104 write tap records zero writes, and
scene state remains 0x01. The exact-record controlled spawn proof therefore
stands, but it is not a natural lower-floor camera event. The route search now
returns to the upstream transfer that would place the player in the row-14
corridor.

The far-frontier direct-action audit is recorded in
`re/mame/findings/20260826-level01-upper-action-frontier-v1.json` and
`re/mame/campaigns/20260826-level01-upper-action-frontier-v1.json`. Ten
branches from the horizontal and vertical far-rope checkpoints cover A/B/C,
Up, Left, Right, jump, sword, and combined action timing. The endpoint either
holds around `(4728,671)`, remains on the upper line around `(4728,661)`, or
falls to the lower band around `(4750,874)`; every branch remains in scene
`0x01`. A focused breakpoint run reaches the action selector and the Level 01
predicate but does not enter the launch, connector, terminal-collision, exit,
or scene-state writer paths. The generated static decompilation is archived by
`re/ghidra/targets/level01-far-exit-transfer-targets.json` and identifies the
coordinate gate as the remaining prerequisite. The direct far-rope action
hypothesis is closed; the unresolved route search now returns upstream to the
level-object/resource activation path around the row-10 upper platform.

The row-10 upper-platform selector `0x87` dispatch is recorded in
`re/mame/findings/20260826-level01-upper-platform-selector87-v1.json` and
`re/mame/campaigns/20260826-level01-upper-platform-selector87-v1.json`. In a
continuous power-on replay, the live refill boundary was redirected to the
exact source word `0x52E8` at `FF22C2`, with runtime selector byte `0x87` at
`FFD7FB`. The row processor reached `0x001B74D6`, entered the generic spawn
allocator at `0x001B525E`, and initialized slot 20 from template `0x001B7A30`
as runtime Type `0x01` at `(4240,384)` with animation `0x00122D92`. The
interaction cell was cleared and scene state remained `0x01`. This proves the
row-10 record is an actor-spawn resource, not a direct scene transition; the
natural approach and the object/resource activation that makes the upper route
reachable remain open.

The controlled far-rope wall-interaction trace is recorded in
`re/mame/findings/20260826-level01-controlled-rope-wall-interaction-v1.json`.
From the valid wall checkpoint, `C+Right` reaches the behavior-0x22 connector
band, briefly dispatches selector `0x60` through `0x001B735E`, and returns to
the rope at world `(2712,453)`. No boundary, scene-script, scene-state, or
scene-table writer fires; selector `0x51` is not reached. This closes the
tested upper-wall dismount family as a local connector interaction and leaves
the natural object/resource activation upstream as the next route question.

The lower-wall terrain response is recorded in
`re/mame/findings/20260826-level01-lower-frontier-terrain-response-v1.json`.
At world `(2846,912)`, the edge probe `0x001AD632` sees map row 40/column 177
word `0x2450` and a blocking behavior byte `0xFF`, then sets
`TERRAIN_STOP_LEFT_MOTION=0xFF`. Neutral and held-left traces remain fixed at
the wall with `SCENE_STATE=0x01`; no actor, scene-script, scene-state, or exit
writer fires. This closes the lower-wall input family as geometry and returns
the route search to the upstream resource/object activation path.

The Type-0x01 upper-platform resource chain is recorded in
`re/mame/findings/20260826-type01-child-chain-v1.json` and
`re/mame/campaigns/20260826-type01-child-chain-v1.json`. Static decoding of
`ACTOR_ANIM_TYPE01` at `0x00122D92` shows a stationary frame at
`0x001F7DC0`, a random branch, and an F5 record that uses template
`0x001B7D00` with signed offset `(+22,-12)`. The child template is Type
`0x84`, has no movement stream, cycles five small effect frames, toggles
actor flag `+0x07` bit `0x20`, and then clears itself. A corrected controlled
injection confirms the child at `(4262,372)` from the parent at `(4240,384)`;
scene state remains `0x01` and no scene/exit writer fires. This removes the
row-10 Type-0x01 chain from the missing lift/exit hypotheses.

The Type-0x01 upper-resource and Type-0x84 child animation family is recorded
in `re/mame/findings/20260829-type01-type84-child-animation-static-decompilation-v1.json`.
Lossless decoding range-bounds the parent at `0x00122D92-0x00122DB1` (32
bytes), including its stationary `0x001F7DC0` frames, random branch, and exact
F5 child record for `ACTOR_TEMPLATE_TYPE84_CHILD`. The child is independently
bounded at `0x00122F38-0x00122F7F` (72 bytes), with its five paired frames,
`F600` cleanup, flag-20 callback parameters, and local-entry loop. The
adjacent `0x00122DB2` and `0x00122F80` fragments remain explicitly unclaimed
by these roots rather than being absorbed by proximity.

The Type-0x34 wall actor animation is recorded in
`re/mame/findings/20260829-type34-wall-animation-static-decompilation-v1.json`.
The selector-0x53 runtime installation path now owns the exact
`0x00122C1E-0x00122C3F` range (34 bytes): four `0x12E2` frame references,
the `0x12E6/0x12EA/0x12EE` response frames, EE timer values, the F0 random
branch, and the EA loop through `0x00122C20`. The separate movement root at
`0x001217B4` remains independently bounded.

The shared Type-0x8D/Type-0x76/terminal animation block is recorded in
`re/mame/findings/20260829-type8d-type76-terminal-animation-static-decompilation-v1.json`.
The Type-0x8D response owns its packed prefix at `0x001240CE-0x00124193`,
including the four embedded F5 child entries and the `F600` response cleanup.
The direct Type-0x76 template root is independently named and bounded at
`0x00124194-0x001241C7`, including its alternate entry at `0x00124198`.
Finally, `Level_ExitAndTerminalTransition`'s direct assignment at
`0x001AFA76` selects the separate 48-byte terminal alternate at
`0x001241C8-0x001241F7`, immediately before the Type-0x89 stream. The old
224-byte Type-8D decoder total is retained as a traversal fact because it
walks through the shared Type-0x76 continuation; it is not used as an
overlapping layout range.

The Type-0x0A guard parent animation is recorded in
`re/mame/findings/20260829-guard-attack-animation-static-decompilation-v1.json`.
The exact `0x0012542A-0x0012548D` range (100 bytes) presents the guard frame
groups and facing clears, gates the Type-0x2D child on the player-X distance,
and preserves both F5 payloads at `0x0012545E` and `0x00125474`. The primary
payload installs `ACTOR_MOVE_GUARD_SWORD_ATTACK` while the alternate retains
the template default movement; the terminal `0x152E` pair loops back to the
guard root immediately before `ACTOR_ANIM_TYPE52_INTERACTION_BASE`.

The Type-0x84 death animation family is recorded in
`re/mame/findings/20260829-type84-death-animation-static-decompilation-v1.json`.
The root `ACTOR_ANIM_DEATH_122FA2` now owns the exact
`0x00122FA2-0x00123013` prefix (114 bytes), including both embedded death
template F5 records and the inline continuation stubs. The shared
`ACTOR_ANIM_DEATH_84_SHARED_CONTINUATION` entry owns
`0x00123014-0x0012312B` (280 bytes), publishes the type-0x84 terminal state
and movement stream `0x00120352`, and branches through the internal
`0x00123130` entry of the separately owned Type-0x5F stream.

The player response streams are recorded in
`re/mame/findings/20260829-player-response-animation-static-decompilation-v1.json`.
`PLAYER_ANIM_LEVEL_EVENT_PRESENTATION` now owns the exact
`0x001258D2-0x00125915` loop (68 bytes) reached after the Type-0x84 level-event
child clears itself. `PLAYER_ANIM_TERRAIN_RESPONSE_SHARED` owns
`0x00125E72-0x00125EED` (124 bytes), presents the tripled 0x0A32-0x0A4E
sequence, and is the shared response branch reached from the bounce stream at
`0x00121AD8`. The adjacent Type-0x0C and terminal-terrain boundaries remain
separate.

The shared Type-0x29 transition animation is recorded in
`re/mame/findings/20260829-type29-transition-shared-animation-static-decompilation-v1.json`.
`ACTOR_ANIM_TYPE29_TRANSITION_SHARED` now owns the exact
`0x00121D5A-0x00121D87` loop (46 bytes). It is used by the Type-0x29
interaction template and is also the continuation reached from the transition
presentation stream at `0x00123352`; the following `0x00121D88` entry remains
separate.

The player idle block is recorded in
`re/mame/findings/20260829-player-idle-animation-static-decompilation-v1.json`.
`PLAYER_ANIM_IDLE_PREROLL` owns the exact 18-byte `0x00121D88-0x00121D99`
prefix, which presents frame `0x09D2` nine times before falling through to
`PLAYER_ANIM_IDLE`. The idle root now owns
`0x00121D9A-0x00121F39` (416 bytes), including its terrain/interaction
response branches, conditional Type-0x46 F5 spawn, and local loop.

The player interaction-pair setup stream is recorded in
`re/mame/findings/20260829-player-interaction-pair-animation-static-decompilation-v1.json`.
`PLAYER_ANIM_INTERACTION_PAIR_SETUP` owns the exact
`0x00121BB6-0x00121C27` region (114 bytes), including sound `0x2F`, both
conditional F5 records targeting the embedded `0x00123158`/`0x00123166`
entries of the Type-0x84 companion stream, and the terminal dynamic state
selection at `0x00121C26`.

The player terrain-bounce response root is recorded in
`re/mame/findings/20260829-player-terrain-bounce-animation-static-decompilation-v1.json`.
`PLAYER_ANIM_TERRAIN_BOUNCE_ROOT` owns the exact
`0x00121AD8-0x00121BB5` region (222 bytes), including the actor-state write,
the `0x093E-0x0956` bounce frames, repeated handoffs to the shared
`PLAYER_ANIM_TERRAIN_RESPONSE_SHARED` stream, and the terminal `0x087E-0x088A`
loop. The interaction-pair setup stream begins immediately at `0x00121BB6`.

The player terrain-alignment response stream is recorded in
`re/mame/findings/20260829-player-terrain-alignment-animation-static-decompilation-v1.json`.
`PLAYER_ANIM_TERRAIN_ALIGNMENT_RESPONSE` owns the exact
`0x00121964-0x00121AC7` region (356 bytes), selected by
`TerrainHandler_SnapToGridBlock` after it aligns the player to the terrain
grid. Its transition/terrain gates, encoded timer phases, randomized frame
loops, and separate dynamic selector at `0x00121AC8` are retained without
absorbing the following terrain-bounce root at `0x00121AD8`.

The terminal-transition presentation streams are recorded in
`re/mame/findings/20260829-terminal-transition-animation-static-decompilation-v1.json`.
`ACTOR_ANIM_TYPE84_TERMINAL_TRANSITION_PRIMARY` owns the exact
`0x00121CB0-0x00121CCD` region (30 bytes) loaded by the primary temporary
actor template, while `ACTOR_ANIM_TYPE84_TERMINAL_TRANSITION_SECONDARY` owns
`0x00121CCE-0x00121D59` (140 bytes) loaded by the secondary template. Their
timer/sound sequences and boundaries are explicit; the shared Type-0x29
transition stream begins immediately at `0x00121D5A`.

The player transition-response entries are recorded in
`re/mame/findings/20260829-player-transition-response-animation-static-decompilation-v1.json`.
`PLAYER_ANIM_TRANSITION_FLAG_RESPONSE` owns the exact
`0x00121C62-0x00121CAF` prefix selected by the nonzero `PLAYER_TRANSITION_FLAG`,
then hands off through EA to the shared 14-byte
`PLAYER_ANIM_TERRAIN_BOUNCE_PRELUDE` at `0x00121ACA`. The prelude branches
through `TERRAIN_RESPONSE_LATCH` to the shared terrain-response stream or
falls through to the separately owned bounce root at `0x00121AD8`.

The adjacent terrain-handler inventory is recorded in
`re/ghidra/targets/level01-terrain-handler-inventory-targets.json`. The
behavior-0x24 upper-stop handler at `0x001B54D2` sets both terrain query-state
bytes and therefore blocks upward continuation; behavior-0x27 at
`0x001B54A6` is the distinct one-shot `PLAYER_Y -= 0x50` interaction response,
while behavior-0x2A at `0x001B55D8` only nudges `PLAYER_X` and subtracts from
`PLAYER_VX`. The player response routines consume these flags but do not write
scene state. This separates the static upper-stop/vertical-response family
from the unresolved route transfer.

The Type-0x2A collision classification is recorded in
`re/mame/findings/20260826-level01-actor-type2a-decomp-v1.json`. A later table
audit corrected its dispatch attribution: entry `0x001D66` contains
`0x001AE9C6`, the shared handler used by Types `0x0A`, `0x1D`, and `0x20`,
while the empty return `0x001AEBFE` belongs to Type `0x2B` at `0x001D6A`.
`re/mame/findings/20260826-player-collision-table-correction-v1.json` records
the raw-byte cross-check. Type `0x2A` remains unproven as a traversal object,
but it must not be classified as statically non-collidable.

The integrated upper-frontier boundary replay is recorded in
`re/mame/findings/20260826-level01-integrated-natural-exit-v1.json` and
`re/mame/campaigns/20260826-level01-integrated-natural-exit-v1.json`. With
`right*160,up*120,none*40,right*40,none*600` and no memory pokes, the player
reaches world `(4746,628)`: the X half of the Level 01 boundary predicate is
satisfied, but the required `Y<470` half is not. The player falls to
`(4750,874)` and `SCENE_STATE` remains `0x01`. This corrects the earlier
interpretation of the pair-Y fixture, whose `(4688,466)` position depended on
an explicit `PLAYER_Y/PLAYER_VY` poke; it is controlled evidence only.

The type-0x1F lower-tower settle trace is recorded in
`re/mame/findings/20260826-level01-type1f-settle-v1.json` and
`re/mame/campaigns/20260826-level01-type1f-settle-v1.json`. From the clean
tower-lamp checkpoint, 600 frames of no input let the proximity actor complete
and repeat its animation cycle. It moves only horizontally from `(2712,658)`
to `(2684,658)`, cycles flag bit `0x20`, and never acquires a vertical movement
state or changes `SCENE_STATE` from `0x01`. The shared player handler and actor
cleanup handler contain no launch or scene-transition path, so this closes the
delayed type-0x1F tower hypothesis.

The selector-0x1A type-0x21 high-walkway settle trace is recorded in
`re/mame/findings/20260826-level01-type21-settle-v1.json` and
`re/mame/campaigns/20260826-level01-type21-settle-v1.json`. From the verified
high-walkway entry, 600 frames of no input leave the actor fixed at
`(1712,530)` with movement PC zero. Its proximity animation cycles flag bit
`0x20`, clears around frame 302, and never changes the gameplay scene state
from `0x01`. Static decoding maps selector `0x1A` to the type-0x21 spawn
handler, whose player/actor collision entries are shared cleanup paths; this
actor is not the missing connector or scene-transition source.

The fresh selector-0x12 dispatch campaign is recorded in
`re/mame/findings/20260826-level01-selector12-dispatch-v1.json` and
`re/mame/campaigns/20260826-level01-selector12-dispatch-v1.json`. The natural
route reaches the generic handler at `0x001B6E7A` seven times, returning through
`0x001AE46E` and allocating ordinary type-0x1D/type-0x40 route actors. It does
not write `SCENE_STATE=0x08`, arm the scene countdown, or reach the exit
predicate; the run stalls below the required upper-band Y condition. This
closes selector `0x12` as the direct transition source while retaining the
named checkpoints as provenance anchors.

The scene-countdown writer audit is recorded in
`re/mame/findings/20260826-level01-scene-countdown-writers-v1.json` and
`re/mame/campaigns/20260826-level01-scene-countdown-writers-v1.json`. The
targeted Ghidra pass covers the remaining countdown stores, and a fresh natural
route watchpoint run observes no gameplay write to `SCENE_SCRIPT_COUNTDOWN`.
The Level 01 exit predicate at `0x001B5B4A` executes repeatedly but its
`X>0x1287, Y<0x1D6` condition remains false; `SCENE_STATE` stays `0x01`.
The other changing gates are terrain or death bookkeeping, so the missing
work is the upper transfer that must satisfy the exit predicate, not another
countdown producer.

The fresh natural gate trace is recorded in
`re/mame/findings/20260827-level01-natural-gate-trace-v1.json` and
`re/mame/campaigns/20260827-level01-natural-gate-trace-v1.json`. From power-on,
the route reaches world `(2564,920)` by frame 2296 and remains in gameplay
scene state `0x01`. The exit predicate executes 1105 times from frame 1190
through 2294, but its `X>0x1287, Y<0x1D6` condition is never satisfied; there
are no gameplay writes to `SCENE_STATE`, `SCENE_SCRIPT_COUNTDOWN`, or
`0xFFF0E9`. The changing `0xFFF0D0` and `0xFFF0DB` fields remain local terrain
bookkeeping, so this closes another direct gate/countdown probe and preserves
the fresh trace as the early-route provenance anchor.

The targeted resource-dispatch decompilation is summarized in
`re/mame/findings/20260827-level01-resource-dispatch-decomp-v1.json`. The
`0x001B58F4-0x001B5B10` family consists of camera-relative or fixed-local actor
allocators; `0x001B5938` specifically materializes type `0x34` with animation
`0x00122C1E`, movement `0x001217B4`, and resource count 6. The range does not
contain a hidden terrain/scene loader, so the next productive runtime target
is the behavior-`0x47` terrain callback and player-response path.

That terrain callback pass is recorded in
`re/mame/findings/20260827-level01-terrain-response-callback-v1.json` and
`re/mame/campaigns/20260827-level01-terrain-response-callback-v1.json`. A new
directional branch from the tower checkpoint reaches behavior `0x47` 111
times, toggles `TERRAIN_SURFACE_MODE` once and arms
`TERRAIN_SURFACE_LATCH`, then falls back to world `(2702,920)` with ordinary
landing state `0x09`. The decompilation confirms that this handler only
selects an adjacent contour byte; there are no writes to `SCENE_STATE`, the
scene countdown, or `0xFFF0E9`. Behavior `0x47` is therefore closed as a
direct vertical-transfer source.

The lower behavior-band runtime pass is recorded in
`re/mame/findings/20260827-level01-behavior46-lower-band-runtime-v1.json` and
`re/mame/campaigns/20260827-level01-behavior46-lower-band-v2.json`. Replaying
the canonical `far-floor-entry.sta` route with injected `b+right*520` reaches
the Level 01 behavior-`0x46` band at world `(3225,912)`: Type `0x1E` slot 11
publishes interaction state `0x46` for frames 401–495, then installs movement
stream `0x0012046C` from frame 405 onward. A branch from the saved frame-400
checkpoint isolates the continuation and reaches the callback requested at
`0x001ACC5E` (MAME reports the post-instruction PC as `0x001ACC60`) seven
times. Both traces remain in scene `0x01`; no scene-state or scene-countdown
write occurs. This is the first positive natural correlation for the extended
actor branch and confirms it as terrain/contact behavior rather than a direct
scene-transition selector.

The follow-up movement decode is recorded in
`re/mame/findings/20260827-level01-behavior46-lower-band-movement-decode-v1.json`.
The complete `0x0012046C` stream is a short actor-local loop: it retains the
actor while interaction state `0x46` is present, advances by `+1`, `+3`, and
`+2`, checks player-X proximity and actor fields, and clears movement while
selecting animation roots `0x00123614` or `0x001237C6` on its exit paths. A
30-frame handler trace joins this with the live record: the predecessor
`0x001204E2` stream selects `0x001237C6`, the animation writes
`0x0012046C`, and the actor reaches the deferred callback at
`0x001ACC5E`. The branch is now classified as a local actor response and is
removed from the direct player-transfer/scene-exit hypothesis set.

The C-button bounce matrix is recorded in
`re/mame/findings/20260827-level01-bounce-c-input-matrix-v1.json` and
`re/mame/campaigns/20260827-level01-bounce-c-matrix-v1.json`. Seven injected
variants from the retained `bounce-approach.sta` checkpoint place a C pulse at
different points through the bounce, plus one eight-frame C hold. All seven
traces retain the handhold actor as type `0x6A`; none reaches the handler's
type-`0x6B` transfer. The shared vertical path is unchanged and all runs
settle on the lower floor in scene `0x01`; C timing changes horizontal travel
only. This closes the C-timing version of the bounce-to-handhold hypothesis.

The player-motion writer audit is recorded in
`re/mame/findings/20260827-level01-player-motion-writer-audit-v1.json`. The
complete player-collision dispatch table identifies the remaining handlers
that can directly write player coordinates or velocity: the `0x11/0x12`
launch family is absent from Level 01, the `0x50/0x51` alignment family belongs
to level-table entry 9, and the `0x67/0x68` alignment family appears in the
Level 0A matrix rather than Level 01. The live `0x65/0x66` bounce and
`0x69..0x6C` handhold families are already covered by natural traces. No
unclassified Level 01 actor-specific player-motion writer remains, so the
next target is the upper-route object/resource producer and its terrain
placement.

The early high-walkway handhold campaign is recorded in
`re/mame/findings/20260827-level01-early-highledge-handhold-route-v1.json` and
`re/mame/campaigns/20260827-level01-early-highledge-handhold-v1.json`. Starting
from the saved `high-walkway-entry` checkpoint, the descent reaches world
`(1592,532)` near frame 36 and materializes slot 24 as type `0x6A` at
`(1656,703)` on frame 65. Applying `C+Right` through the aligned window changes
the actor to type `0x6B` on frame 74 at player world `(1658,695)`, confirming
the early handhold route that the far-right upper-shelf searches had missed.
The positive `C+Up+Left` continuation makes an ordinary `vy=-704` launch,
reattaches to the x=1568 behavior-`0x22` connector around frame 108, and
climbs to its behavior-`0x24` top at world `(1560,479)` by frame 220.
No tested post-grab branch produces the `-0x0500` superjump, hidden ledge,
exit predicate, or scene-state write; gameplay remains `SCENE_STATE=0x01`.
This records a positive route frontier and shifts the next target to the
top-of-connector interaction/resource producer and its hidden-ledge condition.

The lower-tower route join is recorded in
`re/mame/findings/20260827-level01-route-join-v1.json` and
`re/mame/campaigns/20260827-level01-route-join-v1.json`. A clean chained replay
from the lower-tower-entry checkpoint reverses left to the staircase ledge at
world `(2271,832)`, crosses the tower band, and reaches the far lower floor.
Immediate downward sword branches clear the overlapping guards at approximately
`x=3450`, `x=3627`, and `x=4704`, each changing the active enemy to cleanup type
`0x84`. From the resulting far-guard checkpoint, `C+Left` reaches the vertical
rope contact at `(4721,816)`; Up held from that synchronized boundary climbs to
the canonical rope top `(4728,671)` with terrain behavior `0x24`. The gameplay
scene remains `SCENE_STATE=0x01`, so the remaining frontier is the upper-band
transfer and natural exit predicate above the rope.

The upper-band pulse branch is recorded in
`re/mame/findings/20260827-level01-upper-band-pulse-v1.json` and
`re/mame/campaigns/20260827-level01-upper-band-pulse-v1.json`. From the valid
rope-top checkpoint, `C+Left` reaches the behavior-`0x25` horizontal line at
world `(4614,628)`. The recorded sword window converts the type-`0x0A` juggler
to terminal type `0x84`, after which the player drops to the lower gap at
`(4121,912)`. Three timed C pulses end at `(3293,912)` on the lower floor;
`SCENE_STATE` remains `0x01` and no transition-gate write occurs. This closes
the line/juggler/pulse family as an upper-transfer solution and moves the next
target to static decoding of the upper-band resource/interaction producer.

The early connector-top frontier is recorded in
`re/mame/findings/20260827-level01-early-top-frontier-v1.json` and
`re/mame/campaigns/20260827-level01-early-top-frontier-v1.json`. Six controlled
branches from world `(1560,479)` show that plain directional input leaves the
behavior-`0x24` endpoint fixed, while C-jump branches reach the ordinary high
walkway and fall near x≈2277 without reaching the row-10 exit band. The
companion initializer/row trace identifies the natural lower bounce producer:
selector `0x74` at `0x001B670C`, and template `0x001B7E54` in slot 20 at world
`(1576,900)`. Static floor decode places that selector at map row 41/column 100
(source word `0x7098`); the runtime row-context cursor reports adjacent/current
word `0x7078`. No
hidden traversal actor or scene-state write appears in the upper-walkway
branch. This closes the direct connector-top timing family and redirects the
search to the other vertical connector/resource records.

The far-rope contact correction is recorded in
`re/mame/findings/20260827-level01-far-rope-up-contact-v1.json` and
`re/mame/campaigns/20260827-level01-far-rope-up-contact-v1.json`. The timing
sweep proves that the far vertical rope is reachable: Up must be held through
the behavior-`0x22` contact window at world `(4728,739)` and the player then
climbs to the behavior-`0x24` endpoint at `(4728,671)`. Starting Up early,
mid-window, or late produces lower-band negative controls. The endpoint
follow-up is recorded in
`re/mame/findings/20260827-level01-far-rope-endpoint-v1.json` and
`re/mame/campaigns/20260827-level01-far-rope-endpoint-v1.json`; direct neutral,
jump, and sword dismounts return to the known upper band or fall, with no
scene-state or exit-gate write. The endpoint is now the synchronized base for
the remaining upper-transfer search.

## 2026-08-27 actor lifecycle extension

The synchronized Level-01 actor replay now carries the next lifecycle boundary
set. Static disassembly and the controlled refill trace identify
`Actor_HandleType40Interaction` at `0x001AF468` replacing the type-`0x40`
record with template `0x001B7ABC` (type `0x84`, animation root
`0x00122F80`). The first frame transition at `0x00122F8A` flips X-facing;
slot 7 and slot 11 reach that transition at aligned frames 527 and 538. The
resource sweep clears the short slot-10 child at frame 525 and slot 6 at frame
523.

The cull handler at `0x001AE0B0` clears the type/resource links without
zeroing the compact movement-loop words, so native culling now preserves those
fields for a later slot reuse. F5 template `0x001B7AF8` is the type-`0x2A`
route actor (`movement=0x00120F76`, `animation=0x001231DC`). Its first
animation command `FA 42 00 18 02A4` writes the horizontal accumulator; the
accumulator is now part of the actor animation VM state, while movement
integration remains on the following boundary. The same pass corrected the
in-bounds class-zero terrain path: ROM `0x001ADE1E` adds `0x78` to vertical
acceleration but does not rewrite movement flag bit 0 to bit 6 (that rewrite is
only the out-of-range `0x001ADE10` path).

The latest `level01-actor-boot` replay matches all 600 requested post-checkpoint
action frames. The comparison tool also sees one extra synchronized terminal
state and reports an unrelated frame-601 actor-timer/player-position edge;
that sentinel is outside the requested input window. Details are recorded in
`re/mame/findings/20260827-level01-actor-lifecycle-extension-v1.json` and
`re/mame/campaigns/20260827-level01-actor-lifecycle-extension-v1.json`.

The corrected live-camel route is recorded in
`re/mame/findings/20260827-level01-camel-handhold-connector-v1.json` and
`re/mame/campaigns/20260827-level01-camel-handhold-connector-v1.json`.
Starting from the hidden-ledge checkpoint, `right*30,none*20,c*40` reaches
the live Type-0x65 camel at frame 74. Holding C during that descent changes
the launch to `vy=-1328`; steering right then reaches the Type-0x6A handhold,
which becomes Type 0x6B at world `(1658,695)` on frame 114. Issuing
`C+Up+Left` immediately after the completed grab reattaches to the x=1568
behavior-0x22 connector and reaches its behavior-0x24 top at `(1560,479)`.
Both segments remain in `SCENE_STATE=0x01`. This corrects the earlier
provenance mistake: the high-walkway-entry checkpoint did not contain the
live camel; the hidden-ledge descent is the required producer path.

The loaded-state trace harness fix is recorded in commit `7417cce`:
preloaded MAME states can restore an already-consumed emulated-time budget, so
the wrapper now supplies a generous wall-clock ceiling whenever a state is
loaded. The Lua frame limit remains authoritative. This makes long traces
from named checkpoints reproducible instead of silently ending after a few
frames.

The upper Type-0x1E/Type-0x20 pair's VM boundary is recorded in
`re/mame/findings/20260827-level01-upper-pair-vm-trace-v1.json` and
`re/mame/campaigns/20260827-level01-upper-pair-vm-trace-v1.json`. A 180-frame
neutral replay from the synchronized activation checkpoint produced 179
actor-collision breakpoint hits, but no Type-0x1E or Type-0x20 handler entry,
animation ED/FB dispatch, callback entry, or scene-gate write. The pair's
animation cursors advance locally while `SCENE_STATE` stays `0x01`; this
closes the direct neutral VM-probe family and leaves the unresolved work at
the connector/terrain producer boundary.

The delayed early-top dismount campaign is recorded in
`re/mame/findings/20260827-level01-early-top-delayed-dismount-v1.json` and
`re/mame/campaigns/20260827-level01-early-top-delayed-dismount-v1.json`. Seven
branches wait 0, 1, 5, 10, 20, 30, or 45 frames at the synchronized
behavior-`0x24` endpoint, then hold C+Right for ten frames. The immediate and
delayed branches all take the ordinary high-walkway arc; the best reaches
world y=`0x19C` (412), while the later branches simply have less remaining
travel time. No branch enters another connector, reaches the Level 01 exit
predicate, writes a scene gate, or leaves `SCENE_STATE=0x01`. This closes the
delayed-C family and redirects the search to an interaction/resource producer
that can change actor or terrain state.

The far-rope upper resource producer is recorded in
`re/mame/findings/20260827-level01-far-rope-upper-resource-trace-v1.json` and
`re/mame/campaigns/20260827-level01-far-rope-upper-resource-trace-v1.json`.
The natural upward dismount from the synchronized far-rope endpoint reaches the
upper interaction rows at relative frame 57 (MAME debugger frame `0x1403`).
Row word `0x5658` / runtime index `0x2B2C` selects `0x11`, dispatches through
`0x001B6E90`, and initializes slot 4 from template `0x001B7C24` as runtime
Type `0x1E`. Row word `0x5668` / runtime index `0x2B34` selects `0x1B`,
dispatches through `0x001B6EB2`, and initializes slot 5 from template
`0x001B7C10` as runtime Type `0x20`. The full 300-frame route and an 80-frame
register/row-context replay agree on the producer path. Neither replay writes
scene state, the scene cursor, a countdown, or an exit gate; both remain in
`SCENE_STATE=0x01`. This confirms the pair's natural producer and closes the
hypothesis that it is itself the missing transition trigger. The next search is
the remaining upper-band interaction/resource records and any producer capable
of changing traversal or setting the Level 01 countdown.

The complete upper-band inventory is recorded in
`re/mame/findings/20260827-level01-upper-band-inventory-v1.json`. Static decode
of map columns 275–299 (`x=4400–4799`) finds seven interaction records: the
known `0x11`/`0x1B` Type-`0x1E`/Type-`0x20` pair, four selector-`0x60`
Type-`0x40` cleanup/support records, and the already classified selector-`0x12`
generic path. The row-15 `0x80`/`0x81` terrain cells both dispatch to the
shared no-op/default handler at `0x001B65BE`; the `0x22`, `0x24`, and `0x25`
connector/contour bands are already classified. No unclassified upper-band
table entry remains that can explain the missing traversal or countdown, so
the search now moves upstream to the high-ledge/moving-platform launch producer
and its player-coordinate/terrain callbacks. A clean preloaded replay from the
far-rope endpoint (`none*1,c+up*20,up*100`) confirms the same boundary at
runtime: the player leaves behavior `0x24`, makes an ordinary `PLAYER_VY=-0x200`
jump, settles on behavior `0x25` at `(4728,628)`, and never reaches the behavior
`0x29`/`0x2D` launch targets or a scene/countdown gate. The trace is retained at
`build/re/analysis/20260827-level01-upper-band-inventory-v1/runtime-clean`.

## Player AnimationVM cursor lifecycle (20260828)

The serialized decompiler reports for `0x001A8C16`, `0x001A9304`, `0x001A9502`,
`0x001AC784`, `0x001AD150`, and the complete `PLAYER_ANIMATION_PC` /
`PLAYER_ANIMATION_TIMER` writer closure establish the player animation order.
`Game_FrameUpdateLoop` increments `FRAME_PHASE_COUNTER`, runs the locomotion and
action selectors, terrain response and collision handlers, and only then enters
the single common `AnimationVM_TickActors` traversal. `0x001AC784` walks the
player record at `0x00FF7E40` as slot 0, reads its frame reference through the
cursor at `0x00FF7E60`, decrements `0x00FF7E77` when nonzero, otherwise executes
the command stream and writes the advanced cursor back to the same record.

The writers at `0x001A9304`, `0x001A9502`, `0x001A9716`, `0x001A9D98`,
`0x001ABB40`, `0x001AD7B4`, `0x001AE796`, `0x001AEC00`, `0x001AFBF4`,
`0x001B1F28`, `0x001B1FAE`, `0x001B1FFE`, `0x001B5502`, `0x001B55E8`, and
`0x001B56B6` all publish a new root and clear the timer as part of gameplay
selection/response handling before the common traversal. `0x001AD150` clears
the timer and animation gate but does not introduce a second cursor or service
phase. There is no ROM-side pending cursor and no ROM-side service-boundary
enum.

The native player path now follows that ownership directly. Physical apple
selection is made before ordinal 30, so the VM consumes `0x001223DA` itself and
the live cursor advances through `0x001223E2`, `0x00122438`, and `0x0012245C`
without a shadow cursor or apple-specific service state. The former native
`pending_animation_pc_` and player `service_boundary_` fields were removed;
the surviving `actor_service_boundary_` is explicitly limited to producer gates
on non-player actor VM instances. `tests/native_scheduler_trace.py` locks the
apple cursor sequence and focused jump/spawn/scheduler regressions remain green.

## AnimationVM actor-pass split (20260828)

The common animation entry at 0x001AC784 is now separated from its
continuation body at 0x001AC796. The entry clears the pass scratch state and
checks FRAME_PHASE_COUNTER bit 0; the body then scans all 32 actor records,
requires a live type and valid animation pointer, synchronizes the frame
pointer, dispatches EA-FE animation commands through ACTOR_VM_DISPATCH_TABLE,
and decrements the actor-local animation timer when execution is deferred.
This makes the wrapper/core boundary explicit without introducing another
player cursor or scheduler phase.

The static result is recorded in
re/mame/findings/20260828-animation-vm-core-v1.json.

## Camera scroll refill service (20260828)

The pending-scroll wrapper at 0x001AAA2A is now named
Camera_PublishScroll. It consumes the four directional flags only when
CAMERA_SCROLL_APPLY_GATE is clear, dispatching the matching refill helper and
clearing each flag after the helper returns. The horizontal helpers at
0x001AB34E and 0x001AB44C advance the camera state by one 0x10-pixel step,
write 16 terrain-derived VDP entries, and invoke InteractionTable_ProcessRowsA.
The vertical helpers at 0x001AB55A and 0x001AB66C write 23 entries to the VDP
column and invoke InteractionTable_ProcessRowsB. Their existing deadband and
level-bound checks remain explicit in the decompiler record.

The static result is recorded in
re/mame/findings/20260828-camera-scroll-refill-v1.json.

## Sprite record construction and VDP submission (20260828)

The render path is now split into three named stages. Render_BuildActorRecords
at 0x001AB7C4 constructs camera-relative 8-byte sprite attribute records at
0x00FF729A, seeds player/status entries, appends visible actor records from
the 32-slot actor table, applies facing and priority bits, and publishes the
record count through 0x00FFEFEC. Render_SubmitPlayerSprites at 0x001AB776
transfers that attribute list to the VDP. Render_SubmitActorSprites at
0x001AC726 transfers the separate 14-byte actor sprite payload records from
0x00FF769A and waits for the VDP FIFO before finalizing.

The static result is recorded in
re/mame/findings/20260828-render-submission-v1.json.

## VDP tile-word primitive (20260828)

The scene command stream's single-word VDP helper at 0x001B21A8 is now named
VDP_WriteTileWord. It indexes the tile-row command table at 0x00FF8680,
combines the caller's row/word offset into a VDP control address, adds the
scene/resource tile base at 0x00FFEFF0 to the caller's tile word, and writes
both values through the VDP ports while preserving the caller's working
registers. The same primitive is reused by the resource-size setup path.

The static result is recorded in
re/mame/findings/20260828-vdp-tile-word-v1.json.

## Interaction state services (20260828)

The gameplay interaction service is now split into three named helpers.
Interaction_UpdateCounter at 0x001B00CA drains ACTOR_RESPONSE_COUNTER on even
frame phases and advances the decimal interaction counter until its configured
response budget expires. Interaction_UpdateResourceState at 0x001B01AC advances
the scene/resource counter, triggers resource and optional audio work at its
milestones, and arms the terminal transition countdown. Interaction_UpdateTarget
at 0x001B02EC converges the current interaction target toward its published
target value. The unthrottled 0x001B00D6 variant remains distinct and unnamed.

The static result is recorded in
re/mame/findings/20260828-interaction-state-services-v1.json.

## Interaction response-counter drain (20260828)

The reset-path response helper at 0x001B0078 is now named
Interaction_SynchronizeResponseState. It clears the response-sync flag,
drains ACTOR_RESPONSE_COUNTER through the unthrottled helper at 0x001B00D6,
and copies the six-byte pending response state to the published state when
the pending value has not passed it. The 0x001B00D6 helper is now named
Interaction_DrainResponseCounter, distinct from the even-frame gameplay
counter updater at 0x001B00CA.

The static result is recorded in
re/mame/findings/20260828-interaction-counter-drain-v1.json.

## Level boundary and terminal transition coordinator (20260828)

The broad entry at 0x001A8F0C is now named Level_ExitAndTerminalTransition.
Its decompilation shows one coordinator for the level-boundary and terminal
branches: it performs out-of-bounds cleanup, consumes the delayed terminal
transition request, resets actor/resource state, reloads level and scene
resources, refills camera data, and re-enters the frame/render pipeline for the
resulting state. The surrounding Ghidra parent-function artifact is still
large, so its internal services remain represented by their own named entry
points rather than being collapsed into this label.

The static result is recorded in
re/mame/findings/20260828-level-boundary-transition-v1.json.

## Transition render service (20260828)

The transition renderer is now split into named stages. Render_BeginPaletteTransition
at 0x001B2784 selects the palette bands, snapshots the current 64-word VDP
palette, and runs 16 VBlank-paced service frames. The reset-phase entry at
0x001B28A6 sets FRAME_PHASE_COUNTER to 0xFF before entering
Frame_RunServicePass at 0x001B28AE, which advances the phase, services
MovementVM and AnimationVM, builds actor-only records through
Render_BuildActorRecordsOnly at 0x001AB7A4, waits for VBlank, and submits both
sprite stages. Render_ApplyPaletteStep at 0x001B2916 converges the palette one
component group at a time and writes it to VDP. The shared loop at
0x001B293C is now named Render_ConvergePaletteWords; the full-palette and
single-band entries select their respective word counts before entering this
same component-step writer.

The static result is recorded in
re/mame/findings/20260828-transition-render-service-v1.json.

The transition numeric writer family is also named. SceneTransition_WriteDecimalDigit
at `0x001B34B4` extracts one decimal digit from the caller's value and emits
it through VDP_WriteTileWord. The wrappers at `0x001B3472`, `0x001B347C`,
`0x001B3486`, and `0x001B3490` select the 1,000,000, 100,000, 10,000, and
1,000 places respectively. These helpers explain the fixed-place numeric
output in Scene_EnterTransitionMode without assigning it to a resource type.

## Palette upload and transition helpers (20260828)

The palette helper family is now named. Render_UploadPaletteBand0 at 0x001B2678,
Render_UploadPaletteBand1 at 0x001B2664, Render_UploadPaletteBand2 at 0x001B2650,
and Render_UploadPaletteBand3 at 0x001B263C upload 16-word bands to the VDP and
publish their source pointers. Render_ClearPalette at 0x001B26B0 clears all
64 palette words. Render_RunPaletteTransitionFrom at 0x001B278A initializes
the four palette bands from a caller-supplied source, snapshots the current
palette, and runs the same 16 VBlank-paced convergence loop as the fixed-source
transition entry.

Render_ApplyPaletteBandStep at `0x001B28CE` is the 16-word counterpart to
Render_ApplyPaletteStep: it converges one caller-selected palette band by one
component group, writes that band to VDP, and records its source pointer.

Scene_ApplyActiveLevelPaletteTransition at `0x001AE1A0` selects the active
level record from `SCENE_STATE` in the ROM level table, publishes its palette
source, and invokes `Render_RunPaletteTransitionFrom`. This names the shared
scene-side entry without conflating it with any state-specific resource loader.

Render_ReloadPaletteBands at `0x001ACDA2` re-uploads the four currently
published palette bands through the individual VDP upload helpers while
preserving the caller's `A0`. The type-1E recovery path uses this as a palette
refresh primitive rather than selecting a new palette source.

The static result is recorded in
re/mame/findings/20260828-palette-render-helpers-v1.json.

## Scene-resource service lifecycle (20260828)

The scene-resource lifecycle around 0x001B4B5E is now named. The preparation
helper at 0x001B4B28 waits for VBlank, refreshes transition state, clears the
transition VDP plane, and runs the shared VDP fills. SceneResource_RunServiceLoop
at 0x001B4B78 performs up to 300 VBlank-paced iterations while resource status
is clear, ticking MovementVM and AnimationVM, building actor-only records, and
submitting both sprite stages. SceneResource_RunFadeAndReset at 0x001B4B5E
applies the palette transition, clears actor resources, and copies the prepared
scene words to VRAM after the loop.

The static result is recorded in
re/mame/findings/20260828-scene-resource-service-v1.json.

SceneResource_RunTransitionCommandStream at `0x001B46A8` is the stream-driven
counterpart used by Scene_ResetToState0. Given the ROM stream at `0x00127E8C`,
it handles terminator, palette-band, and timed-service commands while running
AnimationVM, VBlank, query, and sprite stages. The name describes the stream
contract without assigning a state-specific resource role.

## VDP scene setup primitives (20260828)

The fixed VDP setup wrappers are now named. VDP_ClearTransitionPlanes at
0x001B211C writes zero to the two transition-plane destinations
0x40000010 and 0x40020010. VDP_ClearVRAM_C000 and VDP_ClearVRAM_E000 at
0x001B2510 and 0x001B2522 clear 0x800 words at their respective VRAM blocks.
VDP_CopyFixedSceneHeader at 0x001B269C copies four fixed words from ROM
0x002A40 to VRAM 0xF400.

The static result is recorded in
re/mame/findings/20260828-vdp-scene-setup-v1.json.

## Frame resource wait and terrain edge probe (20260828)

Frame_InputAndResourceService at 0x001A91C6 now owns the frame-wait and
scene/resource handshake: it samples the VDP/resource state, runs the resource
helper sequence, waits for VBlank work, and returns once the scene countdown is
active. Player_TerrainEdgeProbe at 0x001AD632 is the separate directional
terrain probe. It checks the scene, player-frame, and map-boundary gates,
samples cells around the player, and publishes left/right stop and inner/outer
edge flags plus the upward stop flag with landing-state handling.

The static result is recorded in
re/mame/findings/20260828-frame-terrain-services-v1.json.

## AnimationVM sprite payload bridge (20260828)

The frame-change helper at 0x001AC6D0 is now named
Render_BuildActorSpritePayload. Called by AnimationVM_RunActorPass, it reads
the actor's animation frame record, expands each sub-sprite into a 14-byte
renderer payload at 0x00FF769A, applies the actor sprite attribute and
horizontal state, and increments 0x00FFEFEE for Render_SubmitActorSprites.
This makes the AnimationVM-to-VDP payload boundary explicit and keeps it
separate from the actor animation command interpreter and the sprite attribute
record builder.

The static result is recorded in
re/mame/findings/20260828-animation-sprite-payload-v1.json.

## Z80 audio bootstrap (20260828)

The sound startup chain is now named end-to-end. Audio_AcquireZ80Bus at
0x001E571C waits for the bus handoff, Audio_LoadZ80Driver at 0x001E573A copies
the ROM driver from 0x001B8480-0x001B9D05 into Z80 RAM and clears the remaining
RAM, Audio_StartZ80 at 0x001E5780 releases reset and the bus, and
Audio_Initialize at 0x001E584A queues the initial sound-engine bootstrap
commands. Audio_ReleaseZ80Bus at 0x001E5730 is the shared release primitive.

The static result is recorded in
re/mame/findings/20260828-audio-z80-bootstrap-v1.json.

## Z80 audio bank-window synchronization (20260829)

The former `Z80_SOUND_QUEUE_MARKER` label at `A01B20` was incorrect. The
68000 helper `Audio_AcquireZ80BankWindow` at `0x001E56C0` raises the interrupt
mask, sets the lock byte at `A01B20`, and waits through the Z80 bus handoff
until the Z80 busy byte at `A01B21` is clear. Its paired
`Audio_ReleaseZ80BankWindow` at `0x001E56FE` reacquires the bus, clears the
lock, and restores the interrupt state. The Z80 banked-ROM helper at `0x01F9`
defers its copy while the lock bit is set and publishes the busy bit during the
copy. This is a bank-window protocol, separate from the sound-command queue.

The static result is recorded in
re/mame/findings/20260829-audio-bank-window-v1.json.

The shared bootstrap packet wrapper Audio_QueueThreeBytePacket at `0x001E5824`
is now named as well. It opens the protected sound queue, emits the three
most-significant bytes of the caller's longword through Audio_QueueWriteByte,
and closes the transaction. Audio_Initialize uses it for each caller-supplied
initialization value.

Audio_QueueSingleByte at `0x001E580E` is the corresponding one-byte protected
queue wrapper. Audio_Initialize uses it for the `0xFF` and `0x0B` bootstrap
prefix bytes before queuing its four three-byte initialization values.

## Actor allocation and template lifecycle (20260828)

The actor allocation cluster around `0x001AE206..0x001AE372` is now named from
the recovered decompilations. `Actor_ClearAllRecords` at `0x001AE206` zeroes
the complete `0x840`-byte table, while `Actor_ClearAllAndPublishResources`
(`0x001AE218`) and `Actor_ClearGameplayRecordsAndPublishResources`
(`0x001AE224`) retire live records through `Actor_ClearOwnedResources` and
publish flagged interaction payloads. The free-slot helpers are distinct ROM
pools: common `3..22`, forward `1..24`, reverse `24..1`, reverse-20 `20..1`,
and auxiliary `25..30`, all with the `0x42`-byte actor stride. The type lookup
at `0x001AE2F2` scans non-player records `1..24`.

`Actor_InitializeFromTemplate` at `0x001AE30A` is a partial initializer, not a
blind `0x42`-byte copy. It copies the compact template identity, movement,
animation, sprite, resource-count, facing-Y, and flags fields; clears the
frame pointer, velocity, interaction/resource links, and animation/movement
timers; and leaves destination coordinates, movement-loop cursor/timer, and
movement return cursor untouched. That explains the observed slot-reuse
traces: a zero-type retirement removes the live identity, and the next caller
overwrites the selected fields while the three continuation fields can remain
stale by design.

`AnimationVM_SpawnOrCopyActor` selects among these pools from its F5 mode byte,
calls the partial initializer, then applies source-relative offsets and
optional animation/movement overrides. Interaction and terrain spawners use
the same named allocator/initializer contract. The native model and allocator
unit test now encode these recovered ranges and retention semantics rather
than treating stale loop words as unexplained behavior.

The duplicate cleanup entry at `0x001AE3CE` is now named
`Actor_ClearOwnedResourcesFromA2`. It has the same resource-list clearing body
as `Actor_ClearOwnedResources` at `0x001AE372`, but addresses the actor through
A2 for the linked collision cleanup path at `0x001ABE52`. This is an ABI-level
variant, not a separate allocator or slot-reuse rule. Details are recorded in
`re/mame/findings/20260828-actor-resource-clear-variant-v1.json`.

The shared actor-VM lifecycle handlers are now separated from cursor clearing.
`ActorVM_DestroyOrClearActor` at `0x001AD0FC` handles animation opcode `F6` and
movement opcode `8C`: the zero-mode path with actor flag bit `+0x3C.2` clear
retires the current record and releases only the linked record's link/flag
state, while the alternate path calls `Actor_ClearAndRelease` and clears both
current and linked types/resources. `EC/82` remains cursor-only. The paired
`ActorVM_FaceTowardPlayer` handler at `0x001AD138` clears actor field `+0x09`
and sets it to `0xFF` only when `PLAYER_WORLD_X` is left of the actor. The exact
static result is recorded in
`re/mame/findings/20260829-actor-vm-lifecycle-static-decompilation-v1.json`.

## Actor collision terminal response (20260828)

The shared response block at `0x001AC484` is now decoded from its callers at
`0x001AC350` (receiving type `0x20`) and `0x001AC458` (receiving type `0x0A`).
It adds the receiving record's byte at offset `+0x08` to
`ACTOR_RESPONSE_COUNTER`; when the receiving type is `0x10`, `0x11`, or
`0x13`, it arms `SCENE_SCRIPT_COUNTDOWN=0x20`. It then clears the current
record and any linked record through `+0x3E`, releases their owned resources,
reinitializes the current record from the type-`0x84` terminal template at
`0x001B7940`, and queues audio event `3` when `SCENE_VDP_UPDATE_FLAG` is set.

The cleanup call at `0x001ABE52` is a duplicate-form helper of the confirmed
`Actor_ClearAndRelease` path at `0x001ABE6E`: both clear the current type and
resources and then clear/release a linked actor. The duplicate is now named
`Actor_ClearAndReleaseLinked` because it is the helper used directly by the
shared collision response. This family is an actor terminal replacement and
counter/countdown producer; it does not directly write `SCENE_STATE` or player
coordinates, so it does not alter the existing Level 01 transfer conclusion.

The compact actor-collision handler family at `0x001AC614-0x001AC6C1` is now
separated by lifecycle effect. Type `0x14` and `0x2F` clear/release the source
before entering the shared Type-0x2D interaction path. Type `0x2B` raises the
actor-collision event flag, promotes the receiver to type `0x84` with animation
`0x00123FF8`, and may create a forward-slot response copy. Type `0x30` clears
both resource sides and performs an in-place receiver template replacement
with animation `0x00123024`. The shared Type `0x2D/0x2E/0x31` body promotes a
source type `0x80` to the type-`0x84` animation `0x00122B6E`, permits source
type `0x82` without that promotion, rejects other source types, and then
promotes the receiver with flag `+0x06=0x40`. None of these bodies directly
writes player coordinates or scene state. Details are recorded in
`re/mame/findings/20260829-actor-collision-handler-family-static-decompilation-v1.json`.

The per-frame actor-terrain service at `0x001ADB5C` is now bounded. It scans
actor records `1..31`, clears the per-record terrain flag, samples the current
and adjacent terrain rows, writes the decoded resource byte 2 to actor `+0x3D`,
and applies either the gravity fallback or the grounded facing/vertical
response. The flagged path retires/reinitializes actors whose terrain sample is
nonempty or invalid through `Actor_HandleType2DInteraction`. Its global sampled
behavior byte is now named `ACTOR_TERRAIN_BEHAVIOR_SCRATCH` at `0x00FFF10D`.
This service is separate from the player's landing resolver and does not write
player coordinates or scene state directly. Details are recorded in
`re/mame/findings/20260829-actor-terrain-collision-loop-static-decompilation-v1.json`.

The static result is recorded in
`re/mame/findings/20260828-actor-collision-terminal-response-v1.json`.

## Actor type-0x13 collision response (20260828)

ActorType13_PlayerCollisionHandler at `0x001AF1AC` is the dedicated type
`0x13` branch from Actor_HandlePlayerCollisionBlock. It clears the source
actor's owned resources, allocates a backward-slot response actor, copies the
source position with a `0x20`-pixel vertical offset, sets its response field,
arms `FFF124` to `8`, and conditionally queues the response audio command.
The routine does not directly write player coordinates, velocity, or scene
state; it materializes an actor-side collision response.

The static result is recorded in
`re/mame/findings/20260828-actor-type13-collision-v1.json`.

## Scene-state audio command (20260828)

Audio_SendSceneStateCommand at `0x001AE1DA` selects the scene-transition
audio command word from the `LEVEL_TABLE` entry indexed by `SCENE_STATE` and
sends it only when `SCENE_TRANSITION_EVENT` is set. This keeps scene audio
selection tied to the same table stride as level metadata instead of adding a
separate state-specific switch.

The static result is recorded in
`re/mame/findings/20260828-scene-state-audio-command-v1.json`.

## Scene resource and VDP service helpers (20260828)

The next high-call-count queue cluster is now named from the existing scene
transition evidence. Frame_WaitForVBlankWork at 0x001B249E clears the
VBlank-ready latch, conditionally performs the Z80 bus handshake when
FRAME_WAIT_LATCH is set, and waits for VBlankInterrupt at 0x001B246E to
publish the next ready boundary. This matches the previously recorded frame
wait lifecycle and does not make FRAME_WAIT_LATCH a gameplay scheduler phase.

VDP_CopyWordsToVRAM at 0x001B255C programs the VRAM destination from A1
and copies the requested word count from A0 through the VDP data port. The
scene/resource path uses it alongside the related VDP helpers during scene
setup and transition work.

SceneResource_ProcessCommandStream at 0x001B21F6 interprets the compact
resource stream until SCENE_RESOURCE_STATUS changes. Its object-command
branch reaches SceneResource_InstantiateActors at 0x001B2238, which selects
the active scene-resource record, initializes the rendering origin, resolves
object entries, finds a free gameplay actor slot, and applies the common
Actor_InitializeFromTemplate contract before assigning animation, movement,
and position fields. These are scene/resource producers, not player-transfer
or direct scene-state writers.

The static result extends the transition/resource evidence in
re/mame/findings/20260828-scene-resource-vdp-service-v1.json.

## Compressed resource decoder family (20260828)

The level-loading queue cluster around 0x001B3416 is now separated into its
format-specific helpers. RNC_To_VDP_Loader programs the destination and enters
RNC_DecodePayloadToVRAM at 0x001B35D0. The latter consumes the compressed
payload with RNC_ReadBits at 0x001B376C and RNC_BuildDecodeTable at
0x001B3790, then streams reconstructed words to the VDP data port.

TerrainBehaviorTable_Decode uses the parallel terrain-resource helpers.
TerrainResource_ReadDecodedSize at 0x001B3912 reads the four-byte decoded
size used to bound the output, TerrainResource_ReadBits at 0x001B38EE
consumes the bitstream, and TerrainResource_BuildDecodeTable at 0x001B391E
builds the code table before the packed behavior resource is expanded into
the runtime terrain table. The duplicate helper layout is deliberate because
the two decoders maintain independent bit-buffer state.

VDP_FillWords at 0x001B2534 is also named as the constant-word counterpart to
VDP_CopyWordsToVRAM. These helpers are data-transfer primitives only; this
promotion adds no new gameplay, player-transfer, or scene-state semantics.

The remaining decoder leaves are now named as well. RNC_DecodeSymbol at
0x001B3736 and RNC_RefillBitBuffer at 0x001B3778 complete the RNC payload
path, while TerrainResource_DecodeSymbol at 0x001B38B8 and
TerrainResource_RefillBitBuffer at 0x001B38FA complete the parallel terrain
resource path. The two pairs keep separate bit-buffer and table state.

The static result is recorded in
re/mame/findings/20260828-compressed-resource-decode-v1.json.

## Scene resource transfer helpers (20260828)

The scene/resource transfer primitives at `0x001B2584`, `0x001B2E44`, and
`0x001B4A7A` are now named. VDP_CopyIndirectWordStream at `0x001B2584`
emits 0x200 words through VDP control word 0x70000003 and treats an FFFF
source word as an indirect pointer escape. VDP_CopyScenePlaneF800 at
`0x001B2E44` copies the fixed 0x400-word ROM plane at `0x00129F00` to VRAM
`0xF800`. SceneResource_PrepareBlankFrame at `0x001B4A7A` clears VRAM C000,
clears the scene-resource C000 source pointer, and prepares the shared blank
palette frame from `0x00128ED2`.

The state-specific compressed loaders remain separate until their resource
roles can be distinguished from the scene table without relying on address-only
suffixes. The static result is recorded in
`re/mame/findings/20260828-scene-resource-transfer-v1.json`.

The fixed common-plane loader SceneResource_LoadCommonPlaneC000 at
`0x001B47F0` is now named as part of the same transfer layer. It decompresses
ROM resource `0x0012F4EC` directly to VRAM `0xC000`; unlike the nearby
state-specific resource loaders, its source and destination contract is fixed.

SceneResource_CopyFixedBlockToD000 at `0x001B2E70` is now named as the
matching fixed-word transfer for the `D000` VRAM region. It copies `0x800`
words from ROM `0x0011E0A0` through VDP_CopyWordsToVRAM. The transfer is kept
address-qualified because the payload's higher-level asset role is not yet
established.

The compact state-resource wrappers are now classified by transfer contract.
`SceneResource_LoadCommonVRAMPair` at `0x001B4920` loads the repeated common
resources to VRAM `0x0000`/`0xE000` and publishes palette source `0x129A12`;
`SceneResource_LoadCommonVRAMBase` at `0x001B494E` loads only the common base
resource and publishes `0x1298D2`; and
`SceneResource_LoadC000ResourceAndPrepareFrame` at `0x001B49DA` loads the
`0x12DD76` C000 resource before the shared render-frame preparation. These
names intentionally describe the exact transfer behavior, not unproven asset
roles.

## Controller-pattern table selectors (20260828)

The two small table selectors at `0x001B0B8E` and `0x001B0BA6` are now named
`Input_SelectPrimaryPatternTable` and `Input_SelectAlternatePatternTable`.
They publish the ROM table addresses `0x4128` and `0x413A` to the active
pattern cursor slots at `FF7276`/`FF727A` and clear the shared match latch at
`FF727E`. The tables contain two-byte controller samples terminated by
`FF 00`; the consuming frame/scene services remain separate because the
higher-level purpose of the patterns is not yet established.

The static result is recorded in
`re/mame/findings/20260828-input-pattern-service-v1.json`.

## Scene VDP and query/audio service helpers (20260828)

The shared scene service helpers around the tile-row command tables are now
named. VDP_BuildTileRowCommandTables at 0x001B2142 builds the three 32-entry
VDP command arrays at FF8680, FF8700, and FF8780, selecting the C000/E000 plane
order from FFF165 and advancing by the caller's row stride. The
SceneTransition_VDPHelper wrapper at 0x001B2E9A supplies the 0x80-byte stride
after programming VDP control word 0x9001.

SceneResource_WaitForCompletion at 0x001B2EAC polls the scene-resource status
for a bounded number of VBlank iterations and invokes the raw query sampler
each time. Terrain_QueryFlagDispatcher at 0x001B319C performs the I/O/Z80
handoff, samples IO_PORT1_DATA, and publishes the raw query byte at FFF155.
Audio_QueueSceneUpdateIfPending at 0x001B327A conditionally queues audio
command 4 when SCENE_VDP_UPDATE_FLAG is set. Terrain_QueryCallbackSetup at
0x001B32F0 writes the caller's two setup words through the paired VDP control
and data commands used by the scene and terrain paths.

The static result is recorded in
re/mame/findings/20260828-scene-vdp-audio-service-v1.json.

`Input_SampleControllerState` at `0x001B34CA` is now named as the complete
two-phase controller sampler. It selects the controller port phase, waits for
the Z80 handoff, publishes the directional/query byte at `FFF156`, repeats
the read for the raw scene/resource byte at `FFF155`, and restores the caller's
word register. The existing `Terrain_QueryFlagDispatcher` remains the
single-phase raw-query entry used by the terrain/resource gate.

## Actor allocation entrypoints and terrain callback publication (20260828)

The common level-object allocation paths at `0x001B5266` and `0x001B52A0`
are now named. LevelObjectSpawnEntry at `0x001B5266` selects the first free
common actor record through Actor_FindFreeSlot, applies the caller's template,
stores the interaction index and selector at actor offsets `0x32`/`0x34`,
places the actor from the current interaction origin, and clears the consumed
interaction byte. LevelObjectSpawnVariant at `0x001B52A0` performs the same
template-backed initialization and placement but leaves the source interaction
byte unchanged. This makes the consuming/non-consuming distinction explicit
without claiming that either entrypoint owns the higher-level selector-specific
actor type or animation assignment.

Terrain_InstallQueryCallbacks at `0x001B32E2` is also named. It copies three
longwords from the caller's resource record into
TERRAIN_QUERY_CALLBACK_A/B/C at `FF7DD2`, `FF7DD6`, and `FF7DDA`. This is a
callback-publication service and is separate from the adjacent paired VDP word
setup helper at `0x001B32F0`.

The static result is recorded in
`re/mame/findings/20260828-actor-allocation-entrypoints-v1.json` and
`re/mame/findings/20260828-terrain-query-callback-installer-v1.json`.

The pre-instantiation cleanup at `0x001A92DC` is now named
`Actor_ClearType85Slots`. `SceneResource_InstantiateActors` invokes it after
selecting a resource stream; it scans the non-player pool and retires records
whose type byte is `0x85`, including their owned resources, before decoding new
actor records. This extends the allocation contract with the observed marked-
slot retirement step without assigning a higher-level meaning to type `0x85`.

## Scene/resource orchestration and camera rebuild (20260828)

The larger scene/resource entrypoints are now separated from their lower-level
helpers. SceneResource_InitializeActiveScene at `0x001B080E` performs the
active-scene entry sequence: it rebuilds VDP state, initializes the
scene-backed actors, runs the initial service pass, uploads the palette band,
and enters the recurring frame service when its entry gate is set.

SceneResource_RebuildAfterInteraction at `0x001B16E0` owns the corresponding
interaction-countdown rebuild path. After its scene/counter gate it resets the
scene presentation, clears and reinitializes actor/resource state, loads the
fixed resources, renders the initial service frames, and continues through the
scene frame loop. SceneResource_ProcessCommandStreams at `0x001B430C` is the
small state-table wrapper that invokes the command-stream interpreter twice.

SceneResource_InitializeScriptTransition at `0x001B50EE` is the separate
scene-script transition entry. SceneScript_AdvanceState reaches it when its
transition gate is set; it clears transition and actor state, copies the scene
presentation, loads the selected VRAM resources, initializes the transition
actor at `(0x40, 0x1B8)`, runs the initial service passes, and conditionally
queues the transition audio command. It is named by its caller contract and
keeps the state-specific compressed resource loaders below it unresolved.

Camera_RebuildView at `0x001AA724` either writes the complete 16-by-23 terrain
viewport in special mode or performs paired right/left `0x10`-pixel camera
sweeps while servicing actor VMs, interaction rows, and sprite submission.

The static result is recorded in
`re/mame/findings/20260828-scene-camera-orchestration-v1.json`.

The coordinate-state pair around `0x001B0490` and `0x001B04DE` is now named.
`Camera_RebuildDerivedCoordinateState` combines the low camera-tile nibbles
with local player coordinates to publish camera-pixel fields and aligned tile
fields. `Camera_InitializeCoordinateState` performs the complementary level/
reset initialization from camera pixels and tiles, clears scroll deltas, and
sets the camera reference to the aligned world-camera position.

The static result is recorded in
`re/mame/findings/20260828-camera-coordinate-state-v1.json`.

The reset/active-scene startup helpers at `0x001B0008` and `0x001B0046` are
now named. `Hud_ResetDisplayDigits` clears the six-character ASCII display
buffer at `FF7E29` and terminates it after writing six zero digits.
`Game_InitializeDifficultyCounter` selects the initial ASCII counter at
`FF7E3C` as `5`, `3`, or `2` from the difficulty/menu mode byte at `FF7E21`.
The ROM pointer table at `0x003FD2` resolves to the `PRACTICE`, `NORMAL`, and
`DIFFICULT` strings, so that byte is now canonicalized as
`GAME_DIFFICULTY_MODE`; the interaction-counter initialization description is
updated accordingly rather than treating it as player state.

`Menu_RenderDifficultyLabel` at `0x001B434E` now closes the static consumer
side of that mode byte: it indexes the same pointer table and renders the
selected difficulty label through the scene command-stream service.

The static result is recorded in
`re/mame/findings/20260828-scene-camera-orchestration-v1.json` and
`re/mame/findings/20260828-interaction-counter-init-v1.json`.

## Interaction-counter actor setup (20260828)

InteractionCounter_ConfigureActors at `0x001B1AD6` now names the actor setup
performed during the interaction/resource rebuild. It divides
PLAYER_INTERACTION_COUNTER by 10, selects animation roots from the longword
table at ROM `0x004A58`, configures the slot-6 and slot-7 interaction records,
and marks the quotient-selected slot-6 record as type `0x84` when the quotient
is nonzero. The remainder selects the slot-7 animation root. This establishes
that the counter is also an animation/actor selector, not only a countdown
scalar.

The static result is recorded in
`re/mame/findings/20260828-interaction-counter-actors-v1.json`.

## Player terrain-state helpers (20260828)

The player terrain-response cluster is now named at five concrete service
boundaries. Player_AlignXToCameraGrid at `0x001A99C6` rounds the published
world X coordinate to the `0x10`-pixel camera grid, converts it back to local
PLAYER_X, restores the horizontal camera threshold, and clears the camera
update delay. Player_SelectInteractionAnimation at `0x001A9B38` indexes the
interaction animation-root table when its gate is clear, resets the animation
timer, and publishes horizontal terrain response state 2.

Player_ResetTerrainMotionState at `0x001B1F28` chooses the player response
stream from the raw query, scene, and camera-special gates, then clears
PLAYER_VY and the animation timer while publishing the landing and terrain
response reset flags. These helpers are state transitions used by the existing
player terrain services; they do not introduce another animation cursor or
service-boundary state. Player_SelectIdleOrInteractionAnimation at
`0x001B1FAE` selects the interaction root when the player interaction mode and
`FFF0D3` marker are active, otherwise choosing the normal or camera-special
idle root. Player_SelectRunOrSpecialAnimation at `0x001B1FFE` chooses the
normal run or camera-special root. Both selectors clear the animation timer.

The static result is recorded in
`re/mame/findings/20260828-player-terrain-state-v1.json`.

## Interaction-counter decrement helper (20260828)

FarTransfer_InteractionCounterStepBack at `0x001B0360` is now named as the
counterpart to FarTransfer_InteractionCounterStep at `0x001B0336`. It
decrements the two ASCII digits at `FFEFE0`, rolls a low digit below `'0'`
back to `'9'` while decrementing the high digit, and stops at `"00"`.
ActorType15_PlayerCollisionHandler invokes it three times before selecting its
player response animation. The interaction counter therefore has explicit
bounded increment/decrement operations in the collision/resource lifecycle.

The static result is recorded in
`re/mame/findings/20260828-interaction-counter-decrement-v1.json`.

## Secondary interaction counter reset (20260828)

InteractionCounter_ResetSecondaryDigits at `0x001AA664` resets the separate
two-digit ASCII word at `FFEFE2` to `"00"`. Its companion
InteractionCounter_AdvanceSecondaryDigits at `0x001B0394` increments the
low digit, carries `':'` into the high digit, and stops at `"99"`. Interaction
handlers compare this word with `"99"`, while Render_BuildActorRecords
consumes the digits when constructing interaction records. This distinguishes
the secondary interaction counter from the primary response budget at
`FFEFE0` without assigning it to a particular interaction type.

The static result is recorded in
`re/mame/findings/20260828-interaction-counter-secondary-v1.json`.

## Level-loader scratch helpers (20260828)

The level-loader initialization family is now named at five documented
service boundaries. Level_InitializePlayerAndTerrainState at `0x001AA696`
installs the initial actor template, resets the terrain response timer, and
activates the terrain controller query flags. Level_BuildTerrainRowPointers
at `0x001AA6C8` fills the `0x58` terrain rows from `WORK_RAM_BASE` using the
level width as the row stride.

The adjacent scratch helpers then clear their owned ranges:
Level_ClearActorScratch at `0x001AA6EE` clears the `0x2B`-byte
actor scratch range beginning at `FFEFDC`, while
Level_ClearSceneEventScratch at `0x001AA700` clears the `0x443`-byte
scene-event range beginning at `FFF12E`. The adjacent
Level_ClearInputScratch at `0x001AA712` clears the `0x126`-byte controller
and input range beginning at `FFF008`.

These are exact range-clearing helpers used during level and scene
initialization. Naming the family distinguishes lifecycle scratch ownership
from the larger scene-resource and actor orchestration routines, without
claiming a more specific state or resource role.

The static result is recorded in
`re/mame/findings/20260827-level-loader-decomp-v1.json`.

## Campaign index

## Interaction pending display initialization (20260829)

`Interaction_InitializePendingDisplayValue` at `0x001AFFE4` seeds the
seven-byte pending display buffer at `FF7E30` with ASCII `100000` followed by
a NUL during `System_InitializeRuntime`. `Interaction_SynchronizeResponseState`
at `0x001B0078` compares that pending six-character value with the active
display at `FF7E29` and copies it forward when the pending value is ready.
The field is therefore named by its synchronization role; its user-facing
meaning is intentionally left unresolved.

The static result is recorded in
`re/mame/findings/20260829-interaction-display-initialization-v1.json`.

## Menu selection-marker tile animation (20260829)

`Menu_AnimateSelectionMarkerTiles` at `0x001B3B4A` runs on even
`FRAME_PHASE_COUNTER` phases. It copies one 16-word frame from the 28-frame
ROM table at `0x00129DAA` to VDP destination offset `0x2000`, advances the
shared `VDP_ANIMATION_FRAME_OFFSET` at `FF7280` by two, and wraps it after
`0x38`. The same shared offset is used by the transition-presentation tile
helpers at `0x001B1676` and `0x001B07D0`, each with its own table bound. The
higher-level visual asset identity remains intentionally unassigned.

The static result is recorded in
`re/mame/findings/20260829-menu-selection-marker-animation-v1.json`.

## Scene-rebuild random actor variant selection (20260829)

The helper at `0x001B16B4` is now named
`SceneResource_SelectRandomActorVariant`. It calls the fixed-ROM PRNG at
`0x001B3032`, masks the returned word with `0x78`, and indexes the sixteen
eight-byte entries at `0x00006960`. Each entry contains an actor type and its
paired animation-stream pointer. The helper retries when the selected type is
already installed in actor record 1 at `FF7E82`, then writes the new type and
animation pointer and clears actor byte `+0x37`.

The table is weighted: types `0x01`, `0x02`, `0x03`, and `0x04` occur two,
three, three, and eight times respectively. The static closure establishes the
selection and no-repeat contract without assigning visual identities to those
four actor types.

The static result is recorded in
`re/mame/findings/20260829-scene-random-actor-selection-v1.json`.

## Shared transition tile-band services (20260829)

The compact helpers at `0x001B07D0` and `0x001B1676` are now named
`Level_RenderTerminalTransitionTileBand` and
`SceneResource_RenderTransitionTileBand`. Both copy a sliding word band from
ROM to `VDP_DATA` using the shared `VDP_ANIMATION_FRAME_OFFSET` at `FF7280`,
then decrement the offset by two and wrap at their own bounds. The terminal
transition helper copies fourteen words from `0x00129C52` to VDP command
`0xC0440000` and wraps at `0x1C`; the scene-resource helper copies sixteen
words from `0x00129B92` to command `0xC0000000` and wraps at `0x20`.

The first is called after `Level_ExitAndTerminalTransition` prepares the
terminal scene. The second is reached from
`SceneResource_InitializeTransitionPresentation` when its presentation flag is
set. Their shared offset is now documented as a reusable VDP tile-animation
cursor rather than a menu-only field.

The static result is recorded in
`re/mame/findings/20260829-scene-transition-tile-bands-v1.json`.

## Scene-resource command handlers and level tilemap transfer (20260829)

The scene-resource dispatch handlers at `0x001B23EA`, `0x001B2412`, and
`0x001B2432` are now named `SceneResource_LoadOrClearC000`,
`SceneResource_PrepareFrameAndPalette`, and
`SceneResource_InstantiateActorRecord`. The first selects between clearing
VRAM C000 and invoking the RNC-to-VDP loader from `SCENE_RESOURCE_C000_SOURCE`.
The second waits for VBlank, starts the palette transition, clears C000, and
uploads palette band 3. The third consumes an eight-byte scene object record
containing a template pointer and X/Y words, skips an inactive zero-Y record,
and initializes a free actor slot for an active record.

The fixed transfer at `0x001B2E5A` is now named
`VDP_CopyLevelF800Tilemap`. It copies `0x400` words from the level source at
`0x0011F000` to VRAM `F800`, distinct from the fixed scene source at
`0x00129F00` used by `VDP_CopyScenePlaneF800`.

The static result is recorded in
`re/mame/findings/20260829-scene-resource-command-handlers-v1.json`.

## Fixed-pattern C000 VDP fill (20260829)

The helper at `0x001B26D0` is now named `VDP_FillC000WithTileEEEE`. It writes
the tile word `0x0EEE` sixty-four times to VDP destination `C000`, preserving
the caller's D0 word. The marked Type-0x1E player-collision recovery path uses
it before waiting for VBlank and reloading the palette bands.

## Scene-resource animated palette service (20260829)

`SceneResource_UpdateAnimatedPalette` at `0x001B1B3C` is the per-frame palette
service in `SceneResource_RebuildAfterInteraction`. It masks
`FRAME_PHASE_COUNTER` to a 64-frame phase and updates five CRAM entries at
phases `0x0A` and `0x1E`. The two phase points write palette indices 7, 8, 21,
22, and 23 with distinct five-word sets, making the service a small deterministic
palette animation rather than a general palette transition or a tile transfer.
The static decompilation and exact VDP command/value pairs are recorded in
`re/mame/findings/20260829-scene-resource-animated-palette-v1.json` and
`re/ghidra/targets/scene-resource-animated-palette-targets.json`.

## VDP pattern service frame helper (20260829)

The previously unassigned block at `0x001B1CE4` is now named
`VDP_RunPatternServiceFrames`. It runs exactly `0x28` iterations, preserving
the loop counter around `Frame_RunServicePass`, the RTS-only
`VDP_PatternLoopNoOpHook`, and `VDP_WriteDescendingBytePatternAtVRAMStart`.
The helper's local contract is established without assigning an unsupported
higher-level meaning to the surrounding mode-`0x17` presentation path. The
static contract is recorded in
`re/mame/findings/20260829-vdp-pattern-service-frames-static-decompilation-v1.json`
and `re/ghidra/targets/vdp-pattern-service-frames-targets.json`.

## Scene-resource mode-0x17 service/setup (20260829)

The adjacent blocks at `0x001B1C0C` and `0x001B1D00` are now named
`SceneResource_RunMode17Service` and `SceneResource_InitializeMode17Service`.
The first writes `SCENE_RESOURCE_MODE = 0x17`, runs the actor/render passes,
enters the exact `0x28`-iteration VDP pattern service, and applies the known
interaction-progress and scene-audio gates. The second performs the matching
palette/transition/actor/VDP/Z80 setup and invokes the service. Both `0xD8`
byte boundaries are explicit; the exact user-visible identity of mode `0x17`
remains unresolved. The static contract is recorded in
`re/mame/findings/20260829-scene-resource-mode17-static-decompilation-v1.json`
and `re/ghidra/targets/scene-resource-mode17-targets.json`.

## Interaction response-counter family and resource decrement (20260829)

The fixed response-delay helpers from `0x001B0138` through `0x001B019B`
are now named as the complete `Interaction_AddResponseCounter*` family for
increments `1, 5, 10, 15, 20, 25, 50, 75, 100, and 1000`. The adjacent
`0x001B02B6` helper is now `Interaction_DecrementResourceProgressCounter`; it
decrements the three-digit ASCII interaction/resource progress value with
decimal borrow, preserves `000`, and returns the progress pointer. The exact
fixed bodies and ranges are recorded in
`re/mame/findings/20260829-interaction-response-counter-complete-static-decompilation-v1.json`
and `re/ghidra/targets/interaction-response-counter-complete-targets.json`.

## Actor terrain-response tail dispatch (20260829)

The 46-byte block at `0x001B1E0A` is now named
`Terrain_DispatchFirstActorResponse`. It scans actor records 1 through 24,
selects the first active record with a nonzero `+0x3D` terrain-response byte,
and tail-dispatches through `TERRAIN_RESPONSE_HANDLER_TABLE` at `0x004554`.
Its boundary immediately before `Terrain_ResolvePlayerCell` is explicit; no
static caller or scheduler role is inferred. The contract is recorded in
`re/mame/findings/20260829-terrain-actor-response-dispatch-static-decompilation-v1.json`
and `re/ghidra/targets/terrain-actor-response-dispatch-targets.json`.

## Actor lifecycle helper services (20260829)

The actor cleanup path now has an explicit interaction-table publication helper.
`Actor_RepublishInteractionValueOnCull` at `0x001AE0D4` is called by
`Actor_CullAndRemoveLinked` after clearing an actor's type/resources. When actor
flag bit `0x20` is present and its selector byte is nonzero, it republishes the
selector from actor offsets `+0x32/+0x34` into `INTERACTION_TABLE_RUNTIME` at
`0x00FFAE87`. This explains the previously anonymous writeback observed in the
linked cleanup path without conflating it with the ordinary A1/A2 publication
helpers.

The Type-`0x1E` collision recovery wrapper at `0x001ACD54` is now named
`ActorType1E_PrepareRecoveryPlane`. It delegates to
`VDP_FillC000WithTileEEEE`, after which the caller waits for VBlank and reloads
the four published palette bands. Both helper contracts are recorded in
`re/mame/findings/20260829-actor-lifecycle-helpers-v1.json` and
`re/ghidra/targets/actor-lifecycle-helpers-targets.json`.

## Runtime actor spawn family 0x6E-0x73 (20260829)

The six indirect handlers at `0x001ACE90`, `0x001ACECC`, `0x001ACF08`,
`0x001ACF44`, `0x001ACF80`, and `0x001ACFBC` now have explicit runtime-type
names. They share a duplicate-type check, the reverse auxiliary-slot allocator,
and `Actor_InitializeFromTemplate` with template `0x001B7904`. On successful
allocation they install runtime types `0x6E` through `0x73`, select movement
roots `0x00120584`, `0x001205F4`, `0x00120664`, `0x0012070E`, `0x00120868`, and
`0x001208D8` into actor field `+0x0A`, and copy the source record's X/Y words.
Static decoder evidence now bounds six exact local MovementVM bodies and their
shared 118-byte continuation at `0x00120948-0x001209BD`, ending immediately
before the independent Type-0x7F response stream. This closes the actor-spawn
family's movement contract without assigning unverified gameplay identities to
the individual types. The original handler mapping remains recorded in
`re/mame/findings/20260829-actor-runtime-type6e-73-v1.json`; the corrected
movement classification is recorded in
`re/mame/findings/20260829-actor-runtime-type6e-73-movement-static-v1.json` and
`re/ghidra/targets/actor-runtime-type6e-73-movement-static-targets.json`.

The same packed family also contains six secondary MovementVM entries between
the direct roots: `0x001205CA-0x001205F3`, `0x0012063A-0x00120663`,
`0x001206BC-0x0012070D`, `0x0012075E-0x00120867`, `0x001208AE-0x001208D7`,
and `0x0012091E-0x00120947`. Each linear decoder pass reaches the next known
root or the shared continuation exactly, so the gaps are now named as
per-type secondary entries without claiming more specific gameplay roles. The
static result is recorded in
`re/mame/findings/20260829-actor-runtime-type6e-73-secondary-movement-static-v1.json`
and
`re/ghidra/targets/actor-runtime-type6e-73-secondary-movement-static-targets.json`.

## Actor spawn variation and runtime type 0x40 (20260829)

`Actor_ApplyRandomSpawnVariation` at `0x001B6794` is shared by the adjacent
scene-resource actor producers. It consumes `TerrainScene5RandomStep`, adds a
random signed offset from `-3` through `+4` to the destination actor's X word,
optionally replaces its animation root with `0x001241FC`, and optionally sets
the actor byte at offset `+0x35`. The helper is now named without assigning an
unverified visual identity to the variation.

`InteractionSpawn_RuntimeType40` at `0x001B7332` calls
`LevelObjectSpawnVariant` with template `0x001B79B8` and, on success, installs
runtime type `0x40`, animation root `0x00122C12`, and a cleared byte at `+0x29`.
Its parent adjusts the destination coordinates around four calls, defining a
multi-position type-`0x40` spawn pattern. The exact contracts are recorded in
`re/mame/findings/20260829-actor-spawn-variation-v1.json` and
`re/ghidra/targets/actor-spawn-variation-targets.json`.

## Actor type-0x89 to type-0x84 conversion variants (20260829)

The paired routines at `0x001ACDD0` and `0x001ACE30` scan the 24 primary
actor slots from `0x00FF8470` in reverse record order for an existing type
`0x89`. On the first match they consume `TerrainScene5RandomStep`, add the
signed offset `-3..+4` to actor offset `+0x18`, convert the record to runtime
type `0x84`, install shared frame data `0x00124208`, set actor flag bit
`0x40`, and republish the actor interaction byte through
`INTERACTION_TABLE_RUNTIME`. The variants differ only in animation roots
`0x001209F0` and `0x001209F8`, so they are named by their exact conversion
contract rather than an unverified actor identity.

The static results are recorded in
`re/mame/findings/20260829-actor-runtime-type84-v1.json` and
`re/ghidra/targets/actor-runtime-type84-targets.json`.

## Timed level-event command dispatcher (20260829)

`LevelEvent_DispatchTimedCommand` at `0x001B634E` is the shared per-frame
dispatcher reached by the level callbacks at `0x001B5B94` and `0x001B5D3A`.
It reads a six-byte record from `LEVEL_EVENT_SCRIPT_CURSOR`: a delay byte, a
command byte, and two word payload values. The elapsed counter at
`LEVEL_EVENT_SCRIPT_TICK` advances once per callback; when it exceeds the
record delay, the cursor advances by six bytes, the counter resets, and the
command byte plus `0x1A` selects one of 26 handlers from
`LEVEL_EVENT_COMMAND_DISPATCH_TABLE` at `0x0020C0`. Setup paths publish stream
starts at `0x002128` and `0x0024FC`. This separates the level-event stream
from the scene-script cursor used by scene-state transitions.

The static result is recorded in
`re/mame/findings/20260829-level-event-dispatch-v1.json` and
`re/ghidra/targets/level-event-dispatch-targets.json`.

## Shared actor-VM sound event command (20260829)

The shared dispatch-table entry at `0x001AC9D2` is now named
`ActorVM_HandleSoundEventCommand`. It consumes the event byte from the actor
VM stream only when `SCENE_TRANSITION_EVENT` is active. The high bit selects
between a prepare-only packet and the normal prepare-plus-send sequence; the
low seven bits remain the sound command ID. The prepare-only branch is the
small helper at `0x001ACA00`, now named
`ActorVM_QueuePreparedSoundCommand`, which preserves the VM command value and
working registers while calling `Audio_PrepareCommand`.

This path is shared by the movement command `0x89` and its corresponding
animation event command through `ACTOR_VM_DISPATCH_TABLE`, so the names stay
VM-generic rather than assigning the event to a particular actor or scene.
The exact dispatch and packet behavior is recorded in
`re/mame/findings/20260829-actor-vm-event-v1.json` and
`re/ghidra/targets/actor-vm-event-targets.json`.

The static result is recorded in
`re/mame/findings/20260829-vdp-fill-c000-v1.json`.

## Interaction resource progress pointer (20260829)

`Interaction_GetResourceProgressPointer` at `0x001B003A` is the small shared
getter used by `Interaction_UpdateResourceState` at `0x001B01AC`. It returns
the address `0x00FF7E38`, which holds the three-digit ASCII
`INTERACTION_RESOURCE_PROGRESS_COUNTER` and its terminator. The resource-state
service increments this counter and compares the resulting value against
milestones before issuing the corresponding scene-resource work, so the
pointer now has an explicit contract instead of remaining an anonymous leaf.

The static result is recorded in
`re/mame/findings/20260829-interaction-resource-progress-v1.json` and
`re/ghidra/targets/interaction-resource-progress-targets.json`.

## Windowed resource decoder to VRAM (20260829)

The standalone decoder at `0x001B39A6` is now named
`Resource_DecodeWindowedStreamToVRAM`. It programs the VDP destination from
`A1`, consumes a bitstream from `A0` under control bytes from `A2`, writes
literal bytes into a 4 KiB work-RAM sliding window at `0x00FF0000`, and
reconstructs back-reference runs from that window before streaming output
words through `VDP_DATA`. A zero control byte is skipped and `0xFF` ends the
stream. The name deliberately describes the observed decoder contract without
assigning a format name or an unresolved resource caller.

The static result is recorded in
`re/mame/findings/20260829-windowed-resource-decoder-v1.json` and
`re/ghidra/targets/windowed-resource-decoder-targets.json`.

## Dynamic terrain map behavior cleanup (20260829)

The paired routines at `0x001AC386` and `0x001AC3BC` now have explicit
terrain-side names. Each scans the map work-RAM word range beginning at
`0x00FF0000`, shifts each map word right by one to index
`TERRAIN_BEHAVIOR_INDEX_TABLE`, and clears the map word when its behavior
matches the routine's pair. The first handles behaviors `0x4F` and `0xFE`;
the second handles `0x4E` and `0xFD`. This records the dynamic map mutation
found in the Level 01 transfer closure without conflating it with a writer to
the load-time terrain behavior table.

The static result is recorded in
`re/mame/findings/20260829-terrain-map-behavior-clear-v1.json` and
`re/ghidra/targets/terrain-map-behavior-clear-targets.json`.

## Camera terrain viewport renderer (20260829)

The helper at `0x001AA81A` is now named `Camera_RenderTerrainViewport`. It
renders a 16-by-23 visible terrain tile viewport, selecting rows from
`TERRAIN_ROW_POINTER_TABLE` using `CAMERA_REFERENCE_X/Y`, reading the active
background-block source through `LEVEL_BACKGROUND_BLOCK_SOURCE`, and emitting
four tile words per map word through the generated VDP tile-row command tables.
The active row stride is published as `TERRAIN_ROW_BYTE_STRIDE` at `FF7DB4`.

The static result is recorded in
`re/mame/findings/20260829-camera-terrain-viewport-v1.json`.

## Level-table exit callback dispatch (20260829)

The short routine at `0x001AE1C2` is now named `Level_InvokeExitCallback`. It
indexes the 66-byte `LEVEL_TABLE` record selected by `SCENE_STATE`, loads the
longword at record offset `0x28`, pushes it as a return address, and returns
through `RTS`. This is an indirect tail dispatch to the current level's exit
callback, reached from `Level_ExitAndTerminalTransition` at `0x001A90E8`.

The static result is recorded in
`re/mame/findings/20260829-level-exit-callback-dispatch-v1.json`.

## Level callback dispatch trampolines (20260829)

The active level publishes two additional callback pointers during
`Level_LoadFromSceneState`. `LEVEL_FRAME_CALLBACK` at `0x00FF7DC2` receives the
level-table record's `+0x2C` pointer, and `Level_InvokeFrameCallback` at
`0x001A8F04` tail-dispatches through it from `Game_FrameUpdateLoop` once per
frame. `LEVEL_CAMERA_SCROLL_CALLBACK` at `0x00FF7DA4` receives the record's
`+0x34` pointer, and `Level_InvokeCameraScrollCallback` at `0x001AAA80`
tail-dispatches through it from `Camera_PublishScroll` before the pending
directional refill handlers run. The callback slots are distinct from the
level-table exit callback field; the record `+0x2C` field previously tracked as
an `EnterRoutine` is the per-frame boundary callback, while `+0x34` is the
camera-scroll callback. The dispatch pair is recorded in
`re/mame/findings/20260829-level-callback-dispatch-v1.json` and
`re/ghidra/targets/level-callback-dispatch-targets.json`.

## Active-scene initial C000 and palette transfer (20260829)

`SceneResource_LoadInitialC000AndPaletteSources` at `0x001B25FE` is called by
`SceneResource_InitializeActiveScene` with `A0=0x00129CAA`. It publishes that
pointer and `A0+0x20` as the first two palette-band sources at `FF7262` and
`FF7266`, programs VDP VRAM address `0xC000`, and copies 32 words through the
VDP data port. It preserves the caller's D0 word. The source is named as an
initial C000/palette block; its higher-level visual identity remains open.

The static result is recorded in
`re/mame/findings/20260829-scene-initial-vdp-palette-v1.json`.

## Scene-transition selection cursor (20260829)

`SceneTransition_RenderSelectionCursor` at `0x001B2AB2` processes the ROM
command stream at `0x0012651A` with a width parameter of `0x15` and a row
parameter of `0x14 + SCENE_TRANSITION_SELECTION_INDEX`. Its caller,
`Scene_EnterTransitionMode`, maintains that selection index in the range `0..3`
while handling controller input. The stream contains the cursor arrow and
surrounding blank rows; the user-facing labels represented by the rows remain
unassigned.

The static result is recorded in
`re/mame/findings/20260829-scene-transition-selection-v1.json`.

## Audio control-marker wrappers (20260828)

The two fixed-packet wrappers around `Frame_InputAndResourceService` are now
named. `Audio_QueueControlMarker0C` at `0x001E58CC` emits `FF 0C`, while
`Audio_QueueControlMarker0D` at `0x001E58E0` emits `FF 0D`. Both open the
protected Z80 sound queue, call `Audio_QueueWriteMarker`, and close the queue;
the former is called at the beginning of the frame input/resource sequence and
the latter at its completion.

The packet identities and queue ownership are exact disassembly facts. The
audio driver's internal meanings for control bytes `0x0C` and `0x0D` are not
assigned until the Z80-side command consumer is decoded.

The static result is recorded in
`re/mame/findings/20260828-audio-control-markers-v1.json`.

## Scene-resource presentation wrapper family (20260828)

The fixed wrapper family selected by `SceneResource_Dispatch` is now named.
The `0x001B4BB8` through `0x001B4EDC` bodies each combine a source-qualified
C000 resource loader or the shared blank-frame preparer with one fixed command
stream, dimensions, the temporary `0x00FFEFFC` presentation scratch marker,
and `SceneResource_RunFadeAndReset`.

The source/stream pairs are now explicit in the names and finding record:
state-1 uses `0x0012E34A/0x001270A8` and `0x0012E176/0x00127134`; state-3 uses
`0x0012DD76/0x00127207` and `0x0012DD76/0x00127338`; state-4 uses
`0x0012DA04/0x001273E9` plus blank streams `0x00127BD2` and `0x00127CB4`;
state-5 uses `0x00127571` and `0x001275EE`; state-7 uses
`0x0012D870/0x0012772D`; state-9 uses `0x00127D74`; and state-11 uses
`0x0012792B` and `0x00127C42`. The dispatch code also retains a few wrappers
reachable from state-0 paths; the names deliberately identify the fixed
contracts without assigning stream-internal meanings.

The static result is recorded in
`re/mame/findings/20260828-scene-resource-presentation-wrappers-v1.json`.

## Scene-transition input services (20260828)

The controller-neutral wait at `0x001B3064` is now named
`Input_WaitForNeutralController`. Its live loop drives the controller port
through the Z80 handoff, accepts the directional phase only when
`IO_PORT1_DATA & 0x3F == 0x3F`, accepts the button phase only when
`IO_PORT1_DATA & 0x30 == 0x30`, and returns to `Scene_EnterTransitionMode` once
both masks are neutral.

The adjacent `0x001B2802` helper is now named
`Level_PrepareTerminalTransitionFrame`. It clears VRAM C000 and runs one
`Frame_RunServicePass` before `Level_ExitAndTerminalTransition` checks the
scene-state boundary. Its later controller polling block is statically
unreachable because the local counter is initialized to `0x0103` and compared
immediately against `0x00D2`; that dead block is retained as disassembly
evidence and not assigned live semantics.

`IO_PORT1_CONTROL` at `0x00A10009` is now a shared hardware symbol for the
controller phase-select writes.

The static result is recorded in
`re/mame/findings/20260828-scene-transition-input-services-v1.json`.

## Interaction response-counter adjustments (20260829)

The fixed helpers that adjust `ACTOR_RESPONSE_COUNTER` are now named. The
shared collision reinitialization path calls `Interaction_AddResponseCounter15`
at `0x001B0156`; the type-0x34 handler calls
`Interaction_AddResponseCounter25` at `0x001B016A`; and the long interaction /
resource branch calls `Interaction_AddResponseCounter1000` at `0x001B0192`.
Their exact writes are respectively `+0x000F`, `+0x0019`, and `+0x03E8` to
`0x00FFF14E`. This separates fixed response-delay adjustments from the
frame-gated drain and decimal-display update services.

The static result is recorded in
`re/mame/findings/20260828-interaction-response-counter-adjustments-v1.json`.

## Shared actor-collision reinitialization (20260828)

The short body at `0x001AF4C2` is now named
`Actor_ReinitializeFromCollisionTemplate`. It is reached by both
`ActorType3A_PlayerCollisionHandler` and `Actor_FarTransferPlayerCollisionHandler`.
The shared tail advances `Interaction_UpdateCounter`, clears the current actor
through `Actor_ClearAndRelease`, preserves the cleared record pointer in `A5`,
and calls `Actor_InitializeFromTemplate` with the response template at
`0x001B7ABC`.

This separates the common actor lifecycle operation from the two caller-specific
interaction/audio decisions. The shared template's complete field meaning is
left for a separate data-decoding pass.

The static result is recorded in
`re/mame/findings/20260828-actor-collision-reinitialization-v1.json`.

## Scene-resource VDP command-record streamer (20260828)

The frame/reset helper at `0x001AE0F6` is now named
`SceneResource_StreamVdpRecord`. It is idle when the scene-script pending
state is `0x02` or no stream is installed. Otherwise it advances the
`SCENE_RESOURCE_VDP_STREAM_OFFSET` by `0x0E`, wraps it against the installed
stream bound, reads one 14-byte record, acquires the Z80 bus, writes the
record's long VDP address and five VDP words, waits for the VDP FIFO, and
releases the bus handoff.

`Level_LoadFromSceneState` installs the stream pointer at `FFF140` and its
exclusive byte-offset bound at `FFF148`; `FFF14C` is the rotating offset.
This names the command-record transfer contract without conflating it with
the enclosing `Level_ExitAndTerminalTransition` function or with the separate
scene-resource completion wait.

The static result is recorded in
`re/mame/findings/20260828-scene-resource-vdp-record-v1.json`.

## Difficulty-initialized counter increment (20260828)

The helper at `0x001AEF70` is now named `Game_IncrementDifficultyCounter`.
It increments the ASCII byte at `0x00FF7E3C`, clamps it at `'9'`, and when
`SCENE_VDP_UPDATE_FLAG` is set queues audio command `0x66` with the updated
value. The two callers are the interaction-response drain at `0x001B00D6` and
the scene-resource rebuild path at `0x001B19EE`.

The counter's surrounding contract is now explicit: `Game_InitializeDifficultyCounter`
initializes it from `GAME_DIFFICULTY_MODE` as `'5'`, `'3'`, or `'2'`;
`Render_BuildActorRecords` renders it into a seeded HUD record; and the
terminal level-transition path decrements it when its gate is clear. The
structured name `GAME_DIFFICULTY_COUNTER` records those facts without claiming
whether the displayed value is lives, health, wishes, or another gameplay
resource.

The static result is recorded in
`re/mame/findings/20260828-game-difficulty-counter-v1.json`.

## Primary controller-pattern service (20260828)

The frame-side routine at `0x001B0A46` is now named
`Input_ProcessPrimaryPattern`. It samples both controller phases through the
Z80 handoff, compares the reduced two-byte sample against the active primary
pattern record, sets `INPUT_PATTERN_MATCH_LATCH` only on an exact match, and
advances `INPUT_PATTERN_CURSOR` after the matching release interval. A
mismatch reselects the primary table at `0x00004128`.

The terminal record reselects that primary table, optionally queues audio
commands `0x02` and `0x5B` around `SceneResource_WaitForCompletion`, and arms
`SCENE_SCRIPT_COUNTDOWN` with `0xFF`. This differs from
`Input_ProcessMenuPattern` at `0x001B0BBE`, whose alternate-table terminal path
enters the wish-prompt presentation. The static body establishes the
controller-pattern and scene/audio effects but does not identify the
user-facing purpose of the primary pattern.

The static result is recorded in
`re/mame/findings/20260828-input-primary-pattern-v1.json`.

## Actor sprite-resource allocation (20260828)

The AnimationVM actor pass calls `0x001AD3E8` when an actor's sprite-resource
base at record offset `+0x2E` is empty. The routine is now named
`Actor_AllocateSpriteVRAM`. It reads the template-owned resource count at
`+0x29`, searches the first `0x74` bytes at `0x00FFF008` for a contiguous free
run of `resource_count + 1` bytes, stores the run pointer at `+0x2A`, and marks
the run with `0xFF`.

The selected run start indexes `ACTOR_SPRITE_VRAM_BASE_TABLE` at `0x0011F500`,
whose entries are `0x80`-byte-spaced VRAM bases from `0x8600` through `0xA580`.
The selected base is stored at actor `+0x2E`; `Render_BuildActorSpritePayload`
uses that field to form the sprite tile attribute words. The corresponding
cleanup at `0x001AE372` clears the occupancy run and both owner fields. If the
allocator cannot find a contiguous run, the ROM publishes and clears the
current actor and then clears its linked actor when present.

This closes the anonymous AnimationVM precondition as sprite-resource
ownership, independently of the actor animation cursor and scheduler timing.
The static result is recorded in
`re/mame/findings/20260828-actor-sprite-resource-allocation-v1.json`.

## Scene-state presentation services (20260828)

The scene/resource region contains three distinct presentation entrypoints
around the reset and scene-script handoff. `0x001B0D70` is the countdown-driven
state presentation: it clears the active presentation, selects the resource
pair according to scene/camera gates, initializes the fixed-row actors, and
enters the VBlank-paced service. `0x001B1486` is the shared reset/transition
presentation that loads the fixed `0x0012EA12`/`0x001319EC` pair and places its
two presentation actors. `0x001B202A` is the `SCENE_STATE=1` title/intro path,
with explicit 30-frame fade-in, 60-frame hold, and 900-frame controller-release
wait loops before pending main-scene setup.

The static result is recorded in
`re/mame/findings/20260828-scene-state-resource-presentations-v1.json`.

## Player terrain-response animation selectors (20260828)

The player motion region now has a complete named handoff for terrain
responses. `0x001A986E` consumes the terrain-query and stop/response flags,
applies vertical corrections or cancels the response state, and publishes
`PLAYER_TRANSITION_GATE`. Its correction branches use the odd-frame-gated
selector at `0x001A997A`; the velocity-cancel path uses the ungated selector at
`0x001A9986`. Both selectors index the sixteen-entry pointer table at
`0x00121828` with `(PLAYER_WORLD_Y >> 2) & 0x0F`, update the facing flip for
bands 8 through 15, and clear the animation timer.

The static result is recorded in
`re/mame/findings/20260828-player-terrain-response-v1.json`.

## Startup initialization services (20260828)

The reset entry now has two named bootstrap services. `0x001AA344` performs
the one-time audio/runtime setup, installs the default menu terrain callbacks,
and publishes the initial scene and transition gates. `0x001AA41C` performs
reset-time scene defaults: it selects the initial sound-test table and scene
script, sets `SCENE_STATE=1`, initializes the difficulty-dependent counter,
and writes the active-scene entry gate at `0xFF7E3F`. The gate is consumed by
`SceneResource_InitializeActiveScene` at `0x001B080E`.

The static result is recorded in
`re/mame/findings/20260828-startup-initialization-v1.json`.

## Player terrain-contact resolution (20260828)

The neighboring contact-side player routines are now named. `0x001A99F0`
handles terrain contact while the player is not rising: it selects the
interaction animation and facing for directional push flags, updates camera
thresholds and delay, aligns local Y, and publishes the interaction transition
lock. `0x001A9B6C` owns the exact alignment formula
`((PLAYER_WORLD_Y & 0xFFF0) | 4) - WORLD_CAMERA_Y` and restores the horizontal
camera threshold.

The static result is recorded in
`re/mame/findings/20260828-player-terrain-contact-v1.json`.

## Scene state-10 presentation service (20260828)

The state-10 branch of `SceneResource_Dispatch` now has a named entrypoint at
`0x001B4D0C`. It prepares palette source `0x00129812` and command stream
`0x00127834`, runs the shared AnimationVM/MovementVM/actor/render service for
up to 300 VBlank iterations, advances its bounded VDP fill offset, then fades
and releases the scene resources. The user-facing identity of this state
remains intentionally open.

The static result is recorded in
`re/mame/findings/20260828-scene-state10-presentation-v1.json`.

| Campaign | Status | Purpose |
| --- | --- | --- |
| `20260825-level01-canonical-v1` | in progress | original canonical route record |
| `20260826-level01-fresh-baseline-v1` | recorded | fresh boot baseline |
| `20260826-level01-recording-baseline-v1` | recorded | fresh boot baseline at the ledger commit |
| `20260826-level01-recording-frontier-v1` | recorded-frontier | current-harness route chain and upper-route frontier |
| `20260826-level01-fresh-route-probe-v1` | recorded | natural route and handhold probes |
| `20260826-level01-bounce-up-sweep-v1` | recorded | bounce timing sweep |
| `20260826-level01-route-expansion-v1` | in progress | route expansion and unresolved exit search |
| `20260826-level01-transition-v1` | controlled proof | scene-transition path |
| `20260826-level01-fresh-route-canonical-v2` | recorded-frontier | fresh no-sync route record through the upper-left guard |
| `20260826-level01-fresh-route-high-transfer-v1` | recorded-frontier | fresh handhold-to-high-walkway transfer and upper guard |
| `20260826-level01-recording-fresh-v2` | recorded-frontier | clean power-on replay through the upper-edge frontier |
| `20260826-level01-tower-negative-v1` | recorded-negative | lower-tower surface/actor branch from the clean upper-edge checkpoint |
| `20260826-level01-tower-support-exploration-v1` | recorded-frontier | controlled lower support chain, rope gap, rope fixture, and natural negative |
| `20260826-level01-canonical-recording-v3` | recorded-frontier | current power-on record through lower-tower crossing and far vertical-rope frontier |
| `20260826-level01-canonical-recording-v4` | recorded-frontier | fresh exact-match power-on record, owned continuation to far rope, and natural upper-transfer experiments |
| `20260826-level01-canonical-recording-v5` | recorded-frontier | clean power-on recording and self-contained replay chain through the far vertical-rope frontier |
| `20260826-level01-canonical-recording-v6` | recording-frontier | clean v5 replay, Type1E wall opening, far-rope transfer, and bounded upper-frontier probes |
| `20260826-level01-upper-activation-v1` | recorded-static-correlation | initializer and interaction-row traces for the Type1E/Type20 pair materialized at the upper x≈4688 feature |
| `20260826-level01-reproducibility-v1` | recorded-frontier | current-checkout exact replay of the clean power-on route through the established frontier |
| `20260826-level01-reproducibility-extension-v1` | recorded-frontier | current-checkout continuation with 160 checkpoints through the tower, far rope, wall opening, and upper actor setup |
| `20260826-level01-tower-connector-audit-v1` | recorded-negative | exact lamp-window timing sweep and four-handler upper actor response audit |
| `20260826-level01-upper-frontier-search-v1` | recorded-negative | twelve exact upper-frontier jump, direction, sword, and delayed timing branches |
| `20260826-level01-upper-platform-jump-sweep-v1` | recorded-negative | eleven fixed-position ordinary-C probes across the behavior-25 upper band |
| `20260826-level01-upper-actor-jump-v1` | recorded-negative | eleven directional-C and C+B probes at the upper actor line |
| `20260826-level01-upper-dispatch-trace-v1` | recorded-static-correlation | full replay and breakpoint trace of the upper interaction-row spawn path |
| `20260826-level01-tower-control-matrix-v1` | recorded-negative | eight same-checkpoint control branches over the behavior-47 lower tower |
| `20260826-level01-lower-continuation-v1` | recorded-negative-frontier | sustained and jump-assisted traversal from the lower tower continuation |
| `20260826-level01-tower-type43-interaction-v1` | recorded-negative | pre-conversion type-0x43 lamp interaction branches |
| `20260826-level01-upper-actor-alignment-v1` | recorded-negative | approximate opening alignment and Up/C response matrix for the upper Type-1E/Type-20 pair |
| `20260826-level01-upper-exact-opening-alignment-v2` | recorded-negative | exact Type-1E/Type-20 alignment and Up/C/directional/attack response matrix |
| `20260826-level01-upper-pair-y-alignment-v1` | recorded-negative-controlled | controlled player-Y alignment with the upper Type-1E/Type-20 pair |
| `20260826-level01-upper-x4412-up-matrix-v1` | recorded-negative | Up/C+Up matrix near the row-15 special cell between upper surfaces |
| `20260826-level01-route-geometry-audit-v1` | recorded-negative-frontier | consolidated high-walkway, far-rope, shaft, and upper-platform geometry audit |
| `20260826-level01-exit-predicate-probe-v1` | recorded-controlled-boundary-write | current-harness boundary predicate and gate observation |
| `20260826-level01-natural-tower-response-trace-v1` | recorded-negative-frontier | natural lower-tower terrain, interaction-row, and scene-gate trace |
| `20260826-level01-connector2112-trace-v1` | recorded-negative-frontier | natural column-132 connector climb and upper-stop handler trace |
| `20260826-level01-connector2112-upper-stop-v1` | recorded-negative-frontier | column-132 endpoint and left/right/neutral dismount matrix |
| `20260826-level01-behavior47-collision-edge-trace-v1` | recorded-negative-frontier | indirect edge trace of the natural lower-tower Type-0x20 contact and cleanup path |
| `20260826-level01-tower-plain-c-edge-trace-v1` | recorded-negative-frontier | plain-C/Up edge trace from the natural lower-tower lamp checkpoint |
| `20260826-level01-scene-writer-controlled-current-v1` | recorded-controlled-boundary-write | current-frontier boundary write and negative follow-on writer trace |
| `20260826-level01-scene-table-index-call-path-v1` | recorded-static-correlation | call path separating the state-08 table cycle from the level-01 boundary writer |
| `20260826-level01-countdown-producer-audit-v1` | recorded-negative | static and narrow runtime audit of generic actor-collision scene-countdown producers |
| `20260826-level01-surface-mode-audit-v1` | recorded-static-correlation | behavior-0x47 terrain-resource mode toggle and landing-resolver offset correlation |
| `20260826-level01-canonical-recording-v11` | recorded-negative | ten complete hold-jump transfer-delay traces from the far-rope checkpoint |
| `20260826-level01-actual-80-activation-v1` | recorded-controlled | exact-row controlled dispatch proof for the sole Level 01 interaction selector 0x80; natural activation unresolved |
| `20260826-level01-actual-80-spawn-side-effects-v1` | recorded-controlled | selector 0x80 flag clear, generic spawn, and runtime actor-type proof |
| `20260826-level01-natural-80-camera-scroll-v1` | recorded-negative-natural | lower-floor camera crossing of map cell (249,14); no selector 0x80 dispatch |
| `20260826-level01-upper-frontier-left-v1` | recorded-negative | four left-side branches from world (4372,628), all falling to the lower floor |
| `20260826-level01-lower-floor-frontier-v1` | recorded-negative | eight jump/sword branches at the lower-floor wall; no wall crossing or exit gate |
| `20260826-level01-upper-action-frontier-v1` | recorded-negative-frontier | ten direct-action branches and a breakpoint audit at the far connector; no transfer or scene exit |
| `20260826-level01-upper-platform-selector87-v1` | recorded-controlled | exact row-10 selector-0x87 dispatch, generic spawn, and Type-0x01 actor initialization |
| `20260826-level01-exit-data-reference-audit-v1` | recorded-static-audit | complete Ghidra write inventory for scene state, script cursor/data/index, and player transition gates |
| `20260826-level01-natural-gate-audit-v1` | recorded-negative-natural | far-floor actor terminal countdown and terrain-lock writes are local responses; no scene transition |
| `20260826-level01-controlled-rope-wall-interaction-v1` | recorded-negative-controlled | far-rope upper-wall dismount reaches selector 0x60 and returns to the connector; no scene transition |
| `20260826-level01-lower-frontier-terrain-response-v1` | recorded-negative-frontier | lower frontier stop-left behavior is a solid geometry probe; no scene or actor transition |
| `20260826-type01-child-chain-v1` | recorded-controlled-static-correlation | Type-0x01 upper resource F5 child decode and corrected runtime placement check |
| `20260826-level01-integrated-natural-exit-v1` | recorded-negative-natural | controller-only upper-frontier replay reaches the X boundary but falls before the Y exit condition |
| `20260826-level01-type1f-settle-v1` | recorded-negative | 600-frame no-input settle of the lower-tower type-0x1F proximity actor; no vertical transfer or scene transition |
| `20260826-level01-type21-settle-v1` | recorded-negative | 600-frame no-input settle of the selector-0x1A high-walkway type-0x21 actor; no movement or scene transition |
| `20260826-level01-selector12-dispatch-v1` | recorded-negative-frontier | fresh natural selector-0x12 dispatch trace; generic route actors only, no scene transition |
| `20260826-level01-scene-countdown-writers-v1` | recorded-negative-natural | complete countdown-writer static/runtime audit; no gameplay countdown or scene-state write |
| `20260827-level01-actor-lifecycle-v1` | recorded-lifecycle | type-0x2D player-collision cleanup at the first actor parity break; no scene transition |
| `20260827-level01-actor-refill-vm-v1` | recorded-boundary-classification | interaction-refill allocation, animation-gate crossing, and transient slot-8 cursor boundary |
| `20260827-level01-natural-gate-trace-v1` | recorded-negative-natural | fresh power-on exit-predicate and scene-gate trace through the early lower-floor frontier |
| `20260827-level01-terrain-response-callback-v1` | recorded-negative-frontier | behavior-0x47 surface-mode callback and lower-tower player-response trace |
| `20260827-level01-upper-pair-extended-interaction-v2` | recorded-negative | explicit left/up upper-frontier coverage of the Type-0x1E extended interaction branch |
| `20260827-level01-behavior46-lower-band-v2` | recorded-positive | natural behavior-0x46 Type-0x1E contact, movement handoff, and extended callback trace |
| `20260827-level01-bounce-c-matrix-v1` | recorded-negative-natural | C timing matrix from the lower bounce; no type-0x6A handhold transfer |
| `20260827-level01-player-motion-writer-audit-v1` | recorded-static-audit | complete actor-specific player-coordinate/velocity writer classification; no unclassified Level 01 writer |
| `20260827-level01-early-highledge-handhold-v1` | recorded-positive-route-extension | early high-walkway type-0x6A→0x6B handhold and x=1568 connector route; hidden ledge and exit unresolved |
| `20260827-level01-route-join-v1` | recorded-positive-route-extension | clean lower-tower reversal, three guard clearances, and chained far-rope climb; upper transfer unresolved |
| `20260827-level01-upper-band-pulse-v1` | recorded-negative-frontier | rope-top horizontal line, juggler response, lower-gap drop, and timed C-pulse family; upper transfer unresolved |
| `20260827-level01-early-top-frontier-v1` | recorded-negative-frontier | six connector-top timing branches plus type-0x65 producer/row correlation; upper transfer unresolved |
| `20260827-level01-far-rope-up-contact-v1` | recorded-positive-route-frontier | timing sweep proving the far-rope Up contact and synchronized endpoint |
| `20260827-level01-far-rope-endpoint-v1` | recorded-negative-frontier | direct dismount/jump/attack matrix from the corrected far-rope endpoint |
| `20260827-level01-actor-lifecycle-extension-v1` | completed-boundary-correction | type-0x40 replacement, F5 type-0x2A accumulator propagation, cull retention, and class-zero terrain correction |
| `20260827-level01-upstream-transfer-writer-graph-v1` | recorded-static-audit | backward player-Y/VY writer graph and x≈2112/x≈2720/row-10 producer closure; no transfer-capable local producer |
| `20260827-level01-camel-handhold-connector-v1` | recorded-positive-route-extension | live camel boost, type-0x6A→0x6B handhold, and x=1568 connector-top continuation |
| `20260827-level01-pole-input-matrix-v1` | recorded-negative-frontier | six exact-pole Up/jump/directional branches; ordinary jump arc and upper-edge fall only |
| `20260827-level01-upper-type40-contact-v1` | recorded-negative-frontier | upper type-0x40 contact breakpoints and object cleanup; no launch or scene transition |
| `20260827-level01-upper-type06-jump-sweep-v1` | recorded-negative-frontier | six walk-off/held-jump branches across the hanging type-0x06 actor; no launch or scene transition |
| `20260827-level01-peddler-guide-matrix-v2` | recorded-negative-frontier | eight synchronization-corrected peddler-side walk-off/held-jump branches; 0x86/0x85 edge fall only |
| `20260827-level01-peddler-c-timing-v1` | recorded-negative-frontier | nine synchronization-corrected held-C onset branches from the peddler platform; ordinary jump or edge fall only |
| `20260827-level01-far-floor-type1e-trace-v1` | recorded-negative-frontier | detailed far-floor Type-1E contact, local FFF0E6 timer, and deferred callback trace; no scene transition |
| `20260827-level01-upper-pair-vm-trace-v1` | recorded-negative | 180-frame neutral VM-boundary trace from the synchronized Type-0x1E/Type-0x20 activation pair; no handler or scene transition |
| `20260827-level01-early-top-delayed-dismount-v1` | recorded-negative-frontier | seven delayed-C dismount branches from the early connector top; ordinary high-walkway arc only, no upper transfer |
| `20260827-level01-far-rope-upper-resource-trace-v1` | recorded-positive-producer-frontier | natural far-rope upward dismount produces the upper Type-0x1E/Type-0x20 pair; no scene transition |
| `20260827-level01-upper-band-inventory-v1` | recorded-negative-frontier | complete x=4400–4799 upper-band interaction/terrain inventory plus clean far-rope endpoint replay; no remaining upper-band launch or transition producer |
| `20260827-level01-transition-resource-lifecycle-v1` | completed-negative-resource-lifecycle | upper-frontier boundary writer, scene resource prelude, 300 VBlank-paced service iterations, and persistent nested interaction/resource lifecycle; scene-state dispatch and transition latch writers remain unreachable from the checkpoint |
| `20260827-level01-transition-resource-prerequisite-v1` | controlled-prerequisite-confirmation | paired baseline/control proves nonzero `FFF003` holds the nested `0x001B16E0` lifecycle; controlled zero exposes the scene-script parser one debugger frame later, without proving natural transition reachability |
| `20260827-level01-transition-state3-dispatch-v1` | controlled-state3-resource-boundary | controlled parser return consumes the `0x4082` record, writes `SCENE_STATE=0x03` and cursor `0x408A`, enters state-3 resource dispatch, then remains before `0x001B2ACE` and scene completion |
| `20260827-level01-transition-state3-resource-v1` | controlled-state3-resource-dispatch | two controlled scene-3 replays observe resource status reset and the state-3 helper boundary; transition-mode, scene-table, and script-completion writers remain unreached |
| `20260827-level01-natural-transition-inventory-v1` | recorded-negative-natural-scene-writer-inventory | clean power-on route evaluates the Level 01 boundary 1105 times but reaches only `(2564,920)`; no post-boot scene-state, cursor, table, transition, latch, or completion writer fires |
| `20260827-level01-connector-top-transfer-audit-v1` | recorded-negative-natural-continuation | four controller-only connector-top dismount families reach at most ordinary jump `Y=412`; no behavior-0x29/0x2D launch, scene gate, or new transfer path fires |
| `20260827-level01-transfer-reachability-closure-v1` | recorded-trace-validated-producer-closure | fixed terrain 0x29/0x2D closure plus direct selector-0x0D/0x74 Type-0x6A/0x65 producer validation; remote actor contact/alignment remains the next targeted experiment |
| `20260828-actor-collision-terminal-response-v1` | recorded-static-decompilation | shared terminal actor-collision response, linked cleanup, type-countdown gating, and type-0x84 replacement |
| `20260828-animation-vm-core-v1` | recorded-static-decompilation | AnimationVM entry gate/core split, actor-table traversal, frame synchronization, and EA-FE dispatch |
| `20260828-camera-scroll-refill-v1` | recorded-static-decompilation | pending-scroll publication, directional VDP row/column refill, and interaction-row processor dispatch |
| `20260828-frame-terrain-services-v1` | recorded-static-decompilation | frame/resource wait handshake and player directional terrain edge probes |
| `20260828-interaction-state-services-v1` | recorded-static-decompilation | frame-gated response-counter, resource-milestone, and interaction-target services |
| `20260828-interaction-counter-drain-v1` | recorded-static-decompilation | reset-path response-counter drain and six-byte response-state synchronization |
| `20260828-level-boundary-transition-v1` | recorded-static-decompilation | level-boundary cleanup, terminal transition countdown, scene/resource reload, and frame-pipeline re-entry |
| `20260828-animation-sprite-payload-v1` | recorded-static-decompilation | AnimationVM frame expansion into actor sprite payload records for VDP submission |
| `20260828-audio-z80-bootstrap-v1` | recorded-static-decompilation | Z80 bus handoff, sound-driver copy, Z80 start, and audio bootstrap command sequence |
| `20260828-transition-render-service-v1` | recorded-static-decompilation | palette-transition loop, service-frame execution, actor-only records, and palette-step VDP writes |
| `20260828-palette-render-helpers-v1` | recorded-static-decompilation | palette-band upload, full palette clear, and parameterized palette transition helpers |
| `20260828-scene-resource-service-v1` | recorded-static-decompilation | VBlank scene-resource loop, render-frame preparation, palette fade, actor reset, and VRAM copy |
| `20260828-camera-coordinate-state-v1` | recorded-static-decompilation | complementary camera/player coordinate initialization and derived-state reconstruction |
| `20260828-render-submission-v1` | recorded-static-decompilation | sprite attribute-record construction, camera-relative actor culling, and the two VDP submission stages |
| `20260828-vdp-tile-word-v1` | recorded-static-decompilation | reusable scene/resource VDP control-address and tile-word writer |
| `20260828-scene-resource-vdp-service-v1` | recorded-static-decompilation | VBlank wait/Z80 service, VRAM word transfer, scene-resource command interpretation, and actor instantiation |
| `20260828-compressed-resource-decode-v1` | recorded-static-decompilation | RNC payload decoding, terrain-resource Huffman decoding, and VDP fill helper classification |
| `20260828-vdp-scene-setup-v1` | recorded-static-decompilation | fixed transition-plane clears, VRAM block clears, and scene-header copy |
| `20260828-scene-vdp-audio-service-v1` | recorded-static-decompilation | tile-row VDP command tables, scene-resource completion wait, raw query sampling, conditional scene-update audio, and paired VDP setup |
| `20260828-actor-resource-clear-variant-v1` | recorded-static-decompilation | A2 calling-convention variant of actor-owned resource cleanup used by linked collision teardown |
| `20260828-scene-resource-transfer-v1` | recorded-static-decompilation | indirect VDP word stream, fixed F800 scene-plane copy, and blank scene-resource frame setup |
| `20260828-options-menu-scene-table-v1` | recorded-static-decompilation | startup/options loop, selection-marker coordinate helpers, and final scene-table publication |
| `20260828-menu-soundtest-credits-v1` | recorded-static-decompilation | sound-test entry navigation/playback/redraw and credits command-stream roll |
| `20260828-scene-resource-loader-variants-v1` | recorded-static-decompilation | compact VRAM pair/base loader wrappers and palette-source publication |
| `20260828-scene-resource-c000-loaders-v1` | recorded-static-decompilation | five source-qualified C000 loaders and palette-frame contracts |
| `20260828-scene-resource-setup-loaders-v1` | recorded-static-decompilation | setup-time three-region, E000, and C000/base VRAM loader wrappers |
| `20260828-menu-input-audio-v1` | recorded-static-decompilation | controller-release predicate, trigger-binding renderer, and selection-change audio cue |
| `20260828-menu-pattern-wish-v1` | recorded-static-decompilation | hidden menu pattern recognizer and wish-prompt resource/text presentation |
| `20260828-scene-state-resource-presentations-v1` | recorded-static-decompilation | scene-script state presentation, shared reset/transition presentation, and title/intro timing/resource contracts |
| `20260828-player-terrain-response-v1` | recorded-static-decompilation | terrain-response resolver, odd/even animation selectors, and vertical-band pointer table |
| `20260828-startup-initialization-v1` | recorded-static-decompilation | reset audio/runtime setup, scene/script defaults, menu callback installation, and active-scene gate |
| `20260828-player-terrain-contact-v1` | recorded-static-decompilation | non-rising terrain contact, directional push animation/facing, camera thresholds, and local-Y grid alignment |
| `20260828-scene-state10-presentation-v1` | recorded-static-decompilation | state-10 command stream, VBlank service loop, VDP fill progression, and resource cleanup |
| `20260828-actor-allocation-entrypoints-v1` | recorded-static-decompilation | common level-object allocation, template initialization, coordinate placement, and interaction-byte consumption |
| `20260828-terrain-query-callback-installer-v1` | recorded-static-decompilation | three-pointer terrain query callback publication into the live callback slots |
| `20260828-scene-camera-orchestration-v1` | recorded-static-decompilation | active-scene initialization, interaction-triggered resource rebuild, command-stream dispatch, and viewport reconstruction |
| `20260828-interaction-counter-actors-v1` | recorded-static-decompilation | interaction-counter quotient/remainder selection of scene-rebuild actor animation roots |
| `20260828-player-terrain-state-v1` | recorded-static-decompilation | player camera-grid alignment, interaction-animation selection, and terrain-motion reset |
| `20260828-interaction-counter-decrement-v1` | recorded-static-decompilation | bounded ASCII-style interaction-counter decrement and rollover behavior |
| `20260828-scene-state-audio-command-v1` | recorded-static-decompilation | scene-state-indexed transition audio command selection |

When a campaign is superseded, leave it in this table. A negative result is
valuable because it prevents repeating the same input family.

| `20260829-level08-event-vdp-static-decompilation-v1` | recorded-static-decompilation | Level 08 event-counter update, rotating VDP record emission, timed event-stream dispatch, and conditional actor setup |
| `20260830-level08-event-command-stream-static-v1` | recorded-static-decompilation | Promoted the exact 443-record, two-byte Level-08 event command stream at 0x0000262F-0x000029A4 selected by Level08_ExitRoutine, including its terminal F2 01 scene-transition command and the zero alignment byte before HUD_INTERACTION_FRAME_SEQUENCE |
| `20260829-actor-type84-reinitialize-static-decompilation-v1` | recorded-static-decompilation | Standalone actor resource cleanup, type-0x84 template reinitialization, and transient-field reset primitive |
| `20260829-actor-scene-resource-mode-static-decompilation-v1` | recorded-static-decompilation | Conditional mode-0x11 and unconditional mode-0x16 scene-resource actor instantiation services |
| `20260829-vdp-descending-pattern-static-decompilation-v1` | recorded-static-decompilation | VRAM-start VDP writer for the 40-word descending low-byte pattern |
| `20260829-random-scaled-step-static-decompilation-v1` | recorded-static-decompilation | Shared PRNG advance with counter-scaled mixed-word output |
| `20260829-reset-bootstrap-static-decompilation-v1` | recorded-static-decompilation | Reset-vector hardware bootstrap, inline initialization table, region detection, Z80/VDP setup, and reset-time service entry |
| `20260829-reset-bootstrap-data-objects-v1` | recorded-static-decompilation | Canonical labels for reset-bootstrap initial words, hardware-pointer tuple, VDP register bytes, and follow-up VDP command |
| `20260829-no-op-service-boundaries-static-decompilation-v1` | recorded-static-decompilation | Five RTS-only reset, exit, scene-script, and scene-resource extension boundaries with proven caller positions |
| `20260829-vdp-pattern-loop-hook-static-decompilation-v1` | recorded-static-decompilation | RTS-only boundary between frame-phase service and the descending VDP pattern writer; surrounding presentation purpose remains unresolved |
| `20260829-vdp-pattern-service-frames-static-decompilation-v1` | recorded-static-decompilation | Exact 0x28-iteration frame-phase and descending VDP pattern service helper at 0x001B1CE4; surrounding mode-0x17 presentation purpose remains unresolved |
| `20260829-scene-resource-mode17-static-decompilation-v1` | recorded-static-decompilation | Exact mode-0x17 scene-resource service/setup blocks at 0x001B1C0C and 0x001B1D00, with explicit boundaries and conservative presentation naming |
| `20260829-interaction-response-counter-complete-static-decompilation-v1` | recorded-static-decompilation | Complete fixed response-counter increment family and three-digit interaction/resource progress decrement helper |
| `20260829-terrain-actor-response-dispatch-static-decompilation-v1` | recorded-static-decompilation | First-match non-player actor terrain-response tail dispatch through the 256-entry terrain handler table |
| `20260829-scene-resource-mode-hook-static-decompilation-v1` | recorded-static-decompilation | RTS-only scene-resource mode hook called after modes 0x1B, 0x1C, and 0x04 are selected |
| `20260829-terrain-actor-collision-hook-static-decompilation-v1` | recorded-static-decompilation | RTS-only actor terrain-collision hook between terrain-row selection and behavior-byte lookup |
| `20260829-interrupt-vector-static-decompilation-v1` | recorded-static-decompilation | Exception vector fan-in, line-1010 VDP loop, level-4 RTE stub, and spurious interrupt RTE stub |
| `20260829-actor-template-base-zero-static-decompilation-v1` | recorded-static-decompilation | Shared zero-type actor-template base at 0x001B79B8; exact surrounding template extent deliberately left unresolved |
| `20260829-actor-template-family-static-decompilation-v1` | recorded-static-decompilation | Nine established actor-template entry points promoted to canonical labels without asserting unproven full ranges |
| `20260829-actor-template-record-layout-static-decompilation-v1` | recorded-static-decompilation | Established the 20-byte compact actor-template record extent from the common initializer's 19 sequential source reads and the observed record cadence |
| `20260829-actor-template-additional-family-static-decompilation-v1` | recorded-static-decompilation | Six additional actor-template records tied to exact interaction, F5, terrain, and landing producers |
| `20260829-actor-template-presentation-response-static-decompilation-v1` | recorded-static-decompilation | Transition-presentation, collision-response, and scene/menu type-0x84 template records named by direct consumers |
| `20260829-actor-template-service-family-static-decompilation-v1` | recorded-static-decompilation | Scene-resource, menu, exit, Level 08, and typed interaction template records named from direct consumers |
| `20260829-actor-template-collision-interaction-static-decompilation-v1` | recorded-static-decompilation | Type-0x2D interaction and type-0x11 collision response templates named from direct consumers |
| `20260829-actor-template-interaction-dispatch-static-decompilation-v1` | recorded-static-decompilation | Type-0x87 interaction-response template named from the reverse-slot dispatch entry |
| `20260829-actor-template-interaction-family-static-decompilation-v1` | recorded-static-decompilation | Type-0x29, type-0x10, and type-0x2F interaction templates named from direct dispatch spawn bodies |
| `20260829-actor-template-interaction-dispatch-family-static-decompilation-v1` | recorded-static-decompilation | Type-0x03, type-0x05, type-0x23, type-0x2B, and type-0x84 interaction templates named from direct dispatch bodies |
| `20260829-actor-template-early-interaction-family-static-decompilation-v1` | recorded-static-decompilation | Earlier type-0x5E/type-0x84 pair, type-0x5F, type-0x67, and shared type-0x84 interaction templates named from direct consumers |
| `20260829-actor-template-level-spawn-family-static-decompilation-v1` | recorded-static-decompilation | Level/terrain spawn templates for types 0x69, 0x89, 0x76, 0x84, 0x74, and 0x32 named from direct consumers |
| `20260829-actor-template-presentation-family-static-decompilation-v1` | recorded-static-decompilation | Shared type-0x8B/type-0x1A presentation sources and type-0x53/type-0x17 interaction templates named from direct consumers |
| `20260829-actor-template-scene-setup-static-decompilation-v1` | recorded-static-decompilation | Fixed scene-setup templates and an offset type-0x84 interaction template named from direct consumers |
| `20260829-actor-template-scene-level-family-static-decompilation-v1` | recorded-static-decompilation | Scene initialization, level-event, and terminal-transition templates named from direct consumers |
| `20260829-actor-template-late-dispatch-family-static-decompilation-v1` | recorded-static-decompilation | Level 06 exit and selector-0xFD/0xFE interaction templates named from complete consumers |
| `20260829-actor-template-mid-dispatch-family-static-decompilation-v1` | recorded-static-decompilation | Shared type-0x46, type-0x5A, and type-0x55 templates named from level-event and interaction dispatch consumers |
| `20260829-actor-template-type4e-family-static-decompilation-v1` | recorded-static-decompilation | Type-0x4E and type-0x4F interaction templates named from reverse-slot consumers |
| `20260829-actor-template-special-event-family-static-decompilation-v1` | recorded-static-decompilation | Type-0x0F interaction and Level 12 type-0x2F event templates named from complete consumers |
| `20260829-actor-template-type62-family-static-decompilation-v1` | recorded-static-decompilation | Scene-gated default and scene-5 type-0x62 response templates named from the shared spawn path |
| `20260829-actor-template-late-interaction-family-static-decompilation-v1` | recorded-static-decompilation | Type-0x16, type-0x07, type-0x58, and type-0x36 templates named from late level/interaction consumers |
| `20260829-actor-template-level-entry-family-static-decompilation-v1` | recorded-static-decompilation | Shared type-0x84 and type-0x42 level-entry templates named from early callback consumers |
| `20260829-actor-template-level05-timed-static-decompilation-v1` | recorded-static-decompilation | Directly selected Level 05 type-0x7C timed-spawn template named from the level callback |
| `20260829-hud-display-frame-hooks-static-v1` | recorded-static-decompilation | Final two runtime-observed HUD presentation phase hooks named as inert RTS extension points |
| `20260829-actor-template-bootstrap-static-v1` | recorded-static-decompilation | Player boot and terminal-transition actor templates named from direct initialization consumers |
| `20260829-level-callback-family-static-v1` | recorded-static-decompilation | Complete level-table frame and exit callback matrix named from direct pointer fields |
| `20260829-actor-vm-dispatch-family-static-v1` | recorded-static-decompilation | Complete shared EA-FE/80-94 actor-VM dispatch family named from exact 68000 handler bodies |
| `20260829-level-event-command-family-static-v1` | recorded-static-decompilation | Complete 26-entry level-event command family named from exact 68000 handler bodies |
| `20260829-terrain-response-family-static-v1` | recorded-static-decompilation | Remaining terrain-response table handlers named from exact 68000 bodies and behavior-byte membership |
| `20260829-interaction-spawn-early-family-static-v1` | recorded-static-decompilation | Early interaction-table spawn and response handlers named from exact 68000 bodies and selector-specific offsets |
| `20260829-interaction-spawn-mid-family-static-v1` | recorded-static-decompilation | Mid interaction-table spawn handlers named from exact 68000 bodies, template selection, and coordinate offsets |
| `20260829-interaction-spawn-type5e84-pair-static-v1` | recorded-static-decompilation | E0-E6/E8-E9/F9 paired type-0x5E/type-0x84 interaction handlers named from exact bodies, movement streams, gates, and anchor offsets |
| `20260829-interaction-spawn-type5e-threshold-static-v1` | recorded-static-decompilation | Selector-FB thresholded type-0x5E plus C0-C7/57 type-0x29 and 3D type-0x67 interaction handlers named from exact bodies |
| `20260829-interaction-spawn-response-family-static-v1` | recorded-static-decompilation | Selectors 18/23-25/29-2A/46 response and spawn handlers named from exact templates, palette/audio side effects, and facing updates |
| `20260829-interaction-spawn-type8b-presentation-static-v1` | recorded-static-decompilation | F5-F8 shared type-0x8B presentation handlers named from exact animation-root, palette, and gate contracts |
| `20260829-interaction-spawn-upper-gated-family-static-v1` | recorded-static-decompilation | Selectors 10/12/63/64 upper-route direct and gated spawns named from exact template and flag contracts |
| `20260829-interaction-spawn-type79-type7a-static-v1` | recorded-static-decompilation | Selectors 06-08/0A-0C shared type-0x79/type-0x7A scene-interaction handlers named from exact animation-root contracts |
| `20260829-interaction-spawn-late-level-object-family-static-v1` | recorded-static-decompilation | Selectors 14/16/1C/26/3C/3E/EA late level-object and interaction handlers named from exact templates and side effects |
| `20260829-interaction-spawn-runtime47-4c-static-v1` | recorded-static-decompilation | Selectors B1-B6 shared zero-template runtime-type 47-4C handlers named from exact allocator, animation, movement, flag, and gate contracts |
| `20260829-interaction-spawn-type84-base-static-v1` | recorded-static-decompilation | Selectors B7-B9/BA/CA shared type-0x84 interaction-base handlers named from exact gates, animation roots, and Y adjustments |
| `20260829-interaction-spawn-runtime37-3b-type43-static-v1` | recorded-static-decompilation | Selectors 40/66/6B/FF zero-template and type-0x43 runtime overrides named from exact compare, allocation, field, animation, and flag contracts |
| `20260829-interaction-spawn-runtime3a-41-static-v1` | recorded-static-decompilation | Selectors 53/AC/C8-C9 zero-template runtime-type 3A/41/84/34 handlers named from exact gates, field, animation, movement, and flag contracts |
| `20260829-interaction-spawn-runtime3c-40-static-v1` | recorded-static-decompilation | Selectors 41/42/5F-61/AD zero-template runtime-type 3C/3E/3F/40 handlers named from exact gates, coordinate offsets, animation/movement streams, and post-spawn flags |
| `20260829-interaction-spawn-type46-5a-response-static-v1` | recorded-static-decompilation | Selectors 50/87/8E/90-91/A6/A9/AB/AF/BB named across shared type-0x46, type-0x5A, type-0x01, type-0x4E, type-0x4F, and runtime-type-0x44 spawn paths |
| `20260829-interaction-spawn-runtime52-presentation-static-v1` | recorded-static-decompilation | Selectors 17/2D-30/4E-4F/5B/5D/CB-CF named across shared type-0x52 presentation, type-0x53/type-0x17 interaction, palette, and type-0x84 level-event paths |
| `20260829-type34-wall-response-static-decompilation-v1` | recorded-static-decompilation | Type-0x34 player-collision replacement closed through the exact type-0x8D template and losslessly decoded response animation/control-flow cycle |
| `20260829-type21-proximity-static-decompilation-v1` | recorded-static-decompilation | Selector-0x1A type-0x21 actor stream closed through its exact frame, facing command, player-Y proximity branch, secondary-stream call, and no-movement contract |
| `20260829-type34-movement-static-decompilation-v1` | recorded-static-decompilation | Type-0x34 wall movement root closed through its 102-byte lossless stream, signed vertical oscillation, timer values, and unconditional loop target |
| `20260829-type2a-movement-static-decompilation-v1` | recorded-static-decompilation | Type-0x2A upper-route movement root closed through its exact initialization prefix, fixed vertical profile, and unconditional reset loop |
| `20260829-guard-sword-movement-static-decompilation-v1` | recorded-static-decompilation | Type-0x2D guard-sword movement override closed through its exact F5 producer, two displacement steps, flag callback, arithmetic command, and self-loop |
| `20260829-type2a-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x2A upper-route animation root closed through its exact actor-field write, six-frame sequence, and unconditional loop |
| `20260829-type1f-movement-static-decompilation-v1` | recorded-static-decompilation | Type-0x1F movement gate closed through its exact packed branch region, field tests, animation transition, clear operation, and loop targets |
| `20260829-transition-presentation-movement-static-decompilation-v1` | recorded-static-decompilation | Transition-presentation movement substream closed through its fixed horizontal delta, known consumer, and unconditional self-loop |
| `20260829-scene-resource-default-movement-static-decompilation-v1` | recorded-static-decompilation | Scene-resource default movement region closed through its exact primary delay/step loop, adjacent secondary self-loop, and direct SceneResource_InstantiateActors assignment |
| `20260829-type1e-movement-static-decompilation-v1` | recorded-static-decompilation | Type-0x1E movement family closed through the bounded state-0x46 response loop, proximity/transition predecessor gate, animation handoffs, and lower-band runtime correlation |
| `20260829-transition-presentation-lead-in-static-decompilation-v1` | recorded-static-decompilation | Transition-presentation lead-in closed through its 96-byte signed approach/return profile, scene-resource callback parameters, timer/clear commands, and continuation into the terminal drift loop |
| `20260829-type1e-animation-static-decompilation-v1` | recorded-static-decompilation | Shared proximity substream plus normal and extended Type-0x1E animation roots range-bounded, named, and connected to the state-0x46 movement handoff |
| `20260829-type1f-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x1F proximity animation root range-bounded, named, and connected to its movement install/clear paths |
| `20260829-type2f-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x2F interaction loop and Level-12 terminal-event animation roots range-bounded and connected to their direct template consumers |
| `20260829-type10-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x10 timed interaction-response animation root range-bounded with its direct child-spawn branches |
| `20260829-type21-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x21 proximity animation root range-bounded with its shared secondary-stream call and local state branch |
| `20260829-type0f-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x0F random/timed interaction animation root range-bounded with its direct child-spawn record |
| `20260829-type5f-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x5F interaction animation root range-bounded with its direct reverse-slot consumer |
| `20260829-type84-interaction-pair-companion-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x84 paired-interaction companion animation root range-bounded with its embedded Type-0x23 entry |
| `20260829-type06-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x06 selector animation range-bounded with its player gate and Type-0x2A child spawn |
| `20260829-type67-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x67 interaction animation range-bounded with its conditional child-spawn loop |
| `20260829-transition-presentation-animation-static-decompilation-v1` | recorded-static-decompilation | Transition-presentation animation root range-bounded with its flag-gated child setup and dynamic state handoff |
| `20260829-upper-type20-animation-static-decompilation-v1` | recorded-static-decompilation | Upper Type-0x20 animation family range-bounded with callable prelude, proximity gates, and response child spawn |
| `20260829-type2b-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x2B interaction animation range-bounded with child spawn, cleanup, and terminal frame loop |
| `20260829-type6c-animation-static-decompilation-v1` | recorded-static-decompilation | Promoted Type-0x6C interaction animation prefix range-bounded with its actor-field self-loop and continuation into the Type-0x69 response stream |
| `20260829-type69-animation-static-decompilation-v1` | recorded-static-decompilation | Shared Type-0x69/0x6A handhold animation family range-bounded with root, embedded template, and Type-0x6B alignment entries |
| `20260829-type89-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x89 random-variant animation range-bounded with its alternate post-spawn entry |
| `20260829-type84-terrain-response-animation-static-decompilation-v1` | recorded-static-decompilation | Shared Type-0x84 terrain-response animation range-bounded with state publication and descending frame loop |
| `20260829-type84-interaction-base-animation-static-decompilation-v1` | recorded-static-decompilation | Five selector-owned Type-0x84 interaction-base animation loops range-bounded and connected to B7–B9/BA/CA spawn roots |
| `20260829-exit-type84-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x84 exit-transition presentation stream range-bounded with actor clear, child spawn, terminal frame sequence, and loop |
| `20260829-exit-type84-child-animation-static-decompilation-v1` | recorded-static-decompilation | Exit-transition F5 child promoted to a Type-0x8C template with its event-driven random animation cycle range-bounded |
| `20260829-type7b-level11-animation-static-decompilation-v1` | recorded-static-decompilation | Level 11 Type-0x7B event animation range-bounded and connected to its direct template load; the shared second template pointer is now identified as the Type-0x74 response child |
| `20260829-type8c-landing-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x8C landing-response animation range-bounded with sound, actor-field publication, clear, and repeated response loop |
| `20260829-type43-8a-interaction-animation-static-decompilation-v1` | recorded-static-decompilation | Shared Type-0x43/0x8A interaction animation family range-bounded with embedded runtime entry, Type-0x7F child spawn, and state publication |
| `20260829-type74-terrain-exit-response-animation-static-decompilation-v1` | recorded-static-decompilation | Shared Type-0x74 terrain/Level 06 exit response family range-bounded with two embedded callable streams and direct Type-0x84/Type-0x7B child-template consumers |
| `20260829-type84-opening-level08-animation-static-decompilation-v1` | recorded-static-decompilation | Opening F5 Type-0x84 and Level 08 Type-0x84 animation streams range-bounded at their exact F600 terminators and connected to their direct template consumers |
| `20260829-type32-interaction-animation-static-decompilation-v1` | recorded-static-decompilation | Selector-0xA4 Type-0x32 interaction animation range-bounded with parameter pushes, timer phase, repeated frame sequence, and exact actor-clear terminator |
| `20260829-type14-type84-presentation-animation-static-decompilation-v1` | recorded-static-decompilation | Selector-0x13 Type-0x14 interaction root and its terminal Type-0x84 presentation child range-bounded, with direct Type-0x30 child-template ownership and exact F600 termination |
| `20260829-type30-presentation-child-stream-static-decompilation-v1` | recorded-static-decompilation | Type-0x30 presentation child animation and inline (+4,+2) movement override range-bounded and connected to its exact F5 producer and six-resource template |
| `20260829-type0b-type0c-type79-type7a-animation-static-decompilation-v1` | recorded-static-decompilation | Direct Type-0x0B/0x0C level-event and Type-0x79/0x7A interaction animation roots, shared Type-0x7A response tail, and Type-0x2C child stream range-bounded and connected to exact consumers |
| `20260829-terminal-scene-animation-static-decompilation-v1` | recorded-static-decompilation | Terminal terrain/interaction, fixed scene-initialization, scene-reset animation roots, reset movement entry, and terminal F5 child templates range-bounded with explicit shared-stream boundaries |
| `20260829-type59-scene-event-animation-static-decompilation-v1` | recorded-static-decompilation | Primary and alternate Type-0x59 Level 12 scene-event animation entries range-bounded as one shared packed region immediately before terminal terrain |
| `20260829-level-entry-animation-static-decompilation-v1` | recorded-static-decompilation | Direct Level-entry Type-0x84/0x42/0x54/0x7C/0x7D animation roots range-bounded with explicit shared handoffs and child-state boundaries; the Type-0x7C root at 0x00125916 is shared by the Level-05, wide-random-event, and Level-07 templates |
| `20260829-type05-interaction-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x05 interaction variants A/B/C range-bounded and connected to their exact template consumers, proximity gates, actor-field writes, and child-spawn branch |
| `20260829-type07-type58-interaction-stream-static-decompilation-v1` | recorded-static-decompilation | Type-0x07 movement/animation pair and Type-0x58 primary/alternate animation roots range-bounded with exact template consumers and internal control-flow entries |
| `20260830-type07-type13-movement-stream-static-v1` | recorded-static-decompilation | Corrected the Type-0x07 movement ownership boundary and promoted the directly installed Type-0x13 interaction-response movement root |
| `20260829-level-exit-terminal-animation-static-decompilation-v1` | recorded-static-decompilation | Standalone Level-exit terminal animation at 0x001252A8 range-bounded from its direct pointer installation and exact frame/offset loop before Type-0x07 animation |
| `20260829-type52-type53-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x52 movement/animation variants and Type-0x53 movement/animation stream range-bounded with exact selector consumers, internal entries, child spawns, and loop boundaries |
| `20260829-type36-interaction-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x36 interaction animation range-bounded through its exact child-spawn records, response sequence, internal entry, sound event, and terminal F600 before Level-entry data |
| `20260829-type52-level09-animation-static-decompilation-v1` | recorded-static-decompilation | Level-09 Type-0x52 entry animation range-bounded through its exact template pointer, F4 gate, local EA loop, and boundary before the separate response stream |
| `20260829-type0c-directional-response-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x0C directional response and Type-0x2E child animation closed through the common directional selector, exact F5 template record, parent return, and child self-loop boundaries |
| `20260829-type2d-guard-sword-attack-animation-static-decompilation-v1` | recorded-static-decompilation | Natural Type-0x2D guard sword-attack child animation closed through its exact template pointer, parent F5 producer, sound command, paired frame cycle, and self-loop boundary |
| `20260829-type01-type84-child-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x01 upper-resource root and F5-spawned Type-0x84 child range-bounded with exact frames, callbacks, template records, and explicit adjacent-stream boundaries |
| `20260829-type34-wall-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x34 wall actor animation range-bounded with exact frames, timers, random branch, loop, and selector-0x53 installation contract |
| `20260829-type8d-type76-terminal-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x8D response prefix, direct Type-0x76 shared continuation, and level-exit terminal alternate partitioned with exact non-overlapping boundaries |
| `20260829-guard-attack-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x0A guard parent animation range-bounded with exact frame groups, distance gate, dual Type-0x2D F5 payloads, movement contract, and terminal loop |
| `20260829-type84-death-animation-static-decompilation-v1` | recorded-static-decompilation | Type-0x84 death root and shared continuation range-bounded with exact child spawns, type/movement publication, response loop, and Type-0x5F internal handoff |
| `20260829-player-response-animation-static-decompilation-v1` | recorded-static-decompilation | Level-event presentation and shared terrain-response player streams range-bounded with exact loops and cross-stream handoffs |
| `20260829-type29-transition-shared-animation-static-decompilation-v1` | recorded-static-decompilation | Shared Type-0x29 interaction/transition animation range-bounded with exact random branches and transition handoff |
| `20260829-player-idle-animation-static-decompilation-v1` | recorded-static-decompilation | Player idle pre-roll and root range-bounded with exact response branches, Type-0x46 F5 spawn, and self-loop |
| `20260829-player-interaction-pair-animation-static-decompilation-v1` | recorded-static-decompilation | Player interaction-pair setup range-bounded with dual Type-0x84 child spawns and dynamic state handoff |
| `20260829-player-terrain-bounce-animation-static-decompilation-v1` | recorded-static-decompilation | Player terrain-bounce response root range-bounded with actor-state write, shared response handoffs, terminal frame loop, and exact boundary before interaction-pair setup |
| `20260829-player-terrain-alignment-animation-static-decompilation-v1` | recorded-static-decompilation | Player terrain-alignment response range-bounded with snap-to-grid selection, timer phases, state gates, randomized loops, and exact boundary before the dynamic selector |
| `20260829-terminal-transition-animation-static-decompilation-v1` | recorded-static-decompilation | Primary and secondary Type-0x84 terminal-transition presentation streams independently named and range-bounded before the shared Type-0x29 transition stream |
| `20260829-player-transition-response-animation-static-decompilation-v1` | recorded-static-decompilation | Player transition-flag response prefix and shared terrain-bounce prelude named and range-bounded with exact selector and stream handoffs |
| `20260829-actor-vm-lifecycle-static-decompilation-v1` | recorded-static-decompilation | F6/8C actor retirement paths separated from EC/82 cursor clearing, with exact linked cleanup and F7/8D player-facing semantics |
| `20260829-actor-collision-handler-family-static-decompilation-v1` | recorded-static-decompilation | Type 0x14/0x2B/0x2F/0x30 and shared 0x2D/0x2E/0x31 collision lifecycles separated by exact source/receiver writes and cleanup paths |
| `20260829-actor-terrain-collision-loop-static-decompilation-v1` | recorded-static-decompilation | Actor terrain sampling, actor +0x3D publication, grounded response, gravity fallback, and terrain-invalid cleanup bounded from the exact per-frame loop |
| `20260829-player-landing-response-animation-static-decompilation-v1` | recorded-static-decompilation | Six packed player landing/terrain-response animation entries named and range-bounded from exact landing, terrain-response, and special-camera selector branches |
| `20260829-player-jump-launch-animation-static-decompilation-v1` | recorded-static-decompilation | Five packed player landing-spawn, jump, special-camera, terrain-response, and launch animation entries named and range-bounded from exact selector branches |
| `20260829-player-vertical-interaction-response-animation-static-decompilation-v1` | recorded-static-decompilation | Player upward/downward terrain-response roots, selector-owned tails, interaction loop, and interaction push-down variant named from exact response-gate and terrain-state selectors |
| `20260829-player-action-transition-animation-static-decompilation-v1` | recorded-static-decompilation | Four player action-animation roots named and range-bounded from exact locomotion/action selector gates, including the encoded transition-lock actor spawn |
| `20260829-player-interaction-response-animation-static-decompilation-v1` | recorded-static-decompilation | Camera/normal interaction recovery, special-camera action, and Type-1E collision response animation variants named and range-bounded from exact consumers |
| `20260829-player-action-state-animation-static-decompilation-v1` | recorded-static-decompilation | Five remaining Player_SelectActionAnimation roots named and range-bounded for grounded push-down, terrain-timer, airborne, push-up, and transition-lock action states |
| `20260830-player-action-continuation-banks-static-v1` | recorded-static-decompilation | Four split player action AnimationVM fragments replaced by canonical contiguous continuation owners at 0x001224BA, 0x0012257C, 0x0012280C, and 0x001229C2 |
| `20260829-player-level08-terminal-terrain-animation-static-decompilation-v1` | recorded-static-decompilation | Level 08 exit presentation stream and terminal-terrain player continuation with exact selectors, control-flow handoffs, and non-overlapping boundaries |
| `20260830-player-surface-recovery-animation-static-v1` | recorded-static-decompilation | Behavior-0x27 player surface-recovery stream at 0x001223D0 named and range-bounded immediately before the apple-throw action stream |
| `20260829-shared-movement-streams-static-decompilation-v1` | recorded-static-decompilation | Shared child-entry movement prelude, Type-0x84 menu-presentation loop, and Type-0x34 wall oscillation named and range-bounded from exact template consumers |
| `20260829-player-actor-launch-animation-static-decompilation-v1` | recorded-static-decompilation | Final generic player animation entry named and range-bounded from its direct actor-collision selector; the vertical-launch handler is now explicitly documented |
| `20260829-camera-damping-tables-static-v1` | recorded-static-decompilation | Horizontal and vertical camera-follow damping tables named and range-bounded from Camera_UpdateFollow, with exact lookup and coordinate-update contracts |
| `20260829-collision-dispatch-table-extents-static-v1` | recorded-static-decompilation | Player and actor collision dispatch tables range-bounded from the exact type guards: 127 player entries through 0x7E and 50 actor entries through 0x31 |
| `20260829-fixed-vdp-source-extents-static-v1` | recorded-static-decompilation | Fixed level/scene F800 tilemap sources and initial C000 palette words range-bounded from their exact VDP copy counts |
| `20260829-player-core-animation-entry-extents-static-v1` | recorded-static-decompilation | Player RUN/JUMP/THROW/SWORD entries and the sword response continuation bounded from adjacent stream roots; BRAKE recorded as an alias into transition presentation |
| `20260829-player-vertical-animation-table-extents-static-v1` | recorded-static-decompilation | Player vertical animation selector table bounded as sixteen longwords from 0x00121828 through 0x00121867, immediately before its eight repeated stream roots |
| `20260830-player-terrain-stop-align-animation-static-v1` | recorded-static-decompilation | Behavior-0x2B player terrain-block stop/alignment prelude at 0x0012181A named and range-bounded as a 14-byte stream immediately before the vertical animation table |
| `20260830-player-animation-branch-continuations-static-v1` | recorded-static-decompilation | Five player AnimationVM branch continuations/selectors in the 0x00121AC8-0x0012222D bank named and range-bounded from exact neighboring stream targets |
| `20260829-player-terrain-response-entry-aliases-static-v1` | recorded-static-decompilation | Upward and downward terrain-response tails recorded as embedded aliases at offsets 98 and 76 inside their owning streams, preventing duplicate ROM ownership |
| `20260829-type8d-wall-response-frame-extents-static-v1` | recorded-static-decompilation | First Type-0x8D wall-response Chopper frame bounded as the exact 18-byte record at 0x001EF4D4-0x001EF4E5 before the next frame |
| `20260829-type34-wall-response-frame-extents-static-v1` | recorded-static-decompilation | Four Type-0x34 wall-response Chopper frames bounded as exact 66/66/42/66-byte records at 0x001F3DEE-0x001F3EDD |
| `20260829-actor-allocation-initialization-static-v1` | recorded-static-decompilation | Actor allocator pools, template-to-record field initialization, cleared/untouched fields, and linked F5 child lifecycle recorded from the shared VM helpers |
| `20260829-collision-pass-geometry-static-v1` | recorded-static-decompilation | Player and actor collision passes recorded with exact actor pools, type guards, frame-bound geometry, and dispatch direction |
| `20260829-movement-terrain-handshake-static-v1` | recorded-static-decompilation | MovementVM, actor terrain sampling, player terrain-cell resolution, and player motion integration recorded as one explicit frame-handshake contract |
| `20260829-scene-resource-command-table-static-v1` | recorded-static-decompilation | Complete 16-command scene-resource dispatcher, shared interpreter epilogue, and handler family named from the exact pointer table and 0x001B22FA-0x001B2431 68000 implementation |
| `20260829-scene-resource-presentation-scratch-static-v1` | recorded-static-decompilation | Scene-resource presentation scratch wrapper at 0x001B21E4 and its exact lifetime around the common command interpreter named and bounded |
| `20260829-progressive-palette-services-static-decompilation-v1` | recorded-static-decompilation | Three complete progressive palette services at 0x001B26F0, 0x001B29B0, and 0x001B2A26 named with exact iteration, VDP, and component-convergence contracts; overlapping alternate entries remain aliases/unknown |
| `20260829-scene-render-services-static-decompilation-v1` | recorded-static-decompilation | Five complete adjacent scene/render services at 0x001B2E88, 0x001B2EC2, 0x001B2EE2, 0x001B2F00, and 0x001B2F7E named with exact VDP, service-loop, palette-word, and component-step contracts |
| `20260829-scene-terrain-utility-static-decompilation-v1` | recorded-static-decompilation | Three complete scene/terrain utility bodies at 0x001B32B4, 0x001B3312, and 0x001B3406 named with exact VBlank delay, scratch-clear, and terrain-row write contracts |
| `20260829-scene-transition-digit-wrapper-static-decompilation-v1` | recorded-static-decompilation | Five fixed-divisor scene-transition digit wrappers named, with the units entry preserved as a fallthrough alias into SceneTransition_WriteDecimalDigit |
| `20260829-scene-resource-query-vram-debug-static-decompilation-v1` | recorded-static-decompilation | Gated 108-byte query-to-VDP stream at 0x001B30F0 named conservatively, including its fixed 1800-word readback and intentional infinite-loop diagnostic path |
| `20260829-embedded-word-pair-static-decompilation-v1` | recorded-static-decompilation | Compact 0x001B32C8-0x001B32E1 gap partitioned into a neutral 22-byte add-with-carry helper and adjacent 0x31415926 value, with unresolved ROM-write semantics recorded |
| `20260829-scene-transition-palette-callback-static-decompilation-v1` | recorded-static-decompilation | Transition movement callback pair at 0x001B1640 and 0x001B1656 named and bounded, including their palette uploads and SCENE_TRANSITION_TILE_BAND_READY latch effects |
| `20260829-menu-options-presentation-static-decompilation-v1` | recorded-static-decompilation | Former 0x001B437E-0x001B43C3 code/data gap corrected to one 70-byte menu presentation helper, with the embedded Ghidra data false positive removed by function ownership |
| `20260829-scene-resource-queued-actor-static-decompilation-v1` | recorded-static-decompilation | Scene-resource queued-actor helper at 0x001B4802-0x001B4835 named and bounded with its spawn gate, X/Y cursor, common slot allocator, fixed Type-0x84 template, coordinate publication, and success-only cursor advance |
| `20260829-player-action-transition-child-template-static-v1` | recorded-static-decompilation | Exact F5 child template at 0x001B7918 promoted from the player action-transition stream, with Type-0x80 identity, shared child movement, animation root, resource count, and transition-state flags recorded |
| `20260829-type84-wall-response-child-template-static-v1` | recorded-static-decompilation | Type-0x84 child template at 0x001B7CEC promoted from the four exact F5 records in the Type-0x8D wall-response stream, with its animation root and two-resource ownership recorded |
| `20260829-type84-death-child-template-static-v1` | recorded-static-decompilation | Type-0x84 death-response child template at 0x001B7954 promoted from the two exact F5 records in the death animation root, with its paired inline entries and two-resource ownership recorded |
| `20260829-animation-f5-child-template-family-static-v1` | recorded-static-decompilation | Five directly evidenced AnimationVM child templates promoted at 0x001B7AD0, 0x001B7B20, 0x001B7BFC, 0x001B7C88, and 0x001B8110, covering Type-0x36, Type-0x2B, Type-0x10, Type-0x0F, and shared interaction-pair F5 producers |
| `20260829-type11-interaction-palette-static-v1` | recorded-static-decompilation | Selector-0x1F Type-0x11 interaction handler at 0x001B6CB4 promoted with exact template, allocator, and palette-upload contract |
| `20260829-type12-presentation-template-static-v1` | recorded-static-decompilation | Selector-0x4D Type-0x12 presentation handler at 0x001B6CCE and its exact 20-byte template at 0x001B7EA4 promoted with palette, presentation-flag, and audio side effects |
| `20260829-type50-level09-template-static-v1` | recorded-static-decompilation | Level-09 Type-0x50 spawn base at 0x001B7FA8 promoted from the exact backward-slot allocation and caller-installed movement/animation contract |
| `20260829-type7c-level07-template-static-v1` | recorded-static-decompilation | Level-07 Type-0x7C callback spawn at 0x001B81C4 promoted from its three direct allocation sites and exact shared movement/animation contract |
| `20260829-type3d-collision-child-template-static-v1` | recorded-static-decompilation | Type-0x3D collision-response child at 0x001B8368 promoted from the Type-0x13 actor/player collision handlers and their exact position/facing/timer contract |
| `20260829-level08-exit-type60-template-static-v1` | recorded-static-decompilation | Level-08 Type-0x60 exit-presentation template at 0x001B7DC8 promoted from the direct Level08_ExitRoutine initializer, player-position copy, and dedicated animation-cursor publication |
| `20260829-menu-transition-template-family-static-v1` | recorded-static-decompilation | Menu/scene-transition Type-0x84 templates at 0x001B7D3C, 0x001B7D50, and 0x001B7D64 promoted from the known menu F5 children and direct scene-table transition initializer |
| `20260829-animation-f5-nested-child-family-static-v1` | recorded-static-decompilation | Nested AnimationVM Type-0x84 child templates at 0x001B8124 and 0x001B837C promoted from their exact F5 producer records and inline animation-entry payloads |
| `20260829-level-exit-type84-child-template-static-v1` | recorded-static-decompilation | Direct Level_ExitAndTerminalTransition Type-0x84 child template at 0x001B7E7C promoted from its exact initializer site, embedded Type-0x74 animation entry, and source-position copy contract |
| `20260829-actor-template-f5-moving-child-base-static-v1` | recorded-static-decompilation | Shared Type-0x84 F5 moving-child base at 0x001B7F08 promoted from ten exact producer records across the 0x00122E10 and 0x00124C1A stream families |
| `20260829-actor-template-type84-runtime47-4c-child-static-v1` | recorded-static-decompilation | Shared Type-0x84 child at 0x001B828C promoted from the two exact F5 records in the runtime-0x47-to-0x4C interaction root and its two embedded Type-0x59 animation entries |
| `20260829-actor-template-type84-type87-response-child-static-v1` | recorded-static-decompilation | Type-0x84 child at 0x001B7C60 promoted from the direct F5 producer in the Type-0x87 interaction-response root and its exact template/animation contract |
| `20260829-actor-frame-phase-child-static-v1` | recorded-static-decompilation | Frame-phase actor allocator at 0x001ABF28 and zero-type moving-child template at 0x001B79F4 named from exact phase selection, velocity, position, and initializer behavior |
| `20260829-actor-template-type84-type1a-presentation-child-static-v1` | recorded-static-decompilation | Type-0x84 child template at 0x001B800C promoted from the direct F5 producer in the shared Type-0x1A/0x1B/0x1C presentation stream |
| `20260829-actor-template-type31-f5-child-family-static-v1` | recorded-static-decompilation | Paired Type-0x31 F5 child templates at 0x001B8034 and 0x001B8048 promoted from seven exact producers in the 0x00124F30 animation region |
| `20260829-actor-template-type84-type4d-child-family-static-v1` | recorded-static-decompilation | Type-0x84 and Type-0x4D child templates at 0x001B83A4 and 0x001B83B8 promoted with their bounded parent/child animation streams and exact F5 producers |
| `20260829-actor-template-menu-secondary-static-v1` | recorded-static-decompilation | Direct secondary Type-0x84 menu-presentation template at 0x001B846C and its 0x00126118 animation entry promoted from Menu_RunOptionsAndSelectNextState |
| `20260829-actor-template-scene-rebuild-static-v1` | recorded-static-decompilation | Fixed scene/resource-rebuild Type-0x84 templates at 0x001B8430 and 0x001B8444 promoted from SceneResource_RebuildAfterInteraction, with the 0x001260DA animation entry bounded |
| `20260829-actor-template-scene-resource-family-static-v1` | recorded-static-decompilation | Four Type-0x84 scene-resource actor records at 0x001B83E0-0x001B842F promoted from command-0x0F records, with their bounded animation entries and the 0x00123FD2 F5 child relationship recorded |
| `20260829-player-collision-direct-entry-family-static-v1` | recorded-static-decompilation | Six direct player-collision dispatch entries at 0x001AE64C and 0x001AE9A8-0x001AEA47 promoted with exact boundaries, table type mappings, Type-0x43 local conversion semantics, shared Type-0x06/0x0F response delegation, and interaction-gated Type-0x0C/0x1A/0x1B/0x1C death-template cleanup |
| `20260829-player-collision-shared-entry-family-static-v1` | recorded-static-decompilation | Four shared/fall-through player-collision entries at 0x001AEA48, 0x001AEB7C, 0x001AEBA4, and 0x001AEBDC promoted with exact table type mappings, interaction/presentation semantics, and explicit type-0x7D fall-through into the type-0x78/0x7A proximity tail |
| `20260829-player-collision-response-entry-family-static-v1` | recorded-static-decompilation | Seven remaining compact player-collision entries at 0x001AE722, 0x001AE9D4, 0x001AEB7A, and 0x001AED86-0x001AEE3F promoted with exact table mappings, directional terrain-push behavior, no-op/delegation identities, and type-0x84 response/companion allocation semantics |
| `20260829-player-collision-response-counter-family-static-v1` | recorded-static-decompilation | Eleven response-counter/difficulty/event-latch player-collision entries at 0x001AEECA-0x001AF0B7 promoted with exact table mappings and non-overlapping boundaries, including the type-0x44/0x45 counters, type-0x46 difficulty increment, and type-0x47-0x4C event latches converging on Actor_ReinitializeFromCollisionTemplate |
| `20260829-player-collision-launch-placement-family-static-v1` | recorded-static-decompilation | Type-0x4D placement and shared type-0x11/0x12 launch handlers at 0x001AF0B8 and 0x001AF110 promoted with exact bounds, velocity/camera writes, animation selection, and the existing negative Level 01 reachability qualification |
| `20260829-player-collision-direct-transfer-family-static-v1` | recorded-static-decompilation | Type-0x4F and type-0x4E direct transfer writers at 0x001AFC4E and 0x001AFCD2 promoted with exact bounds, +/-0x0800/-0x0700 vertical launch responses, type-0x4E horizontal velocity selection, shared transition animation, terrain-response latches, and negative Level 01 reachability qualification |
| `20260829-player-collision-actor-relative-placement-family-static-v1` | recorded-static-decompilation | Five actor-relative player placement/terminal handlers at 0x001AF740-0x001AF8F5 promoted with exact table mappings, vertical snap thresholds, signed horizontal offsets, type transitions, animation/audio effects, and Level 01 reachability qualification |
| `20260829-player-collision-global-placement-family-static-v1` | recorded-static-decompilation | Shared type-0x50/0x51 player-collision handler at 0x001AF8F6 promoted with exact actor-to-player coordinate formulas, transition/camera latches, type promotion, animation root, dispatch boundary, and Level 01 reachability qualification |
| `20260829-player-collision-transition-family-static-v1` | recorded-static-decompilation | Three dense transition handlers at 0x001AF9F6, 0x001AFA84, and 0x001AFB36 promoted for Type-0x76/0x77, Type-0x74/0x75, and Type-0x6E through Type-0x73 with exact placement windows, transition state, child allocation, stream roots, and boundaries |
| `20260829-player-collision-response-helper-static-v1` | recorded-static-decompilation | Type-0x23 collision-response pair builder at 0x001ABFFA and its zero-template/source-position helper at 0x001AC0BA promoted with exact source conversion, companion allocations, templates, animation roots, and bounds |
| `20260829-player-collision-type4e-audio-helper-static-v1` | recorded-static-decompilation | Final mechanical queue entry at 0x001AFD32 promoted as the Type-0x4E randomized response-audio helper with exact PRNG bit selection, event-0x46/0x47 gate, audio calls, and function boundary |
| `20260829-audio-z80-driver-image-static-v1` | recorded-static-decompilation | Exact 0x001B8480-0x001B9D05 Z80 sound-driver ROM image promoted as AUDIO_DATA from Audio_LoadZ80Driver's reset copy loop, with internal Z80 boundaries intentionally left unresolved |
| `20260829-audio-z80-tables-static-v1` | recorded-static-decompilation | Promoted the contiguous Z80-consumed ROM tables at 0x001B9D06-0x001BB052: the 125-entry patch table/records, secondary channel-parameter table, and 0x72-entry sequence pointer table; corrected the driver extractor to honor the 0x001B9D06 exclusive copy boundary |
| `20260829-sparse-ghidra-function-bodies-v1` | recorded-tooling | Preserved discontiguous Ghidra function-body ranges in the whole-ROM export and layout/query services so sparse functions no longer claim unrelated ROM gaps |
| `20260829-sprite-graphics-corpus-static-v1` | recorded-static-decompilation | Promoted the Chopper sprite pointer table at 0x000006AE, 1409 contiguous frame records at 0x001E5A24-0x001FE9C5, and the contiguous referenced tile payload at 0x00010000-0x0011E15F |
| `20260829-level-block-dictionary-static-v1` | recorded-static-decompilation | Promoted eight exact 8-byte level background-block dictionaries at 0x00145B2C-0x001758E3 from every map's largest referenced offset, resolving 196024 bytes as LEVEL_DATA without claiming adjacent compressed resources |
| `20260829-rom-padding-static-v1` | recorded-static-decompilation | Promoted the byte-uniform $FF fill at 0x000069E0-0x0000FFFF and $00 tail at 0x001FE9C6-0x001FFFFF as explicit PADDING, leaving mixed-content gaps untouched |
| `20260829-scene-transition-stream-static-v1` | recorded-static-decompilation | Promoted the four adjacent 0x70A-byte scene-resource command streams selected by the 0x00004B04 table, using the direct SceneResource_ProcessCommandStream consumer and matching 0x00 0x00 terminator tails |
| `20260829-terrain-collision-profile-static-v1` | recorded-static-decompilation | Promoted the 256x16 terrain collision-profile table at 0x00002FD2-0x00003FD1 from the player and actor terrain consumers, plus the three-entry difficulty-label pointer table and text streams at 0x00003FD2-0x00003FFF |
| `20260829-actor-animation-stream-family-static-v1` | recorded-static-decompilation | Range-bound the directly selected actor AnimationVM entries at 0x00124A2E-0x00125109, including presentation variants, interaction/collision responses, scene setup, Type-0x31 child entries, terrain variants, and the inline Type-0x30 child stream |
| `20260829-actor-animation-collision-child-sequence-static-v1` | recorded-static-decompilation | Split the former 0x00124D5C-0x00124F95 gap into the Type-0x16 collision-response F5 sequence and the following Type-0x31 child-spawn prefix at their exact payload and F600 boundaries |
| `20260829-scene-resource-presentation-stream-extents-static-v1` | recorded-static-decompilation | Promoted eight directly wrapper-selected scene-resource command streams at 0x00127834-0x00127E7F with exact command-0 boundaries, while leaving the adjacent unreferenced narrative/object block unresolved |
| `20260829-credits-stream-extents-static-v1` | recorded-static-decompilation | Promoted the complete 0x00127E8C-0x00128E44 credits command/text corpus from its two direct consumers and specialized 0xFD terminal, leaving the adjacent sound-test records separate |
| `20260829-sound-test-entry-table-extents-static-v1` | recorded-static-decompilation | Promoted the complete 94-entry plus sentinel sound-test command/label table at 0x0012675E-0x00126D4D from fixed-size navigation and reset-pointer evidence |
| `20260829-scene-resource-mode-record-table-extents-static-v1` | recorded-static-decompilation | Promoted the 26 fixed 12-byte scene-resource mode records at 0x00126D7E-0x00126EB5 from SceneResource_InstantiateActors index arithmetic and the following text boundary |
| `20260829-scene-resource-mode-stream-extents-static-v1` | recorded-static-decompilation | Promoted the 26 individually selected NUL-terminated scene-resource mode streams at 0x00126F0E-0x001270A6, including the command-prefixed presentation messages and exact contiguous terminators |
| `20260829-scene-resource-presentation-stream-extents-static-v1` | recorded-static-decompilation | Promoted eight direct presentation-wrapper command streams at 0x001270A8-0x001277C4 with interpreter-derived command-0 terminals, while leaving intervening unreferenced gaps unresolved |
| `20260829-scene-palette-bank-extents-static-v1` | recorded-static-decompilation | Promoted the two exact 128-byte four-band palette sources at 0x00128ED2-0x00128F51 and 0x00128FD2-0x00129051 from the shared transition helper's 64-word source contract |
| `20260829-scene-transition-mode-stream-extents-static-v1` | recorded-static-decompilation | Promoted the directly selected 0x0012622E scene-transition presentation stream through its exact first command-0 terminal at 0x00126511, while leaving adjacent ON/OFF labels and the separate 0x0012651A cursor stream unresolved |
| `20260829-scene-transition-menu-stream-extents-static-v1` | recorded-static-decompilation | Promoted nine directly selected transition/menu command-text streams at 0x00126512-0x0012671D with exact interpreter terminals, including the cursor, status-selected rows, wish prompt, and options presentation corpus |
| `20260829-fixed-palette-source-extents-static-v1` | recorded-static-decompilation | Promoted fourteen fixed 16-word palette sources at 0x00129012, 0x00129092, 0x001290B2, 0x00129152, interaction banks through 0x00129631, and transition banks at 0x00129B52-0x00129B91 from exact palette upload loops and direct consumers |
| `20260829-scene-transition-graphics-extents-static-v1` | recorded-static-decompilation | Promoted the scene-transition tile tables at 0x00129B92-0x00129BD1 and 0x00129C52-0x00129C89 plus the terminal palette sources at 0x00129BD2-0x00129C51 and 0x00129C8A-0x00129CA9 from exact frame-offset and VDP copy contracts |
| `20260829-title-menu-graphics-extents-static-v1` | recorded-static-decompilation | Promoted the title transition palette, scene/wish band-0 sources, and selection-marker tile table at 0x00129CEA-0x00129DFF from exact palette and frame-offset copy contracts |
| `20260829-scene-resource-palette-source-extents-static-v1` | recorded-static-decompilation | Promoted seventeen scene-resource/menu palette sources at 0x001297F2-0x00129AD1 from exact band upload and four-band transition contracts, leaving 0x00129A32-0x00129A91 unresolved |
| `20260829-scene-reset-credits-palette-extents-static-v1` | recorded-static-decompilation | Promoted the reset-scene and credits four-band palette sources at 0x00129E00-0x00129E7F and 0x00129E80-0x00129EFF from direct Render_RunPaletteTransitionFrom consumers |
| `20260829-level-palette-source-extents-static-v1` | recorded-static-decompilation | Promoted the 13 LEVEL_TABLE palette selections as exact four-band sources at 0x00129052-0x001296B1, including the state-07 remainder after its existing first palette band |
| `20260829-type0f-child-and-type6e-default-animation-static-v1` | recorded-static-decompilation | Promoted the exact Type-0x0F F5 child animation at 0x00123D34-0x00123DE1, shared Type-0x6E-through-0x73 base-template default stream at 0x00123DEA-0x00123E35, runtime-0x47-through-0x4C child-spawn root at 0x00123E36-0x00123E75, and compact Type-0x84 interaction response at 0x00123E76-0x00123E7D, leaving the unreferenced 0x00123DE2 self-loop unresolved |
| `20260829-mid-actor-animation-stream-extents-static-v1` | recorded-static-decompilation | Promoted six exact mid-band actor AnimationVM roots at 0x00122C40-0x00122C65, 0x00122C66-0x00122CAB, 0x00122D54-0x00122D91, 0x00122DD8-0x00122DED, 0x00122DEE-0x00122DF1, and 0x00122DF2-0x00122E15 from direct consumers and decoded terminals, leaving the 0x00122DB2 fragment unresolved |
| `20260829-type84-type01-response-animation-static-v1` | recorded-static-decompilation | Promoted the exact shared Type-0x84 response at 0x00122DB2-0x00122DD7 from the 0x001AFD84 and 0x001ABF9C producers, its ED type-0x01 publication, and EA return to ACTOR_ANIM_TYPE01 |
| `20260829-type03-collision-response-animation-static-v1` | recorded-static-decompilation | Promoted the Type-0x03 player-collision Type-0x84 response family at 0x00122E16-0x00122F37, including the six moving-child F5 placements, nested spawn entry, shared response loop, and alias-only internal entries at 0x00122E96 and 0x00122F06 |
| `20260829-type84-shared-0f22-response-animation-static-v1` | recorded-static-decompilation | Promoted the shared Type-0x84 0x0F22 response stream at 0x00122F80-0x00122FA1 from queued-actor, collision-response, and adjacent Type-0x84 template references, with its random flag gate and F600 terminal |
| `20260829-type87-interaction-response-animation-family-static-v1` | recorded-static-decompilation | Promoted the complete Type-0x87 interaction-response animation family at 0x00123AC4-0x00123CF7, including two FC-return presentation phases, the direct Type-0x87 root, ten collision-response F5 spawns, the embedded loop aliases, and the Type-0x84 child response |
| `20260829-type37-interaction-response-animation-static-v1` | recorded-static-decompilation | Promoted the Type-0x37 selector-0x6B interaction-response stream at 0x00123F10-0x00123F7D, including its timer, random flag gate, frame loop, and exact boundary before the scene-resource response prefix |
| `20260829-upper-collision-response-animation-family-static-v1` | recorded-static-decompilation | Promoted the contiguous upper collision/interaction AnimationVM family at 0x001233CC-0x00123559: the Type-0x20 collision response, Type-0x1D interaction composite with five alias entries, and direct Type-0x1E/Type-0x21 normal and alternate response roots |
| `20260829-type84-interaction-base-b6-static-v1` | recorded-static-decompilation | Promoted the selector-0xB6 Type-0x84 interaction-base handler at 0x001B70F8 and its exact 26-byte animation loop at 0x001242B0, closing the B6-B9/BA/CA family and correcting the prior unclaimed-fragment finding |
| `20260829-interaction-counter-animation-bank-static-v1` | recorded-static-decompilation | Promoted the 43-entry counter-indexed root table at 0x00004A58-0x00004B03 and its 42-entry frame/EC01 animation bank at 0x00122CAC-0x00122D53, preserving the sparse reserved table slots and exact boundary before ACTOR_ANIM_TYPE55_INTERACTION |
| `20260829-upper-type1f-type22-collision-animation-static-v1` | recorded-static-decompilation | Promoted the contiguous 0x0012384A-0x001238FF Type-0x1F/Type-0x22 player-collision family: two normal response roots, the Type-0x22 alternate response with two aliases, and the Type-0x1F alternate prefix into ACTOR_ANIM_TYPE1F_PROXIMITY_GATE |
| `20260829-player-action-transition-child-animation-static-v1` | recorded-static-decompilation | Promoted the exact 22-byte Type-0x80 child animation loop at 0x00122B58-0x00122B6D from the direct F5 template at 0x001B7918 and the player action-transition stream |
| `20260829-type10-collision-response-animation-static-v1` | recorded-static-decompilation | Promoted the exact Type-0x10 actor-collision response root at 0x001239A0-0x001239C9 and its continuation alias at 0x001239DE into the existing Type-0x10 interaction-response owner |
| `20260829-mid-actor-collision-animation-family-static-v1` | recorded-static-decompilation | Promoted the contiguous 0x00122B6E-0x00122C1D actor collision/interaction family: Type-0x84 Type-2D/2E/31 response with child variants, shared Type-0x3A/3B response, and compact Type-0x40 interaction root |
| `20260829-direct-movement-response-streams-static-v1` | recorded-static-decompilation | Promoted four directly installed actor MovementVM roots at 0x001209BE, 0x001209F0, 0x001209F8, and 0x00120A42 with exact producer writes, decoder boundaries, and corrected movement-versus-animation field semantics |
| `20260829-shared-type3c-3d-3e-3f-movement-static-v1` | recorded-static-decompilation | Promoted the shared 70-byte Type-0x3C/0x3D/0x3E/0x3F response trajectory at 0x0012146C from three interaction-handler writes and the Type-0x3D collision-child template |
| `20260829-level08-exit-movement-stream-static-v1` | recorded-static-decompilation | Promoted the exact 110-byte Level-08 Type-0x60 exit-presentation movement stream at 0x0012152A-0x00121597 from its direct template selection and self-loop boundary |
| `20260829-level-event-movement-stream-family-static-v1` | recorded-static-decompilation | Promoted four directly selected MovementVM roots at 0x00120FB4, 0x00120FDE, 0x00120FFE, and 0x00121082 from level-event producers and the Type-0x17 actor template, while leaving the preceding 0x00121038-0x00121081 bytes unresolved |
| `20260829-player-response-reset-fallback-animation-static-v1` | recorded-static-decompilation | Promoted the exact eight-byte player AnimationVM response-reset fallback at 0x00121C28 from the final Player_ResetAnimationResponseGate selection path at 0x001AD2D4 |
| `20260829-actor-type4d-collision-response-animation-static-v1` | recorded-static-decompilation | Promoted the exact 16-byte Type-0x12 actor response stream at 0x001222C2 installed by ActorType4D_PlayerCollisionHandler at 0x001AF0CA, preserving its actor-owned distinction from neighboring player terrain streams |
| `20260829-player-animation-lookup-family-static-v1` | recorded-static-decompilation | Promoted eight vertical-band player animation preludes at 0x00121868-0x001218D7, the ten-entry interaction-animation table at 0x001218D8-0x001218FF, and ten table-selected interaction preludes at 0x00121900-0x00121963 with exact decoder boundaries |
| `20260829-actor-type41-interaction-response-animation-static-v1` | recorded-static-decompilation | Promoted the exact 70-byte runtime type-0x41 interaction-response animation at 0x00125D7E-0x00125DC3 from interaction selector 0xC8, including its two F5 child records and local loop, while leaving the preceding unselected fragment unresolved |
| `20260829-type84-interaction-fd-fe-animation-static-v1` | recorded-static-decompilation | Promoted the selector-0xFD/0xFE Type-0x84 interaction AnimationVM family at 0x00125DEA-0x00125E71 as a 30-byte root plus two exact conditional response entries selected by ACTOR_TEMPLATE_TYPE_84_INTERACTION_FD_FE |
| `20260829-type84-unreferenced-response-family-static-v1` | recorded-static-decompilation | Exact ownership for the Type-0x84 0x0F22 wall-response template at 0x001B8304, its decompiled 90-byte movement prefix at 0x00121412-0x0012146B, and its decompiled 38-byte animation loop at 0x00125D58-0x00125D7D; the template family remains provisional because runtime reachability is unresolved, and the movement prefix spawns the Type-0x8D wall-response template |
| `20260829-scene-reset-secondary-animation-static-v1` | recorded-static-decompilation | Promoted the exact 108-byte scene-reset secondary animation at 0x00125EEE-0x00125F59 and its 42-byte-offset embedded active-scene entry at 0x00125F18, while keeping the adjacent nested F5 stream separate |
| `20260829-scene-transition-closing-stream-static-v1` | recorded-static-decompilation | Promoted the six-byte transition-closing command stream at 0x00128E45-0x00128E4A, its embedded base-0x0000 entry at 0x00128E49, and the two-byte sound-test presentation stream at 0x00128E4D-0x00128E4E |
| `20260829-menu-sound-test-shared-presentation-stream-static-v1` | recorded-static-decompilation | Promoted the shared 86-byte menu/sound-test scene-resource presentation stream at 0x00128E5B-0x00128EB0 from four direct callers and its first command-0 terminator, leaving 0x00128EB1-0x00128ED1 unresolved before SCENE_BLANK_PALETTE |
| `20260829-menu-palette-record-bank-static-v1` | recorded-static-decompilation | Promoted the exact ten-record, 320-byte Genesis palette bank at 0x001296B2-0x001297F1 from its menu staging load and palette-shaped record partition, while leaving active upload timing and individual record roles unresolved |
| `20260829-interaction-spawn-type5e84-pair-movement-streams-static-v1` | recorded-static-decompilation | Promoted the five directly selected Type-0x5E interaction-pair MovementVM roots at 0x0011FD18-0x00120351 for selectors E3/E4/E5, E6, F9, E8, and E9 with exact terminal-jump boundaries and a separate shared continuation at 0x0011F890 |
| `20260829-type84-death-terminal-movement-static-v1` | recorded-static-decompilation | Promoted the 14-byte Type-0x84 death-terminal MovementVM loop at 0x00120352-0x0012035F from the exact ED root publication in ACTOR_ANIM_DEATH_84_SHARED_CONTINUATION |
| `20260829-actor-template-type31-f5-child-movement-static-v1` | recorded-static-decompilation | Promoted the shared 536-byte Type-0x31 F5 child MovementVM trajectory at 0x00120B62-0x00120D79 from both exact template records, including its conditional terminal variants and local loop |
| `20260829-actor-template-type84-runtime47-4c-child-movement-static-v1` | recorded-static-decompilation | Promoted the shared 106-byte Type-0x84 runtime-0x47-through-0x4C child MovementVM response bank at 0x00121256-0x001212BF from its exact template pointer and five random alternatives |
| `20260829-type52-level09-movement-static-v1` | recorded-static-decompilation | Promoted the exact 226-byte Level-09 Type-0x52 MovementVM entry at 0x00121300-0x001213E1 from the direct template installation and terminal self-loop |
| `20260829-actor-template-type64-movement-static-v1` | recorded-static-decompilation | Promoted the exact 44-byte Type-0x64 interaction MovementVM stream at 0x00120B36-0x00120B61 from the direct template pointer and local terminal loop |
| `20260829-type7b-level11-movement-static-v1` | recorded-static-decompilation | Promoted the exact 24-byte primary Type-0x7B Level-11 event MovementVM loop at 0x0012120E-0x00121225 from its direct template pointer and terminal self-loop, leaving adjacent conditional alternate entries unresolved |
| `20260829-type7b-level11-movement-alternate-entries-static-v1` | recorded-static-decompilation | Closed the two conditional Type-0x7B Level-11 movement entries at 0x00121226-0x0012123D and 0x0012123E-0x0012123F, with the latter falling into the existing guard-sword owner |
| `20260829-type84-presentation-response-movement-static-v1` | recorded-static-decompilation | Promoted the exact 120-byte Type-0x84 presentation-child movement response at 0x001214B2-0x00121529, including its conditional 0x00121502 settle entry and terminal movement-state clear before the Level-08 exit stream |
| `20260829-type2f-interaction-movement-static-v1` | recorded-static-decompilation | Promoted the Type-0x2F interaction MovementVM block at 0x00120ACC-0x00120B35 as its 72-byte response root, two 12-byte state entries, and a ten-byte 0x87 transition prefix into the existing Type-0x64 stream |
| `20260829-type7c-type7d-level-event-movement-static-v1` | recorded-static-decompilation | Promoted the contiguous Type-0x7C/Type-0x7D level-event MovementVM body at 0x00121180-0x0012120D as a ten-byte wide-random-offset prelude plus a 132-byte shared owner, with the Type-0x7D pointer at 0x001211C4 recorded as an alias entry |
| `20260829-menu-presentation-child-movement-static-v1` | recorded-static-decompilation | Promoted the exact menu-presentation child MovementVM entry prefixes at 0x00121684-0x001216A9 and 0x001216AA-0x001216C5 from their direct F5 template pointers, leaving the shared Type-0x07 continuation under its existing owner |
| `20260829-type84-menu-presentation-child-a-animation-static-v1` | recorded-static-decompilation | Range-bounded the direct menu-selected Type-0x84 child-A AnimationVM stream at 0x00125F5A-0x0012602F, including all twelve nested F5 records and the exact boundary before the separately owned child-B stream |
| `20260829-type64-interaction-animation-static-v1` | recorded-static-decompilation | Promoted the compact four-byte Type-0x64 interaction AnimationVM entry at 0x00124CD8 from ACTOR_TEMPLATE_TYPE_64, preserving the boundary before the Type-0x62 response entries |
| `20260829-type4d-type7b-response-child-movement-static-v1` | recorded-static-decompilation | Promoted the exact Type-0x4D and Type-0x7B response-child MovementVM owners at 0x00121710-0x0012171B and 0x0012171C-0x001217A1 from their direct template pointers and decoder loop boundaries |
| `20260829-menu-transition-scene-table-movement-static-v1` | recorded-static-decompilation | Promoted the exact 42-byte Type-0x84 scene-table transition MovementVM stream at 0x001209C6-0x001209EF from its direct template pointer and internal random alternatives |
| `20260829-actor-runtime-type6e-73-movement-static-v1` | recorded-static-decompilation | Promoted six directly installed runtime-type 0x6E-0x73 MovementVM roots and their shared 118-byte response continuation, correcting the actor +0x0A movement-field classification and preserving the Type-0x7F boundary |
| `20260829-actor-runtime-type6e-73-secondary-movement-static-v1` | recorded-static-decompilation | Promoted six exact packed secondary MovementVM entries between the runtime-type 0x6E-0x73 roots, removing the false UNKNOWN gaps while preserving conservative per-type naming |
| `20260830-font-glyph-bank-static-v1` | recorded-static-decompilation | Promoted the 58-record fixed-width startup font bitmap bank at 0x000004D8-0x000006A7 and its preceding alignment padding, eliminating false pointer-table candidates before reset bootstrap |
| `20260830-type62-63-player-collision-movement-static-v1` | recorded-static-decompilation | Promoted the directly installed 64-byte Type-0x62/0x63 player-collision MovementVM root at 0x00121598, with its exact boundary before the unresolved packed entry at 0x001215D8 |
| `20260830-startup-region-warning-static-v1` | recorded-static-disassembly | Promoted the VDP/font and regional compatibility-warning path at 0x00000344-0x0000044D plus its selector, variant-table, and text-data partitions through 0x000004CF |
| `20260830-type29-player-collision-response-static-v1` | recorded-static-disassembly | Promoted the Type-0x29 player-collision handler at 0x001AF400 and its paired exact animation/movement roots at 0x00121C30 and 0x001215E0 |
| `20260830-startup-region-warning-helper-symbols-v1` | recorded-static-disassembly | Named the 54-byte VDP warning-text helper at 0x00000418 and the reset thunk at 0x000006A8, and aligned the main warning renderer boundary with Ghidra's split |
| `20260830-runtime-initializer-symbol-v1` | recorded-static-decompilation | Named the 460-byte runtime/scene initialization entry at 0x001A8A4A that performs the first service pass before entering Game_FrameUpdateLoop |
| `20260830-type3e-3f-player-collision-response-static-v1` | recorded-static-disassembly | Promoted the separate Type-0x3E and Type-0x3F player-collision producers at 0x001AF2B0/0x001AF2FA, their shared exact 108-byte MovementVM root at 0x00121618, and the directly referenced Type-0x84 child template at 0x001B7B5C |
| `20260830-interaction-anchor-callback-static-v1` | recorded-static-disassembly | Range-bounded and named the contiguous actor interaction-anchor callback family at 0x001B57C4-0x001B58D7, including the two anchored movement-step callbacks and the VM-pushed anchor publication helper |
| `20260830-interaction-spawn-runtime22-static-v1` | recorded-static-decompilation | Named the selector-0x19 interaction handler at 0x001B6EEE-0x001B6F0A, which uses ACTOR_TEMPLATE_TYPE_1F, clears movement state, installs animation 0x001238B2, and publishes runtime type 0x22 |
| `20260830-level-camera-scroll-callback-static-v1` | recorded-static-decompilation | Closed the nine unique level camera-scroll callbacks selected by LEVEL_TABLE offset 0x34 across the formerly unknown 0x001AAA88-0x001AB34D region, including shared implementations for levels 0-2, 5-6, and 11-12 |
| `20260830-menu-scene-input-data-static-v1` | recorded-static-decompilation | Promoted the six-record menu control-layout table at 0x00004012, the startup scene script at 0x00004082, the Level-08 14-byte VDP stream at 0x000040B8, and the primary/alternate input-pattern tables at 0x00004128/0x0000413A |
| `20260830-level-event-stream-extents-static-v1` | recorded-static-decompilation | Promoted the Level-02 and Level-06 timed event streams at 0x00002128 and 0x000024FC using the six-byte cursor advance, named exit-routine setters, zero-duration terminators, and the following Level-08 animation boundary |
| `20260830-actor-surface-flags-table-static-v1` | recorded-static-decompilation | Promoted the complete 256-byte terrain-behavior surface-flags lookup at 0x0000683E-0x0000693D, indexed by Actor_TerrainCollisionLoop before publishing the actor orientation marker |
| `20260830-system-exception-vector-table-static-v1` | recorded-static-disassembly | Promoted the complete 64-entry 68000 exception/interrupt vector table at the ROM origin, including the reset bootstrap and VBlank/interrupt vectors through 0x000000FF |
| `20260830-scene-resource-object-animation-table-static-v1` | recorded-static-decompilation | Promoted the exact sixteen-entry scene-resource object-animation pointer table at 0x00004A18, indexed by object commands 0x20-0x2F in SceneResource_InstantiateActors and ending before the interaction-counter table |
| `20260830-level08-rotating-vdp-record-table-static-v1` | recorded-static-decompilation | Promoted the exact sixteen-entry Level-08 rotating VDP record table at 0x000029E0-0x00002A3F, consumed in six-byte steps by VDP_WriteRotatingCommandRecord and wrapped by Level08_EnterRoutine at offset 0x60 |
| `20260830-scene-vdp-fixed-data-static-v1` | recorded-static-decompilation | Promoted the adjacent fixed scene-header VDP words at 0x00002A40-0x00002A47 and the ten-byte indirect zero-fill stream at 0x00002A48-0x00002A51, closing the former camera-adjacent layout gap before the damping table |
| `20260830-scene-vdp-transition-plane-offset-table-static-v1` | recorded-static-decompilation | Promoted the exact 32-word transition-plane offset table at 0x00002080-0x000020BF, indexed by even FRAME_PHASE_COUNTER offsets in Scene_VDP_ClearTransitionPlane before the level-event dispatch table |
| `20260830-actor-frame-phase-child-animation-table-static-v1` | recorded-static-decompilation | Promoted the exact four-entry phase-child runtime-type/animation table at 0x000049A9-0x000049C0, selected in six-byte steps by Actor_SpawnFramePhaseChild while preserving the repeated tail and command-table boundary |
| `20260830-rom-padding-002022-static-v1` | recorded-static-decompilation | Promoted the exact 0xFF-filled ROM padding range at 0x00002022-0x0000207F immediately before the decoded scene transition-plane offset table, while leaving the preceding mixed-content bytes unresolved |
| `20260830-player-collision-handler-family-extended-static-v1` | recorded-static-decompilation | Promoted the remaining pointer-table-backed player-collision handler bodies for types 0x01, 0x02, 0x33, 0x37-0x3D, 0x41-0x42, 0x55-0x61, and 0x7E, plus the missing actor Type-0x0D facing-toggle entry, with exact shared-body and fall-through boundaries |
| `20260830-actor-collision-handler-family-extended-static-v1` | recorded-static-decompilation | Promoted the remaining pointer-table-backed actor-collision handler bodies for types 0x01-0x05, 0x08-0x09, 0x18-0x1C, 0x1F, and 0x22-0x28, preserving shared bodies and the Type-0x23 fall-through into its response allocator |
| `20260830-final-mechanical-function-closure-static-v1` | recorded-static-decompilation | Named the final three anonymous Ghidra functions: the player-collision suppression setter at 0x001AE6B4, the shared type-cleanup helper at 0x001AF56C, and the ASCII interaction-counter decrement at 0x001B03BE; split the prior broad type-0x5D range accordingly |
| `20260830-type04-collision-animation-entry-static-v1` | recorded-static-decompilation | Promoted the full 34-byte Type-0x04 collision AnimationVM entry at 0x00124C18 and retained the nested F5 producer at 0x00124C1A as a non-owning alias |
| `20260830-actor-animation-callback-family-static-v1` | recorded-static-decompilation | Promoted the eight indirect AnimationVM callback bodies at 0x001ACC18-0x001ACDA1, including actor-flag toggles, random audio/event dispatch, and randomized actor-offset helpers |
| `20260830-actor-animation-callback-family-extended-static-v1` | recorded-static-decompilation | Promoted five exact indirect AnimationVM callbacks at 0x001ACB5A-0x001ACBF1 for actor flag updates and linked-actor coordinate copying |
| `20260830-interaction-resource-progress-reset-static-v1` | recorded-static-decompilation | Promoted the exact 22-byte interaction/resource progress reset service at 0x001B0024, which writes `000\0` to INTERACTION_RESOURCE_PROGRESS_COUNTER before the existing getter |
| `20260830-actor-animation-response-helpers-static-v1` | recorded-static-decompilation | Promoted the final two exact actor AnimationVM response helpers at 0x001ACB18-0x001ACBD7 for interaction-response reinitialization and collision-response child spawning |
| `20260830-actor-resource-clear-register-variants-static-v1` | recorded-static-decompilation | Promoted the missing A0-register actor resource-clear variant at 0x001AE3A0, pairing it with the existing A2-register helper through the complete 0x001AE3A0-0x001AE3FB block |
| `20260830-actor-slot-allocator-family-static-v1` | recorded-static-decompilation | Promoted the extended forward actor-slot allocator at 0x001AE2C2, which scans 31 gameplay records for a free slot and is used by the interaction-anchor path |
| `20260830-actor-interaction-value-publisher-register-variants-static-v1` | recorded-static-decompilation | Promoted the missing A5-register interaction-value publisher at 0x001AE700, completing the contiguous A2/A1/A5 helper family |
| `20260830-interaction-resource-delay-counter-static-v1` | recorded-static-decompilation | Promoted the exact +1/+7 interaction-resource delay helpers at 0x001B019C-0x001B01AB and named their frame-gated counter at 0x00FFF159 |
| `20260830-interaction-target-dispatch-static-v1` | recorded-static-decompilation | Promoted the computed interaction target-state dispatcher at 0x001B0316-0x001B0333, including its selector index and terminal-transition side effect |
| `20260830-interaction-response-target-static-v1` | recorded-static-decompilation | Promoted the exact response-target convergence helper at 0x001B0434-0x001B044D and named the current/pending response bytes at 0x00FFEFFA/0x00FFEFFB |
| `20260830-vdp-interaction-digit-writers-static-v1` | recorded-static-decompilation | Promoted the self-contained ASCII and three-digit VDP output helpers at 0x001B044E-0x001B048F, retaining the non-overlapping wrapper/helper boundary |
| `20260830-scene-resource-rebuild-wait-static-v1` | recorded-static-decompilation | Promoted the fixed 31-VBlank scene-resource rebuild wait loop at 0x001B1AA0-0x001B1AB5, including its FRAME_PHASE_COUNTER progression and fall-through continuation |
| `20260830-scene-resource-vdp-accumulator-static-v1` | recorded-static-decompilation | Promoted the bounded 224-word accumulated VDP stream writer at 0x001B1DDA-0x001B1E09 while leaving its higher-level asset ownership unresolved |
| `20260830-player-facing-launch-motion-static-v1` | recorded-static-decompilation | Promoted the isolated player-facing launch-motion initializer at 0x001AE61A-0x001AE64B, including its terrain-bounce animation-state reset and facing-controlled +/-0x0400 horizontal velocity |
| `20260830-random-parity-callback-static-v1` | recorded-static-decompilation | Promoted the AnimationVM random-parity callback at 0x001B52FA-0x001B5317, which advances the shared PRNG until its low bit changes and publishes that bit to 0x00FFF111 |
| `20260830-scene-resource-wrapper-gap-closures-static-v1` | recorded-static-decompilation | Promoted the missing scene-resource loader at 0x001B47BE, blank-frame wrappers at 0x001B4CE6/0x001B4E44, and their directly selected command streams at 0x001277C5/0x00127B60 |
| `20260830-camera-scroll-cursor-callback-static-v1` | recorded-static-decompilation | Promoted the three AnimationVM camera-scroll cursor setters at 0x001B52D6/0x001B52E2/0x001B52EE and the 16-word signed delta table at 0x0000693E-0x0000695D |
| `20260830-interaction-anchor-forward-spawn-static-v1` | recorded-static-disassembly | Promoted the interaction-anchor forward spawner at 0x001B5786-0x001B57C3, including its extended actor-slot allocation, type-0x84 response template, and independent random placement offsets |
| `20260830-low-confidence-scene-terrain-services-static-v1` | recorded-static-decompilation | Closed the remaining low-confidence function queue: trace-validated SceneScript_AdvanceState, decompiled Scene_EnterTransitionMode and SceneScript_CompleteToState1, and the exact Terrain_ContourLookupHelper name/body |
| `20260830-player-action-airborne-continuation-static-v1` | recorded-static-decompilation | Promoted the direct 64-byte F2 continuation at 0x00122672 for the player airborne action stream, including its exact F5 child spawn, FB callback, F11F publication, and EA return |
| `20260830-static-rom-data-family-v1` | recorded-static-disassembly | Named the bounded Sega header fields at 0x00000100, shared Type-0x40/0x3A level-event movement prelude at 0x00121034, transition/result/menu text corpora, the BONUS LEVEL and PRESENTS labels, and the SceneResource_Dispatch four-band palette source at 0x00129AD2 |
| `20260830-interaction-spawn-type5e84-e1e2-movement-v1` | recorded-static-decompilation | Range-bounded the directly installed Type-0x5E E1/E2 MovementVM stream at 0x0011F8A4-0x0011FAA7, including its terminal clear-state step and shared continuation boundary |
| `20260830-unindexed-graphics-gap-static-v1` | recorded-static-decompilation | Promoted the exact 117-tile unindexed graphics band at 0x0011E160-0x0011EFFF, zero padding at 0x001A830A-0x001A831F, and the uncompressed graphics band at 0x001A8320-0x001A8A49 before the known startup initializer |
| `20260830-header-tail-and-scene-padding-static-v1` | recorded-static-decompilation | Completed the standard Genesis header through 0x000001FF and promoted the zero alignment byte at 0x001270A7 between adjacent scene-resource streams |
| `20260830-render-data-static-v1` | recorded-static-decompilation | Promoted the player-sprite VDP control word at 0x00001CB2 and the terminated HUD interaction frame sequence at 0x000029A6-0x000029DF from their direct render-service consumers |
| `20260830-unindexed-movement-stream-bands-static-v1` | recorded-static-disassembly | Promoted six exact decoder-bounded MovementVM ownership bands, including the decompiled Type-0x5E/0x84 pair bank at 0x0011FAA8-0x0011FD17 and decompiled paired-spawn AnchorResponse handler body at 0x001B69A6-0x001B69FF; decoded callback/step evidence now identifies the 0x0011FAA8 anchor-response grid, the 0x001210FE Flag-20 terrain response, the 0x001212C0 interaction-anchor bank, the 0x001213E2 Type-0x8D wall-response child prefix, and the 0x001215D8 unit vertical step loop while preserving provisional reachability for the two unresolved template bands |
| `20260830-type1e-proximity-movement-handoff-static-v1` | recorded-static-decompilation | Promoted the exact 50-byte Type-0x1E proximity-response movement handoff at 0x001235E2-0x00123613, including its direct FD branch from the known root, ED publication of movement continuation 0x001204DA, and runtime-observed internal loop entry at 0x001235EC |
| `20260830-type7a-terminal-jumps-static-v1` | recorded-static-decompilation | Extended the three Type-0x7A interaction roots at 0x00125A68, 0x00125A88, and 0x00125AA8 to own their exact post-FC terminal EA jumps, removing 18 false UNKNOWN bytes before the shared response tail |
