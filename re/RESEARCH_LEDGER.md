# OpenAladdin reverse-engineering ledger

This file is the index for the reproducible reverse-engineering record. It is
deliberately separate from implementation notes: a claim belongs here only
when a tracked source, a replayable campaign, or a deterministic analysis
artifact supports it.

## Baseline

Captured on 2026-08-26.

| Item | Value |
| --- | --- |
| Repository | `master` |
| ROM | `rom/Disneys_Aladdin_U_p1.bin` |
| ROM SHA-256 | `8199d016f7bb88ea73b635dcc072c126b40f01c662707ed3f67d865fd86c0ab6` |
| MAME submodule | `c04a5be3a8c3761744df29bebedcf5c71e6a71af` |
| SDL submodule | `be51a15ba2079e6449a169e8b8ddb559c798b15d` |
| Ghidra submodule | `8b4c91d4d5bd1549622bfbade0df199585b98365` |
| Runtime build | `cmake --build build -j20` |
| Test status at capture | CTest: 16/16 passing |

The repository history is itself part of the record. Do not rewrite the
reverse-engineering commits or replace the ROM/emulator baseline without
adding a new dated baseline section.

## Recording contract

Every significant experiment should leave all of the following:

1. A tracked campaign or finding manifest under `re/mame/`.
2. The exact ROM hash, emulator revision, and harness commit in that manifest.
3. A replayable input schedule or loaded-state provenance.
4. Named MAME checkpoints for meaningful events, with state files retained in
   the ignored `build/re/` tree.
5. A short conclusion that labels each result as verified, negative evidence,
   or an unresolved hypothesis.
6. A commit containing the manifest/finding and any code or symbol changes.

Large traces and MAME `.sta` files remain ignored build artifacts. The tracked
manifest must point to them and `campaign.py verify` must pass before a
campaign is considered recorded. If a trace is regenerated, retain the same
input and state provenance and record the new output directory rather than
silently overwriting the old evidence.

For direct harness runs, use `tools/openaladdin/mame/run.sh`; it records the
ROM identity in the trace header and supports deterministic frame schedules,
saved-state loading, actor/RAM/VDP capture, and named checkpoints. For native
regressions, keep the corresponding MAME trace and the comparison command in
the finding or commit message.

## Verified evidence index

### Runtime and emulator

- `49f95cf` made direct MAME traces self-identify with the ROM hash.
- Relative saved-state paths are normalized by `run.sh`; the load-state smoke
  test is recorded in the platform-chain investigation.
- The normal trace path is headless, with Xvfb available through `MAME_XVFB=1`.

### Gameplay and actor behavior

- `d96db08`: fresh-boot level-01 baseline.
- `6c84fde`: fresh natural route progression through the opening route,
  including the upper ledge, lower chamber, wall, and handhold probes.
- `fb5100d`: bounce/handhold timing family; tested timings did not produce a
  transfer to the upper route.
- `183a082`: exact player-attack differential checkpoint; native and MAME
  state traces match for 47 frames from the synchronized checkpoint.
- `477f5c1`: apparent pre-wall platform chain classified as guard-sword child
  (`0x2D`) and oscillating interaction actor (`0x34`), not a traversable
  moving-platform transfer.
- `20260826-level01-object-trace-frontier-v1`: upper-platform actor
  initialization and animation trace. It records `0x7F` as a transient
  spawn/clear effect and `0x2A` as a falling/resetting movement-stream actor;
  the `0x1E` player-proximity actor remains unresolved.
- `20260826-level01-recording-high-walkway-v1`: current-baseline replay of the
  retained high-platform route. The `0x1D` guard is sword-cleared and the
  upper walkway is reached, but its right edge falls back to the lower band;
  the current replay has a small coordinate drift from the older trace.
- `20260826-level01-recording-frontier-v1`: current-harness replay of the
  lower-gap → handhold → upper-platform → wall-rope chain, with 22 verified
  named checkpoints. The associated frontier matrix records the direct wall,
  upper-rope, upper-platform, and spring negative branches.
- `20260826-level01-fresh-route-canonical-v2`: fresh level-entry replay with
  debugger synchronization disabled, four segments and 27 verified named
  checkpoints through the handhold, upper rope, upper-left ledge, and guard.
  This is the current canonical route frontier; it remains in scene state
  `0x01` and does not yet prove the transfer to the retained high walkway.
- `20260826-level01-fresh-route-high-transfer-v1`: fresh handhold replay that
  reaches the earlier `0x22` rope at X≈1560, transfers to the Y=530 high
  walkway, clears the upper guard, and records the right-edge fall. This
  converts the retained high-walkway state into a verified natural-route
  continuation.
- `20260826-level01-recording-fresh-v2`: clean power-on replay of the same
  route in one continuous no-sync segment. It records 16 named checkpoints
  from power-on through the upper-edge frontier, with the ROM and harness
  provenance captured at the current commit.
