#include "term_apple1.h"
#include "term_config.h"
#include "term_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Defines matching term_internal.h declarations */
static const char *RAM_CHOICES[] = { "4", "8", "16", "32", "48", "64" };
static const char
    *BAUD_CHOICES[] = { "300", "1200", "1500", "2400", "4800", "9600", "Fast" };

Field fields[] = {
	{ "ROM FILE (-r)",
	    "Path to 256-byte Wozmon ROM (optional, uses embedded by default)",
	    'r',
	    FT_FILE,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "RAM SIZE (-m)",
	    "RAM in KB: 4,8,16,32,48,64  (default 64)",
	    'm',
	    FT_CHOICE,
	    false,
	    "64",
	    RAM_CHOICES,
	    6,
	    5,
	    false,
	    0 },
	{ "ACI ROM (-a)",
	    "Path to ACI ROM (optional, uses embedded by default)",
	    'a',
	    FT_FILE,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "LOAD BINARY (-l)",
	    "Preload file into RAM:  file@HEXADDR  e.g. basic.rom@E000",
	    'l',
	    FT_STR,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "DEFAULT TAPE (-e)",
	    "Default cassette tape (.aci) file path to load on startup",
	    'e',
	    FT_FILE,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "TERMINAL BAUD (-B)",
	    "Terminal display speed in baud (300=authentic Apple-1, "
	    "Fast=instant)",
	    'B',
	    FT_CHOICE,
	    false,
	    "300",
	    BAUD_CHOICES,
	    7,
	    0,
	    false,
	    0 },
	{ "DRAM REFRESH (-d)",
	    "Emulate DRAM refresh cycle-stealing (slows ~5%)",
	    'd',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "KBD BOUNCE (-b)",
	    "Emulate keyboard contact bounce (cosmetic)",
	    'b',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "NO PIA THROTTLE (-p)",
	    "Disable 977 ns PIA I/O throttling (slightly faster PIA)",
	    'p',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "DETERMINISTIC (-s)",
	    "Disable cold-boot RAM randomisation (affects BASIC)",
	    's',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "DEBUG MODE (-g)",
	    "Start with interactive debugger (pauses CPU first)",
	    'g',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "TRACE MODE (-t)",
	    "Print CPU trace to stdout (pipe to file to capture)",
	    't',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "REAL BACKSPACE (-x)",
	    "Enable destructive backspace (cursor moves back and erases)",
	    'x',
	    FT_BOOL,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
	{ "CONFIG PATH",
	    "Where to save apple1.conf",
	    0,
	    FT_STR,
	    false,
	    "",
	    NULL,
	    0,
	    0,
	    false,
	    0 },
};

const int NF = sizeof(fields) / sizeof(fields[0]);
const int ICFG = (sizeof(fields) / sizeof(fields[0])) - 1;

int editing_field_idx = -1;
int config_scroll_offset = 0;

char config_status_msg[256] = "";
uint64_t config_status_until = 0;

/* Colors from config_ui */
const SDL_Color PANEL = { 18, 18, 24, 255 };
const SDL_Color BORD = { 40, 40, 50, 255 };
const SDL_Color AMBER = { 255, 176, 0, 255 };
const SDL_Color GREEN = { 51, 255, 51, 255 };
const SDL_Color DIM = { 80, 120, 80, 255 };
const SDL_Color SELBG = { 20, 50, 20, 255 };
const SDL_Color SELBO = { 51, 255, 51, 255 };
const SDL_Color WHITE = { 220, 220, 220, 255 };
const SDL_Color RED = { 255, 80, 80, 255 };
const SDL_Color BTNBG = { 18, 18, 26, 255 };
const SDL_Color BTNHV = { 30, 55, 30, 255 };

/* ── CONFIG LOAD/SAVE HELPERS ────────────────────────────────────────────── */

static void
get_xdg_config_path(char *out_path, size_t max_len)
{
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home && xdg_config_home[0] != '\0') {
		snprintf(out_path,
		    max_len,
		    "%s/apple1/apple1.conf",
		    xdg_config_home);
	} else {
		const char *home = getenv("HOME");
		if (home && home[0] != '\0') {
			snprintf(out_path,
			    max_len,
			    "%s/.config/apple1/apple1.conf",
			    home);
		} else {
			snprintf(out_path, max_len, "apple1.conf");
		}
	}
}

static void
mkdirs(const char *path)
{
	char t[1024];
	snprintf(t, sizeof(t), "%s", path);
	for (char *p = t + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(t, 0755);
			*p = '/';
		}
	}
}

