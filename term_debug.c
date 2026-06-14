#include "disasm.h"
#include "font5x7.h"
#include "term_apple1.h"
#include "term_debug.h"
#include "term_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── FONT5X7 MONO RENDERER ───────────────────────────────────────────────── */
#define MONO_SCALE 2
#define MONO_CW	   6		    /* (5 cols * scale) + 1px gap */
#define MONO_CH	   (7 * MONO_SCALE) /* 14px */

static void
draw_mono_char(SDL_Renderer *rend, char c, int x, int y, SDL_Color col)
{
	if (c < 0x20 || c > 0x7E)
		c = '?';
	const unsigned char *glyph = &Font5x7[(c - 0x20) * 5];
	SDL_SetRenderDrawColor(rend, col.r, col.g, col.b, col.a);
	for (int col_i = 0; col_i < 5; col_i++) {
		uint8_t colbits = glyph[col_i];
		for (int row = 0; row < 7; row++) {
			if (colbits & (1 << row)) {
				SDL_FRect px = { (float)(x +
						     col_i * MONO_SCALE),
					(float)(y + row * MONO_SCALE),
					(float)MONO_SCALE,
					(float)MONO_SCALE };
				SDL_RenderFillRect(rend, &px);
			}
		}
	}
}

static void
draw_mono_str(SDL_Renderer *rend, const char *s, int x, int y, SDL_Color col)
{
	while (*s) {
		draw_mono_char(rend, *s++, x, y, col);
		x += MONO_CW * MONO_SCALE;
	}
}

/* ── TRACE RING BUFFER STATE ──────────────────────────────────────────────── */
#define TRACE_DEFAULT_MAX 5000
#define TRACE_LINE_LEN	  160

typedef struct {
	char line[TRACE_LINE_LEN];
} TraceLine;

static TraceLine *trace_buf = NULL;
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
	const char *p = str;
	while (*p) {
		const char *nl = strchr(p, '\n');
		int seg = nl ? (int)(nl - p) : (int)strlen(p);
		if (dbg_num_lines < DBG_WIN_LINES) {
			int col = (int)strlen(dbg_output[dbg_num_lines]);
			int room = DBG_WIN_COLS - 1 - col;
			if (room > 0) {
				int copy = seg < room ? seg : room;
				memcpy(dbg_output[dbg_num_lines] + col,
				    p,
				    copy);
				dbg_output[dbg_num_lines][col + copy] = '\0';
			}
			if (nl) {
				dbg_num_lines++;
				if (dbg_num_lines < DBG_WIN_LINES)
					dbg_output[dbg_num_lines][0] = '\0';
			}
		}
		p += seg + (nl ? 1 : 0);
		if (!nl)
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
	if (debug_ren) {
		SDL_DestroyRenderer(debug_ren);
		debug_ren = NULL;
	}
	if (debug_win) {
		SDL_DestroyWindow(debug_win);
		debug_win = NULL;
	}
	if (trace_ren) {
		SDL_DestroyRenderer(trace_ren);
		trace_ren = NULL;
	}
	if (trace_win) {
		SDL_DestroyWindow(trace_win);
		trace_win = NULL;
	}
	if (trace_buf) {
		free(trace_buf);
		trace_buf = NULL;
	}
}

