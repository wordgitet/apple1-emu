#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <SDL3/SDL.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "aci.h"
#include "bus.h"
#include "cpu.h"
#include "dbg.h"
#include "disasm.h"
#include "embedded_roms.h"
#include "font5x7.h"
#include "krusader.h"
#include "term_apple1.h"
#include "term_config.h"
#include "term_debug.h"
#include "term_internal.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

extern struct bus *g_bus;
extern struct cpu *g_cpu;
extern bool g_debug_enabled;
extern char *g_argv0;
extern bool
io_has_buffered_key(void);

/* ── CONSTANTS & PALETTES ────────────────────────────────────────────────── */
const struct palette PALETTES[] = {
	/* Green */
	{ { 5, 17, 5, 255 }, { 51, 255, 51, 255 }, { 17, 136, 17, 64 } },
	/* Amber */
	{ { 18, 9, 0, 255 }, { 255, 176, 0, 255 }, { 170, 102, 0, 64 } },
	/* Mono */
	{ { 8, 8, 8, 255 }, { 240, 240, 240, 255 }, { 128, 128, 128, 64 } }
};

/* ── EMULATED TERMINAL STATE ─────────────────────────────────────────────── */
static uint8_t vram[24][40];
static int cx = 0;
static int cy = 0;
static bool vram_initialized = false;
static bool boot_sweep = true;
static bool reset_pending = false;
static bool screen_dirty = false;

/* ── POWER STATE ─────────────────────────────────────────────────────────── */
static bool machine_powered = true;
static bool emulation_paused = false;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
uint8_t charmap_data[2048];
bool charmap_loaded = false;
int charmap_size = 2048;
static uint64_t last_redraw_ms = 0;
static uint8_t buffered_key_sdl = 0;

/* paste buffer */
static char paste_buf[4096];
static int paste_pos = 0;
static int paste_len = 0;

/* ── GUI INTERACTIVE SETTINGS ────────────────────────────────────────────── */
enum monitor_tint monitor_tint = MONITOR_GREEN;
bool scanlines_enabled = true;
float scanline_opacity = 0.35f;

char cassette_files[MAX_CASSETTES][512];
int num_cassettes = 0;
int selected_cassette_idx = 0;

char status_text[128] = "SYSTEM INITIALISED.";
bool config_modal_open = false;

/* ── CONFIG MODAL STATE & FIELDS ─────────────────────────────────────────── */
static bool file_menu_open = false;
static bool emulation_menu_open = false;
static bool display_menu_open = false;
static bool debug_menu_open = false;
static bool trace_menu_open = false;
static bool help_menu_open = false;

/* context menu */
static bool context_menu_open = false;
static int context_menu_x = 0;
static int context_menu_y = 0;

/* ── INTERNAL HELPERS ────────────────────────────────────────────────────── */

void
scan_tapes(void)
{
	DIR *d;

	num_cassettes = 0;

	/* Scan root directory "." for .aci files */
	d = opendir(".");
	if (d != NULL) {
		struct dirent *dir;
		while ((dir = readdir(d)) != NULL) {
			if (dir->d_type == DT_REG) {
				const char *ext = strrchr(dir->d_name, '.');
				if (ext != NULL && strcmp(ext, ".aci") == 0) {
					if (num_cassettes < MAX_CASSETTES) {
						snprintf(
						    cassette_files[num_cassettes],
						    512,
						    "%s",
						    dir->d_name);
						num_cassettes++;
					}
				}
			}
		}
		closedir(d);
	}

	/* Scan "cassettes" directory for .aci files */
	d = opendir("cassettes");
	if (d != NULL) {
		struct dirent *dir;
		while ((dir = readdir(d)) != NULL) {
			if (dir->d_type == DT_REG) {
				const char *ext = strrchr(dir->d_name, '.');
				if (ext && strcmp(ext, ".aci") == 0) {
					if (num_cassettes < MAX_CASSETTES) {
						snprintf(
						    cassette_files[num_cassettes],
						    512,
						    "cassettes/%s",
						    dir->d_name);
						num_cassettes++;
					}
				}
			}
		}
		closedir(d);
	}

	if (num_cassettes > 0) {
		if (selected_cassette_idx >= num_cassettes ||
		    selected_cassette_idx < 0) {
			selected_cassette_idx = 0;
		}
	} else {
		selected_cassette_idx = -1;
	}
}

bool
load_charmap(void)
{
	/* Try authentic Apple-1 character ROM first */
	FILE *f = fopen("2513_Apple-1.bin", "rb");
	if (!f) {
		f = fopen("roms/2513_Apple-1.bin", "rb");
	}
	if (f) {
		size_t read_bytes = fread(charmap_data, 1, 2048, f);
		fclose(f);
		if (read_bytes == 2048) {
			charmap_size = 2048;
			return (true);
		}
	}

	/* Fallback to embedded authentic Apple-1 2513 character ROM */
	memcpy(charmap_data, embedded_2513_charmap, 2048);
	charmap_size = 2048;
	return (true);
}

static void
scroll_up(void)
{
	for (int y = 0; y < 23; y++) {
		memcpy(vram[y], vram[y + 1], 40);
	}
	memset(vram[23], 0x20, 40);
}

static struct expansion_card *
get_or_add_aci(void)
{
	struct expansion_card *aci_card;
	const char *aci_path;
	int i;

	if (g_bus == NULL)
		return (NULL);
	for (i = 0; i < g_bus->num_cards; i++) {
		if (strcmp(g_bus->cards[i]->name, "ACI") == 0) {
			return (g_bus->cards[i]);
		}
	}

	aci_path = NULL;
	for (i = 0; i < NF - 1; i++) {
		if (fields[i].flag == 'a') {
			if (fields[i].sval[0] != '\0') {
				aci_path = fields[i].sval;
			}
			break;
		}
	}

	aci_card = aci_create(g_bus, aci_path);

	if (aci_card != NULL) {
		bus_add_card(g_bus, aci_card);
		strncpy(status_text,
		    "ACI CARD LOADED",
		    sizeof(status_text) - 1);
	} else if (aci_path == NULL) {
		strncpy(status_text,
		    "NO ACI ROM PATH SET",
		    sizeof(status_text) - 1);
	} else {
		snprintf(status_text,
		    sizeof(status_text),
		    "ACI ROM LOAD FAILED");
	}
	return (aci_card);
}

/* ── CHARMAP RENDERERS ───────────────────────────────────────────────────── */

/* Map an ASCII code to the 2513 ROM index. */
int
ascii_to_rom_idx(uint8_t ascii)
{
	return ((int)((ascii - 0x40) & 0x3F));
}

/* Draw a character at 4x scale with soft phosphor glow. */
static void
draw_char_4x_glow(SDL_Renderer *rend,
    uint8_t glyphIndex,
    int cell_x,
    int cell_y,
    const struct palette *pal)
{
	if (!charmap_loaded)
		return;
	int rom_idx = ascii_to_rom_idx(glyphIndex);
	const uint8_t *glyph = &charmap_data[rom_idx * 8];

	for (int r = 0; r < GLYPH_ROWS; ++r) {
		uint8_t bits = glyph[r +
		    1]; /* row 0 is always blank; glyphs start at row 1 */
		for (int c = 0; c < GLYPH_COLS; ++c) {
			bool active = (bits & (1 << (4 - c))) != 0;
			if (active) {
				int px = cell_x + c * 4;
				int py = cell_y + r * 4;
				/* Glow: 6×6 box centred on the 4x4 pixel, semi-transparent */
				SDL_SetRenderDrawBlendMode(rend,
				    SDL_BLENDMODE_BLEND);
				SDL_FRect glow = { (float)(px - 1),
					(float)(py - 1),
					6.0f,
					6.0f };
				SDL_SetRenderDrawColor(rend,
				    pal->glow.r,
				    pal->glow.g,
				    pal->glow.b,
				    pal->glow.a);
				SDL_RenderFillRect(rend, &glow);
				SDL_SetRenderDrawBlendMode(rend,
				    SDL_BLENDMODE_NONE);
				/* Solid dot */
				SDL_FRect dot = { (float)px,
					(float)py,
					4.0f,
					4.0f };
				SDL_SetRenderDrawColor(rend,
				    pal->pixel.r,
				    pal->pixel.g,
				    pal->pixel.b,
				    pal->pixel.a);
				SDL_RenderFillRect(rend, &dot);
			}
		}
	}
}

/* Sidebar and config text rendering with custom integer scaling. */
void
draw_text_scaled(SDL_Renderer *rend,
    const char *str,
    int x,
    int y,
    int scale,
    SDL_Color color)
{
	if (!charmap_loaded)
		return;
	int cur_x = x;
	while (*str) {
		uint8_t ch = (uint8_t)*str;
		if (ch >= 'a' && ch <= 'z')
			ch -= 32; /* force uppercase */
		int rom_idx = ascii_to_rom_idx(ch);
		const uint8_t *glyph = &charmap_data[rom_idx * 8];
		SDL_SetRenderDrawColor(rend, color.r, color.g, color.b, color.a);
		for (int r = 0; r < GLYPH_ROWS; ++r) {
			uint8_t bits = glyph[r + 1]; /* row 0 is blank */
			for (int c = 0; c < GLYPH_COLS; ++c) {
				if (bits & (1 << (4 - c))) {
					SDL_FRect dot = { (float)(cur_x +
							      c * scale),
						(float)(y + r * scale),
						(float)scale,
						(float)scale };
					SDL_RenderFillRect(rend, &dot);
				}
			}
		}
		cur_x += (GLYPH_COLS + 1) * scale;
		str++;
	}
}

