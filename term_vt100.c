/*
 * term_vt100.c - VT-100-style console backend (teletype, no full-screen ANSI).
 *
 * Strict VT-100/VT-220 terminals (and Plan 9 native windows) do not handle
 * term_ansi.c's home-cursor plus LF-only row redraw.  This driver keeps the
 * same vram model for the PIA but echoes Woz Monitor output as plain bytes
 * with CR/LF (teletype style).
 */
#include "port.h"
#include "term_apple1.h"

static uint8_t vram[24][40];
static int cursor_x = 0;
static int cursor_y = 0;
static bool reset_pending = false;

extern uint32_t opt_baud;

static uint32_t last_write_us = 0;

static void
scroll_up(void)
{
	int y;

	for (y = 0; y < 23; y++) {
		port_memcpy(vram[y], vram[y + 1], 40);
	}
	port_memset(vram[23], 0x20, 40);
}

static void
vt100_echo_char(char c)
{
	port_term_write_buf(&c, 1);
}

static void
vt100_echo_crlf(void)
{
	port_term_write_buf("\r\n", 2);
}

void
term_init(void)
{
	int x;
	int y;

	for (y = 0; y < 24; y++) {
		for (x = 0; x < 40; x++) {
			vram[y][x] = 0x20;
		}
	}
	cursor_x = 0;
	cursor_y = 0;
	vram[0][0] = 0x00;

	port_term_raw_enable();
	port_term_write_buf("Apple-1 emulator (VT-100 console)\r\n\r\n", 37);
}

void
term_shutdown(void)
{
	port_term_write_buf("\r\n", 2);
	port_term_raw_disable();
}

void
term_write(uint8_t val)
{
	char out;
	uint8_t glyph;

	val &= 0x7F;

	if (val == 0x0D || val == 0x0A) {
		if (val == 0x0D) {
			vram[cursor_y][cursor_x] = 0x20;
			cursor_x = 0;
			cursor_y++;
			if (cursor_y >= 24) {
				scroll_up();
				cursor_y = 23;
			}
			vram[cursor_y][cursor_x] = 0x00;
			vt100_echo_crlf();
		}
	} else if (val >= 0x20 && val <= 0x7E) {
		glyph = val;
		if (glyph >= 'a' && glyph <= 'z') {
			glyph -= 32;
		}
		vram[cursor_y][cursor_x] = glyph;
		out = (char)glyph;
		vt100_echo_char(out);
		cursor_x++;
		if (cursor_x >= 40) {
			cursor_x = 0;
			cursor_y++;
			if (cursor_y >= 24) {
				scroll_up();
				cursor_y = 23;
			}
			vt100_echo_crlf();
		}
		vram[cursor_y][cursor_x] = 0x00;
	} else if (val == 0x08) {
		if (cursor_x > 0) {
			cursor_x--;
			vram[cursor_y][cursor_x] = 0x20;
			vram[cursor_y][cursor_x + 1] = 0x00;
			vt100_echo_char('\b');
			vt100_echo_char(' ');
			vt100_echo_char('\b');
		}
	}

	if (opt_baud > 0) {
		last_write_us = port_gettime_us();
	}
}

bool
term_dsp_ready(void)
{
	uint32_t now;
	uint32_t window_us;

	if (opt_baud == 0) {
		return (true);
	}

	now = port_gettime_us();
	window_us = 10000000UL / opt_baud;
	return ((now - last_write_us) >= window_us);
}

void
term_update(void)
{
	/* Teletype mode: term_write already echoed output. */
}

uint8_t
term_poll(void)
{
	uint8_t ch;
	int c;

	ch = 0;
	c = port_term_read_char();
	if (c > 0) {
		ch = (uint8_t)c;
	}

	if (ch != 0) {
		if (ch == 0x03) {
			port_signal_quit();
			term_shutdown();
			return (0);
		}
		if (ch == 0x0C) {
			port_memset(vram, 0x20, sizeof(vram));
			cursor_x = 0;
			cursor_y = 0;
			vram[0][0] = 0x00;
			return (0);
		}
		if (ch == 0x12) {
			reset_pending = true;
			return (0);
		}
		if (ch >= 'a' && ch <= 'z') {
			ch -= 32;
		}
		return (ch | 0x80);
	}
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
	bool r;

	r = reset_pending;
	reset_pending = false;
	return (r);
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

bool
term_is_paused(void)
{
	return (false);
}

void
term_trace_push(const char *line)
{
	(void)line;
}

bool
term_should_step(void)
{
	return (false);
}

bool
term_trace_active(void)
{
	return (false);
}

void
term_request_step(void)
{
}

void
term_close_debugger(void)
{
}

void
term_open_debugger(void)
{
}
