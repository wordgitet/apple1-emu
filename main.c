#define _POSIX_C_SOURCE 199309L
#include "aci.h"
#include "bus.h"
#include "cpu.h"
#include "dbg.h"
#include "disasm.h"
#include "io.h"
#include "krusader.h"
#include "term_apple1.h"
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CLOCK_RATE_HZ 1022727 // Apple 1 runs at 1.022727 MHz
#define CYCLES_PER_MS (CLOCK_RATE_HZ / 1000)

static volatile sig_atomic_t g_debug_break = 0;
bool g_debug_enabled = false;

Bus *g_bus = NULL;
CPU *g_cpu = NULL;
debugger_t *g_dbg = NULL;
char *g_argv0 = NULL;

static void
handle_signal(int sig)
{
	(void)sig;
	if (g_debug_enabled) {
		g_debug_break = 1;
	} else {
		// Calling exit() triggers the atexit handlers, which restores the terminal.
		exit(0);
	}
}

static void
print_usage(const char *prog_name)
{
	printf("Apple 1 Emulator in C\n");
	printf("Usage: %s -r <rom_path> [options] [flat_binary]\n\n",
	    prog_name);
	printf("Required (unless --flat-bus is specified):\n");
	printf("  -r <rom_path>         Path to exactly 256-byte Woz Monitor "
	       "ROM file.\n\n");
	printf("Options:\n");
	printf("  -m <ram_kb>           RAM size in KB (4 to 64, default is "
	       "8).\n");
	printf("  -l <file>@<hex_addr>  Load binary file into RAM at hex "
	       "address (e.g. basic.bin@E000).\n");
	printf("  -c                    Cap overall CPU emulation speed to "
	       "1.023 MHz.\n");
	printf("  -p                    Disable 977ns PIA I/O throttling.\n");
	printf("  -d                    Enable DRAM refresh emulation cycle "
	       "stealing.\n");
	printf("  -b                    Enable keyboard bounce emulation.\n");
	printf("  -s                    Disable cold boot RAM/PIA "
	       "randomization.\n");
	printf("  -f, --flat-bus        Map entire 0x0000-0xFFFF range as "
	       "plain RAM.\n");
	printf("  -H                    Headless mode (disable terminal raw "
	       "mode, video, throttler).\n");
	printf("  -g, --debug           Enable interactive debugger (pauses on "
	       "start).\n");
	printf("  -e, --tape <file>     Load WAV tape (.wav) for playback.\n");
	printf("  -E, --save-tape <f>  Save ACI tape output as WAV file on "
	       "exit.\n");
	printf("  -x                    Enable destructive backspace (cursor "
	       "moves back and erases).\n");
}