static Field *
by_flag(char f)
{
	for (int i = 0; i < NF - 1; i++) {
		if (fields[i].flag == f)
			return &fields[i];
	}
	return NULL;
}

static void
load_conf(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		size_t l = strlen(line);
		while (l > 0 &&
		    (line[l - 1] == '\n' || line[l - 1] == '\r' ||
			line[l - 1] == ' ')) {
			line[--l] = '\0';
		}
		char *p = line;
		while (*p == ' ')
			p++;
		if (!*p || *p == '#')
			continue;
		if (p[0] == '-' && p[1]) {
			char fl = p[1];
			char *v = p + 2;
			while (*v == ' ')
				v++;
			Field *f = by_flag(fl);
			if (!f)
				continue;
			if (f->type == FT_BOOL) {
				f->bval = true;
			} else if (f->type == FT_CHOICE) {
				for (int j = 0; j < f->nchoices; j++) {
					if (strcmp(f->choices[j], v) == 0) {
						f->cidx = j;
						snprintf(f->sval,
						    sizeof(f->sval),
						    "%s",
						    v);
						break;
					}
				}
			} else {
				snprintf(f->sval, sizeof(f->sval), "%s", v);
			}
		}
	}
	fclose(fp);
}

static void
save_conf(const char *path)
{
	mkdirs(path);
	FILE *fp = fopen(path, "w");
	if (!fp)
		return;
	fprintf(fp, "# Apple-1 Emulator Config (SDL3 GUI)\n");
	fprintf(fp,
	    "# Options managed live in the emulator sidebar are NOT "
	    "here:\n");
	fprintf(fp, "#   CPU speed (-c), ACI tape (-e/-E), Krusader (-k)\n\n");
	for (int i = 0; i < NF - 1; i++) {
		Field *f = &fields[i];
		if (f->type == FT_BOOL) {
			if (f->bval)
				fprintf(fp, "-%c\n", f->flag);
		} else if (f->type == FT_CHOICE) {
			fprintf(fp, "-%c %s\n", f->flag, f->choices[f->cidx]);
		} else if (f->sval[0]) {
			fprintf(fp, "-%c %s\n", f->flag, f->sval);
		}
	}
	fclose(fp);
}

/* ── INTERFACE EXPORTS ───────────────────────────────────────────────────── */

void
term_config_init(void)
{
	get_xdg_config_path(fields[ICFG].sval, sizeof(fields[ICFG].sval));
	if (access(fields[ICFG].sval, F_OK) != 0) {
		config_scroll_offset = 0;
		config_modal_open = true;
	} else {
		load_conf(fields[ICFG].sval);
	}
}

static bool
draw_config_button(int x,
    int y,
    int w,
    int h,
    const char *lbl,
    SDL_Color tint,
    int mx,
    int my)
{
	bool hov = (mx >= x && mx < x + w && my >= y && my < y + h);
	SDL_FRect btn_rect = { (float)x, (float)y, (float)w, (float)h };

	SDL_SetRenderDrawColor(renderer,
	    hov ? BTNHV.r : BTNBG.r,
	    hov ? BTNHV.g : BTNBG.g,
	    hov ? BTNHV.b : BTNBG.b,
	    255);
	SDL_RenderFillRect(renderer, &btn_rect);
	SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b, 255);
	SDL_RenderRect(renderer, &btn_rect);

	int cw = (GLYPH_COLS + 1) * 2;
	int ch2 = GLYPH_ROWS * 2;
	draw_text_2x(renderer,
	    lbl,
	    x + (w - (int)strlen(lbl) * cw) / 2,
	    y + (h - ch2) / 2,
	    tint);
	return hov;
}

