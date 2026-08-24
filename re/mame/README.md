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
