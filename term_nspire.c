/*
 * term_nspire.c - TI-Nspire raw-LCD terminal backend for the Apple-1 emulator.
 *
 * Renders characters into a local 16-bit RGB565 buffer, then blits to the screen
 * using the official Ndless lcd_blit() API.  This prevents Data aborts on newer
 * hardware (like the CX II) where direct register VRAM writing is protected.
 *
 * Console geometry: 40 cols x 20 rows
 *   Width : 40 * 6 = 240 px, left margin = (320-240)/2 = 40 px
 *   Height: 20 * 8 = 160 px, top  margin = (240-160)/2 = 40 px
 *
 * Blank screen at launch; Wozmon drives the display.  Cursor cells (0x00)
 * blink as '@' via term_update().
 */

#include "port.h"
#include "term_apple1.h"
#include "font5x7.h"

#include <libndls.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t opt_baud;

static uint32_t last_write_us;

/* ------------------------------------------------------------------ */
/* Console geometry                                                     */
/* ------------------------------------------------------------------ */

#define TERM_COLS  40
#define TERM_ROWS  20
#define CELL_W     6    /* 5-px glyph + 1-px gap */
#define CELL_H     8    /* 7-px glyph + 1-px gap */
#define MARGIN_X   ((SCREEN_WIDTH  - TERM_COLS * CELL_W) / 2)  /* = 40 */
#define MARGIN_Y   ((SCREEN_HEIGHT - TERM_ROWS * CELL_H) / 2)  /* = 40 */

/* ------------------------------------------------------------------ */
/* Apple-1 display VRAM (PIA-driven)                                    */
/* ------------------------------------------------------------------ */

static uint8_t vram[TERM_ROWS][TERM_COLS];
static int cursor_x;
static int cursor_y;
static uint16_t *g_screen_buf;
static uint8_t g_blink_phase;

#define NSPIRE_KEY_SLOTS 80
static bool g_prev[NSPIRE_KEY_SLOTS];

/* ------------------------------------------------------------------ */
/* Pixel helpers                                                        */
/* ------------------------------------------------------------------ */

static void
put_pixel(int x, int y, int fg)
{
	if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
		return;

	if (g_screen_buf != NULL)
		g_screen_buf[y * SCREEN_WIDTH + x] = fg ? 0xFFFFu : 0x0000u;
}

static void
draw_glyph(int px, int py, unsigned char ch)
{
	int col;
	int row;
	int bidx;
	uint8_t col_data;

	if (ch < 0x20u || ch > 0x7Fu)
		ch = 0x20u;

	bidx = (int)(ch - 0x20u) * 5;

	for (col = 0; col < 5; col++) {
		col_data = Font5x7[bidx + col];
		for (row = 0; row < 7; row++)
			put_pixel(px + col, py + row, (col_data >> row) & 1);
		put_pixel(px + col, py + 7, 0);
	}
	for (row = 0; row < CELL_H; row++)
		put_pixel(px + 5, py + row, 0);
}

static void
erase_cell_px(int col, int row)
{
	int px;
	int py;
	int x;
	int y;

	px = MARGIN_X + col * CELL_W;
	py = MARGIN_Y + row * CELL_H;
	for (y = py; y < py + CELL_H; y++)
		for (x = px; x < px + CELL_W; x++)
			put_pixel(x, y, 0);
}

static void
scroll_up(void)
{
	int row;
	int y;
	int col;

	for (row = 0; row < TERM_ROWS - 1; row++) {
		int src_y;
		int dst_y;

		src_y = MARGIN_Y + (row + 1) * CELL_H;
		dst_y = MARGIN_Y + row * CELL_H;
		for (y = 0; y < CELL_H; y++) {
			memmove(
			    &g_screen_buf[(dst_y + y) * SCREEN_WIDTH + MARGIN_X],
			    &g_screen_buf[(src_y + y) * SCREEN_WIDTH + MARGIN_X],
			    (size_t)(TERM_COLS * CELL_W * sizeof(uint16_t)));
		}
		port_memcpy(&vram[row][0], &vram[row + 1][0], (size_t)TERM_COLS);
	}

	for (col = 0; col < TERM_COLS; col++) {
		vram[TERM_ROWS - 1][col] = 0x20;
		erase_cell_px(col, TERM_ROWS - 1);
	}
}

