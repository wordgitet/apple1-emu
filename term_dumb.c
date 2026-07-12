/*
 * term_dumb.c - Dumb/teletype terminal backend.
 *
 * No vram buffer, no ANSI escapes, no periodic full-screen redraws.
 * Characters are written directly to stdout as the 6502 outputs them.
 * Suitable for slow terminals, record-oriented I/O systems (e.g.
 * OpenVMS), or any host where escape-sequence rendering is too costly.
 *
 * The real Apple-1 only outputs CR (0x0D) as its line terminator.
 * We expand CR -> CR+LF so it renders correctly on modern terminals.
 */

#include "port.h"
#include "term_apple1.h"

static bool reset_pending = false;

void
term_init(void)
{
	port_term_raw_enable();
	reset_pending = true;
	/* Print a blank line so output starts clean. */
	port_term_write_buf("\r\n", 2);
	port_term_flush();
}

void
term_shutdown(void)
{
	port_term_write_buf("\r\n", 2);
	port_term_flush();
	port_term_raw_disable();
}

void
term_write(uint8_t val)
{
	char buf[2];
	int len;

	val &= 0x7F;

	if (val == 0x0D) {
		/* OpenVMS C RTL translates \n to CRLF for records automatically. */
		buf[0] = '\n';
		len = 1;
	} else if (val >= 0x20 && val <= 0x7E) {
		buf[0] = (char)val;
		len = 1;
	} else {
		return;
	}

	port_term_write_buf(buf, (port_size_t)len);
	port_term_flush();
}

bool
term_dsp_ready(void)
{
	return (true);
}

void
term_update(void)
{
	/* Nothing to redraw in dumb mode. */
}

uint8_t
term_poll(void)
{
	uint8_t ch;
	int c;

	c = port_term_read_char();
	if (c <= 0) {
		return (0);
	}
	ch = (uint8_t)c;

	if (ch == 0x03) {
		port_signal_quit();
		term_shutdown();
		return (0);
	}
	if (ch == 0x0A) {
		/* Modern terminals send LF; Woz Monitor expects CR. */
		ch = 0x0D;
	}
	if (ch == 0x12 || ch == 0x14) { /* Ctrl+R or Ctrl+T resets */
		reset_pending = true;
		return (0);
	}
	if (ch >= 'a' && ch <= 'z') {
		ch -= 32;
	}
	return (ch | 0x80);
}

bool
term_reset_pending(void)
{
	bool r;

	r = reset_pending;
	reset_pending = false;
	return (r);
}