void
draw_text_2x(SDL_Renderer *rend, const char *str, int x, int y, SDL_Color color)
{
	draw_text_scaled(rend, str, x, y, 2, color);
}

/* ── BUTTON GUI BUILDER (sidebar, not scaled) ────────────────────────────── */

bool
draw_button(SDL_Renderer *rend,
    int x,
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

	SDL_SetRenderDrawColor(rend,
	    hov ? BTNHV.r : BTNBG.r,
	    hov ? BTNHV.g : BTNBG.g,
	    hov ? BTNHV.b : BTNBG.b,
	    255);
	SDL_RenderFillRect(rend, &btn_rect);
	SDL_SetRenderDrawColor(rend, tint.r, tint.g, tint.b, 255);
	SDL_RenderRect(rend, &btn_rect);

	int cw = (GLYPH_COLS + 1) * 2;
	int ch2 = GLYPH_ROWS * 2;
	draw_text_2x(rend,
	    lbl,
	    x + (w - (int)strlen(lbl) * cw) / 2,
	    y + (h - ch2) / 2,
	    tint);
	return (hov);
}

void
set_config_status(const char *m, int ms)
{
	snprintf(config_status_msg, sizeof(config_status_msg), "%s", m);
	config_status_until = SDL_GetTicks() + (uint64_t)ms;
}

/* ── MENU BAR & DROPDOWNS DRAWING ────────────────────────────────────────── */

static void
draw_menu_bar(void)
{
	/* Draw background */
	SDL_Color menu_bg = { 25, 25, 30, 255 };
	SDL_Color menu_border = { 45, 45, 50, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color text_hover = { 51, 255, 51, 255 };

	SDL_FRect bar_rect = { 0, 0, (float)SCREEN_W, (float)MENU_BAR_H };
	SDL_SetRenderDrawColor(renderer,
	    menu_bg.r,
	    menu_bg.g,
	    menu_bg.b,
	    menu_bg.a);
	SDL_RenderFillRect(renderer, &bar_rect);

	SDL_SetRenderDrawColor(renderer,
	    menu_border.r,
	    menu_border.g,
	    menu_border.b,
	    menu_border.a);
	SDL_RenderLine(renderer, 0, MENU_BAR_H - 1, SCREEN_W, MENU_BAR_H - 1);

	/* Get mouse state */
	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	/* FILE menu item */
	bool file_hover = (mx >= 10 && mx < 70 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "FILE",
	    15,
	    6,
	    (file_hover || file_menu_open) ? text_hover : text_color);

	/* CONFIG menu item */
	bool config_hover =
	    (mx >= 80 && mx < 160 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "CONFIG",
	    85,
	    6,
	    (config_hover || config_modal_open) ? text_hover : text_color);

	/* EMULATION menu item */
	bool emulation_hover =
	    (mx >= 170 && mx < 280 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "EMULATION",
	    175,
	    6,
	    (emulation_hover || emulation_menu_open) ? text_hover : text_color);

	/* DISPLAY menu item */
	bool display_hover =
	    (mx >= 290 && mx < 380 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "DISPLAY",
	    295,
	    6,
	    (display_hover || display_menu_open) ? text_hover : text_color);

	/* DEBUG menu item */
	bool debug_hover =
	    (mx >= 390 && mx < 460 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "DEBUG",
	    395,
	    6,
	    (debug_hover || debug_menu_open) ? text_hover : text_color);

	/* TRACE menu item */
	bool trace_hover =
	    (mx >= 470 && mx < 540 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "TRACE",
	    475,
	    6,
	    (trace_hover || trace_menu_open) ? text_hover : text_color);

	/* HELP menu item */
	bool help_hover = (mx >= 550 && mx < 610 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer,
	    "HELP",
	    555,
	    6,
	    (help_hover || help_menu_open) ? text_hover : text_color);
}

static void
draw_file_dropdown(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = 10;
	int drop_y = MENU_BAR_H;
	int drop_w = 260;
	int item_h = 30;
	int num_items = 5;
	int drop_h = item_h * num_items + 6;

	/* Background and border */
	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	/* Get mouse state */
	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = { "LOAD TAPE (.ACI)",
		"LOAD ROM TO RAM...",
		"LOAD WOZMON",
		"LOAD WOZMON TXT...",
		"QUIT" };

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				(float)item_h };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}
}

static void
draw_emulation_dropdown(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = 170;
	int drop_y = MENU_BAR_H;
	int drop_w = 320;
	int item_h = 30;
	int num_items = 5;
	int drop_h = item_h * num_items + 6;

	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	char item0[64];
	snprintf(item0,
	    sizeof(item0),
	    "POWER: %s",
	    machine_powered ? "CONNECTED (ON)" : "DISCONNECTED (OFF)");

	char item1[64];
	snprintf(item1,
	    sizeof(item1),
	    "STATE: %s",
	    emulation_paused ? "PAUSED" : "RUNNING");

	char item3[64];
	bool uncapped = g_bus && g_bus->opts.uncapped;
	snprintf(item3,
	    sizeof(item3),
	    "SPEED: %s",
	    uncapped ? "UNCAPPED (MAX)" : "CAPPED (1.02 MHz)");

	const char *items[] = { item0,
		item1,
		"RESET struct cpu",
		"CLEAR SCREEN (VRAM)",
		item3 };

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				(float)item_h };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}
}

static void
draw_display_dropdown(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = 280;
	int drop_y = MENU_BAR_H;
	int drop_w = 260;
	int item_h = 30;
	int num_items = 4;
	int drop_h = item_h * num_items + 6;

	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	char item0[64];
	char item1[64];
	char item2[64];
	char item3[64];
	snprintf(item0,
	    sizeof(item0),
	    "PHOSPHOR: GREEN %s",
	    (monitor_tint == MONITOR_GREEN) ? "(ON)" : "");
	snprintf(item1,
	    sizeof(item1),
	    "PHOSPHOR: AMBER %s",
	    (monitor_tint == MONITOR_AMBER) ? "(ON)" : "");
	snprintf(item2,
	    sizeof(item2),
	    "PHOSPHOR: MONO %s",
	    (monitor_tint == MONITOR_MONO) ? "(ON)" : "");
	snprintf(item3,
	    sizeof(item3),
	    "SCANLINES: %s",
	    scanlines_enabled ? "ON" : "OFF");

	const char *items[] = { item0, item1, item2, item3 };

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				(float)item_h };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}
}

static void
draw_debug_dropdown(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = 380;
	int drop_y = MENU_BAR_H;
	int drop_w = 260;
	int item_h = 30;
	int num_items = 4;
	int drop_h = item_h * num_items + 6;

	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = { term_debug_is_open() ? "CLOSE DEBUG WINDOW"
						     : "OPEN DEBUG WINDOW",
		"STEP INSTRUCTION (s)",
		"CONTINUE RUNNING (c)",
		"CLEAR BREAKPOINTS" };

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				(float)item_h };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}
}

static void
draw_trace_dropdown(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = 460;
	int drop_y = MENU_BAR_H;
	int drop_w = 260;
	int item_h = 30;
	int num_items = 3;
	int drop_h = item_h * num_items + 6;

	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = { term_trace_is_open() ? "CLOSE TRACE WINDOW"
						     : "OPEN TRACE WINDOW",
		"CLEAR TRACE BUFFER",
		"EXPORT TRACE TO FILE" };

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				(float)item_h };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}
}

static void
draw_help_dropdown(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = 540;
	int drop_y = MENU_BAR_H;
	int drop_w = 260;
	int item_h = 30;
	int num_items = 2;
	int drop_h = item_h * num_items + 6;

	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = { "EMULATOR MANUAL (.MD)",
		"APPLE-1 MANUAL (PDF)" };

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				(float)item_h };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}
}

/* ── FILE MENU ACTIONS ───────────────────────────────────────────────────── */

#ifdef __APPLE__
static void
convert_ext_to_applescript(const char *ext, char *out_types, size_t max_len)
{
	out_types[0] = '\0';
	if (!ext || strcmp(ext, "*.*") == 0) {
		return;
	}
	size_t len = 0;
	len += snprintf(out_types + len, max_len - len, "of type {");
	const char *p = ext;
	bool first = true;
	while (*p) {
		while (*p && (*p == ' ' || *p == '*')) {
			p++;
		}
		if (!*p)
			break;
		if (*p == '.') {
			p++;
		}
		char type[32];
		int t_idx = 0;
		while (*p && *p != ' ' && t_idx < 31) {
			type[t_idx++] = *p++;
		}
		type[t_idx] = '\0';

		if (!first) {
			len += snprintf(out_types + len, max_len - len, ", ");
		}
		first = false;
		len += snprintf(out_types + len, max_len - len, "\"%s\"", type);
	}
	len += snprintf(out_types + len, max_len - len, "}");
}
#endif

