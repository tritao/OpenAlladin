# Z80 sound driver

The 68000 sound initialization routine at `0x001E573A` copies the driver
image described in [z80-driver.yml](z80-driver.yml) from ROM into Z80 RAM.
The copy is `0x001B8480-0x001B9D05` inclusive; the following byte begins the
separate ROM-resident audio tables.
Regenerate the extracted image and machine-readable map with:

```sh
python3 -m genie audio-driver
```

The driver consumes the shared 64-byte queue at Z80 `$1B40`, using cursor bytes
`$36` and `$37`. Its command dispatcher is at Z80 `$0945`. Command `$10`
selects a sound ID from the 16-bit little-endian table at ROM `$1BAF6F`, copies
a 33-byte window, and initializes up to sixteen channel records at `$1B80`
with 24-bit ROM stream pointers. The logical header begins with a track-count
byte followed by that many little-endian track offsets relative to `$1BAF6F`;
the logical records are packed as `1 + 2 * track_count` bytes even though the
Z80 copies the fixed 33-byte window.

The stream interpreter is at Z80 `$04BC` and reads through `$03EF`. Its byte
classes are notes `$00–$5F`, control opcodes `$60–$7F`, and two six-bit-group
signed operand encodings `$80–$BF` and `$C0–$FF`, stored in separate channel
state fields. Each channel maintains a 16-byte cached ROM window while its
cursor advances through the 24-bit stream.

The currently confirmed Level 01 command IDs are music `0x49`, animation F3
effects `0x4C`, and the fixed interaction event `0x31`. The native runtime
can audition any sequence-table entry with `--sound-id ID`; IDs are valid from
`0x00` through `0x71`.

The fourth pointer sent by the 68K audio initializer is ROM `$1C73CB`. Z80
`$1336` selects one of thirty 12-byte sample descriptors. Their relative
offsets and lengths form one contiguous waveform payload at `$1C7533-$1E56BE`;
`$1E56C0` is the next 68K audio service entry point.

Regenerate the map, including the decoded music/SFX table, with:

```sh
python3 -m genie audio-driver
```

To capture the live pointer/header/channel state while a title-menu trace is
running, pair the existing audio trace with the gated driver dump:

```sh
OPENALADDIN_TRACE_AUDIO_DRIVER=1 python3 -m genie trace title-menu \
  --audio --audio-mailbox --audio-commands \
  --trace-dir build/re/traces/audio-driver-state
```

This writes `z80_driver_state.jsonl` beside the normal trace files. The dump is
disabled unless the environment variable is explicitly set.

Native runs can produce the counterpart trace with:

```sh
SDL_AUDIODRIVER=dummy ./build/openaladdin --no-window --frames 360 \
  --audio-trace build/re/traces/audio-native.jsonl
```

Use `python -m genie audio-parity` to compare normalized Z80 bus writes and
decoded command IDs. The native trace also includes decoded driver events for
investigating stream timing and channel allocation.
