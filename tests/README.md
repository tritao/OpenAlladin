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
```

Generated traces and extracted assets remain under `build/`.
`native_actor_movement.py` compares the native signed-delta actor movement VM
against the captured slot-19 type-`0x84` stream, including its `0x84` timer
command handoff.