static void
term_redraw(void)
{
	int x;
	int y;
	bool blink_on;
	unsigned char ch;

	if (g_screen_buf == NULL)
		return;

	blink_on = (g_blink_phase != 0);

	for (y = 0; y < TERM_ROWS; y++) {
		for (x = 0; x < TERM_COLS; x++) {
			ch = (unsigned char)vram[y][x];
			if (ch == 0x00) {
				if (blink_on != 0)
					ch = '@';
				else
					ch = ' ';
			}
			draw_glyph(MARGIN_X + x * CELL_W,
			    MARGIN_Y + y * CELL_H,
			    ch);
		}
	}
	lcd_blit(g_screen_buf, SCR_320x240_565);
}

static void
term_blank_init(void)
{
	int r;
	int c;

	for (r = 0; r < TERM_ROWS; r++) {
		for (c = 0; c < TERM_COLS; c++)
			vram[r][c] = 0x20;
	}
	cursor_x = 0;
	cursor_y = 0;
	vram[0][0] = 0x00;
}

void
term_cold_boot(void)
{
	int i;

	for (i = 0; i < NSPIRE_KEY_SLOTS; i++)
		g_prev[i] = false;
	term_blank_init();
	g_blink_phase = 0;
	term_redraw();
}

/* ------------------------------------------------------------------ */
/* Term API                                                             */
/* ------------------------------------------------------------------ */

void
port_term_init(void)
{
	cursor_x = 0;
	cursor_y = 0;

	lcd_init(lcd_type());

	if (g_screen_buf == NULL)
		g_screen_buf = (uint16_t *)malloc(
		    SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));

	if (g_screen_buf != NULL) {
		port_memset(g_screen_buf,
		    0,
		    SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
		term_cold_boot();
	}
}

void
port_term_clear(void)
{
	int col;
	int row;

	for (row = 0; row < TERM_ROWS; row++) {
		for (col = 0; col < TERM_COLS; col++)
			vram[row][col] = 0x20;
	}
	cursor_x = 0;
	cursor_y = 0;
	vram[0][0] = 0x00;
	term_redraw();
}

void
port_term_write_char(char c)
{
	/*
	 * Direct draw for fatal/error strings only (before the CPU loop).
	 * Normal emulation output goes through term_write() -> vram.
	 */
	if (c == '\r' || c == '\n') {
		cursor_x = 0;
		cursor_y++;
		if (cursor_y >= TERM_ROWS) {
			scroll_up();
			cursor_y = TERM_ROWS - 1;
		}
		term_redraw();
		return;
	}

	if (c >= 0x20 && c <= 0x7E) {
		vram[cursor_y][cursor_x] = (uint8_t)c;
		cursor_x++;
		if (cursor_x >= TERM_COLS) {
			cursor_x = 0;
			cursor_y++;
			if (cursor_y >= TERM_ROWS) {
				scroll_up();
				cursor_y = TERM_ROWS - 1;
			}
		}
		term_redraw();
	}
}

void
port_term_write_buf(const char *buf, port_size_t len)
{
	port_size_t i;

	for (i = 0; i < len; i++)
		port_term_write_char(buf[i]);
}

/*
 * port_term_read_char -- non-blocking keyboard poll (edge-triggered).
 */

static int
nspire_key_edge(const t_key *key, int idx, int ch)
{
	bool now;

	if (idx < 0 || idx >= NSPIRE_KEY_SLOTS)
		return (-1);

	now = isKeyPressed(*key) != 0;
	if (now != 0 && g_prev[idx] == 0) {
		g_prev[idx] = true;
		return (ch);
	}
	if (now == 0)
		g_prev[idx] = false;
	return (-1);
}

static int
nspire_shift_key(const t_key *key, int idx, int plain, int shifted)
{
	bool shift;

	shift = isKeyPressed(KEY_NSPIRE_SHIFT) != 0;
	return (nspire_key_edge(key, idx, shift != 0 ? shifted : plain));
}

static int
nspire_shift_ctrl_key(const t_key *key,
    int idx,
    int plain,
    int shifted,
    int ctrl_ch)
{
	bool ctrl;
	bool shift;

	ctrl = isKeyPressed(KEY_NSPIRE_CTRL) != 0;
	if (ctrl != 0)
		return (nspire_key_edge(key, idx, ctrl_ch));
	shift = isKeyPressed(KEY_NSPIRE_SHIFT) != 0;
	return (nspire_key_edge(key, idx, shift != 0 ? shifted : plain));
}