static void
trigger_sel_tape(void)
{
#ifdef __APPLE__
	FILE *pipe = popen("osascript -e 'POSIX path of (choose file with "
			   "prompt \"Select ACI Cassette Tape\" of type "
			   "{\"aci\"})' 2>/dev/null",
	    "r");
#else
	/* Try zenity first (GNOME), then kdialog (KDE) */
	FILE *pipe = popen("zenity --file-selection"
			   " --title='Select ACI Cassette Tape'"
			   " --file-filter='ACI Cassettes (*.aci) | *.aci'"
			   " 2>/dev/null"
			   " || kdialog --getopenfilename . '*.aci'"
			   " 2>/dev/null",
	    "r");
#endif
	if (pipe) {
		char path[512] = { 0 };
		if (fgets(path, sizeof(path), pipe)) {
			path[strcspn(path, "\n")] = 0; /* strip newline */
			if (strlen(path) > 0) {
				/* Insert at front of cassette list and select it */
				if (num_cassettes < MAX_CASSETTES)
					num_cassettes++;
				/* Shift existing entries down */
				for (int i = num_cassettes - 1; i > 0; i--)
					snprintf(cassette_files[i],
					    sizeof(cassette_files[i]),
					    "%s",
					    cassette_files[i - 1]);
				snprintf(cassette_files[0],
				    sizeof(cassette_files[0]),
				    "%s",
				    path);
				selected_cassette_idx = 0;
				const char *base = strrchr(path, '/');
				if (base)
					base++;
				else
					base = path;
				snprintf(status_text,
				    sizeof(status_text),
				    "TAPE: %s",
				    base);
			}
		}
		pclose(pipe);
	} else {
		strncpy(status_text,
		    "NO DIALOG TOOL FOUND",
		    sizeof(status_text) - 1);
	}
}

typedef bool (*rom_loader_fn)(const char *path, void *ctx);

static void
load_rom_pick(const char *title,
    const char *ext,
    const char *ok_msg,
    const char *fail_msg,
    rom_loader_fn loader,
    void *ctx)
{
	char picked[512] = { 0 };
	if (!pick_file_dialog(picked, sizeof(picked), title, ext)) {
		/* User cancelled */
		return;
	}
	if (loader(picked, ctx)) {
		strncpy(status_text, ok_msg, sizeof(status_text) - 1);
	} else {
		strncpy(status_text, fail_msg, sizeof(status_text) - 1);
	}
}

static bool
prompt_hex_address(const char *title,
    const char *msg,
    const char *default_val,
    uint16_t *out_val)
{
	char cmd[512];
#ifdef __APPLE__
	snprintf(cmd,
	    sizeof(cmd),
	    "osascript -e 'text returned of (display dialog \"%s\" default "
	    "answer \"%s\" with title \"%s\")' 2>/dev/null",
	    msg,
	    default_val,
	    title);
#else
	snprintf(cmd,
	    sizeof(cmd),
	    "zenity --entry --title='%s' --text='%s' --entry-text='%s' "
	    "2>/dev/null "
	    "|| kdialog --inputbox '%s' '%s' 2>/dev/null",
	    title,
	    msg,
	    default_val,
	    msg,
	    default_val);
#endif
	FILE *p = popen(cmd, "r");
	if (!p)
		return (false);
	char buf[64] = { 0 };
	bool ok = false;
	if (fgets(buf, sizeof(buf), p)) {
		buf[strcspn(buf, "\n")] = '\0';
		char *end;
		long val = strtol(buf, &end, 16);
		if (end != buf) {
			*out_val = (uint16_t)val;
			ok = true;
		}
	}
	pclose(p);
	return (ok);
}

static void
load_any_rom(void)
{
	char picked[512] = { 0 };
	if (!pick_file_dialog(picked,
		sizeof(picked),
		"Select ROM/Binary File",
		"*.rom *.bin")) {
		return;
	}
	uint16_t addr = 0xE000;
	if (!prompt_hex_address("Load Address",
		"Enter hexadecimal load address:",
		"E000",
		&addr)) {
		return;
	}
	if (g_bus && bus_load_bin(g_bus, picked, addr)) {
		snprintf(status_text,
		    sizeof(status_text),
		    "ROM LOADED AT $%04X",
		    addr);
	} else {
		strncpy(status_text,
		    "ROM LOAD FAILED",
		    sizeof(status_text) - 1);
	}
}

static void
load_wozmon_txt_gui(void)
{
	char picked[512] = { 0 };
	if (!pick_file_dialog(picked,
		sizeof(picked),
		"Select Woz Monitor Text Dump",
		"*.*")) {
		return;
	}
	uint16_t run_addr;
	bool has_run_addr;
	if (g_bus &&
	    bus_load_wozmon_txt(g_bus, picked, &run_addr, &has_run_addr)) {
		if (has_run_addr) {
			g_bus->ram[RESET_VECTOR] = run_addr & 0xFF;
			g_bus->ram[RESET_VECTOR + 1] = run_addr >> 8;
			reset_pending = true;
			snprintf(status_text,
			    sizeof(status_text),
			    "WOZMON TXT LOADED - RUNNING AT $%04X",
			    run_addr);
		} else {
			strncpy(status_text,
			    "WOZMON TXT LOADED",
			    sizeof(status_text) - 1);
		}
	} else {
		strncpy(status_text,
		    "WOZMON TXT LOAD FAILED",
		    sizeof(status_text) - 1);
	}
}

static bool
load_wozmon_fn(const char *path, void *ctx)
{
	(void)ctx;
	if (!g_bus || !bus_load_bin(g_bus, path, 0xFF00))
		return (false);
	reset_pending = true;
	return (true);
}

static void
handle_file_menu_action(int idx)
{
	switch (idx) {
	case 0: /* LOAD TAPE (.aci) */
		trigger_sel_tape();
		break;
	case 1: /* LOAD ROM TO RAM... */
		load_any_rom();
		break;
	case 2: /* LOAD WOZMON */
		load_rom_pick("Select Wozmon ROM",
		    "*.rom *.bin",
		    "WOZMON LOADED.",
		    "ERROR: FAILED TO LOAD WOZMON ROM",
		    load_wozmon_fn,
		    NULL);
		break;
	case 3: /* LOAD WOZMON TXT... */
		load_wozmon_txt_gui();
		break;
	case 4: /* QUIT */
		exit(0);
		break;
	}
}

static void
handle_menu_click(int x, int y)
{
	(void)y;
	context_menu_open = false;
	bool any_open = file_menu_open || config_modal_open ||
	    emulation_menu_open || display_menu_open || debug_menu_open ||
	    trace_menu_open || help_menu_open;
	file_menu_open = false;
	emulation_menu_open = false;
	display_menu_open = false;
	debug_menu_open = false;
	trace_menu_open = false;
	help_menu_open = false;

	if (x >= 10 && x < 70) {
		file_menu_open = !any_open;
		config_modal_open = false;
		return;
	}
	if (x >= 80 && x < 160) {
		config_modal_open = !config_modal_open;
		if (config_modal_open) {
			config_scroll_offset = 0;
			term_config_init(); /* Reload settings from config file */
		}
		return;
	}
	if (x >= 170 && x < 280) {
		emulation_menu_open = true;
		return;
	}
	if (x >= 290 && x < 380) {
		display_menu_open = true;
		return;
	}
	if (x >= 390 && x < 460) {
		debug_menu_open = true;
		return;
	}
	if (x >= 470 && x < 540) {
		trace_menu_open = true;
		return;
	}
	if (x >= 550 && x < 610) {
		help_menu_open = true;
		return;
	}
}

bool
pick_file_dialog(char *out_path,
    size_t max_len,
    const char *title,
    const char *ext)
{
	char cmd[512];
#ifdef __APPLE__
	char types_clause[256];
	convert_ext_to_applescript(ext, types_clause, sizeof(types_clause));
	snprintf(cmd,
	    sizeof(cmd),
	    "osascript -e 'POSIX path of (choose file with prompt \"%s\" %s)' "
	    "2>/dev/null",
	    title,
	    types_clause);
#else
	snprintf(cmd,
	    sizeof(cmd),
	    "zenity --file-selection --title='%s' --file-filter='%s | %s' "
	    "2>/dev/null "
	    "|| kdialog --getopenfilename . '%s' 2>/dev/null",
	    title,
	    title,
	    ext,
	    ext);
#endif
	FILE *p = popen(cmd, "r");
	if (!p)
		return (false);
	bool ok = false;
	if (fgets(out_path, (int)max_len, p)) {
		out_path[strcspn(out_path, "\n")] = '\0';
		ok = strlen(out_path) > 0;
	}
	pclose(p);
	return (ok);
}

static bool
pick_save_dialog(char *out_path,
    size_t max_len,
    const char *title,
    const char *ext)
{
	char cmd[512];
#ifdef __APPLE__
	(void)ext;
	snprintf(cmd,
	    sizeof(cmd),
	    "osascript -e 'POSIX path of (choose file name with prompt "
	    "\"%s\")' 2>/dev/null",
	    title);
#else
	snprintf(cmd,
	    sizeof(cmd),
	    "zenity --file-selection --save --confirm-overwrite --title='%s' "
	    "--file-filter='%s | %s' "
	    "2>/dev/null "
	    "|| kdialog --getsavefilename . '%s' 2>/dev/null",
	    title,
	    title,
	    ext,
	    ext);
#endif
	FILE *p = popen(cmd, "r");
	if (!p)
		return (false);
	bool ok = false;
	if (fgets(out_path, (int)max_len, p)) {
		out_path[strcspn(out_path, "\n")] = '\0';
		ok = strlen(out_path) > 0;
	}
	pclose(p);
	return (ok);
}

