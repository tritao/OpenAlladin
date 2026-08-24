# ROM input

Place a legally obtained ROM dump here, or pass an existing path to
`tools/import-rom.py`.

The repository contains the publisher-authorized canonical dump at the
root-level `Disneys_Aladdin_U_p1.bin` path used by the analysis tools.

Check an image before importing it:

```bash
python tools/verify-rom.py Disneys_Aladdin_U_p1.bin
```

The verifier records size, CRC32, SHA-1, and SHA-256. The configured local
dump is intentionally marked as a local, unverified identity; replace the
entry in `re/config/roms.yml` when a different dump becomes canonical.
