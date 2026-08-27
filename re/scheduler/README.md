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

The first committed dynamic validation is recorded in
`re/mame/campaigns/20260827-scheduler-static-dynamic-v1.json`. Its focused
trace observes the exact 37-call sequence in 209 complete gameplay passes and
keeps `FRAME_WAIT_LATCH` unresolved because no gameplay writer was observed.

Reanalyze an existing capture with:

```sh
python3 tools/openaladdin/mame/analyze_scheduler_trace.py \
  build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/debug.log \
  --trace-boot build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/trace_boot.jsonl \
  --state build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/state.jsonl \
  --output build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/scheduler-analysis.json
```
