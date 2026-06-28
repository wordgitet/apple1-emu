# Building

The primary build system is the top-level **Makefile**. It auto-selects a platform
port file and links `port_string.c` with one `port_*.c`.

## Requirements

- C99 compiler (`cc`, `gcc`, or `clang`)
- Python 3 (for amalgamation and `make dos` only)
- POSIX-like shell for `make check`

## Standard build (Unix / macOS / Linux)

```bash
make clean
make              # produces ./apple1
make check        # build and run six unit tests
```

The Makefile sets:

| Variable | Default | Purpose |
|----------|---------|---------|
| `CC` | `cc` | C compiler |
| `CFLAGS` | `-O2 -g` | Optimisation / debug |
| `WFLAGS` | `-Wall -Wextra` | Warnings |
| `STDFLAG` | `-std=c99` | Language standard |
| `EXTRA_CFLAGS` | (empty) | **Your compile-time `-D` flags go here** |

Example minimal build (no debugger, smaller binary):

```bash
make EXTRA_CFLAGS='-DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER'
```

### Platform port selection

| Host | Port object linked |
|------|-------------------|
| Windows (`OS=Windows_NT`) | `port_win.c` |
| OS/2 | `port_os2.c` |
| Haiku, Linux, macOS, *BSD, … | `port_posix.c` |

Every build also links **`port_string.c`** (freestanding string/format/getopt shims).

Override manually for experiments:

```bash
make PORT_SRC=port_bare.c EXTRA_CFLAGS='-DAPPLE1_CUSTOM_MALLOC'
```

(`PORT_SRC` is a Makefile variable — set it on the command line; it is not exported
by default.)

## Single-file amalgamation

`tools/amalgamate.py` concatenates sources into `apple1.c` + `apple1.h` for
single-translation-unit builds (embedded targets, odd cross-compilers).

```bash
python3 tools/amalgamate.py
# default: port_posix.c + term_ansi.c

python3 tools/amalgamate.py --port port_msdos.c --term term_dos.c
cc -O2 -DAPPLE1_OMIT_CHARMAP apple1.c -o apple1
```

| Flag | Default | Purpose |
|------|---------|---------|
| `--port` | `port_posix.c` | Platform shim file to inline |
| `--term` | `term_ansi.c` | Terminal backend to inline |
| `--srcdir` | repo root | Source directory |
| `--outdir` | repo root | Output directory |

## MS-DOS cross-build (DJGPP)

Requires **`i586-pc-msdosdjgpp-gcc`** on `PATH` (DJGPP toolchain).

```bash
export PATH="$HOME/djgpp/bin:$PATH"
make dos          # writes apple1.exe via amalgamation + cross-compile
```

This uses `port_msdos.c` + `term_dos.c` (conio/BIOS — no ANSI.SYS required).

### Running in DOSBox

1. Copy to the same directory:
   - `APPLE1.EXE`
   - `CWSDPMI.EXE` (DPMI host — from [csdpmi7b.zip](https://www.mirrorservice.org/sites/ftp.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip))
   - Your ROM/files (8.3 names are safest: `WOZMON.BIN`)
2. Run:
   ```dos
   APPLE1 -R WOZMON.BIN
   ```

On macOS, if the compiler dies with `Killed: 9`, clear Gatekeeper quarantine:

```bash
xattr -dr com.apple.quarantine ~/djgpp
```

## Test targets

| Target | Description |
|--------|-------------|
| `make check` | All six unit tests (pass/fail summary) |
| `make test-official` | Klaus Dormann 6502 functional test (headless) |

Individual test binaries (not installed — built in repo root):

- `test_cpu` — CPU arithmetic, illegal opcodes, breakpoints
- `test_aci` — ACI cassette round-trip
- `test_bus` — Woz monitor text loader
- `test_dualram` — 8 KB split RAM mapping
- `test_tomharte` — Tom Harte opcode suite (`tests/harte_6502.bin`)
- `test_interrupts` — IRQ/NMI/BRK behaviour

Functional test (downloads test ROM):

```bash
make test-official
# equivalent to:
# ./apple1 -H -F 6502_functional_test.bin
```

Requires 64 KB flat bus (`-F`) and headless mode (`-H`). The binary must be passed
as a positional argument after options.

## Clean

```bash
make clean    # removes apple1, test binaries, *.o, port_*.o
```

Build artefacts **`apple1.exe`**, **`CWSDPMI.EXE`**, and generated **`apple1.c`**
from amalgamation are not removed by `make clean` if they exist as untracked files.

## Autotools note

`Makefile.am` exists for autoconf/automake workflows but is **out of date**
(it predates the port layer split). Prefer the top-level **Makefile** unless you
update `Makefile.am` to match.
