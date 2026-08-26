# OpenAladdin

Open-source reimplementation work for Disney's Aladdin on Sega Genesis.

## Reverse-engineering setup

The canonical reverse-engineering knowledge lives in Git. Ghidra projects and
generated analysis remain local and ignored. The configured ROM input is
`rom/Disneys_Aladdin_U_p1.bin`.

After cloning, initialize the pinned submodules and run:

```bash
git submodule update --init --recursive
python tools/oa.py setup
python tools/oa.py ghidra rebuild
```

The supported workflow frontend is:

```bash
./oa.sh status
./oa.sh mame
```

The root `oa.sh` wrapper forwards all arguments to `tools/oa.py`. The direct
Python form remains equivalent:

```bash
python tools/oa.py status
python tools/oa.py verify
python tools/oa.py ghidra rebuild
python tools/oa.py mame
python tools/oa.py trace title-menu --capture state
python tools/oa.py trace player-run --capture state
python tools/oa.py trace player-jump --capture state
python tools/oa.py record level01-good-run
python tools/oa.py replay level01-good-run --client mame
python tools/oa.py replay level01-good-run --client native
python tools/oa.py parity level01-good-run
python tools/oa.py replay level01-good-run --client native --segment level01-entry
python tools/oa.py parity level01-good-run --segment level01-entry
python tools/oa.py decode animation --verify
python tools/oa.py decode movement --verify
python tools/oa.py assets
python tools/oa.py validate
```

Named traces write to `build/re/traces/<scenario>/`. The `state` capture
profile writes the versioned `openaladdin-frame-state-v1` JSONL stream at
`state.jsonl`; use `--state-output` with `ram`, `vdp`, or `full` when a raw
capture also needs semantic state. Compare two implementations with:

```bash
python tools/oa.py compare genesis.jsonl openaladdin.jsonl
```

Run a checkpointed MAME-to-native differential probe with:

```bash
python tools/oa.py regression player-jump
```

This runs the MAME experiment, finds its gameplay_checkpoint marker, replays
the exact post-checkpoint input tokens in the native slice, aligns both traces
at frame 0, and compares the implemented player physics fields plus the
decoded player frame pointer. Scene state remains outside this focused
comparison; native Level 01 actor state is now refilled from the interaction
map rather than a default snapshot.

The first native-vs-MAME actor-table probe is:

```bash
python tools/oa.py regression level01-actor-boot \
  --trace-dir build/re/level01-actor-boot
```

It boots through the real menu sequence, aligns at the first gameplay frame,
walks through the opening refill window with no actor fixture, and compares
shared actor-table fields by slot. The actor comparator ignores slot 0 by
default because player parity is reported separately; use
`tools/openaladdin/mame/compare_actors.py --include-player` when needed. Until
scene-created actors are recovered, this probe intentionally reports the first
remaining actor-spawn divergence and exits non-zero.

The experiment manifest is `re/mame/experiments/manifest.yml`. It supports boot
scenarios, input actions, and direct memory/PC wait conditions; the MAME Lua
harness evaluates those waits while the emulator runs.

## Recorded gameplay runs

Launch a general interactive MAME session through the project wrapper with:

```bash
python tools/oa.py mame
```

It runs until MAME exits and writes semantic state under
`build/re/mame-session/`. Use `--headless --frames N` for bounded automated
sessions, or `--input SCHEDULE` to inject a deterministic schedule.

Record a normal interactive MAME session with:

```bash
python tools/oa.py record level01-good-run
```

Quit MAME when the session is complete. Recording is deliberately two-stage:
the live pass writes the per-frame `input.jsonl` and MAME's native `mame.inp`,
then an automatic headless `.inp` playback performs synchronized capture and
event extraction. The run is written below `build/runs/level01-good-run/` with
the provenance manifest, `raw/state.jsonl`, synchronized `state.synced.jsonl`,
semantic `state.jsonl`, semantic `events.jsonl`, derived `segments.json`, and
the `checkpoints/` state directory.
The canonical input mask is active-high and uses `up=1`, `down=2`, `left=4`,
`right=8`, `a=16`, `b=32`, `c=64`, and `start=128`. The formal frame contract
is `S[N] = state at synchronization boundary N` and `I[N] = input used for
the transition S[N] -> S[N+1]`; it is not a host key event.

The post-recording Python event engine evaluates the tracked detector
definitions against synchronized states. It records both `onset_frame` and
`confirmed_frame` (the latter is used for the replay checkpoint), and a fast
second playback materializes `checkpoints/genesis/*.sta`. `segments.json`
indexes the first replayable frame after each detected event and retains both
the detector's `event_frame` and the segment's `start_frame`; the complete
timelines remain the source of truth. Detector definitions live in
`re/mame/events/manifest.yml`.
Detector `emit` modes are explicit: `once` means once per run,
`rising_edge` means once per active interval after confirmation, and
`every_stable_interval` retains the repeatable per-interval behavior.

MAME and native parity intentionally have separate boundaries. `start_frame`
is the exact post-event MAME save-state boundary. A detector may also declare
`native_ready` predicates; `native_start_frame` is discovered from the
captured state trace as the first frame whose predicates remain true for the
configured stable window. This handles transient entry/transition state
without baking a frame offset into the tooling. A segment without a stable
native boundary remains valid for MAME replay but is reported as unavailable
for native parity. When the first animation-cursor transition is isolated in
the trace, `native_animation_phase` also records the derived native VM
scheduler delay; this is kept separate from the Genesis animation timer.

