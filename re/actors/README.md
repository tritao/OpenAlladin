# Actor snapshots and interaction spawning

`level01.tsv` is a small, reviewable actor seed exported from a synchronized
MAME state trace. It remains available as a compatibility fixture, but the
native Level 01 runtime now starts without it and builds actors from the
decoded interaction map in `floor.bin`.

Regenerate it from another captured state with:

```bash
python3 tools/openaladdin/analysis/export_actor_records.py \
  --trace build/re/actor-flags-final/state.jsonl \
  --frame 1436 \
  --output re/actors/level01.tsv
```

The columns are `slot type x y movement_pc collision_frame_ptr animation_pc flags`,
with optional `facing_x_flip`, `facing_y_flip`, `movement_command_timer`,
`movement_loop_pc`, `movement_loop_timer`, and `movement_return_pc` columns for
movement VM fixtures.
The native runtime loads no snapshot by default. Use `--actor-records FILE` to
select this or another snapshot explicitly for a replay fixture.

The live interaction table is `floor.bin[3 + (map_word >> 1)]`. Level 01 has
175 records and 22 selectors. Camera refill edges dispatch selectors through
the recovered ROM handler templates, allocate the matching actor slot pool,
initialize the actor from its compact ROM template, and consume the runtime
selector. Interaction-created actors are then subject to the native actor VM
and camera culling/cleanup path.

The focused guard collision fixture is `guard-collision.tsv`. It contains the
confirmed level-01 type-`0x0A` record at world `(0x0530, 0x0340)` and its live
collision-frame pointer `0x001F6500`. The four box bytes at `+2..+5` are read
from the fixed ROM by the native collision pass. The fixture is used by
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

The focused movement fixture `actor-movement.tsv` starts slot 19 at the
captured `0x00120360` stream. `tests/native_actor_movement.py` compares its
signed position deltas, movement cursor, and timer-command handoff against
MAME frames 361..381.
