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
python -m genie symbols unknown --kind function
python -m genie symbols next --kind function
python -m genie symbols rename 0x00184320 SceneResource_Whatever
python -m genie symbols describe 0x00184320 "resource decoder entry point"
python -m genie symbols confidence 0x00184320 decompiled
```

For a fresh whole-ROM static database, run:

```bash
python -m genie ghidra scan
```

This retains the normal `build/re/` exports and additionally writes the
queryable database below `build/re/full-rom/`:
`metadata.json`, `functions.json`, `callgraph.json`, `xrefs.json`,
`memory_reads.json`, `memory_writes.json`, `indirect_calls.json`,
`jump_tables.json`, `address_classes.json`, and `instructions.json`. The
instruction export preserves original bytes, decoded mnemonic/operands, and
references for offline deassembly. The graph and reference files
can be queried without launching Ghidra again:

```bash
python -m genie ghidra function 0x001AC784
python -m genie ghidra callers 0x001AC784
python -m genie ghidra callees 0x001AC784
python -m genie ghidra writers 0x00FF7E60
python -m genie ghidra readers 0x00FF7E28
python -m genie ghidra xrefs 0x001B557E
python -m genie ghidra context 0x00184320
python -m genie ghidra decompile 0x00184320
python -m genie ghidra decompile --review
python -m genie ghidra unknown
```

The review form batches all named functions with open semantic-review
questions into one Ghidra launch and caches each pseudocode body under
`build/re/full-rom/decompile/`.

Before using a scan as deassembly input, run its known-fact trust gate:

```bash
python -m genie ghidra validate-db
```

The first ROM layout projection is written beside the scan and merges Ghidra
functions/data, canonical symbols, VM stream reports, decoded asset manifests,
and recovered jump tables into one gap-free range partition:

```bash
python -m genie layout build
python -m genie layout show 0x001223DA
python -m genie layout gaps
python -m genie layout stats
python -m genie layout validate
```

Generate the first complete local ROM source from the offline scan:

```bash
python -m genie deasm build
python -m genie deasm stats
python -m genie deasm todo
```

The emitter consumes only the ROM, `layout.json`, `instructions.json`, and
canonical symbols. It refuses to emit if the layout is not a gap-free,
non-overlapping partition or if an instruction's exported bytes differ from
the ROM.

When `build/assets/sprites/frames.json` is present, the layout also consumes
the validated Chopper manifest: the 1409-entry frame pointer table, each
variable-size sprite frame record, and the physical tile-data runs are marked
as `POINTER_TABLE`/`GRAPHICS`. Run `python -m genie assets` before rebuilding
the layout when starting from a clean checkout.

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