static void
render_config_field(int i, int idx, int mx, int my)
{
	Field *f = &fields[i];
	int y = MODAL_Y + SB_Y + idx * FIELD_H;
	int x = MODAL_X + SB_X;
	bool sel =
	    (mx >= x && mx < x + FIELD_W && my >= y && my < y + FIELD_H - 2);

	SDL_FRect field_rect = { (float)x,
		(float)y,
		(float)FIELD_W,
		(float)(FIELD_H - 2) };
	SDL_SetRenderDrawColor(renderer,
	    sel ? SELBG.r : PANEL.r,
	    sel ? SELBG.g : PANEL.g,
	    sel ? SELBG.b : PANEL.b,
	    255);
	SDL_RenderFillRect(renderer, &field_rect);
	SDL_SetRenderDrawColor(renderer,
	    sel ? SELBO.r : BORD.r,
	    sel ? SELBO.g : BORD.g,
	    sel ? SELBO.b : BORD.b,
	    255);
	SDL_RenderRect(renderer, &field_rect);

	draw_text_2x(renderer, f->label, x + 8, y + 14, sel ? GREEN : DIM);

	int vx = x + 340;
	char vb[80];
	switch (f->type) {
	case FT_BOOL:
		draw_config_button(vx,
		    y + 8,
		    90,
		    26,
		    f->bval ? "YES" : "NO",
		    f->bval ? GREEN : DIM,
		    mx,
		    my);
		break;
	case FT_CHOICE:
		draw_config_button(vx, y + 8, 30, 26, "<", GREEN, mx, my);
		if (f->flag == 'm') {
			snprintf(vb, sizeof(vb), "%s KB", f->choices[f->cidx]);
		} else if (f->flag == 'B') {
			if (strcmp(f->choices[f->cidx], "Fast") == 0) {
				snprintf(vb,
				    sizeof(vb),
				    "%s",
				    f->choices[f->cidx]);
			} else {
				snprintf(vb,
				    sizeof(vb),
				    "%s baud",
				    f->choices[f->cidx]);
			}
		} else {
			snprintf(vb, sizeof(vb), "%s", f->choices[f->cidx]);
		}
		draw_text_2x(renderer, vb, vx + 38, y + 14, WHITE);
		draw_config_button(vx + 120, y + 8, 30, 26, ">", GREEN, mx, my);
		break;
	case FT_STR:
	case FT_FILE: {
		const char *v = f->sval[0] ? f->sval : "(EMPTY)";
		int vl = (int)strlen(v);
		char tr[40];
		if (vl > 35) {
			snprintf(tr, sizeof(tr), "...%s", v + vl - 32);
		} else {
			snprintf(tr, sizeof(tr), "%s", v);
		}
		draw_text_2x(renderer,
		    tr,
		    vx,
		    y + 14,
		    f->editing ? AMBER : WHITE);
		if (f->type == FT_FILE) {
			draw_config_button(vx + 380,
			    y + 8,
			    90,
			    26,
			    "BROWSE",
			    AMBER,
			    mx,
			    my);
		}
		/* Blinking cursor */
		if (f->editing) {
			int cx2 = vx + f->cursor * (GLYPH_COLS + 1) * 2;
			if ((SDL_GetTicks() / 400) % 2 == 0) {
				SDL_FRect cursor_rect = { (float)cx2,
					(float)(y + 13),
					2.0f,
					(float)(GLYPH_ROWS * 2 + 2) };
				SDL_SetRenderDrawColor(renderer,
				    AMBER.r,
				    AMBER.g,
				    AMBER.b,
				    AMBER.a);
				SDL_RenderFillRect(renderer, &cursor_rect);
			}
		}
		break;
	}
	}
}