static void
cleanup_cards(Bus *bus, const char *save_tape_path)
{
	for (int i = 0; i < bus->num_cards; i++) {
		if (strcmp(bus->cards[i]->name, "ACI") == 0) {
			if (save_tape_path) {
				aci_save_tape(bus->cards[i], save_tape_path);
			}
			aci_free(bus->cards[i]);
		} else if (strcmp(bus->cards[i]->name, "Krusader") == 0) {
			krusader_free(bus->cards[i]);
		}
	}
	bus->num_cards = 0;
}

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
			// fallback to current directory
			snprintf(out_path, max_len, "apple1.conf");
		}
	}
}

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
	FILE *fp = fopen(path, "r");
	if (!fp) {
		return;
	}

	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		// Strip trailing newline/whitespace
		size_t len = strlen(line);
		while (len > 0 &&
		    (line[len - 1] == '\r' || line[len - 1] == '\n' ||
			line[len - 1] == ' ' || line[len - 1] == '\t')) {
			line[--len] = '\0';
		}
		// Strip leading whitespace
		char *ptr = line;
		while (*ptr == ' ' || *ptr == '\t') {
			ptr++;
		}
		// Skip comment or empty line
		if (*ptr == '#' || *ptr == '\0') {
			continue;
		}

		// Must start with '-'
		if (ptr[0] == '-' && ptr[1] != '\0') {
			char flag = ptr[1];
			char *val = ptr + 2;
			while (*val == ' ' || *val == '\t') {
				val++;
			}
			bool has_val = (*val != '\0');

			switch (flag) {
			case 'r':
				if (has_val) {
					if (*rom_path)
						free(*rom_path);
					*rom_path = strdup(val);
				}
				break;
			case 'm':
				if (has_val) {
					int kb = atoi(val);
					if (kb >= 4 && kb <= 64) {
						*ram_size = kb * 1024;
					}
				}
				break;
			case 'l':
				if (has_val) {
					char *val_dup = strdup(val);
					char *at = strchr(val_dup, '@');
					if (at) {
						*at = '\0';
						if (*bin_path)
							free(*bin_path);
						*bin_path = strdup(val_dup);
						*bin_address = (uint16_t)
						    strtol(at + 1, NULL, 16);
					}
					free(val_dup);
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
			case 'f':
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
				if (has_val) {
					if (*aci_path)
						free(*aci_path);
					*aci_path = strdup(val);
				}
				break;
			case 'e':
				if (has_val) {
					if (*tape_path)
						free(*tape_path);
					*tape_path = strdup(val);
				}
				break;
			case 'E':
				if (has_val) {
					if (*save_tape_path)
						free(*save_tape_path);
					*save_tape_path = strdup(val);
				}
				break;
			case 'k':
				if (has_val) {
					if (*krusader_path)
						free(*krusader_path);
					*krusader_path = strdup(val);
				}
				break;
			case 'x':
				break;
			default:
				break;
			}
		}
	}
	fclose(fp);
}

