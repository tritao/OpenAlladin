# MAME integration

MAME-facing symbol files are generated from `re/symbols/*.yml` by the ROM
import pipeline. Generated files live in `build/re/` and are not canonical
knowledge; edit the YAML sources instead.

## First trace

With the MAME submodule built and the local ROM at the repository root:

```sh
./tools/openaladdin/mame/run.sh
```

The command writes generated output under `build/re/traces/`:

- `trace_boot.jsonl` contains run metadata, register/input records, and the
  current VDP state/checksums when that capture profile includes VDP data.
- `state.jsonl` contains the normal semantic `openaladdin-frame-state-v1`
  stream: player motion, scene state, camera, and active actor cursors.
- `ram_frames.bin` contains one raw 64 KiB work-RAM image per frame when the
  `ram` or `full` profile is selected.
- `vdp_vram_frames.bin`, `vdp_cram_frames.bin`, `vdp_vsram_frames.bin`,
  `vdp_regs_frames.bin`, and `vdp_writes.jsonl` are emitted only by the `vdp`
  or `full` profiles.

The default is 120 frames and the default capture profile is `state`.  Select
deeper captures explicitly with `OPENALADDIN_CAPTURE=ram`, `vdp`, or `full`.
Override the frame count with `OPENALADDIN_TRACE_FRAMES`.
Input can be supplied as comma-separated frame tokens, for example:

```sh
OPENALADDIN_TRACE_FRAMES=90 OPENALADDIN_INPUT='none,right*60,none' \
  ./tools/openaladdin/mame/run.sh
```

The unified frontend exposes the same profiles directly:

```sh
python tools/oa.py trace player-jump --capture state
python tools/oa.py trace player-jump --capture full
```

## Audio-bus trace

The original sound path can be captured without recording host-dependent WAV
output.  `--audio` records deterministic YM2612 and PSG writes from both the
68000 and the Genesis Z80 to `sound_writes.jsonl`.  `--audio-commands` also
logs the ROM's level-music selector and shared animation SFX command path in
`debug.log`:

```sh
python tools/oa.py trace title-menu --frames 360 \
  --audio --audio-commands --capture state \
  --trace-dir build/re/traces/audio-title

PYTHONPATH=tools python tools/openaladdin/mame/audio_trace.py \
  build/re/traces/audio-title
```

The bus trace covers 68000 YM2612 `$A04000-$A04003`, 68000 PSG ports behind
the VDP map, Z80 YM2612 `$4000-$4003`, and Z80 PSG `$7F00-$7FFF`.  It is
intended to recover the original command protocol and channel behavior before
adding a native mixer.  The command log currently identifies animation `F3`
SFX operands, level-table music IDs, and the fixed interaction event `0x31`.
The focused 68000 writes to the Z80 command cell at `$A00036` are saved
separately in `sound_mailbox.jsonl`; this distinguishes a dispatched command
from a command that never reaches the shared sound state. Enable that stream
explicitly with `--audio-mailbox` when using the unified CLI.
For a focused consumption check, add selected hexadecimal command frames after
the first command trace identifies them:

```sh
python tools/oa.py trace player-run --frames 1400 \
  --audio --audio-mailbox --audio-commands \
  --audio-read-frame 0x496 --audio-read-frame 0x55D \
  --audio-read-frame 0x56B --audio-read-frame 0x56D \
  --trace-dir build/re/traces/audio-targeted-reads
```

The selected Z80 shared-RAM reads are included in `audio_summary.json` as
`sound_mailbox_reads`; unrestricted polling is intentionally not enabled by
default because the sound driver reads its command cell continuously.
The same report decodes the 68000 queue at `$A01B40` into
`sound_mailbox_packets`. Each observed packet is `FF`, a queue opcode, and the
optional sound command ID; `0x12` is the prepare packet, `0x10` is the send
packet, and `0x16` is a two-byte control packet.

For a repeatable gameplay checkpoint, schedule a state and screenshot after
the scripted input has had time to enter the game:

