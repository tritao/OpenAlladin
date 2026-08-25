# Replayable MAME campaigns

Campaign manifests are tracked; their large trace and `.sta` artifacts live
under ignored `build/re/campaigns/` directories. A campaign records the ROM
hash, MAME submodule commit, harness commit, input schedule, loaded-state
provenance, and named checkpoints.

Verify a campaign after reproducing or restoring its artifacts:

```sh
python tools/openaladdin/mame/campaign.py verify \
  re/mame/campaigns/20260825-level01-canonical-v1.json
```

Do not delete failed segments. They are part of the route evidence and keep
future searches from repeating already-tested input families.