MAME replay uses the native `.inp` as its authoritative replay artifact,
re-runs the same synchronized capture, and compares the regenerated state
trace with the recording on the common atomic frame set:

```bash
python tools/oa.py replay level01-good-run --client mame
```

The canonical JSONL timeline can also drive the native runtime, after which
the first semantic divergence is reported with:

```bash
python tools/oa.py replay level01-good-run --client native
python tools/oa.py parity level01-good-run
python tools/oa.py inputs summarize build/runs/level01-good-run/input.jsonl
```

The recorder never rewrites raw observations. The synchronized semantic
pipeline records its transformations in the state header, including the
narrow animation-write-order repair where Genesis writes a frame pointer
before its advanced cursor. Synchronized runs use
`openaladdin-frame-state-v2`: each record marked `capture.atomic` contains the
player, camera, terrain, scene, and all 32 actor slots read at one game-loop
boundary. `state.raw.jsonl` remains the video-boundary observation and
`state.synced.jsonl` is the pre-normalization derived view. Strict actor
comparisons reject traces without this qualification; use
`compare_state.py --require-left-atomic --left-atomic-only` or the actor
comparator's equivalent flags when comparing against a native trace.
`trace-quality.json` reports input/state continuity, synchronization and
atomic coverage, normalization counts, checkpoint hash verification, replay
round-trip status, and the quality stage (`recorded`, `captured`,
`deterministic`, `semantic-verified`, or `parity-ready`).

Detected segments can be replayed without rerunning the menu or earlier
gameplay. A MAME segment replay starts from the save state captured at the
event boundary; the native client starts from the stable checkpoint values at
`native_start_frame` materialized in `segments.json`. Segment artifacts live under
`replay/<client>/<segment>/`, including rebased `genesis.jsonl` and
`input.jsonl`. The full-run MAME replay remains the `.inp` determinism check;
segment MAME replay uses the canonical input schedule because MAME's native
`.inp` format does not provide a portable seek operation.

To extract the known Genesis graphics and animation data:

```bash
python tools/oa.py assets
```

The generated asset manifest and renders are under `build/assets/` and are
not committed. See [`re/assets/README.md`](re/assets/README.md) for the
current format coverage.

`oa setup` downloads Ghidra 12.1.3, verifies its SHA-256, and installs
PyGhidra into `.tools/venv`. `oa ghidra rebuild` uses the built-in 68000 raw
loader for deterministic imports, then applies the tracked Genesis memory map,
vectors, symbols, and structures.

Generated exports are written to `build/re/`; edit files under `re/` instead.

The repository boundaries are:

```text
src/       native OpenAladdin implementation
tests/     native/tooling tests
tools/     Python workflow implementation (`tools/oa.py` is the frontend)
re/        tracked reverse-engineering knowledge and MAME/Ghidra inputs
rom/       ROM inputs
external/  source dependencies and developer tools
build/     ignored generated output
```

The committed dump is recorded as the canonical local identity in
`re/config/roms.yml`. Use `--allow-unverified` when experimenting with a
different image.

## C++/SDL vertical slice

The first runtime slice is now buildable from the extracted level-01 assets.
It renders the exact Genesis background pixels, loads the big-endian terrain
map and `floor.bin` behavior table, and implements the recovered player 8.8
motion integrator, jump impulse, gravity miss path, and surface snapping.
The player now uses the extracted Chopper sprite database. The native runtime
decodes `build/assets/sprites/frames.json` and its `.SEG` tile sets into
palette-indexed multipart frames. A minimal data-driven player animation VM
now advances the recovered idle, run, brake, jump, and landing streams; it is
deliberately limited to the observed frame sequences and dwell times, not yet
the complete conditional ROM VM. Chopper's 0x80/0x80 frame origin, part
offsets, source-order layering, palette lines, and renderer X/Y flips are
covered by the pure sprite renderer test.

Regenerate the runtime-friendly PPM render and build it with:

```bash
./build.sh
./run.sh
```

Audio is enabled by default. The native Level 01 music sequence is `0x49`;
use `--sound-id ID` to audition any recovered ROM sequence (`0x00` through
`0x71`), for example:

```bash
SDL_AUDIODRIVER=pulse ./run.sh --sound-id 0x4C --frames 600
```

The currently confirmed IDs are Level 01 music `0x49`, animation SFX `0x4C`,
and interaction event `0x31`.

For deterministic audio parity captures, add `--audio-trace PATH`; this writes
native command, decoded-driver-event, and YM2612/PSG bus records as JSONL.
Compare it with a MAME trace using:

```bash
python tools/oa.py audio-parity \
  build/re/traces/audio-title build/re/traces/audio-native.jsonl
```

The native build is also available directly through CMake. CMake detects the
repository-local SDL2 sysroot under `build/deps/sdl2` when it exists, or a
system SDL2 installation otherwise:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Set `-DOPENALADDIN_SDL2_ROOT=/path/to/sdl2` when SDL2 is installed in a
non-standard prefix. Run the focused sprite checks with:

```bash
ctest --test-dir build -R 'sprite_renderer|player_animation' --output-on-failure
python3 tests/native_sprites.py
```

For a deterministic headless smoke test:

```bash
SDL_VIDEODRIVER=dummy ./run.sh --no-window --frames 120
```

Use `--demo` with that command to run a deterministic right-and-jump input
sequence for smoke testing.

Arrow keys or A/D apply horizontal input; Space or C jumps. The next runtime
slice can add confirmed animation and movement VM interpreters without mixing
those questions with graphics-format decoding.
