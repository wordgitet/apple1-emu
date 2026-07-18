# Compile-Time Configuration

All tunable limits and feature toggles live in **`apple1limit.h`** and
preprocessor flags passed via `EXTRA_CFLAGS` or `-D` on the compiler command line.

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
| `APPLE1_STATIC_RAM_SIZE` | 8192 | 4096–65536 bytes compiled into `main.c` |
| `APPLE1_DEFAULT_CPU_HZ` | 1000000 | Reserved default (runtime rate ~1.023 MHz) |
| `APPLE1_DEFAULT_BAUD` | 0 | Default terminal baud (0 = instant) |
| `APPLE1_KBD_BUFFER_SIZE` | 2048 | Keyboard ring buffer in `io.c` |
| `APPLE1_PASTE_BUFFER_SIZE` | 2048 | Bracketed-paste staging in `term_ansi.c` |
| `APPLE1_ACI_MAX_TAPE_PULSES` | (undefined) | Enables fixed-size ACI tape buffer |

Example — 4 KB static RAM:

```bash
make EXTRA_CFLAGS='-DAPPLE1_STATIC_RAM_SIZE=4096'
```

---

## Feature omission flags (`APPLE1_OMIT_*`)

Define a flag to **strip** a subsystem at compile time.

| Flag | Removes |
|------|---------|
| `APPLE1_OMIT_DEBUGGER` | `dbg.c`, debugger CLI, breakpoints. **Implies** `APPLE1_OMIT_DISASM`. |
| `APPLE1_OMIT_DISASM` | Disassembler (`disasm.c`) and `-t` trace formatting |
| `APPLE1_OMIT_ACI` | ACI cassette card (`aci.c`). **Implies** `APPLE1_OMIT_WAV`. |
| `APPLE1_OMIT_WAV` | WAV load/save in ACI |
| `APPLE1_OMIT_KRUSADER` | Krusader expansion card |
| `APPLE1_OMIT_DISKIO` | File loading in `bus.c` |
| `APPLE1_OMIT_WOZMON` | Embedded Woz Monitor ROM fallback |
| `APPLE1_OMIT_CHARMAP` | Embedded 2513 charmap ROM data (**enabled by default in Makefile**) |
| `APPLE1_OMIT_STDIO` | CLI help text and stderr logging macros |
| `APPLE1_OMIT_BUS_ACCESS_CB` | Bus access callback (watchpoints need this) |
| `APPLE1_OMIT_PIA_THROTTLE` | Microsecond delay on PIA access |
| `APPLE1_OMIT_KBD_BOUNCE` | Keyboard debounce simulation |
| `APPLE1_OMIT_PASTE` | Bracketed-paste mode in `term_ansi.c` / `port_posix.c` |

Typical minimal build:

```bash
cc -DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER \
   -DAPPLE1_OMIT_CHARMAP ... -o apple1 apple1.c
```

---

## Memory allocator modes

| Flag | Behaviour |
|------|-----------|
| (default) | `port_malloc` / `port_free` / `port_realloc` from libc |
| `APPLE1_ZERO_MALLOC` | All allocations fail (static buffers only) |
| `APPLE1_CUSTOM_MALLOC` | You provide `port_malloc`, `port_free`, `port_realloc` |

Pair **`APPLE1_ACI_MAX_TAPE_PULSES`** with zero/custom malloc for fully static ACI.

---

## Makefile built-in defines

The Makefile passes `-DAPPLE1_OMIT_CHARMAP` on core object builds and:

```
-DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI
```

POSIX feature-test macros (`_POSIX_C_SOURCE`) are set in `port_posix_inc.h`,
included only by `port_posix.c`.  Core sources do not include system headers.

---

## Platform selection

There is one port backend and one terminal backend:

### Port backend

| Macro | Source file |
|-------|-------------|
| `APPLE1_PORT_POSIX` | `port_posix.c` |

Always linked alongside: **`port_string.c`** (strings, format, getopt, RNG)
and **`port_path.c`** (path resolution).

**`port_tcc_va.c`** is included automatically on TCC x86_64/Linux builds to
work around TCC's non-standard `va_list` layout.

### Terminal backend

| Macro | Source file |
|-------|-------------|
| `APPLE1_TERM_ANSI` | `term_ansi.c` |

---

## Port layer architecture

```
Core (.c files)  →  port.h (declarations only)
                         ↓
              port_string.c  (always: strings, format, getopt, RNG)
              port_path.c    (always: path resolution)
              port_posix.c   (timing, terminal raw mode, VFS)
```

### VFS (file I/O)

Core code never calls `fopen` directly. It uses:

```c
port_vfs_default.open(path, PORT_VFS_READ);
port_vfs_default.read(f, buf, sz, &nread);
port_vfs_default.close(f);
```

Replace **`g_port_vfs`** before `bus_init()` to override storage
(embedded blobs, custom backing store).

---

## Runtime vs compile-time

| Setting | Where |
|---------|-------|
| RAM size | Compile-time `APPLE1_STATIC_RAM_SIZE` (default 8 KB) |
| Flat 64 KB bus | `-F` or positional flat binary |
| Speed cap (~1.023 MHz) | Default; `-c` or `speed_cap = yes` in `.conf` |
| Uncapped | `-u` or `speed_cap = no` in `.conf` |
| Headless | `-H` |
| Debugger | `-g` (must not define `APPLE1_OMIT_DEBUGGER`) |
| Baud rate | `-B 1200` or `baud` in `.conf` |

See [usage.md](usage.md) for the full runtime reference.