void
term_config_modal_render(SDL_Renderer *rend)
{
	(void)
	    rend; /* Shared renderer is global, but parameter is kept for signature */

	/* Semi-transparent dimming background overlay over the entire screen */
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
	SDL_FRect overlay = { 0, 0, (float)SCREEN_W, (float)SCREEN_H };
	SDL_RenderFillRect(renderer, &overlay);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

	/* Centered modal panel */
	SDL_FRect modal_rect = { (float)MODAL_X,
		(float)MODAL_Y,
		(float)MODAL_W,
		(float)MODAL_H };
	SDL_SetRenderDrawColor(renderer, PANEL.r, PANEL.g, PANEL.b, PANEL.a);
	SDL_RenderFillRect(renderer, &modal_rect);
	SDL_SetRenderDrawColor(renderer, BORD.r, BORD.g, BORD.b, BORD.a);
	SDL_RenderRect(renderer, &modal_rect);

	/* Mouse position for hover states */
	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	/* Draw Title */
	draw_text_scaled(renderer,
	    "APPLE-1 EMULATOR CONFIG",
	    MODAL_X + 20,
	    MODAL_Y + 20,
	    3,
	    AMBER);
	SDL_SetRenderDrawColor(renderer, BORD.r, BORD.g, BORD.b, BORD.a);
	SDL_RenderLine(renderer,
	    MODAL_X + 20,
	    MODAL_Y + 58,
	    MODAL_X + MODAL_W - 20,
	    MODAL_Y + 58);

	/* Draw column headers */
	draw_text_2x(renderer,
	    "OPTION",
	    MODAL_X + SB_X + 8,
	    MODAL_Y + SB_Y - 20,
	    DIM);
	draw_text_2x(renderer,
	    "VALUE",
	    MODAL_X + SB_X + 340,
	    MODAL_Y + SB_Y - 20,
	    DIM);

	/* Draw fields */
	for (int idx = 0; idx < VISIBLE_FIELDS; idx++) {
		int i = config_scroll_offset + idx;
		if (i >= NF)
			break;
		render_config_field(i, idx, mx, my);
	}

	/* Draw Scrollbar if needed */
	if (NF > VISIBLE_FIELDS) {
		int track_x = MODAL_X + 855;
		int track_y = MODAL_Y + SB_Y;
		int track_w = 15;
		int track_h = VISIBLE_FIELDS * FIELD_H - 2;

		SDL_FRect track_rect = { (float)track_x,
			(float)track_y,
			(float)track_w,
			(float)track_h };
		SDL_SetRenderDrawColor(renderer, 10, 10, 14, 255);
		SDL_RenderFillRect(renderer, &track_rect);
		SDL_SetRenderDrawColor(renderer, BORD.r, BORD.g, BORD.b, BORD.a);
		SDL_RenderRect(renderer, &track_rect);

		/* Thumb height and position */
		float ratio = (float)VISIBLE_FIELDS / (float)NF;
		int thumb_h = (int)(ratio * track_h);
		if (thumb_h < 20)
			thumb_h = 20;

		float scroll_pct = (float)config_scroll_offset /
		    (float)(NF - VISIBLE_FIELDS);
		int thumb_y = track_y + (int)(scroll_pct * (track_h - thumb_h));

		SDL_FRect thumb_rect = { (float)track_x + 2,
			(float)thumb_y,
			(float)track_w - 4,
			(float)thumb_h };
		bool thumb_hover = (mx >= track_x && mx < track_x + track_w &&
		    my >= track_y && my < track_y + track_h);
		SDL_SetRenderDrawColor(renderer,
		    thumb_hover ? GREEN.r : DIM.r,
		    thumb_hover ? GREEN.g : DIM.g,
		    thumb_hover ? GREEN.b : DIM.b,
		    255);
		SDL_RenderFillRect(renderer, &thumb_rect);
	}

	/* Note at the bottom */
	SDL_Color note_color = { 60, 80, 60, 255 };
	draw_text_scaled(renderer,
	    "NOTE: CPU SPEED / TAPE / KRUSADER ARE CONTROLLED LIVE IN THE "
	    "EMULATOR SIDEBAR",
	    MODAL_X + 20,
	    MODAL_Y + MODAL_H - 118,
	    1,
	    note_color);

	/* Bottom Buttons */
	int bby = MODAL_Y + MODAL_BBY;
	SDL_SetRenderDrawColor(renderer, 30, 30, 36, 255);
	SDL_FRect bar = { (float)MODAL_X,
		(float)(bby - 10),
		(float)MODAL_W,
		2.0f };
	SDL_RenderFillRect(renderer, &bar);

	draw_config_button(MODAL_X + 20,
	    bby,
	    160,
	    34,
	    "SAVE CONFIG",
	    AMBER,
	    mx,
	    my);
	draw_config_button(MODAL_X + 200,
	    bby,
	    180,
	    34,
	    "APPLY & REBOOT",
	    GREEN,
	    mx,
	    my);
	draw_config_button(MODAL_X + 400, bby, 100, 34, "CLOSE", RED, mx, my);

	/* Status Message */
	if (SDL_GetTicks() < config_status_until && config_status_msg[0]) {
		draw_text_2x(renderer,
		    config_status_msg,
		    MODAL_X + 510,
		    bby + 8,
		    AMBER);
	}

	/* Hint for hovered field */
	for (int idx = 0; idx < VISIBLE_FIELDS; idx++) {
		int i = config_scroll_offset + idx;
		if (i >= NF)
			break;
		int fy = MODAL_Y + SB_Y + idx * FIELD_H;
		if (my >= fy && my < fy + FIELD_H - 2 && mx >= MODAL_X + SB_X &&
		    mx < MODAL_X + SB_X + FIELD_W) {
			draw_text_2x(renderer,
			    fields[i].hint,
			    MODAL_X + 20,
			    MODAL_Y + MODAL_H - 95,
			    DIM);
			break;
		}
	}
}

