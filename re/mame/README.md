# MAME integration

MAME-facing symbol files are generated from `re/symbols/*.yml` by the ROM
import pipeline. Generated files live in `build/re/` and are not canonical
knowledge; edit the YAML sources instead.

## First trace

With the MAME submodule built and the local ROM at the repository root:

```sh
./tools/mame-trace.sh
```

The command writes generated output under `build/re/traces/`:

- `trace_boot.jsonl` contains reset-vector, register, input, and per-frame RAM
  metadata, plus the current VDP address/code/register state and checksums.
- `ram_frames.bin` contains one raw 64 KiB `0xff0000`-`0xffffff` Genesis RAM
  image for each recorded frame, starting with frame zero.
- `vdp_vram_frames.bin`, `vdp_cram_frames.bin`, and `vdp_vsram_frames.bin`
  contain the complete MAME VDP memories for each frame, encoded as
  big-endian Genesis words.
- `vdp_regs_frames.bin` contains the 32 saved VDP register words per frame.
- `vdp_writes.jsonl` records every 68000 write to the VDP ports with frame and
  program-counter context.  The stream is enough to reconstruct VDP command
  pairs and DMA requests.

The default is 120 frames.  Override it with `OPENALADDIN_TRACE_FRAMES`.
Input can be supplied as comma-separated frame tokens, for example:

```sh
OPENALADDIN_TRACE_FRAMES=90 OPENALADDIN_INPUT='none,right*60,none' \
  ./tools/mame-trace.sh
```

For a repeatable gameplay checkpoint, schedule a state and screenshot after
the scripted input has had time to enter the game:

```sh
OPENALADDIN_TRACE_FRAMES=360 \
OPENALADDIN_INPUT='none*30,start*90,none*60,right*120,none*60' \
OPENALADDIN_SAVE_FRAME=300 OPENALADDIN_SNAPSHOT_FRAME=300 \
  ./tools/mame-trace.sh
```

State files and PNG snapshots are written below `build/re/traces/states/` and
`build/re/traces/snapshots/`.

An existing state can be loaded by its MAME state name:

```sh
OPENALADDIN_LOAD_STATE=gameplay MAME_XVFB=1 \
  OPENALADDIN_TRACE_FRAMES=180 ./tools/mame-trace.sh
```

The normal trace mode is fully headless (`-video none`).  If SDL needs a real
display, run through Xvfb instead:

```sh
MAME_XVFB=1 OPENALADDIN_TRACE_FRAMES=120 ./tools/mame-trace.sh
```

This uses a virtual 1024×768 X11 display and MAME's software renderer.  Set
`OPENALADDIN_MAME_VIDEO` to choose another MAME video backend.

This harness deliberately has no game-specific addresses yet.  Its first job
is to establish repeatable emulator observations before we identify player and
actor RAM symbols.

To rank changing 16-bit words from a controlled interval:

```sh
python3 tools/analyze-mame-trace.py build/re/traces --input right
```

The output is only a list of candidates.  Confirmed addresses will be added to
`re/symbols/ram.yml` only after a second experiment and a CPU write watchpoint.

To inventory the common actor table, enable tracing for all 32 records.  The
trace adds the table layout to its header; the analyzer reads the captured RAM
snapshots and reports type intervals, cursor positions, and ROM-decoder probes:

```sh
OPENALADDIN_TRACE_ACTORS=1 OPENALADDIN_TRACE_FRAMES=1550 \
  OPENALADDIN_TRACE_DIR=build/re/actor-gameplay \
  ./tools/mame-trace.sh
python3 tools/analyze-actor-animation-trace.py
```

The generated `build/re/actor_animation_inventory.json` is intentionally
ignored.  Actor `animation_pc` is a moving ROM cursor, so a cursor observed in
RAM is evidence of stream membership, not automatically a stream entry point.

For a deterministic probe of a statically identified actor template, clone the
0x42-byte type-0x7D template at `0x001B81D8` into an unused slot and override its
animation cursor:

