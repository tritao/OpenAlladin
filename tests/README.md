# Tests

Tests are organized by the code they exercise:

```text
tests/unit/        core and native unit tests
tests/assets/      asset-format regression tests
tests/regression/  Genesis/reference behavior tests
```

The trace-backed actor replay check is run with:

```bash
python3 tests/native_actor_timeline.py
python3 tests/native_actor_collision.py
python3 tests/native_actor_actor_collision.py
```

Generated traces and extracted assets remain under `build/`.
`native_actor_movement.py` compares the native signed-delta actor movement VM
against the captured slot-19 type-`0x84` stream, including its `0x84` timer
command handoff.

## Visual audit

The native renderer can emit an unscaled 320x224 framebuffer for comparison
with a MAME snapshot:

```bash
./build/openaladdin --no-window --frames 1 \
  --framebuffer-out build/re/visual-audit/native.ppm
python3 tools/openaladdin/analysis/visual_diff.py \
  build/re/traces/snapshots/gameplay.png \
  build/re/visual-audit/native.ppm \
  --overlay build/re/visual-audit/diff.ppm \
  --json build/re/visual-audit/diff.json
```

Use `--framebuffer-frame N` when the native run is replaying a specific
checkpoint. The comparator accepts MAME RGB/RGBA PNGs and native P6 PPMs,
reports the differing-pixel bounding box, and returns failure for any
difference unless a tolerance is supplied.

Capture the ROM-side reference with the MAME harness at the matching
checkpoint. For example:

```bash
OPENALADDIN_TRACE_DIR=build/re/visual-audit/mame \
OPENALADDIN_CAPTURE=full OPENALADDIN_TRACE_FRAMES=1400 \
OPENALADDIN_SNAPSHOT_FRAME=1300 \
OPENALADDIN_SNAPSHOT_NAME=gameplay.png \
MAME_XVFB=1 OPENALADDIN_MAME_HEADLESS=0 \
./tools/openaladdin/mame/run.sh
```

Use the MAME `state.jsonl` record at that frame to supply the native
`--checkpoint-player`, `--checkpoint-frame-ptr`, and `--checkpoint-camera`
arguments. Compare the unscaled framebuffer, and use `--region` to isolate
the player sprite while background/camera work is still being brought into
parity.

The checkpoint extraction and native replay can be automated with:

```bash
PYTHONPATH=tools python3 tools/openaladdin/analysis/audit_visual.py \
  --trace-dir build/re/visual-audit/mame \
  --frame 1300 \
  --reference build/re/visual-audit/mame/snapshots/gameplay.png \
  --player-region \
  --report-only
```

`--player-region` derives the crop from the MAME player's world position,
camera origin, frame pointer, and the native multipart-frame manifest. The
report directory contains the full-frame `native.ppm`/`diff.ppm` pair plus
`player-reference.ppm`, `player-native.ppm`, and `player-diff.ppm` for the
focused comparison. It also derives an opaque-pixel mask from the captured
frame pointer's PNG and reports `player sprite audit`; strict mode validates
that mask, so background differences do not hide sprite parity. The masked
overlay is written as `player-sprite-diff.ppm`. Use `--player-padding N` to
enlarge the contextual crop.

Remove `--report-only` once a checkpoint is expected to match exactly.
