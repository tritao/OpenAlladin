# Actor snapshots

`level01.tsv` is a small, reviewable actor seed exported from a synchronized
MAME state trace. It provides the native vertical slice with stable actor
records while actor spawning, movement streams, and lifetime management are
still being recovered.

Regenerate it from another captured state with:

```bash
python3 tools/openaladdin/analysis/export_actor_records.py \
  --trace build/re/actor-flags-final/state.jsonl \
  --frame 1436 \
  --output re/actors/level01.tsv
```

The columns are `slot type x y movement_pc frame_ptr animation_pc flags`.
The native runtime loads this file by default; use `--actor-records FILE` to
select a different snapshot explicitly.

The focused guard collision fixture is `guard-collision.tsv`. It contains the
confirmed level-01 type-`0x0A` record at world `(0x0530, 0x0340)` and is used by
`tests/native_actor_collision.py` to exercise the `0x0A -> 0x84` terminal path.

For a frame-accurate replay slice, export a range from the same state trace:

```bash
python3 tools/openaladdin/analysis/export_actor_timeline.py \
  --trace build/re/actor-flags-final/state.jsonl \
  --start-frame 1436 \
  --end-frame 1450 \
  --output re/actors/level01-interaction.timeline.tsv
```

Run it with `--actor-timeline FILE`. Timeline frames are rebased to zero and
replace the actor table at each captured frame, including inactive slots.