```sh
OPENALADDIN_TRACE_FRAMES=360 \
OPENALADDIN_INPUT='none*30,start*90,none*60,right*120,none*60' \
OPENALADDIN_SAVE_FRAME=300 OPENALADDIN_SNAPSHOT_FRAME=300 \
  ./tools/openaladdin/mame/run.sh
```

State files and PNG snapshots are written below `build/re/traces/states/` and
`build/re/traces/snapshots/`.

An existing state can be loaded by its MAME state name:

```sh
OPENALADDIN_LOAD_STATE=gameplay MAME_XVFB=1 \
  OPENALADDIN_TRACE_FRAMES=180 ./tools/openaladdin/mame/run.sh
```

The normal trace mode is fully headless: the wrapper forces SDL's `dummy`
video driver and does not inherit an interactive desktop display.  MAME's
`-nowindow` option is deliberately not used because in MAME it means
fullscreen, not no-window.  If SDL needs a real display, run through Xvfb
instead:

```sh
MAME_XVFB=1 OPENALADDIN_TRACE_FRAMES=120 ./tools/openaladdin/mame/run.sh
```

This uses a virtual 1024×768 X11 display and MAME's software renderer; the
window remains inside Xvfb and is not shown on the desktop.  Set
`OPENALADDIN_MAME_VIDEO` to choose another MAME video backend.

An interactive MAME/debugger window is opt-in:

```sh
OPENALADDIN_MAME_HEADLESS=0 OPENALADDIN_MAME_DEBUG_UI=1 \
  ./tools/openaladdin/mame/run.sh
```

The harness also accepts declarative experiment actions compiled by
`tools/oa.py`. Boot schedules, input actions, and waits on tracked symbols or
68000 PCs are evaluated inside MAME, so gameplay captures do not need guessed
frame counts for those checkpoints.

To rank changing 16-bit words from a controlled interval:

```sh
PYTHONPATH=tools python3 tools/openaladdin/mame/analyze_trace.py build/re/traces --input right
```

The output is only a list of candidates.  Confirmed addresses will be added to
`re/symbols/ram.yml` only after a second experiment and a CPU write watchpoint.

To inventory the common actor table, enable tracing for all 32 records.  The
trace adds the table layout to its header; the analyzer reads the captured RAM
snapshots and reports type intervals, cursor positions, and ROM-decoder probes:

```sh
OPENALADDIN_TRACE_ACTORS=1 OPENALADDIN_TRACE_FRAMES=1550 \
  OPENALADDIN_TRACE_DIR=build/re/actor-gameplay \
  ./tools/openaladdin/mame/run.sh
PYTHONPATH=tools python3 tools/openaladdin/analysis/actors.py
```

The generated `build/re/actor_animation_inventory.json` is intentionally
ignored.  Actor `animation_pc` is a moving ROM cursor, so a cursor observed in
RAM is evidence of stream membership, not automatically a stream entry point.

## Canonical frame-state trace

The unified frontend can request a stable gameplay state stream:

```sh
python tools/oa.py trace player-jump --capture state
```

This writes `build/re/traces/player-jump/state.jsonl` using the
`openaladdin-frame-state-v1` format. It contains the player position and 8.8
velocities, animation cursor, scene state, camera, active actor cursors, and
the resolved collision rectangle for every non-zero animation frame pointer.
The rectangle is derived from the frame record's bytes at `+2..+5`, including
the original X-flipped signed-byte path; a missing or zero pointer emits
`null`.
The first differing field between two implementations is reported by:

```sh
python tools/oa.py compare genesis.jsonl openaladdin.jsonl
```

For collision work, compare only the resolved geometry and report the first
requested actor transition without requiring unrelated state fields to match:

```sh
python tools/oa.py compare-collision genesis.jsonl openaladdin.jsonl \
  --actor-slot 5 --transition-type 0x84
```

For a deterministic probe of a statically identified actor template, clone the
0x42-byte type-0x7D template at `0x001B81D8` into an unused slot and override its
animation cursor:

