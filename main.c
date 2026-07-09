/*
 * main_cli.c - Portable CLI entry point for the Apple 1 emulator.
 *
 * Zero platform dependencies beyond C89 + port.h.  Compiles on any
 * hosted C89-or-later toolchain (POSIX, Windows, Haiku, OS/2, RTEMS, …).
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

#include "aci.h"
#include "bus.h"
#include "cli_config.h"
#include "cpu.h"
#include "dbg.h"
#include "disasm.h"
#include "io.h"
#include "krusader.h"
#include "port.h"
#include "term_apple1.h"

static void
cli_error(const char *fmt, ...)
{
	char buf[2048];
	va_list args;
	va_start(args, fmt);
	port_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	port_term_write_buf(buf, port_strlen(buf));
}

static void PORT_UNUSED
cli_warn(const char *fmt, ...)
{
	char buf[2048];
	va_list args;
	va_start(args, fmt);
	port_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	port_term_write_buf(buf, port_strlen(buf));
}

static void PORT_UNUSED
cli_printf(const char *fmt, ...)
{
	char buf[2048];
	va_list args;
	va_start(args, fmt);
	port_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	port_term_write_buf(buf, port_strlen(buf));
}

#define CLOCK_RATE_HZ 1022727 /* Apple 1 clock: 1.022727 MHz */
#define CYCLES_PER_MS (CLOCK_RATE_HZ / 1000)
#define KBD_POLL_US   1000UL /* stdin poll rate in interactive mode */

/* Globals read by dbg.c, term_ansi.c, and term_internal.h */
struct bus *g_bus = NULL;
struct cpu *g_cpu = NULL;
#ifndef APPLE1_OMIT_DEBUGGER
debugger_t *g_dbg = NULL;
#endif
char *g_argv0 = NULL;
bool g_debug_enabled = false;

/* Shared global authentic speed setting */
uint32_t opt_baud = 0;

/* ------------------------------------------------------------------ */
/*  Log file globals                                                     */
/* ------------------------------------------------------------------ */

static port_file_t g_log_file = NULL;
static int g_log_level = 1; /* 0=errors, 1=warn+errors, 2=info+all */

/* ------------------------------------------------------------------ */
/*  Signal handler                                                      */
/* ------------------------------------------------------------------ */

port_sig_flag g_quit_flag = 0;

/* Ctrl-C / SIGINT: request emulator exit (no debugger path). */
static int
process_sigint(void)
{
	if (g_quit_flag == 0) {
		return (0);
	}
	g_quit_flag = 0;
	return (1);
}

#ifndef APPLE1_OMIT_DEBUGGER
/*
 * Ctrl-C while the debugger is enabled: break back into the debugger
 * rather than exiting.  Sets step_mode and returns 0 so the main loop
 * continues and hits the debugger check on the next iteration.
 * Returns 1 (exit) only when g_debug_enabled is false.
 */
static int
process_sigint_dbg(debugger_t *dbg)
{
	if (g_quit_flag == 0) {
		return (0);
	}
	g_quit_flag = 0;
	if (g_debug_enabled != 0) {
		dbg->step_mode = true;
		return (0);
	}
	return (1);
}
#endif

/* ------------------------------------------------------------------ */
/*  Log callbacks                                                        */
/* ------------------------------------------------------------------ */

/*
 * file_log -- write a timestamped entry to the open log file.
 * Called via bus.log; silently skipped when g_log_file is NULL
 * or the message level is below g_log_level.
 */
static void
file_log(void *ctx, int level, const char *msg)
{
	char buf[2048];
	port_size_t n;
	const char *prefix;

	(void)ctx;
	if (g_log_file == NULL) {
		return;
	}
	if (level > g_log_level) {
		return;
	}
	prefix = (level == BUS_LOG_ERROR) ? "ERROR"
	    : (level == BUS_LOG_WARN)	  ? "WARN"
					  : "INFO";
	port_snprintf(buf, sizeof(buf), "[%s] %s\n", prefix, msg);
	n = port_strlen(buf);
	g_port_vfs->write(g_log_file, buf, n, NULL);
}

/*
 * stderr_log -- fallback used when no log file is open.
 * Errors and warnings only (respects g_log_level).
 */
