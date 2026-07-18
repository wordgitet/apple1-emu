# FreeRTOS integration

The emulator has a generic FreeRTOS port and a separate adapter for the
official POSIX simulator.  FreeRTOS standardizes scheduling and heap APIs,
but not console, keyboard, or filesystem APIs.  Those services belong to the
board support package.

## POSIX simulator

The existing simulator integration remains available for Linux and macOS.
It uses `port_freertos_posix.c`, selected with
`APPLE1_FREERTOS_POSIX_SIM`.

| Command | Scheduler | Purpose |
|---------|-----------|---------|
| `make freertos` | none | Quick POSIX-adapter binary named `apple1_freertos` |
| `freertos_demo_test.sh` | FreeRTOS Posix_GCC | Scheduler integration test |

To run the scheduler integration test:

```bash
cd /path/to/FreeRTOS/FreeRTOS/Demo/Posix_GCC
export FREERTOS_DIR=/path/to/FreeRTOS/FreeRTOS
export APPLE1_DIR=/path/to/apple1-emu
bash /path/to/apple1-emu/freertos_demo_test.sh
./apple1_test
```

## Generic board port

Build the emulator with `-DAPPLE1_PORT_FREERTOS`, the FreeRTOS include paths,
and **without** `APPLE1_FREERTOS_POSIX_SIM`.  The generic port includes only
`FreeRTOS.h`, `task.h`, and `port_freertos_board.h`; it has no POSIX headers.

Your board must implement these functions declared in
[`port_freertos_board.h`](../port_freertos_board.h):

```c
int apple1_freertos_console_read_char(void);
void apple1_freertos_console_write(const char *buf, port_size_t n);
void apple1_freertos_console_flush(void);
```

`apple1_freertos_console_read_char()` must return `PORT_TERM_NODATA` when no
byte is waiting, `PORT_TERM_EOF` when input is closed, or an unsigned byte
value.  A UART interrupt queue is generally the best implementation.

The generic port supplies a no-filesystem VFS.  Before calling
`emulator_main()`, install a board VFS by assigning `g_port_vfs` to a
`struct port_vfs` for FatFs, littlefs, a ROM blob, or another storage backend.
If the application embeds all ROMs and does not load files, the default VFS is
safe to leave installed.

The generic port uses `pvPortMalloc()` and `vPortFree()`.  FreeRTOS has no
portable `realloc()`, so a nonzero resize returns `NULL` and preserves the old
allocation.  Enable the static ACI buffer or omit ACI when a board requires
recording cassette data.

`port_exit()` deletes the current FreeRTOS task; it never resets or halts the
board.  The application owns startup, reset policy, and scheduler startup.