void
term_config_modal_handle_click(int bx, int by, bool is_wizard, bool *done)
{
	int bby = MODAL_Y + MODAL_BBY;

	/* 1. Check bottom buttons */
	if (bx >= MODAL_X + 20 && bx < MODAL_X + 180 && by >= bby &&
	    by < bby + 34) {
		save_conf(fields[ICFG].sval);
		set_config_status("CONFIG SAVED!", 2500);
		if (is_wizard)
			*done = true;
		return;
	}
	if (bx >= MODAL_X + 200 && bx < MODAL_X + 380 && by >= bby &&
	    by < bby + 34) {
		save_conf(fields[ICFG].sval);
		if (is_wizard) {
			*done = true;
		} else {
			reboot_emulator();
		}
		return;
	}
	if (!is_wizard && bx >= MODAL_X + 400 && bx < MODAL_X + 500 &&
	    by >= bby && by < bby + 34) {
		if (editing_field_idx >= 0) {
			fields[editing_field_idx].editing = false;
			editing_field_idx = -1;
		}
		config_modal_open = false;
		return;
	}

	/* 2. Check click on scrollbar */
	if (NF > VISIBLE_FIELDS) {
		int track_x = MODAL_X + 855;
		int track_y = MODAL_Y + SB_Y;
		int track_w = 15;
		int track_h = VISIBLE_FIELDS * FIELD_H - 2;

		if (bx >= track_x && bx < track_x + track_w && by >= track_y &&
		    by < track_y + track_h) {
			float click_pct = (float)(by - track_y) /
			    (float)track_h;
			int new_offset =
			    (int)(click_pct * (NF - VISIBLE_FIELDS + 1));
			if (new_offset < 0)
				new_offset = 0;
			if (new_offset > NF - VISIBLE_FIELDS)
				new_offset = NF - VISIBLE_FIELDS;
			config_scroll_offset = new_offset;
			return;
		}
	}

	/* Close editing field if clicked elsewhere */
	if (editing_field_idx >= 0) {
		fields[editing_field_idx].editing = false;
		editing_field_idx = -1;
	}

	/* 3. Check each visible field */
	for (int idx = 0; idx < VISIBLE_FIELDS; idx++) {
		int i = config_scroll_offset + idx;
		if (i >= NF)
			break;
		int fy = MODAL_Y + SB_Y + idx * FIELD_H;
		if (by < fy || by >= fy + FIELD_H - 2 || bx < MODAL_X + SB_X ||
		    bx >= MODAL_X + SB_X + FIELD_W) {
			continue;
		}
		Field *f = &fields[i];
		int vx = MODAL_X + SB_X + 340;
		switch (f->type) {
		case FT_BOOL:
			if (bx >= vx && bx < vx + 90) {
				f->bval = !f->bval;
			}
			break;
		case FT_CHOICE:
			if (bx >= vx && bx < vx + 30) {
				f->cidx = (f->cidx - 1 + f->nchoices) %
				    f->nchoices;
				snprintf(f->sval,
				    sizeof(f->sval),
				    "%s",
				    f->choices[f->cidx]);
			} else if (bx >= vx + 120 && bx < vx + 150) {
				f->cidx = (f->cidx + 1) % f->nchoices;
				snprintf(f->sval,
				    sizeof(f->sval),
				    "%s",
				    f->choices[f->cidx]);
			}
			break;
		case FT_FILE:
			if (bx >= vx + 380 && bx < vx + 470) {
				char picked[512] = { 0 };
				const char *ext =
				    (f->flag == 'r' || f->flag == 'a') ? "*."
									 "rom "
									 "*.bin"
								       : "*";
				if (pick_file_dialog(picked,
					sizeof(picked),
					"Select ROM",
					ext)) {
					snprintf(f->sval,
					    sizeof(f->sval),
					    "%s",
					    picked);
				}
			} else if (bx >= vx && bx < vx + 370) {
				f->editing = true;
				f->cursor = (int)strlen(f->sval);
				editing_field_idx = i;
			}
			break;
		case FT_STR:
			if (bx >= vx && bx < vx + 390) {
				f->editing = true;
				f->cursor = (int)strlen(f->sval);
				editing_field_idx = i;
			}
			break;
		}
		break;
	}
}

