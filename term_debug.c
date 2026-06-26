#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disasm.h"
#include "font5x7.h"
#include "term_apple1.h"
#include "term_debug.h"
#include "term_internal.h"

/* ── FONT5X7 MONO RENDERER ───────────────────────────────────────────────── */
#define MONO_SCALE 2
#define MONO_CW	   6		    /* (5 cols * scale) + 1px gap */
#define MONO_CH	   (7 * MONO_SCALE) /* 14px */

static void
draw_mono_char(SDL_Renderer *rend, char c, int x, int y, SDL_Color col)
{
	const unsigned char *glyph;
	int col_i;
	int row;
	uint8_t colbits;
	SDL_FRect px;

	if (c < 0x20 || c > 0x7E)
		c = '?';
	glyph = &Font5x7[(c - 0x20) * 5];
	SDL_SetRenderDrawColor(rend, col.r, col.g, col.b, col.a);
	for (col_i = 0; col_i < 5; col_i++) {
		colbits = glyph[col_i];
		for (row = 0; row < 7; row++) {
			if ((colbits & (1 << row)) != 0) {
				px.x = (float)(x + col_i * MONO_SCALE);
				px.y = (float)(y + row * MONO_SCALE);
				px.w = (float)MONO_SCALE;
				px.h = (float)MONO_SCALE;
				SDL_RenderFillRect(rend, &px);
			}
		}
	}
}

static void
draw_mono_str(SDL_Renderer *rend, const char *s, int x, int y, SDL_Color col)
{
	while (*s != '\0') {
		draw_mono_char(rend, *s++, x, y, col);
		x += MONO_CW * MONO_SCALE;
	}
}

/* ── TRACE RING BUFFER STATE ──────────────────────────────────────────────── */
#define TRACE_DEFAULT_MAX 5000
#define TRACE_LINE_LEN	  160

struct trace_line {
	char line[TRACE_LINE_LEN];
};

static struct trace_line *trace_buf = NULL;
static int trace_max = TRACE_DEFAULT_MAX;
static int trace_head = 0;
static int trace_count = 0;
static bool trace_window_open = false;
static SDL_Window *trace_win = NULL;
static SDL_Renderer *trace_ren = NULL;
static int trace_scroll = 0;
static bool trace_frozen = false;

/* ── DEBUG WINDOW STATE ───────────────────────────────────────────────────── */
static bool debug_window_open = false;
static SDL_Window *debug_win = NULL;
static SDL_Renderer *debug_ren = NULL;

#define DBG_WIN_LINES 40
#define DBG_WIN_COLS  100
static char dbg_output[DBG_WIN_LINES][DBG_WIN_COLS];
static int dbg_num_lines = 0;
static char dbg_input_buf[256];
static int dbg_input_len = 0;
static bool dbg_needs_step = false;

extern debugger_t *g_dbg;

/* ── LOGGER & HELPERS ────────────────────────────────────────────────────── */

void
dbg_log_append(const char *str)
{
	const char *p;
	const char *nl;
	int seg;
	int col;
	int room;
	int copy;

	p = str;
	while (*p != '\0') {
		nl = strchr(p, '\n');
		seg = (nl != NULL) ? (int)(nl - p) : (int)strlen(p);
		if (dbg_num_lines < DBG_WIN_LINES) {
			col = (int)strlen(dbg_output[dbg_num_lines]);
			room = DBG_WIN_COLS - 1 - col;
			if (room > 0) {
				copy = seg < room ? seg : room;
				memcpy(dbg_output[dbg_num_lines] + col,
				    p,
				    copy);
				dbg_output[dbg_num_lines][col + copy] = '\0';
			}
			if (nl != NULL) {
				dbg_num_lines++;
				if (dbg_num_lines < DBG_WIN_LINES)
					dbg_output[dbg_num_lines][0] = '\0';
			}
		}
		p += seg + ((nl != NULL) ? 1 : 0);
		if (nl == NULL)
			break;
	}
}

/* ── EXPORTED API IMPLEMENTATIONS ────────────────────────────────────────── */

