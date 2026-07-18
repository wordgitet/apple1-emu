# Building

## Autotools (default)

```bash
./configure
make
make check       # 9 unit tests
./apple1 -r wozmon.bin
```

Requires `autoconf`, `automake`, and a C89-capable compiler.  To regenerate
`configure` after editing `configure.ac` or `Makefile.am`:

```bash
autoreconf -fi
```

### Stripped optimized binary

```bash
make apple1-opt   # builds apple1 then strips it
```

### Minimal binary

```bash
make minimal      # ./apple1_minimal — strips debugger, ACI, Krusader, etc.
```

---

## Alternate compilers

All use the same autotools build system (`make CC=<compiler>`):

| Target | Compiler |
|--------|----------|
| `make tcc` | TinyCC |
| `make pcc` | Portable C Compiler |
| `make lacc` | lacc |
| `make nwcc` | nwcc |
| `make antcc` | antcc |
| `make cproc` | cproc |
| `make cparser` | cparser |
| `make chibicc` | chibicc |
| `make ccomp` | CompCert |

Each target runs `make clean` first, then rebuilds `apple1` with that compiler.

---

## Single-file amalgamation

Generates `apple1.c` + `apple1.h` — a self-contained single-file build:

```bash
make amalgamation
```

Compile the amalgamation directly:

```bash
cc -std=c89 -O2 -DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI \
   apple1.c -o apple1
```

Build and link-test in one step:

```bash
make single        # generates and compiles
make check-single  # generates and link-tests only
```

The amalgamation tool (`tools/amalgamate.py`) inlines every `port_*.c` and
`term_*.c` it finds, so no manual list update is needed.

---

## UnixWare / OpenUNIX

On the target machine, using native USL `cc` and `make` (not GNU make):

```bash
make -f Makefile.uw
make -f Makefile.uw minimal
./apple1
```

Run `make check` on a standard POSIX host before deploying.

---

## Test targets

| Target | Description |
|--------|-------------|
| `make check` | All nine unit tests (pass/fail summary) |
| `make test-official` | Klaus Dormann 6502 functional test (downloads ROM) |

Individual test binaries (built in repo root, not installed):

| Binary | Tests |
|--------|-------|
| `test_cpu` | CPU arithmetic, illegal opcodes, breakpoints |
| `test_aci` | ACI cassette round-trip |
| `test_bus` | Woz monitor text loader |
| `test_dualram` | 8 KB split RAM mapping |
| `test_tomharte` | Tom Harte opcode suite (`tests/harte_6502.bin`) |
| `test_interrupts` | IRQ/NMI/BRK behaviour |
| `test_vfs` | `port_vfs` swap and Woz monitor text load via VFS |
| `test_config` | Human-readable `.conf` parsing |
| `test_mem` | Port memory allocator dispatch |

`tests/harte_6502.bin` is tracked with **Git LFS** (~117 MB).  After clone:

```bash
git lfs pull
```

If GitHub LFS returns 404, pull from Codeberg:

```bash
git lfs fetch codeberg main --include=tests/harte_6502.bin
git lfs checkout tests/harte_6502.bin
```

Functional test (downloads test ROM on first run):

```bash
make test-official
# equivalent to: ./apple1 -H -F 6502_functional_test.bin
```

---

## Clean

```bash
make clean         # removes apple1, test binaries, *.o
make distclean     # also removes Makefile.in, configure, aclocal.m4
```

Generated amalgamation files `apple1.c` / `apple1.h` are also removed by
`make clean`.
