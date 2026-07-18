/*
 * port_freertos_board.h - Board hooks for the generic FreeRTOS port.
 *
 * FreeRTOS provides scheduling and heap services, but deliberately does
 * not define console or filesystem APIs.  Each board supplies these three
 * console hooks.  Return PORT_TERM_NODATA when no input is available.
 *
 * Install a board VFS by assigning g_port_vfs before calling emulator_main().
 */
#ifndef PORT_FREERTOS_BOARD_H
#define PORT_FREERTOS_BOARD_H

#include "port.h"

int
apple1_freertos_console_read_char(void);
void
apple1_freertos_console_write(const char *buf, port_size_t n);
void
apple1_freertos_console_flush(void);

#endif /* PORT_FREERTOS_BOARD_H */
