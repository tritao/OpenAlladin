# Reverse engineering workspace

Git-tracked YAML files are the canonical reverse-engineering knowledge. The
Ghidra project under `re/ghidra/project/` is disposable and ignored.

The reproducible local workflow is:

```bash
python -m genie setup
python -m genie verify
python -m genie ghidra rebuild
```

The importer uses the built-in Ghidra 68000 language, defines the Genesis
address map, parses the vector table, applies tracked symbols, and writes
machine-readable exports to `build/re/`.

The recovered frame scheduler is tracked separately in
`re/scheduler/frame_phases.yml`. It is a static call-chain model rooted at
`Game_FrameUpdateLoop` (`0x001A8C16`) and `VBlankInterrupt` (`0x001B246E`);
native scheduler phases remain provisional until a ROM mechanism and causal
parity evidence support the mapping.

Native asset extraction is separate from the Ghidra project and writes
generated graphics under `build/assets/`:

```bash
python -m genie assets
```
