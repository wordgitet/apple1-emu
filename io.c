#include "io.h"
#include "port.h"
#include "term_apple1.h"

static uint8_t buffered_key = 0;

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
	 * store the result if the buffer is empty.
	 */
	uint8_t key = term_poll();
	if (buffered_key == 0) {
		buffered_key = key;
	}
	return (buffered_key != 0);
}

uint8_t
io_read_keyboard(void)
{
	uint8_t key = buffered_key;

	buffered_key = 0;
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
	buffered_key = 0;
}

bool
io_has_buffered_key(void)
{
	return (buffered_key != 0);
}
