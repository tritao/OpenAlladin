# Reverse engineering workspace

Git-tracked YAML files are the canonical reverse-engineering knowledge. The
Ghidra project under `re/ghidra/project/` is disposable and ignored.

The reproducible local workflow is:

```bash
python tools/setup-ghidra.py
python tools/verify-rom.py Disneys_Aladdin_U_p1.bin
python tools/import-rom.py Disneys_Aladdin_U_p1.bin
./tools/ghidra.sh
```

The importer uses the built-in Ghidra 68000 language, defines the Genesis
address map, parses the vector table, applies tracked symbols, and writes
machine-readable exports to `build/re/`.