static void
show_error_dialog(const char *title, const char *message)
{
	char cmd[1024];
#ifdef __APPLE__
	snprintf(cmd,
	    sizeof(cmd),
	    "osascript -e 'display dialog \"%s\" with title \"%s\" buttons "
	    "{\"OK\"} default button \"OK\" with icon stop' 2>/dev/null",
	    message,
	    title);
#else
	snprintf(cmd,
	    sizeof(cmd),
	    "zenity --error --title='%s' --text='%s' --no-markup 2>/dev/null "
	    "|| kdialog --title '%s' --error '%s' 2>/dev/null",
	    title,
	    message,
	    title,
	    message);
#endif
	FILE *p = popen(cmd, "r");
	if (p) {
		pclose(p);
	}
}

/*
 * Build a plain-text snapshot of the screen using the same per-cell
 * decoding rules as render_gui:
 *   0x00  cursor (@)   → treated as a space (position marker, not content)
 *   0xFF  sweep  (_)   → treated as a space (boot animation, not content)
 *   0x20  blank        → space
 *   else               → the ASCII glyph stored directly in VRAM
 *
 * Each row is right-trimmed; trailing blank rows are stripped.  The
 * result is pushed to the system clipboard via native tools (xclip,
 * xsel, wl-copy) so we don't depend on SDL's X11/Wayland selection
 * ownership mechanism.
 */
static void
do_copy_screen(void)
{
	/* TERM_ROWS rows × (TERM_COLS chars + 1 newline) + NUL */
	char buf[TERM_ROWS * (TERM_COLS + 1) + 1];
	int pos = 0;

	for (int r = 0; r < TERM_ROWS; r++) {
		/* Right-trim: find the last column with a real glyph. */
		int last_col = -1;
		for (int c = 0; c < TERM_COLS; c++) {
			uint8_t v = vram[r][c];
			if (v != 0x00 && v != 0xFF && v != 0x20)
				last_col = c;
		}
		/* Emit characters up to last_col. */
		for (int c = 0; c <= last_col; c++) {
			uint8_t v = vram[r][c];
			buf[pos++] = (v == 0x00 || v == 0xFF) ? ' ' : (char)v;
		}
		buf[pos++] = '\n';
	}
	/* Strip trailing blank rows. */
	while (pos > 0 && buf[pos - 1] == '\n')
		pos--;
	buf[pos] = '\0';

	/*
	 * Push to system clipboard using native tools — SDL_SetClipboardText
	 * is deliberately not used here as it produces garbage on X11/Wayland.
	 * Error is shown in the status bar, NOT on stdout/stderr.
	 */
	bool ok = false;
#ifdef __APPLE__
	const char *tools[] = { "pbcopy", NULL };
#else
	const char *tools[] = { "xclip -selection clipboard",
		"xsel --clipboard --input",
		"wl-copy",
		NULL };
#endif
	for (int i = 0; tools[i] && !ok; i++) {
		FILE *p = popen(tools[i], "w");
		if (p) {
			fputs(buf, p);
			ok = (pclose(p) == 0);
		}
	}
	if (ok) {
		snprintf(status_text,
		    sizeof(status_text),
		    "COPIED %d CHARS.",
		    pos);
	} else {
		strncpy(status_text,
		    "COPY FAILED: INSTALL xclip, xsel OR wl-clipboard.",
		    sizeof(status_text) - 1);
		show_error_dialog("Clipboard Error",
		    "Could not copy text to clipboard. Please install xclip, "
		    "xsel, or wl-clipboard.");
	}
}

static char *
get_system_clipboard(bool *success)
{
#ifdef __APPLE__
	const char *tools[] = { "pbpaste", NULL };
#else
	const char *tools[] = { "xclip -selection clipboard -o",
		"xsel --clipboard --output",
		"wl-paste -n",
		NULL };
#endif
	static char clip_buf[65536];
	*success = false;
	for (int i = 0; tools[i]; i++) {
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "%s 2>/dev/null", tools[i]);
		FILE *p = popen(cmd, "r");
		if (p) {
			size_t total = 0;
			size_t n;
			while (total < sizeof(clip_buf) - 1 &&
			    (n = fread(clip_buf + total,
				 1,
				 sizeof(clip_buf) - total - 1,
				 p)) > 0) {
				total += n;
			}
			clip_buf[total] = '\0';
			int status = pclose(p);
			if (status == 0) {
				*success = true;
				return (clip_buf);
			}
		}
	}
	return (NULL);
}

static void
do_paste(void)
{
	bool success = false;
	char *text = get_system_clipboard(&success);
	if (!success) {
		strncpy(status_text,
		    "PASTE FAILED: INSTALL xclip, xsel OR wl-clipboard.",
		    sizeof(status_text) - 1);
		show_error_dialog("Clipboard Error",
		    "Could not paste text from clipboard. Please install "
		    "xclip, "
		    "xsel, or wl-clipboard.");
		return;
	}
	if (!text || text[0] == '\0') {
		return;
	}
	int len = 0;
	for (int i = 0; text[i] && len < (int)sizeof(paste_buf) - 1; i++) {
		char c = text[i];
		if (c == '\n' || c == '\r') {
			paste_buf[len++] = '\r';
			continue;
		}
		if (c >= 'a' && c <= 'z') {
			c -= 32; /* force uppercase */
		}
		if (c >= 0x20 && c <= 0x7E) {
			paste_buf[len++] = c;
		}
	}
	paste_buf[len] = '\0';
	paste_pos = 0;
	paste_len = len;
	strncpy(status_text, "PASTING CLIPBOARD...", sizeof(status_text) - 1);
}

static bool
save_png(const char *filename, SDL_Surface *surface)
{
	SDL_Surface *converted =
	    SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
	if (!converted) {
		return (false);
	}
	int ret = stbi_write_png(filename,
	    converted->w,
	    converted->h,
	    3,
	    converted->pixels,
	    converted->pitch);
	SDL_DestroySurface(converted);
	return (ret != 0);
}

static void
do_save_screenshot(void)
{
	char picked[512];
	if (!pick_save_dialog(picked,
		sizeof(picked),
		"Save Screenshot — use .bmp or .png extension",
		"*.png *.bmp")) {
		return;
	}

	SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
	if (!surface) {
		strncpy(status_text,
		    "SCREENSHOT FAILED.",
		    sizeof(status_text) - 1);
		return;
	}

	char filename[512];
	strncpy(filename, picked, sizeof(filename) - 1);
	filename[sizeof(filename) - 1] = '\0';

	size_t len = strlen(filename);
	bool is_png = false;
	bool is_bmp = false;
	bool unknown_ext = false;

	if (len >= 4) {
		const char *ext = filename + len - 4;
		if (strcasecmp(ext, ".png") == 0) {
			is_png = true;
		} else if (strcasecmp(ext, ".bmp") == 0) {
			is_bmp = true;
		} else {
			unknown_ext = true;
		}
	} else {
		unknown_ext = true;
	}

	if (unknown_ext) {
		strncat(filename, ".bmp", sizeof(filename) - len - 1);
		is_bmp = true;
	}

	bool success = false;
	if (is_png) {
		success = save_png(filename, surface);
	} else if (is_bmp) {
		success = SDL_SaveBMP(surface, filename);
	}

	SDL_DestroySurface(surface);

	if (success) {
		const char *base = strrchr(filename, '/');
		if (base) {
			base++;
		} else {
			base = filename;
		}

		if (unknown_ext) {
			snprintf(status_text,
			    sizeof(status_text),
			    "UNKNOWN EXT - SAVED AS BMP: %s",
			    base);
		} else {
			snprintf(status_text,
			    sizeof(status_text),
			    "SCREENSHOT SAVED: %s",
			    base);
		}
	} else {
		strncpy(status_text,
		    "SCREENSHOT FAILED.",
		    sizeof(status_text) - 1);
	}
}

static void
draw_context_menu(void)
{
	SDL_Color bg = { 18, 18, 24, 255 };
	SDL_Color border = { 51, 255, 51, 255 };
	SDL_Color text_color = { 220, 220, 220, 255 };
	SDL_Color hover_color = { 51, 255, 51, 255 };
	SDL_Color hover_bg = { 30, 55, 30, 255 };

	int drop_x = context_menu_x;
	int drop_y = context_menu_y;
	int drop_w = 260;
	int drop_h = 134;

	if (drop_x + drop_w > SCREEN_W) {
		drop_x = SCREEN_W - drop_w - 2;
	}
	if (drop_y + drop_h > SCREEN_H) {
		drop_y = SCREEN_H - drop_h - 2;
	}
	if (drop_x < 0)
		drop_x = 0;
	if (drop_y < MENU_BAR_H)
		drop_y = MENU_BAR_H;

	context_menu_x = drop_x;
	context_menu_y = drop_y;

	SDL_FRect drop_rect = { (float)drop_x,
		(float)drop_y,
		(float)drop_w,
		(float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = { "COPY SCREEN TEXT",
		"PASTE TO KEYBOARD",
		"SELECT ALL",
		"SAVE SCREENSHOT..." };

	for (int i = 0; i < 4; i++) {
		int iy = drop_y + 3 + i * 30;
		if (i == 3) {
			iy += 8;
		}
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 &&
		    my >= iy && my < iy + 30);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3),
				(float)iy,
				(float)(drop_w - 6),
				30.0f };
			SDL_SetRenderDrawColor(renderer,
			    hover_bg.r,
			    hover_bg.g,
			    hover_bg.b,
			    hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer,
		    items[i],
		    drop_x + 10,
		    iy + 7,
		    hover ? hover_color : text_color);
	}

	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	int sep_y = drop_y + 3 + 90 + 4;
	SDL_RenderLine(renderer,
	    (float)(drop_x + 3),
	    (float)sep_y,
	    (float)(drop_x + drop_w - 3),
	    (float)sep_y);
}

