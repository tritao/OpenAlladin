# OpenAladdin

Open-source reimplementation work for Disney's Aladdin on Sega Genesis.

## Reverse-engineering setup

The canonical reverse-engineering knowledge lives in Git. Ghidra projects and
generated analysis remain local and ignored. The configured ROM input is
`rom/Disneys_Aladdin_U_p1.bin`.

After cloning, initialize the pinned submodules and run:

```bash
git submodule update --init --recursive
python -m genie setup
python -m genie ghidra rebuild
# Debian/Ubuntu: install the pinned 68000 assembler toolchain for deasm verify
sudo apt-get install binutils-m68k-linux-gnu
```

Genie is the packaged frontend for the same workflow. Install it from the
checkout to enable both entry forms:

```bash
python3 -m pip install -e .
genie --help
python3 -m genie --help
genie doctor
```

`genie doctor` checks for GNU m68k binutils, configured as version 2.42, which
provides the assembler/linker used for deterministic source verification.

The canonical tracked symbols are queryable directly through Genie:

```bash
genie symbols show 0x001AC784
genie symbols find AnimationVM
genie symbols validate
genie symbols stats
genie symbols unknown --kind function
genie symbols next --kind function
genie symbols rename 0x00184320 SceneResource_Whatever
genie symbols describe 0x00184320 "resource decoder entry point"
genie symbols confidence 0x00184320 decompiled
```

Build the whole-ROM static analysis database once with `genie ghidra scan`;
its JSON indexes are written to `build/re/full-rom/` and can then be queried
with `genie ghidra function`, `callers`, `callees`, `writers`, `readers`,
`xrefs`, `context`, and `unknown` without another Ghidra run. The context
query combines callers, callees, RAM references, xrefs, nearby layout objects,
runtime coverage, and known symbols referenced by one address.

Decompile one function on demand. The first request uses the disposable Ghidra
project; later requests reuse the cached pseudocode:

```bash
genie ghidra decompile 0x001AC784
genie ghidra decompile --review
genie ghidra context 0x001AC784 --include-decompile
```

`--include-decompile` includes cached pseudocode in the context output; it
never launches Ghidra, so it is safe for fast offline triage after a
decompilation has been cached. `--review` decompiles every named function
with an open semantic-review question in one Ghidra launch and fills the same
per-function cache.

Validate a generated scan against the recovered scheduler, canonical symbols,
dispatch-table xrefs, and normalized ROM coverage before consuming it:

```bash
genie ghidra validate-db
```

Build and inspect the first fused ROM layout view. It combines Ghidra ranges,
tracked symbols, recovered VM streams, decoded assets, and jump-table evidence
into a gap-free partition:

```bash
genie layout build
genie layout show 0x001223DA
genie layout gaps
genie layout candidates --limit 50
genie layout stats
genie layout validate

genie data stats
genie data todo --kind animation
genie data next --kind actor-template
genie data context 0x00121964
```

`layout candidates` is an offline triage report for remaining `UNKNOWN`
ranges. It ranks gaps using incoming Ghidra references, decoded animation or
movement streams, direct pointers in established actor-template records, and
conservative VM probes at referenced anchors. It is advisory only; promotion
into the canonical layout still requires a reviewed symbol, boundary test,
and evidence record. The report marks evidence quality and promotion advice;
data-only xrefs alone do not establish a target format. Use
`genie layout candidates --strong-only` to hide those weak data-only leads
while working through the queue.

Generate a complete local ROM representation from the validated layout and
canonical instruction export. The output is ignored and can be regenerated
after every scan:

```bash
genie deasm build
genie deasm stats
genie deasm todo
genie deasm verify
```

`deasm build` refuses layouts with holes or overlaps and emits every ROM byte
exactly once. `deasm verify` assembles that source with the pinned GNU m68k
toolchain, preserves exact bytes for instructions that do not safely round-trip
as readable syntax, and compares the rebuilt ROM byte-for-byte. Run
`genie ghidra scan` first when `instructions.json` is absent. Cached single-
function pseudocode is written below `build/re/full-rom/decompile/`.

Use the `genie` command for all reverse-engineering workflows. The equivalent
module form, `python -m genie`, is useful when running from an unmanaged
checkout.

The supported workflow frontend is:

```bash
genie status
genie play
genie play mame
genie mame
```

`genie play` launches the native OpenAladdin build by default. It delegates to
the root `run.sh`, which performs an incremental native build before launch.
Use `genie play mame` (or `--client mame`) for a windowed MAME session; pass
`--headless` when running either client from automation.