- `20260826-level01-canonical-recording-v3`: current-state clean replay and
  loaded-state continuation. It records the power-on route, lower-tower
  reversal/crossing, a guarded far-floor branch, and the positive far vertical
  rope approach/climb at world X≈4728. The route remains in scene state `0x01`.
- `re/mame/findings/20260826-level01-canonical-recording-v3.json`: separates
  verified positive route evidence from guard/reset and horizontal-line
  negative branches and records the next upper-transfer frontier.
- `20260826-level01-tower-negative-v1`: loaded-state recording from the clean
  upper-edge checkpoint through the lower tower. It verifies the 0x47 surface
  mode/latch behavior, identifies the resident 0x43 lamp and 0x20 enemy, and
  records the tower as a terminal lower/death-band branch rather than a route
  connector.
- `20260826-level01-tower-support-exploration-v1`: phase-1 controlled-frontier
  recording layered on the clean route. It verifies the lower support contour,
  bounds the empty lower-to-rope gap, re-records the known 0x22 rope climb with
  checkpoints, and preserves the natural edge timing as negative evidence.

### Terrain/connector decompilation

- `re/mame/findings/level01-tower-frontier-decomp-v1.json` records the corrected
  Ghidra target mapping. Behavior 0x47 dispatches to `0x001B5470`, where it
  toggles `TERRAIN_SURFACE_MODE` and arms `TERRAIN_SURFACE_LATCH`; behavior
  0x22/0x23 dispatches to `0x001B54D8`, which raises `TERRAIN_QUERY_STATE_A`.
  Neither handler itself creates a missing actor or scene transition.

### Scene and exit work

- `re/mame/findings/level-transition-findings.json`: controlled scene-state
  transition proof and table/exit-gate work.
- `re/mame/findings/natural-exit-findings.json`: controller-only natural route
  currently reaches approximately world `x=2176`, `y=912` and does not reach
  the exit; the controlled boundary write path is known.
- `re/symbols/functions.yml`: level-01 enter/exit routines at `0x1B5B4A` and
  `0x1B6406`.

## Current frontier

The next search is not a new project bootstrap. It is a targeted level-01
geometry/object investigation. The extracted level package is available at
`build/assets/levels/level01/` and is derived from the ROM table entry at
`0x2CBA`:

- map: 300 x 45 terrain words, ROM source `0x195176`;
- floor/behavior data: ROM source `0x1434C4`;
- background block table: ROM source `0x145B2C`;
- enter routine: `0x1B5B4A`;
- exit routine: `0x1B6406`.

The immediate question is now narrower: what natural interaction or terrain
response transfers the verified far vertical rope around world `x=4728`,
`y=671` to the upper platform/exit corridor above `y=470`? The controlled
support route is not being treated as proof of the intended route. The next
campaign must start from the current `rope-top-frontier` checkpoint, trace the
handhold/line transfer and nearby actors, and save a checkpoint for every
launch, wall transfer, rope attachment, dismount, reset, or scene gate.

## Campaign index

| Campaign | Status | Purpose |
| --- | --- | --- |
| `20260825-level01-canonical-v1` | in progress | original canonical route record |
| `20260826-level01-fresh-baseline-v1` | recorded | fresh boot baseline |
| `20260826-level01-recording-baseline-v1` | recorded | fresh boot baseline at the ledger commit |
| `20260826-level01-recording-frontier-v1` | recorded-frontier | current-harness route chain and upper-route frontier |
| `20260826-level01-fresh-route-probe-v1` | recorded | natural route and handhold probes |
| `20260826-level01-bounce-up-sweep-v1` | recorded | bounce timing sweep |
| `20260826-level01-route-expansion-v1` | in progress | route expansion and unresolved exit search |
| `20260826-level01-transition-v1` | controlled proof | scene-transition path |
| `20260826-level01-fresh-route-canonical-v2` | recorded-frontier | fresh no-sync route record through the upper-left guard |
| `20260826-level01-fresh-route-high-transfer-v1` | recorded-frontier | fresh handhold-to-high-walkway transfer and upper guard |
| `20260826-level01-recording-fresh-v2` | recorded-frontier | clean power-on replay through the upper-edge frontier |
| `20260826-level01-tower-negative-v1` | recorded-negative | lower-tower surface/actor branch from the clean upper-edge checkpoint |
| `20260826-level01-tower-support-exploration-v1` | recorded-frontier | controlled lower support chain, rope gap, rope fixture, and natural negative |
| `20260826-level01-canonical-recording-v3` | recorded-frontier | current power-on record through lower-tower crossing and far vertical-rope frontier |

When a campaign is superseded, leave it in this table. A negative result is
valuable because it prevents repeating the same input family.