```sh
OPENALADDIN_CAPTURE_VDP=0 OPENALADDIN_TRACE_ACTORS=1 \
OPENALADDIN_TRACE_FRAMES=240 OPENALADDIN_INJECT_ACTOR_FRAME=2 \
OPENALADDIN_INJECT_ACTOR_SLOT=31 OPENALADDIN_INJECT_ACTOR_TYPE=125 \
OPENALADDIN_TRACE_DIR=build/re/actor-injection-template \
  ./tools/openaladdin/mame/run.sh
PYTHONPATH=tools python3 tools/openaladdin/analysis/actors.py \
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
  ./tools/openaladdin/mame/run.sh
PYTHONPATH=tools python3 tools/openaladdin/analysis/actor_initializers.py \
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

For a reproducible command-level movement probe, seed an actor at a known ROM
stream after entering the gameplay loop:

```sh
OPENALADDIN_CAPTURE_VDP=0 OPENALADDIN_TRACE_ACTORS=1 \
OPENALADDIN_TRACE_FRAMES=381 OPENALADDIN_INPUT='none*320,start*5,none*55' \
OPENALADDIN_INJECT_ACTOR_FRAME=361 OPENALADDIN_INJECT_ACTOR_SLOT=31 \
OPENALADDIN_INJECT_ACTOR_TYPE=125 OPENALADDIN_INJECT_ACTOR_MOVEMENT_PC=0x11f730 \
OPENALADDIN_INJECT_ACTOR_X=150 OPENALADDIN_INJECT_ACTOR_Y=416 \
OPENALADDIN_INJECT_ACTOR_FACING_X=0 OPENALADDIN_INJECT_ACTOR_FACING_Y=0 \
OPENALADDIN_INJECT_ACTOR_MOVEMENT_TIMER=0 \
OPENALADDIN_TRACE_DIR=build/re/actor-vm-command-81 \
  ./tools/openaladdin/mame/run.sh
python3 tests/native_actor_vm_commands.py
```

The checked-in command fixtures cover streams at `0x0011F730` (`0x81`),
`0x0011F728` (`0x82`), and `0x0012171C` (`0x8D`, `0x84`, and `0x85`). The
comparison selects only fields implemented by the native movement slice;
animation-owned type/frame fields are deliberately excluded.

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
  ./tools/openaladdin/mame/run.sh
```

The actor initializer at `0x001AE30A` also establishes the confirmed runtime
fields used by this slice: position at `+0x02/+0x04`, movement cursor at
`+0x0A`, velocity accumulators at `+0x18/+0x1A`, animation cursor at `+0x20`,
resource count at `+0x29`, and behavior flags at `+0x3C`.

## Targeted combat/state decompilation

The focused Ghidra request is tracked at
`re/ghidra/targets/actor-combat-targets.json`.  Re-run it from the repository root
with the existing read-only Ghidra project:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/targets/actor-combat-targets.json \
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
`re/ghidra/targets/collision-damage-targets.json` and can be regenerated with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/targets/collision-damage-targets.json \
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
./tools/openaladdin/mame/run.sh
```

Write taps can record the 68000 PC responsible for a candidate address:

```sh
OPENALADDIN_WATCH_ADDRESSES=0xFF7E28 \
  OPENALADDIN_TRACE_FRAMES=20 ./tools/openaladdin/mame/run.sh
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
PYTHONPATH=tools python3 tools/openaladdin/vm/movement.py \
  rom/Disneys_Aladdin_U_p1.bin \
  --output build/re/movement_streams.json
