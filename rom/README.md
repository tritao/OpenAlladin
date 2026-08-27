# ROM input

Place a legally obtained ROM dump here, or pass an existing path to
`genie verify` or another Genie command.

The configured canonical dump is `rom/Disneys_Aladdin_U_p1.bin`.

Check an image before importing it:

```bash
python -m genie verify
```

The verifier records size, CRC32, SHA-1, and SHA-256. The configured local
dump is intentionally marked as a local, unverified identity; replace the
entry in `re/config/roms.yml` when a different dump becomes canonical.
