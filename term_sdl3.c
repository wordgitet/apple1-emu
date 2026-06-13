#define _POSIX_C_SOURCE 200809L
#include "term_apple1.h"
#include "bus.h"
#include "cpu.h"
#include "aci.h"
#include "embedded_roms.h"
#include "krusader.h"
#include "dbg.h"
#include "disasm.h"
#include "font5x7.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern Bus *g_bus;
extern CPU *g_cpu;
extern bool g_debug_enabled;

/* ── CONSTANTS & PALETTES ────────────────────────────────────────────────── */

/* Apple-1 terminal character cell: 5px wide glyph + 2px gap = 7px cell,
 * 7px tall glyph + 1px gap = 8px cell. 40 cols x 24 rows.
 * Rendered at 4x integer scale:
 * Cell: 28 x 32 pixels.
 * CRT area size: 1120 x 768 pixels.
 * Sidebar panel: 300 pixels wide.
 * Menu bar: 28 pixels high at top.
 * Total window size: 1420 x 796. */
#define CELL_W       28
#define CELL_H       32
#define GLYPH_COLS   5
#define GLYPH_ROWS   7
#define TERM_COLS    40
#define TERM_ROWS    24
#define CRT_DISP_W   (TERM_COLS * CELL_W)   /* 1120 */
#define CRT_DISP_H   (TERM_ROWS * CELL_H)   /* 768 */
#define SIDEBAR_W    300
#define MENU_BAR_H   28
#define SCREEN_W     (CRT_DISP_W + SIDEBAR_W) /* 1420 */
#define SCREEN_H     (CRT_DISP_H + MENU_BAR_H) /* 796 */

typedef enum {
	MONITOR_GREEN,
	MONITOR_AMBER,
	MONITOR_MONO
} MonitorTint;

typedef struct {
	SDL_Color bg;
	SDL_Color pixel;
	SDL_Color glow;
} Palette;

