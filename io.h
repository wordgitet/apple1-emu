#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stdint.h>

// Initialize terminal to raw mode
void
io_init(void);

// Restore terminal settings to original state
void
io_cleanup(void);

// Check if keyboard input is available (non-blocking)
bool
io_check_keyboard(void);

// Read a filtered character from keyboard (auto-uppercases, handles Enter/CR, returns with bit 7 set)
uint8_t
io_read_keyboard(void);

// Write character to display (interprets bit 7 and handles carriage returns)
void
io_write_display(uint8_t value);

// Set welcome messages to display at the bottom of the alternate screen
void
io_set_welcome(const char *msg1, const char *msg2);

// Returns true (once) if Ctrl-R was pressed — caller should call cpu_reset()
bool
io_reset_pending(void);

// Reset keyboard buffer
void
io_reset(void);

#endif // IO_H
