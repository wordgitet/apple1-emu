# Building

The primary build system is the top-level **Makefile**. It auto-selects a platform
port file and links `port_string.c` with one `port_*.c`.

## Requirements

- C89 compiler (`cc`, `gcc`, `clang`, [PCC](https://pcc.ludd.ltu.se/), [lacc](https://github.com/larme/lacc), …)
- Python 3 (for amalgamation and `make dos-djgpp` / `make dos-watcom`)
- POSIX-like shell for `make check`
- [Git LFS](https://git-lfs.com/) for `tests/harte_6502.bin` (Tom Harte opcode suite), or fetch the blob from Codeberg (see [Test targets](#test-targets))

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
make check        # build and run eight unit tests
```

The Makefile sets:

| Variable | Default | Purpose |
|----------|---------|---------|
| `CC` | `cc` | C compiler |
| `CFLAGS` | `-O2 -g` | Optimisation / debug |
| `WFLAGS` | `-Wall -Wextra` | Warnings |
| `STDFLAG` | `-std=c89` | Language standard |
| `DEFS` | `-DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI` | Platform and terminal backends |
| `EXTRA_CFLAGS` | (empty) | **Your compile-time `-D` flags go here** |

Core sources include only `port.h` — no system headers — so the Makefile does **not**
pass libc feature macros (`_XOPEN_SOURCE`, `_DEFAULT_SOURCE`, …).  The POSIX port
sets `_POSIX_C_SOURCE` in `port_posix_inc.h` before any `<signal.h>` / `<unistd.h>`
include.  See `documentation/configuration.md`.

[TinyCC](https://bellard.org/tcc/) on **x86_64** (non-Windows): `make tcc` or
`make CC=tcc WFLAGS= STDFLAG=`.  Only that target needs `port_stdarg.h` /
`port_tcc_va.c`; other CPUs and compilers use normal `<stdarg.h>`.  To run the
unit tests under TinyCC: `make check CC=tcc WFLAGS= STDFLAG= DEFS=`.

Other hosted C89 compilers on Linux (smoke-tested):

```bash
make pcc
make tcc
make clean && make CC=lacc
```

Example minimal build (no debugger, smaller binary):

```bash
make EXTRA_CFLAGS='-DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER'
```

### Platform port selection

The Makefile sets `-DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI` by default (see `DEFS`).
[`port.c`](../port.c) and [`term.c`](../term.c) select the implementation from these
flags; if neither is set, they auto-detect from compiler predefined macros.

| Host | Default port | Default terminal |
|------|--------------|------------------|
| Linux, macOS, *BSD, Haiku, … | `port_posix.c` | `term_ansi.c` |
| Windows (`Makefile.msc`) | `port_win.c` | `term_ansi.c` |
| Plan 9 / 9front (`mkfile`) | `port_plan9.c` | `term_vt100.c` |
| MS-DOS (`make dos-*`) | `port_msdos.c` | `term_dos.c` |

Every build also links **`port_string.c`** (freestanding string/format/getopt shims).

Override manually for experiments:

```bash
make EXTRA_CFLAGS='-DAPPLE1_PORT_BARE -DAPPLE1_CUSTOM_MALLOC'
```

See [configuration.md](configuration.md) for the full `APPLE1_PORT_*` and
`APPLE1_TERM_*` registry.

## Single-file amalgamation

`tools/amalgamate.py` concatenates sources into `apple1.c` + `apple1.h` for
single-translation-unit builds (embedded targets, odd cross-compilers).

```bash
python3 tools/amalgamate.py
# default: port_posix.c + term_ansi.c

python3 tools/amalgamate.py --port port_msdos.c --term term_dos.c
cc -O2 -DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_MSDOS -DAPPLE1_TERM_DOS apple1.c -o apple1
```

| Flag | Default | Purpose |
|------|---------|---------|
| `--port` | `port_posix.c` | Platform shim file to inline |
| `--term` | `term_ansi.c` | Terminal backend to inline |
| `--srcdir` | repo root | Source directory |
| `--outdir` | repo root | Output directory |

## MS-DOS cross-build

Two toolchains are supported. Both use the same DOS amalgamation
(`port_msdos.c` + `term_dos.c`).

| Target | Toolchain | Output | DPMI host |
|--------|-----------|--------|-----------|
| `make dos-djgpp` | DJGPP (`i586-pc-msdosdjgpp-gcc`) | `apple1.exe` | **CWSDPMI.EXE** required |
| `make dos-watcom` | Open Watcom (`wcl386` or `owcc`) | `apple1.exe` | none (CauseWay stub embedded) |

### DJGPP

Requires **`i586-pc-msdosdjgpp-gcc`** on `PATH`.

```bash
export PATH="$HOME/djgpp/bin:$PATH"
make dos-djgpp
```

### Open Watcom

Install [Open Watcom](https://github.com/open-watcom/open-watcom-v2). The Makefile
defaults to **`$HOME/watcom`** with host tools from **`bino64`** (macOS) or
**`binl64`** (Linux):

```bash
make dos-watcom
```

Custom install path:

```bash
make dos-watcom WATCOM=$HOME/watcom WATCOM_BINDIR=$HOME/watcom/bino64
```

The Makefile sets `INCLUDE=$WATCOM/h` and `LIB=$WATCOM/lib386/dos` automatically.
It also puts **`$WATCOM/binw`** on `PATH` during the link step so **`cwstub.exe`**
(the CauseWay stub) is found. Without it the linker warns `cannot open cwstub.exe`
and the resulting `apple1.exe` exits immediately after the CauseWay banner.
Open Watcom’s 32-bit `conio.h` lacks DJGPP’s `gotoxy`/`clrscr`; the DOS terminal
backend uses BIOS int 10h instead (`int386` under Watcom, `int86` under DJGPP).

The default driver is **`wcl386`** with the **CauseWay** extender (`-l=causeway`), so
the resulting `apple1.exe` is self-contained — no **DOS4GW.EXE** or **CWSDPMI.EXE**
needed in DOSBox. If your install exposes **`owcc`** instead:

```bash
make dos-watcom WATCOM_CC=owcc WATCOM_CFLAGS="-bdos -ox -zq -fe=apple1.exe -bcauseway"
```

To use classic DOS/4GW instead (requires **DOS4GW.EXE** alongside the app):

```bash
make dos-watcom WATCOM_CFLAGS="-bt=dos -ox -zq -w3 -fe=apple1.exe -l=dos4g"
```

Both targets use `port_msdos.c` + `term_dos.c` (conio/BIOS — no ANSI.SYS required).

### Running in DOSBox

1. Copy to the same directory:
   - `APPLE1.EXE` (Watcom/CauseWay build needs nothing else)
   - For **DJGPP** builds only: `CWSDPMI.EXE` (from [csdpmi7b.zip](https://www.mirrorservice.org/sites/ftp.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip))
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
| `make check` | All eight unit tests (pass/fail summary) |
| `make test-official` | Klaus Dormann 6502 functional test (headless) |

Individual test binaries (not installed — built in repo root):

- `test_cpu` — CPU arithmetic, illegal opcodes, breakpoints
- `test_aci` — ACI cassette round-trip
- `test_bus` — Woz monitor text loader
- `test_dualram` — 8 KB split RAM mapping
- `test_tomharte` — Tom Harte opcode suite (`tests/harte_6502.bin`)

`tests/harte_6502.bin` is tracked with **Git LFS** (~117 MB).  After clone,
run `git lfs pull`.  If GitHub LFS returns 404, pull from Codeberg instead:

```bash
git lfs fetch codeberg main --include=tests/harte_6502.bin
git lfs checkout tests/harte_6502.bin
```

Or download the object directly:

```bash
curl -fL -o tests/harte_6502.bin \
  'https://codeberg.org/wordgitet/apple1-emu.git/info/lfs/objects/798e7c09a6830877573db418f73225fb537dfa048afcbc1e46bfcd332e4fa4f0'
```

Verify: `file tests/harte_6502.bin` must report `data` and start with `HRT1`, not
a 134-byte `version https://git-lfs...` pointer.

- `test_interrupts` — IRQ/NMI/BRK behaviour
- `test_vfs` — `port_vfs` swap and Woz monitor text load via VFS
- `test_config` — human-readable `.conf` parsing

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

**MS-DOS (DJGPP or Open Watcom):**
- DJGPP: `make dos-djgpp` (needs `CWSDPMI.EXE` in DOSBox)
- Open Watcom: `make dos-watcom` (CauseWay stub embedded; no extra EXE in DOSBox)

**Other platforms (OS/2, Plan 9, VxWorks, FreeRTOS, Zephyr):**
- **Plan 9 / 9front:** `dircp` tree to `$home`, `rm -f apple1`, then `mk all`. Run `./6.out` (no `vt` needed). Headless: `./6.out -H`. See `documentation/plan9-terminal.md`.
- Other ports: amalgamation with explicit flags, e.g. `python3 tools/amalgamate.py --port port_plan9.c` and `-DAPPLE1_PORT_PLAN9 -DAPPLE1_TERM_VT100`
