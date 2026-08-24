# Native asset extraction

The repository now has a portable Python extractor for the known Aladdin
Genesis formats. Noesis remains useful as an independent reference tool, but
is not required by the pipeline and is Windows-only.

Run it against the local ROM with:

```bash
python tools/extract-assets.py Disneys_Aladdin_U_p1.bin
python tools/validate-assets.py
```

To run only the compression corpus pass:

```bash
python tools/extract-rnc.py Disneys_Aladdin_U_p1.bin
```

To re-run classification/contact-sheet generation without decompressing the
ROM again:

```bash
python tools/classify-rnc-assets.py
```

To scan for ROM pointer tables that reference the decompressed corpus:

```bash
python tools/find-rnc-references.py
```

To group the unassigned blocks by contiguous compressed storage and nearby
68000 code references:

```bash
python tools/analyze-rnc-families.py
```

To recover every direct RNC-to-VDP upload call and its VRAM destination:

```bash
python tools/analyze-rnc-loaders.py
```

To correlate those uploads with a captured MAME VRAM/CRAM trace and render
palette candidates:

```bash
python tools/analyze-rnc-runtime.py --trace build/re/actor-gameplay
```

When a MAME loader breakpoint report is available, merge it as dynamic
evidence:

```bash
python tools/analyze-rnc-runtime.py \
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
└── rnc/
    ├── manifest.json    # every RNC block, hashes, consumers, and failures
    ├── classification.json # tile candidates and contiguous block families
    ├── classified/       # contact sheets for likely Genesis tile data
    ├── pointer_references.json # 68000 pointer and table candidates
    ├── family_analysis.json # storage families, code clusters, and previews
    ├── loader_analysis.json # RNC-to-VDP call sites and destinations
    ├── runtime_analysis.json # optional VRAM/CRAM matches and palette evidence
    ├── runtime/          # optional palette-aware rendered previews
    └── blocks/          # decompressed block data named by ROM offset
```

The level path implements the format behavior documented by the Noesis
Aladdin level-dump tool: the level table, RNC/ProPack method 1 data, Genesis
character tiles, block maps, parallax tiles, and palettes. The sprite path
implements the Aladdin Chopper runtime frame and tile tables.

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