void
term_debug_init(void)
{
	/* Handled dynamically as windows open */
}

void
term_debug_shutdown(void)
{
	if (debug_ren != NULL) {
		SDL_DestroyRenderer(debug_ren);
		debug_ren = NULL;
	}
	if (debug_win != NULL) {
		SDL_DestroyWindow(debug_win);
		debug_win = NULL;
	}
	if (trace_ren != NULL) {
		SDL_DestroyRenderer(trace_ren);
		trace_ren = NULL;
	}
	if (trace_win != NULL) {
		SDL_DestroyWindow(trace_win);
		trace_win = NULL;
	}
	if (trace_buf != NULL) {
		free(trace_buf);
		trace_buf = NULL;
	}
}

void
term_debug_toggle(void)
{
	char dis[64];
	char hdr[128];

	if (debug_window_open == true) {
		if (debug_ren != NULL) {
			SDL_DestroyRenderer(debug_ren);
			debug_ren = NULL;
		}
		if (debug_win != NULL) {
			SDL_DestroyWindow(debug_win);
			debug_win = NULL;
		}
		debug_window_open = false;
		g_debug_enabled = false;
		if (g_dbg != NULL)
			g_dbg->step_mode = false;
	} else {
		debug_win =
		    SDL_CreateWindow("Apple-1 Debugger  [db> ]", 900, 600, 0);
		if (debug_win == NULL)
			return;
		debug_ren = SDL_CreateRenderer(debug_win, NULL);
		if (debug_ren == NULL) {
			SDL_DestroyWindow(debug_win);
			debug_win = NULL;
			return;
		}
		SDL_StartTextInput(debug_win);
		debug_window_open = true;
		g_debug_enabled = true;
		dbg_num_lines = 0;
		memset(dbg_output, 0, sizeof(dbg_output));
		dbg_input_len = 0;
		dbg_input_buf[0] = '\0';
		if (g_dbg != NULL) {
			g_dbg->step_mode = true;
			/* Print initial state */
			cpu_disassemble(g_dbg->cpu->bus, g_dbg->cpu->pc, dis);
			snprintf(hdr,
			    sizeof(hdr),
			    "PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X  "
			    "%s",
			    g_dbg->cpu->pc,
			    g_dbg->cpu->a,
			    g_dbg->cpu->x,
			    g_dbg->cpu->y,
			    g_dbg->cpu->s,
			    g_dbg->cpu->p,
			    dis);
			dbg_log_append(hdr);
			dbg_log_append("\n");
		}
	}
}

bool
term_debug_is_open(void)
{
	return (debug_window_open);
}