void
reboot_emulator(void)
{
	term_shutdown();
	char *args[] = { g_argv0 ? g_argv0 : "apple1", NULL };
	execvp(args[0], args);
	perror("reboot failed");
	exit(1);
}

/* ── GUI MAIN RENDERING ──────────────────────────────────────────────────── */

static void
render_gui(void)
{
	if (!renderer)
		return;

	const struct palette *pal = &PALETTES[monitor_tint];

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	SDL_SetRenderDrawColor(renderer, 10, 10, 12, 255);
	SDL_RenderClear(renderer);

	/* ── 1. CRT TERMINAL AREA ── */
	SDL_SetRenderDrawColor(renderer,
	    pal->bg.r,
	    pal->bg.g,
	    pal->bg.b,
	    pal->bg.a);
	SDL_FRect crt_bg = { 0,
		(float)MENU_BAR_H,
		(float)CRT_DISP_W,
		(float)CRT_DISP_H };
	SDL_RenderFillRect(renderer, &crt_bg);

	uint64_t now_ms = SDL_GetTicks();
	bool cursor_on = emulation_paused ? true : ((now_ms / 333) % 2 == 0);

	if (machine_powered) {
		for (int row = 0; row < TERM_ROWS; row++) {
			for (int col = 0; col < TERM_COLS; col++) {
				uint8_t val = vram[row][col];
				int gx = col * CELL_W;
				int gy = row * CELL_H + MENU_BAR_H;

				if (val == 0x00) {
					if (cursor_on)
						draw_char_4x_glow(renderer,
						    '@',
						    gx,
						    gy,
						    pal);
				} else if (val == 0xFF) {
					draw_char_4x_glow(renderer,
					    '_',
					    gx,
					    gy,
					    pal);
				} else if (val != 0x20) {
					draw_char_4x_glow(renderer,
					    val,
					    gx,
					    gy,
					    pal);
				}
			}
		}
	} else {
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_FRect crt_off = { 0,
			(float)MENU_BAR_H,
			(float)CRT_DISP_W,
			(float)CRT_DISP_H };
		SDL_RenderFillRect(renderer, &crt_off);
	}

	if (scanlines_enabled) {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer,
		    0,
		    0,
		    0,
		    (uint8_t)(255 * scanline_opacity));
		for (int y = MENU_BAR_H; y < CRT_DISP_H + MENU_BAR_H; y += 4) {
			SDL_RenderLine(renderer, 0, y, CRT_DISP_W, y);
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	/* ── 2. SIDEBAR PANEL ── */
	SDL_FRect sidebar = { (float)CRT_DISP_W,
		(float)MENU_BAR_H,
		(float)SIDEBAR_W,
		(float)CRT_DISP_H };
	SDL_SetRenderDrawColor(renderer, 18, 18, 20, 255);
	SDL_RenderFillRect(renderer, &sidebar);
	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	SDL_RenderLine(renderer, CRT_DISP_W, MENU_BAR_H, CRT_DISP_W, SCREEN_H);

	int sx = CRT_DISP_W + 15;

	SDL_Color amber = { 255, 176, 0, 255 };
	SDL_Color green = { 51, 255, 51, 255 };
	SDL_Color dimmed = { pal->pixel.r / 2,
		pal->pixel.g / 2,
		pal->pixel.b / 2,
		180 };

	char hdr_buf[64];
	if (g_bus) {
		snprintf(hdr_buf,
		    sizeof(hdr_buf),
		    "APPLE-1 (%dKB)",
		    g_bus->ram_size / 1024);
	} else {
		strcpy(hdr_buf, "APPLE-1 CONSOLE");
	}
	draw_text_2x(renderer, hdr_buf, sx, 20 + MENU_BAR_H, amber);
	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	SDL_RenderLine(renderer,
	    CRT_DISP_W,
	    44 + MENU_BAR_H,
	    SCREEN_W,
	    44 + MENU_BAR_H);

	draw_text_2x(renderer, "struct cpu REGISTERS:", sx, 54 + MENU_BAR_H, amber);
	char reg_buf[64];
	if (g_cpu && g_bus) {
		snprintf(reg_buf,
		    sizeof(reg_buf),
		    "A :%02X  X:%02X  Y:%02X",
		    g_cpu->a,
		    g_cpu->x,
		    g_cpu->y);
		draw_text_2x(renderer, reg_buf, sx, 76 + MENU_BAR_H, dimmed);
		snprintf(reg_buf,
		    sizeof(reg_buf),
		    "SP:%02X  PC:%04X  P:%02X",
		    g_cpu->s,
		    g_cpu->pc,
		    g_cpu->p);
		draw_text_2x(renderer, reg_buf, sx, 96 + MENU_BAR_H, dimmed);
		snprintf(reg_buf,
		    sizeof(reg_buf),
		    "FLAGS:%c%c-%c%c%c%c%c  %s",
		    (g_cpu->p & FLAG_NEGATIVE) ? 'N' : '.',
		    (g_cpu->p & FLAG_OVERFLOW) ? 'V' : '.',
		    (g_cpu->p & FLAG_BREAK) ? 'B' : '.',
		    (g_cpu->p & FLAG_DECIMAL) ? 'D' : '.',
		    (g_cpu->p & FLAG_INTERRUPT) ? 'I' : '.',
		    (g_cpu->p & FLAG_ZERO) ? 'Z' : '.',
		    (g_cpu->p & FLAG_CARRY) ? 'C' : '.',
		    g_cpu->halted ? "HALT" : "RUN ");
		draw_text_2x(renderer, reg_buf, sx, 116 + MENU_BAR_H, dimmed);
	}

	SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
	SDL_RenderLine(renderer,
	    CRT_DISP_W,
	    136 + MENU_BAR_H,
	    SCREEN_W,
	    136 + MENU_BAR_H);

	draw_text_2x(renderer, "CONTROLS:", sx, 146 + MENU_BAR_H, amber);
	draw_button(renderer,
	    sx,
	    166 + MENU_BAR_H,
	    115,
	    28,
	    "RESET",
	    green,
	    mx,
	    my);
	draw_button(renderer,
	    sx + 125,
	    166 + MENU_BAR_H,
	    145,
	    28,
	    "CLR SCREEN",
	    green,
	    mx,
	    my);

	draw_button(renderer,
	    sx,
	    204 + MENU_BAR_H,
	    270,
	    28,
	    emulation_paused ? "RESUME EMULATION" : "PAUSE EMULATION",
	    green,
	    mx,
	    my);

	char speed_label[48];
	if (g_bus) {
		snprintf(speed_label,
		    sizeof(speed_label),
		    "SPEED:%s",
		    g_bus->opts.uncapped ? "UNCAPPED" : "1.02MHZ");
	} else {
		strcpy(speed_label, "SPEED:CAPPED");
	}
	draw_button(renderer,
	    sx,
	    242 + MENU_BAR_H,
	    270,
	    28,
	    speed_label,
	    green,
	    mx,
	    my);

	SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
	SDL_RenderLine(renderer,
	    CRT_DISP_W,
	    280 + MENU_BAR_H,
	    SCREEN_W,
	    280 + MENU_BAR_H);

	draw_text_2x(renderer, "CASSETTE DECK:", sx, 290 + MENU_BAR_H, amber);
	char tape_disp[32];
	if (selected_cassette_idx >= 0 &&
	    selected_cassette_idx < num_cassettes) {
		const char *tape_base = cassette_files[selected_cassette_idx];
		const char *slash = strrchr(tape_base, '/');
		if (slash)
			tape_base = slash + 1;
		snprintf(tape_disp, sizeof(tape_disp), "%.20s", tape_base);
	} else {
		snprintf(tape_disp, sizeof(tape_disp), "NO TAPE");
	}
	draw_text_2x(renderer, tape_disp, sx, 312 + MENU_BAR_H, dimmed);

	draw_button(renderer, sx, 338 + MENU_BAR_H, 35, 24, "<", green, mx, my);
	draw_button(renderer,
	    sx + 45,
	    338 + MENU_BAR_H,
	    180,
	    24,
	    "SEL TAPE",
	    green,
	    mx,
	    my);
	draw_button(renderer,
	    sx + 235,
	    338 + MENU_BAR_H,
	    35,
	    24,
	    ">",
	    green,
	    mx,
	    my);
	draw_button(renderer,
	    sx,
	    372 + MENU_BAR_H,
	    130,
	    24,
	    "PLAY/LOAD",
	    green,
	    mx,
	    my);
	draw_button(renderer,
	    sx + 140,
	    372 + MENU_BAR_H,
	    130,
	    24,
	    "REC/SAVE",
	    green,
	    mx,
	    my);

	struct field *f_baud = NULL;
	for (int i = 0; i < NF - 1; i++) {
		if (fields[i].flag == 'B') {
			f_baud = &fields[i];
			break;
		}
	}
	const char *baud_val =
	    (f_baud && f_baud->cidx >= 0 && f_baud->cidx < f_baud->nchoices)
	    ? f_baud->choices[f_baud->cidx]
	    : "300";
	char baud_label[64];
	if (strcmp(baud_val, "Fast") == 0) {
		snprintf(baud_label, sizeof(baud_label), "TERM BAUD: FAST");
	} else {
		snprintf(baud_label,
		    sizeof(baud_label),
		    "TERM BAUD: %s",
		    baud_val);
	}
	draw_button(renderer,
	    sx,
	    406 + MENU_BAR_H,
	    270,
	    28,
	    baud_label,
	    green,
	    mx,
	    my);

	SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
	SDL_RenderLine(renderer,
	    CRT_DISP_W,
	    446 + MENU_BAR_H,
	    SCREEN_W,
	    446 + MENU_BAR_H);

	SDL_FRect sbar = { (float)(CRT_DISP_W + 10),
		(float)(SCREEN_H - 42),
		(float)(SIDEBAR_W - 20),
		32 };
	SDL_SetRenderDrawColor(renderer, 22, 22, 26, 255);
	SDL_RenderFillRect(renderer, &sbar);
	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	SDL_RenderRect(renderer, &sbar);
	draw_text_2x(renderer,
	    status_text,
	    CRT_DISP_W + 14,
	    SCREEN_H - 34,
	    amber);

	/* ── 3. MENU BAR & DROP DOWN & CONFIG MODAL ── */
	draw_menu_bar();

	if (file_menu_open) {
		draw_file_dropdown();
	}
	if (emulation_menu_open) {
		draw_emulation_dropdown();
	}
	if (display_menu_open) {
		draw_display_dropdown();
	}
	if (debug_menu_open) {
		draw_debug_dropdown();
	}
	if (trace_menu_open) {
		draw_trace_dropdown();
	}
	if (help_menu_open) {
		draw_help_dropdown();
	}

	if (config_modal_open) {
		term_config_modal_render(renderer);
	}

	if (context_menu_open) {
		draw_context_menu();
	}

	SDL_RenderPresent(renderer);
}

/* ── MOUSE CLICK HANDLING ────────────────────────────────────────────────── */

static void
handle_mouse_event(const SDL_MouseButtonEvent *button)
{
	if (button->button == SDL_BUTTON_RIGHT) {
		/* Only open over the CRT area */
		int adj_y = (int)button->y - MENU_BAR_H;
		if (adj_y >= 0 && (int)button->x < CRT_DISP_W) {
			context_menu_open = true;
			context_menu_x = (int)button->x;
			context_menu_y = (int)button->y;
			/* close any open top-bar menus */
			file_menu_open = emulation_menu_open =
			    display_menu_open = debug_menu_open =
				trace_menu_open = help_menu_open = false;
		}
		return;
	}

	if (button->button != SDL_BUTTON_LEFT)
		return;

	int x = (int)button->x;
	int y = (int)button->y;

	if (context_menu_open) {
		/* hit-test each item, dispatch action, close menu */
		int drop_x = context_menu_x;
		int drop_y = context_menu_y;
		int drop_w = 260;
		int drop_h = 134;

		if (drop_x + drop_w > SCREEN_W) {
			drop_x = SCREEN_W - drop_w - 2;
		}
		if (drop_y + drop_h > SCREEN_H) {
			drop_y = SCREEN_H - drop_h - 2;
		}
		if (drop_x < 0)
			drop_x = 0;
		if (drop_y < MENU_BAR_H)
			drop_y = MENU_BAR_H;

		if (x >= drop_x + 3 && x < drop_x + drop_w - 3) {
			if (y >= drop_y + 3 && y < drop_y + 33) {
				do_copy_screen();
			} else if (y >= drop_y + 33 && y < drop_y + 63) {
				do_paste();
			} else if (y >= drop_y + 63 && y < drop_y + 93) {
				do_copy_screen(); /* Select All = Copy */
			} else if (y >= drop_y + 101 && y < drop_y + 131) {
				do_save_screenshot();
			}
		}
		context_menu_open = false;
		return;
	}

	if (config_modal_open) {
		bool dummy = false;
		term_config_modal_handle_click(x, y, false, &dummy);
		return;
	}

	if (file_menu_open) {
		int drop_x = 10;
		int drop_y = MENU_BAR_H;
		int drop_w = 260;
		int item_h = 30;
		int num_items = 5;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y &&
		    y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				handle_file_menu_action(click_idx);
			}
		}
		file_menu_open = false;
		return;
	}

	if (emulation_menu_open) {
		int drop_x = 170;
		int drop_y = MENU_BAR_H;
		int drop_w = 320;
		int item_h = 30;
		int num_items = 5;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y &&
		    y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0: /* Power Plug */
					machine_powered = !machine_powered;
					if (!machine_powered) {
						for (int r = 0; r < 24; r++) {
							memset(vram[r],
							    0x20,
							    40);
						}
						cx = 0;
						cy = 0;
						vram[cy][cx] = 0x00;
						screen_dirty = true;
						emulation_paused = false;
						strncpy(status_text,
						    "POWER DISCONNECTED.",
						    sizeof(status_text) - 1);
					} else {
						for (int r = 0; r < 24; r++) {
							for (int c = 0; c < 40;
							    c++) {
								vram[r][c] =
								    (c % 2)
								    ? 0x00
								    : 0xFF;
							}
						}
						cx = 0;
						cy = 0;
						boot_sweep = true;
						if (g_cpu) {
							g_cpu->halted = true;
						}
						if (g_bus) {
							if (g_bus->opts
								.randomize_cold_boot) {
								for (uint32_t i =
									 0;
								    i <
								    g_bus->ram_size;
								    i++) {
									g_bus->ram
									    [i] =
									    (rand() &
										1)
									    ? 0xFF
									    : 0x00;
								}
							} else {
								memset(
								    g_bus->ram,
								    0,
								    g_bus
									->ram_size);
							}
						}
						strncpy(status_text,
						    "POWER CONNECTED.",
						    sizeof(status_text) - 1);
					}
					break;
				case 1: /* Pause Emulation */
					if (machine_powered) {
						emulation_paused =
						    !emulation_paused;
						if (emulation_paused) {
							strncpy(status_text,
							    "EMULATION PAUSED.",
							    sizeof(
								status_text) -
								1);
						} else {
							strncpy(status_text,
							    "EMULATION "
							    "RESUMED.",
							    sizeof(
								status_text) -
								1);
						}
					}
					break;
				case 2: /* Reset */
					reset_pending = true;
					strncpy(status_text,
					    "struct cpu RESET.",
					    sizeof(status_text) - 1);
					break;
				case 3: /* Clear screen */
					for (int r = 0; r < 24; r++) {
						memset(vram[r], 0x20, 40);
					}
					cx = 0;
					cy = 0;
					vram[cy][cx] = 0x00;
					screen_dirty = true;
					strncpy(status_text,
					    "VRAM CLEARED.",
					    sizeof(status_text) - 1);
					break;
				case 4: /* Speed Toggle */
					if (g_bus) {
						g_bus->opts.uncapped =
						    !g_bus->opts.uncapped;
						if (g_bus->opts.uncapped) {
							g_bus->opts
							    .emulate_dram_refresh =
							    false;
							strncpy(status_text,
							    "SPEED: UNCAPPED",
							    sizeof(
								status_text) -
								1);
							if (term_trace_is_open()) {
								term_trace_toggle();
							}
						} else {
							g_bus->opts
							    .emulate_dram_refresh =
							    true;
							strncpy(status_text,
							    "SPEED: 1.02 MHZ",
							    sizeof(
								status_text) -
								1);
						}
					}
					break;
				}
			}
		}
		emulation_menu_open = false;
		return;
	}

	if (display_menu_open) {
		int drop_x = 280;
		int drop_y = MENU_BAR_H;
		int drop_w = 260;
		int item_h = 30;
		int num_items = 4;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y &&
		    y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					monitor_tint = MONITOR_GREEN;
					break;
				case 1:
					monitor_tint = MONITOR_AMBER;
					break;
				case 2:
					monitor_tint = MONITOR_MONO;
					break;
				case 3:
					scanlines_enabled = !scanlines_enabled;
					break;
				}
			}
		}
		display_menu_open = false;
		return;
	}

	if (debug_menu_open) {
		int drop_x = 380;
		int drop_y = MENU_BAR_H;
		int drop_w = 260;
		int item_h = 30;
		int num_items = 4;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y &&
		    y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					term_debug_toggle();
					break;
				case 1:
					term_request_step();
					break;
				case 2:
					if (g_dbg) {
						g_dbg->step_mode = false;
					}
					emulation_paused = false;
					term_close_debugger();
					break;
				case 3:
					if (g_dbg) {
						g_dbg->num_breakpoints = 0;
						strncpy(status_text,
						    "BREAKPOINTS CLEARED.",
						    sizeof(status_text) - 1);
					}
					break;
				}
			}
		}
		debug_menu_open = false;
		return;
	}

	if (trace_menu_open) {
		int drop_x = 460;
		int drop_y = MENU_BAR_H;
		int drop_w = 260;
		int item_h = 30;
		int num_items = 3;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y &&
		    y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					if (g_bus && g_bus->opts.uncapped) {
						strncpy(status_text,
						    "TRACE NEEDS 1.02MHZ",
						    sizeof(status_text) - 1);
					} else {
						term_trace_toggle();
					}
					break;
				case 1:
					/* Reload trace values by triggering toggle reset */
					if (term_trace_is_open()) {
						term_trace_toggle();
						term_trace_toggle();
					}
					strncpy(status_text,
					    "TRACE BUFFER CLEARED.",
					    sizeof(status_text) - 1);
					break;
				case 2:
					term_trace_export();
					break;
				}
			}
		}
		trace_menu_open = false;
		return;
	}

	if (help_menu_open) {
		int drop_x = 540;
		int drop_y = MENU_BAR_H;
		int drop_w = 260;
		int item_h = 30;
		int num_items = 2;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y &&
		    y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					system("xdg-open "
					       "documentation/manual.md &");
					break;
				case 1:
					system("xdg-open "
					       "documentation/"
					       "apple-1_operation_manual.pdf "
					       "&");
					break;
				}
			}
		}
		help_menu_open = false;
		return;
	}

	if (y < MENU_BAR_H) {
		handle_menu_click(x, y);
		return;
	}

	y -= MENU_BAR_H;
	int sx = CRT_DISP_W + 15;

	/* Reset Button (sx, 166, 115, 28) */
	if (x >= sx && x < sx + 115 && y >= 166 && y < 166 + 28) {
		reset_pending = true;
		strncpy(status_text,
		    "struct cpu RESET.",
		    sizeof(status_text) - 1);
		return;
	}

	/* Clear Screen Button (sx + 125, 166, 145, 28) */
	if (x >= sx + 125 && x < sx + 125 + 145 && y >= 166 && y < 166 + 28) {
		for (int r = 0; r < 24; r++) {
			memset(vram[r], 0x20, 40);
		}
		cx = 0;
		cy = 0;
		vram[cy][cx] = 0x00;
		screen_dirty = true;
		strncpy(status_text, "VRAM CLEARED.", sizeof(status_text) - 1);
		return;
	}

	/* Pause / Resume Button (sx, 204, 270, 28) */
	if (x >= sx && x < sx + 270 && y >= 204 && y < 204 + 28) {
		if (machine_powered) {
			emulation_paused = !emulation_paused;
			if (emulation_paused) {
				strncpy(status_text,
				    "EMULATION PAUSED.",
				    sizeof(status_text) - 1);
			} else {
				strncpy(status_text,
				    "EMULATION RESUMED.",
				    sizeof(status_text) - 1);
			}
		}
		return;
	}

	/* Speed Toggle Button (sx, 242, 270, 28) */
	if (x >= sx && x < sx + 270 && y >= 242 && y < 242 + 28) {
		if (g_bus) {
			g_bus->opts.uncapped = !g_bus->opts.uncapped;
			if (g_bus->opts.uncapped) {
				g_bus->opts.emulate_dram_refresh = false;
				strncpy(status_text,
				    "SPEED: UNCAPPED",
				    sizeof(status_text) - 1);
				if (term_trace_is_open()) {
					term_trace_toggle();
				}
			} else {
				g_bus->opts.emulate_dram_refresh = true;
				strncpy(status_text,
				    "SPEED: 1.02 MHZ",
				    sizeof(status_text) - 1);
			}
		}
		return;
	}

	/* SEL TAPE button (sx+45, 338, 180, 24) */
	if (x >= sx + 45 && x < sx + 45 + 180 && y >= 338 && y < 338 + 24) {
		trigger_sel_tape();
		return;
	}

	/* Cassette Selection Prev (sx, 338, 35, 24) */
	if (x >= sx && x < sx + 35 && y >= 338 && y < 338 + 24) {
		if (num_cassettes > 0) {
			if (selected_cassette_idx < 0) {
				selected_cassette_idx = num_cassettes - 1;
			} else {
				selected_cassette_idx = (selected_cassette_idx -
							    1 + num_cassettes) %
				    num_cassettes;
			}
			const char *name =
			    cassette_files[selected_cassette_idx];
			const char *slash = strrchr(name, '/');
			if (slash)
				name = slash + 1;
			snprintf(status_text,
			    sizeof(status_text),
			    "SELECTED TAPE: %s",
			    name);
		} else {
			strncpy(status_text,
			    "NO TAPES AVAILABLE",
			    sizeof(status_text) - 1);
		}
		return;
	}

	/* Cassette Selection Next (sx + 235, 338, 35, 24) */
	if (x >= sx + 235 && x < sx + 235 + 35 && y >= 338 && y < 338 + 24) {
		if (num_cassettes > 0) {
			if (selected_cassette_idx < 0) {
				selected_cassette_idx = 0;
			} else {
				selected_cassette_idx =
				    (selected_cassette_idx + 1) % num_cassettes;
			}
			const char *name =
			    cassette_files[selected_cassette_idx];
			const char *slash = strrchr(name, '/');
			if (slash)
				name = slash + 1;
			snprintf(status_text,
			    sizeof(status_text),
			    "SELECTED TAPE: %s",
			    name);
		} else {
			strncpy(status_text,
			    "NO TAPES AVAILABLE",
			    sizeof(status_text) - 1);
		}
		return;
	}

	/* Play / Load Tape (sx, 372, 130, 24) */
	if (x >= sx && x < sx + 130 && y >= 372 && y < 372 + 24) {
		struct expansion_card *aci_card = get_or_add_aci();
		if (aci_card) {
			if (selected_cassette_idx >= 0 &&
			    selected_cassette_idx < num_cassettes) {
				const char *path =
				    cassette_files[selected_cassette_idx];
				if (aci_load_tape(aci_card, path)) {
					snprintf(status_text,
					    sizeof(status_text),
					    "TAPE LOADED! C100R");
				} else {
					snprintf(status_text,
					    sizeof(status_text),
					    "TAPE LOAD FAILED");
				}
			} else {
				strncpy(status_text,
				    "NO TAPE SELECTED",
				    sizeof(status_text) - 1);
			}
		}
		return;
	}

	/* Record / Save Tape (sx + 140, 372, 130, 24) */
	if (x >= sx + 140 && x < sx + 140 + 130 && y >= 372 && y < 372 + 24) {
		struct expansion_card *aci_card = get_or_add_aci();
		if (aci_card) {
			if (selected_cassette_idx >= 0 &&
			    selected_cassette_idx < num_cassettes) {
				if (aci_save_tape(aci_card,
					"recorded_tape.aci")) {
					strncpy(status_text,
					    "TAPE SAVED",
					    sizeof(status_text) - 1);
					scan_tapes();
				} else {
					strncpy(status_text,
					    "TAPE SAVE FAILED",
					    sizeof(status_text) - 1);
				}
			} else {
				strncpy(status_text,
				    "NO TAPE SELECTED",
				    sizeof(status_text) - 1);
			}
		}
		return;
	}

	/* Baud Speed Button (sx, 406, 270, 28) */
	if (x >= sx && x < sx + 270 && y >= 406 && y < 406 + 28) {
		struct field *f_baud = NULL;
		for (int i = 0; i < NF - 1; i++) {
			if (fields[i].flag == 'B') {
				f_baud = &fields[i];
				break;
			}
		}
		if (f_baud) {
			f_baud->cidx = (f_baud->cidx + 1) % f_baud->nchoices;
			snprintf(f_baud->sval,
			    sizeof(f_baud->sval),
			    "%s",
			    f_baud->choices[f_baud->cidx]);
			if (strcmp(f_baud->sval, "Fast") == 0) {
				snprintf(status_text,
				    sizeof(status_text),
				    "BAUD SET TO: FAST");
			} else {
				snprintf(status_text,
				    sizeof(status_text),
				    "BAUD SET TO: %s",
				    f_baud->sval);
			}
		}
		return;
	}
}

