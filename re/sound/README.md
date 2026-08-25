# Z80 sound driver

The 68000 sound initialization routine at `0x001E573A` copies the driver
image described in [z80-driver.yml](z80-driver.yml) from ROM into Z80 RAM.
Regenerate the extracted image and machine-readable map with:

```sh
python3 tools/oa.py audio-driver
```

The driver consumes the shared 64-byte queue at Z80 `$1B40`, using cursor bytes
`$36` and `$37`. Its command dispatcher is at Z80 `$0945`; the tracked handler
addresses are the next target for recovering music and sound-effect data
formats.
