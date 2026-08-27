# Native asset extraction

The repository now has a portable Python extractor for the known Aladdin
Genesis formats. Noesis remains useful as an independent reference tool, but
is not required by the pipeline and is Windows-only.

Run it against the local ROM with:

```bash
python tools/oa.py assets
python tools/oa.py validate
```

To run only the compression corpus pass:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/rnc_extract.py rom/Disneys_Aladdin_U_p1.bin
```

To re-run classification/contact-sheet generation without decompressing the
ROM again:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/classify.py
```

To scan for ROM pointer tables that reference the decompressed corpus:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/references.py
```

To group the unassigned blocks by contiguous compressed storage and nearby
68000 code references:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/rnc_families_cli.py
```

To recover every direct RNC-to-VDP upload call and its VRAM destination:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/rnc_loaders_cli.py
```

To decompile only the unresolved loader clusters and their surrounding scene
setup code through the local Ghidra project:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/decompile.py \
  --extra-address 0x1B4B28 \
  --extra-address 0x1B4B5E \
  --extra-address 0x1B21F6
```

The report is written to `build/re/rnc_targeted_decompile.json`.  The default
targets are the confirmed loader at `0x1B3416` and loader-containing functions
that still have no dynamic asset evidence.  `--extra-address` adds a known
scene-dispatch or post-load function without broadening the decompile pass.

To correlate those uploads with a captured MAME VRAM/CRAM trace and render
palette candidates:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/rnc_runtime_cli.py --trace build/re/actor-gameplay
```

When a MAME loader breakpoint report is available, merge it as dynamic
evidence:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/rnc_runtime_cli.py \
  --trace build/re/rnc-loader-gameplay \
  --load-trace build/re/rnc-loader-gameplay/rnc_loads.json
```

The same runtime step can be included in the full extraction command with
`--runtime-trace`.

Generated files are written under `build/assets/` and are intentionally
ignored by Git:

```text
build/assets/
├── manifest.json       # ROM identity and extraction summary
├── levels.json         # level table and asset metadata
├── levels/             # raw decompressed data and rendered PNGs
├── sprites.json        # Chopper frame/tile metadata
├── sprites/             # SEG-equivalent tile sets and frame PNGs
│   └── frames.json      # detailed frame/part metadata
├── animations.json      # known animation stream records
├── scene_transitions.json # ROM scene table and compact transition script records
└── rnc/
    ├── manifest.json    # every RNC block, hashes, consumers, and failures
    ├── classification.json # tile candidates and contiguous block families
    ├── classified/       # contact sheets for likely Genesis tile data
    ├── pointer_references.json # 68000 pointer and table candidates
    ├── family_analysis.json # storage families, code clusters, and previews
    ├── loader_analysis.json # RNC-to-VDP call sites and destinations
    ├── scene_resources.json # static state-to-resource map validation
    ├── runtime_analysis.json # optional VRAM/CRAM matches and palette evidence
    ├── runtime/          # optional palette-aware rendered previews
    └── blocks/          # decompressed block data named by ROM offset
```

The level path implements the format behavior documented by the Noesis
Aladdin level-dump tool: the level table, RNC/ProPack method 1 data, Genesis
character tiles, block maps, parallax tiles, and palettes. The sprite path
implements the Aladdin Chopper runtime frame and tile tables.

The ROM level table is also recorded in `re/assets/level_table.yml` as the
runtime-facing scene-state metadata: start/camera fields, map dimensions,
resources, music, and enter/exit callbacks. Validate it against the generated
extract with:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/validate_level_table.py
```

The current native graphics slice uses the player frame records in
`re/assets/player_sprite.yml` and the recovered timing tables in
`re/assets/player_animation.yml`. The minimal native VM covers the observed
idle, run, brake, jump, and landing streams; conditional branches and dynamic
action selection remain follow-up work. The 0x80/0x80 Chopper frame origin is
normalized into pixel offsets by the extractor, and level-01's observed CRAM
palette line 3 is used for the player.

The RNC path scans the entire ROM, decodes every valid `RNC\x01` block, and
records known level-table consumers. Blocks without a known consumer are
preserved in `rnc/blocks/` and listed as unassigned discovery targets.

The output is evidence, not canonical knowledge. Once an address or format is
confirmed, record it under `re/assets/` or the existing `re/symbols/` files;
generated assets remain ignored. Other ROM images should remain uncommitted
unless their redistribution is authorized.

`family_analysis.json` is intentionally a review report. It records the
compressed block sizes, pointer locations, a small local 68000 instruction
window, optional containing function names from `build/re/functions.csv`, and
the existing evidence-palette preview. Its `unknown_graphics` and
`genesis_tile_candidate` labels are hypotheses, not final asset names.

`loader_analysis.json` confirms the semantics of helper `0x1B3416`: `A0` is
an RNC source and `A1` is a VDP VRAM byte address. `runtime_analysis.json`
only promotes a palette to observed evidence when the captured CRAM at the
same VRAM match is non-empty; otherwise the palette remains unknown.

The targeted pass currently shows that the contiguous unresolved stubs at
`0x1B498A` through `0x1B4A52` each select a different RNC source, call the
common loader, and then share post-load routine `0x1B4B28`.  Scene setup at
`0x1B0F66` dispatches these resource paths from the state byte at
`0xFF7E26`, giving the next MAME experiments specific state transitions to
exercise.

That mapping is tracked in `re/assets/scene_resources.yml` and checked against
the static loader report during `extract-assets.py`.  Run the check directly
with:

```bash
PYTHONPATH=tools python tools/openaladdin/assets/validate_scene_resources.py
```

The scene-transition pass is driven by `re/assets/scene_transitions.yml`. It
decodes the five 6-byte records used by `0x001B3B96` at ROM `0x004B04` and the
14 four-byte records around the runtime script cursor at `0x00004082`.
`tools/oa.py assets` writes the generated report to
`build/assets/scene_transitions.json`.
