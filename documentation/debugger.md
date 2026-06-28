# Interactive Debugger

Enable with **`-g`** at startup. The emulator pauses before the main loop and
prints a **`db>`** prompt on the terminal.

Requires a build **without** `APPLE1_OMIT_DEBUGGER`.

---

## Starting and stopping

| Action | How |
|--------|-----|
| Start in debugger | `./apple1 -g …` |
| Step one instruction | `s` or press **Enter** on an empty line |
| Continue running | `c` |
| Quit emulator | `q` |
| Break from running | **Ctrl+C** (sets debug break if `-g`, else quits) |

While running, breakpoints and watchpoints can trap back into the debugger loop.

---

## Commands

| Command | Description |
|---------|-------------|
| **`s`** | Step one CPU instruction |
| **`<Enter>`** | Same as `s` |
| **`c`** | Continue execution (leave debugger) |
| **`r`** | Print registers: PC, A, X, Y, SP, P flags |
| **`m [start] [end]`** | Hex memory dump (default: PC .. PC+15) |
| **`w [addr] [val]`** | Write one byte to memory |
| **`b [addr]`** | Set breakpoint at address; `b` alone lists breakpoints |
| **`d [addr]`** | Delete breakpoint; `d` alone clears all |
| **`wp [addr] [type]`** | Watchpoint — type `r`, `w`, or `rw` (default `w`) |
| **`wd [addr]`** | Delete watchpoint; `wd` alone clears all |
| **`t`** | Show recent PC trace (control-flow jumps) |
| **`h`** / **`?`** | Help |
| **`q`** | Exit emulator |

### Examples

```
db> r
db> m 0200 0210
db> w 0200 A9
db> b 0300
db> wp 0200 w
db> s
db> c
```

Addresses are **16-bit hex** without `$` prefix (e.g. `FF00`, not `$FF00`).

---

## Breakpoints and watchpoints

Limits are compile-time (`apple1limit.h`):

- **`APPLE1_MAX_BREAKPOINTS`** (default 16)
- **`APPLE1_MAX_WATCHPOINTS`** (default 8)

Watchpoints use the bus access callback (`bus.access_cb`). Do not define
`APPLE1_OMIT_BUS_ACCESS_CB` in debugger builds.

---

## Trace mode vs debugger

| Feature | Flag | Behaviour |
|---------|------|-----------|
| Debugger | `-g` | Interactive `db>` prompt, breakpoints |
| Instruction trace | `-t` | Logs every instruction during normal run (uses disassembler) |

You can combine `-g` and `-t` in principle, but `-t` is usually used without the
interactive debugger for long traces.

---

## Compile-time removal

```bash
make EXTRA_CFLAGS='-DAPPLE1_OMIT_DEBUGGER'
```

This removes `dbg.c`, the `-g` flag support, and breakpoint hooks in the main loop.
`APPLE1_OMIT_DISASM` is implied automatically.