static const Palette PALETTES[] = {
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

/* ── FONT5X7 MONO RENDERER (for debug/trace windows) ────────────────────── */
/* Font5x7 is column-major: each byte is one vertical column (5 cols/char),
 * bit 0 = top row, bit 6 = bottom row, 7 rows per char.
 * At 2x scale: each char = 10x14 px + 2px gap = 12px wide, 14px tall. */
#define MONO_SCALE   2
#define MONO_CW      6   /* (5 cols * scale) + 1px gap */
#define MONO_CH      (7 * MONO_SCALE)   /* 14px */

static void
draw_mono_char(SDL_Renderer *rend, char c, int x, int y, SDL_Color col)
{
	if (c < 0x20 || c > 0x7E) c = '?';
	const unsigned char *glyph = &Font5x7[(c - 0x20) * 5];
	SDL_SetRenderDrawColor(rend, col.r, col.g, col.b, col.a);
	for (int col_i = 0; col_i < 5; col_i++) {
		uint8_t colbits = glyph[col_i];
		for (int row = 0; row < 7; row++) {
			if (colbits & (1 << row)) {
				SDL_FRect px = {
					(float)(x + col_i * MONO_SCALE),
					(float)(y + row * MONO_SCALE),
					(float)MONO_SCALE, (float)MONO_SCALE
				};
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

/* ── TRACE RING BUFFER ───────────────────────────────────────────────────── */
#define TRACE_DEFAULT_MAX  5000
#define TRACE_LINE_LEN     160

typedef struct {
	char line[TRACE_LINE_LEN];
} TraceLine;

static TraceLine  *trace_buf  = NULL;  /* dynamically allocated ring  */
static int         trace_max  = TRACE_DEFAULT_MAX;
static int         trace_head = 0;     /* next write index            */
static int         trace_count = 0;    /* how many lines stored       */
static bool        trace_window_open = false;
static SDL_Window    *trace_win  = NULL;
static SDL_Renderer  *trace_ren  = NULL;
static int         trace_scroll = 0;   /* rows scrolled from bottom   */
static bool        trace_frozen = false;

/* ── DEBUG WINDOW ────────────────────────────────────────────────────────── */
static bool        debug_window_open = false;
static SDL_Window    *debug_win  = NULL;
static SDL_Renderer  *debug_ren  = NULL;
/* Text output lines in the debug window */
#define DBG_WIN_LINES  40
#define DBG_WIN_COLS   100
static char  dbg_output[DBG_WIN_LINES][DBG_WIN_COLS];
static int   dbg_num_lines = 0;
static char  dbg_input_buf[256];
static int   dbg_input_len = 0;
static bool  dbg_needs_step = false;   /* set when user hits 's' */
extern debugger_t *g_dbg;              /* exposed from main.c    */

static uint8_t charmap_data[2048];
static bool charmap_loaded = false;
static int charmap_size = 2048;

/* ── SDL3 HANDLES ────────────────────────────────────────────────────────── */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static uint64_t last_redraw_ms = 0;
static uint8_t buffered_key_sdl = 0;

/* ── GUI INTERACTIVE SETTINGS ────────────────────────────────────────────── */
static MonitorTint monitor_tint = MONITOR_GREEN;
static bool scanlines_enabled = true;
static float scanline_opacity = 0.35f;

#define MAX_CASSETTES 32
static char cassette_files[MAX_CASSETTES][512];
static int num_cassettes = 0;
static int selected_cassette_idx = 0;

static void
scan_tapes(void);
static void
set_config_status(const char *m, int ms);
static char status_text[128] = "SYSTEM INITIALISED.";

/* ── CONFIG MODAL STATE & FIELDS ─────────────────────────────────────────── */
static bool file_menu_open = false;
static bool config_modal_open = false;
static bool emulation_menu_open = false;
static bool display_menu_open = false;
static bool debug_menu_open = false;
static bool trace_menu_open = false;
static bool help_menu_open = false;

typedef enum { FT_STR, FT_BOOL, FT_CHOICE, FT_FILE } FType;
typedef struct {
	const char *label;
	const char *hint;
	char flag;
	FType type;
	bool bval;
	char sval[512];
	const char **choices;
	int nchoices;
	int cidx;
	bool editing;
	int cursor;
} Field;

static const char *RAM_CHOICES[] = { "4", "8", "16", "32", "48", "64" };
static const char *BAUD_CHOICES[] = { "300", "1200", "1500", "2400", "4800", "9600", "Fast" };

static Field fields[] = {
	{ "ROM FILE (-r)",       "Path to 256-byte Wozmon ROM (optional, uses embedded by default)",   'r', FT_FILE,   false, "",           NULL,        0, 0, false, 0 },
	{ "RAM SIZE (-m)",       "RAM in KB: 4,8,16,32,48,64  (default 64)",           'm', FT_CHOICE, false, "64",         RAM_CHOICES, 6, 5, false, 0 },
	{ "ACI ROM (-a)",        "Path to ACI ROM (optional, uses embedded by default)",               'a', FT_FILE,   false, "",           NULL,        0, 0, false, 0 },
	{ "LOAD BINARY (-l)",    "Preload file into RAM:  file@HEXADDR  e.g. basic.rom@E000", 'l', FT_STR,    false, "",           NULL,        0, 0, false, 0 },
	{ "DEFAULT TAPE (-e)",   "Default cassette tape (.aci) file path to load on startup",          'e', FT_FILE,   false, "",           NULL,        0, 0, false, 0 },
	{ "TERMINAL BAUD (-B)",  "Terminal display speed in baud (300=authentic Apple-1, Fast=instant)", 'B', FT_CHOICE, false, "300",        BAUD_CHOICES, 7, 0, false, 0 },
	{ "DRAM REFRESH (-d)",   "Emulate DRAM refresh cycle-stealing (slows ~5%)",    'd', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "KBD BOUNCE (-b)",     "Emulate keyboard contact bounce (cosmetic)",          'b', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "NO PIA THROTTLE (-p)", "Disable 977 ns PIA I/O throttling (slightly faster PIA)", 'p', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "DETERMINISTIC (-s)",  "Disable cold-boot RAM randomisation (affects BASIC)", 's', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "DEBUG MODE (-g)",     "Start with interactive debugger (pauses CPU first)", 'g', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "TRACE MODE (-t)",     "Print CPU trace to stdout (pipe to file to capture)", 't', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "REAL BACKSPACE (-x)", "Enable destructive backspace (cursor moves back and erases)", 'x', FT_BOOL,   false, "",           NULL,        0, 0, false, 0 },
	{ "CONFIG PATH",         "Where to save apple1.conf",                           0,   FT_STR,    false, "",           NULL,        0, 0, false, 0 },
};

#define NF   ((int)(sizeof(fields)/sizeof(fields[0])))
#define ICFG (NF-1)

static int editing_field_idx = -1;
static int config_scroll_offset = 0;
#define VISIBLE_FIELDS 8

static char config_status_msg[256] = "";
static uint64_t config_status_until = 0;

/* Centered modal coordinates */
#define MODAL_W   900
#define MODAL_H   560
#define MODAL_X   ((SCREEN_W - MODAL_W) / 2)   /* (1420 - 900) / 2 = 260 */
#define MODAL_Y   ((SCREEN_H - MODAL_H) / 2)   /* (796 - 560) / 2 = 118 */
#define MODAL_BBY (MODAL_H - 55)

#define SB_X        20
#define SB_Y        96
#define FIELD_H     42
#define FIELD_W    820

/* Colors from config_ui */
static const SDL_Color PANEL = { 18, 18, 24, 255 };
static const SDL_Color BORD  = { 40, 40, 50, 255 };
static const SDL_Color AMBER = { 255, 176, 0, 255 };
static const SDL_Color GREEN = { 51, 255, 51, 255 };
static const SDL_Color DIM   = { 80, 120, 80, 255 };
static const SDL_Color SELBG = { 20, 50, 20, 255 };
static const SDL_Color SELBO = { 51, 255, 51, 255 };
static const SDL_Color WHITE = { 220, 220, 220, 255 };
static const SDL_Color RED   = { 255, 80, 80, 255 };
static const SDL_Color BTNBG = { 18, 18, 26, 255 };
static const SDL_Color BTNHV = { 30, 55, 30, 255 };

/* ── CONFIG LOAD/SAVE HELPERS ────────────────────────────────────────────── */

static void
get_xdg_config_path(char *out_path, size_t max_len)
{
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	if (xdg_config_home && xdg_config_home[0] != '\0') {
		snprintf(out_path, max_len, "%s/apple1/apple1.conf", xdg_config_home);
	} else {
		const char *home = getenv("HOME");
		if (home && home[0] != '\0') {
			snprintf(out_path, max_len, "%s/.config/apple1/apple1.conf", home);
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
		if (fields[i].flag == f) return &fields[i];
	}
	return NULL;
}

static void
load_conf(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) return;
	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		size_t l = strlen(line);
		while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) {
			line[--l] = '\0';
		}
		char *p = line;
		while (*p == ' ') p++;
		if (!*p || *p == '#') continue;
		if (p[0] == '-' && p[1]) {
			char fl = p[1];
			char *v = p + 2;
			while (*v == ' ') v++;
			Field *f = by_flag(fl);
			if (!f) continue;
			if (f->type == FT_BOOL) {
				f->bval = true;
			} else if (f->type == FT_CHOICE) {
				for (int j = 0; j < f->nchoices; j++) {
					if (strcmp(f->choices[j], v) == 0) {
						f->cidx = j;
						snprintf(f->sval, sizeof(f->sval), "%s", v);
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
	if (!fp) return;
	fprintf(fp, "# Apple-1 Emulator Config (SDL3 GUI)\n");
	fprintf(fp, "# Options managed live in the emulator sidebar are NOT here:\n");
	fprintf(fp, "#   CPU speed (-c), ACI tape (-e/-E), Krusader (-k)\n\n");
	for (int i = 0; i < NF - 1; i++) {
		Field *f = &fields[i];
		if (f->type == FT_BOOL) {
			if (f->bval) fprintf(fp, "-%c\n", f->flag);
		} else if (f->type == FT_CHOICE) {
			fprintf(fp, "-%c %s\n", f->flag, f->choices[f->cidx]);
		} else if (f->sval[0]) {
			fprintf(fp, "-%c %s\n", f->flag, f->sval);
		}
	}
	fclose(fp);
}

static void
reboot_emulator(void)
{
	term_shutdown();
	char *args[] = { "apple1", NULL };
	execv("/proc/self/exe", args);
	perror("reboot failed");
	exit(1);
}

static bool
pick_file_dialog(char *out_path, size_t max_len, const char *title, const char *ext)
{
	char cmd[512];
	snprintf(cmd, sizeof(cmd),
		"zenity --file-selection --title='%s' --file-filter='%s | %s' 2>/dev/null "
		"|| kdialog --getopenfilename . '%s' 2>/dev/null",
		title, title, ext, ext);
	FILE *p = popen(cmd, "r");
	if (!p) return false;
	bool ok = false;
	if (fgets(out_path, (int)max_len, p)) {
		out_path[strcspn(out_path, "\n")] = '\0';
		ok = strlen(out_path) > 0;
	}
	pclose(p);
	return ok;
}

/* ── INTERNAL HELPERS ────────────────────────────────────────────────────── */

static void
scan_tapes(void)
{
	num_cassettes = 0;

	// Scan root directory "." for .aci files
	DIR *d = opendir(".");
	if (d) {
		struct dirent *dir;
		while ((dir = readdir(d)) != NULL) {
			if (dir->d_type == DT_REG) {
				const char *ext = strrchr(dir->d_name, '.');
				if (ext && strcmp(ext, ".aci") == 0) {
					if (num_cassettes < MAX_CASSETTES) {
						snprintf(cassette_files[num_cassettes], 512, "%s", dir->d_name);
						num_cassettes++;
					}
				}
			}
		}
		closedir(d);
	}

	// Scan "cassettes" directory for .aci files
	d = opendir("cassettes");
	if (d) {
		struct dirent *dir;
		while ((dir = readdir(d)) != NULL) {
			if (dir->d_type == DT_REG) {
				const char *ext = strrchr(dir->d_name, '.');
				if (ext && strcmp(ext, ".aci") == 0) {
					if (num_cassettes < MAX_CASSETTES) {
						snprintf(cassette_files[num_cassettes], 512, "cassettes/%s", dir->d_name);
						num_cassettes++;
					}
				}
			}
		}
		closedir(d);
	}

	if (num_cassettes > 0) {
		if (selected_cassette_idx >= num_cassettes || selected_cassette_idx < 0) {
			selected_cassette_idx = 0;
		}
	} else {
		selected_cassette_idx = -1;
	}
}

static bool
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
			return true;
		}
	}

	/* Fallback to embedded authentic Apple-1 2513 character ROM */
	memcpy(charmap_data, embedded_2513_charmap, 2048);
	charmap_size = 2048;
	return true;
}

static void
scroll_up(void)
{
	for (int y = 0; y < 23; y++) {
		memcpy(vram[y], vram[y + 1], 40);
	}
	memset(vram[23], 0x20, 40);
}

static expansion_card_t *
get_or_add_aci(void)
{
	if (!g_bus) return NULL;
	for (int i = 0; i < g_bus->num_cards; i++) {
		if (strcmp(g_bus->cards[i]->name, "ACI") == 0) {
			return g_bus->cards[i];
		}
	}

	const char *aci_path = NULL;
	Field *f_aci = by_flag('a');
	if (f_aci && f_aci->sval[0] != '\0') {
		aci_path = f_aci->sval;
	}

	expansion_card_t *aci_card = aci_create(aci_path);

	if (aci_card) {
		bus_add_card(g_bus, aci_card);
		strncpy(status_text, "ACI CARD LOADED", sizeof(status_text) - 1);
	} else if (!aci_path) {
		strncpy(status_text, "NO ACI ROM PATH SET", sizeof(status_text) - 1);
	} else {
		snprintf(status_text, sizeof(status_text), "ACI ROM LOAD FAILED");
	}
	return aci_card;
}


/* ── CHARMAP RENDERERS ───────────────────────────────────────────────────── */

/* Map an ASCII code to the 2513 ROM index.
 * The 2513 stores 64 chars. Apple-1 video uses bits[5:0] of ASCII:
 *   '@'(0x40) -> ROM[0], 'A'(0x41) -> ROM[1], ... '_'(0x5F) -> ROM[31]
 *   ' '(0x20) -> ROM[32], '!'(0x21) -> ROM[33], ... '?'(0x3F) -> ROM[63] */
static inline int
ascii_to_rom_idx(uint8_t ascii)
{
	return (int)((ascii - 0x40) & 0x3F);
}

/* Draw a character at 4x scale with soft phosphor glow. */
static void
draw_char_4x_glow(SDL_Renderer *rend, uint8_t glyphIndex, int cell_x, int cell_y, const Palette *pal)
{
	if (!charmap_loaded) return;
	int rom_idx = ascii_to_rom_idx(glyphIndex);
	const uint8_t *glyph = &charmap_data[rom_idx * 8];

	for (int r = 0; r < GLYPH_ROWS; ++r) {
		uint8_t bits = glyph[r + 1]; /* row 0 is always blank; glyphs start at row 1 */
		for (int c = 0; c < GLYPH_COLS; ++c) {
			bool active = (bits & (1 << (4 - c))) != 0;
			if (active) {
				int px = cell_x + c * 4;
				int py = cell_y + r * 4;
				/* Glow: 6×6 box centred on the 4x4 pixel, semi-transparent */
				SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
				SDL_FRect glow = { (float)(px - 1), (float)(py - 1), 6.0f, 6.0f };
				SDL_SetRenderDrawColor(rend,
					pal->glow.r, pal->glow.g, pal->glow.b, pal->glow.a);
				SDL_RenderFillRect(rend, &glow);
				SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
				/* Solid dot */
				SDL_FRect dot = { (float)px, (float)py, 4.0f, 4.0f };
				SDL_SetRenderDrawColor(rend,
					pal->pixel.r, pal->pixel.g, pal->pixel.b, pal->pixel.a);
				SDL_RenderFillRect(rend, &dot);
			}
		}
	}
}

/* Sidebar and config text rendering with custom integer scaling. */
static void
draw_text_scaled(SDL_Renderer *rend, const char *str, int x, int y, int scale, SDL_Color color)
{
	if (!charmap_loaded) return;
	int cur_x = x;
	while (*str) {
		uint8_t ch = (uint8_t)*str;
		if (ch >= 'a' && ch <= 'z') ch -= 32; /* force uppercase */
		int rom_idx = ascii_to_rom_idx(ch);
		const uint8_t *glyph = &charmap_data[rom_idx * 8];
		SDL_SetRenderDrawColor(rend, color.r, color.g, color.b, color.a);
		for (int r = 0; r < GLYPH_ROWS; ++r) {
			uint8_t bits = glyph[r + 1]; /* row 0 is blank */
			for (int c = 0; c < GLYPH_COLS; ++c) {
				if (bits & (1 << (4 - c))) {
					SDL_FRect dot = {
						(float)(cur_x + c * scale),
						(float)(y + r * scale),
						(float)scale, (float)scale
					};
					SDL_RenderFillRect(rend, &dot);
				}
			}
		}
		cur_x += (GLYPH_COLS + 1) * scale;
		str++;
	}
}

static void
draw_text_2x(SDL_Renderer *rend, const char *str, int x, int y, SDL_Color color)
{
	draw_text_scaled(rend, str, x, y, 2, color);
}

/* ── BUTTON GUI BUILDER (sidebar, not scaled) ────────────────────────────── */

static bool
draw_button(SDL_Renderer *rend, int x, int y, int w, int h, const char *text, SDL_Color tint, int mouse_x, int mouse_y)
{
	bool hovered = (mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h);

	/* Draw background */
	if (hovered) {
		SDL_SetRenderDrawColor(rend, 45, 45, 50, 255);
	} else {
		SDL_SetRenderDrawColor(rend, 25, 25, 30, 255);
	}
	SDL_FRect btn_rect = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderFillRect(rend, &btn_rect);

	/* Border */
	SDL_SetRenderDrawColor(rend, tint.r / 2, tint.g / 2, tint.b / 2, 255);
	SDL_RenderRect(rend, &btn_rect);

	/* Centered 2x text */
	int char_w = (GLYPH_COLS + 1) * 2;
	int char_h = GLYPH_ROWS * 2;
	int text_len = (int)strlen(text);
	int tx = x + (w - text_len * char_w) / 2;
	int ty = y + (h - char_h) / 2;
	draw_text_2x(rend, text, tx, ty, tint);

	return hovered;
}

/* ── CONFIG MODAL BUTTONS & FIELDS DRAWING ───────────────────────────────── */

static bool
draw_config_button(int x, int y, int w, int h, const char *lbl, SDL_Color tint, int mx, int my)
{
	bool hov = (mx >= x && mx < x + w && my >= y && my < y + h);
	SDL_FRect btn_rect = { (float)x, (float)y, (float)w, (float)h };

	SDL_SetRenderDrawColor(renderer, hov ? BTNHV.r : BTNBG.r, hov ? BTNHV.g : BTNBG.g, hov ? BTNHV.b : BTNBG.b, 255);
	SDL_RenderFillRect(renderer, &btn_rect);
	SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b, 255);
	SDL_RenderRect(renderer, &btn_rect);

	int cw = (GLYPH_COLS + 1) * 2;
	int ch2 = GLYPH_ROWS * 2;
	draw_text_2x(renderer, lbl, x + (w - (int)strlen(lbl) * cw) / 2, y + (h - ch2) / 2, tint);
	return hov;
}

static void
render_config_field(int i, int idx, int mx, int my)
{
	Field *f = &fields[i];
	int y = MODAL_Y + SB_Y + idx * FIELD_H;
	int x = MODAL_X + SB_X;
	bool sel = (mx >= x && mx < x + FIELD_W && my >= y && my < y + FIELD_H - 2);

	SDL_FRect field_rect = { (float)x, (float)y, (float)FIELD_W, (float)(FIELD_H - 2) };
	SDL_SetRenderDrawColor(renderer, sel ? SELBG.r : PANEL.r, sel ? SELBG.g : PANEL.g, sel ? SELBG.b : PANEL.b, 255);
	SDL_RenderFillRect(renderer, &field_rect);
	SDL_SetRenderDrawColor(renderer, sel ? SELBO.r : BORD.r, sel ? SELBO.g : BORD.g, sel ? SELBO.b : BORD.b, 255);
	SDL_RenderRect(renderer, &field_rect);

	draw_text_2x(renderer, f->label, x + 8, y + 14, sel ? GREEN : DIM);

	int vx = x + 340;
	char vb[80];
	switch (f->type) {
	case FT_BOOL:
		draw_config_button(vx, y + 8, 90, 26, f->bval ? "YES" : "NO", f->bval ? GREEN : DIM, mx, my);
		break;
	case FT_CHOICE:
		draw_config_button(vx, y + 8, 30, 26, "<", GREEN, mx, my);
		if (f->flag == 'm') {
			snprintf(vb, sizeof(vb), "%s KB", f->choices[f->cidx]);
		} else if (f->flag == 'B') {
			if (strcmp(f->choices[f->cidx], "Fast") == 0) {
				snprintf(vb, sizeof(vb), "%s", f->choices[f->cidx]);
			} else {
				snprintf(vb, sizeof(vb), "%s baud", f->choices[f->cidx]);
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
		draw_text_2x(renderer, tr, vx, y + 14, f->editing ? AMBER : WHITE);
		if (f->type == FT_FILE) {
			draw_config_button(vx + 380, y + 8, 90, 26, "BROWSE", AMBER, mx, my);
		}
		/* Blinking cursor */
		if (f->editing) {
			int cx2 = vx + f->cursor * (GLYPH_COLS + 1) * 2;
			if ((SDL_GetTicks() / 400) % 2 == 0) {
				SDL_FRect cursor_rect = { (float)cx2, (float)(y + 13), 2.0f, (float)(GLYPH_ROWS * 2 + 2) };
				SDL_SetRenderDrawColor(renderer, AMBER.r, AMBER.g, AMBER.b, AMBER.a);
				SDL_RenderFillRect(renderer, &cursor_rect);
			}
		}
		break;
	}
	}
}

static void
draw_config_modal(void)
{
	/* Semi-transparent dimming background overlay over the entire screen */
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
	SDL_FRect overlay = { 0, 0, (float)SCREEN_W, (float)SCREEN_H };
	SDL_RenderFillRect(renderer, &overlay);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

	/* Centered modal panel */
	SDL_FRect modal_rect = { (float)MODAL_X, (float)MODAL_Y, (float)MODAL_W, (float)MODAL_H };
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
	draw_text_scaled(renderer, "APPLE-1 EMULATOR CONFIG", MODAL_X + 20, MODAL_Y + 20, 3, AMBER);
	SDL_SetRenderDrawColor(renderer, BORD.r, BORD.g, BORD.b, BORD.a);
	SDL_RenderLine(renderer, MODAL_X + 20, MODAL_Y + 58, MODAL_X + MODAL_W - 20, MODAL_Y + 58);

	/* Draw column headers */
	draw_text_2x(renderer, "OPTION", MODAL_X + SB_X + 8, MODAL_Y + SB_Y - 20, DIM);
	draw_text_2x(renderer, "VALUE", MODAL_X + SB_X + 340, MODAL_Y + SB_Y - 20, DIM);

	/* Draw fields */
	for (int idx = 0; idx < VISIBLE_FIELDS; idx++) {
		int i = config_scroll_offset + idx;
		if (i >= NF) break;
		render_config_field(i, idx, mx, my);
	}

	/* Draw Scrollbar if needed */
	if (NF > VISIBLE_FIELDS) {
		int track_x = MODAL_X + 855;
		int track_y = MODAL_Y + SB_Y;
		int track_w = 15;
		int track_h = VISIBLE_FIELDS * FIELD_H - 2;

		SDL_FRect track_rect = { (float)track_x, (float)track_y, (float)track_w, (float)track_h };
		SDL_SetRenderDrawColor(renderer, 10, 10, 14, 255);
		SDL_RenderFillRect(renderer, &track_rect);
		SDL_SetRenderDrawColor(renderer, BORD.r, BORD.g, BORD.b, BORD.a);
		SDL_RenderRect(renderer, &track_rect);

		/* Thumb height and position */
		float ratio = (float)VISIBLE_FIELDS / (float)NF;
		int thumb_h = (int)(ratio * track_h);
		if (thumb_h < 20) thumb_h = 20;

		float scroll_pct = (float)config_scroll_offset / (float)(NF - VISIBLE_FIELDS);
		int thumb_y = track_y + (int)(scroll_pct * (track_h - thumb_h));

		SDL_FRect thumb_rect = { (float)track_x + 2, (float)thumb_y, (float)track_w - 4, (float)thumb_h };
		bool thumb_hover = (mx >= track_x && mx < track_x + track_w && my >= track_y && my < track_y + track_h);
		SDL_SetRenderDrawColor(renderer, thumb_hover ? GREEN.r : DIM.r, thumb_hover ? GREEN.g : DIM.g, thumb_hover ? GREEN.b : DIM.b, 255);
		SDL_RenderFillRect(renderer, &thumb_rect);
	}

	/* Note at the bottom */
	SDL_Color note_color = { 60, 80, 60, 255 };
	draw_text_scaled(renderer, "NOTE: CPU SPEED / TAPE / KRUSADER ARE CONTROLLED LIVE IN THE EMULATOR SIDEBAR",
		MODAL_X + 20, MODAL_Y + MODAL_H - 118, 1, note_color);

	/* Bottom Buttons */
	int bby = MODAL_Y + MODAL_BBY;
	SDL_SetRenderDrawColor(renderer, 30, 30, 36, 255);
	SDL_FRect bar = { (float)MODAL_X, (float)(bby - 10), (float)MODAL_W, 2.0f };
	SDL_RenderFillRect(renderer, &bar);

	draw_config_button(MODAL_X + 20, bby, 160, 34, "SAVE CONFIG", AMBER, mx, my);
	draw_config_button(MODAL_X + 200, bby, 180, 34, "APPLY & REBOOT", GREEN, mx, my);
	draw_config_button(MODAL_X + 400, bby, 100, 34, "CLOSE", RED, mx, my);

	/* Status Message */
	if (SDL_GetTicks() < config_status_until && config_status_msg[0]) {
		draw_text_2x(renderer, config_status_msg, MODAL_X + 510, bby + 8, AMBER);
	}

	/* Hint for hovered field */
	for (int idx = 0; idx < VISIBLE_FIELDS; idx++) {
		int i = config_scroll_offset + idx;
		if (i >= NF) break;
		int fy = MODAL_Y + SB_Y + idx * FIELD_H;
		if (my >= fy && my < fy + FIELD_H - 2 && mx >= MODAL_X + SB_X && mx < MODAL_X + SB_X + FIELD_W) {
			draw_text_2x(renderer, fields[i].hint, MODAL_X + 20, MODAL_Y + MODAL_H - 95, DIM);
			break;
		}
	}
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
	SDL_SetRenderDrawColor(renderer, menu_bg.r, menu_bg.g, menu_bg.b, menu_bg.a);
	SDL_RenderFillRect(renderer, &bar_rect);

	SDL_SetRenderDrawColor(renderer, menu_border.r, menu_border.g, menu_border.b, menu_border.a);
	SDL_RenderLine(renderer, 0, MENU_BAR_H - 1, SCREEN_W, MENU_BAR_H - 1);

	/* Get mouse state */
	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	/* FILE menu item */
	bool file_hover = (mx >= 10 && mx < 70 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "FILE", 15, 6, (file_hover || file_menu_open) ? text_hover : text_color);

	/* CONFIG menu item */
	bool config_hover = (mx >= 80 && mx < 160 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "CONFIG", 85, 6, (config_hover || config_modal_open) ? text_hover : text_color);

	/* EMULATION menu item */
	bool emulation_hover = (mx >= 170 && mx < 280 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "EMULATION", 175, 6, (emulation_hover || emulation_menu_open) ? text_hover : text_color);

	/* DISPLAY menu item */
	bool display_hover = (mx >= 290 && mx < 380 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "DISPLAY", 295, 6, (display_hover || display_menu_open) ? text_hover : text_color);

	/* DEBUG menu item */
	bool debug_hover = (mx >= 390 && mx < 460 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "DEBUG", 395, 6, (debug_hover || debug_menu_open) ? text_hover : text_color);

	/* TRACE menu item */
	bool trace_hover = (mx >= 470 && mx < 540 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "TRACE", 475, 6, (trace_hover || trace_menu_open) ? text_hover : text_color);

	/* HELP menu item */
	bool help_hover = (mx >= 550 && mx < 610 && my >= 0 && my < MENU_BAR_H);
	draw_text_2x(renderer, "HELP", 555, 6, (help_hover || help_menu_open) ? text_hover : text_color);
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
	int num_items = 4;
	int drop_h = item_h * num_items + 6;

	/* Background and border */
	SDL_FRect drop_rect = { (float)drop_x, (float)drop_y, (float)drop_w, (float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	/* Get mouse state */
	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = {
		"LOAD TAPE (.ACI)",
		"LOAD ROM TO RAM...",
		"LOAD WOZMON",
		"QUIT"
	};

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 && my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3), (float)iy, (float)(drop_w - 6), (float)item_h };
			SDL_SetRenderDrawColor(renderer, hover_bg.r, hover_bg.g, hover_bg.b, hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer, items[i], drop_x + 10, iy + 7, hover ? hover_color : text_color);
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

	SDL_FRect drop_rect = { (float)drop_x, (float)drop_y, (float)drop_w, (float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	char item0[64];
	snprintf(item0, sizeof(item0), "POWER: %s", machine_powered ? "CONNECTED (ON)" : "DISCONNECTED (OFF)");
	
	char item1[64];
	snprintf(item1, sizeof(item1), "STATE: %s", emulation_paused ? "PAUSED" : "RUNNING");

	char item3[64];
	bool uncapped = g_bus && g_bus->opts.uncapped;
	snprintf(item3, sizeof(item3), "SPEED: %s", uncapped ? "UNCAPPED (MAX)" : "CAPPED (1.02 MHz)");

	const char *items[] = {
		item0,
		item1,
		"RESET CPU",
		"CLEAR SCREEN (VRAM)",
		item3
	};

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 && my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3), (float)iy, (float)(drop_w - 6), (float)item_h };
			SDL_SetRenderDrawColor(renderer, hover_bg.r, hover_bg.g, hover_bg.b, hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer, items[i], drop_x + 10, iy + 7, hover ? hover_color : text_color);
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

	SDL_FRect drop_rect = { (float)drop_x, (float)drop_y, (float)drop_w, (float)drop_h };
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
	snprintf(item0, sizeof(item0), "PHOSPHOR: GREEN %s", (monitor_tint == MONITOR_GREEN) ? "(ON)" : "");
	snprintf(item1, sizeof(item1), "PHOSPHOR: AMBER %s", (monitor_tint == MONITOR_AMBER) ? "(ON)" : "");
	snprintf(item2, sizeof(item2), "PHOSPHOR: MONO %s", (monitor_tint == MONITOR_MONO) ? "(ON)" : "");
	snprintf(item3, sizeof(item3), "SCANLINES: %s", scanlines_enabled ? "ON" : "OFF");

	const char *items[] = {
		item0,
		item1,
		item2,
		item3
	};

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 && my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3), (float)iy, (float)(drop_w - 6), (float)item_h };
			SDL_SetRenderDrawColor(renderer, hover_bg.r, hover_bg.g, hover_bg.b, hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer, items[i], drop_x + 10, iy + 7, hover ? hover_color : text_color);
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

	SDL_FRect drop_rect = { (float)drop_x, (float)drop_y, (float)drop_w, (float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = {
		debug_window_open ? "CLOSE DEBUG WINDOW" : "OPEN DEBUG WINDOW",
		"STEP INSTRUCTION (s)",
		"CONTINUE RUNNING (c)",
		"CLEAR BREAKPOINTS"
	};

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 && my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3), (float)iy, (float)(drop_w - 6), (float)item_h };
			SDL_SetRenderDrawColor(renderer, hover_bg.r, hover_bg.g, hover_bg.b, hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer, items[i], drop_x + 10, iy + 7, hover ? hover_color : text_color);
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

	SDL_FRect drop_rect = { (float)drop_x, (float)drop_y, (float)drop_w, (float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = {
		trace_window_open ? "CLOSE TRACE WINDOW" : "OPEN TRACE WINDOW",
		"CLEAR TRACE BUFFER",
		"EXPORT TRACE TO FILE"
	};

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 && my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3), (float)iy, (float)(drop_w - 6), (float)item_h };
			SDL_SetRenderDrawColor(renderer, hover_bg.r, hover_bg.g, hover_bg.b, hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer, items[i], drop_x + 10, iy + 7, hover ? hover_color : text_color);
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

	SDL_FRect drop_rect = { (float)drop_x, (float)drop_y, (float)drop_w, (float)drop_h };
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(renderer, &drop_rect);
	SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
	SDL_RenderRect(renderer, &drop_rect);

	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	const char *items[] = {
		"EMULATOR MANUAL (.MD)",
		"APPLE-1 MANUAL (PDF)"
	};

	for (int i = 0; i < num_items; i++) {
		int iy = drop_y + 3 + i * item_h;
		bool hover = (mx >= drop_x + 3 && mx < drop_x + drop_w - 3 && my >= iy && my < iy + item_h);

		if (hover) {
			SDL_FRect h_rect = { (float)(drop_x + 3), (float)iy, (float)(drop_w - 6), (float)item_h };
			SDL_SetRenderDrawColor(renderer, hover_bg.r, hover_bg.g, hover_bg.b, hover_bg.a);
			SDL_RenderFillRect(renderer, &h_rect);
		}

		draw_text_2x(renderer, items[i], drop_x + 10, iy + 7, hover ? hover_color : text_color);
	}
}

/* ── FILE MENU ACTIONS ───────────────────────────────────────────────────── */

static void
trigger_sel_tape(void)
{
	/* Try zenity first (GNOME), then kdialog (KDE) */
	FILE *pipe = popen(
		"zenity --file-selection"
		" --title='Select ACI Cassette Tape'"
		" --file-filter='ACI Cassettes (*.aci) | *.aci'"
		" 2>/dev/null"
		" || kdialog --getopenfilename . '*.aci'"
		" 2>/dev/null",
		"r");
	if (pipe) {
		char path[512] = {0};
		if (fgets(path, sizeof(path), pipe)) {
			path[strcspn(path, "\n")] = 0; /* strip newline */
			if (strlen(path) > 0) {
				/* Insert at front of cassette list and select it */
				if (num_cassettes < MAX_CASSETTES)
					num_cassettes++;
				/* Shift existing entries down */
				for (int i = num_cassettes - 1; i > 0; i--)
					snprintf(cassette_files[i], sizeof(cassette_files[i]), "%s", cassette_files[i-1]);
				snprintf(cassette_files[0], sizeof(cassette_files[0]), "%s", path);
				selected_cassette_idx = 0;
				const char *base = strrchr(path, '/');
				if (base) base++; else base = path;
				snprintf(status_text, sizeof(status_text), "TAPE: %s", base);
			}
		}
		pclose(pipe);
	} else {
		strncpy(status_text, "NO DIALOG TOOL FOUND", sizeof(status_text) - 1);
	}
}

/* Always open a file picker and load the chosen file into the emulator.
 * title    : dialog window title
 * hint     : status bar prefix for success/failure
 * loader   : callback that does the actual loading; returns true on success.
 * ctx      : opaque pointer forwarded to loader */
typedef bool (*rom_loader_fn)(const char *path, void *ctx);

static void
load_rom_pick(const char *title, const char *ext,
              const char *ok_msg, const char *fail_msg,
              rom_loader_fn loader, void *ctx)
{
	char picked[512] = {0};
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
prompt_hex_address(const char *title, const char *msg, const char *default_val, uint16_t *out_val)
{
	char cmd[512];
	snprintf(cmd, sizeof(cmd),
		"zenity --entry --title='%s' --text='%s' --entry-text='%s' 2>/dev/null "
		"|| kdialog --inputbox '%s' '%s' 2>/dev/null",
		title, msg, default_val, msg, default_val);
	FILE *p = popen(cmd, "r");
	if (!p) return false;
	char buf[64] = {0};
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
	return ok;
}

static void
load_any_rom(void)
{
	char picked[512] = {0};
	if (!pick_file_dialog(picked, sizeof(picked), "Select ROM/Binary File", "*.rom *.bin")) {
		return;
	}
	uint16_t addr = 0xE000;
	if (!prompt_hex_address("Load Address", "Enter hexadecimal load address:", "E000", &addr)) {
		return;
	}
	if (g_bus && bus_load_bin(g_bus, picked, addr)) {
		snprintf(status_text, sizeof(status_text), "ROM LOADED AT $%04X", addr);
	} else {
		strncpy(status_text, "ROM LOAD FAILED", sizeof(status_text) - 1);
	}
}

static bool load_wozmon_fn(const char *path, void *ctx)
{
	(void)ctx;
	/* Load into $FF00 (256-byte Woz Monitor slot) and reset */
	if (!g_bus || !bus_load_bin(g_bus, path, 0xFF00)) return false;
	reset_pending = true;
	return true;
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
		load_rom_pick("Select Wozmon ROM", "*.rom *.bin",
		              "WOZMON LOADED — RESETTING CPU",
		              "ERROR: FAILED TO LOAD WOZMON ROM",
		              load_wozmon_fn, NULL);
		break;
	case 3: /* QUIT */
		exit(0);
		break;
	}
}

static void
handle_menu_click(int x, int y)
{
	(void)y;
	/* Close all menus first */
	bool any_open = file_menu_open || config_modal_open || emulation_menu_open ||
	                display_menu_open || debug_menu_open || trace_menu_open || help_menu_open;
	file_menu_open = false; emulation_menu_open = false; display_menu_open = false;
	debug_menu_open = false; trace_menu_open = false; help_menu_open = false;

	if (x >= 10 && x < 70) {
		file_menu_open = !any_open;
		config_modal_open = false;
		return;
	}
	if (x >= 80 && x < 160) {
		config_modal_open = !config_modal_open;
		if (config_modal_open) {
			config_scroll_offset = 0;
			load_conf(fields[ICFG].sval);
		}
		return;
	}
	if (x >= 170 && x < 280) { emulation_menu_open = true; return; }
	if (x >= 290 && x < 380) { display_menu_open   = true; return; }
	if (x >= 390 && x < 460) { debug_menu_open     = true; return; }
	if (x >= 470 && x < 540) { trace_menu_open     = true; return; }
	if (x >= 550 && x < 610) { help_menu_open      = true; return; }
}

void
dbg_log_append(const char *str)
{
	/* Split str on newlines, appending each line into dbg_output[] */
	const char *p = str;
	while (*p) {
		const char *nl = strchr(p, '\n');
		int seg = nl ? (int)(nl - p) : (int)strlen(p);
		if (dbg_num_lines < DBG_WIN_LINES) {
			int col = (int)strlen(dbg_output[dbg_num_lines]);
			int room = DBG_WIN_COLS - 1 - col;
			if (room > 0) {
				int copy = seg < room ? seg : room;
				memcpy(dbg_output[dbg_num_lines] + col, p, copy);
				dbg_output[dbg_num_lines][col + copy] = '\0';
			}
			if (nl) {
				dbg_num_lines++;
				if (dbg_num_lines < DBG_WIN_LINES)
					dbg_output[dbg_num_lines][0] = '\0';
			}
		}
		p += seg + (nl ? 1 : 0);
		if (!nl) break;
	}
}

/* ── DEBUG WINDOW ────────────────────────────────────────────────────────── */

static void
toggle_debug_window(void)
{
	if (debug_window_open) {
		if (debug_ren) { SDL_DestroyRenderer(debug_ren); debug_ren = NULL; }
		if (debug_win) { SDL_DestroyWindow(debug_win);   debug_win = NULL; }
		debug_window_open = false;
		g_debug_enabled = false;
		if (g_dbg) g_dbg->step_mode = false;
	} else {
		debug_win = SDL_CreateWindow("Apple-1 Debugger  [db> ]", 900, 600, 0);
		if (!debug_win) return;
		debug_ren = SDL_CreateRenderer(debug_win, NULL);
		if (!debug_ren) { SDL_DestroyWindow(debug_win); debug_win = NULL; return; }
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
			snprintf(hdr, sizeof(hdr),
			         "PC:%04X A:%02X X:%02X Y:%02X SP:%02X P:%02X  %s",
			         g_dbg->cpu->pc, g_dbg->cpu->a, g_dbg->cpu->x,
			         g_dbg->cpu->y, g_dbg->cpu->s, g_dbg->cpu->p, dis);
			dbg_log_append(hdr);
			dbg_log_append("\n");
		}
	}
}

static void
render_debug_window(void)
{
	if (!debug_ren) return;
	SDL_Color bg      = {  8,  8, 10, 255 };
	SDL_Color fg      = { 51, 255, 51, 255 };
	SDL_Color dim     = { 30, 140, 30, 255 };
	SDL_Color prompt  = { 255, 176, 0, 255 };

	SDL_SetRenderDrawColor(debug_ren, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(debug_ren);

	int x0 = 8, y0 = 8;
	int line_h = MONO_CH + 4;
	int max_visible = (600 - 60) / line_h;
	int start = dbg_num_lines > max_visible ? dbg_num_lines - max_visible : 0;
	for (int i = start; i < dbg_num_lines; i++) {
		draw_mono_str(debug_ren, dbg_output[i], x0, y0 + (i - start) * line_h, dim);
	}

	/* Prompt line at bottom */
	int py = 600 - 48;
	SDL_SetRenderDrawColor(debug_ren, 18, 18, 22, 255);
	SDL_FRect pbar = {0, (float)py - 4, 900, (float)(MONO_CH + 12)};
	SDL_RenderFillRect(debug_ren, &pbar);

	draw_mono_str(debug_ren, "db> ", x0, py, prompt);
	draw_mono_str(debug_ren, dbg_input_buf, x0 + 4 * MONO_CW * MONO_SCALE, py, fg);

	SDL_RenderPresent(debug_ren);
}

static void
handle_debug_window_event(const SDL_Event *ev)
{
	if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		toggle_debug_window();
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
				strncpy(cmd_copy, dbg_input_buf, sizeof(cmd_copy) - 1);
				cmd_copy[sizeof(cmd_copy) - 1] = '\0';
				dbg_run_command(g_dbg, cmd_copy);
			}
			dbg_input_buf[0] = '\0';
			dbg_input_len = 0;
		} else if (k == SDLK_ESCAPE) {
			toggle_debug_window();
		}
	}
}