/* ── KEYBOARD KEYSTROKE HANDLING ─────────────────────────────────────────── */

static void
handle_key_event(const SDL_KeyboardEvent *key)
{
	SDL_Keycode sym = key->key;
	SDL_Keymod mod = key->mod;

	if (mod & SDL_KMOD_CTRL) {
		if (sym == SDLK_R) {
			reset_pending = true;
			strncpy(status_text,
			    "struct cpu RESET.",
			    sizeof(status_text) - 1);
			return;
		}
		if (sym == SDLK_L) {
			for (int y = 0; y < 24; y++) {
				memset(vram[y], 0x20, 40);
			}
			cx = 0;
			cy = 0;
			vram[cy][cx] = 0x00;
			screen_dirty = true;
			strncpy(status_text,
			    "VRAM CLEARED.",
			    sizeof(status_text) - 1);
			return;
		}
		if (sym >= SDLK_A && sym <= SDLK_Z) {
			buffered_key_sdl = (uint8_t)((sym - SDLK_A + 1) | 0x80);
			return;
		}
		return;
	}

	bool is_stopped = emulation_paused ||
	    (g_debug_enabled && g_dbg && g_dbg->step_mode);
	if (is_stopped) {
		if (sym == SDLK_S) {
			term_request_step();
			return;
		}
		if (sym == SDLK_C) {
			if (g_dbg)
				g_dbg->step_mode = false;
			emulation_paused = false;
			term_close_debugger();
			strncpy(status_text,
			    "EMULATION RESUMED.",
			    sizeof(status_text) - 1);
			return;
		}
		return;
	}

	int ch = 0;
	if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
		ch = 0x0D;
	} else if (sym == SDLK_BACKSPACE || sym == SDLK_DELETE) {
		ch = 0x5F;
	}

	if (ch != 0) {
		buffered_key_sdl = (uint8_t)(ch | 0x80);
	}
}