void
term_debug_render(void)
{
	SDL_Color bg = { 8, 8, 10, 255 };
	SDL_Color fg = { 51, 255, 51, 255 };
	SDL_Color dim = { 30, 140, 30, 255 };
	SDL_Color prompt = { 255, 176, 0, 255 };
	int x0;
	int y0;
	int line_h;
	int max_visible;
	int start;
	int i;
	int py;
	SDL_FRect pbar;

	if (debug_ren == NULL)
		return;

	SDL_SetRenderDrawColor(debug_ren, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(debug_ren);

	x0 = 8;
	y0 = 8;
	line_h = MONO_CH + 4;
	max_visible = (600 - 60) / line_h;
	start = dbg_num_lines > max_visible ? dbg_num_lines - max_visible
					    : 0;
	for (i = start; i < dbg_num_lines; i++) {
		draw_mono_str(debug_ren,
		    dbg_output[i],
		    x0,
		    y0 + (i - start) * line_h,
		    dim);
	}

	/* Prompt line at bottom */
	py = 600 - 48;
	SDL_SetRenderDrawColor(debug_ren, 18, 18, 22, 255);
	pbar.x = 0.0f;
	pbar.y = (float)py - 4;
	pbar.w = 900.0f;
	pbar.h = (float)(MONO_CH + 12);
	SDL_RenderFillRect(debug_ren, &pbar);

	draw_mono_str(debug_ren, "db> ", x0, py, prompt);
	draw_mono_str(debug_ren,
	    dbg_input_buf,
	    x0 + 4 * MONO_CW * MONO_SCALE,
	    py,
	    fg);

	SDL_RenderPresent(debug_ren);
}

bool
term_debug_is_window(SDL_Window *win)
{
	return (win != NULL && win == debug_win);
}

void
term_debug_handle_event(const SDL_Event *ev)
{
	const char *t;
	SDL_Keycode k;
	char echo[260];
	char cmd_copy[256];

	if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		term_debug_toggle();
		return;
	}
	if (ev->type == SDL_EVENT_TEXT_INPUT) {
		t = ev->text.text;
		while (*t != '\0' && dbg_input_len < 255) {
			dbg_input_buf[dbg_input_len++] = *t++;
			dbg_input_buf[dbg_input_len] = '\0';
		}
	}
	if (ev->type == SDL_EVENT_KEY_DOWN) {
		k = ev->key.key;
		if (k == SDLK_BACKSPACE && dbg_input_len > 0) {
			dbg_input_buf[--dbg_input_len] = '\0';
		} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
			/* Echo command */
			snprintf(echo, sizeof(echo), "db> %s", dbg_input_buf);
			dbg_log_append(echo);
			dbg_log_append("\n");
			/* Run it */
			if (g_dbg != NULL) {
				strncpy(cmd_copy,
				    dbg_input_buf,
				    sizeof(cmd_copy) - 1);
				cmd_copy[sizeof(cmd_copy) - 1] = '\0';
				dbg_run_command(g_dbg, cmd_copy);
			}
			dbg_input_buf[0] = '\0';
			dbg_input_len = 0;
		} else if (k == SDLK_ESCAPE) {
			term_debug_toggle();
		}
	}
}

void
term_trace_toggle(void)
{
	if (trace_window_open == true) {
		if (trace_ren != NULL) {
			SDL_DestroyRenderer(trace_ren);
			trace_ren = NULL;
		}
		if (trace_win != NULL) {
			SDL_DestroyWindow(trace_win);
			trace_win = NULL;
		}
		trace_window_open = false;
		if (trace_buf != NULL) {
			free(trace_buf);
			trace_buf = NULL;
		}
		trace_head = 0;
		trace_count = 0;
		trace_frozen = false;
	} else {
		trace_win = SDL_CreateWindow("Apple-1 struct cpu Trace", 1100, 700, 0);
		if (trace_win == NULL)
			return;
		trace_ren = SDL_CreateRenderer(trace_win, NULL);
		if (trace_ren == NULL) {
			SDL_DestroyWindow(trace_win);
			trace_win = NULL;
			return;
		}
		trace_buf = calloc(trace_max, sizeof(struct trace_line));
		trace_head = 0;
		trace_count = 0;
		trace_scroll = 0;
		trace_frozen = false;
		trace_window_open = true;
	}
}

bool
term_trace_is_open(void)
{
	return (trace_window_open);
}

