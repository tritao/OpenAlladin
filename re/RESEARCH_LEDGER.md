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
`SceneTable_SelectNextState`, reached from the pending-script terminator in
`SceneScript_CompleteToState1`. The level-01 boundary instead arms the
countdown and takes the script writer to state `0x03` with table index `0`.
The state-`0x08` table entry is the second selector-cycle result at index `1`,
so it remains a separate scene-table/resource proof rather than the direct
natural level-01 exit target.

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

The static result is recorded in
`re/mame/findings/20260828-actor-collision-terminal-response-v1.json`.

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

## Campaign index

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
| `20260828-scene-resource-vdp-service-v1` | recorded-static-decompilation | VBlank wait/Z80 service, VRAM word transfer, scene-resource command interpretation, and actor instantiation |

When a campaign is superseded, leave it in this table. A negative result is
valuable because it prevents repeating the same input family.