/* ── EXPORTED TERM INTERFACE ─────────────────────────────────────────────── */

void
term_init(void)
{
	/* Load config path and configuration values */
	term_config_init();

	/* Initialize SDL3 */
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		fprintf(stderr,
		    "Failed to initialize SDL3: %s\n",
		    SDL_GetError());
		exit(1);
	}

	window =
	    SDL_CreateWindow("Apple-1 System Console", SCREEN_W, SCREEN_H, 0);
	if (!window) {
		fprintf(stderr,
		    "Failed to create SDL3 window: %s\n",
		    SDL_GetError());
		SDL_Quit();
		exit(1);
	}

	renderer = SDL_CreateRenderer(window, NULL);
	if (!renderer) {
		fprintf(stderr,
		    "Failed to create SDL3 renderer: %s\n",
		    SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		exit(1);
	}

	/* Load custom charmap */
	charmap_loaded = load_charmap();
	if (!charmap_loaded) {
		fprintf(stderr,
		    "Warning: 2513_Apple-1.bin could not be loaded.\n");
	}

	SDL_StartTextInput(window);

	/* Scan available cassettes */
	scan_tapes();

	/* Pre-populate cassette files if default tape config exists */
	for (int i = 0; i < NF - 1; i++) {
		if (fields[i].flag == 'e') {
			if (fields[i].sval[0] != '\0') {
				if (num_cassettes < MAX_CASSETTES)
					num_cassettes++;
				for (int j = num_cassettes - 1; j > 0; j--) {
					snprintf(cassette_files[j],
					    sizeof(cassette_files[j]),
					    "%s",
					    cassette_files[j - 1]);
				}
				snprintf(cassette_files[0],
				    sizeof(cassette_files[0]),
				    "%s",
				    fields[i].sval);
				selected_cassette_idx = 0;
			}
			break;
		}
	}

	/* Pre-register ACI card if default tape exists */
	for (int i = 0; i < NF - 1; i++) {
		if (fields[i].flag == 'e') {
			if (fields[i].sval[0] != '\0') {
				get_or_add_aci();
			}
			break;
		}
	}

	/* Initial screen state: sweep lines */
	if (!vram_initialized) {
		for (int y = 0; y < 24; y++) {
			for (int x = 0; x < 40; x++) {
				vram[y][x] = (x % 2) ? 0x00 : 0xFF;
			}
		}
		cx = 0;
		cy = 0;
		boot_sweep = true;
		vram_initialized = true;
	}

	last_redraw_ms = SDL_GetTicks();
	render_gui();
}

