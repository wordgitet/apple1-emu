/*
 * main_nspire.c - TI-Nspire (Ndless) entry point for the Apple-1 emulator.
 *
 * Differences from main.c (POSIX CLI):
 *   - No command-line argument parsing.  The Nspire launcher gives argv[0]
 *     (the .tns path) but no shell arguments.
 *   - Config is loaded from a hardcoded path:
 *       <documents_dir>/ndless/apple1.conf.tns
 *     If the file is absent the emulator boots with all defaults.
 *   - Data files (ROM dumps) may use a .tns extension for Ndless transfer;
 *     rename raw .bin images to .tns — the VFS reads file bytes as-is.
 *   - cpu_reset() is called immediately (no "wait for Ctrl+R").
 *   - Quit/reset/clear are polled via isKeyPressed():
 *       ESC           -> exit
 *       DOC           -> reset 6502  (CX/touchpad; HOME on classic)
 *       Ctrl + DEL    -> clear screen
 *   - term_update() toggles cursor blink every ~256k CPU cycles (~250 ms).
 *   - No signal handler (port_signal_setup is a no-op on Nspire).
 */

#include "aci.h"
#include "bus.h"
#include "cli_config.h"
#include "cpu.h"
#include "io.h"
#include "krusader.h"
#include "port.h"
#include "term_apple1.h"

#include <libndls.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Config path                                                          */
/* ------------------------------------------------------------------ */

#define NSPIRE_CFG_SUBPATH "/ndless/apple1.conf.tns"
#define NSPIRE_DATA_PREFIX "/ndless/"

/* ------------------------------------------------------------------ */
/* Data file path resolution                                            */
/* ------------------------------------------------------------------ */

/*
 * Turn config-relative paths (roms/basic.tns) into absolute paths under
 * <documents>/ndless/.  enable_relative_paths() is unreliable when argv[0]
 * has no directory component, so fopen("roms/...") often fails on-device.
 * Falls back to Ndless locate() by basename.
 */
static char *
nspire_resolve_data_path(const char *path)
{
	char buf[384];
	const char *base;
	const char *docs;
	const char *rel;
	port_file_t f;

	if (path == NULL || path[0] == '\0')
		return (NULL);
	if (path[0] == '/')
		return (port_strdup(path));

	docs = get_documents_dir();
	rel = path;
	if (rel[0] == '.' && rel[1] == '/')
		rel += 2;
	if (strncmp(rel, "ndless/", 7) == 0)
		rel += 7;
	port_snprintf(buf, sizeof(buf), "%s%s%s", docs, NSPIRE_DATA_PREFIX, rel);
	f = port_vfs_default.open(buf, PORT_VFS_READ);
	if (f != PORT_FILE_INVALID) {
		port_vfs_default.close(f);
		return (port_strdup(buf));
	}

	base = strrchr(path, '/');
	if (base != NULL)
		base++;
	else
		base = path;
	if (locate(base, buf, sizeof(buf)) == 0)
		return (port_strdup(buf));

	return (port_strdup(path));
}

static void
nspire_fatal(const char *msg)
{
	port_term_write_buf(msg, port_strlen(msg));
	wait_key_pressed();
}

/* ------------------------------------------------------------------ */
/* Globals shared with bus.c / term_nspire.c                           */
/* ------------------------------------------------------------------ */

struct bus *g_bus = NULL;
struct cpu *g_cpu = NULL;
char *g_argv0   = NULL;
bool  g_debug_enabled = false;
uint32_t opt_baud = 0;

/* ------------------------------------------------------------------ */
/* Minimal log callback (writes to stderr via newlib)                  */
/* ------------------------------------------------------------------ */

static void
nspire_log(void *ctx, int level, const char *msg)
{
	(void)ctx;
	(void)level;
	fprintf(stderr, "%s\n", msg);
}