```sh
OPENALADDIN_CAPTURE_VDP=0 OPENALADDIN_TRACE_ACTORS=1 \
OPENALADDIN_TRACE_FRAMES=240 OPENALADDIN_INJECT_ACTOR_FRAME=2 \
OPENALADDIN_INJECT_ACTOR_SLOT=31 OPENALADDIN_INJECT_ACTOR_TYPE=125 \
OPENALADDIN_TRACE_DIR=build/re/actor-injection-template \
  ./tools/mame-trace.sh
python3 tools/analyze-actor-animation-trace.py \
  --trace-dir build/re/actor-injection-template \
  --output build/re/actor-injection-template/actor_animation_inventory.json
```

The captured record reaches the injected animation cursor with type `0x7D`; the
common helper then follows its short branch path and retires the synthetic
record.  This is a state-stream probe, not a replacement for a naturally
spawned gameplay actor; the injector overrides the cursor after copying the
record image.

To inventory naturally created actors, enable the initializer breakpoint.  MAME
writes records to `debug.log`; the return address is the caller of the common
initializer at `0x001AE30A`:

```sh
OPENALADDIN_CAPTURE_VDP=0 OPENALADDIN_TRACE_ACTOR_INIT=1 \
OPENALADDIN_TRACE_FRAMES=2880 \
OPENALADDIN_INPUT='none*320,start*5,none*200,start*5,none*170,start*5,none*200,start*5,none*150,start*5,none*180,right*1400,none*255' \
OPENALADDIN_TRACE_DIR=build/re/actor-init-gameplay \
  ./tools/mame-trace.sh
python3 tools/analyze-actor-initializers.py \
  --log debug.log \
  --output build/re/actor-init-gameplay/actor_initializers.json
```

The analyzer resolves each compact source template against the ROM and reports
its type byte, movement cursor copied from source `+0x06` to actor `+0x0A`,
animation cursor copied from source `+0x0C` to actor `+0x20`, destination slots,
and initializer callers.  A recent gameplay run captured 119 initializer calls
spanning 26 distinct templates; these are observations to classify before
assigning semantic actor names.

The same RAM trace exposes movement cursors alongside animation cursors.  In a
natural run, type `0x2D` appeared in slot 3 with movement cursor
`0x0011F6D4` and animation cursor `0x00123EE8`; the movement cursor advanced to
`0x0011F6DE` and `0x0011F6E6` as the actor state stream was consumed.

The initializer trace also captures `A2`.  When the return address is
`0x001AD0AC`, `A2` points at the signed spawn-offset payload consumed by the
animation VM's `F5` spawn/copy handler.  In a targeted 1800-frame run, type
`0x2D` was spawned by the type `0x0A` guard stream at `0x0012542A`; its child
animation uses sprite frames `1338-1345`, identifying it as the guard's sword
attack child.  At frame 1639, the child record had `x=0x0512`, `y=0x0333`,
and frame pointer `0x001FDDB4` (Chopper frame 1338).

The same scene provides a complete guard damage/death trace.  With the player
approaching on `right` and timed `a` presses, slot 5 (`0x00FF7F8A`) remains a
type `0x0A` guard from frames 1596-1667 at fixed position
`x=0x0530,y=0x0340`; its movement cursor is zero while its animation cursor
walks through `0x0012542A`-`0x00125486`.  At frame 1668, the sword hit replaces
that record in place with type `0x84`, selects animation `0x00122FA2`, and
advances through 43 terminal frames before the type byte clears at frame 1711.
The type `0x84` sequence is shared/unnamed beyond its confirmed terminal role;
the guard-specific evidence is the in-place transition and the matching
`0x001B7940` template selected by the 68K path around `0x001AC4B0`.

Reproduce the probe with:

```sh
OPENALADDIN_CAPTURE_VDP=0 OPENALADDIN_TRACE_ACTORS=1 \
OPENALADDIN_TRACE_FRAMES=1690 \
OPENALADDIN_INPUT='none*320,start*5,none*200,start*5,none*170,start*5,none*200,start*5,none*150,start*5,none*180,right*390,a*2,none*18,a*2,none*18,a*2,none*18,a*2,none*18,a*2,none*100' \
OPENALADDIN_TRACE_DIR=build/re/guard-death \
  ./tools/mame-trace.sh
```

