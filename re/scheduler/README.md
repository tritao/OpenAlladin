# Scheduler reconstruction

`frame_phases.yml` is the canonical static model of the ROM scheduler. It
records the direct call sequence recovered from `Game_FrameUpdateLoop` at
`0x001A8C16`, the separate `VBlankInterrupt` entry at `0x001B246E`, the
relevant RAM gates, actor ranges, and the currently unresolved native-phase
mapping.

The model intentionally keeps unknown `FUN_*` routines and uncertain entry
labels visible. A native phase is not promoted to a ROM phase just because a
parity trace contains a similarly timed transition.

Regenerate the disposable targeted decompile with:

```sh
./.tools/ghidra-12.1.3/support/pyghidraRun \
  -H re/ghidra/project aladdin -process -readOnly \
  -scriptPath re/ghidra/scripts \
  -postScript ExportTargetedDecompile.py \
  re/ghidra/targets/scheduler-targets.json \
  build/re/scheduler-targeted-decompile.json
```

The generated JSON is evidence for the YAML model and remains under
`build/`. Dynamic MAME campaigns and native causal parity can promote a row
from `decompiled` to `trace_validated` and `parity_validated`; they must carry
their own committed provenance.