/* ------------------------------------------------------------------ */
/* apply_loaded_config — mirror of the one in main.c                   */
/*   (duplicated so main_nspire.c compiles standalone without main.c)  */
/* ------------------------------------------------------------------ */

static void
apply_cfg(struct cli_config_opts *cfg,
    char **rom_path,
    char **bin_path,
    uint16_t *bin_address,
    uint32_t *ram_size,
    bool *opt_uncapped,
    bool *opt_throttle_pia,
    bool *opt_emulate_dram,
    bool *opt_emulate_bounce,
    bool *opt_randomize_cold,
    bool *opt_flat_bus)
{
	if (cfg->rom_path != NULL) {
		if (*rom_path != NULL)
			port_free(*rom_path);
		*rom_path = cfg->rom_path;
		cfg->rom_path = NULL;
	}
	if (cfg->bin_path != NULL) {
		if (*bin_path != NULL)
			port_free(*bin_path);
		*bin_path = cfg->bin_path;
		cfg->bin_path = NULL;
		*bin_address = cfg->bin_address;
	}
	if (cfg->flat_bin_path != NULL) {
		if (*bin_path != NULL)
			port_free(*bin_path);
		*bin_path = cfg->flat_bin_path;
		cfg->flat_bin_path = NULL;
		*bin_address = 0x0000;
		*opt_flat_bus = true;
	}
	if (cfg->ram_size != 0)
		*ram_size = cfg->ram_size;
	*opt_uncapped = cfg->opt_uncapped;
	*opt_throttle_pia = cfg->opt_throttle_pia;
	*opt_emulate_dram = cfg->opt_emulate_dram;
	*opt_emulate_bounce = cfg->opt_emulate_bounce;
	*opt_randomize_cold = cfg->opt_randomize_cold;
	*opt_flat_bus = cfg->opt_flat_bus;
	if (cfg->baud != 0)
		opt_baud = cfg->baud;
}

/* ------------------------------------------------------------------ */
/* Key polling helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * Returns non-zero on the leading edge of ESC, DOC (CX) / HOME (classic),
 * or Ctrl+DEL — whichever fires first.  Caller interprets the return value.
 *
 * Return codes:
 *   0  = nothing special
 *   1  = ESC (quit)
 *   2  = reset
 *   3  = clear screen
 */
#define KEY_ACTION_NONE   0
#define KEY_ACTION_QUIT   1
#define KEY_ACTION_RESET  2
#define KEY_ACTION_CLEAR  3

static int
poll_special_keys(void)
{
	if (isKeyPressed(KEY_NSPIRE_ESC))
		return (KEY_ACTION_QUIT);

	/*
	 * DOC exists only on CX / touchpad models.  On classic hardware its
	 * row/col is _KEY_DUMMY_ROW / _KEY_DUMMY_COL which isKeyPressed()
	 * never matches, so the HOME fallback fires instead.
	 */
	if (isKeyPressed(KEY_NSPIRE_DOC) || (is_classic && isKeyPressed(KEY_NSPIRE_HOME)))
		return (KEY_ACTION_RESET);

	if (isKeyPressed(KEY_NSPIRE_CTRL) && isKeyPressed(KEY_NSPIRE_DEL))
		return (KEY_ACTION_CLEAR);

	return (KEY_ACTION_NONE);
}