The actor initializer at `0x001AE30A` also establishes the confirmed runtime
fields used by this slice: position at `+0x02/+0x04`, movement cursor at
`+0x0A`, velocity accumulators at `+0x18/+0x1A`, animation cursor at `+0x20`,
resource count at `+0x29`, and behavior flags at `+0x3C`.

## Targeted combat/state decompilation

The focused Ghidra request is tracked at
`re/ghidra/actor-combat-targets.json`.  Re-run it from the repository root
with the existing read-only Ghidra project:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/actor-combat-targets.json \
  build/re/actor-combat-targeted-decompile.json
```

The JSON report is generated output under `build/re/` and is intentionally
ignored.  The current target pass establishes these boundaries:

- `0x001A9502` is the player action-animation selector.  It resets the player
  animation timer and assigns `0x0012271A` for the sword branch.
- `0x001AC0D2` is a callable type-`0x84` transition block: it clears the
  current actor type, releases owned resources, runs common cleanup, and
  installs animation `0x0012319C`.
- `0x001AC3F2` toggles the actor horizontal-facing byte `+0x09`, guarded by
  actor flag bit 6 at `+0x06`.
- `0x001AC4B0` is the guard-hit terminal replacement block.  It clears the
  existing record, cleans the A2-owned resources, and initializes the same
  record from template `0x001B7940`; the initializer writes type `0x84` at
  `0x001AE30E`.
- `0x001ACBF2` selects animation stream `0x00125966` from the actor position
  and global direction-state checks.
- `0x001AE0B0` is the actor cull/remove helper.  It clears the actor, releases
  its resource list, publishes interaction state when required, and repeats
  the operation for a linked actor at `+0x3E`.
- `0x001AE6BC` and `0x001AE6DE` publish actor byte `+0x34` into the global
  interaction table at `0x00FFAE87`, indexed by actor word `+0x32`.

The exact guard-hit write was also confirmed with the MAME debugger watchpoint
on `0x00FF7F8A`: the existing type is cleared in the `0x001AC4B0` path and the
subsequent `Actor_InitializeFromTemplate` write installs `0x84`.  These
addresses are now recorded in `re/symbols/functions.yml`; mid-dispatch
addresses such as `0x001AC4B0` remain documented as blocks rather than being
invented as standalone functions.

The next collision-focused request is tracked at
`re/ghidra/collision-damage-targets.json` and can be regenerated with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/collision-damage-targets.json \
  build/re/collision-damage-targeted-decompile.json
```

That pass recovers the dispatch chain.  The main frame loop calls
`0x001ABB40` before the actor animation VM; this routine scans the player
collision rectangle against the 24 records at `0x00FF7E82` and dispatches by
actor type through the ROM table at `0x001CBE`.  The type-`0x0A` guard entry at
`0x001CE6` points to `0x001AE9C6`, which calls the shared response block at
`0x001AEC00` and then `0x001AE4F8` for player interaction-state handling.

`0x001ABD7E` is a separate seven-record actor-to-actor pass using the handler
table at `0x001EBA`.  The two routines at `0x001AE3FC` and `0x001AE47E` consume
non-zero values from `0x00FFAE87` and dispatch them through the ROM handler
table at `0x004154`.  The guard trace's collision dispatch writes the shared
collision flag at `0x001ABC8A`; the subsequent observed guard transition is
the already-confirmed clear at `0x001AC4B2` followed by the type-`0x84`
initializer write at `0x001AE30E`.

For a live watchpoint run, reuse the guard input schedule above and add:

```sh
OPENALADDIN_DEBUG_WATCH=1 \
OPENALADDIN_WATCH_ADDRESSES=0xFF7F8A,0xFF7FBC,0xFF7FBE,0xFFE1C2,0xFFF0D8,0xFFF0F4,0xFF7E60 \
OPENALADDIN_TRACE_FRAMES=1690 \
OPENALADDIN_TRACE_DIR=build/re/guard-collision-watch \
./tools/mame-trace.sh
```

Write taps can record the 68000 PC responsible for a candidate address:

```sh
OPENALADDIN_WATCH_ADDRESSES=0xFF7E28 \
  OPENALADDIN_TRACE_FRAMES=20 ./tools/mame-trace.sh
```

