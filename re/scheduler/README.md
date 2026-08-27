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
trace observes the exact 37-call sequence in 209 complete gameplay passes.
The follow-up lifecycle audit is recorded in
`re/mame/campaigns/20260827-frame-wait-lifecycle-v1.json`: it resolves the
static writer inventory for `FRAME_WAIT_LATCH`, observes its reset/level-entry
initialization, and records that the natural Level 01 route has no gameplay
writer. The transition writers are statically identified but were not reached
by the restart attempt from the available checkpoint.

The important semantic distinction is that `FRAME_WAIT_LATCH` is only the
conditional gate for the Z80 bus handshake in `0x001B249E`. The helper's
actual release condition is `VBLANK_READY_LATCH` at `0x00FF7E1E`, written by
`VBlankInterrupt`.

The transition/resource follow-up is recorded in
`re/mame/campaigns/20260827-level01-transition-resource-lifecycle-v1.json`.
Its upper-frontier replay reaches the Level 01 boundary and validates the
`SceneScript_AdvanceState` resource prelude, including 300
`0x001B28AE`/`0x001B249E` VBlank-paced service iterations. The nested
`0x001B16E0` lifecycle then remains active for the capture, so the script
cursor/state branch and `Scene_EnterTransitionMode` are not reached. This is
restartability/resource-context evidence, not a natural-route failure.

The paired prerequisite probe is recorded in
`re/mame/campaigns/20260827-level01-transition-resource-prerequisite-v1.json`.
It confirms that the checkpoint's nonzero `0x00FFF003` interaction counter is
the immediate gate keeping `0x001B16E0` active: a controlled zero at that
entry exposes the `SceneScript_AdvanceState` parser on the next debugger
frame, while the unmodified baseline does not. The controlled write is causal
evidence only and is not a native scheduler phase or a natural transition.

The controlled parser/state-3 boundary is recorded in
`re/mame/campaigns/20260827-level01-transition-state3-dispatch-v1.json`.
After the same controlled counter clear, the parser consumes the frontier
record, advances `0x00FFF572` from `0x4082` to `0x408A`, and writes
`SCENE_STATE=0x03`. The following `SceneResource_Dispatch` entry selects its
state-3 branch and resets the resource error/status context, but the replay
does not reach `0x001B2ACE` or `0x001B315C`. This separates parser/state-3
dispatch from transition completion; it remains controlled evidence rather
than a natural transition or native scheduler phase.

The state-3 resource continuation is recorded in
`re/mame/campaigns/20260827-level01-transition-state3-resource-v1.json`.
Two focused no-input replays from the earlier controlled `scene3-active`
checkpoint observe the state-3 dispatcher, its resource status/error reset,
and the state-3 resource-helper boundary. Neither replay reaches the
transition-mode writers at `0x001B2DF4`/`0x001B2E02`, scene-table selection, or
script completion; both remain at state `0x03`, cursor `0x408A`, table index
`0`. This is resource-path boundary evidence only, not natural progression.

Reanalyze an existing capture with:

```sh
python3 tools/openaladdin/mame/analyze_scheduler_trace.py \
  build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/debug.log \
  --trace-boot build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/trace_boot.jsonl \
  --state build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/state.jsonl \
  --output build/re/campaigns/20260827-scheduler-static-dynamic-v1/player-run/scheduler-analysis.json
```