void
term_debug_toggle(void)
{
	if (debug_window_open) {
		if (debug_ren) {
			SDL_DestroyRenderer(debug_ren);
			debug_ren = NULL;
		}
		if (debug_win) {
			SDL_DestroyWindow(debug_win);
			debug_win = NULL;
		}
		debug_window_open = false;
		g_debug_enabled = false;
		if (g_dbg)
			g_dbg->step_mode = false;
	} else {
		debug_win =
		    SDL_CreateWindow("Apple-1 Debugger  [db> ]", 900, 600, 0);
		if (!debug_win)
			return;
		debug_ren = SDL_CreateRenderer(debug_win, NULL);
		if (!debug_ren) {
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
		if (g_dbg) {
			g_dbg->step_mode = true;
			/* Print initial state */
			char dis[64];
			cpu_disassemble(g_dbg->cpu->bus, g_dbg->cpu->pc, dis);
			char hdr[128];
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
	return debug_window_open;
}

void
term_debug_render(void)
{
	if (!debug_ren)
		return;
	SDL_Color bg = { 8, 8, 10, 255 };
	SDL_Color fg = { 51, 255, 51, 255 };
	SDL_Color dim = { 30, 140, 30, 255 };
	SDL_Color prompt = { 255, 176, 0, 255 };

	SDL_SetRenderDrawColor(debug_ren, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(debug_ren);

	int x0 = 8, y0 = 8;
	int line_h = MONO_CH + 4;
	int max_visible = (600 - 60) / line_h;
	int start = dbg_num_lines > max_visible ? dbg_num_lines - max_visible
						: 0;
	for (int i = start; i < dbg_num_lines; i++) {
		draw_mono_str(debug_ren,
		    dbg_output[i],
		    x0,
		    y0 + (i - start) * line_h,
		    dim);
	}

	/* Prompt line at bottom */
	int py = 600 - 48;
	SDL_SetRenderDrawColor(debug_ren, 18, 18, 22, 255);
	SDL_FRect pbar = { 0, (float)py - 4, 900, (float)(MONO_CH + 12) };
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
	return win && (win == debug_win);
}

void
term_debug_handle_event(const SDL_Event *ev)
{
	if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		term_debug_toggle();
		return;
	}
	if (ev->type == SDL_EVENT_TEXT_INPUT) {
		const char *t = ev->text.text;
		while (*t && dbg_input_len < 255) {
			dbg_input_buf[dbg_input_len++] = *t++;
			dbg_input_buf[dbg_input_len] = '\0';
		}
	}
	if (ev->type == SDL_EVENT_KEY_DOWN) {
		SDL_Keycode k = ev->key.key;
		if (k == SDLK_BACKSPACE && dbg_input_len > 0) {
			dbg_input_buf[--dbg_input_len] = '\0';
		} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
			/* Echo command */
			char echo[260];
			snprintf(echo, sizeof(echo), "db> %s", dbg_input_buf);
			dbg_log_append(echo);
			dbg_log_append("\n");
			/* Run it */
			if (g_dbg) {
				char cmd_copy[256];
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
	if (trace_window_open) {
		if (trace_ren) {
			SDL_DestroyRenderer(trace_ren);
			trace_ren = NULL;
		}
		if (trace_win) {
			SDL_DestroyWindow(trace_win);
			trace_win = NULL;
		}
		trace_window_open = false;
		if (trace_buf) {
			free(trace_buf);
			trace_buf = NULL;
		}
		trace_head = 0;
		trace_count = 0;
		trace_frozen = false;
	} else {
		trace_win = SDL_CreateWindow("Apple-1 CPU Trace", 1100, 700, 0);
		if (!trace_win)
			return;
		trace_ren = SDL_CreateRenderer(trace_win, NULL);
		if (!trace_ren) {
			SDL_DestroyWindow(trace_win);
			trace_win = NULL;
			return;
		}
		trace_buf = calloc(trace_max, sizeof(TraceLine));
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
	return trace_window_open;
}

void
term_trace_render(void)
{
	if (!trace_ren)
		return;
	SDL_Color bg = { 5, 5, 8, 255 };
	SDL_Color fg = { 51, 255, 51, 255 };
	SDL_Color hdr = trace_frozen ? (SDL_Color){ 255, 64, 64, 255 }
				     : (SDL_Color){ 255, 176, 0, 255 };

	SDL_SetRenderDrawColor(trace_ren, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(trace_ren);

	/* Header */
	char hbuf[128];
	if (trace_frozen) {
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

	int line_h = MONO_CH + 3;
	int max_vis = (700 - 40) / line_h;
	int total = trace_count;
	int start_idx = total - max_vis - trace_scroll;
	if (start_idx < 0)
		start_idx = 0;
	int end_idx = start_idx + max_vis;
	if (end_idx > total)
		end_idx = total;

	for (int i = start_idx; i < end_idx; i++) {
		int real = (trace_head - total + i + trace_max) % trace_max;
		if (real < 0)
			real += trace_max;
		int vy = 38 + (i - start_idx) * line_h;
		draw_mono_str(trace_ren, trace_buf[real].line, 8, vy, fg);
	}

	SDL_RenderPresent(trace_ren);
}

bool
term_trace_is_window(SDL_Window *win)
{
	return win && (win == trace_win);
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
		if (ev->key.key == SDLK_ESCAPE)
			term_trace_toggle();
		else if (ev->key.key == SDLK_SPACE) {
			trace_frozen = !trace_frozen;
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
	FILE *f = fopen("apple1_trace.txt", "w");
	if (!f) {
		strncpy(status_text,
		    "TRACE WRITE FAILED",
		    sizeof(status_text) - 1);
		return;
	}
	int total = trace_count;
	for (int i = 0; i < total; i++) {
		int real = (trace_head - total + i + trace_max) % trace_max;
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
	if (trace_frozen)
		return;

	if (!trace_buf) {
		trace_buf = calloc(trace_max, sizeof(TraceLine));
		if (!trace_buf)
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
	return trace_window_open && (trace_buf != NULL);
}

bool
term_should_step(void)
{
	if (dbg_needs_step) {
		dbg_needs_step = false;
		return true;
	}
	return false;
}

void
term_request_step(void)
{
	dbg_needs_step = true;
}

void
term_close_debugger(void)
{
	if (debug_window_open) {
		term_debug_toggle();
	}
}

void
term_open_debugger(void)
{
	if (!debug_window_open) {
		term_debug_toggle();
	}
}
