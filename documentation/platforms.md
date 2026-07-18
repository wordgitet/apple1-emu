# Platform ports

The codebase targets **POSIX** exclusively.  The port and terminal backend
are selected at compile time; `port.c` and `term.c` include the matching shim.

| Platform | Build command | Port | Terminal |
|----------|---------------|------|----------|
| **Linux** | `./configure && make` | `port_posix.c` | `term_ansi.c` |
| **macOS** | `./configure && make` | `port_posix.c` | `term_ansi.c` |
| ***BSD** | `./configure && make` | `port_posix.c` | `term_ansi.c` |
| **Illumos / Solaris** | `./configure && gmake` | `port_posix.c` | `term_ansi.c` |
| **MINIX 3.3** | `./configure && make` | `port_posix.c` | `term_ansi.c` |
| **Sortix 1.1** | `autoreconf -fi && ./configure && make` | `port_posix.c` | `term_ansi.c` |
| **UnixWare / OpenUNIX** | `make -f Makefile.uw` | `port_posix.c` | `term_ansi.c` |

Compiler portability (gcc, clang, TCC, pcc, lacc, nwcc, cproc, cparser,
chibicc, ccomp, vbcc) is preserved — see
[building.md](building.md#alternate-compilers).

---

## POSIX (default)

```bash
./configure
make
make check
./apple1 -r wozmon.bin
```

- **Display:** 40×24 ANSI terminal; power-on checkerboard.
- **Config:** `apple1.conf` or CLI switches — see [usage.md](usage.md).
- **Keyboard:** Ctrl+C quit, Ctrl+R / Ctrl+T reset, Ctrl+L clear screen.

---

## UnixWare / OpenUNIX

On the target machine (native USL `cc`, not GNU make):

```bash
make -f Makefile.uw
make -f Makefile.uw minimal
./apple1
```

Uses `port_posix.c` with `__USLC__` shims.  Run tests on a standard POSIX
host before deploying.

---

## Illumos / Solaris

Requires GNU make:

```bash
./configure
gmake
gmake check
```

Compiles warning-free with GCC; all 9 tests pass.

---

## MINIX 3

Tested on **MINIX 3.3.0** (i386).  Install a compiler from pkgin first:

```sh
pkgin -y install clang binutils
./configure
make
./apple1 -r wozmon.bin
```

---

## Sortix 1.1

Autotools and GCC are pre-installed; autoreconf first:

```sh
autoreconf -fi
./configure
make
./apple1 -r wozmon.bin
```

---

## Single-file amalgamation

```bash
make amalgamation           # generates apple1.c + apple1.h
make single                 # also compiles POSIX binary
make check-single           # link-only smoke test
```

See [building.md](building.md#single-file-amalgamation).
