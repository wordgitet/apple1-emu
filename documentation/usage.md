# Usage

The emulator runs as a **terminal application**. It renders a 40×24 character
Apple-1 display in the terminal (unless `-H` headless mode is used) and accepts
keyboard input through the PIA.

```bash
./apple1 [options] [flat_binary]
./apple1 [-H] [-g] config.conf
```

Run `./apple1 -h` for a summary.

**Two modes:**

- **Switch mode** — no `.conf` file; use command-line options below.
- **Config mode** — pass a `.conf` file as a positional argument. Only `-H`
  and `-g` may appear on the command line with it (not in the file).

---

## Command-line options (switch mode)

| Option | Description |
|--------|-------------|
| `-r <rom>` | Path to 256-byte Woz Monitor ROM (default: embedded ROM if compiled in) |
| `-m <kb>` | RAM size in kilobytes (4–64, default 8). Cannot exceed `APPLE1_STATIC_RAM_SIZE`. |
| `-l <file>@<hex>` | Load binary at hex address (e.g. `-l program.bin@0300`) |
| `-w <txt>` | Load Woz Monitor hex text file |
| `-a <rom>` | ACI cassette interface 256-byte ROM |
| `-e <wav>` | Load WAV file for ACI tape playback |
| `-E <wav>` | Save recorded ACI tape to WAV on exit |
| `-k <rom>` | Krusader assembler ROM (1–4096 bytes) |
| `-B <baud>` | Terminal baud-rate limit (e.g. `-B 1200`; 0 = no limit) |
| `-c` | Cap CPU speed to authentic ~1.023 MHz |
| `-F` | Flat bus: map `$0000–$FFFF` as linear RAM (requires 64 KB static RAM) |
| `-p` | Disable PIA I/O timing throttle |
| `-d` | Emulate DRAM refresh cycle stealing |
| `-b` | Emulate keyboard bounce/debounce |
| `-s` | Disable cold-boot RAM randomisation (zero-fill instead) |
| `-H` | Headless: no terminal rendering or keyboard (CPU still runs) |
| `-g` | Enable debugger (pauses at startup with `db>` prompt) |
| `-t` | Print each executed instruction (disassembly to terminal) |
| `-h` | Show help |

### Positional argument

If a **flat binary** path is given after all options, it is loaded at `$0000` and
**flat bus mode is enabled** automatically (same as `-F`).

Example — Klaus Dormann functional test:

```bash
./apple1 -H -F 6502_functional_test.bin
```

---

## Config file format

See **`apple1.conf.example`** in the repository root for a fully commented
template listing every key, its default when omitted, and copy-paste examples.

Config files use the `.conf` suffix and **`key = value`** lines (ASCII only,
portable across all platforms). Lines starting with `#` are comments. Boolean
settings accept `yes`/`no`, `true`/`false`, `on`/`off`, or `1`/`0`.

**Every key is optional.** Comment out or delete any line you do not need; the
emulator uses the same defaults as switch mode (see `apple1.conf.example`). A
file that contains only comments, or is empty, is valid.

**Invalid config:** parsing stops on the first bad line. The emulator prints
`Error: config '<path>': <reason>` (for example `line 5: unknown key 'foo'`,
`ram_kb must be 4-64`, or `'headless' is CLI-only; use -H or -g'`) and exits
with code **1**. No settings from that file are applied after an error. If the
file cannot be opened, a warning is printed and the emulator also exits with
code **1**.

Example `apple1.conf`:

```
# Woz Monitor and 16 KB RAM
rom = wozmon.bin
ram_kb = 16

# Load a program at $0300
load = myprog.bin @ 0300

# Authentic ~1.023 MHz cap
speed_cap = yes

# Optional ACI
aci_rom = aci_rom.bin
tape_in = tape.wav
tape_out = recorded.wav
```

| Key | Description |
|-----|-------------|
| `rom` | Woz Monitor ROM path |
| `ram_kb` | RAM size 4–64 |
| `load` | Binary load: `file @ hexaddr` |
| `wozmon_txt` | Woz Monitor hex text dump |
| `flat_bin` | Flat binary at `$0000` (enables flat bus) |
| `aci_rom` | ACI card ROM |
| `tape_in` | ACI playback WAV |
| `tape_out` | ACI record WAV on exit |
| `krusader` | Krusader ROM |
| `baud` | Terminal baud limit |
| `speed_cap` | Cap CPU to authentic speed |
| `flat_bus` | Linear 64 KB map |
| `throttle_pia` | PIA I/O timing |
| `dram_refresh` | DRAM refresh steal |
| `keyboard_bounce` | Keyboard bounce |
| `randomize_ram` | Randomise cold-boot RAM |
| `trace` | Instruction trace |

**Not valid in config files:** `headless`, `debugger` — use `-H` and `-g` on
the command line only.

```bash
./apple1 apple1.conf
./apple1 -H apple1.conf -g
```

---

## Keyboard (terminal mode)

When the terminal UI is active (`term_ansi.c` on Unix, `term_dos.c` on MS-DOS):

| Key | Action |
|-----|--------|
| Normal keys | Sent to Apple-1 keyboard (uppercase; strobe via PIA) |
| **Ctrl+C** | Quit (or debug break if `-g` debugger enabled) |
| **Ctrl+L** | Clear emulator screen buffer |
| **Ctrl+R** | Reset Apple-1 (CPU + bus) |

On POSIX hosts, **Ctrl+C** also triggers the signal handler (`port_signal_quit`).
On MS-DOS, only the terminal poll path applies.

---

## Common examples

**Basic session with external Woz ROM:**

```bash
./apple1 -r wozmon.bin
```

**Load program and run capped at authentic speed:**

```bash
./apple1 -c -r wozmon.bin -l wozmon.bin@0300
```

**Debugger from reset:**

```bash
./apple1 -g -r wozmon.bin
```

**ACI tape load:**

```bash
./apple1 -r wozmon.bin -a aci_rom.bin -e recording.wav
```

**Headless flat-bus test ROM:**

```bash
./apple1 -H -F test.bin
```

**MS-DOS (DOSBox):**

```dos
APPLE1 -R WOZMON.BIN -C
```

---

## Display behaviour

- The UI is a **40×24** character matrix with a blinking `@` cursor (authentic
  Apple-1 video pattern).
- On Unix/macOS, **`term_ansi.c`** uses ANSI escape sequences (clear screen, cursor
  home). Use a terminal that supports ANSI (Terminal.app, iTerm2, xterm, …).
- On MS-DOS, **`term_dos.c`** uses conio (`gotoxy` / `putch`) — no ANSI.SYS needed.
- With **`-B baud`**, display output is rate-limited to simulate serial terminal
  speed; Woz Monitor’s DSP busy polling works non-blockingly.
- **`-H`** skips all of the above — useful for automated tests and benchmarks.

---

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Clean exit (user quit, test passed, or normal completion) |
| 1 | Error (bad options, load failure, functional test fail, …) |

The debugger **`q`** command calls `port_exit(0)`.

---

## Logging

Bus messages (ROM load errors, card registration, …) route through an optional
log callback. The CLI registers `stderr_log`, which prints to the terminal via
`port_term_write_buf` unless `APPLE1_OMIT_STDIO` is defined at compile time.
