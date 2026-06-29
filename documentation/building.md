# Building

The primary build system is the top-level **Makefile**. It auto-selects a platform
port file and links `port_string.c` with one `port_*.c`.

## Requirements

- C89 compiler (`cc`, `gcc`, or `clang`)
- Python 3 (for amalgamation and `make dos` only)
- POSIX-like shell for `make check`

## Build-time configuration

Build-time options are set via `apple1limit.h` or `-D` flags:

| Flag | Purpose |
|------|---------|
| `-DAPPLE1_OMIT_DEBUGGER` | Remove debugger |
| `-DAPPLE1_OMIT_ACI` | Remove cassette interface |
| `-DAPPLE1_OMIT_KRUSADER` | Remove Krusader ROM |
| `-DAPPLE1_STATIC_RAM_SIZE=4096` | Set max RAM size (embedded) |

See `apple1limit.h` for all available options and limits.

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
| `STDFLAG` | `-std=c89` | Language standard |
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

## Autotools (POSIX only)

`configure.ac` and `Makefile.am` exist for autotools builds on POSIX systems (Linux, macOS, *BSD). The autotools build is POSIX-only and uses `port_posix.c`.

For non-POSIX platforms, use the alternatives below.

### Non-POSIX platform alternatives

**Windows (MSVC):**
```bash
nmake -f Makefile.msc
```
- Use the top-level Makefile with MinGW
- Use amalgamation: `python3 tools/amalgamate.py --port port_win.c`

**MS-DOS (DJGPP):**
- Use `make dos` (amalgamation + cross-compile)

**Other platforms (OS/2, Plan 9, VxWorks, FreeRTOS, Zephyr):**
- Use the top-level Makefile with manual port selection: `make PORT_SRC=port_plan9.c`
- Use amalgamation: `python3 tools/amalgamate.py --port port_plan9.c`