int
main(int argc, char *argv[])
{
	g_argv0 = argv[0];
	srand(time(NULL));

	char *rom_path = NULL;
	char *bin_path = NULL;
	char *wozmon_txt_path = NULL;
	char *aci_path = NULL;
	char *tape_path = NULL;
	char *save_tape_path = NULL;
	char *krusader_path = NULL;
	uint16_t bin_address = 0;
	uint32_t ram_size = 0; /* must come from config */

	bool opt_uncapped = false;
	bool opt_throttle_pia = false;
	bool opt_emulate_dram = false;
	bool opt_emulate_bounce = false;
	bool opt_randomize_cold = false;
	bool opt_flat_bus = false;
	bool opt_headless = false;
	bool opt_debug = false;
	bool opt_trace = false;

	char config_path[512];
	get_xdg_config_path(config_path, sizeof(config_path));

	/* No config file — run the blocking first-time setup wizard */
	if (access(config_path, F_OK) != 0) {
		term_run_config_wizard();
	}

	load_config_file(config_path,
	    &rom_path,
	    &ram_size,
	    &bin_path,
	    &bin_address,
	    &opt_uncapped,
	    &opt_throttle_pia,
	    &opt_emulate_dram,
	    &opt_emulate_bounce,
	    &opt_randomize_cold,
	    &opt_flat_bus,
	    &opt_headless,
	    &opt_debug,
	    &opt_trace,
	    &aci_path,
	    &tape_path,
	    &save_tape_path,
	    &krusader_path);

	// Pre-process argv to convert --flat-bus to -f, --debug to -g, --trace to -t, --tape to -e, --save-tape to -E, and -wm to -w
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--flat-bus") == 0) {
			argv[i] = "-f";
		} else if (strcmp(argv[i], "--debug") == 0) {
			argv[i] = "-g";
		} else if (strcmp(argv[i], "--trace") == 0) {
			argv[i] = "-t";
		} else if (strcmp(argv[i], "--tape") == 0) {
			argv[i] = "-e";
		} else if (strcmp(argv[i], "--save-tape") == 0) {
			argv[i] = "-E";
		} else if (strcmp(argv[i], "-wm") == 0) {
			argv[i] = "-w";
		}
	}

	// Parse CLI arguments
	int opt;

	while (
	    (opt = getopt(argc, argv, "r:m:l:w:cpdbshfHa:k:gte:E:x")) != -1) {
		switch (opt) {
		case 'r':
			rom_path = optarg;
			break;
		case 'm': {
			int kb = atoi(optarg);

			if (kb < 4 || kb > 64) {
				fprintf(stderr,
				    "Error: RAM size must be between 4 and "
				    "64 KB.\n");
				return 1;
			}
			ram_size = kb * 1024;
			break;
		}
		case 'l': {
			char *at = strchr(optarg, '@');

			if (!at) {
				fprintf(stderr,
				    "Error: Invalid binary load format. "
				    "Use <file>@<hex_addr> (e.g. "
				    "basic.bin@E000).\n");
				return 1;
			}
			*at = '\0';
			bin_path = optarg;
			bin_address = (uint16_t)strtol(at + 1, NULL, 16);
			break;
		}
		case 'w':
			wozmon_txt_path = optarg;
			break;
		case 'c':
			opt_uncapped = false;
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
		case 'f':
			opt_flat_bus = true;
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
		case 'a':
			aci_path = optarg;
			break;
		case 'e':
			tape_path = optarg;
			break;
		case 'E':
			save_tape_path = optarg;
			break;
		case 'k':
			krusader_path = optarg;
			break;
		case 'x':
			break;
		case 'h':
		default:
			print_usage(argv[0]);
			return 0;
		}
	}

	/* Validate required config values — fall back to embedded ROM if not present */
	if (!opt_flat_bus) {
		if (!rom_path || rom_path[0] == '\0' ||
		    access(rom_path, F_OK) != 0) {
			if (rom_path && rom_path[0] != '\0') {
				fprintf(stderr,
				    "Warning: ROM file '%s' not found. "
				    "Using embedded Wozmon ROM.\n",
				    rom_path);
			} else {
				printf("Using embedded Wozmon ROM.\n");
			}
			rom_path = NULL;
		}
	}
	if (ram_size == 0) {
		fprintf(stderr,
		    "Error: RAM size not set in config. "
		    "Open the emulator and go to CONFIG to set it.\n");
		return 1;
	}

	if (tape_path && opt_uncapped) {
		fprintf(stderr,
		    "Error: ACI tape loading requires capped CPU speed. "
		    "Please run with the -c flag.\n");
		return 1;
	}

	/*
	 * DRAM refresh is always present on real Apple-1 hardware.
	 * Auto-enable it whenever capped speed is requested so ACI
	 * tape timing matches real silicon.
	 */
	if (!opt_uncapped) {
		opt_emulate_dram = true;
	}

	// Set up exit signals to clean up raw terminal mode
	g_debug_enabled = opt_debug;
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	atexit(io_cleanup);

	char *flat_bin_path = NULL;

	if (opt_flat_bus) {
		ram_size = 65536;
		if (optind < argc) {
			flat_bin_path = argv[optind];
		} else {
			fprintf(stderr,
			    "Error: Flat binary file path is required when "
			    "--flat-bus is enabled.\n");
			print_usage(argv[0]);
			return 1;
		}
	}

	// Initialize systems
	Bus bus;

	if (!bus_init(&bus, ram_size)) {
		return 1;
	}
	g_bus = &bus;

	// Apply configured option overrides
	bus.opts.uncapped = opt_uncapped;
	bus.opts.throttle_pia = opt_throttle_pia;
	bus.opts.emulate_dram_refresh = opt_emulate_dram;
	bus.opts.emulate_kbd_bounce = opt_emulate_bounce;
	bus.opts.randomize_cold_boot = opt_randomize_cold;
	bus.opts.flat_bus = opt_flat_bus;
	bus.opts.headless = opt_headless;

	// Perform actual cold boot RAM/PIA initialization based on options
	if (bus.opts.randomize_cold_boot) {
		for (uint32_t i = 0; i < ram_size; i++) {
			bus.ram[i] = (rand() & 1) ? 0xFF : 0x00;
		}
		bus.pia.kbd_data = rand() & 0xFF;
		bus.pia.kbd_control = rand() & 0xFF;
		bus.pia.dsp_data = rand() & 0xFF;
		bus.pia.dsp_control = rand() & 0xFF;
	} else {
		memset(bus.ram, 0, ram_size);
		bus.pia.kbd_data = 0x00;
		bus.pia.kbd_control = 0x00;
		bus.pia.dsp_data = 0x00;
		bus.pia.dsp_control = 0x80; // Default ready
	}

	// Load ROM
	if (!bus.opts.flat_bus) {
		if (!bus_load_rom(&bus, rom_path)) {
			bus_free(&bus);
			return 1;
		}
	} else {
		if (!bus_load_bin(&bus, flat_bin_path, 0x0000)) {
			bus_free(&bus);
			return 1;
		}
		// Overwrite Reset Vector to point to $0400
		bus.ram[RESET_VECTOR] = 0x00;
		bus.ram[RESET_VECTOR + 1] = 0x04;
	}

	// Load additional binary into RAM if specified
	if (bin_path) {
		if (!bus_load_bin(&bus, bin_path, bin_address)) {
			bus_free(&bus);
			return 1;
		}
	}

	// Load Woz Monitor text dump if specified
	if (wozmon_txt_path) {
		uint16_t run_addr;
		bool has_run_addr;
		if (!bus_load_wozmon_txt(&bus,
			wozmon_txt_path,
			&run_addr,
			&has_run_addr)) {
			bus_free(&bus);
			return 1;
		}
		if (has_run_addr) {
			bus.ram[RESET_VECTOR] = run_addr & 0xFF;
			bus.ram[RESET_VECTOR + 1] = run_addr >> 8;
		}
	}

	// Load ACI expansion card if path is specified
	if (aci_path) {
		expansion_card_t *aci_card = aci_create(aci_path);
		if (aci_card) {
			if (tape_path) {
				if (!aci_load_tape(aci_card, tape_path)) {
					aci_free(aci_card);
					bus_free(&bus);
					return 1;
				}
			}
			bus_add_card(&bus, aci_card);
		} else if (tape_path || save_tape_path) {
			fprintf(stderr,
			    "Error: ACI tape operations requested, but ACI "
			    "ROM could not be loaded.\n");
			bus_free(&bus);
			return 1;
		}
	}

	// Load Krusader expansion card if path is specified
	if (krusader_path) {
		expansion_card_t *krusader_card = krusader_create(
		    krusader_path);
		if (!krusader_card) {
			cleanup_cards(&bus, NULL);
			bus_free(&bus);
			return 1;
		}
		bus_add_card(&bus, krusader_card);
	}

	if (!bus.opts.headless) {
		io_init();
	}

	// Initialize CPU
	CPU cpu;

	cpu_init(&cpu, &bus);
	g_cpu = &cpu;

	if (bus.opts.headless) {
		cpu_reset(&cpu);
	}

	debugger_t dbg;

	dbg_init(&dbg, &cpu);
	g_dbg = &dbg;
	if (opt_debug) {
		dbg.step_mode = true;
	}

	uint32_t cycle_accumulator = 0;
	struct timespec last_time;

	clock_gettime(CLOCK_MONOTONIC, &last_time);

	uint16_t prev_pc = 0xFFFF;
	int loop_count = 0;

	while (true) {
		if (!bus.opts.headless &&
		    (!term_is_powered() ||
			(term_is_paused() && !term_should_step()))) {
			term_poll();
			struct timespec req = { 0, 10000000 }; // 10ms
			nanosleep(&req, NULL);
			continue;
		}

		// 1. Poll for input and update PIA strobe
		if (!bus.opts.headless) {
			bus_update_keyboard(&bus);
			/* Ctrl-R: assert the 6502 RESET line */
			if (io_reset_pending()) {
				bus_reset(&bus);
				io_reset();
				cpu_reset(&cpu);
			}
		}

		// Check if we should break into the debugger before executing the instruction
		if (g_debug_enabled) {
			if (g_debug_break || dbg.step_mode ||
			    dbg_has_breakpoint(&dbg, cpu.pc)) {
				g_debug_break = 0;
				if (bus.opts.headless) {
					dbg_interactive_loop(&dbg);
				} else {
					dbg.step_mode = true;
					term_poll();
					if (!term_should_step()) {
						struct timespec req = { 0,
							1000000 }; // 1ms
						nanosleep(&req, NULL);
						continue;
					}
				}
			}
		}

		// Tracing — send to GUI trace window when active, else stdout
		if (opt_trace || (!bus.opts.headless && term_trace_active())) {
			char disasm_buf[64];
			uint8_t op = bus_read(&bus, cpu.pc);
			int len = cpu_disassemble(&bus, cpu.pc, disasm_buf);
			char hex_bytes[16];

			if (len == 1) {
				sprintf(hex_bytes, "%02X      ", op);
			} else if (len == 2) {
				sprintf(hex_bytes,
				    "%02X %02X   ",
				    op,
				    bus_read(&bus, cpu.pc + 1));
			} else {
				sprintf(hex_bytes,
				    "%02X %02X %02X",
				    op,
				    bus_read(&bus, cpu.pc + 1),
				    bus_read(&bus, cpu.pc + 2));
			}
			char trace_line[160];
			snprintf(trace_line,
			    sizeof(trace_line),
			    "$%04X  %s  %-20s A:%02X X:%02X Y:%02X SP:%02X "
			    "P:%02X",
			    cpu.pc,
			    hex_bytes,
			    disasm_buf,
			    cpu.a,
			    cpu.x,
			    cpu.y,
			    cpu.s,
			    cpu.p);
			if (!bus.opts.headless && term_trace_active()) {
				term_trace_push(trace_line);
			} else {
				printf("%s\n", trace_line);
			}
		}

		// 2. Step the CPU
		bus.access_cb = dbg_check_access;
		bus.access_cb_ctx = &dbg;
		dbg.current_instruction_pc = cpu.pc;
		uint8_t cycles = cpu_step(&cpu);
		bus.access_cb = NULL;

		cycle_accumulator += cycles;

		// Tick all expansion cards
		for (int i = 0; i < bus.num_cards; i++) {
			if (bus.cards[i]->tick) {
				bus.cards[i]->tick(bus.cards[i]->ctx, cycles);
			}
		}

		// Loop detection
		if (bus.opts.flat_bus && bus.opts.headless) {
			if (cpu.pc == prev_pc) {
				loop_count++;
				if (loop_count >= 3) {
					// Halt and check PC
					if (cpu.pc == 0x3469) {
						printf("Klaus Dormann "
						       "functional test: "
						       "PASS\n");
						bus_free(&bus);
						return 0;
					} else if (cpu.pc == 0x0324) {
						printf("Wolfgang Lorenz "
						       "illegal opcode test: "
						       "PASS\n");
						bus_free(&bus);
						return 0;
					} else {
						fprintf(stderr,
						    "FAIL: CPU trapped at "
						    "$%04X\n",
						    cpu.pc);
						bus_free(&bus);
						return 1;
					}
				}
			} else {
				loop_count = 0;
			}
			prev_pc = cpu.pc;
		}

		// 3. Timing throttler (capped mode)
		if (!bus.opts.uncapped && !bus.opts.headless &&
		    cycle_accumulator >= CYCLES_PER_MS) {
			struct timespec current_time;

			clock_gettime(CLOCK_MONOTONIC, &current_time);

			long elapsed_ns =
			    (current_time.tv_sec - last_time.tv_sec) *
				1000000000L +
			    (current_time.tv_nsec - last_time.tv_nsec);
			long expected_ns =
			    1000000L; // 1 millisecond in nanoseconds

			if (elapsed_ns < expected_ns) {
				struct timespec sleep_time;

				sleep_time.tv_sec = 0;
				sleep_time.tv_nsec = expected_ns - elapsed_ns;
				nanosleep(&sleep_time, NULL);
			}

			// Sync timing reference
			clock_gettime(CLOCK_MONOTONIC, &last_time);
			cycle_accumulator = 0;
		}
	}

	// Free resources (though we won't reach here normally due to infinite loop and exit signals)
	cleanup_cards(&bus, save_tape_path);
	bus_free(&bus);
	return 0;
}
