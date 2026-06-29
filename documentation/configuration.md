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

The Makefile always passes:

```
-D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
-DAPPLE1_OMIT_CHARMAP          # per-object rule on core .c files
```

MS-DOS cross-build adds:

```
-D__MSDOS__ -DAPPLE1_OMIT_CHARMAP
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

| File | Use when |
|------|----------|
| `term_ansi.c` | ANSI terminal (macOS Terminal, Linux, iTerm) |
| `term_dos.c` | MS-DOS (`make dos-djgpp` or `make dos-watcom`) |

### Platform port files

| File | Target |
|------|--------|
| `port_posix.c` | Linux, macOS, *BSD, Android, RTEMS-style POSIX |
| `port_msdos.c` | MS-DOS / DJGPP |
| `port_win.c` | Windows |
| `port_os2.c` | OS/2 (includes POSIX subset) |
| `port_elks.c`, `port_vxworks.c` | ELKS / VxWorks (timing overrides + POSIX) |
| `port_plan9.c`, `port_zephyr.c`, `port_freertos.c` | Stubs / partial |
| `port_bare.c` | Bare-metal fallback (no filesystem) |

Not every port file is CI-tested; **`port_posix.c`** and **`port_msdos.c`** are the
maintained paths.

---

## Runtime vs compile-time

| Setting | Where |
|---------|-------|
| RAM size 4–64 KB | `-m` CLI or config file (≤ `APPLE1_STATIC_RAM_SIZE`) |
| Flat 64 KB bus | `-F` or positional flat binary argument |
| Speed cap (~1.023 MHz) | `-c` |
| Headless | `-H` |
| Debugger | `-g` (must not define `APPLE1_OMIT_DEBUGGER`) |
| Baud rate | `-B 1200` |

See [usage.md](usage.md) for the full runtime reference.
