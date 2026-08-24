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
do not commit the ROM or generated copyrighted assets.
