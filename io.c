#include "io.h"
#include "port.h"
#include "term_apple1.h"

#define KBD_BUFFER_SIZE 256

static uint8_t kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_read_idx = 0;
static int kbd_write_idx = 0;

void
io_init(void)
{
	term_init();
}

void
io_cleanup(void)
{
	term_shutdown();
}

bool
io_check_keyboard(void)
{
	/*
	 * Always call term_poll() so Ctrl-R / Ctrl-L side-effects are
	 * processed even when the PIA keyboard strobe is still set (i.e.
	 * the struct cpu hasn't consumed the previous keypress yet).  Only
	 * store the result if the buffer has space.
	 */
	uint8_t key = term_poll();
	if (key != 0) {
		int next_write = (kbd_write_idx + 1) % KBD_BUFFER_SIZE;
		if (next_write != kbd_read_idx) {
			kbd_buffer[kbd_write_idx] = key;
			kbd_write_idx = next_write;
		}
	}
	return (kbd_read_idx != kbd_write_idx);
}

uint8_t
io_read_keyboard(void)
{
	uint8_t key = 0;

	if (kbd_read_idx != kbd_write_idx) {
		key = kbd_buffer[kbd_read_idx];
		kbd_read_idx = (kbd_read_idx + 1) % KBD_BUFFER_SIZE;
	}
	return (key);
}

void
io_write_display(uint8_t value)
{
	term_write(value);
}

void
io_set_welcome(const char *msg1, const char *msg2)
{
	term_set_welcome(msg1, msg2);
}

bool
io_reset_pending(void)
{
	return (term_reset_pending());
}

void
io_reset(void)
{
	kbd_read_idx = 0;
	kbd_write_idx = 0;
}

bool
io_has_buffered_key(void)
{
	return (kbd_read_idx != kbd_write_idx);
}
