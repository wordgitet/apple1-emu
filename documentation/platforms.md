# Platform ports

Every target is selected at **compile time** with `-DAPPLE1_PORT_*` and
`-DAPPLE1_TERM_*`.  [`port.c`](../port.c) and [`term.c`](../term.c) `#include`
the matching shim.  See [configuration.md](configuration.md) for the full
macro registry.

| Platform | Build | Entry | Port | Terminal | Notes |
|----------|-------|-------|------|----------|-------|
| **POSIX** (Linux, macOS, *BSD, Haiku, …) | `./configure && make` | `main.c` | `port_posix.c` | `term_ansi.c` | Default.  Ctrl+R reset, Ctrl+L clear. |
| **MS-DOS** (DJGPP) | `make dos-djgpp` | `main.c` | `port_msdos.c` | `term_dos.c` | Needs `CWSDPMI.EXE` in DOSBox. |
| **MS-DOS** (Open Watcom) | `make dos-watcom` | `main.c` | `port_msdos.c` | `term_dos.c` | CauseWay stub embedded. |
| **Windows** (MSVC) | `nmake -f Makefile.msc` | `main.c` | `port_win.c` | `term_ansi.c` | Or MinGW via autotools. |
| **Plan 9 / 9front** | `mk all` | `main.c` | `port_plan9.c` | `term_vt100.c` | See [plan9-terminal.md](plan9-terminal.md). |
| **UnixWare / OpenUNIX** | `make -f Makefile.uw` | `main.c` | `port_posix.c` | `term_vt100.c` | Native `cc` + `make`, not autotools. |
| **MINIX 3.3** | `./configure && make` | `main.c` | `port_posix.c` | `term_ansi.c` | Needs `clang` + `binutils` from pkgin (neither in base). |
| **Sortix 1.1** | `autoreconf -fi && ./configure && make` | `main.c` | `port_posix.c` | `term_ansi.c` | Compiles out of the box with GCC 14.2.0. |
| **TI-Nspire** (Ndless) | `make nspire` | `main_nspire.c` | `port_nspire.c` | `term_nspire.c` | No CLI; config via `apple1.conf.tns`. |
| **VxWorks 7 RTP** | `bash vxworks_rtp_build.sh` | `main.c` | `port_vxworks.c` | `term_vt100.c` | See [VXWORKS_RTP.md](VXWORKS_RTP.md). |
| **FreeRTOS** (simulator) | `make freertos` | `main.c` | `port_freertos.c` | `term_ansi.c` | See [FREERTOS_DEMO.md](FREERTOS_DEMO.md). |
| **Zephyr** | explicit `-D` | `main.c` | `port_zephyr.c` | `term_ansi.c` | Stub port. |
| **OS/2** | amalgamation | `main.c` | `port_os2.c` | `term_ansi.c` | Experimental. |
| **Bare metal** | amalgamation | `main.c` | `port_bare.c` | `term_ansi.c` | No filesystem unless you wire VFS. |

---

## POSIX (default)

```bash
./configure
make
make check
./apple1 -r wozmon.bin
```

- **Display:** 40×24 ANSI terminal; power-on checkerboard (`_` / blinking `@`).
- **Config:** `apple1.conf` or CLI switches — see [usage.md](usage.md).
- **Keyboard:** Ctrl+C quit, Ctrl+R / Ctrl+T reset, Ctrl+L clear screen.

Alternate compilers: `make antcc`, `make chibicc`, `make ccomp`, `make cparser`, `make cproc`, `make nwcc`, `make tcc`, `make pcc`, `make lacc`.

---

## MS-DOS

Two cross-compilers, same DOS terminal backend.

| Target | Command | Runtime in DOSBox |
|--------|---------|-------------------|
| DJGPP | `make dos-djgpp` | `APPLE1.EXE` + `CWSDPMI.EXE` |
| Open Watcom | `make dos-watcom` | `APPLE1.EXE` only (CauseWay) |

```dos
APPLE1 -R WOZMON.BIN
```

Uses conio / BIOS int 10h — no ANSI.SYS.  Full CLI in [usage.md](usage.md).

