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

The tracked symbol maps are available through one normalized Genie service:

```bash
python -m genie symbols show 0x001AC784
python -m genie symbols find AnimationVM
python -m genie symbols list --kind function
python -m genie symbols validate
python -m genie symbols stats
```

For a fresh whole-ROM static database, run:

```bash
python -m genie ghidra scan
```

This retains the normal `build/re/` exports and additionally writes the
queryable database below `build/re/full-rom/`:
`metadata.json`, `functions.json`, `callgraph.json`, `xrefs.json`,
`memory_reads.json`, `memory_writes.json`, `indirect_calls.json`,
`jump_tables.json`, and `address_classes.json`. The graph and reference files
can be queried without launching Ghidra again:

```bash
python -m genie ghidra function 0x001AC784
python -m genie ghidra callers 0x001AC784
python -m genie ghidra callees 0x001AC784
python -m genie ghidra writers 0x00FF7E60
python -m genie ghidra readers 0x00FF7E28
python -m genie ghidra xrefs 0x001B557E
python -m genie ghidra unknown
```

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
