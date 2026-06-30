# Plan 9 / 9front terminal notes

This document explains how terminals work on Plan 9 and 9front, why the
current Apple-1 build looks wrong in a native window, and what a proper
`term_vt100.c` would need to do.  It is background for the port — not a
user manual.

See also `building.md` (how to compile with `mk`) and `usage.md` (keyboard
shortcuts in `term_ansi.c`, which Plan 9 does not honor yet).

## What you are talking to

On 9front (including a `drawterm` session into a VM):

```
rio window  →  terminal program (rc, cpu, 9term, …)  →  /dev/cons  →  fd 0 / fd 1
```

- **Input:** programs `read(0, …)` from the console device.
- **Output:** programs `write(1, …)` to the same window.
- There is **no separate ANSI/xterm layer** in the native stack.  Bytes written
  to fd 1 are mostly shown as-is (UTF-8 runes in modern 9front).

`port_plan9.c` uses exactly that: `read(0, …)` and `write(1, …)`.
`port_term_raw_enable()` is currently a stub — Plan 9 does not use POSIX
`termios`.

## Native terminals are not VT100

Unix builds use **`term_ansi.c`**: a 40×24 character “CRT” drawn with **ANSI/ECMA-48
escape sequences**:

| Sequence (C string) | Intent on xterm / Linux console |
|---------------------|----------------------------------|
| `\x1b[?25l`         | hide cursor                      |
| `\x1b[2J`           | clear entire screen              |
| `\x1b[H`            | cursor to home (row 1, col 1)    |
| `\x1b[?25h`         | show cursor (shutdown)           |

On Plan 9 / 9term / drawterm, those bytes are **not interpreted**.  The window
prints the escape character as `^[` (or similar) and the following `[`, `?`, `2`,
`J`, etc. as ordinary text.  That is the “garbage” at the top of the screen.

Until `term_vt100.c` existed, the build used `term_ansi.c`, which is wrong on
native 9term (literal `^[` garbage) and also fails inside **`vt(1)`**: after
`ESC [ 2 J` clears the screen, `term_update()` repaints with `ESC [ H` once
then newline-only row breaks — **VT-100 LF does not return to column 1**, so
later rows are drawn off-screen and the display looks blank.

The current **`term_vt100.c`** driver avoids ANSI entirely: it echoes Woz
Monitor output as plain bytes (teletype style).  No `vt` wrapper required.
Run `./6.out` directly in a normal window.

| Mode | Status |
|------|--------|
| **Headless** `./6.out -H` | Works — no terminal UI |
| **Interactive** `./6.out` | Teletype console via `term_vt100.c` |
| **Inside `vt`** | Optional; not needed for basic use |

## Cooked vs raw input (9term)

Plan 9 terminal windows support two input styles (see **9term(1)** in plan9port,
same ideas on 9front):

- **Cooked (default):** the window may edit a line before the program sees it
  (erase word, erase line, etc.).  Line buffering can delay `read(0)` until
  Enter.
- **Raw / “pass-through”:** keystrokes go to the program with less editing;
  used when a program disables echo (typical for Unix `csh`/`bash` with
  `stty`).

On Plan 9, **`read(0)` on `/dev/cons` blocks until Enter`** unless **`rawon`**
has been written to **`/dev/consctl`** (see **cons(3)**).  The first
`term_poll()` in the main loop used to freeze the emulator before `cpu_step()`
ran — banner only, no Woz output.

`port_plan9.c` now:

1. Writes **`rawon`** to **`/dev/consctl`** in `port_term_raw_enable()`.
2. Spawns an **`rfork(RFPROC|RFMEM|RFNOWAIT)`** reader (libc, no `libthread`)
   that blocks on `read(0)` in the background and fills a ring buffer; `port_term_read_char()` returns `PORT_TERM_NODATA`
   when empty (non-blocking).
3. Interactive builds call **`cpu_reset()`** immediately (other hosts wait for
   Ctrl+R for authentic power-on).

Woz still expects **key bytes with the high bit set** (`term_poll()` returns
`ch | 0x80`).

## `vt(1)` — foreign terminal emulation

9front ships **vt(1)** (see `sys/man/1/vt`): a **VT-100 / VT-220 / xterm**
emulator for talking to *non*-Plan-9 software.  It is the system’s admission
that normal windows do not speak ANSI.

In theory you could run the emulator inside `vt` so `term_ansi.c` works:

```rc
vt -xterm ./6.out
```

(`-a` ANSI, `-2` VT-220, `-x` xterm — exact flags per your `vt` man page.)

That is a **workaround**, not the native path.  It adds overhead and may still
differ from Linux xterm (colors, bracketed paste, etc.).

## Comparison with other backends in this repo

| Backend        | Host display API              | ANSI escapes? |
|----------------|-------------------------------|---------------|
| `term_ansi.c`  | stdout + escape sequences     | Yes           |
| `term_dos.c`   | BIOS `int 10h` / conio        | No            |
| (needed) `term_vt100.c` | plain `write(1)` or draw | No            |

**`term_dos.c`** is the closest model: same `vram` logic, but **`dos_goto` /
`dos_clrscr`** instead of `\x1b[…]`.  A Plan 9 driver would:

- omit all `\x1b` sequences;
- either print only **changed** cells, or print 24 lines with `\r` and accept
  scrollback growth, or use a small amount of **in-band 9term protocol** (e.g.
  window title / hold — not full cursor positioning);
- avoid redrawing 24×40 `@` grids at 30 Hz unless necessary;
- wire `port_term_raw_enable()` when we find the right Plan 9 API for
  character-at-a-time input.

There is **no** standard “cursor to (x,y)” on native Plan 9 consoles comparable
to ANSI or BIOS.  Full-screen TUI on Plan 9 usually uses **libdraw** / **rio**
windows, not escape codes — that would be a larger port than a `term_*.c` swap.

## What works today on 9front

| Mode | Status |
|------|--------|
| **Headless** `./6.out -H` | Works |
| **Interactive** `./6.out` | Plain-console Woz output (`term_vt100.c`) |
| **Debugger `-g`** | Untested on Plan 9 |
| **Config file** | Should work (VFS via `port_plan9.c`) |

## Intended build layout

```
term.c          →  APPLE1_TERM_VT100  →  term_vt100.c
port.c          →  APPLE1_PORT_PLAN9  →  port_plan9.c
mkfile          →  TARG=out  →  ./6.out
```

## References

- 9front `vt(1)` — VT-100/ANSI emulator for foreign programs
- plan9port `9term(1)` — terminal window behavior (cooked/raw, hold mode)
- This repo: `term_ansi.c` (what we ship today), `term_dos.c` (non-ANSI pattern)
