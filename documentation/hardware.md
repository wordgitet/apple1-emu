# Hardware Model

Cycle-oriented model of Steve Wozniak's 1976 Apple-1. The CPU is a MOS 6502 (NMOS)
with undocumented opcodes emulated for test-suite compatibility.

---

## Memory map (standard Apple-1 layout)

When **flat bus mode** (`-F`) is off, the bus uses the classic Apple-1 decode.
RAM size is configurable (`-m`, default 8 KB) with optional **split mapping** for
8 KB: `$0000–$0FFF` and `$E000–$EFFF` map to physical RAM banks.

| Range | Device |
|-------|--------|
| `$0000–$00FF` | Zero page |
| `$0100–$01FF` | Stack |
| `$0200–$02FF` | Woz Monitor input buffer / workspace |
| `$0300–` | User RAM (extends per configured size) |
| `$C000–$C0FF` | ACI cassette interface registers (if card installed) |
| `$C100–$C1FF` | ACI ROM (256 bytes on card) |
| `$D010–$D013` | Motorola 6820 PIA (keyboard + display) |
| `$D000–$DFFF` | PIA mirrors (6821-style partial address decode) |
| `$E000–$EFFF` | Second RAM bank (8 KB config) or expansion ROM area |
| `$F000–$FDFF` | Expansion ROM (e.g. Krusader at `$E000` mask) |
| `$FF00–$FFFF` | Woz Monitor ROM (256 bytes) |

### PIA registers (`$D010–$D013`)

| Address | Register | Role |
|---------|----------|------|
| `$D010` | KBD data | Keyboard character read |
| `$D011` | KBD control | Bit 7 = keyboard strobe |
| `$D012` | DSP data | Display character write |
| `$D013` | DSP control | Bit 7 = display ready (cleared while “busy”) |

The emulator models PIA **mirroring**: only address lines A0–A1 reach the chip; upper
bits are don't-care (see `test_dualram` / PIA tests).

### Flat bus mode (`-F`)

All addresses `$0000–$FFFF` map linearly into 64 KB RAM. Used for:

- Klaus Dormann / Tom Harte–style test ROMs
- Position flat binary: `./apple1 -H mytest.bin`

Requires **`APPLE1_STATIC_RAM_SIZE=65536`** at compile time.

---

## Clock and timing

| Setting | Behaviour |
|---------|-----------|
| Default | **Uncapped** — runs as fast as the host CPU allows |
| `-c` | Cap at **1.022727 MHz** (authentic Apple-1 crystal) |
| `-d` | DRAM refresh cycle stealing |
| `-p` off | Disable microsecond PIA access delay when uncapped |
| `-B baud` | Inter-character display delay (DSP busy bit timing) |

---

## Expansion cards

Cards are registered on the **system bus** with a name, address decode (`base` +
`mask`), and read/write/tick callbacks.

| Card | CLI | Address | Notes |
|------|-----|---------|-------|
| **ACI** | `-a` `-e` `-E` | `$C000` region | Cassette interface; WAV tape load/save |
| **Krusader** | `-k` | `$E000` (4 KB) | Assembler ROM; read-only |

Maximum cards: **`APPLE1_MAX_CARDS`** (default 8).

---

## ROM sources

| ROM | Source |
|-----|--------|
| Woz Monitor | `-r file` or embedded default (`embedded_roms.h`) unless `APPLE1_OMIT_WOZMON` |
| ACI firmware | `-a file` (256 bytes) |
| Krusader | `-k file` (1–4096 bytes) |
| User program | `-l bin@addr`, `-w txt`, or positional flat binary |

---

## Terminal video

The Apple-1 did not use ANSI terminals. This emulator **maps** the original video
hardware onto a modern terminal:

- 40 columns × 24 rows
- `@` blinking cursor at the current position
- Uppercase glyphs only (lowercase input is converted)

Authentic phosphor/charmap ROM behaviour is optional (`APPLE1_OMIT_CHARMAP` removes
embedded charmap data from the build).

---

## Reset

- **Power-on**: CPU fetches reset vector from `$FFFC–$FFFD` (Woz Monitor ROM).
- **Ctrl+R** (terminal): software reset via emulator (bus + CPU reset).
- **Cold boot RAM**: randomised by default (`-s` to zero-fill instead).

---

## References

- Original Apple-1 operation manual (PDF not bundled here)
- Woz Monitor prompt: `\` at `$FF00`

For software interface details see [usage.md](usage.md) and [debugger.md](debugger.md).