```

The currently confirmed movement roots include `0x0011F6D4` for the natural
type-`0x2D` guard child and `0x0011F6FE` for the looping type-`0x84` stream.
The existing actor trace inventory confirms that the decoded step addresses
are the same cursors written back to actor field `+0x0A`; conditional command
paths remain state-dependent and are listed as branch targets rather than
being guessed statically.

The targeted decompile of `MovementVM_TickActors` also confirms that signed
delta X/Y are mirrored by actor fields `+0x09`/`+0x35`, and that field `+0x36`
is the movement delay counter. Shared command `0x84` masks bit 7 from its
operand before storing the counter in movement mode. The native slot-19
fixture and `tests/native_actor_movement.py` exercise this confirmed path
against MAME frames 361..381.

## Player movement and terrain collision

The player movement/terrain request is tracked at
`re/ghidra/targets/player-movement-collision-targets.json` and can be regenerated with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/targets/player-movement-collision-targets.json \
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

`0x001B1E38` computes `WORLD_CAMERA_Y + PLAYER_Y - 0x00F0`, selects one
16-pixel row band through `0x00FF9884`, computes the single column
`(WORLD_CAMERA_X + PLAYER_X + 0x10) >> 4`, maps that terrain word through
`0x00FFAE86`, and dispatches the resulting behavior byte through the ROM
handler table at `0x004554`. It does not search nearby rows. The byte at
`0x00FFF156` is active-low controller/query state: the four query helpers
test its direction bits and `SEQ` writes the resulting pressed-direction
flags to `0x00FFF07C-0x00FFF07F` before the resolver runs. The callback
pointers observed in the gameplay trace are `0x001B3244`, `0x001B323A`, and
`0x001B324E`.

The horizontal/ceiling probe at `0x001AD632` is a separate exact pass. It
uses `WORLD_CAMERA_Y + PLAYER_Y - 0x110`, treats behavior bytes above `0xDF`
as blocking, probes the left group from `column` and the right group from
`column + 2`, and gates each extra downward-row test on
`TERRAIN_LANDING_STATE == 0`. The native mirror and regression now use those
addresses and conditions directly.

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
  ./tools/openaladdin/mame/run.sh
```

The focused terrain probes are declared in
`re/mame/experiments/manifest.yml`. They emit the semantic terrain RAM fields
on every frame in `state.jsonl`; the captured findings and the remaining
unhit ceiling/slope/special cases are recorded in
`re/mame/findings/player-terrain-experiments.json`.

The focused matrix is complemented by deterministic handler probes at the
fixed level-01 cells for behaviors `0x2B` and `0x47`, plus a runtime table
fixture for behavior `0x30`. The `0x2B` fixture starts at row 30/column 182
(`0x6008`) with `PLAYER_VY=0` and a clear landing state, reaches
`0x001B5502` on frame 1301, clears the horizontal/response fields, selects
animation stream `0x0012181A`, aligns local X from `162` to `158`, and then
continues through the positive vertical sequence `0, 180, 240`. The `0x30`
fixture uses the opening-ground cell
at row 40/column 7 (`0x6C20`), temporarily changes its behavior-table byte
from `0x11` to `0x30`, and reaches `0x001B537A` on frame 1301. The handler
subtracts `0x7C` from `PLAYER_VY`, clears the horizontal/response state,
arms `TERRAIN_LANDING_STATE=0xFF`, and aligns local X through
`0x001A99C6`.
The `0x47` probe confirms that `0xFFF0A4` is the toggled surface-mode word
and `0xFFF0C2` is the handler's one-shot latch; the native trace exposes both
fields separately.

Run the landing fixture through the unified frontend with:

```sh
python3 tools/oa.py trace terrain-handler-30 --state-output --edges
```

Run the accepted stop-and-align fixture with:

```sh
python3 tools/oa.py trace terrain-handler-2b --state-output --edges
```

The native vertical slice now mirrors this fixed-ROM lookup in
`Level::resolve_player_cell`/`Level::query_player`. `support_row()` and the
rectangle-based `horizontal_blocked()` path have been removed. The native
regression can be run after building with:

```sh
python tests/native_terrain_physics.py
```

## Player camera model

The camera recovery is recorded in
`re/mame/findings/player-camera-findings.json`. The fixed-ROM coordinate
pipeline is:

```text
PLAYER_X/Y (local)
  + WORLD_CAMERA_X/Y
  = PLAYER_WORLD_X/Y
  -> PLAYER_WORLD_Y - 0x00F0 for terrain visual coordinates
```

