/*
 * term_dos.c - MS-DOS terminal backend (conio / BIOS, no ANSI escapes).
 *
 * Renders the 40x24 Apple-1 screen with BIOS/conio calls so plain DOS
 * consoles and DOSBox work without ANSI.SYS.  Works with DJGPP and Open Watcom.
 */
#include "port.h"
#include "term_apple1.h"
#include <conio.h>
#if defined(__WATCOMC__)
#include <i86.h>
#else
#include <dos.h>
#endif

static uint8_t vram[24][40];
static int cursor_x = 0;
static int cursor_y = 0;
static bool reset_pending = false;

/* Shared global authentic speed setting - defined in main.c */
extern uint32_t opt_baud;

static uint32_t last_write_us = 0;

#if defined(__WATCOMC__) && !defined(_M_I86)
#define dos_int86(num, in, out) int386((num), (in), (out))
#else
#define dos_int86(num, in, out) int86((num), (in), (out))
#endif

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
dos_set_cursor_visible(int visible)
{
	union REGS regs;

	port_memset(&regs, 0, sizeof(regs));
	regs.h.ah = 0x01;
	if (visible != 0) {
		regs.h.ch = 0x06;
		regs.h.cl = 0x07;
	} else {
		regs.h.ch = 0x20;
		regs.h.cl = 0x00;
	}
	dos_int86(0x10, &regs, &regs);
}

static void
dos_goto(int x, int y)
{
	union REGS regs;

	port_memset(&regs, 0, sizeof(regs));
	regs.h.ah = 0x02;
	regs.h.bh = 0;
	regs.h.dh = (unsigned char)y;
	regs.h.dl = (unsigned char)x;
	dos_int86(0x10, &regs, &regs);
}

static void
dos_clrscr(void)
{
	union REGS regs;

	port_memset(&regs, 0, sizeof(regs));
	regs.h.ah = 0x06;
	regs.h.al = 0;
	regs.h.bh = 0x07;
	regs.h.ch = 0;
	regs.h.cl = 0;
	regs.h.dh = 24;
	regs.h.dl = 79;
	dos_int86(0x10, &regs, &regs);
	dos_goto(0, 0);
}

static void
dos_out_char(char c)
{
	union REGS regs;

	port_memset(&regs, 0, sizeof(regs));
	regs.h.ah = 0x0E;
	regs.h.al = (unsigned char)c;
	regs.h.bh = 0;
	dos_int86(0x10, &regs, &regs);
}

void
term_init(void)
{
	int x, y;

	for (y = 0; y < 24; y++) {
		for (x = 0; x < 40; x++) {
			vram[y][x] = ((x + y) & 1) ? 0x5F : 0x00;
		}
	}
	cursor_x = 0;
	cursor_y = 0;

	port_term_raw_enable();
	dos_clrscr();
	dos_set_cursor_visible(0);
}

void
term_shutdown(void)
{
	dos_set_cursor_visible(1);
	dos_clrscr();
	port_term_raw_disable();
}

void
term_write(uint8_t val)
{
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
		uint8_t glyph = val;

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
		/* Backspace - pass through to emulator, don't display locally */
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
	int x, y;
	uint32_t now;
	bool blink_on;

	now = port_gettime_us();
	blink_on = ((now / 250000UL) & 1) != 0;

	for (y = 0; y < 24; y++) {
		dos_goto(0, y);
		for (x = 0; x < 40; x++) {
			uint8_t c = vram[y][x];

			if (c == 0x00) {
				if (blink_on != 0) {
					dos_out_char('@');
				} else {
					dos_out_char(' ');
				}
			} else {
				dos_out_char((char)c);
			}
		}
	}
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