static void
stderr_log(void *ctx, int level, const char *msg)
{
#ifndef APPLE1_OMIT_STDIO
	const char *prefix;

	(void)ctx;
	if (level > g_log_level) {
		return;
	}
	prefix = (level == BUS_LOG_ERROR) ? "Error"
	    : (level == BUS_LOG_WARN)	  ? "Warning"
					  : "Info";
	cli_error("%s: %s\n", prefix, msg);
#else
	(void)ctx;
	(void)level;
	(void)msg;
#endif
}

/* Emit an INFO message through the bus log (no-op if level < 2). */
static void
log_info(struct bus *bus, const char *msg)
{
	BUS_LOG(bus, BUS_LOG_INFO, msg);
}

/* ------------------------------------------------------------------ */
/*  Usage                                                               */
/* ------------------------------------------------------------------ */

static void
print_usage(const char *prog)
{
#ifndef APPLE1_OMIT_STDIO
	cli_error("Apple 1 Emulator (portable CLI build)\n"
		  "Usage: %s [options] [flat_binary]\n"
		  "       %s [-H] [-g] config.conf\n"
		  "\n"
		  "Config file mode: pass a .conf file (see usage.md).  Only "
		  "-H and -g may accompany it.\n"
		  "Switch mode: use options below without a .conf file.\n"
		  "\n"
		  "Options:\n"
		  "  -r <rom>             Path to 256-byte Woz Monitor ROM\n"
		  "  -m <kb>              RAM size in KB (4-64, default 8)\n"
		  "  -l <file>@<hex>      Load binary at hex address\n"
		  "  -c                   Cap emulation speed to 1.023 MHz\n"
		  "  -u                   Run uncapped (as fast as host "
		  "allows)\n"
		  "  -p                   Disable PIA I/O throttling\n"
		  "  -d                   Emulate DRAM refresh cycle stealing\n"
		  "  -b                   Emulate keyboard bounce\n"
		  "  -s                   Disable cold-boot RAM randomisation\n"
		  "  -F, --flat-bus       Map 0x0000-0xFFFF as plain RAM\n"
		  "  -H                   Headless mode (no terminal "
		  "rendering)\n"
		  "  -g                   Enable debugger (pauses on start)\n",
	    prog,
	    prog);
	cli_error("  -t                   Enable CPU trace to stdout\n"
		  "  -w <txt>             Load Woz Monitor text file\n"
		  "  -a <rom>             Load ACI cassette card ROM\n"
		  "  -e <wav>             Load WAV tape for ACI playback\n"
		  "  -E <wav>             Save recorded ACI tape to WAV on "
		  "exit\n"
		  "  -B <baud>            Emulate terminal baud rate (e.g. "
		  "1200)\n"
		  "  -k <rom>             Load Krusader assembler ROM\n"
		  "  -L <file>            Write log to file\n"
		  "  -V <0|1|2>           Log verbosity: 0=errors, 1=warn "
		  "(default), 2=info\n"
		  "  -h                   Show this help\n");
#else
	(void)prog;
#endif
}

/* ------------------------------------------------------------------ */
/*  CLI / config file mode                                              */
/* ------------------------------------------------------------------ */

static int
cli_arg_is_runtime_only(const char *arg)
{
	if (arg == NULL || arg[0] != '-') {
		return (0);
	}
	if (port_strcmp(arg, "-H") == 0) {
		return (1);
	}
	if (port_strcmp(arg, "-g") == 0) {
		return (1);
	}
	if (port_strcmp(arg, "-h") == 0) {
		return (1);
	}
	return (0);
}