/* ── TRACE WINDOW ────────────────────────────────────────────────────────── */

static void
toggle_trace_window(void)
{
	if (trace_window_open) {
		if (trace_ren) { SDL_DestroyRenderer(trace_ren); trace_ren = NULL; }
		if (trace_win) { SDL_DestroyWindow(trace_win);   trace_win = NULL; }
		trace_window_open = false;
		if (trace_buf) { free(trace_buf); trace_buf = NULL; }
		trace_head = 0; trace_count = 0;
		trace_frozen = false;
	} else {
		trace_win = SDL_CreateWindow("Apple-1 CPU Trace", 1100, 700, 0);
		if (!trace_win) return;
		trace_ren = SDL_CreateRenderer(trace_win, NULL);
		if (!trace_ren) { SDL_DestroyWindow(trace_win); trace_win = NULL; return; }
		trace_buf = calloc(trace_max, sizeof(TraceLine));
		trace_head = 0; trace_count = 0; trace_scroll = 0;
		trace_frozen = false;
		trace_window_open = true;
	}
}

static void
render_trace_window(void)
{
	if (!trace_ren) return;
	SDL_Color bg  = {  5,  5,  8, 255 };
	SDL_Color fg  = { 51, 255, 51, 255 };
	SDL_Color hdr = trace_frozen ? (SDL_Color){ 255, 64, 64, 255 } : (SDL_Color){ 255, 176, 0, 255 };

	SDL_SetRenderDrawColor(trace_ren, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderClear(trace_ren);

	/* Header */
	char hbuf[128];
	if (trace_frozen) {
		snprintf(hbuf, sizeof(hbuf), "TRACE: %d lines  (FROZEN - Press SPACE to resume, scroll with wheel)", trace_count);
	} else {
		snprintf(hbuf, sizeof(hbuf), "TRACE: %d lines  (ACTIVE - Press SPACE to freeze, scroll with wheel)", trace_count);
	}
	draw_mono_str(trace_ren, hbuf, 8, 8, hdr);
	SDL_SetRenderDrawColor(trace_ren, 30, 60, 30, 255);
	SDL_RenderLine(trace_ren, 0, 30, 1100, 30);

	int line_h = MONO_CH + 3;
	int max_vis = (700 - 40) / line_h;
	int total = trace_count;
	int start_idx = total - max_vis - trace_scroll;
	if (start_idx < 0) start_idx = 0;
	int end_idx = start_idx + max_vis;
	if (end_idx > total) end_idx = total;

	for (int i = start_idx; i < end_idx; i++) {
		/* Circular-buffer read index */
		int real = (trace_head - total + i + trace_max) % trace_max;
		if (real < 0) real += trace_max;
		int vy = 38 + (i - start_idx) * line_h;
		draw_mono_str(trace_ren, trace_buf[real].line, 8, vy, fg);
	}

	SDL_RenderPresent(trace_ren);
}

static void
handle_trace_window_event(const SDL_Event *ev)
{
	if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		toggle_trace_window();
	} else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
		trace_scroll += (int)ev->wheel.y * -3;
		if (trace_scroll < 0) trace_scroll = 0;
		if (trace_scroll > trace_count) trace_scroll = trace_count;
	} else if (ev->type == SDL_EVENT_KEY_DOWN) {
		if (ev->key.key == SDLK_ESCAPE) toggle_trace_window();
		else if (ev->key.key == SDLK_SPACE) { trace_frozen = !trace_frozen; }
		else if (ev->key.key == SDLK_DOWN)  { trace_scroll -= 3; if (trace_scroll < 0) trace_scroll = 0; }
		else if (ev->key.key == SDLK_UP)    { trace_scroll += 3; if (trace_scroll > trace_count) trace_scroll = trace_count; }
	}
}