/* Simple debounce: wait until all special keys are released. */
static void
wait_keys_released(void)
{
	while (isKeyPressed(KEY_NSPIRE_ESC) ||
	    isKeyPressed(KEY_NSPIRE_DOC)    ||
	    isKeyPressed(KEY_NSPIRE_HOME)   ||
	    isKeyPressed(KEY_NSPIRE_DEL))
		;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

/* Prototype for custom terminal clear defined in term_nspire.c */
void port_term_clear(void);
void term_cold_boot(void);

int
main(int argc, char **argv)
{
	static uint8_t static_ram[APPLE1_STATIC_RAM_SIZE];

	struct bus bus;
	struct cpu cpu;
	struct cli_config_opts cfg;
	char cfg_path[256];
	char errbuf[256];
	char *rom_path;
	char *bin_path;
	uint16_t bin_address;
	uint32_t ram_size;
	uint32_t k;
	uint32_t cycle_accumulator;
	uint32_t last_time;
	uint32_t last_kbd_poll;
	uint32_t term_cycle_acc;
	bool opt_uncapped;
	bool opt_throttle_pia;
	bool opt_emulate_dram;
	bool opt_emulate_bounce;
	bool opt_randomize_cold;
	bool opt_flat_bus;
	int cfg_rc;
	int action;

	(void)argc;

	/* Make file I/O relative to the .tns directory */
	enable_relative_paths(argv);
	g_argv0 = argv[0];

	/* Defaults */
	rom_path          = NULL;
	bin_path          = NULL;
	bin_address       = 0;
	ram_size          = (uint32_t)(APPLE1_DEFAULT_RAM_KB * 1024);
	opt_uncapped      = false;
	opt_throttle_pia  = true;
	opt_emulate_dram  = false;
	opt_emulate_bounce = false;
	opt_randomize_cold = true;
	opt_flat_bus      = false;

	/* Try to load config from <documents>/ndless/apple1.conf.tns */
	port_snprintf(cfg_path, sizeof(cfg_path),
	    "%s%s", get_documents_dir(), NSPIRE_CFG_SUBPATH);

	cli_config_init_defaults(&cfg);
	cfg_rc = load_config_file(cfg_path, &cfg, errbuf, sizeof(errbuf));
	if (cfg_rc == CLI_CONFIG_OK) {
		apply_cfg(&cfg,
		    &rom_path,
		    &bin_path,
		    &bin_address,
		    &ram_size,
		    &opt_uncapped,
		    &opt_throttle_pia,
		    &opt_emulate_dram,
		    &opt_emulate_bounce,
		    &opt_randomize_cold,
		    &opt_flat_bus);
	} else if (cfg_rc == CLI_CONFIG_PARSE) {
		fprintf(stderr,
		    "apple1.conf.tns: %s (using defaults)\n",
		    errbuf);
	}
	/* CLI_CONFIG_CANTOPEN = no config file, silently use defaults */
	cli_config_free_strings(&cfg);

	/* Init terminal (does NOT clear screen — VRAM artifact preserved) */
	term_init();

	/* Init bus */
	{
		port_result_t rc;

		rc = bus_init(&bus, static_ram, ram_size);
		if (rc != PORT_OK) {
			port_term_write_buf("bus_init failed\r\n", 17);
			wait_key_pressed();
			return (1);
		}
	}
	bus.log     = nspire_log;
	bus.log_ctx = NULL;
	g_bus = &bus;

	bus.opts.uncapped             = opt_uncapped;
	bus.opts.throttle_pia         = opt_throttle_pia;
	bus.opts.emulate_dram_refresh = opt_emulate_dram;
	bus.opts.emulate_kbd_bounce   = opt_emulate_bounce;
	bus.opts.randomize_cold_boot  = opt_randomize_cold;
	bus.opts.flat_bus             = opt_flat_bus;
	bus.opts.headless             = false;

	/* Cold-boot RAM randomisation */
	if (opt_randomize_cold) {
		for (k = 0; k < ram_size; k++)
			static_ram[k] = (uint8_t)(port_gettime_us() & 0xFF);
	} else {
		port_memset(static_ram, 0, ram_size);
	}

	/* Load ROM (NULL = use embedded WozMon) */
	{
		port_result_t rc;
		char *resolved;

		if (rom_path != NULL) {
			resolved = nspire_resolve_data_path(rom_path);
			if (resolved != NULL) {
				port_free(rom_path);
				rom_path = resolved;
			}
		}
		rc = bus_load_rom(&bus, rom_path);
		if (rc != PORT_OK) {
			nspire_fatal("bus_load_rom failed\r\n");
			bus_free(&bus);
			return (1);
		}
		if (rom_path != NULL) {
			port_free(rom_path);
			rom_path = NULL;
		}
	}

	if (bin_path != NULL) {
		port_result_t rc;
		char *resolved;
		char msg[96];

		resolved = nspire_resolve_data_path(bin_path);
		if (resolved != NULL) {
			port_free(bin_path);
			bin_path = resolved;
		}
		rc = bus_load_bin(&bus, bin_path, bin_address);
		if (rc != PORT_OK) {
			port_snprintf(msg,
			    sizeof(msg),
			    "LOAD FAIL\r\n%s\r\n",
			    bin_path);
			nspire_fatal(msg);
			bus_free(&bus);
			return (1);
		}
		if (bus_read(&bus, bin_address) == 0x00) {
			port_snprintf(msg,
			    sizeof(msg),
			    "LOAD EMPTY?\r\n%s\r\n",
			    bin_path);
			nspire_fatal(msg);
			bus_free(&bus);
			return (1);
		}
		port_free(bin_path);
		bin_path = NULL;
	}

	/* CPU init + immediate reset (no "wait for Ctrl+R" on Nspire) */
	cpu_init(&cpu, &bus);
	bus_reset(&bus);
	cpu_reset(&cpu);
	g_cpu = &cpu;

	io_reset();

	/* ---------------------------------------------------------------- */
	/* Emulator loop                                                     */
	/* ---------------------------------------------------------------- */

	cycle_accumulator = 0;
	last_time         = port_gettime_us();
	last_kbd_poll     = last_time;
	term_cycle_acc    = 0;

	for (;;) {
		uint32_t now_kbd;
		uint8_t cycles;

		now_kbd = port_gettime_us();

		/* Poll special keys and keyboard ~1 kHz */
		if (now_kbd - last_kbd_poll >= 1000UL) {
			last_kbd_poll = now_kbd;

			action = poll_special_keys();
			if (action == KEY_ACTION_QUIT) {
				wait_keys_released();
				break;
			}
			if (action == KEY_ACTION_RESET) {
				wait_keys_released();
				term_cold_boot();
				bus_reset(&bus);
				io_reset();
				cpu_reset(&cpu);
			}
			if (action == KEY_ACTION_CLEAR) {
				wait_keys_released();
				port_term_clear();
			}

			bus_update_keyboard(&bus);

			if (io_reset_pending()) {
				term_cold_boot();
				bus_reset(&bus);
				io_reset();
				cpu_reset(&cpu);
			}
		}

		/* Step CPU */
		cycles = cpu_step(&cpu);

		cycle_accumulator += (uint32_t)cycles;
		bus_tick_kbd_bounce(&bus, cycles);

		/* ~250 ms blink at 1.023 MHz */
		term_cycle_acc += (uint32_t)cycles;
		if (term_cycle_acc >= 256000UL) {
			term_cycle_acc = 0;
			term_update();
		}

		/* Speed throttle: cap at ~1.023 MHz unless opt_uncapped */
		if (opt_uncapped == false &&
		    cycle_accumulator >= (1022727UL / 1000UL)) {
			uint32_t now;
			uint32_t elapsed;

			now     = port_gettime_us();
			elapsed = now - last_time;
			if (elapsed < 1000UL)
				port_sleep_us(1000UL - elapsed);
			last_time         = port_gettime_us();
			cycle_accumulator = 0;
		}

		/*
		 * term_update() runs on a 250 ms timer above for blink; Wozmon
		 * output is redrawn from vram on each term_write() as well.
		 */
	}

	bus_free(&bus);
	io_cleanup();
	return (0);
}

/* Required stub when dbg.c is omitted or not linked */
#ifdef APPLE1_OMIT_DEBUGGER
void
dbg_log_append(const char *str)
{
	(void)str;
}
#endif
