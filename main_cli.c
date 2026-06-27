/*
 * main_cli.c - Portable CLI entry point for the Apple 1 emulator.
 *
 * Zero platform dependencies beyond C99 + port.h.  Compiles on any
 * hosted C99 toolchain (POSIX, Windows, Haiku, OS/2, RTEMS, …).
 *
 * Config is loaded from a file passed with -f <file>.  All other
 * settings come from command-line switches only.  No XDG/macOS
 * Library paths, no env-variable guessing.
 *
 * Single-file build example:
 *   cc -DAPPLE1_OMIT_DEBUGGER \
 *      bus.c cpu.c aci.c disasm.c io.c krusader.c \
 *      term_ansi.c main_cli.c -o apple1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aci.h"
#include "bus.h"
#include "cpu.h"
#include "dbg.h"
#include "disasm.h"
#include "io.h"
#include "krusader.h"
#include "port.h"
#include "term_apple1.h"

#ifdef APPLE1_OMIT_STDIO
#  define cli_error(...) ((void)0)
#  define cli_warn(...)  ((void)0)
#  define cli_printf(...) ((void)0)
#else
#  define cli_error(...)  (fprintf(stderr, __VA_ARGS__))
#  define cli_warn(...)   (fprintf(stderr, __VA_ARGS__))
#  define cli_printf(...) (printf(__VA_ARGS__))
#endif


#define CLOCK_RATE_HZ 1022727 /* Apple 1 clock: 1.022727 MHz */
#define CYCLES_PER_MS (CLOCK_RATE_HZ / 1000)

/* Globals read by dbg.c, term_ansi.c, and term_internal.h */
struct bus  *g_bus  = NULL;
struct cpu  *g_cpu  = NULL;
#ifndef APPLE1_OMIT_DEBUGGER
debugger_t  *g_dbg  = NULL;
#endif
char        *g_argv0 = NULL;
bool         g_debug_enabled = false;

static volatile sig_atomic_t g_debug_break = 0;

/* Referenced by term_ansi.c for baud-rate throttle */
extern uint32_t opt_baud;


/* ------------------------------------------------------------------ */
/*  Signal handler                                                      */
/* ------------------------------------------------------------------ */

static void
handle_signal(int sig)
{
	(void)sig;
	if (g_debug_enabled != 0) {
		g_debug_break = 1;
	} else {
		/* exit() triggers atexit handlers (terminal restore) */
		exit(0);
	}
}

/* ------------------------------------------------------------------ */
/*  stderr log callback                                                 */
/* ------------------------------------------------------------------ */

static void
stderr_log(void *ctx, int level, const char *msg)
{
#ifndef APPLE1_OMIT_STDIO
	const char *prefix;

	(void)ctx;
	prefix = (level == BUS_LOG_ERROR) ? "Error" :
	    (level == BUS_LOG_WARN)  ? "Warning" : "Info";
	cli_error( "%s: %s\n", prefix, msg);
#else
	(void)ctx;
	(void)level;
	(void)msg;
#endif
}

/* ------------------------------------------------------------------ */
/*  Usage                                                               */
/* ------------------------------------------------------------------ */

static void
print_usage(const char *prog)
{
#ifndef APPLE1_OMIT_STDIO
	cli_error(
	    "Apple 1 Emulator (portable CLI build)\n"
	    "Usage: %s [options] [flat_binary]\n"
	    "\n"
	    "Options:\n"
	    "  -f <file>            Load configuration from <file>\n"
	    "  -r <rom>             Path to 256-byte Woz Monitor ROM\n"
	    "  -m <kb>              RAM size in KB (4-64, default 8)\n"
	    "  -l <file>@<hex>      Load binary at hex address\n"
	    "  -c                   Cap emulation speed to 1.023 MHz\n"
	    "  -p                   Disable PIA I/O throttling\n"
	    "  -d                   Emulate DRAM refresh cycle stealing\n"
	    "  -b                   Emulate keyboard bounce\n"
	    "  -s                   Disable cold-boot RAM randomisation\n"
	    "  -F, --flat-bus       Map 0x0000-0xFFFF as plain RAM\n"
	    "  -H                   Headless mode (no terminal rendering)\n"
	    "  -g                   Enable debugger (pauses on start)\n"
	    "  -t                   Enable CPU trace to stdout\n"
	    "  -w <txt>             Load Woz Monitor text file\n"
	    "  -a <rom>             Load ACI cassette card ROM\n"
	    "  -e <wav>             Load WAV tape for ACI playback\n"
	    "  -E <wav>             Save recorded ACI tape to WAV on exit\n"
	    "  -B <baud>            Emulate terminal baud rate (e.g. 1200)\n"
	    "  -k <rom>             Load Krusader assembler ROM\n"
	    "  -h                   Show this help\n",
	    prog);
#else
	(void)prog;
#endif
}