`Camera_UpdateFollow` at `0x001AA90C` compares the local player against
`CAMERA_HORIZONTAL_THRESHOLD`/`CAMERA_VERTICAL_THRESHOLD`, indexes the ROM
dampening tables at `0x2A52` and `0x2BA4`, and moves local player and camera
coordinates by equal and opposite deltas. `CAMERA_REFERENCE_X/Y` plus the
signed `CAMERA_SCROLL_X/Y` accumulators preserve the 16-pixel tile-update
state. The limits are `LEVEL_WIDTH_PIXELS - 0x161` and
`LEVEL_HEIGHT_PIXELS - 0xF1`, with a minimum effective coordinate of `0x11`.

Scene state `0x08` bypasses normal follow and enters the transition branch at
`0x001A9D18`; the native slice exposes that mode in `CameraState` and keeps
the controller fallback's eight-pixel local-coordinate bounds.

The native renderer consumes the same `CameraState.x/y` used by terrain and
world-coordinate calculations. Camera fields, local/world player positions,
thresholds, accumulators, limits, and state-08 mode are emitted in the shared
`openaladdin-frame-state-v1` JSONL trace. Differential replay can include the
camera coordinates directly:

```sh
python tools/oa.py regression player-jump --field player.world_x --field player.world_y --field camera.x --field camera.y
```

## Level-transition state tracing

The level-transition request is tracked at
`re/ghidra/targets/level-transition-targets.json` and includes both code targets and
references to the RAM state/transition flags. Regenerate its focused report
with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/targets/level-transition-targets.json \
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

The table is a five-entry, six-byte record array at ROM `0x004B04`. Its state
byte is at record offset `+4`; entry 1 is at `0x004B0A` and contains state
`0x08` at `0x004B0E`. The state write instruction in the selector is
`0x001B3F0A`. The compact script region decoded by the asset pipeline spans
`0x004080` through `0x0040B8`; the live cursor observed in the first level is
`0x004082`, midway through the first record. The script-side state move is at
`0x001A8ED2`.

Regenerate the machine-readable table/script report with the normal asset
extractor:

```sh
python tools/oa.py assets --rom rom/Disneys_Aladdin_U_p1.bin --no-levels --no-sprites --no-animations
```

It writes `build/assets/scene_transitions.json` and records it in the asset
manifest.

To capture the actual state setter PC during a gameplay experiment:

```sh
OPENALADDIN_CAPTURE_VDP=0 \
OPENALADDIN_TRACE_SCENE_STATES=1 \
OPENALADDIN_DEBUG_WATCH=1 \
OPENALADDIN_WATCH_ADDRESSES=0xFF7E26,0xFF7E22,0xFFF57C,0xFFF57E,0xFFF0D0,0xFFF0DA,0xFFF0DC,0xFFF0E6 \
OPENALADDIN_TRACE_FRAMES=5000 \
OPENALADDIN_INPUT='none*320,start*5,none*200,start*5,none*170,start*5,none*200,start*5,none*150,start*5,none*180,right+a*3755' \
OPENALADDIN_TRACE_DIR=build/re/level-transition-watch \
  ./tools/openaladdin/mame/run.sh rom/Disneys_Aladdin_U_p1.bin
PYTHONPATH=tools python3 tools/openaladdin/analysis/transition_watch.py \
  --log debug.log \
  --output build/re/level-transition-watch/transition_watch.json
```

The completed title-to-first-level trace covered 1,300 frames and wrote
`SCENE_STATE` only during startup (`0x00` to `0x01`). A save-state-controlled
selector probe now also confirms the ROM's dynamic table-entry-1 write to
`SCENE_STATE=0x08`; the full player-driven level exit remains a separate
longer traversal experiment.

The machine-readable result is recorded in
`re/mame/findings/level-transition-findings.json`. It includes the selector
write PC, all four transition-gate observations, and the five RNC uploads
observed after dispatching state `0x08`.

The natural-exit experiment is recorded separately in
`re/mame/findings/natural-exit-findings.json`. It uses controller input only,
reaches the first rope and upper walkway, clears both guards on that route, and
confirms that the level-01 exit predicate is not reached by the current
6,000-frame route; no direct scene or player-memory setup is used in that
trace.

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
finding is recorded in `re/mame/findings/player-terrain-findings.json`.

