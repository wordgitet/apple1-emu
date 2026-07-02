#include "../bus.h"
#include "../port.h"
#include "../term_apple1.h"
#include <stddef.h>

struct bus *g_bus = NULL;
port_sig_flag g_quit_flag = 0;

void
dbg_log_append(const char *str)
{
	(void)str;
}

void
term_init(void)
{
}

void
term_shutdown(void)
{
}

void
term_write(uint8_t val)
{
	(void)val;
}

void
term_update(void)
{
}

uint8_t
term_poll(void)
{
	return (0);
}

void
term_set_welcome(const char *msg1, const char *msg2)
{
	(void)msg1;
	(void)msg2;
}

bool
term_reset_pending(void)
{
	return (false);
}

bool
term_dsp_ready(void)
{
	return (true);
}