static void
export_trace_file(void)
{
	FILE *f = fopen("apple1_trace.txt", "w");
	if (!f) {
		strncpy(status_text, "TRACE WRITE FAILED", sizeof(status_text) - 1);
		return;
	}
	int total = trace_count;
	for (int i = 0; i < total; i++) {
		int real = (trace_head - total + i + trace_max) % trace_max;
		if (real < 0) real += trace_max;
		fprintf(f, "%s\n", trace_buf[real].line);
	}
	fclose(f);
	strncpy(status_text, "TRACE EXPORTED", sizeof(status_text) - 1);
}



/* ── GUI MAIN RENDERING ──────────────────────────────────────────────────── */

static void
render_gui(void)
{
	if (!renderer) return;

	const Palette *pal = &PALETTES[monitor_tint];

	/* Get mouse state (physical window coords) */
	float mx_f, my_f;
	SDL_GetMouseState(&mx_f, &my_f);
	int mx = (int)mx_f;
	int my = (int)my_f;

	SDL_SetRenderDrawColor(renderer, 10, 10, 12, 255);
	SDL_RenderClear(renderer);

	/* ── 1. CRT TERMINAL AREA (Direct Physical Render) ─────────────────────── */

	/* Black background fills the CRT area */
	SDL_SetRenderDrawColor(renderer, pal->bg.r, pal->bg.g, pal->bg.b, pal->bg.a);
	SDL_FRect crt_bg = { 0, (float)MENU_BAR_H, (float)CRT_DISP_W, (float)CRT_DISP_H };
	SDL_RenderFillRect(renderer, &crt_bg);

	/* Draw VRAM characters directly at 4x physical scale */
	uint64_t now_ms = SDL_GetTicks();
	bool cursor_on = emulation_paused ? true : ((now_ms / 333) % 2 == 0);

	if (machine_powered) {
		for (int row = 0; row < TERM_ROWS; row++) {
			for (int col = 0; col < TERM_COLS; col++) {
				uint8_t val = vram[row][col];
				int gx = col * CELL_W;
				int gy = row * CELL_H + MENU_BAR_H;

				if (val == 0x00) {
					/* Cursor blink */
					if (cursor_on)
						draw_char_4x_glow(renderer, '@', gx, gy, pal);
				} else if (val == 0xFF) {
					draw_char_4x_glow(renderer, '_', gx, gy, pal);
				} else if (val != 0x20) {
					draw_char_4x_glow(renderer, val, gx, gy, pal);
				}
			}
		}
	} else {
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_FRect crt_off = { 0, (float)MENU_BAR_H, (float)CRT_DISP_W, (float)CRT_DISP_H };
		SDL_RenderFillRect(renderer, &crt_off);
	}

	/* CRT Scanlines — drawn directly at 4x physical scale (1px black line every 4 rows) */
	if (scanlines_enabled) {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, (uint8_t)(255 * scanline_opacity));
		for (int y = MENU_BAR_H; y < CRT_DISP_H + MENU_BAR_H; y += 4) {
			SDL_RenderLine(renderer, 0, y, CRT_DISP_W, y);
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	/* ── 2. SIDEBAR PANEL ─────────────────────────────────────────────────── */
	SDL_FRect sidebar = { (float)CRT_DISP_W, (float)MENU_BAR_H, (float)SIDEBAR_W, (float)CRT_DISP_H };
	SDL_SetRenderDrawColor(renderer, 18, 18, 20, 255);
	SDL_RenderFillRect(renderer, &sidebar);
	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	SDL_RenderLine(renderer, CRT_DISP_W, MENU_BAR_H, CRT_DISP_W, SCREEN_H);

	int sx = CRT_DISP_W + 15; /* sidebar x origin */

	SDL_Color amber  = { 255, 176,   0, 255 };
	SDL_Color green  = {  51, 255,  51, 255 };
	SDL_Color dimmed = { pal->pixel.r / 2, pal->pixel.g / 2, pal->pixel.b / 2, 180 };

	/* Header */
	char hdr_buf[64];
	if (g_bus) {
		snprintf(hdr_buf, sizeof(hdr_buf), "APPLE-1 (%dKB)", g_bus->ram_size / 1024);
	} else {
		strcpy(hdr_buf, "APPLE-1 CONSOLE");
	}
	draw_text_2x(renderer, hdr_buf, sx, 20 + MENU_BAR_H, amber);
	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	SDL_RenderLine(renderer, CRT_DISP_W, 44 + MENU_BAR_H, SCREEN_W, 44 + MENU_BAR_H);

	/* CPU Registers */
	draw_text_2x(renderer, "CPU REGISTERS:", sx, 54 + MENU_BAR_H, amber);
	char reg_buf[64];
	if (g_cpu && g_bus) {
		snprintf(reg_buf, sizeof(reg_buf), "A :%02X  X:%02X  Y:%02X",
			g_cpu->a, g_cpu->x, g_cpu->y);
		draw_text_2x(renderer, reg_buf, sx, 76 + MENU_BAR_H, dimmed);
		snprintf(reg_buf, sizeof(reg_buf), "SP:%02X  PC:%04X  P:%02X",
			g_cpu->s, g_cpu->pc, g_cpu->p);
		draw_text_2x(renderer, reg_buf, sx, 96 + MENU_BAR_H, dimmed);
		snprintf(reg_buf, sizeof(reg_buf), "FLAGS:%c%c-%c%c%c%c%c  %s",
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
	SDL_RenderLine(renderer, CRT_DISP_W, 136 + MENU_BAR_H, SCREEN_W, 136 + MENU_BAR_H);

	/* Emulation Controls */
	draw_text_2x(renderer, "CONTROLS:", sx, 146 + MENU_BAR_H, amber);
	draw_button(renderer, sx,       166 + MENU_BAR_H, 115, 28, "RESET",     green, mx, my);
	draw_button(renderer, sx + 125, 166 + MENU_BAR_H, 145, 28, "CLR SCREEN", green, mx, my);

	/* Pause / Resume Button */
	draw_button(renderer, sx, 204 + MENU_BAR_H, 270, 28, emulation_paused ? "RESUME EMULATION" : "PAUSE EMULATION", green, mx, my);

	char speed_label[48];
	if (g_bus) {
		snprintf(speed_label, sizeof(speed_label), "SPEED:%s",
			g_bus->opts.uncapped ? "UNCAPPED" : "1.02MHZ");
	} else {
		strcpy(speed_label, "SPEED:CAPPED");
	}
	draw_button(renderer, sx, 242 + MENU_BAR_H, 270, 28, speed_label, green, mx, my);

	SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
	SDL_RenderLine(renderer, CRT_DISP_W, 280 + MENU_BAR_H, SCREEN_W, 280 + MENU_BAR_H);

	/* Cassette Deck */
	draw_text_2x(renderer, "CASSETTE DECK:", sx, 290 + MENU_BAR_H, amber);
	char tape_disp[32];
	if (selected_cassette_idx >= 0 && selected_cassette_idx < num_cassettes) {
		const char *tape_base = cassette_files[selected_cassette_idx];
		const char *slash = strrchr(tape_base, '/');
		if (slash) tape_base = slash + 1;
		snprintf(tape_disp, sizeof(tape_disp), "%.20s", tape_base);
	} else {
		snprintf(tape_disp, sizeof(tape_disp), "NO TAPE");
	}
	draw_text_2x(renderer, tape_disp, sx, 312 + MENU_BAR_H, dimmed);

	draw_button(renderer, sx,        338 + MENU_BAR_H, 35, 24, "<",        green, mx, my);
	draw_button(renderer, sx + 45,   338 + MENU_BAR_H, 180, 24, "SEL TAPE", green, mx, my);
	draw_button(renderer, sx + 235,  338 + MENU_BAR_H, 35, 24, ">",        green, mx, my);
	draw_button(renderer, sx,        372 + MENU_BAR_H, 130, 24, "PLAY/LOAD", green, mx, my);
	draw_button(renderer, sx + 140,  372 + MENU_BAR_H, 130, 24, "REC/SAVE",  green, mx, my);

	/* Terminal Baud Rate Selector */
	Field *f_baud = by_flag('B');
	const char *baud_val = (f_baud && f_baud->cidx >= 0 && f_baud->cidx < f_baud->nchoices) ? f_baud->choices[f_baud->cidx] : "300";
	char baud_label[64];
	if (strcmp(baud_val, "Fast") == 0) {
		snprintf(baud_label, sizeof(baud_label), "TERM BAUD: FAST");
	} else {
		snprintf(baud_label, sizeof(baud_label), "TERM BAUD: %s", baud_val);
	}
	draw_button(renderer, sx,        406 + MENU_BAR_H, 270, 28, baud_label, green, mx, my);

	SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
	SDL_RenderLine(renderer, CRT_DISP_W, 446 + MENU_BAR_H, SCREEN_W, 446 + MENU_BAR_H);

	/* Status Bar */
	SDL_FRect sbar = { (float)(CRT_DISP_W + 10), (float)(SCREEN_H - 42), (float)(SIDEBAR_W - 20), 32 };
	SDL_SetRenderDrawColor(renderer, 22, 22, 26, 255);
	SDL_RenderFillRect(renderer, &sbar);
	SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
	SDL_RenderRect(renderer, &sbar);
	draw_text_2x(renderer, status_text, CRT_DISP_W + 14, SCREEN_H - 34, amber);

	/* ── 3. MENU BAR & DROP DOWN & CONFIG MODAL ────────────────────────────── */
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
		draw_config_modal();
	}

	SDL_RenderPresent(renderer);
}

/* ── MOUSE CLICK HANDLING ────────────────────────────────────────────────── */

static void
handle_config_click(int bx, int by, bool is_wizard, bool *out_done)
{
	int bby = MODAL_Y + MODAL_BBY;

	/* 1. Check bottom buttons */
	if (bx >= MODAL_X + 20 && bx < MODAL_X + 180 && by >= bby && by < bby + 34) {
		save_conf(fields[ICFG].sval);
		set_config_status("CONFIG SAVED!", 2500);
		if (is_wizard) *out_done = true;
		return;
	}
	if (bx >= MODAL_X + 200 && bx < MODAL_X + 380 && by >= bby && by < bby + 34) {
		save_conf(fields[ICFG].sval);
		if (is_wizard) {
			*out_done = true;
		} else {
			reboot_emulator();
		}
		return;
	}
	if (!is_wizard && bx >= MODAL_X + 400 && bx < MODAL_X + 500 && by >= bby && by < bby + 34) {
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

		if (bx >= track_x && bx < track_x + track_w && by >= track_y && by < track_y + track_h) {
			float click_pct = (float)(by - track_y) / (float)track_h;
			int new_offset = (int)(click_pct * (NF - VISIBLE_FIELDS + 1));
			if (new_offset < 0) new_offset = 0;
			if (new_offset > NF - VISIBLE_FIELDS) new_offset = NF - VISIBLE_FIELDS;
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
		if (i >= NF) break;
		int fy = MODAL_Y + SB_Y + idx * FIELD_H;
		if (by < fy || by >= fy + FIELD_H - 2 || bx < MODAL_X + SB_X || bx >= MODAL_X + SB_X + FIELD_W) {
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
				f->cidx = (f->cidx - 1 + f->nchoices) % f->nchoices;
				snprintf(f->sval, sizeof(f->sval), "%s", f->choices[f->cidx]);
			} else if (bx >= vx + 120 && bx < vx + 150) {
				f->cidx = (f->cidx + 1) % f->nchoices;
				snprintf(f->sval, sizeof(f->sval), "%s", f->choices[f->cidx]);
			}
			break;
		case FT_FILE:
			if (bx >= vx + 380 && bx < vx + 470) {
				char picked[512] = { 0 };
				const char *ext = (f->flag == 'r' || f->flag == 'a') ? "*.rom *.bin" : "*";
				if (pick_file_dialog(picked, sizeof(picked), "Select ROM", ext)) {
					snprintf(f->sval, sizeof(f->sval), "%s", picked);
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

static void
handle_mouse_event(const SDL_MouseButtonEvent *button)
{
	if (button->button != SDL_BUTTON_LEFT) return;

	int x = (int)button->x;
	int y = (int)button->y;

	if (config_modal_open) {
		bool dummy = false;
		handle_config_click(x, y, false, &dummy);
		return;
	}

	if (file_menu_open) {
		int drop_x = 10;
		int drop_y = MENU_BAR_H;
		int drop_w = 260;
		int item_h = 30;
		int num_items = 4;

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y && y < drop_y + item_h * num_items + 6) {
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

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y && y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0: /* Power Plug */
					machine_powered = !machine_powered;
					if (!machine_powered) {
						for (int r = 0; r < 24; r++) {
							memset(vram[r], 0x20, 40);
						}
						cx = 0;
						cy = 0;
						vram[cy][cx] = 0x00;
						screen_dirty = true;
						emulation_paused = false;
						strncpy(status_text, "POWER DISCONNECTED.", sizeof(status_text) - 1);
					} else {
						for (int r = 0; r < 24; r++) {
							for (int c = 0; c < 40; c++) {
								vram[r][c] = (c % 2) ? 0x00 : 0xFF;
							}
						}
						cx = 0;
						cy = 0;
						vram[cy][cx] = 0x00;
						boot_sweep = true;
						if (g_cpu) {
							g_cpu->halted = true;
						}
						if (g_bus) {
							if (g_bus->opts.randomize_cold_boot) {
								for (uint32_t i = 0; i < g_bus->ram_size; i++) {
									g_bus->ram[i] = (rand() & 1) ? 0xFF : 0x00;
								}
							} else {
								memset(g_bus->ram, 0, g_bus->ram_size);
							}
						}
						strncpy(status_text, "POWER CONNECTED.", sizeof(status_text) - 1);
					}
					break;
				case 1: /* Pause Emulation */
					if (machine_powered) {
						emulation_paused = !emulation_paused;
						if (emulation_paused) {
							strncpy(status_text, "EMULATION PAUSED.", sizeof(status_text) - 1);
						} else {
							strncpy(status_text, "EMULATION RESUMED.", sizeof(status_text) - 1);
						}
					}
					break;
				case 2: /* Reset */
					reset_pending = true;
					strncpy(status_text, "RESETTING CPU...", sizeof(status_text) - 1);
					break;
				case 3: /* Clear screen */
					for (int r = 0; r < 24; r++) {
						memset(vram[r], 0x20, 40);
					}
					cx = 0;
					cy = 0;
					vram[cy][cx] = 0x00;
					screen_dirty = true;
					strncpy(status_text, "VRAM CLEARED.", sizeof(status_text) - 1);
					break;
				case 4: /* Speed Toggle */
					if (g_bus) {
						g_bus->opts.uncapped = !g_bus->opts.uncapped;
						if (g_bus->opts.uncapped) {
							g_bus->opts.emulate_dram_refresh = false;
							strncpy(status_text, "SPEED: UNCAPPED", sizeof(status_text) - 1);
							if (trace_window_open) {
								toggle_trace_window();
							}
						} else {
							g_bus->opts.emulate_dram_refresh = true;
							strncpy(status_text, "SPEED: 1.02 MHZ", sizeof(status_text) - 1);
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

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y && y < drop_y + item_h * num_items + 6) {
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

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y && y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					toggle_debug_window();
					break;
				case 1:
					dbg_needs_step = true;
					break;
				case 2:
					if (g_dbg) {
						g_dbg->step_mode = false;
					}
					break;
				case 3:
					if (g_dbg) {
						g_dbg->num_breakpoints = 0;
						strncpy(status_text, "BREAKPOINTS CLEARED.", sizeof(status_text) - 1);
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

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y && y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					if (g_bus && g_bus->opts.uncapped) {
						strncpy(status_text, "TRACE NEEDS 1.02MHZ", sizeof(status_text) - 1);
					} else {
						toggle_trace_window();
					}
					break;
				case 1:
					trace_head = 0;
					trace_count = 0;
					trace_scroll = 0;
					strncpy(status_text, "TRACE BUFFER CLEARED.", sizeof(status_text) - 1);
					break;
				case 2:
					export_trace_file();
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

		if (x >= drop_x && x < drop_x + drop_w && y >= drop_y && y < drop_y + item_h * num_items + 6) {
			int click_idx = (y - (drop_y + 3)) / item_h;
			if (click_idx >= 0 && click_idx < num_items) {
				switch (click_idx) {
				case 0:
					system("xdg-open documentation/manual.md &");
					break;
				case 1:
					system("xdg-open documentation/apple-1_operation_manual.pdf &");
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
		strncpy(status_text, "RESETTING CPU...", sizeof(status_text) - 1);
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
				strncpy(status_text, "EMULATION PAUSED.", sizeof(status_text) - 1);
			} else {
				strncpy(status_text, "EMULATION RESUMED.", sizeof(status_text) - 1);
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
				strncpy(status_text, "SPEED: UNCAPPED", sizeof(status_text) - 1);
				if (trace_window_open) {
					toggle_trace_window();
				}
			} else {
				g_bus->opts.emulate_dram_refresh = true;
				strncpy(status_text, "SPEED: 1.02 MHZ", sizeof(status_text) - 1);
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
				selected_cassette_idx = (selected_cassette_idx - 1 + num_cassettes) % num_cassettes;
			}
			const char *name = cassette_files[selected_cassette_idx];
			const char *slash = strrchr(name, '/');
			if (slash) name = slash + 1;
			snprintf(status_text, sizeof(status_text), "SELECTED TAPE: %s", name);
		} else {
			strncpy(status_text, "NO TAPES AVAILABLE", sizeof(status_text) - 1);
		}
		return;
	}

	/* Cassette Selection Next (sx + 235, 338, 35, 24) */
	if (x >= sx + 235 && x < sx + 235 + 35 && y >= 338 && y < 338 + 24) {
		if (num_cassettes > 0) {
			if (selected_cassette_idx < 0) {
				selected_cassette_idx = 0;
			} else {
				selected_cassette_idx = (selected_cassette_idx + 1) % num_cassettes;
			}
			const char *name = cassette_files[selected_cassette_idx];
			const char *slash = strrchr(name, '/');
			if (slash) name = slash + 1;
			snprintf(status_text, sizeof(status_text), "SELECTED TAPE: %s", name);
		} else {
			strncpy(status_text, "NO TAPES AVAILABLE", sizeof(status_text) - 1);
		}
		return;
	}

	/* Play / Load Tape (sx, 372, 130, 24) */
	if (x >= sx && x < sx + 130 && y >= 372 && y < 372 + 24) {
		expansion_card_t *aci_card = get_or_add_aci();
		if (aci_card) {
			if (selected_cassette_idx >= 0 && selected_cassette_idx < num_cassettes) {
				const char *path = cassette_files[selected_cassette_idx];
				if (aci_load_tape(aci_card, path)) {
					snprintf(status_text, sizeof(status_text), "TAPE LOADED! C100R");
				} else {
					snprintf(status_text, sizeof(status_text), "TAPE LOAD FAILED");
				}
			} else {
				strncpy(status_text, "NO TAPE SELECTED", sizeof(status_text) - 1);
			}
		}
		return;
	}

	/* Record / Save Tape (sx + 140, 372, 130, 24) */
	if (x >= sx + 140 && x < sx + 140 + 130 && y >= 372 && y < 372 + 24) {
		expansion_card_t *aci_card = get_or_add_aci();
		if (aci_card) {
			if (selected_cassette_idx >= 0 && selected_cassette_idx < num_cassettes) {
				if (aci_save_tape(aci_card, "recorded_tape.aci")) {
					strncpy(status_text, "TAPE SAVED", sizeof(status_text) - 1);
					scan_tapes();
				} else {
					strncpy(status_text, "TAPE SAVE FAILED", sizeof(status_text) - 1);
				}
			} else {
				strncpy(status_text, "NO TAPE SELECTED", sizeof(status_text) - 1);
			}
		}
		return;
	}

	/* Baud Speed Button (sx, 406, 270, 28) */
	if (x >= sx && x < sx + 270 && y >= 406 && y < 406 + 28) {
		Field *f_baud = by_flag('B');
		if (f_baud) {
			f_baud->cidx = (f_baud->cidx + 1) % f_baud->nchoices;
			snprintf(f_baud->sval, sizeof(f_baud->sval), "%s", f_baud->choices[f_baud->cidx]);
			if (strcmp(f_baud->sval, "Fast") == 0) {
				snprintf(status_text, sizeof(status_text), "BAUD SET TO: FAST");
			} else {
				snprintf(status_text, sizeof(status_text), "BAUD SET TO: %s", f_baud->sval);
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

	/* Ctrl shortcuts only — printable chars handled via SDL_EVENT_TEXT_INPUT */
	if (mod & SDL_KMOD_CTRL) {
		if (sym == SDLK_R) {
			reset_pending = true;
			strncpy(status_text, "RESETTING CPU...", sizeof(status_text) - 1);
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
			strncpy(status_text, "VRAM CLEARED.", sizeof(status_text) - 1);
			return;
		}
		if (sym == SDLK_C) {
			exit(0);
		}
		/* Swallow other Ctrl combos so they don't feed text input */
		return;
	}

	/* Special non-printable keys */
	int ch = 0;
	if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
		ch = 0x0D;
	} else if (sym == SDLK_BACKSPACE || sym == SDLK_DELETE) {
		ch = 0x5F; /* Apple-1 backspace is '_' */
	}

	if (ch != 0) {
		buffered_key_sdl = (uint8_t)(ch | 0x80);
	}
	/* All printable chars are handled by SDL_EVENT_TEXT_INPUT below */
}

static void
handle_config_text_input(const char *t)
{
	if (editing_field_idx < 0) return;
	Field *f = &fields[editing_field_idx];
	size_t tl = strlen(t);
	size_t sl = strlen(f->sval);
	if (sl + tl < sizeof(f->sval) - 1) {
		memmove(f->sval + f->cursor + tl, f->sval + f->cursor, sl - f->cursor + 1);
		memcpy(f->sval + f->cursor, t, tl);
		f->cursor += (int)tl;
	}
}

static void
handle_config_key_event(const SDL_KeyboardEvent *key)
{
	if (editing_field_idx < 0) return;
	Field *f = &fields[editing_field_idx];
	SDL_Keycode k = key->key;
	int sl = (int)strlen(f->sval);

	if (k == SDLK_RETURN || k == SDLK_ESCAPE) {
		f->editing = false;
		editing_field_idx = -1;
	} else if (k == SDLK_BACKSPACE && f->cursor > 0) {
		memmove(f->sval + f->cursor - 1, f->sval + f->cursor, sl - f->cursor + 1);
		f->cursor--;
	} else if (k == SDLK_DELETE && f->cursor < sl) {
		memmove(f->sval + f->cursor, f->sval + f->cursor + 1, sl - f->cursor);
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

static void
set_config_status(const char *m, int ms)
{
	snprintf(config_status_msg, sizeof(config_status_msg), "%s", m);
	config_status_until = SDL_GetTicks() + (uint64_t)ms;
}

/* ── EXPORTED TERM INTERFACE ─────────────────────────────────────────────── */

void
term_init(void)
{
	/* Load config path and configuration values */
	get_xdg_config_path(fields[ICFG].sval, sizeof(fields[ICFG].sval));
	if (access(fields[ICFG].sval, F_OK) != 0) {
		/* No config file found — open CONFIG modal on first render
		 * so the user is prompted to set things up and save. */
		config_scroll_offset = 0;
		config_modal_open = true;
	} else {
		load_conf(fields[ICFG].sval);
	}

	/* Initialize SDL3 */
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		fprintf(stderr, "Failed to initialize SDL3: %s\n", SDL_GetError());
		exit(1);
	}

	window = SDL_CreateWindow("Apple-1 System Console", SCREEN_W, SCREEN_H, 0);
	if (!window) {
		fprintf(stderr, "Failed to create SDL3 window: %s\n", SDL_GetError());
		SDL_Quit();
		exit(1);
	}

	renderer = SDL_CreateRenderer(window, NULL);
	if (!renderer) {
		fprintf(stderr, "Failed to create SDL3 renderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		exit(1);
	}

	/* Load custom charmap */
	charmap_loaded = load_charmap();
	if (!charmap_loaded) {
		fprintf(stderr, "Warning: 2513_Apple-1.bin could not be loaded.\n");
	}

	/* SDL3 requires explicit opt-in to receive SDL_EVENT_TEXT_INPUT events */
	SDL_StartTextInput(window);

	/* Scan available cassettes */
	scan_tapes();

	Field *f_tape = by_flag('e');
	if (f_tape && f_tape->sval[0] != '\0') {
		if (access(f_tape->sval, F_OK) == 0) {
			int found_idx = -1;
			for (int i = 0; i < num_cassettes; i++) {
				if (strcmp(cassette_files[i], f_tape->sval) == 0) {
					found_idx = i;
					break;
				}
			}
			if (found_idx >= 0) {
				selected_cassette_idx = found_idx;
			} else {
				if (num_cassettes < MAX_CASSETTES) {
					num_cassettes++;
				}
				for (int i = num_cassettes - 1; i > 0; i--) {
					strcpy(cassette_files[i], cassette_files[i-1]);
				}
				strcpy(cassette_files[0], f_tape->sval);
				selected_cassette_idx = 0;
			}
		} else {
			selected_cassette_idx = -1;
		}
	} else {
		selected_cassette_idx = -1;
	}

	/* Setup initial shift register garbage in VRAM */
	if (!vram_initialized) {
		for (int y = 0; y < 24; y++) {
			for (int x = 0; x < 40; x++) {
				vram[y][x] = (x % 2) ? 0x00 : 0xFF;
			}
		}
		cx = 0;
		cy = 0;
		vram[cy][cx] = 0x00; /* start active cursor at 0,0 */
		boot_sweep = true;
		vram_initialized = true;
	}

	last_redraw_ms = SDL_GetTicks();
	render_gui();
}

void
term_shutdown(void)
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
			return fields[i].bval;
		}
	}
	return false;
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
	if (val == 0x5F && is_real_backspace_enabled()) {
		if (cx > 0) {
			vram[cy][cx] = 0x20;
			cx--;
			vram[cy][cx] = 0x00;
		} else if (cy > 0) {
			vram[cy][cx] = 0x20;
			cx = 39;
			cy--;
			vram[cy][cx] = 0x00;
		}
		screen_dirty = true;
	} else if (val == 0x0D) {
		/* carriage return: clear to end of current line */
		for (int x = cx; x < 40; x++) {
			vram[cy][x] = 0x20;
		}
		cx = 0;
		cy++;
		if (cy >= 24) {
			scroll_up();
			cy = 23;
		}
		vram[cy][cx] = 0x00;
		screen_dirty = true;
	} else if (val == 0x0C) {
		/* clear display */
		for (int y = 0; y < 24; y++) {
			memset(vram[y], 0x20, 40);
		}
		cx = 0;
		cy = 0;
		vram[cy][cx] = 0x00;
		screen_dirty = true;
	} else if (val == 0x0A) {
		/* ignore line feed */
	} else {
		/* Hardware ASCII Fold */
		uint8_t glyphCode = val & 0x7F;
		if (glyphCode & 0x40) {
			glyphCode &= 0xDF;
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
		Field *f_baud = by_flag('B');
		if (f_baud && f_baud->sval[0] && strcmp(f_baud->sval, "Fast") != 0) {
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

	/* Throttled render (fast/unbauded path) */
	uint64_t now = SDL_GetTicks();
	if (now - last_redraw_ms >= 16) {
		render_gui();
		last_redraw_ms = now;
		screen_dirty = false;
	}
}

void
term_update(void)
{
	uint64_t now = SDL_GetTicks();
	if (now - last_redraw_ms >= 16) {
		render_gui();
		if (debug_window_open && debug_ren) {
			render_debug_window();
		}
		if (trace_window_open && trace_ren) {
			render_trace_window();
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
		return SDL_GetWindowFromID(ev->window.windowID);
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return SDL_GetWindowFromID(ev->key.windowID);
	case SDL_EVENT_TEXT_INPUT:
		return SDL_GetWindowFromID(ev->text.windowID);
	case SDL_EVENT_MOUSE_MOTION:
		return SDL_GetWindowFromID(ev->motion.windowID);
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return SDL_GetWindowFromID(ev->button.windowID);
	case SDL_EVENT_MOUSE_WHEEL:
		return SDL_GetWindowFromID(ev->wheel.windowID);
	default:
		return NULL;
	}
}

uint8_t
term_poll(void)
{
	term_update();

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		SDL_Window *win = get_event_window(&event);
		if (win == debug_win) {
			handle_debug_window_event(&event);
		} else if (win == trace_win) {
			handle_trace_window_event(&event);
		} else {
			if (event.type == SDL_EVENT_QUIT) {
				exit(0);
			} else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
				exit(0);
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (config_modal_open && editing_field_idx >= 0) {
					handle_config_key_event(&event.key);
				} else {
					handle_key_event(&event.key);
				}
			} else if (event.type == SDL_EVENT_TEXT_INPUT) {
				if (config_modal_open && editing_field_idx >= 0) {
					handle_config_text_input(event.text.text);
				} else {
					/* Use text input for printable chars — SDL handles shift/caps correctly */
					char c = event.text.text[0];
					if (c >= 32 && c <= 126 && buffered_key_sdl == 0) {
						/* Apple-1 only has uppercase — force it */
						if (c >= 'a' && c <= 'z') c = (char)(c - 32);
						buffered_key_sdl = (uint8_t)((unsigned char)c | 0x80);
					}
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				handle_mouse_event(&event.button);
			} else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				if (config_modal_open) {
					config_scroll_offset -= (int)event.wheel.y;
					if (config_scroll_offset < 0) config_scroll_offset = 0;
					if (config_scroll_offset > NF - VISIBLE_FIELDS) config_scroll_offset = NF - VISIBLE_FIELDS;
				}
			}
		}
	}

	uint8_t key = buffered_key_sdl;
	buffered_key_sdl = 0;
	return key;
}

void
term_set_welcome(const char *msg1, const char *msg2)
{
	/* Welcome messages not drawn on screen as requested, so this is a no-op */
	(void)msg1;
	(void)msg2;
}

bool
term_reset_pending(void)
{
	if (reset_pending) {
		reset_pending = false;
		strncpy(status_text, "CPU RESET COMPLETED.", sizeof(status_text) - 1);
		render_gui(); /* Update the status text on screen immediately */
		return true;
	}
	return false;
}

/* ── FIRST-TIME SETUP WIZARD ─────────────────────────────────────────────── */

void
term_run_config_wizard(void)
{
	get_xdg_config_path(fields[ICFG].sval, sizeof(fields[ICFG].sval));

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
		return;
	}

	SDL_Window   *wiz_win = SDL_CreateWindow(
		"Apple-1 First Time Setup — Save Config to Continue",
		SCREEN_W, SCREEN_H, 0);
	SDL_Renderer *wiz_ren = SDL_CreateRenderer(wiz_win, NULL);
	SDL_StartTextInput(wiz_win);

	/* Temporarily wire global renderer/window to the wizard */
	renderer = wiz_ren;
	window   = wiz_win;

	charmap_loaded = load_charmap();
	config_scroll_offset = 0;
	config_modal_open = true;

	/* Banner message: wizard mode — no way out without saving */
	set_config_status("NO CONFIG FOUND — FILL IN SETTINGS AND CLICK SAVE", 0xFFFFFFFF);
	config_status_until = UINT64_MAX;

	bool done = false;
	while (!done) {
		/* Dark background */
		SDL_SetRenderDrawColor(renderer, 8, 8, 10, 255);
		SDL_RenderClear(renderer);
		draw_config_modal();
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
				window   = NULL;
				fprintf(stderr, "Setup cancelled. No config saved.\n");
				exit(1);
			}

			if (ev.type == SDL_EVENT_KEY_DOWN) {
				if (ev.key.key == SDLK_ESCAPE) continue; /* blocked */
				if (editing_field_idx >= 0)
					handle_config_key_event(&ev.key);
			}

			if (ev.type == SDL_EVENT_TEXT_INPUT) {
				if (editing_field_idx >= 0)
					handle_config_text_input(ev.text.text);
			}

			if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
				config_scroll_offset -= (int)ev.wheel.y;
				if (config_scroll_offset < 0) config_scroll_offset = 0;
				if (config_scroll_offset > NF - VISIBLE_FIELDS) config_scroll_offset = NF - VISIBLE_FIELDS;
			}

			if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
			    ev.button.button == SDL_BUTTON_LEFT) {
				int bx = (int)ev.button.x;
				int by = (int)ev.button.y;
				handle_config_click(bx, by, true, &done);
			}
		}
		SDL_Delay(16);
	}

	SDL_StopTextInput(wiz_win);
	SDL_DestroyRenderer(wiz_ren);
	SDL_DestroyWindow(wiz_win);
	SDL_Quit();
	renderer = NULL;
	window   = NULL;
	config_modal_open = false;
}

bool
term_is_powered(void)
{
	return machine_powered;
}

bool
term_trace_active(void)
{
	return trace_window_open && (trace_buf != NULL);
}

void
term_trace_push(const char *line)
{
	if (trace_frozen) return;

	if (!trace_buf) {
		trace_buf = calloc(trace_max, sizeof(TraceLine));
		if (!trace_buf) return;
	}
	strncpy(trace_buf[trace_head].line, line, TRACE_LINE_LEN - 1);
	trace_buf[trace_head].line[TRACE_LINE_LEN - 1] = '\0';
	trace_head = (trace_head + 1) % trace_max;
	if (trace_count < trace_max) {
		trace_count++;
	}
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

bool
term_is_paused(void)
{
	return emulation_paused;
}

void
term_close_debugger(void)
{
	if (debug_window_open) {
		toggle_debug_window();
	}
}
