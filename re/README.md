# Reverse engineering workspace

Git-tracked YAML files are the canonical reverse-engineering knowledge. The
Ghidra project under `re/ghidra/project/` is disposable and ignored.

The reproducible local workflow is:

```bash
python tools/oa.py setup
python tools/oa.py verify
python tools/oa.py ghidra rebuild
```

The importer uses the built-in Ghidra 68000 language, defines the Genesis
address map, parses the vector table, applies tracked symbols, and writes
machine-readable exports to `build/re/`.

Native asset extraction is separate from the Ghidra project and writes
generated graphics under `build/assets/`:

```bash
python tools/oa.py assets
```