Write events appear as `{"type":"write", ...}` records in
`trace_boot.jsonl`.

## Actor movement VM and stream decoder

The actor movement interpreter is `MovementVM_TickActors` at `0x001ADE36`.
It walks 32 records at `0x00FF7E40` with stride `0x42`, consumes the movement
cursor at record offset `+0x0A`, and integrates velocity accumulators at
`+0x18/+0x1A`.

Movement streams use the following format:

```text
signed delta_x
signed delta_y
zero or more commands 0x80..0x94
```

The command bytes reuse the animation handlers through the mapping
`0x80 -> 0xEA` through `0x94 -> 0xFE` in the shared table at `0x004954`.
The decoder preserves each step, raw command bytes, shared opcode, operands,
and statically visible branch targets:

```sh
python3 tools/decode-movement-streams.py \
  Disneys_Aladdin_U_p1.bin \
  --output build/re/movement_streams.json
```

The currently confirmed movement roots include `0x0011F6D4` for the natural
type-`0x2D` guard child and `0x0011F6FE` for the looping type-`0x84` stream.
The existing actor trace inventory confirms that the decoded step addresses
are the same cursors written back to actor field `+0x0A`; conditional command
paths remain state-dependent and are listed as branch targets rather than
being guessed statically.

## Player movement and terrain collision

The player movement/terrain request is tracked at
`re/ghidra/player-movement-collision-targets.json` and can be regenerated with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/player-movement-collision-targets.json \
  build/re/player-movement-collision-targeted-decompile.json
```

The verified per-frame order is:

```text
MovementVM / actor updates
  -> player-vs-actor collision pass at 0x001ABB40
  -> terrain query flags 0x00FFF07C-0x00FFF07F
  -> terrain resolver at 0x001B1E38
  -> player terrain response at 0x001A9D98
  -> jump/vertical-state handler at 0x001A9716
  -> animation and remaining actor passes
```

`0x001B1E38` computes a row from `WORLD_CAMERA_Y + PLAYER_Y`, selects a
terrain word through `0x00FF9884`, maps that word through `0x00FFAE86`, and
dispatches the resulting behavior byte through the ROM handler table at
`0x004554`. The callback pointers observed in the gameplay trace are
`0x001B3244`, `0x001B323A`, and `0x001B324E`; the four query helpers test bits
of `0x00FFF156` and convert them into the four terrain flags before the
resolver runs.

The motion routine at `0x001A9B90` is explicitly 8.8 fixed-point: horizontal
velocity consumes its signed high byte and accelerates by `0x28`, while
vertical velocity consumes its signed high byte and accelerates by `0x3C`.
`0x001A9D18` derives `PLAYER_WORLD_X/Y` from the local player coordinates and
the camera origin.

A controlled C-button trace confirms the jump path: frame 1326 writes
`PLAYER_VY=0xFE00`, then the vertical integrator advances it by `0x3C` per
frame while `PLAYER_Y` follows the ascent/descent arc; landing returns the
velocity to zero and arms the grounded response state. Reproduce that trace
with:

```sh
OPENALADDIN_CAPTURE_VDP=0 \
OPENALADDIN_TRACE_FRAMES=1700 \
OPENALADDIN_TRACE_DIR=build/re/player-jump-c-watch \
OPENALADDIN_INPUT='none*320,start*5,none*200,start*5,none*170,start*5,none*200,start*5,none*150,start*5,none*180,right*80,c*2,none*55,right*80,c*2,none*55,right*80,c*2,none*180' \
  ./tools/mame-trace.sh
```

## Level-transition state tracing

The level-transition request is tracked at
`re/ghidra/level-transition-targets.json` and includes both code targets and
references to the RAM state/transition flags. Regenerate its focused report
with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/level-transition-targets.json \
  build/re/level-transition-targeted-decompile.json
```

The recovered state path is:

```text
scene script terminator
  -> SceneScript_AdvanceState (0x001A8E3E)
  -> SCENE_STATE (0x00FF7E26)
  -> SceneResource_Dispatch (0x001B0F66)
  -> VDP/resource rebuild
```

