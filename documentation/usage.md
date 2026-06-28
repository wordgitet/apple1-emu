# Usage

The emulator runs as a **terminal application**. It renders a 40×24 character
Apple-1 display in the terminal (unless `-H` headless mode is used) and accepts
keyboard input through the PIA.

```bash
./apple1 [options] [flat_binary]
```

Run `./apple1 -h` for a summary. Options can also be placed in a **config file**
loaded with `-f`.

---

## Command-line options

| Option | Description |
|--------|-------------|
| `-f <file>` | Load options from a config file (see below). Processed first; CLI overrides. |
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

A config file is a line-oriented list of the same single-letter flags used on the
command line. Lines starting with `#` are comments.

Example `apple1.conf`:

```
# Woz Monitor and 8 KB RAM
-r wozmon.bin
-m 8

# Load a program
-l myprog.bin@0300

# Authentic speed
-c

# Optional ACI
-a aci_rom.bin
-e tape.wav
```

Rules:

- Each option is `-` followed by a flag letter, optional whitespace, then a value
  (for flags that take arguments).
- Boolean flags (`-c`, `-H`, `-g`, …) appear alone on a line.
- Load with: `./apple1 -f apple1.conf`

There is no automatic search path — you must pass `-f` explicitly.

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
