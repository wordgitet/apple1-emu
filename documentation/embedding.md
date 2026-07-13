# Embedding and Custom Ports

The Apple-1 Emulator is designed to be easily embedded into custom environments, such as microcontrollers, bare-metal hardware, custom simulators, or proprietary operating systems, without modifying the core emulator repository files.

This is done by compiling with the preprocessor flags `-DAPPLE1_PORT_OTHER` and/or `-DAPPLE1_TERM_OTHER` and providing your own platform-specific implementations.

---

## Defining Custom Platform Ports (`APPLE1_PORT_OTHER`)

When `-DAPPLE1_PORT_OTHER` is defined, the default platform shims inside `port.c` are bypassed. You must implement and link the following portable system API functions (declared in `port.h`):

### Timing APIs
* `uint32_t port_gettime_us(void);`
  Returns a monotonic microsecond clock timestamp. Used for timing/throttling.
* `void port_sleep_us(uint32_t us);`
  Suspends execution for the specified number of microseconds.

### Terminal Raw Mode Control
* `void port_term_raw_enable(void);`
  Puts the host terminal/interface into raw/cbreak mode (no echo, pass control characters).
* `void port_term_raw_disable(void);`
  Restores the host terminal/interface to its original state.
* `void port_term_dbg_enable(void);`
  Configures the terminal for the debugger CLI (e.g. restoring echoing if needed).
* `void port_term_dbg_disable(void);`
  Restores the terminal from debugger configuration.

### Terminal I/O
* `int port_term_read_char(void);`
  Performs a non-blocking read of one byte from the input interface.
  Returns the byte on success, `PORT_TERM_NODATA` (a negative constant) if no key is pressed, or `PORT_TERM_EOF` if the input stream is closed/errored.
* `void port_term_write_buf(const char *buf, port_size_t n);`
  Writes `n` bytes of raw characters from `buf` to the terminal output.
* `void port_term_flush(void);`
  Flushes any buffered terminal output.
* `int port_term_read_line(char *buf, port_size_t size);`
  Performs a blocking read of a line of text (up to `size - 1` characters), NUL-terminating the result. Returns `1` on success, `0` on EOF/error.

### Process Exit & Strings
* `void port_exit(int code);`
  Terminates the program. If on a bare-metal environment, this can enter an infinite loop.
* `char *port_strdup(const char *str);`
  Copies a string into newly allocated memory. You can implement this yourself or reuse `port_string.c`.

### Storage / Virtual File System
* `extern struct port_vfs *g_port_vfs;`
  You must define and export this global pointer, pointing it to your `struct port_vfs` initialization structure (containing function pointers for `open`, `close`, `read`, `size`, `seek`, `write`, and `read_line` file operations).

---

## Defining Custom Terminal Drivers (`APPLE1_TERM_OTHER`)

When `-DAPPLE1_TERM_OTHER` is defined, the terminal backend selector in `term.c` is bypassed. You must implement the terminal interface (declared in `term_apple1.h`):

* `void term_init(void);`
  Called once during system startup to initialize terminal display structures.
* `void term_shutdown(void);`
  Called once on system shutdown to restore display state.
* `void term_write(uint8_t val);`
  Called by the emulated PIA display register to output a character.
* `bool term_dsp_ready(void);`
  Returns `true` if the display is ready to accept a new character.
* `uint8_t term_poll(void);`
  Polled periodically. Returns `0` if no input is ready, or the keycode (optionally with the high bit set, depending on the target Wozmon expectations) if a key was pressed.
* `bool term_reset_pending(void);`
  Returns `true` if a CPU reset key sequence (typically Ctrl+R) has been typed.

---

## Compiling Your Implementation

To compile the emulator with your custom port:

1. Compile the emulator source files (e.g. `main.c`, `cpu.c`, `bus.c`, `io.c`, `aci.c`, `term.c`, `port.c`, `cli_config.c`, `disasm.c`, `dbg.c`, `krusader.c`) with:
   ```bash
   cc -DAPPLE1_PORT_OTHER -DAPPLE1_TERM_OTHER ...
   ```
2. Link your own implementation file (e.g. `port_myrtos.c` or object files) that provides the missing `port_*` and `term_*` functions.
