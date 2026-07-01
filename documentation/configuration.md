# Compile-Time Configuration

All tunable limits and feature toggles live in **`apple1limit.h`** and **preprocessor
flags** passed via `EXTRA_CFLAGS` (Makefile) or `-D` on the compiler command line.

Core source files include only **`port.h`** — not `<stdint.h>`, `<stdio.h>`, or
other system headers. Sized integers are typedef'd in `port.h`.

---

## Limits (`apple1limit.h`)

Define these **before** including headers, or edit `apple1limit.h` directly.

| Macro | Default | Range / notes |
|-------|---------|---------------|
| `APPLE1_MAX_CARDS` | 8 | 1–32 expansion cards on the bus |
| `APPLE1_MAX_BREAKPOINTS` | 16 | 1–64 debugger breakpoints |
| `APPLE1_MAX_WATCHPOINTS` | 8 | 1–32 debugger watchpoints |
| `APPLE1_MAX_TRACE_LINES` | 64 | 1–4096 PC trace ring buffer |
| `APPLE1_STATIC_RAM_SIZE` | 65536 | 4096–65536 bytes compiled into `main.c` |
| `APPLE1_DEFAULT_RAM_KB` | 8 | Default RAM if `-m` not given; must fit in static size |
| `APPLE1_DEFAULT_CPU_HZ` | 1000000 | Reserved default (authentic rate is ~1.023 MHz at runtime) |
| `APPLE1_DEFAULT_BAUD` | 0 | Default terminal baud (0 = instant) |
| `APPLE1_ACI_MAX_TAPE_PULSES` | (undefined) | If set, enables fixed-size ACI tape buffer (no heap growth) |

Example — 4 KB static RAM for tiny embedded builds:

```bash
make EXTRA_CFLAGS='-DAPPLE1_STATIC_RAM_SIZE=4096 -DAPPLE1_DEFAULT_RAM_KB=4'
```

---

## Feature omission flags (`APPLE1_OMIT_*`)

Define a flag to **strip** a subsystem at compile time (smaller binary, fewer
dependencies).

| Flag | Removes |
|------|---------|
| `APPLE1_OMIT_DEBUGGER` | `dbg.c`, debugger CLI, breakpoints in main loop. **Implies** `APPLE1_OMIT_DISASM`. |
| `APPLE1_OMIT_DISASM` | Disassembler (`disasm.c`) and `-t` trace formatting |
| `APPLE1_OMIT_ACI` | ACI cassette card (`aci.c`). **Implies** `APPLE1_OMIT_WAV`. |
| `APPLE1_OMIT_WAV` | WAV load/save in ACI (tape pulse I/O only if ACI kept) |
| `APPLE1_OMIT_KRUSADER` | Krusader expansion card |
| `APPLE1_OMIT_DISKIO` | File loading in `bus.c` (bin, Woz text, external ROM paths) |
| `APPLE1_OMIT_WOZMON` | Embedded Woz Monitor ROM fallback in `bus.c` |
| `APPLE1_OMIT_CHARMAP` | Embedded 2513 charmap ROM data (**enabled by default in Makefile**) |
| `APPLE1_OMIT_STDIO` | CLI help text and stderr logging macros |
| `APPLE1_OMIT_BUS_ACCESS_CB` | Bus access callback (watchpoints need this — keep for debugger builds) |
| `APPLE1_OMIT_PIA_THROTTLE` | Microsecond delay on PIA access when uncapped |
| `APPLE1_OMIT_KBD_BOUNCE` | Keyboard debounce simulation in `bus.c` |

Typical minimal CLI for embedded:

```bash
cc -DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER \
   -DAPPLE1_OMIT_CHARMAP ... -o apple1 apple1.c
```

---

## Memory allocator modes (`port.h`)

| Flag | Behaviour |
|------|-----------|
| (default) | `port_malloc` / `port_free` / `port_realloc` from platform libc |
| `APPLE1_ZERO_MALLOC` | All allocations fail (no heap — you must use static buffers) |
| `APPLE1_CUSTOM_MALLOC` | You provide `port_malloc`, `port_free`, `port_realloc` |

Pair **`APPLE1_ACI_MAX_TAPE_PULSES`** with zero/custom malloc for fully static ACI.

---

## Makefile built-in defines

The Makefile passes `-DAPPLE1_OMIT_CHARMAP` on core object builds and sets:

```
-DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI
```

via `DEFS`. POSIX feature-test macros (`_POSIX_C_SOURCE`) are set in `port_posix_inc.h`
and included only by the POSIX port layer (`port_posix.c`).
Core sources do not include system headers and do not need libc feature
macros on the compiler command line.

MS-DOS cross-build adds:

```
-D__MSDOS__ -DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_MSDOS -DAPPLE1_TERM_DOS
```

Plan 9 [`mkfile`](../mkfile):

```
-DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_PLAN9 -DAPPLE1_TERM_VT100
```

(`mk pcc` also passes `-DAPPLE1_PORT_PLAN9_APE` for the APE libc path.)

---

## Platform selection (`APPLE1_PORT_*` / `APPLE1_TERM_*`)

Build files set two orthogonal flags. [`port.c`](../port.c) and [`term.c`](../term.c)
include the matching `port_*.c` / `term_*.c` implementation. If neither flag is set,
they fall back to **auto-detect** from compiler predefined macros (amalgamation and
casual `cc apple1.c` builds).

### Port backends (`APPLE1_PORT_*`)

