# External dependencies

All source dependencies used by the project live under this directory:

```text
external/SDL/       SDL runtime dependency
external/mame/      Genesis emulator and tracing environment
external/ghidra/    Ghidra source/reference checkout
```

Downloaded or generated developer binaries belong in `.tools/`, which is
ignored. The project uses Ghidra's built-in 68000 loader; no external Genesis
loader extension is required.
