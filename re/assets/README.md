# Native asset extraction

The repository now has a portable Python extractor for the known Aladdin
Genesis formats. Noesis remains useful as an independent reference tool, but
is not required by the pipeline and is Windows-only.

Run it against the local ROM with:

```bash
python tools/extract-assets.py Disneys_Aladdin_U_p1.bin
python tools/validate-assets.py
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
├── animations.json     # known animation stream records
└── inventory           # RNC blocks are listed in manifest.json
```

The level path implements the format behavior documented by the Noesis
Aladdin level-dump tool: the level table, RNC/ProPack method 1 data, Genesis
character tiles, block maps, parallax tiles, and palettes. The sprite path
implements the Aladdin Chopper runtime frame and tile tables.

The output is evidence, not canonical knowledge. Once an address or format is
confirmed, record it under `re/assets/` or the existing `re/symbols/` files;
do not commit the ROM or generated copyrighted assets.