void
term_config_modal_handle_key(const SDL_KeyboardEvent *key)
{
	if (editing_field_idx < 0)
		return;
	Field *f = &fields[editing_field_idx];
	SDL_Keycode k = key->key;
	int sl = (int)strlen(f->sval);

	if (k == SDLK_RETURN || k == SDLK_ESCAPE) {
		f->editing = false;
		editing_field_idx = -1;
	} else if (k == SDLK_BACKSPACE && f->cursor > 0) {
		memmove(f->sval + f->cursor - 1,
		    f->sval + f->cursor,
		    sl - f->cursor + 1);
		f->cursor--;
	} else if (k == SDLK_DELETE && f->cursor < sl) {
		memmove(f->sval + f->cursor,
		    f->sval + f->cursor + 1,
		    sl - f->cursor);
	} else if (k == SDLK_LEFT && f->cursor > 0) {
		f->cursor--;
	} else if (k == SDLK_RIGHT && f->cursor < sl) {
		f->cursor++;
	} else if (k == SDLK_HOME) {
		f->cursor = 0;
	} else if (k == SDLK_END) {
		f->cursor = sl;
	}
}

void
term_config_modal_handle_text_input(const char *text)
{
	if (editing_field_idx < 0)
		return;
	Field *f = &fields[editing_field_idx];
	size_t tl = strlen(text);
	size_t sl = strlen(f->sval);
	if (sl + tl < sizeof(f->sval) - 1) {
		memmove(f->sval + f->cursor + tl,
		    f->sval + f->cursor,
		    sl - f->cursor + 1);
		memcpy(f->sval + f->cursor, text, tl);
		f->cursor += (int)tl;
	}
}

void
term_config_scroll(int delta)
{
	config_scroll_offset += delta;
	if (config_scroll_offset < 0)
		config_scroll_offset = 0;
	if (config_scroll_offset > NF - VISIBLE_FIELDS)
		config_scroll_offset = NF - VISIBLE_FIELDS;
}

void
term_run_config_wizard(void)
{
	get_xdg_config_path(fields[ICFG].sval, sizeof(fields[ICFG].sval));

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
		return;
	}

	SDL_Window *wiz_win = SDL_CreateWindow("Apple-1 First Time Setup — "
					       "Save Config to Continue",
	    SCREEN_W,
	    SCREEN_H,
	    0);
	SDL_Renderer *wiz_ren = SDL_CreateRenderer(wiz_win, NULL);
	SDL_StartTextInput(wiz_win);

	/* Temporarily wire global renderer/window to the wizard */
	renderer = wiz_ren;
	window = wiz_win;

	charmap_loaded = load_charmap();
	config_scroll_offset = 0;
	config_modal_open = true;

	/* Banner message: wizard mode — no way out without saving */
	set_config_status("NO CONFIG FOUND — FILL IN SETTINGS AND CLICK SAVE",
	    0xFFFFFFFF);
	config_status_until = UINT64_MAX;

	bool done = false;
	while (!done) {
		/* Dark background */
		SDL_SetRenderDrawColor(renderer, 8, 8, 10, 255);
		SDL_RenderClear(renderer);
		term_config_modal_render(NULL);
		SDL_RenderPresent(renderer);

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_EVENT_QUIT) {
				/* Force-quit only — user didn't save */
				SDL_StopTextInput(wiz_win);
				SDL_DestroyRenderer(wiz_ren);
				SDL_DestroyWindow(wiz_win);
				SDL_Quit();
				renderer = NULL;
				window = NULL;
				fprintf(stderr,
				    "Setup cancelled. No config saved.\n");
				exit(1);
			}

			if (ev.type == SDL_EVENT_KEY_DOWN) {
				if (ev.key.key == SDLK_ESCAPE)
					continue; /* blocked */
				if (editing_field_idx >= 0)
					term_config_modal_handle_key(&ev.key);
			}

			if (ev.type == SDL_EVENT_TEXT_INPUT) {
				if (editing_field_idx >= 0)
					term_config_modal_handle_text_input(
					    ev.text.text);
			}

			if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
				term_config_scroll(-(int)ev.wheel.y);
			}

			if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
			    ev.button.button == SDL_BUTTON_LEFT) {
				int bx = (int)ev.button.x;
				int by = (int)ev.button.y;
				term_config_modal_handle_click(bx,
				    by,
				    true,
				    &done);
			}
		}
		SDL_Delay(16);
	}

	SDL_StopTextInput(wiz_win);
	SDL_DestroyRenderer(wiz_ren);
	SDL_DestroyWindow(wiz_win);
	SDL_Quit();
	renderer = NULL;
	window = NULL;
	config_modal_open = false;
}
