# OpenAladdin

Open-source reimplementation work for Disney's Aladdin on Sega Genesis.

## Reverse-engineering setup

The canonical reverse-engineering knowledge lives in Git. Ghidra projects and
generated analysis remain local and ignored. The publisher-authorized ROM dump
used by the reproducible tooling is committed at the repository root.

After cloning, initialize the pinned submodules and run:

```bash
git submodule update --init --recursive
python tools/setup-ghidra.py
python tools/import-rom.py Disneys_Aladdin_U_p1.bin
./tools/ghidra.sh
```

The supported workflow frontend is:

```bash
python tools/oa.py status
python tools/oa.py verify
python tools/oa.py ghidra rebuild
python tools/oa.py trace title-menu --capture state
python tools/oa.py trace player-run --capture state
python tools/oa.py trace player-jump --capture state
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

The experiment manifest is `re/mame/experiments.yml`. It supports boot
scenarios, input actions, and direct memory/PC wait conditions; the MAME Lua
harness evaluates those waits while the emulator runs.

To extract the known Genesis graphics and animation data:

```bash
python tools/extract-assets.py Disneys_Aladdin_U_p1.bin
```

The generated asset manifest and renders are under `build/assets/` and are
not committed. See [`re/assets/README.md`](re/assets/README.md) for the
current format coverage.

`setup-ghidra.py` downloads Ghidra 12.1.3, verifies its SHA-256, installs
PyGhidra into `.tools/venv`, builds the Genesis loader submodule, and installs
the resulting extension locally. `import-rom.py` uses the built-in 68000 raw
loader for deterministic imports, then applies the tracked Genesis memory map,
vectors, symbols, and structures.

Generated exports are written to `build/re/`; edit files under `re/` instead.

The committed dump is recorded as the canonical local identity in
`re/config/roms.yml`. Use `--allow-unverified` when experimenting with a
different image.

## C++/SDL vertical slice

The first runtime slice is now buildable from the extracted level-01 assets.
It renders the exact Genesis background pixels, loads the big-endian terrain
map and `floor.bin` behavior table, and implements the recovered player 8.8
motion integrator, jump impulse, gravity miss path, and surface snapping.
The player is intentionally a diagnostic silhouette until the recovered player
sprite-frame format is connected to the animation VM.

Regenerate the runtime-friendly PPM render and build it with:

```bash
./build.sh
./run.sh
```

For a deterministic headless smoke test:

```bash
SDL_VIDEODRIVER=dummy ./run.sh --no-window --frames 120
```

Use `--demo` with that command to run a deterministic right-and-jump input
sequence for smoke testing.

Arrow keys or A/D apply horizontal input; Space or C jumps. The next runtime
slice should replace the silhouette with the already-extracted player frame
streams, then add the confirmed animation and movement VM interpreters.