| Macro | Source file | Auto-detect trigger |
|-------|-------------|---------------------|
| `APPLE1_PORT_POSIX` | `port_posix.c` | `__unix__`, `__linux__`, `__APPLE__`, *BSD, Haiku, QNX, RTEMS |
| `APPLE1_PORT_WIN` | `port_win.c` | `_WIN32`, `_WIN64` |
| `APPLE1_PORT_MSDOS` | `port_msdos.c` | `__MSDOS__`, `__dos__`, Watcom DOS |
| `APPLE1_PORT_PLAN9` | `port_plan9.c` | `__PLAN9__`, `__plan9__` |
| `APPLE1_PORT_OS2` | `port_os2.c` | `__OS2__`, `__os2__` |
| `APPLE1_PORT_VXWORKS` | `port_vxworks.c` | `__RTP__`, `_WRS_KERNEL` |
| `APPLE1_PORT_FREERTOS` | `port_freertos.c` | POSIX host + `-D`; see `documentation/FREERTOS_DEMO.md` |
| `APPLE1_PORT_ZEPHYR` | `port_zephyr.c` | (none — explicit `-D` only) |
| `APPLE1_PORT_BARE` | `port_bare.c` | fallback when host is unknown |

Sub-variant for Plan 9 APE/pcc builds: **`APPLE1_PORT_PLAN9_APE`** (affects
`port.h` / `port_stdarg.h` only).

Always linked: **`port_string.c`** (strings, format, getopt, RNG). Conditional:
**`port_tcc_va.c`** (TinyCC x86_64 va_list shim; omitted when `APPLE1_PORT_PLAN9`).

Plan 9 native `6c` builds omit these companion headers (weak preprocessor):
**`port_attrs.h`**, **`port_stdarg_libc.h`**, **`apple1limit_checks.h`** — replaced by
inline shims in `port.h`, `port_stdarg_plan9.h`, or no `#error` guards respectively.

### Terminal backends (`APPLE1_TERM_*`)

| Macro | Source file | Auto-detect trigger |
|-------|-------------|---------------------|
| `APPLE1_TERM_ANSI` | `term_ansi.c` | default (non-DOS, non-Plan 9) |
| `APPLE1_TERM_DOS` | `term_dos.c` | MS-DOS |
| `APPLE1_TERM_VT100` | `term_vt100.c` | Plan 9 / 9front |

CI-maintained paths: **`port_posix.c`** + **`term_ansi.c`**, and **`port_msdos.c`**
+ **`term_dos.c`**. Other ports are experimental or stub implementations.

Example — bare-metal with custom allocator:

```bash
make EXTRA_CFLAGS='-DAPPLE1_PORT_BARE -DAPPLE1_TERM_ANSI -DAPPLE1_CUSTOM_MALLOC'
```

---

## Port layer

### Architecture

```
Core (.c files)  →  port.h (declarations only)
                         ↓
              port_string.c  (always: strings, format, getopt, RNG)
              port_posix.c   (or port_msdos.c, port_win.c, …)
```

### VFS (file I/O)

Core code never calls `fopen` directly. It uses:

```c
port_vfs_default.open(path, PORT_VFS_READ);
port_vfs_default.read(f, buf, sz, &nread);
port_vfs_default.close(f);
```

Replace **`g_port_vfs`** before `bus_init()` for custom storage (ROM disk, embedded
blobs) on bare-metal targets.

### Terminal backends

| File | Macro | Use when |
|------|-------|----------|
| `term_ansi.c` | `APPLE1_TERM_ANSI` | ANSI terminal (macOS Terminal, Linux, iTerm, Windows) |
| `term_dos.c` | `APPLE1_TERM_DOS` | MS-DOS (`make dos-djgpp` or `make dos-watcom`) |
| `term_vt100.c` | `APPLE1_TERM_VT100` | Plan 9 teletype console — see `documentation/plan9-terminal.md` |

### Platform port files

| File | Macro | Target |
|------|-------|--------|
| `port_posix.c` | `APPLE1_PORT_POSIX` | Linux, macOS, *BSD, Android, RTEMS-style POSIX |
| `port_msdos.c` | `APPLE1_PORT_MSDOS` | MS-DOS / DJGPP |
| `port_win.c` | `APPLE1_PORT_WIN` | Windows |
| `port_plan9.c` | `APPLE1_PORT_PLAN9` | Plan 9 / 9front |
| `port_os2.c` | `APPLE1_PORT_OS2` | OS/2 (includes POSIX subset) |
| `port_vxworks.c` | `APPLE1_PORT_VXWORKS` | VxWorks RTP / kernel |
| `port_freertos.c` | `APPLE1_PORT_FREERTOS` | FreeRTOS Posix_GCC simulator (POSIX host) |
| `port_zephyr.c` | `APPLE1_PORT_ZEPHYR` | Zephyr RTOS (stub) |
| `port_bare.c` | `APPLE1_PORT_BARE` | Bare-metal fallback (no filesystem) |

Not every port file is CI-tested; **`port_posix.c`** and **`port_msdos.c`** are the
maintained paths.

---

## Runtime vs compile-time

| Setting | Where |
|---------|-------|
| RAM size 4–64 KB | `-m` CLI or `ram_kb` in `.conf` (≤ `APPLE1_STATIC_RAM_SIZE`) |
| Flat 64 KB bus | `-F` or `flat_bin` / positional flat binary |
| Speed cap (~1.023 MHz) | Default; `-c` or `speed_cap = yes` in `.conf` |
| Uncapped (full host speed) | `-u` or `speed_cap = no` in `.conf` |
| Headless | `-H` (CLI only) |
| Debugger | `-g` (CLI only; must not define `APPLE1_OMIT_DEBUGGER`) |
| Baud rate | `-B 1200` or `baud` in `.conf` |

See [usage.md](usage.md) for the full runtime reference.