The direct Python form remains equivalent:

```bash
python -m genie status
python -m genie play
python -m genie play mame
python -m genie verify
python -m genie ghidra rebuild
python -m genie mame
python -m genie trace title-menu --capture state
python -m genie trace player-run --capture state
python -m genie trace player-jump --capture state
python -m genie record level01-good-run
python -m genie replay level01-good-run --client mame
python -m genie replay level01-good-run --client native
python -m genie parity level01-good-run
python -m genie replay level01-good-run --client native --segment level01-entry
python -m genie parity level01-good-run --segment level01-entry
python -m genie decode animation --verify
python -m genie decode movement --verify
python -m genie assets
python -m genie validate
python -m genie coverage report
```

Named traces write to `build/re/traces/<scenario>/`. The `state` capture
profile writes the versioned `openaladdin-frame-state-v1` JSONL stream at
`state.jsonl`; use `--state-output` with `ram`, `vdp`, or `full` when a raw
capture also needs semantic state. Compare two implementations with:

```bash
python -m genie compare genesis.jsonl openaladdin.jsonl
```

Run a checkpointed MAME-to-native differential probe with:

```bash
python -m genie regression player-jump
```

This runs the MAME experiment, finds its gameplay_checkpoint marker, replays
the exact post-checkpoint input tokens in the native slice, aligns both traces
at frame 0, and compares the implemented player physics fields plus the
decoded player frame pointer. Scene state remains outside this focused
comparison; native Level 01 actor state is now refilled from the interaction
map rather than a default snapshot.

The first native-vs-MAME actor-table probe is:

```bash
python -m genie regression level01-actor-boot \
  --trace-dir build/re/level01-actor-boot
```

It boots through the real menu sequence, aligns at the first gameplay frame,
walks through the opening refill window with no actor fixture, and compares
shared actor-table fields by slot. The actor comparator ignores slot 0 by
default because player parity is reported separately; use
`genie/games/aladdin/mame/compare_actors.py --include-player` when needed. Until
scene-created actors are recovered, this probe intentionally reports the first
remaining actor-spawn divergence and exits non-zero.

The experiment manifest is `re/mame/experiments/manifest.yml`. It supports boot
scenarios, input actions, and direct memory/PC wait conditions; the MAME Lua
harness evaluates those waits while the emulator runs.

## Recorded gameplay runs

Launch a general interactive MAME session through the project wrapper with:

```bash
python -m genie mame
```

It runs until MAME exits and writes semantic state under
`build/re/mame-session/`. Use `--headless --frames N` for bounded automated
sessions, or `--input SCHEDULE` to inject a deterministic schedule.

Record a normal interactive MAME session with:

```bash
python -m genie record level01-good-run
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
python -m genie replay level01-good-run --client mame
```

The canonical JSONL timeline can also drive the native runtime, after which
the first semantic divergence is reported with:

```bash
python -m genie replay level01-good-run --client native
python -m genie parity level01-good-run
python -m genie inputs summarize build/runs/level01-good-run/input.jsonl
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
`genie/core/mame/trace.py --require-left-atomic --left-atomic-only` or the actor
comparator's equivalent flags when comparing against a native trace.
Native state output uses `openaladdin-frame-state-v3`: it adds scheduler gates,
all causal actor fields, and the 0x42-byte per-slot VM actor record as hex.
Native checkpoints are binary, asset-bound debugging artifacts that preserve
the same VM RAM, actor tables, scene, camera, VDP, and scheduler state for
uninterrupted continuation tests.
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
python -m genie assets
```

The generated asset manifest and renders are under `build/assets/` and are
not committed. See [`re/assets/README.md`](re/assets/README.md) for the
current format coverage.

`genie setup` downloads Ghidra 12.1.3, verifies its SHA-256, and installs
PyGhidra into `.tools/venv`. `genie ghidra rebuild` uses the built-in 68000 raw
loader for deterministic imports, then applies the tracked Genesis memory map,
vectors, symbols, and structures.

Generated exports are written to `build/re/`; edit files under `re/` instead.

The repository boundaries are:

```text
src/       native OpenAladdin implementation
tests/     native/tooling tests
genie/     Python workflow implementation and CLI
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

Regenerate the runtime-friendly PPM render and explicitly bootstrap the build
with:

```bash
./build.sh
```

For normal native launches, use `genie play` (or `./run.sh` directly). Both
launchers perform an incremental native build automatically.

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
python -m genie audio-parity \
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
