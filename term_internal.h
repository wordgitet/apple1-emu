#ifndef TERM_INTERNAL_H
#define TERM_INTERNAL_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "bus.h"
#include "cpu.h"
#include "dbg.h"

/* ── CONSTANTS & PALETTES ────────────────────────────────────────────────── */
#define CELL_W	      28
#define CELL_H	      32
#define GLYPH_COLS    5
#define GLYPH_ROWS    7
#define TERM_COLS     40
#define TERM_ROWS     24
#define CRT_DISP_W    (TERM_COLS * CELL_W) /* 1120 */
#define CRT_DISP_H    (TERM_ROWS * CELL_H) /* 768 */
#define SIDEBAR_W     300
#define MENU_BAR_H    28
#define SCREEN_W      (CRT_DISP_W + SIDEBAR_W)	/* 1420 */
#define SCREEN_H      (CRT_DISP_H + MENU_BAR_H) /* 796 */

#define MAX_CASSETTES 32

enum monitor_tint { MONITOR_GREEN, MONITOR_AMBER, MONITOR_MONO };

struct palette {
	SDL_Color bg;
	SDL_Color pixel;
	SDL_Color glow;
};

extern const struct palette PALETTES[];

/* ── SHARED GLOBAL VARIABLES ─────────────────────────────────────────────── */
extern struct bus *g_bus;
extern struct cpu *g_cpu;
extern debugger_t *g_dbg;
extern bool g_debug_enabled;
extern char *g_argv0;

extern SDL_Window *window;
extern SDL_Renderer *renderer;

extern uint8_t charmap_data[2048];
extern bool charmap_loaded;
extern int charmap_size;

extern enum monitor_tint monitor_tint;
extern bool scanlines_enabled;
extern float scanline_opacity;

extern char cassette_files[MAX_CASSETTES][512];
extern int num_cassettes;
extern int selected_cassette_idx;

extern char status_text[128];
extern bool config_modal_open;

/* Config modal status message */
extern char config_status_msg[256];
extern uint64_t config_status_until;

/* Colors from config_ui */
extern const SDL_Color PANEL;
extern const SDL_Color BORD;
extern const SDL_Color AMBER;
extern const SDL_Color GREEN;
extern const SDL_Color DIM;
extern const SDL_Color SELBG;
extern const SDL_Color SELBO;
extern const SDL_Color WHITE;
extern const SDL_Color RED;
extern const SDL_Color BTNBG;
extern const SDL_Color BTNHV;

/* ── CONFIG FIELDS ────────────────────────────────────────────────────────── */
enum ftype { FT_STR, FT_BOOL, FT_CHOICE, FT_FILE };
struct field {
	const char *label;
	const char *hint;
	char flag;
	enum ftype type;
	bool bval;
	char sval[512];
	const char **choices;
	int nchoices;
	int cidx;
	bool editing;
	int cursor;
};

extern struct field fields[];
extern const int NF;
extern const int ICFG;
extern int editing_field_idx;
extern int config_scroll_offset;
#define VISIBLE_FIELDS 8

#define MODAL_W	       900
#define MODAL_H	       560
#define MODAL_X	       ((SCREEN_W - MODAL_W) / 2)
#define MODAL_Y	       ((SCREEN_H - MODAL_H) / 2)
#define MODAL_BBY      (MODAL_H - 55)

#define SB_X	       20
#define SB_Y	       96
#define FIELD_H	       42
#define FIELD_W	       820

/* ── SHARED RENDER & UTILITY FUNCTIONS ───────────────────────────────────── */
void
draw_text_scaled(SDL_Renderer *rend,
    const char *str,
    int x,
    int y,
    int scale,
    SDL_Color color);
void
draw_text_2x(SDL_Renderer *rend, const char *str, int x, int y, SDL_Color color);
bool
draw_button(SDL_Renderer *rend,
    int x,
    int y,
    int w,
    int h,
    const char *lbl,
    SDL_Color tint,
    int mx,
    int my);
int
ascii_to_rom_idx(uint8_t ascii);
bool
load_charmap(void);
void
set_config_status(const char *m, int ms);
void
scan_tapes(void);
void
reboot_emulator(void);
bool
pick_file_dialog(char *out_path,
    size_t max_len,
    const char *title,
    const char *ext);

#endif /* TERM_INTERNAL_H */
