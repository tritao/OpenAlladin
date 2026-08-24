# OpenAladdin

Open-source reimplementation work for Disney's Aladdin on Sega Genesis.

## Reverse-engineering setup

The canonical reverse-engineering knowledge lives in Git. Ghidra projects and
the ROM remain local and ignored.

After cloning, initialize the pinned submodules and run:

```bash
git submodule update --init --recursive
python tools/setup-ghidra.py
python tools/import-rom.py Disneys_Aladdin_U_p1.bin
./tools/ghidra.sh
```

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

The current local dump is accepted as a recorded, unverified local identity.
Use `--allow-unverified` when experimenting with a different image.