void
term_trace_render(void)
{
	SDL_Color bg = { 5, 5, 8, 255 };
	SDL_Color fg = { 51, 255, 51, 255 };
	SDL_Color hdr;
	char hbuf[128];
	int line_h;
	int max_vis;
	int total;
	int start_idx;
	int end_idx;
	int i;
	int real;
	int vy;

	if (trace_ren == NULL)
		return;

	if (trace_frozen == true) {
		hdr.r = 255;
		hdr.g = 64;
		hdr.b = 64;
		hdr.a = 255;
	} else {
		hdr.r = 255;
		hdr.g = 176;
		hdr.b = 0;
		hdr.a = 255;
	}

	SDL_SetRenderDrawColor(trace_ren, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(trace_ren);

	/* Header */
	if (trace_frozen == true) {
		snprintf(hbuf,
		    sizeof(hbuf),
		    "TRACE: %d lines  (FROZEN - Press SPACE to resume, "
		    "scroll with wheel)",
		    trace_count);
	} else {
		snprintf(hbuf,
		    sizeof(hbuf),
		    "TRACE: %d lines  (ACTIVE - Press SPACE to freeze, "
		    "scroll with wheel)",
		    trace_count);
	}
	draw_mono_str(trace_ren, hbuf, 8, 8, hdr);
	SDL_SetRenderDrawColor(trace_ren, 30, 60, 30, 255);
	SDL_RenderLine(trace_ren, 0, 30, 1100, 30);

	line_h = MONO_CH + 3;
	max_vis = (700 - 40) / line_h;
	total = trace_count;
	start_idx = total - max_vis - trace_scroll;
	if (start_idx < 0)
		start_idx = 0;
	end_idx = start_idx + max_vis;
	if (end_idx > total)
		end_idx = total;

	for (i = start_idx; i < end_idx; i++) {
		real = (trace_head - total + i + trace_max) % trace_max;
		if (real < 0)
			real += trace_max;
		vy = 38 + (i - start_idx) * line_h;
		draw_mono_str(trace_ren, trace_buf[real].line, 8, vy, fg);
	}

	SDL_RenderPresent(trace_ren);
}

bool
term_trace_is_window(SDL_Window *win)
{
	return (win != NULL && win == trace_win);
}

void
term_trace_handle_event(const SDL_Event *ev)
{
	if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		term_trace_toggle();
	} else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
		trace_scroll += (int)ev->wheel.y * -3;
		if (trace_scroll < 0)
			trace_scroll = 0;
		if (trace_scroll > trace_count)
			trace_scroll = trace_count;
	} else if (ev->type == SDL_EVENT_KEY_DOWN) {
		if (ev->key.key == SDLK_ESCAPE) {
			term_trace_toggle();
		} else if (ev->key.key == SDLK_SPACE) {
			trace_frozen = (trace_frozen == true) ? false : true;
		} else if (ev->key.key == SDLK_DOWN) {
			trace_scroll -= 3;
			if (trace_scroll < 0)
				trace_scroll = 0;
		} else if (ev->key.key == SDLK_UP) {
			trace_scroll += 3;
			if (trace_scroll > trace_count)
				trace_scroll = trace_count;
		}
	}
}

void
term_trace_export(void)
{
	FILE *f;
	int total;
	int i;
	int real;

	f = fopen("apple1_trace.txt", "w");
	if (f == NULL) {
		strncpy(status_text,
		    "TRACE WRITE FAILED",
		    sizeof(status_text) - 1);
		return;
	}
	total = trace_count;
	for (i = 0; i < total; i++) {
		real = (trace_head - total + i + trace_max) % trace_max;
		if (real < 0)
			real += trace_max;
		fprintf(f, "%s\n", trace_buf[real].line);
	}
	fclose(f);
	strncpy(status_text, "TRACE EXPORTED", sizeof(status_text) - 1);
}

/* ── PUBLIC term_apple1.h EXPORTS ────────────────────────────────────────── */

void
term_trace_push(const char *line)
{
	if (trace_frozen == true)
		return;

	if (trace_buf == NULL) {
		trace_buf = calloc(trace_max, sizeof(struct trace_line));
		if (trace_buf == NULL)
			return;
	}
	strncpy(trace_buf[trace_head].line, line, TRACE_LINE_LEN - 1);
	trace_buf[trace_head].line[TRACE_LINE_LEN - 1] = '\0';
	trace_head = (trace_head + 1) % trace_max;
	if (trace_count < trace_max) {
		trace_count++;
	}
}

bool
term_trace_active(void)
{
	return (trace_window_open == true && trace_buf != NULL);
}

bool
term_should_step(void)
{
	if (dbg_needs_step == true) {
		dbg_needs_step = false;
		return (true);
	}
	return (false);
}

void
term_request_step(void)
{
	dbg_needs_step = true;
}

void
term_close_debugger(void)
{
	if (debug_window_open == true) {
		term_debug_toggle();
	}
}

void
term_open_debugger(void)
{
	if (debug_window_open == false) {
		term_debug_toggle();
	}
}
