/*
 * term_vt100.c - VT-100-style console backend (40x24 CRT, minimal escapes).
 *
 * Same vram model as term_ansi.c: Woz writes update vram only and
 * term_update() redraws with a blinking '@' in empty cells.  Rows are
 * painted with CSI cursor addressing (ESC[row;1H) and no LF — the real
 * Apple-1 display is a fixed matrix, not a scrolling teletype.
 */
#include "port.h"
#include "term_apple1.h"

static uint8_t vram[24][40];
static uint8_t shadow_vram[24][40];
static bool shadow_blink_on = false;
static bool shadow_valid = false;
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
term_clear_host_screen(void)
{
	int pos_len;
	int y;
	char pos[24];

	shadow_valid = false;

#ifdef __PLAN9__
	/* Native console: no CSI clear. */
#else
#ifdef __plan9__
	/* Native console: no CSI clear. */
#else
	port_term_write_buf("\x1b[?25l\x1b[0m\x1b[2J\x1b[H", 14);
	for (y = 1; y <= 24; y++) {
		pos_len =
		    port_snprintf(pos, sizeof(pos), "\x1b[%d;1H\x1b[2K", y);
		port_term_write_buf(pos, (port_size_t)pos_len);
	}
	port_term_write_buf("\x1b[1;1H", 6);
#endif
#endif
}

static void
term_paint_row_plan9(int y, bool blink_on)
{
	int x;
	char line[41];
	uint8_t c;

	for (x = 0; x < 40; x++) {
		c = vram[y][x];
		if (c == 0x00) {
			if (blink_on != 0) {
				line[x] = '@';
			} else {
				line[x] = ' ';
			}
		} else {
			line[x] = (char)c;
		}
	}
	port_term_write_buf("\r", 1);
	port_term_write_buf(line, 40);
	port_term_write_buf("\r\n", 2);
}

static void
term_paint_row_csi(int y, bool blink_on)
{
	int pos_len;
	int x;
	char line[41];
	char pos[16];
	uint8_t c;

	pos_len = port_snprintf(pos, sizeof(pos), "\x1b[%d;1H", y + 1);
	port_term_write_buf(pos, (port_size_t)pos_len);
	for (x = 0; x < 40; x++) {
		c = vram[y][x];
		if (c == 0x00) {
			if (blink_on != 0) {
				line[x] = '@';
			} else {
				line[x] = ' ';
			}
		} else {
			line[x] = (char)c;
		}
	}
	port_term_write_buf(line, 40);
}

static void
term_paint_row(int y, bool blink_on)
{
#ifdef __PLAN9__
	term_paint_row_plan9(y, blink_on);
#else
#ifdef __plan9__
	term_paint_row_plan9(y, blink_on);
#else
	term_paint_row_csi(y, blink_on);
#endif
#endif
}

static bool
row_needs_repaint(int y, bool blink_on)
{
	int x;
	bool has_blinking;

	has_blinking = false;
	if (shadow_valid == 0) {
		return (true);
	}
	for (x = 0; x < 40; x++) {
		if (vram[y][x] != shadow_vram[y][x]) {
			return (true);
		}
		if (vram[y][x] == 0x00 || shadow_vram[y][x] == 0x00) {
			has_blinking = true;
		}
	}
	if (has_blinking != 0 && blink_on != shadow_blink_on) {
		return (true);
	}
	return (false);
}

static void
term_redraw(void)
{
	int y;
	uint32_t now;
	bool blink_on;

#ifdef __PLAN9__
	port_term_write_buf("\x1b[24A", 4);
#else
#ifdef __plan9__
	port_term_write_buf("\x1b[24A", 4);
#endif
#endif

	now = port_gettime_us();
	blink_on = ((now / 250000UL) & 1) != 0;

	for (y = 0; y < 24; y++) {
#ifdef __PLAN9__
		term_paint_row(y, blink_on);
#else
#ifdef __plan9__
		term_paint_row(y, blink_on);
#else
		if (row_needs_repaint(y, blink_on) != 0) {
			term_paint_row(y, blink_on);
			port_memcpy(shadow_vram[y], vram[y], 40);
		}
#endif
#endif
	}

	shadow_blink_on = blink_on;
	shadow_valid = true;
}

void
term_init(void)
{
	int x;
	int y;

	for (y = 0; y < 24; y++) {
		for (x = 0; x < 40; x++) {
			vram[y][x] = ((x + y) & 1) ? 0x5F : 0x00;
		}
	}
	cursor_x = 0;
	cursor_y = 0;

	port_term_raw_enable();
	term_clear_host_screen();
	term_redraw();
}

void
term_shutdown(void)
{
#ifdef __PLAN9__
	port_term_write_buf("\r\n", 2);
#else
#ifdef __plan9__
	port_term_write_buf("\r\n", 2);
#else
	term_clear_host_screen();
	port_term_write_buf("\x1b[?25h", 6);
#endif
#endif
	port_term_raw_disable();
}

void
term_write(uint8_t val)
{
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
		}
	} else if (val >= 0x20 && val <= 0x7E) {
		glyph = val;
		if (glyph >= 'a' && glyph <= 'z') {
			glyph -= 32;
		}
		vram[cursor_y][cursor_x] = glyph;
		cursor_x++;
		if (cursor_x >= 40) {
			cursor_x = 0;
			cursor_y++;
			if (cursor_y >= 24) {
				scroll_up();
				cursor_y = 23;
			}
		}
		vram[cursor_y][cursor_x] = 0x00;
	} else if (val == 0x08 || val == 0x7F || val == 0x5F) {
		/* Backspace - pass through to emulator, don't display locally. */
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
	term_redraw();
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
		if (ch == 0x0A) {
			/* Woz Monitor expects CR; modern terminals send LF. */
			ch = 0x0D;
		}
		if (ch == 0x0C) {
			port_memset(vram, 0x20, sizeof(vram));
			cursor_x = 0;
			cursor_y = 0;
			vram[0][0] = 0x00;
			return (0);
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
	return (0);
}

bool
term_reset_pending(void)
{
	bool r;

	r = reset_pending;
	reset_pending = false;
	return (r);
}