static int
apply_loaded_config(struct cli_config_opts *cfg,
    char **rom_path,
    uint32_t *ram_size,
    char **bin_path,
    uint16_t *bin_address,
    char **wozmon_txt_path,
    char **flat_bin_path,
    bool *opt_uncapped,
    bool *opt_throttle_pia,
    bool *opt_emulate_dram,
    bool *opt_emulate_bounce,
    bool *opt_randomize_cold,
    bool *opt_flat_bus,
    bool *opt_trace,
    char **aci_path,
    char **tape_path,
    char **save_tape_path,
    char **krusader_path,
    uint32_t *baud,
    char **log_path,
    int *log_level)
{
	*rom_path = cfg->rom_path;
	cfg->rom_path = NULL;
	*ram_size = cfg->ram_size;
	*bin_path = cfg->bin_path;
	cfg->bin_path = NULL;
	*bin_address = cfg->bin_address;
	*wozmon_txt_path = cfg->wozmon_txt_path;
	cfg->wozmon_txt_path = NULL;
	*flat_bin_path = cfg->flat_bin_path;
	cfg->flat_bin_path = NULL;
	*aci_path = cfg->aci_path;
	cfg->aci_path = NULL;
	*tape_path = cfg->tape_path;
	cfg->tape_path = NULL;
	*save_tape_path = cfg->save_tape_path;
	cfg->save_tape_path = NULL;
	*krusader_path = cfg->krusader_path;
	cfg->krusader_path = NULL;
	*baud = cfg->baud;
	*opt_uncapped = cfg->opt_uncapped;
	*opt_throttle_pia = cfg->opt_throttle_pia;
	*opt_emulate_dram = cfg->opt_emulate_dram;
	*opt_emulate_bounce = cfg->opt_emulate_bounce;
	*opt_randomize_cold = cfg->opt_randomize_cold;
	*opt_flat_bus = cfg->opt_flat_bus;
	*opt_trace = cfg->opt_trace;
	*log_path = cfg->log_path;
	cfg->log_path = NULL;
	*log_level = cfg->log_level;
	return (0);
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
		if (port_strcmp(bus->cards[i]->name, "ACI") == 0) {
			if (save_tape_path != NULL) {
				port_result_t rc;
				rc = aci_save_tape(bus->cards[i],
				    save_tape_path);
				if (rc != PORT_OK) {
					cli_error("Error: aci_save_tape "
						  "failed: %s\n",
					    port_error_string(rc));
				}
			}
			aci_free(bus->cards[i]);
			bus->cards[i] = NULL;
		}
#endif
#ifndef APPLE1_OMIT_KRUSADER
		if (bus->cards[i] != NULL &&
		    port_strcmp(bus->cards[i]->name, "Krusader") == 0) {
			krusader_free(bus->cards[i]);
			bus->cards[i] = NULL;
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

#ifdef FREERTOS_DEMO
int
emulator_main(int argc, char *argv[])
#else
int
main(int argc, char *argv[])
#endif
{
	/* Static RAM buffer — must not live on the stack (DOS extenders use ~8 KB). */
	static uint8_t static_ram[APPLE1_STATIC_RAM_SIZE];
	struct bus bus;
#ifndef APPLE1_OMIT_DEBUGGER
	debugger_t dbg;
#endif
	struct cpu cpu;
	char trace_line[160];
	char disasm_buf[64];
	char hex_bytes[16];
	uint32_t last_kbd_poll;
	uint32_t last_render;
	uint32_t last_time;
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
	bool opt_set_capped;
	bool opt_set_uncapped;
	bool has_run_addr;
	char *aci_path;
	char *bin_path;
	char *config_path;
	char *flat_bin_path;
	char *krusader_path;
	char *log_path;
	char *rom_path;
	char *save_tape_path;
	char *tape_path;
	char *wozmon_txt_path;
	int  log_level;

	/* Initialize all path pointers to NULL */
	aci_path = NULL;
	bin_path = NULL;
	config_path = NULL;
	flat_bin_path = NULL;
	krusader_path = NULL;
	log_path = NULL;
	rom_path = NULL;
	save_tape_path = NULL;
	tape_path = NULL;
	wozmon_txt_path = NULL;

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
	aci_card = NULL;
	aci_path = NULL;
	bin_path = NULL;
	config_path = NULL;
	flat_bin_path = NULL;
	krusader_path = NULL;
	log_path = NULL;
	rom_path = NULL;
	save_tape_path = NULL;
	tape_path = NULL;
	wozmon_txt_path = NULL;
	bin_address = 0;
	ram_size = 8 * 1024; /* 8 KB default */
	opt_debug = false;
	opt_emulate_bounce = false;
	opt_emulate_dram = false;
	opt_flat_bus = false;
	opt_headless = false;
	opt_randomize_cold = true;
	opt_throttle_pia = true;
	opt_trace = false;
	opt_uncapped = false;
	opt_set_capped = false;
	opt_set_uncapped = false;
	log_level = 1; /* warn+errors by default */

	/* Scan for .conf path, runtime flags (-H/-g/-h), and config mode. */
	{
		int ai;

		for (ai = 1; ai < argc; ai++) {
			if (port_strcmp(argv[ai], "-h") == 0) {
				print_usage(argv[0]);
				return (0);
			}
			if (port_strcmp(argv[ai], "-H") == 0) {
				opt_headless = true;
			} else if (port_strcmp(argv[ai], "-g") == 0) {
				opt_debug = true;
			} else if (argv[ai][0] == '-') {
				continue;
			} else if (cli_path_is_config_file(argv[ai]) != 0) {
				if (config_path != NULL) {
					cli_error("Error: multiple config "
						  "files specified\n");
					return (1);
				}
				config_path = argv[ai];
			}
		}
	}

	if (config_path != NULL) {
		struct cli_config_opts cfg;
		char errbuf[256];
		int cfg_rc;
		int ai;

		for (ai = 1; ai < argc; ai++) {
			if (argv[ai][0] == '-' &&
			    cli_arg_is_runtime_only(argv[ai]) == 0) {
				cli_error("Error: option '%s' cannot be used "
					  "with a config file (only -H and -g "
					  "are allowed)\n",
				    argv[ai]);
				return (1);
			}
		}

		cli_config_init_defaults(&cfg);
		cfg_rc =
		    load_config_file(config_path, &cfg, errbuf, sizeof(errbuf));
		if (cfg_rc == CLI_CONFIG_PARSE) {
			cli_error("Error: config '%s': %s\n",
			    config_path,
			    errbuf);
			cli_config_free_strings(&cfg);
			return (1);
		}
		if (cfg_rc == CLI_CONFIG_CANTOPEN) {
			cli_config_free_strings(&cfg);
			return (1);
		}
		apply_loaded_config(&cfg,
		    &rom_path,
		    &ram_size,
		    &bin_path,
		    &bin_address,
		    &wozmon_txt_path,
		    &flat_bin_path,
		    &opt_uncapped,
		    &opt_throttle_pia,
		    &opt_emulate_dram,
		    &opt_emulate_bounce,
		    &opt_randomize_cold,
		    &opt_flat_bus,
		    &opt_trace,
		    &aci_path,
		    &tape_path,
		    &save_tape_path,
		    &krusader_path,
		    &opt_baud,
		    &log_path,
		    &log_level);
		cli_config_free_strings(&cfg);
	} else {
		while ((opt = port_getopt(argc,
			    argv,
			    "r:m:l:w:a:e:E:B:k:L:V:cFpdbsHgthu")) != -1) {
			switch (opt) {
			case 'r':
				if (rom_path != NULL) {
					port_free(rom_path);
				}
				rom_path = port_strdup(port_optarg);
				break;
			case 'm': {
				int kb;

				kb = (int)port_strtoul(port_optarg,
				    (void *)0,
				    10);
				if (kb >= 4 && kb <= 64) {
					ram_size = (uint32_t)(kb * 1024);
				}
				break;
			}
			case 'l': {
				char *dup;
				char *at;

				dup = port_strdup(port_optarg);
				at = port_strchr(dup, '@');
				if (at != NULL) {
					*at = '\0';
					if (bin_path != NULL) {
						port_free(bin_path);
					}
					bin_path = port_strdup(dup);
					bin_address = (uint16_t)
					    port_strtol(at + 1, NULL, 16);
				}
				port_free(dup);
				break;
			}
			case 'w':
				wozmon_txt_path = port_strdup(port_optarg);
				break;
			case 'a':
				if (aci_path != NULL) {
					port_free(aci_path);
				}
				aci_path = port_strdup(port_optarg);
				break;
			case 'e':
				if (tape_path != NULL) {
					port_free(tape_path);
				}
				tape_path = port_strdup(port_optarg);
				break;
			case 'E':
				if (save_tape_path != NULL) {
					port_free(save_tape_path);
				}
				save_tape_path = port_strdup(port_optarg);
				break;
			case 'B': {
				int baud;

				baud = (int)port_strtoul(port_optarg,
				    (void *)0,
				    10);
				if (baud > 0) {
					opt_baud = (uint32_t)baud;
				}
				break;
			}
			case 'k':
				if (krusader_path != NULL) {
					port_free(krusader_path);
				}
				krusader_path = port_strdup(port_optarg);
				break;
			case 'c':
				opt_set_capped = true;
				opt_uncapped = false;
				break;
			case 'u':
				opt_set_uncapped = true;
				opt_uncapped = true;
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
			case 'L':
				if (log_path != NULL) {
					port_free(log_path);
				}
				log_path = port_strdup(port_optarg);
				break;
			case 'V': {
				int lv;

				lv = (int)port_strtoul(port_optarg,
				    NULL, 10);
				if (lv >= 0 && lv <= 2) {
					log_level = lv;
				}
				break;
			}
			case 'h':
				print_usage(argv[0]);
				return (0);
			default:
				print_usage(argv[0]);
				return (1);
			}
		}

		if (opt_set_capped != 0 && opt_set_uncapped != 0) {
			cli_error("Error: -c and -u are mutually exclusive\n");
			return (1);
		}

		/* Positional arg = flat binary (switch mode only) */
		if (port_optind < argc) {
			flat_bin_path = argv[port_optind];
			opt_flat_bus = true;
		}
	}

	/* Validate RAM size */
	if (opt_flat_bus != 0) {
		if (APPLE1_STATIC_RAM_SIZE < 65536) {
			cli_error("Error: Flat bus option requires 64 KB of "
				  "RAM, but "
				  "emulator was compiled with "
				  "APPLE1_STATIC_RAM_SIZE = %u KB.\n",
			    APPLE1_STATIC_RAM_SIZE / 1024);
			return (1);
		}
		ram_size = 65536;
	}
	if (ram_size == 0) {
		ram_size = APPLE1_DEFAULT_RAM_KB * 1024;
	}
	if (ram_size > APPLE1_STATIC_RAM_SIZE) {
		cli_error("Error: Requested RAM size (%u KB) exceeds maximum "
			  "compiled static RAM size (%u KB).\n",
		    ram_size / 1024,
		    APPLE1_STATIC_RAM_SIZE / 1024);
		return (1);
	}

	/* Open log file (if requested) before bus init so all messages land */
	g_log_level = log_level;
	if (log_path != NULL) {
		g_log_file = g_port_vfs->open(log_path, PORT_VFS_WRITE);
		if (g_log_file == PORT_FILE_INVALID) {
			cli_error("Warning: cannot open log file '%s'\n",
			    log_path);
			g_log_file = NULL;
		}
	}

	/* Install signal handler */
	port_signal_setup(&g_quit_flag);

	/* Initialise bus with static buffer — no malloc required */
	{
		port_result_t rc;
		rc = bus_init(&bus, static_ram, ram_size);
		if (rc != PORT_OK) {
			cli_error("Error: bus_init failed: %s\n",
			    port_error_string(rc));
			return (1);
		}
	}
	bus.log = (g_log_file != NULL) ? file_log : stderr_log;
	bus.log_ctx = NULL;

	/* Emit startup config summary (INFO; only visible at log_level 2) */
	{
		char msg[256];

		port_snprintf(msg, sizeof(msg),
		    "apple1-emu starting: RAM=%uKB speed=%s baud=%u",
		    (unsigned)(ram_size / 1024),
		    opt_uncapped != 0 ? "uncapped" : "capped",
		    (unsigned)opt_baud);
		log_info(&bus, msg);
	}

	g_bus = &bus;

	/* Apply options */
	bus.opts.uncapped = opt_uncapped;
	bus.opts.throttle_pia = opt_throttle_pia;
	bus.opts.emulate_dram_refresh = opt_emulate_dram;
	bus.opts.emulate_kbd_bounce = opt_emulate_bounce;
	bus.opts.randomize_cold_boot = opt_randomize_cold;
	bus.opts.flat_bus = opt_flat_bus;
	bus.opts.headless = opt_headless;

	/* Cold-boot RAM randomisation */
	if (bus.opts.randomize_cold_boot != 0) {
		for (k = 0; k < ram_size; k++) {
			static_ram[k] = (uint8_t)(port_gettime_us() & 0xFF);
		}
	} else {
		port_memset(static_ram, 0, ram_size);
	}

	/* Load ROM */
	{
		port_result_t rc;
		rc = bus_load_rom(&bus, rom_path);
		if (rc != PORT_OK) {
			cli_error("Error: bus_load_rom failed: %s\n",
			    port_error_string(rc));
			bus_free(&bus);
			return (1);
		}
		if (rom_path != NULL) {
			char msg[512];

			port_snprintf(msg, sizeof(msg),
			    "ROM loaded: %s", rom_path);
			log_info(&bus, msg);
		} else {
			log_info(&bus, "ROM: using embedded Woz Monitor");
		}
	}

	/* Load flat binary */
#ifndef APPLE1_OMIT_DISKIO
	if (flat_bin_path != NULL) {
		port_result_t rc;
		rc = bus_load_bin(&bus, flat_bin_path, 0x0000);
		if (rc != PORT_OK) {
			cli_error("Error: bus_load_bin failed: %s\n",
			    port_error_string(rc));
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
		port_result_t rc;
		rc = bus_load_bin(&bus, bin_path, bin_address);
		if (rc != PORT_OK) {
			cli_error("Error: bus_load_bin failed: %s\n",
			    port_error_string(rc));
			bus_free(&bus);
			return (1);
		}
		{
			char msg[512];

			port_snprintf(msg, sizeof(msg),
			    "Binary loaded: %s at $%04X",
			    bin_path, (unsigned)bin_address);
			log_info(&bus, msg);
		}
		port_free(bin_path);
		bin_path = NULL;
	}
#endif

	/* Load Woz Monitor text file */
#ifndef APPLE1_OMIT_DISKIO
	if (wozmon_txt_path != NULL) {
		port_result_t rc;
		has_run_addr = false;
		run_addr = 0;
		rc = bus_load_wozmon_txt(&bus,
		    wozmon_txt_path,
		    &run_addr,
		    &has_run_addr);
		if (rc != PORT_OK) {
			cli_error("Error: bus_load_wozmon_txt failed: %s\n",
			    port_error_string(rc));
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
			{
				char msg[512];

				port_snprintf(msg, sizeof(msg),
				    "ACI ROM loaded: %s", aci_path);
				log_info(&bus, msg);
			}
			if (tape_path != NULL) {
				port_result_t rc;
				rc = aci_load_tape(aci_card, tape_path);
				if (rc != PORT_OK) {
					cli_error("Error: aci_load_tape "
						  "failed: %s\n",
					    port_error_string(rc));
					aci_free(aci_card);
					bus_free(&bus);
					return (1);
				}
				{
					char msg[512];

					port_snprintf(msg, sizeof(msg),
					    "ACI tape loaded: %s",
					    tape_path);
					log_info(&bus, msg);
				}
			}
			bus_add_card(&bus, aci_card);
		} else if (tape_path != NULL || save_tape_path != NULL) {
			cli_error("Error: ACI tape operations requested but "
				  "ACI "
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
			char msg[512];

			bus_add_card(&bus, kcard);
			port_snprintf(msg, sizeof(msg),
			    "Krusader ROM loaded: %s", krusader_path);
			log_info(&bus, msg);
		}
		port_free(krusader_path);
		krusader_path = NULL;
	}
#endif /* APPLE1_OMIT_KRUSADER */

	/* CPU init */
	cpu_init(&cpu, &bus);
	log_info(&bus, "CPU initialized");
	/* Headless/debug runs reset immediately; interactive play waits
	 * for Ctrl+R (authentic Apple-1 power-on behaviour). */
	if (opt_headless != false || opt_debug != false) {
		cpu_reset(&cpu);
		log_info(&bus, "CPU reset");
	}
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
	last_time = port_gettime_us();
	last_kbd_poll = last_time;
	last_render = port_gettime_us();
	prev_pc = 0xFFFF;
	loop_count = 0;

	for (;;) {
		if (process_sigint() != 0) {
			break;
		}

		/* Keyboard + reset (poll stdin ~1 kHz, not every insn) */
		if (opt_headless == false) {
			uint32_t now_kbd;

			now_kbd = port_gettime_us();
			if (now_kbd - last_kbd_poll >= KBD_POLL_US) {
				bus_update_keyboard(&bus);
				last_kbd_poll = now_kbd;
				if (io_reset_pending() != 0) {
					bus_reset(&bus);
					io_reset();
					cpu_reset(&cpu);
				}
			}
#ifndef APPLE1_OMIT_DEBUGGER
			if (process_sigint_dbg(&dbg) != 0) {
				break;
			}
#else
			if (process_sigint() != 0) {
				break;
			}
#endif
		}

		/* Debugger: pause before run (-g) or at breakpoint */
#ifndef APPLE1_OMIT_DEBUGGER
		if (g_debug_enabled != 0) {
			if (dbg.step_mode != 0 ||
			    dbg_has_breakpoint(&dbg, cpu.pc) != 0) {
				int empty_step;

				empty_step = dbg.step_mode;
				dbg.step_mode = false;
				if (dbg_interactive_loop(&dbg, empty_step) !=
				    0) {
					break;
				}
				continue;
			}
		}
#endif

#ifndef APPLE1_OMIT_DEBUGGER
		if (process_sigint_dbg(&dbg) != 0) {
			break;
		}
#else
		if (process_sigint() != 0) {
			break;
		}
#endif

		/* Optional CPU trace */
#ifndef APPLE1_OMIT_DISASM
		if (opt_trace != 0) {
			uint8_t op;
			int len;

			op = bus_read(&bus, cpu.pc);
			len = cpu_disassemble(&bus, cpu.pc, disasm_buf);

			if (len == 1) {
				port_snprintf(hex_bytes,
				    sizeof(hex_bytes),
				    "%02X      ",
				    op);
			} else if (len == 2) {
				port_snprintf(hex_bytes,
				    sizeof(hex_bytes),
				    "%02X %02X   ",
				    op,
				    bus_read(&bus, cpu.pc + 1));
			} else {
				port_snprintf(hex_bytes,
				    sizeof(hex_bytes),
				    "%02X %02X %02X",
				    op,
				    bus_read(&bus, cpu.pc + 1),
				    bus_read(&bus, cpu.pc + 2));
			}
			port_snprintf(trace_line,
			    sizeof(trace_line),
			    "$%04X  %s  %-20s A:%02X X:%02X Y:%02X "
			    "SP:%02X P:%02X",
			    cpu.pc,
			    hex_bytes,
			    disasm_buf,
			    cpu.a,
			    cpu.x,
			    cpu.y,
			    cpu.s,
			    cpu.p);
			cli_printf("%s\n", trace_line);
		}
#endif

		/* Step CPU */
#ifndef APPLE1_OMIT_DEBUGGER
		bus.access_cb = dbg_check_access;
		bus.access_cb_ctx = &dbg;
		dbg.current_instruction_pc = cpu.pc;
#endif
		{
			uint8_t cycles = cpu_step(&cpu);

			bus.access_cb = NULL;
			cycle_accumulator += cycles;
			bus_tick_kbd_bounce(&bus, cycles);

			/* Tick expansion cards */
			for (j = 0; j < bus.num_cards; j++) {
				if (bus.cards[j]->tick != NULL) {
					bus.cards[j]->tick(bus.cards[j]->ctx,
					    cycles);
				}
			}
		}

#ifndef APPLE1_OMIT_DEBUGGER
		/* Single-step from debugger: run one insn, then pause again */
		if (g_debug_enabled != 0 && dbg.repause != 0) {
			dbg.repause = 0;
			if (dbg_interactive_loop(&dbg, 0) != 0) {
				break;
			}
			continue;
		}
#endif

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
						cli_error("FAIL: cpu trapped "
							  "at "
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
		if (opt_uncapped == false &&
		    cycle_accumulator >= CYCLES_PER_MS) {
			uint32_t current_time;
			uint32_t elapsed_us;
			uint32_t expected_us;

			current_time = port_gettime_us();
			elapsed_us = current_time - last_time;
			expected_us = 1000UL; /* 1 ms = 1000 us */

			if (elapsed_us < expected_us) {
				port_sleep_us(expected_us - elapsed_us);
			}
			last_time = port_gettime_us();
			cycle_accumulator = 0;
		}

		/* Render terminal at ~30 fps regardless of speed mode */
		if (opt_headless == false) {
			uint32_t now = port_gettime_us();
			if (now - last_render >= 33333UL) {
				term_update();
				last_render = now;
			}
		}
	}

	cleanup_cards(&bus, save_tape_path);
	bus_free(&bus);
	
	/* Free allocated path strings */
	if (rom_path != NULL) {
		port_free(rom_path);
	}
	if (bin_path != NULL) {
		port_free(bin_path);
	}
	if (wozmon_txt_path != NULL) {
		port_free(wozmon_txt_path);
	}
	if (aci_path != NULL) {
		port_free(aci_path);
	}
	if (tape_path != NULL) {
		port_free(tape_path);
	}
	if (save_tape_path != NULL) {
		port_free(save_tape_path);
	}
	if (krusader_path != NULL) {
		port_free(krusader_path);
	}
	if (log_path != NULL) {
		port_free(log_path);
	}

	if (opt_headless == false) {
		io_cleanup();
	}

	/* Final log entry and close */
	if (g_log_file != NULL) {
		BUS_LOG((&bus), BUS_LOG_INFO, "Emulator exited normally");
		g_port_vfs->close(g_log_file);
		g_log_file = NULL;
	}
	return (0);
}

/* dbg_log_append stub — retained for optional frontends */
void
dbg_log_append(const char *str)
{
	(void)str;
}
