#include "port.h"
#include "term_apple1.h"
#include "apple1limit.h"

/*
 * term_ansi.c - Hosted POSIX terminal (40x24 CRT via ANSI escapes).
 *
 * Woz writes update vram only; term_update() redraws the full screen at
 * ~30 Hz with a blinking '@' in empty cells (0x00), matching real Apple-1
 * power-on checkerboard and cursor behavior.
 */

static uint8_t vram[24][40];
static uint8_t shadow_vram[24][40];
static bool shadow_blink_on = false;
static bool shadow_valid = false;
static int cursor_x = 0;
static int cursor_y = 0;
static bool raw_mode_active = false;
static bool reset_pending = false;
#ifndef APPLE1_OMIT_PASTE
static bool bracketed_paste_active = false;
static int escape_seq_pos = 0;
static char escape_seq_buf[32];
static uint8_t paste_buffer[APPLE1_PASTE_BUFFER_SIZE];
static int paste_read_idx = 0;
static int paste_write_idx = 0;

static void
paste_buffer_clear(void)
{
	paste_read_idx = 0;
	paste_write_idx = 0;
}

static void
paste_buffer_put(uint8_t ch)
{
	int next;

	if (ch == 0x0A)
		ch = 0x0D;
	if (ch >= 'a' && ch <= 'z')
		ch -= 32;
	if (ch < 0x20 || ch > 0x7E)
		return;

	next = paste_write_idx + 1;
	if (next >= APPLE1_PASTE_BUFFER_SIZE)
		next = 0;
	if (next == paste_read_idx)
		return;

	paste_buffer[paste_write_idx] = ch;
	paste_write_idx = next;
}

static uint8_t
paste_buffer_get(void)
{
	uint8_t ch;

	if (paste_read_idx == paste_write_idx)
		return (0);

	ch = paste_buffer[paste_read_idx];
	paste_read_idx++;
	if (paste_read_idx >= APPLE1_PASTE_BUFFER_SIZE)
		paste_read_idx = 0;
	return (ch);
}
#endif

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
ansi_out(const char *buf, port_size_t len)
{
	port_term_write_buf(buf, len);
}

static void
term_clear_host_screen(void)
{
	int pos_len;
	int y;
	char pos[24];

	shadow_valid = false;

	ansi_out("\x1b[?25l\x1b[0m\x1b[2J\x1b[H", 14);
	for (y = 1; y <= 24; y++) {
		pos_len =
		    port_snprintf(pos, sizeof(pos), "\x1b[%d;1H\x1b[2K", y);
		ansi_out(pos, (port_size_t)pos_len);
	}
	ansi_out("\x1b[1;1H", 6);
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

#ifndef APPLE1_OMIT_PASTE
	paste_buffer_clear();
	bracketed_paste_active = false;
	escape_seq_pos = 0;
#endif

	port_term_raw_enable();
	raw_mode_active = true;

	term_clear_host_screen();
}

void
term_shutdown(void)
{
#if !defined(__PLAN9__) && !defined(__plan9__)
	term_clear_host_screen();
	port_term_write_buf("\x1b[?25h", 6);
#else
	port_term_write_buf("\r\n", 2);
#endif

	if (raw_mode_active != 0) {
		port_term_raw_disable();
		raw_mode_active = false;
	}
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

void
term_update(void)
{
	int pos_len;
	int x;
	int y;
	uint32_t now;
	bool blink_on;
	char line[41];
	char pos[16];

#ifndef APPLE1_OMIT_PASTE
	if (bracketed_paste_active != 0) {
		return;
	}
#endif

	now = port_gettime_us();
	blink_on = ((now / 250000UL) & 1) != 0;

	for (y = 0; y < 24; y++) {
		if (row_needs_repaint(y, blink_on) != 0) {
			pos_len = port_snprintf(pos, sizeof(pos), "\x1b[%d;1H", y + 1);
			ansi_out(pos, (port_size_t)pos_len);
			for (x = 0; x < 40; x++) {
				uint8_t c;

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
			ansi_out(line, 40);
			port_memcpy(shadow_vram[y], vram[y], 40);
		}
	}

	shadow_blink_on = blink_on;
	shadow_valid = true;
	port_term_flush();
}

uint8_t
term_poll(void)
{
	uint8_t ch;
	int c;

#ifndef APPLE1_OMIT_PASTE
	ch = paste_buffer_get();
	if (ch != 0)
		return (ch | 0x80);
#endif

	ch = 0;
	c = port_term_read_char();
	if (c > 0) {
		ch = (uint8_t)c;
	}

	if (ch != 0) {
#ifndef APPLE1_OMIT_PASTE
		if (ch == 0x1B) {
			escape_seq_pos = 0;
			escape_seq_buf[escape_seq_pos++] = (char)ch;
			return (0);
		}
		if (escape_seq_pos > 0) {
			escape_seq_buf[escape_seq_pos++] = (char)ch;
			if (escape_seq_pos >= 6) {
				if (port_strcmp(escape_seq_buf, "\x1b[200~") ==
				    0) {
					paste_buffer_clear();
					bracketed_paste_active = true;
				}
				if (port_strcmp(escape_seq_buf, "\x1b[201~") ==
				    0) {
					bracketed_paste_active = false;
				}
				escape_seq_pos = 0;
			}
			return (0);
		}
#endif

		if (ch == 0x03) {
			port_signal_quit();
			term_shutdown();
			return (0);
		}
#ifndef APPLE1_OMIT_PASTE
		if (bracketed_paste_active != 0) {
			paste_buffer_put(ch);
			return (0);
		}
#endif
		if (ch == 0x0A) {
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
