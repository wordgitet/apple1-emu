#if defined(_WIN32) || defined(_WIN64)
#  include <conio.h>
#  include <windows.h>
#else
#  include <sys/select.h>
#  include <termios.h>
#  include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "term_apple1.h"

#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004

static uint8_t vram[24][40];
#if !defined(_WIN32) && !defined(_WIN64)
static struct termios orig_termios;
#else
static DWORD orig_console_mode;
#endif
static int cursor_x = 0;
static int cursor_y = 0;
static bool raw_mode_active = false;
static bool reset_pending = false;

/* Shared global authentic speed setting */
uint32_t opt_baud = 0;

static void
scroll_up(void)
{
	int y;

	for (y = 0; y < 23; y++) {
		memcpy(vram[y], vram[y + 1], 40);
	}
	memset(vram[23], 0x20, 40);
}

void
term_init(void)
{
	memset(vram, 0x20, sizeof(vram));
	cursor_x = 0;
	cursor_y = 0;

#if defined(_WIN32) || defined(_WIN64)
	HANDLE h_out;
	DWORD mode;

	h_out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h_out != INVALID_HANDLE_VALUE) {
		GetConsoleMode(h_out, &orig_console_mode);
		mode = orig_console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(h_out, mode);
	}
	raw_mode_active = true;
#else
	struct termios raw;

	if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
		raw = orig_termios;
		raw.c_lflag &= ~(ICANON | ECHO | ISIG);
		raw.c_iflag &= ~(IXON | ICRNL);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
			raw_mode_active = true;
		}
	}
#endif
	/* Hide cursor and clear physical screen */
	printf("\x1b[?25l\x1b[2J\x1b[H");
	fflush(stdout);
}

void
term_shutdown(void)
{
	/* Show cursor, clear screen, reset attributes */
	printf("\x1b[?25h\x1b[0m\x1b[2J\x1b[H");
	fflush(stdout);

	if (raw_mode_active == true) {
#if defined(_WIN32) || defined(_WIN64)
		HANDLE h_out;

		h_out = GetStdHandle(STD_OUTPUT_HANDLE);
		if (h_out != INVALID_HANDLE_VALUE) {
			SetConsoleMode(h_out, orig_console_mode);
		}
#else
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
#endif
		raw_mode_active = false;
	}
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
	} else if (val == 0x08 || val == 0x7F || val == 0x5F) {
		/* Backspace */
		if (cursor_x > 0) {
			vram[cursor_y][cursor_x] = 0x20;
			cursor_x--;
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
	}

	if (opt_baud > 0) {
		/* 10 bits per char, sleep accordingly */
		port_sleep_ns(10000000000ULL / opt_baud);
	}
}

void
term_update(void)
{
	int x, y;

	printf("\x1b[H");
	for (y = 0; y < 24; y++) {
		for (x = 0; x < 40; x++) {
			uint8_t c = vram[y][x];
			if (c == 0x00) {
				printf("\x1b[7m@\x1b[0m");
			} else {
				putchar(c);
			}
		}
		putchar('\n');
	}
	fflush(stdout);
}

uint8_t
term_poll(void)
{
	uint8_t ch;

	ch = 0;
#if defined(_WIN32) || defined(_WIN64)
	if (_kbhit() != 0) {
		ch = (uint8_t)_getch();
	}
#else
	char c;
	if (read(STDIN_FILENO, &c, 1) == 1) {
		ch = (uint8_t)c;
	}
#endif

	if (ch != 0) {
		if (ch == 0x12) {
			/* Ctrl-R (Reset) */
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
