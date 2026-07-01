# FreeRTOS POSIX simulator

Apple-1 can run as a FreeRTOS task in the official [Posix_GCC
simulator](https://github.com/FreeRTOS/FreeRTOS/tree/main/FreeRTOS/Demo/Posix_GCC)
on Linux or macOS.  This is the supported FreeRTOS target; bare-metal QEMU is
not maintained here.

## Two build paths

| Command | Scheduler | I/O | Purpose |
|---------|-----------|-----|---------|
| `make freertos` | none (plain process) | POSIX (`port_posix.c` via include) | quick local binary named `apple1_freertos` |
| `freertos_demo_test.sh` | FreeRTOS Posix port | POSIX + `FREERTOS_DEMO` heap/time | real scheduler integration test |

`port_freertos.c` includes `port_posix.c` and, when `FREERTOS_DEMO` is defined,
overrides only `port_malloc`, `port_free`, `port_realloc`, `port_gettime_us`,
`port_sleep_us`, and `port_exit`.

## Scheduler demo (recommended test)

```bash
cd /path/to/FreeRTOS/FreeRTOS/Demo/Posix_GCC
export FREERTOS_DIR=/path/to/FreeRTOS/FreeRTOS
export APPLE1_DIR=/path/to/apple1-emu
bash /path/to/apple1-emu/freertos_demo_test.sh
./apple1_test
```

The script links the FreeRTOS kernel, Posix port, `freertos_demo_main.c`, and
the emulator.  `main.c` is built as `emulator_main()` instead of `main()`.

## Local binary without the kernel

From the emulator tree:

```bash
make freertos
./apple1_freertos
```

This does **not** start the FreeRTOS scheduler; it only selects
`APPLE1_PORT_FREERTOS` (POSIX I/O, same as the normal Linux build).