/* ------------------------------------------------------------------ */
/*  Config file parser (same syntax as main.c)                         */
/* ------------------------------------------------------------------ */

static void
load_config_file(const char *path,
    char **rom_path,
    uint32_t *ram_size,
    char **bin_path,
    uint16_t *bin_address,
    bool *opt_uncapped,
    bool *opt_throttle_pia,
    bool *opt_emulate_dram,
    bool *opt_emulate_bounce,
    bool *opt_randomize_cold,
    bool *opt_flat_bus,
    bool *opt_headless,
    bool *opt_debug,
    bool *opt_trace,
    char **aci_path,
    char **tape_path,
    char **save_tape_path,
    char **krusader_path)
{
	FILE *fp;
	char line[1024];

	fp = fopen(path, "r");
	if (fp == NULL) {
		cli_error( "Warning: cannot open config '%s'\n", path);
		return;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *ptr;
		char *val;
		bool has_val;
		size_t len;
		char flag;

		len = strlen(line);
		while (len > 0 &&
		    (line[len - 1] == '\r' || line[len - 1] == '\n' ||
			line[len - 1] == ' '  || line[len - 1] == '\t')) {
			line[--len] = '\0';
		}
		ptr = line;
		while (*ptr == ' ' || *ptr == '\t') {
			ptr++;
		}
		if (*ptr == '#' || *ptr == '\0') {
			continue;
		}
		if (ptr[0] != '-' || ptr[1] == '\0') {
			continue;
		}
		flag = ptr[1];
		val  = ptr + 2;
		while (*val == ' ' || *val == '\t') {
			val++;
		}
		has_val = (*val != '\0');

		switch (flag) {
		case 'r':
			if (has_val != 0) {
				if (*rom_path != NULL)
					port_free(*rom_path);
				*rom_path = port_strdup(val);
			}
			break;
		case 'm':
			if (has_val != 0) {
				int kb = atoi(val);
				if (kb >= 4 && kb <= 64)
					*ram_size = (uint32_t)(kb * 1024);
			}
			break;
		case 'l':
			if (has_val != 0) {
				char *dup = port_strdup(val);
				char *at  = strchr(dup, '@');
				if (at != NULL) {
					*at = '\0';
					if (*bin_path != NULL)
						port_free(*bin_path);
					*bin_path    = port_strdup(dup);
					*bin_address = (uint16_t)strtol(at + 1,
					    NULL, 16);
				}
				port_free(dup);
			}
			break;
		case 'c':
			*opt_uncapped = false;
			break;
		case 'p':
			*opt_throttle_pia = false;
			break;
		case 'd':
			*opt_emulate_dram = true;
			break;
		case 'b':
			*opt_emulate_bounce = true;
			break;
		case 's':
			*opt_randomize_cold = false;
			break;
		case 'F':
			*opt_flat_bus = true;
			break;
		case 'H':
			*opt_headless = true;
			break;
		case 'g':
			*opt_debug = true;
			break;
		case 't':
			*opt_trace = true;
			break;
		case 'a':
			if (has_val != 0) {
				if (*aci_path != NULL)
					port_free(*aci_path);
				*aci_path = port_strdup(val);
			}
			break;
		case 'e':
			if (has_val != 0) {
				if (*tape_path != NULL)
					port_free(*tape_path);
				*tape_path = port_strdup(val);
			}
			break;
		case 'E':
			if (has_val != 0) {
				if (*save_tape_path != NULL)
					port_free(*save_tape_path);
				*save_tape_path = port_strdup(val);
			}
			break;
		case 'k':
			if (has_val != 0) {
				if (*krusader_path != NULL)
					port_free(*krusader_path);
				*krusader_path = port_strdup(val);
			}
			break;
		default:
			break;
		}
	}
	fclose(fp);
}