`SceneTable_SelectNextState` at `0x001B3B96` selects a state and script pointer
from the ROM transition table using `SCENE_TABLE_INDEX` at `0x00FFF57A`.
`SceneScript_CompleteToState1` at `0x001B315C` returns the active scene to
state `0x01` after a script terminates. State `0x08` is special: the player
movement routine switches to a camera/transition branch instead of normal
player integration.

To capture the actual state setter PC during a gameplay experiment:

```sh
OPENALADDIN_CAPTURE_VDP=0 \
OPENALADDIN_TRACE_SCENE_STATES=1 \
OPENALADDIN_DEBUG_WATCH=1 \
OPENALADDIN_WATCH_ADDRESSES=0xFF7E26,0xFF7E22,0xFFF57C,0xFFF57E,0xFFF0D0,0xFFF0DA,0xFFF0DC,0xFFF0E6 \
OPENALADDIN_TRACE_FRAMES=5000 \
OPENALADDIN_INPUT='none*320,start*5,none*200,start*5,none*170,start*5,none*200,start*5,none*150,start*5,none*180,right+a*3755' \
OPENALADDIN_TRACE_DIR=build/re/level-transition-watch \
  ./tools/mame-trace.sh Disneys_Aladdin_U_p1.bin
python3 tools/analyze-transition-watch.py \
  --log debug.log \
  --output build/re/level-transition-watch/transition_watch.json
```

The completed title-to-first-level trace covered 1,300 frames and wrote
`SCENE_STATE` only during startup (`0x00` to `0x01`); it did not reach the
level-exit script. The longer 5,000-frame command above is retained as a
repeatable traversal experiment, but the next useful runtime experiment should
target a level-exit trigger rather than simply extend right-input playback.

The machine-readable result is recorded in
`re/mame/level-transition-findings.json`. It deliberately keeps the dynamic
claim narrow: state `0x08` is statically recovered, while the captured
title-to-level run verifies only the reset-to-state-1 path.

The initial level-01 runtime tables are matched byte-for-byte to the extracted
assets. `level01/raw/map.bin` is loaded at `0x00FF0000` and has 27,000 bytes;
the active level end marker is `0x00FF725C`, or `0x6978` bytes from the RAM
base. Its row pointers at `0x00FF9884` select 16-pixel Y bands, and the loaded
rows advance by `0x258` bytes (300 big-endian terrain words) per band.

`level01/raw/floor.bin` is initially loaded byte-for-byte at `0x00FFAE84`,
including the two-byte offset before `TERRAIN_BEHAVIOR_INDEX_TABLE` at
`0x00FFAE86`. The resolver's `terrain_word >> 1` lookup therefore indexes the
extracted floor data directly. Later gameplay can mutate three entries in
that region through the shared interaction workspace; the terrain entries
used by this trace remain unchanged.
This gives a reproducible static map of non-flat behavior cells, including
behavior `0x2B` at row 30/column 182 (`0x6008`) and behavior `0x47` at
row 31/column 142 (`0x60C0`).

The focused trace `build/re/player-terrain-rightjump` reached a live non-flat
cell at frame 1159: the player probe was at world `(481, 880)`, the resolver
read row 40/column 31 (`0x6C58`), produced behavior `0x0A`, and selected the
handler-table entry `0x001B5320`. Behavior `0x0A` remained active while the
player crossed the adjacent surface cells. The complete machine-readable
finding is recorded in `re/mame/player-terrain-findings.json`.

To compare the captured VDP memories and DMA stream with the native assets
already extracted from the ROM:

```sh
python3 tools/compare-runtime-assets.py
```

The report is written to `build/re/vdp_asset_comparison.json`.  Exact matches
mean a native binary is present contiguously in captured VDP memory; sample
matches locate a 64-byte portion of larger or partially loaded assets.  This
is an observation report, not a replacement for the tracked asset parsers.

To log every execution of the RNC-to-VDP helper at `0x001B3416`, including the
source ROM address, VRAM destination, caller return address, and MAME frame:

