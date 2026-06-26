#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../bus.h"
#include "../term_apple1.h"

struct bus *g_bus = NULL;

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

void
term_run_config_wizard(void)
{
}

bool
term_is_powered(void)
{
	return (true);
}

void
term_trace_push(const char *line)
{
	(void)line;
}

bool
term_trace_active(void)
{
	return (false);
}

bool
term_should_step(void)
{
	return (false);
}

void
term_request_step(void)
{
}

bool
term_is_paused(void)
{
	return (false);
}

void
term_close_debugger(void)
{
}

void
term_open_debugger(void)
{
}
