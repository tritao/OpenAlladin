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

Write taps can record the 68000 PC responsible for a candidate address:

```sh
OPENALADDIN_WATCH_ADDRESSES=0xFF7E28 \
  OPENALADDIN_TRACE_FRAMES=20 ./tools/mame-trace.sh
```

Write events appear as `{"type":"write", ...}` records in
`trace_boot.jsonl`.

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
