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

For direct harness runs, use `tools/openaladdin/mame/run.sh`; it records the
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

### Scene and exit work

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
`0x001B5B66`, but the extended current-frontier run stays in scene state
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
`0x001B5B66 -> FFF0E9=FF`, but no writer reaches `SCENE_STATE`,
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
`re/mame/findings/20260826-level01-actor-type2a-decomp-v1.json`. Its player
collision-table entry at `0x001D66` points to `0x001AEBFE`, which is an empty
return. The vertically moving Type-0x2A object observed in the upper-route
inventory is consequently non-collidable scenery/effect data, not the missing
traversal connector.

The integrated upper-frontier boundary replay is recorded in
`re/mame/findings/20260826-level01-integrated-natural-exit-v1.json` and
`re/mame/campaigns/20260826-level01-integrated-natural-exit-v1.json`. With
`right*160,up*120,none*40,right*40,none*600` and no memory pokes, the player
reaches world `(4746,628)`: the X half of the Level 01 boundary predicate is
satisfied, but the required `Y<470` half is not. The player falls to
`(4750,874)` and `SCENE_STATE` remains `0x01`. This corrects the earlier
interpretation of the pair-Y fixture, whose `(4688,466)` position depended on
an explicit `PLAYER_Y/PLAYER_VY` poke; it is controlled evidence only.

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

When a campaign is superseded, leave it in this table. A negative result is
valuable because it prevents repeating the same input family.