int
port_term_read_char(void)
{
	int ch;

	if (isKeyPressed(KEY_NSPIRE_ESC) != 0)
		return (-1);
	if (isKeyPressed(KEY_NSPIRE_DOC) != 0)
		return (-1);
	if (is_classic != 0 && isKeyPressed(KEY_NSPIRE_HOME) != 0)
		return (-1);

	ch = nspire_key_edge(&KEY_NSPIRE_A, 0, 'A');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_B, 1, 'B');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_C, 2, 'C');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_D, 3, 'D');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_E, 4, 'E');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_F, 5, 'F');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_G, 6, 'G');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_H, 7, 'H');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_I, 8, 'I');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_J, 9, 'J');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_K, 10, 'K');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_L, 11, 'L');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_M, 12, 'M');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_N, 13, 'N');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_O, 14, 'O');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_P, 15, 'P');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_Q, 16, 'Q');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_R, 17, 'R');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_S, 18, 'S');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_T, 19, 'T');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_U, 20, 'U');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_V, 21, 'V');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_W, 22, 'W');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_X, 23, 'X');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_Y, 24, 'Y');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_Z, 25, 'Z');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_0, 26, '0');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_1, 27, '1');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_2, 28, '2');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_3, 29, '3');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_4, 30, '4');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_5, 31, '5');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_6, 32, '6');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_7, 33, '7');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_8, 34, '8');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_9, 35, '9');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_RET, 36, '\r');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_ENTER, 37, '\r');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_SPACE, 38, ' ', '_');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_PERIOD, 39, '.', ':');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_COMMA, 40, ',', ';');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_PLUS, 41, '+', '>');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_MINUS, 42, '-', '<');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_MULTIPLY, 43, '*', '"');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_DIVIDE, 44, '/', '\\');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_LP, 45, '(', '[');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_RP, 46, ')', ']');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_NEGATIVE, 47, '-');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_EQU, 48, '=', '|');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_APOSTROPHE, 49, '\'');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_QUOTE, 50, '"');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_QUES, 51, '?', '!');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_BAR, 52, '|');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_EXP, 53, '^');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_LTHAN, 54, '<');
	if (ch >= 0)
		return (ch);
	ch = nspire_key_edge(&KEY_NSPIRE_GTHAN, 55, '>');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_ctrl_key(&KEY_NSPIRE_EE, 56, '&', '%', '@');
	if (ch >= 0)
		return (ch);
	ch = nspire_shift_key(&KEY_NSPIRE_TAB, 57, '\t', '\t');
	if (ch >= 0)
		return (ch);

	return (-1);
}

/* ================================================================== */
/* High-level term_* API expected by core emulator                    */
/* ================================================================== */

void
term_init(void)
{
	port_term_init();
}

void
term_shutdown(void)
{
	if (g_screen_buf != NULL) {
		free(g_screen_buf);
		g_screen_buf = NULL;
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
			if (cursor_y >= TERM_ROWS) {
				scroll_up();
				cursor_y = TERM_ROWS - 1;
			}
			vram[cursor_y][cursor_x] = 0x00;
		}
	} else if (val >= 0x20 && val <= 0x7E) {
		glyph = val;
		if (glyph >= 'a' && glyph <= 'z')
			glyph -= 32;
		vram[cursor_y][cursor_x] = glyph;
		cursor_x++;
		if (cursor_x >= TERM_COLS) {
			cursor_x = 0;
			cursor_y++;
			if (cursor_y >= TERM_ROWS) {
				scroll_up();
				cursor_y = TERM_ROWS - 1;
			}
		}
		vram[cursor_y][cursor_x] = 0x00;
	} else if (val == 0x08 || val == 0x7F || val == 0x5F) {
		/* Rubout — Wozmon drives the display; no local backspace. */
		return;
	}

	if (opt_baud > 0)
		last_write_us = port_gettime_us();

	term_redraw();
}

int
term_dsp_ready(void)
{
	uint32_t now;
	uint32_t window_us;

	if (opt_baud == 0)
		return (1);

	now = port_gettime_us();
	window_us = 10000000UL / opt_baud;
	return ((now - last_write_us) >= window_us);
}

void
term_update(void)
{
	g_blink_phase ^= 1;
	term_redraw();
}

uint8_t
term_poll(void)
{
	uint8_t ch;
	int c;

	ch = 0;
	c = port_term_read_char();
	if (c > 0)
		ch = (uint8_t)c;

	if (ch != 0) {
		if (ch == 0x0A)
			ch = 0x0D;
		if (ch >= 'a' && ch <= 'z')
			ch -= 32;
		return (ch | 0x80);
	}
	return (0);
}

int
term_reset_pending(void)
{
	return (0);
}