Details: [building.md](building.md#ms-dos-cross-build).

---

## Windows

```bash
nmake -f Makefile.msc
nmake -f Makefile.msc bare
```

Produces `apple1.exe` with `port_win.c` + `term_ansi.c`.  `bare` builds
`apple1_bare.exe` with the same `APPLE1_OMIT_*` strip set as `make bare`.
MinGW builds can use autotools with `CC=x86_64-w64-mingw32-gcc`.

---

## Plan 9 / 9front

```bash
mk all
./6.out
mk bare
./apple1_bare
```

Headless: `./6.out -H`.

- **Why VT100, not ANSI:** `vt` in Plan 9 / 9front only emulates a VT-100 or 
VT-220 terminal. See [plan9-terminal.md](plan9-terminal.md).
- **Amalgamation:** `mk amalg` (no Python).

---

## UnixWare / OpenUNIX

On the target machine (native USL `cc`, not GNU make):

```bash
make -f Makefile.uw
make -f Makefile.uw bare
./apple1
```

Uses `port_posix.c` with `__USLC__` shims and `term_vt100.c`.  No `make check`
on UnixWare — run tests on a POSIX host first.

---

## MINIX 3

Tested on **MINIX 3.3.0** (i386).  Neither a compiler nor a linker ships
in the base system — install both from pkgin:

```sh
pkgin -y install clang binutils
```

Then build normally:

```sh
./configure
make
./apple1 -r wozmon.bin
```

`configure` picks up clang automatically.  No source changes are needed;
the code is strict C89 POSIX and compiles warning-free.

**Ctrl+R over SSH:** most local shells (bash, zsh) intercept Ctrl+R for
reverse history search before the byte reaches the SSH stream.  Run the
emulator on the MINIX console directly, or use `bind -r '\C-r'` in bash
on the host before connecting, to pass Ctrl+R through.

---

## Sortix 1.1

Tested on **Sortix 1.1.0-dev** (x86_64).  Since autotools (autoreconf, autoconf,
automake) and GCC 14.2.0 are preinstalled on Sortix, the emulator builds completely
out of the box.

Generate configure script first:

```sh
autoreconf -fi
```

Then build normally:

```sh
./configure
make
./apple1 -r wozmon.bin
```

No source changes are needed; the C89 POSIX codebase compiles and runs cleanly.

---

## TI-Nspire (Ndless)

Apple-1 for TI-Nspire calculators running [Ndless](https://ndless.me/).  Uses
the LCD (`lcd_blit`) and physical keyboard (`isKeyPressed`).

**Build** (requires Ndless SDK: `nspire-gcc`, `nspire-ld`, `genzehn`,
`make-prg`):

```bash
make nspire
make nspire NDLESS_SDK=/path/to/ndless-sdk
```

Output: **`apple1.tns`**.  Omitted at compile time: debugger, ACI, Krusader,
disassembler, charmap, PIA throttle.  Entry: `main_nspire.c` (no argv parsing).

**Install:**

```
<documents>/ndless/apple1.tns
<documents>/ndless/apple1.conf.tns    # optional
<documents>/ndless/roms/basic.tns     # example data file
```

Ndless transfer only accepts `.tns` — rename raw `.bin` dumps for upload; the
emulator reads bytes as-is.  Config paths resolve under `<documents>/ndless/`
unless absolute (`/documents/ndless/...`).  Same `key = value` format as desktop
`.conf` files — see `apple1.conf.tns` in the repo and [usage.md](usage.md).

Example config:

```
load = roms/basic.tns @ E000
```

**Run:** launch from Ndless menu.  Blank screen + blinking `@`, Wozmon boots
automatically (no manual reset).  40×20 chars on 320×240 LCD.

| Key | Action |
|-----|--------|
| ESC | Quit |
| DOC (CX) / HOME (classic) | Reset 6502 |
| Ctrl + DEL | Clear screen |
| Shift + Space | `_` (rubout) |
| Shift + × | `"` |
| Ctrl + EE | `@` |

No DEL/backspace mapped — use Shift+Space for rubout.

---

## VxWorks 7 RTP

```bash
source ~/vxworks-sdk/sdkenv.sh
bash vxworks_rtp_build.sh
```

Produces `apple1.vxe`.  Uses `term_vt100.c` on serial stdio.

**Full guide:** [VXWORKS_RTP.md](VXWORKS_RTP.md).

---

## FreeRTOS

Quick local binary (no scheduler):

```bash
make freertos
./apple1_freertos
```

Full Posix_GCC integration test:

```bash
bash freertos_demo_test.sh   # from FreeRTOS/Demo/Posix_GCC
```

**Full guide:** [FREERTOS_DEMO.md](FREERTOS_DEMO.md).

---

## Zephyr / OS/2 / bare metal

Build via amalgamation with explicit flags:

```bash
python3 tools/amalgamate.py --port port_bare.c
cc -std=c89 -DAPPLE1_PORT_BARE -DAPPLE1_TERM_ANSI apple1.c -o apple1
```

`port_zephyr.c` and `port_os2.c` are stubs or lightly tested.  Wire
`g_port_vfs` before `bus_init()` on bare metal if you have no filesystem.

---

## Single-file amalgamation

`make single` with `HOST=` selects the port preset:

| `HOST` | Port / term |
|--------|-------------|
| `posix` | auto / ANSI |
| `dos`, `watcom` | MS-DOS / DOS |
| `win` | Windows / ANSI |
| `plan9` | Plan 9 / VT100 |
| `unixware` | POSIX / VT100 |

See [building.md](building.md#single-file-amalgamation-make-single).