/* ------------------------------------------------------------------ */
/*  Card cleanup helper                                                 */
/* ------------------------------------------------------------------ */

static void
cleanup_cards(struct bus *bus, const char *save_tape_path)
{
	int i;

#ifdef APPLE1_OMIT_ACI
	(void)save_tape_path;
#endif

	for (i = 0; i < bus->num_cards; i++) {
#ifndef APPLE1_OMIT_ACI
		if (strcmp(bus->cards[i]->name, "ACI") == 0) {
			if (save_tape_path != NULL) {
				aci_save_tape(bus->cards[i], save_tape_path);
			}
			aci_free(bus->cards[i]);
		} else
#endif
#ifndef APPLE1_OMIT_KRUSADER
		if (strcmp(bus->cards[i]->name, "Krusader") == 0) {
			krusader_free(bus->cards[i]);
		}
#else
		(void)0;
#endif
	}
	bus->num_cards = 0;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
	uint8_t  static_ram[APPLE1_STATIC_RAM_SIZE];
	struct bus bus;
#ifndef APPLE1_OMIT_DEBUGGER
	debugger_t dbg;
#endif
	struct cpu cpu;
	char     trace_line[160];
	char     disasm_buf[64];
	char     hex_bytes[16];
	uint64_t last_time;
	struct expansion_card *aci_card;
	uint32_t cycle_accumulator;
	uint32_t k;
	uint32_t ram_size;
	uint16_t bin_address;
	uint16_t prev_pc;
	uint16_t run_addr;
	int j;
	int loop_count;
	int opt;
	bool opt_debug;
	bool opt_emulate_bounce;
	bool opt_emulate_dram;
	bool opt_flat_bus;
	bool opt_headless;
	bool opt_randomize_cold;
	bool opt_throttle_pia;
	bool opt_trace;
	bool opt_uncapped;
	bool has_run_addr;
	char *aci_path;
	char *bin_path;
	char *config_path;
	char *flat_bin_path;
	char *krusader_path;
	char *rom_path;
	char *save_tape_path;
	char *tape_path;
	char *wozmon_txt_path;

	g_argv0 = argv[0];

	/* Silence unused warnings when building with OMIT flags */
#ifdef APPLE1_OMIT_ACI
	(void)aci_card;
	(void)aci_path;
	(void)save_tape_path;
	(void)tape_path;
#endif
#ifdef APPLE1_OMIT_DISKIO
	(void)bin_path;
	(void)flat_bin_path;
	(void)run_addr;
	(void)has_run_addr;
	(void)wozmon_txt_path;
#endif
#ifdef APPLE1_OMIT_KRUSADER
	(void)krusader_path;
#endif
#ifdef APPLE1_OMIT_DISASM
	(void)trace_line;
	(void)disasm_buf;
	(void)hex_bytes;
#endif

	/* Defaults */
	aci_card       = NULL;
	aci_path       = NULL;
	bin_path       = NULL;
	config_path    = NULL;
	flat_bin_path  = NULL;
	krusader_path  = NULL;
	rom_path       = NULL;
	save_tape_path = NULL;
	tape_path      = NULL;
	wozmon_txt_path = NULL;
	bin_address    = 0;
	ram_size       = 8 * 1024; /* 8 KB default */
	opt_baud       = 0;
	opt_debug      = false;
	opt_emulate_bounce = false;
	opt_emulate_dram   = false;
	opt_flat_bus       = false;
	opt_headless       = false;
	opt_randomize_cold = true;
	opt_throttle_pia   = true;
	opt_trace          = false;
	opt_uncapped       = true;

	/*
	 * First pass: pick up -f <config> so it is loaded before other
	 * switches, which can then override individual settings.
	 */
	{
		int ai;
		for (ai = 1; ai < argc - 1; ai++) {
			if (strcmp(argv[ai], "-f") == 0) {
				config_path = argv[ai + 1];
				break;
			}
		}
	}

	if (config_path != NULL) {
		load_config_file(config_path,
		    &rom_path, &ram_size, &bin_path, &bin_address,
		    &opt_uncapped, &opt_throttle_pia,
		    &opt_emulate_dram, &opt_emulate_bounce,
		    &opt_randomize_cold, &opt_flat_bus,
		    &opt_headless, &opt_debug, &opt_trace,
		    &aci_path, &tape_path, &save_tape_path,
		    &krusader_path);
	}

	/* Second pass: command-line overrides */
	while ((opt = port_getopt(argc, argv,
	    "f:r:m:l:w:a:e:E:B:k:cFpdbsHgth")) != -1) {
		switch (opt) {
		case 'f':
			/* Already handled */
			break;
		case 'r':
			if (rom_path != NULL)
				port_free(rom_path);
			rom_path = port_strdup(port_optarg);
			break;
		case 'm': {
			int kb = atoi(port_optarg);
			if (kb >= 4 && kb <= 64)
				ram_size = (uint32_t)(kb * 1024);
			break;
		}
		case 'l': {
			char *dup = port_strdup(port_optarg);
			char *at  = strchr(dup, '@');
			if (at != NULL) {
				*at = '\0';
				if (bin_path != NULL)
					port_free(bin_path);
				bin_path    = port_strdup(dup);
				bin_address = (uint16_t)strtol(at + 1,
				    NULL, 16);
			}
			port_free(dup);
			break;
		}
		case 'w':
			wozmon_txt_path = port_strdup(port_optarg);
			break;
		case 'a':
			if (aci_path != NULL)
				port_free(aci_path);
			aci_path = port_strdup(port_optarg);
			break;
		case 'e':
			if (tape_path != NULL)
				port_free(tape_path);
			tape_path = port_strdup(port_optarg);
			break;
		case 'E':
			if (save_tape_path != NULL)
				port_free(save_tape_path);
			save_tape_path = port_strdup(port_optarg);
			break;
		case 'B': {
			int baud = atoi(port_optarg);
			if (baud > 0)
				opt_baud = (uint32_t)baud;
			break;
		}
		case 'k':
			if (krusader_path != NULL)
				port_free(krusader_path);
			krusader_path = port_strdup(port_optarg);
			break;
		case 'c':
			opt_uncapped = false;
			break;
		case 'F':
			opt_flat_bus = true;
			break;
		case 'p':
			opt_throttle_pia = false;
			break;
		case 'd':
			opt_emulate_dram = true;
			break;
		case 'b':
			opt_emulate_bounce = true;
			break;
		case 's':
			opt_randomize_cold = false;
			break;
		case 'H':
			opt_headless = true;
			break;
		case 'g':
			opt_debug = true;
			break;
		case 't':
			opt_trace = true;
			break;
		case 'h':
			print_usage(argv[0]);
			return (0);
		default:
			print_usage(argv[0]);
			return (1);
		}
	}

	/* Positional arg = flat binary */
	if (port_optind < argc) {
		flat_bin_path = argv[port_optind];
		opt_flat_bus  = true;
	}

	/* Validate RAM size */
	if (opt_flat_bus != 0) {
		if (APPLE1_STATIC_RAM_SIZE < 65536) {
			cli_error(
			    "Error: Flat bus option requires 64 KB of RAM, but "
			    "emulator was compiled with APPLE1_STATIC_RAM_SIZE = %u KB.\n",
			    APPLE1_STATIC_RAM_SIZE / 1024);
			return (1);
		}
		ram_size = 65536;
	}
	if (ram_size == 0) {
		ram_size = APPLE1_DEFAULT_RAM_KB * 1024;
	}
	if (ram_size > APPLE1_STATIC_RAM_SIZE) {
		cli_error(
		    "Error: Requested RAM size (%u KB) exceeds maximum compiled static RAM size (%u KB).\n",
		    ram_size / 1024, APPLE1_STATIC_RAM_SIZE / 1024);
		return (1);
	}

	/* Install signal handler */
#ifdef SIGINT
	signal(SIGINT,  handle_signal);
#endif
#ifdef SIGTERM
	signal(SIGTERM, handle_signal);
#endif

	/* Initialise bus with static buffer — no malloc required */
	if (bus_init(&bus, static_ram, ram_size) == false) {
		cli_error( "Error: bus_init failed\n");
		return (1);
	}
	bus.log     = stderr_log;
	bus.log_ctx = NULL;

	g_bus = &bus;

	/* Apply options */
	bus.opts.uncapped           = opt_uncapped;
	bus.opts.throttle_pia       = opt_throttle_pia;
	bus.opts.emulate_dram_refresh = opt_emulate_dram;
	bus.opts.emulate_kbd_bounce = opt_emulate_bounce;
	bus.opts.randomize_cold_boot = opt_randomize_cold;
	bus.opts.flat_bus           = opt_flat_bus;
	bus.opts.headless           = opt_headless;

	/* Cold-boot RAM randomisation */
	if (bus.opts.randomize_cold_boot != 0) {
		for (k = 0; k < ram_size; k++) {
			static_ram[k] = (uint8_t)(port_gettime_ns() & 0xFF);
		}
	} else {
		memset(static_ram, 0, ram_size);
	}

	/* Load ROM */
	if (bus_load_rom(&bus, rom_path) == false) {
		bus_free(&bus);
		return (1);
	}

	/* Load flat binary */
#ifndef APPLE1_OMIT_DISKIO
	if (flat_bin_path != NULL) {
		if (bus_load_bin(&bus, flat_bin_path, 0x0000) == false) {
			bus_free(&bus);
			return (1);
		}
		/* Overwrite Reset Vector to point to $0400 */
		bus.ram[RESET_VECTOR] = 0x00;
		bus.ram[RESET_VECTOR + 1] = 0x04;
	}
#endif

	/* Load -l binary */
#ifndef APPLE1_OMIT_DISKIO
	if (bin_path != NULL) {
		if (bus_load_bin(&bus, bin_path, bin_address) == false) {
			bus_free(&bus);
			return (1);
		}
		port_free(bin_path);
		bin_path = NULL;
	}
#endif

	/* Load Woz Monitor text file */
#ifndef APPLE1_OMIT_DISKIO
	if (wozmon_txt_path != NULL) {
		has_run_addr = false;
		run_addr     = 0;
		if (bus_load_wozmon_txt(&bus, wozmon_txt_path,
		    &run_addr, &has_run_addr) == false) {
			bus_free(&bus);
			return (1);
		}
		if (has_run_addr != 0) {
			bus.ram[0xFFFC] = (uint8_t)(run_addr & 0xFF);
			bus.ram[0xFFFD] = (uint8_t)(run_addr >> 8);
		}
		port_free(wozmon_txt_path);
		wozmon_txt_path = NULL;
	}
#endif

	/* ACI expansion card */
#ifndef APPLE1_OMIT_ACI
	if (aci_path != NULL) {
		aci_card = aci_create(&bus, aci_path);
		if (aci_card != NULL) {
			if (tape_path != NULL) {
				if (aci_load_tape(aci_card, tape_path) == false) {
					aci_free(aci_card);
					bus_free(&bus);
					return (1);
				}
			}
			bus_add_card(&bus, aci_card);
		} else if (tape_path != NULL || save_tape_path != NULL) {
			cli_error(
			    "Error: ACI tape operations requested but ACI "
			    "card failed to load.\n");
			bus_free(&bus);
			return (1);
		}
	}
#endif /* APPLE1_OMIT_ACI */

	/* Krusader */
#ifndef APPLE1_OMIT_KRUSADER
	if (krusader_path != NULL) {
		struct expansion_card *kcard;
		kcard = krusader_create(&bus, krusader_path);
		if (kcard != NULL) {
			bus_add_card(&bus, kcard);
		}
		port_free(krusader_path);
		krusader_path = NULL;
	}
#endif /* APPLE1_OMIT_KRUSADER */

	/* Debugger init */
	cpu_init(&cpu, &bus);
	cpu_reset(&cpu);
#ifndef APPLE1_OMIT_DEBUGGER
	dbg_init(&dbg, &cpu);
	g_dbg = &dbg;
#endif
	g_cpu = &cpu;
	g_debug_enabled = opt_debug;

#ifndef APPLE1_OMIT_DEBUGGER
	if (opt_debug != 0) {
		dbg.step_mode = true;
	}
#endif

	/* IO / terminal init */
	if (opt_headless == false) {
		io_init();
	}

	cycle_accumulator = 0;
	last_time  = port_gettime_ns();
	prev_pc    = 0xFFFF;
	loop_count = 0;

	for (;;) {

		/* When paused / powered off: just sleep */
		if (opt_headless == false &&
		    term_is_powered() == false) {
			port_sleep_ns(10000000ULL); /* 10ms */
			continue;
		}

		/* Keyboard + reset */
		if (opt_headless == false) {
			bus_update_keyboard(&bus);
			if (io_reset_pending() != 0) {
				bus_reset(&bus);
				io_reset();
				cpu_reset(&cpu);
			}
		}

		/* Debugger breakpoint / step */
#ifndef APPLE1_OMIT_DEBUGGER
		if (g_debug_enabled != 0) {
			if (g_debug_break != 0 || dbg.step_mode != 0 ||
			    dbg_has_breakpoint(&dbg, cpu.pc) != 0) {
				g_debug_break = 0;
				dbg_interactive_loop(&dbg);
			}
		}
#endif

		/* Optional CPU trace */
#ifndef APPLE1_OMIT_DISASM
		if (opt_trace != 0) {
			uint8_t op;
			int len;

			op  = bus_read(&bus, cpu.pc);
			len = cpu_disassemble(&bus, cpu.pc, disasm_buf);

			if (len == 1) {
				snprintf(hex_bytes, sizeof(hex_bytes),
				    "%02X      ", op);
			} else if (len == 2) {
				snprintf(hex_bytes, sizeof(hex_bytes),
				    "%02X %02X   ",
				    op, bus_read(&bus, cpu.pc + 1));
			} else {
				snprintf(hex_bytes, sizeof(hex_bytes),
				    "%02X %02X %02X",
				    op, bus_read(&bus, cpu.pc + 1),
				    bus_read(&bus, cpu.pc + 2));
			}
			snprintf(trace_line, sizeof(trace_line),
			    "$%04X  %s  %-20s A:%02X X:%02X Y:%02X "
			    "SP:%02X P:%02X",
			    cpu.pc, hex_bytes, disasm_buf,
			    cpu.a, cpu.x, cpu.y, cpu.s, cpu.p);
			cli_printf("%s\n", trace_line);
		}
#endif

		/* Step CPU */
#ifndef APPLE1_OMIT_DEBUGGER
		bus.access_cb     = dbg_check_access;
		bus.access_cb_ctx = &dbg;
		dbg.current_instruction_pc = cpu.pc;
#endif
		{
			uint8_t cycles = cpu_step(&cpu);

			bus.access_cb = NULL;
			cycle_accumulator += cycles;

			/* Tick expansion cards */
			for (j = 0; j < bus.num_cards; j++) {
				if (bus.cards[j]->tick != NULL) {
					bus.cards[j]->tick(
					    bus.cards[j]->ctx, cycles);
				}
			}
		}

		/* Loop detection (flat-bus headless mode) */
		if (bus.opts.flat_bus != 0 && bus.opts.headless != 0) {
			if (cpu.pc == prev_pc) {
				loop_count++;
				if (loop_count >= 3) {
					if (cpu.pc == 0x3469) {
						cli_printf("Klaus Dormann "
						       "functional test: "
						       "PASS\n");
					} else {
						cli_error(
						    "FAIL: cpu trapped at "
						    "$%04X\n",
						    cpu.pc);
						cleanup_cards(&bus,
						    save_tape_path);
						bus_free(&bus);
						if (opt_headless == false)
							io_cleanup();
						return (1);
					}
					cleanup_cards(&bus, save_tape_path);
					bus_free(&bus);
					if (opt_headless == false)
						io_cleanup();
					return (0);
				}
			} else {
				loop_count = 0;
			}
			prev_pc = cpu.pc;
		}

		/* Speed throttle (capped mode) */
		if (opt_uncapped == false && opt_headless == false &&
		    cycle_accumulator >= CYCLES_PER_MS) {
			uint64_t current_time;
			uint64_t elapsed_ns;
			uint64_t expected_ns;

			current_time = port_gettime_ns();
			elapsed_ns   = current_time - last_time;
			expected_ns  = 1000000ULL; /* 1 ms */

			if (elapsed_ns < expected_ns) {
				port_sleep_ns(expected_ns - elapsed_ns);
			}
			last_time         = port_gettime_ns();
			cycle_accumulator = 0;
		}

		/* Render terminal */
		if (opt_headless == false && opt_uncapped == false) {
			term_update();
		}
	}

	cleanup_cards(&bus, save_tape_path);
	bus_free(&bus);
	if (opt_headless == false) {
		io_cleanup();
	}
	return (0);
}

/* dbg_log_append stub — only the GUI needs the ring buffer */
void
dbg_log_append(const char *str)
{
	(void)str;
}