void
term_shutdown(void)
{
	term_debug_shutdown();

	if (renderer) {
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}
	if (window) {
		SDL_DestroyWindow(window);
		window = NULL;
	}
	SDL_Quit();
}

static bool
is_real_backspace_enabled(void)
{
	for (int i = 0; i < NF - 1; i++) {
		if (fields[i].flag == 'x') {
			return (fields[i].bval);
		}
	}
	return (false);
}

void
term_write(uint8_t val)
{
	if (boot_sweep) {
		boot_sweep = false;
		cx = 0;
		cy = 0;
		screen_dirty = true;
	}

	val &= 0x7F;

	if (val == 0x0D || val == 0x0A) {
		if (val == 0x0D) {
			vram[cy][cx] = 0x20;
			cx = 0;
			cy++;
			if (cy >= 24) {
				scroll_up();
				cy = 23;
			}
			vram[cy][cx] = 0x00;
			screen_dirty = true;
		}
	} else if (val == 0x08 || val == 0x7F || val == 0x5F) {
		/* Backspace */
		if (cx > 0) {
			vram[cy][cx] = 0x20;
			cx--;
			if (is_real_backspace_enabled()) {
				vram[cy][cx] = 0x00;
			} else {
				vram[cy][cx] = 0x00;
			}
			screen_dirty = true;
		}
	} else if (val >= 0x20 && val <= 0x7E) {
		uint8_t glyphCode = val;
		if (glyphCode >= 'a' && glyphCode <= 'z') {
			glyphCode -= 32;
		}
		vram[cy][cx] = glyphCode;
		cx++;
		if (cx >= 40) {
			cx = 0;
			cy++;
			if (cy >= 24) {
				scroll_up();
				cy = 23;
			}
		}
		vram[cy][cx] = 0x00;
		screen_dirty = true;
	}

	/* Terminal baud rate throttle: 8N1 = 10 bits/char → delay = 10000/baud ms */
	{
		struct field *f_baud = NULL;
		for (int i = 0; i < NF - 1; i++) {
			if (fields[i].flag == 'B') {
				f_baud = &fields[i];
				break;
			}
		}
		if (f_baud && f_baud->sval[0] &&
		    strcmp(f_baud->sval, "Fast") != 0) {
			int baud = atoi(f_baud->sval);
			if (baud > 0) {
				int delay_ms = 10000 / baud;
				if (delay_ms > 0) {
					/* Render first so each char is visible as it appears */
					render_gui();
					last_redraw_ms = SDL_GetTicks();
					screen_dirty = false;
					SDL_Delay(delay_ms);
					return;
				}
			}
		}
	}
}

void
term_update(void)
{
	static char last_status_text[128] = "";
	static uint64_t status_clear_ticks = 0;
	uint64_t now = SDL_GetTicks();

	if (strcmp(status_text, last_status_text) != 0) {
		strcpy(last_status_text, status_text);
		if (status_text[0] != '\0') {
			status_clear_ticks = now + 3000;
		} else {
			status_clear_ticks = 0;
		}
	}

	if (status_clear_ticks != 0 && now >= status_clear_ticks) {
		status_text[0] = '\0';
		last_status_text[0] = '\0';
		status_clear_ticks = 0;
	}

	if (now - last_redraw_ms >= 16) {
		render_gui();
		if (term_debug_is_open()) {
			term_debug_render();
		}
		if (term_trace_is_open()) {
			term_trace_render();
		}
		last_redraw_ms = now;
		screen_dirty = false;
	}
}

static SDL_Window *
get_event_window(const SDL_Event *ev)
{
	switch (ev->type) {
	case SDL_EVENT_WINDOW_SHOWN:
	case SDL_EVENT_WINDOW_HIDDEN:
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_MOVED:
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
	case SDL_EVENT_WINDOW_MINIMIZED:
	case SDL_EVENT_WINDOW_MAXIMIZED:
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
	case SDL_EVENT_WINDOW_HIT_TEST:
	case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		return (SDL_GetWindowFromID(ev->window.windowID));
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return (SDL_GetWindowFromID(ev->key.windowID));
	case SDL_EVENT_TEXT_INPUT:
		return (SDL_GetWindowFromID(ev->text.windowID));
	case SDL_EVENT_MOUSE_MOTION:
		return (SDL_GetWindowFromID(ev->motion.windowID));
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return (SDL_GetWindowFromID(ev->button.windowID));
	case SDL_EVENT_MOUSE_WHEEL:
		return (SDL_GetWindowFromID(ev->wheel.windowID));
	default:
		return (NULL);
	}
}

uint8_t
term_poll(void)
{
	static uint64_t last_poll_ms = 0;
	uint64_t now = SDL_GetTicks();

	if (now - last_poll_ms >= 5 || now < last_poll_ms) {
		last_poll_ms = now;
		term_update();

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			SDL_Window *win = get_event_window(&event);
			if (term_debug_is_window(win)) {
				term_debug_handle_event(&event);
			} else if (term_trace_is_window(win)) {
				term_trace_handle_event(&event);
			} else {
				if (event.type == SDL_EVENT_QUIT) {
					exit(0);
				} else if (event.type ==
				    SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
					exit(0);
				} else if (event.type == SDL_EVENT_KEY_DOWN) {
					if (config_modal_open) {
						term_config_modal_handle_key(
						    &event.key);
					} else {
						handle_key_event(&event.key);
					}
				} else if (event.type == SDL_EVENT_TEXT_INPUT) {
					if (config_modal_open) {
						term_config_modal_handle_text_input(
						    event.text.text);
					} else {
						const char *t = event.text.text;
						while (*t) {
							char c = *t++;
							if (c >= 'a' &&
							    c <= 'z')
								c -= 32;
							buffered_key_sdl =
							    (uint8_t)(c | 0x80);
						}
					}
				} else if (event.type ==
				    SDL_EVENT_MOUSE_WHEEL) {
					if (config_modal_open) {
						term_config_scroll(
						    -(int)event.wheel.y);
					}
				} else if (event.type ==
				    SDL_EVENT_MOUSE_BUTTON_DOWN) {
					handle_mouse_event(&event.button);
				}
			}
		}
	}

	bool can_paste = false;
	if (g_bus) {
		if (!(g_bus->pia.kbd_control & 0x80) &&
		    g_bus->kbd_bounce_cycles == 0 && !io_has_buffered_key()) {
			can_paste = true;
		}
	} else {
		if (!io_has_buffered_key()) {
			can_paste = true;
		}
	}

	if (paste_len > 0 && buffered_key_sdl == 0 && can_paste) {
		char c = paste_buf[paste_pos++];
		paste_len--;
		buffered_key_sdl = (uint8_t)(c | 0x80);
		if (paste_len == 0) {
			strncpy(status_text,
			    "PASTE COMPLETE.",
			    sizeof(status_text) - 1);
		}
	}

	uint8_t val = buffered_key_sdl;
	buffered_key_sdl = 0;
	return (val);
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
	bool pending = reset_pending;
	reset_pending = false;
	return (pending);
}

bool
term_is_powered(void)
{
	return (machine_powered);
}

bool
term_is_paused(void)
{
	return (emulation_paused);
}