The common `0x001B5320` handler is now mirrored in the native slice for the
observed path. When the landing state is non-zero and no type-`0x8C` actor
exists, it scans actor slots 3 through 22, copies the template at
`0x001B7E2C`, and writes the player's world position. The template selects
animation stream `0x00124408`; the first animation cursor advance occurs on
the next frame. The exact ROM guard and allocator details are recorded in
`re/mame/findings/player-terrain-experiments.json`. Ceiling, slope, and other
special handlers remain unhit by the generic opening-room routes and still
need controlled fixtures.

To compare the captured VDP memories and DMA stream with the native assets
already extracted from the ROM:

```sh
PYTHONPATH=tools python3 tools/openaladdin/assets/compare_runtime.py
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
  ./tools/openaladdin/mame/run.sh
PYTHONPATH=tools python3 tools/openaladdin/assets/rnc_load_trace.py \
  --log debug.log \
  --output build/re/rnc-loader-gameplay/rnc_loads.json
```

Merge the parsed execution evidence into the runtime asset report:

```sh
PYTHONPATH=tools python3 tools/openaladdin/assets/rnc_runtime_cli.py \
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
  ./tools/openaladdin/mame/run.sh
PYTHONPATH=tools python3 tools/openaladdin/assets/rnc_load_trace.py \
  --log debug.log \
  --output build/re/rnc-loader-gameplay/rnc_loads.json
PYTHONPATH=tools python3 tools/openaladdin/analysis/scenes.py \
  --trace build/re/rnc-loader-gameplay \
  --load-trace build/re/rnc-loader-gameplay/rnc_loads.json
```

Checkpoint-loaded traces have a MAME machine-frame offset because the saved
state restores the emulated frame counter.  Pass that offset when correlating
the loader log with the Lua trace, for example:

```sh
PYTHONPATH=tools python3 tools/openaladdin/analysis/scenes.py \
  --trace build/re/state03-transition-final-20260825 \
  --load-trace build/re/state03-transition-final-20260825/rnc_loads.json \
  --machine-frame-offset 3247 \
  --output build/re/state03-transition-final-20260825/scene_state_runtime.json
```

The scene report attributes each dynamic upload to the most recent observed
state and checks it against `re/assets/scene_resources.yml`.  The capture
matrix enables this trace automatically and writes the combined report to
`build/re/rnc-capture-matrix/combined/scene_state_runtime.json`.

## Runtime capture matrix

For repeatable multi-scene coverage, edit the controller schedules in
`re/mame/experiments/capture_matrix.yml` and run:

```sh
PYTHONPATH=tools python3 tools/openaladdin/mame/capture_matrix.py rom/Disneys_Aladdin_U_p1.bin
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
PYTHONPATH=tools python3 tools/openaladdin/mame/capture_matrix.py \
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
autoplayer can finish the stage. The focused state-`0x08` selector evidence is
tracked separately in `re/mame/findings/level-transition-findings.json`.

Set `OPENALADDIN_CAPTURE=ram` when only the raw work-RAM trace is wanted.
`OPENALADDIN_CAPTURE_VDP=0` remains accepted as a compatibility alias.

If a memory tap does not observe a candidate, enable MAME’s native debugger
watchpoint fallback:

```sh
OPENALADDIN_DEBUG_WATCH=1 OPENALADDIN_WATCH_ADDRESSES=0xFF7E28 \
  ./tools/openaladdin/mame/run.sh
```

Debugger output is written to MAME's `debug.log` in the working directory.

## Confirmed observation

The verified USA/NTSC ROM reaches the first Agrabah gameplay screen after the
title/tutorial sequence.  In a controlled right-input run, RAM word
`0xff7e28` advances by `0x0100` per walking frame.  It is tracked as
`PLAYER_X` in `re/symbols/ram.yml` and exported to `build/re/mame_symbols.lua`.