```sh
OPENALADDIN_TRACE_RNC_LOADS=1 OPENALADDIN_TRACE_FRAMES=1550 \
  OPENALADDIN_TRACE_DIR=build/re/rnc-loader-gameplay \
  ./tools/mame-trace.sh
python3 tools/analyze-rnc-load-trace.py \
  --log debug.log \
  --output build/re/rnc-loader-gameplay/rnc_loads.json
```

Merge the parsed execution evidence into the runtime asset report:

```sh
python3 tools/analyze-rnc-runtime.py \
  --trace build/re/rnc-loader-gameplay \
  --load-trace build/re/rnc-loader-gameplay/rnc_loads.json
```

The debugger breakpoint is optional and is enabled only by
`OPENALADDIN_TRACE_RNC_LOADS=1`; ordinary traces remain unaffected.

To confirm the static scene-resource map at runtime, also trace writes to the
dispatcher state byte `0xFF7E26`:

```sh
OPENALADDIN_TRACE_RNC_LOADS=1 OPENALADDIN_TRACE_SCENE_STATES=1 \
  OPENALADDIN_TRACE_FRAMES=1550 \
  OPENALADDIN_TRACE_DIR=build/re/rnc-loader-gameplay \
  ./tools/mame-trace.sh
python3 tools/analyze-rnc-load-trace.py \
  --log debug.log \
  --output build/re/rnc-loader-gameplay/rnc_loads.json
python3 tools/analyze-scene-state-trace.py \
  --trace build/re/rnc-loader-gameplay \
  --load-trace build/re/rnc-loader-gameplay/rnc_loads.json
```

The scene report attributes each dynamic upload to the most recent observed
state and checks it against `re/assets/scene_resources.yml`.  The capture
matrix enables this trace automatically and writes the combined report to
`build/re/rnc-capture-matrix/combined/scene_state_runtime.json`.

## Runtime capture matrix

For repeatable multi-scene coverage, edit the controller schedules in
`re/mame/capture_matrix.yml` and run:

```sh
python3 tools/run-mame-capture-matrix.py Disneys_Aladdin_U_p1.bin
```

Each scenario gets its own ignored directory under
`build/re/rnc-capture-matrix/`.  The runner saves that scenario's debugger log,
parses its RNC loader executions, merges all RAM/VRAM/CRAM frames, and updates
the normal RNC runtime report from the combined trace.  The merged report is
available at:

```text
build/re/rnc-capture-matrix/combined/rnc_loads.json
build/assets/rnc/runtime_analysis.json
```

Run only selected scenarios while iterating:

```sh
python3 tools/run-mame-capture-matrix.py \
  --scenario first-gameplay \
  --scenario gameplay-progression
```

The matrix manifest and merged trace retain both `scenario` and
`scenario_frame` fields, so runtime observations can be attributed back to a
specific scripted experiment.

The checked-in matrix includes three progression-focused probes in addition to
the short menu/gameplay smoke tests:

- `story-to-first-level` isolates the known opening/menu cadence and first-level
  story/title assets.
- `combat-and-death` replays the confirmed guard collision and sword-hit path.
- `long-gameplay-traversal` keeps the emulator in a long attack/movement window
  to catch later resource loads if ordinary input reaches another scene.

The long probe is deliberately an observation run, not a claim that a simple
autoplayer can finish the stage. If it still observes only scene state `0x01`,
the next experiment should target the level-exit/transition condition instead
of making the input schedule longer.

Set `OPENALADDIN_CAPTURE_VDP=0` when only the original RAM trace is wanted.

If a memory tap does not observe a candidate, enable MAME’s native debugger
watchpoint fallback:

```sh
OPENALADDIN_DEBUG_WATCH=1 OPENALADDIN_WATCH_ADDRESSES=0xFF7E28 \
  ./tools/mame-trace.sh
```

Debugger output is written to MAME's `debug.log` in the working directory.

## Confirmed observation

The verified USA/NTSC ROM reaches the first Agrabah gameplay screen after the
title/tutorial sequence.  In a controlled right-input run, RAM word
`0xff7e28` advances by `0x0100` per walking frame.  It is tracked as
`PLAYER_X` in `re/symbols/ram.yml` and exported to `build/re/mame_symbols.lua`.
