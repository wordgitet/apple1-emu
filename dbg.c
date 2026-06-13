#include "dbg.h"
#include "disasm.h"
#include "io.h"
#include "term_apple1.h"
#include "bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

int dbg_printf(const char *format, ...);
#define printf(...) dbg_printf(__VA_ARGS__)

static void
print_help(void)
{
	printf("Debugger commands:\n");
	printf("  s, <Enter>        Step one instruction\n");
	printf("  c                 Continue execution\n");
	printf("  r                 Show CPU registers (A, X, Y, PC, S, P "
	       "flags)\n");
	printf("  m [start] [end]   Dump memory in hex (e.g. 'm 0200 0210')\n");
	printf("  w [addr] [val]    Write byte to memory (e.g. 'w 0200 EA')\n");
	printf("  b [addr]          Add breakpoint (e.g. 'b FF00'). With no "
	       "address, lists breakpoints\n");
	printf("  d [addr]          Delete breakpoint (e.g. 'd FF00'). With no "
	       "address, clears all\n");
	printf("  t                 Show recent control flow jumps (PC trace)\n");
	printf("  h, ?              Show this help menu\n");
	printf("  q                 Quit emulator\n");
}


static void
print_registers(CPU *cpu)
{
	printf("PC:%04X  A:%02X  X:%02X  Y:%02X  SP:%02X  P:%02X (",
		cpu->pc,
		cpu->a,
		cpu->x,
		cpu->y,
		cpu->s,
		cpu->p);
	printf("%c", (cpu->p & FLAG_NEGATIVE) ? 'N' : '-');
	printf("%c", (cpu->p & FLAG_OVERFLOW) ? 'V' : '-');
	printf("-");
	printf("%c", (cpu->p & FLAG_BREAK) ? 'B' : '-');
	printf("%c", (cpu->p & FLAG_DECIMAL) ? 'D' : '-');
	printf("%c", (cpu->p & FLAG_INTERRUPT) ? 'I' : '-');
	printf("%c", (cpu->p & FLAG_ZERO) ? 'Z' : '-');
	printf("%c", (cpu->p & FLAG_CARRY) ? 'C' : '-');
	printf(")\n");
}

static void
dump_memory(debugger_t *dbg, uint16_t start, uint16_t end)
{
	uint32_t current = start;

	while (current <= end) {
		printf("$%04X: ", current);
		uint32_t line_end = current + 15;

		if (line_end > end) {
			line_end = end;
		}
		for (uint32_t i = current; i <= current + 15; i++) {
			if (i <= line_end) {
				printf("%02X ", bus_read(dbg->cpu->bus, i));
			} else {
				printf("   ");
			}
		}
		printf(" |");
		for (uint32_t i = current; i <= line_end; i++) {
			uint8_t val = bus_read(dbg->cpu->bus, i);
			uint8_t ch = val & 0x7F;

			printf("%c", (ch >= 0x20 && ch <= 0x7E) ? ch : '.');
		}
		printf("|\n");
		if (current >= 0xFFF0) {
			break;
		}
		current += 16;
	}
}

void
dbg_init(debugger_t *dbg, CPU *cpu)
{
	dbg->cpu = cpu;
	dbg->num_breakpoints = 0;
	dbg->step_mode = true;
}

void
dbg_add_breakpoint(debugger_t *dbg, uint16_t addr)
{
	if (dbg_has_breakpoint(dbg, addr)) {
		printf("Breakpoint at $%04X already exists.\n", addr);
		return;
	}
	if (dbg->num_breakpoints >= MAX_BREAKPOINTS) {
		printf("Error: Maximum number of breakpoints reached (%d).\n",
			MAX_BREAKPOINTS);
		return;
	}
	dbg->breakpoints[dbg->num_breakpoints++] = addr;
	printf("Breakpoint added at $%04X.\n", addr);
}

void
dbg_remove_breakpoint(debugger_t *dbg, uint16_t addr)
{
	int idx = -1;

	for (int i = 0; i < dbg->num_breakpoints; i++) {
		if (dbg->breakpoints[i] == addr) {
			idx = i;
			break;
		}
	}
	if (idx == -1) {
		printf("No breakpoint found at $%04X.\n", addr);
		return;
	}
	for (int i = idx; i < dbg->num_breakpoints - 1; i++) {
		dbg->breakpoints[i] = dbg->breakpoints[i + 1];
	}
	dbg->num_breakpoints--;
	printf("Breakpoint removed at $%04X.\n", addr);
}

bool
dbg_has_breakpoint(debugger_t *dbg, uint16_t addr)
{
	for (int i = 0; i < dbg->num_breakpoints; i++) {
		if (dbg->breakpoints[i] == addr) {
			return true;
		}
	}
	return false;
}

void
dbg_run_command(debugger_t *dbg, const char *cmd_line)
{
	char input[256];
	strncpy(input, cmd_line, sizeof(input) - 1);
	input[sizeof(input) - 1] = '\0';

	char *cmd_str = input;

	while (*cmd_str == ' ' || *cmd_str == '\t') {
		cmd_str++;
	}
	if (*cmd_str == '\0') {
		cmd_str = "s";
	}

	char cmd = cmd_str[0];
	char *args = cmd_str + 1;

	while (*args == ' ' || *args == '\t') {
		args++;
	}

	if (cmd == 'q') {
		printf("Exiting emulator...\n");
		exit(0);
	} else if (cmd == 'h' || cmd == '?') {
		print_help();
	} else if (cmd == 's') {
		dbg->step_mode = true;
		term_request_step();
	} else if (cmd == 'c') {
		dbg->step_mode = false;
		term_close_debugger();
		printf("Continuing...\n");
	} else if (cmd == 'r') {
		print_registers(dbg->cpu);
	} else if (cmd == 'm') {
		unsigned int start_val = dbg->cpu->pc;
		unsigned int end_val = 0;
		int parsed = sscanf(args, "%x %x", &start_val, &end_val);
		if (parsed == 1) {
			end_val = start_val + 15;
		} else if (parsed == 0 || parsed == EOF) {
			start_val = dbg->cpu->pc;
			end_val = start_val + 15;
		}
		if (start_val > 0xFFFF || end_val > 0xFFFF) {
			printf("Error: Addresses must be 16-bit hex values.\n");
		} else if (end_val < start_val) {
			printf("Error: End address cannot be less than start address.\n");
		} else {
			if (end_val - start_val > 255) {
				end_val = start_val + 255;
				printf("Warning: Capping memory dump to 256 bytes.\n");
			}
			dump_memory(dbg, (uint16_t)start_val, (uint16_t)end_val);
		}
	} else if (cmd == 'w') {
		unsigned int addr = 0;
		unsigned int val = 0;

		if (sscanf(args, "%x %x", &addr, &val) == 2) {
			if (addr > 0xFFFF || val > 0xFF) {
				printf("Error: Invalid address ($%04X) or value ($%02X).\n", addr, val);
			} else {
				bus_write(dbg->cpu->bus, (uint16_t)addr, (uint8_t)val);
				printf("Wrote $%02X to $%04X.\n", val, addr);
			}
		} else {
			printf("Usage: w [hex_addr] [hex_val]\n");
		}
	} else if (cmd == 'b') {
		unsigned int addr = 0;

		if (sscanf(args, "%x", &addr) == 1) {
			if (addr > 0xFFFF) {
				printf("Error: Address must be a 16-bit hex value.\n");
			} else {
				dbg_add_breakpoint(dbg, (uint16_t)addr);
			}
		} else {
			if (dbg->num_breakpoints == 0) {
				printf("No active breakpoints.\n");
			} else {
				printf("Active breakpoints:\n");
				for (int i = 0; i < dbg->num_breakpoints; i++) {
					printf("  Breakpoint %d at $%04X\n", i + 1, dbg->breakpoints[i]);
				}
			}
		}
	} else if (cmd == 'd') {
		unsigned int addr = 0;

		if (sscanf(args, "%x", &addr) == 1) {
			if (addr > 0xFFFF) {
				printf("Error: Address must be a 16-bit hex value.\n");
			} else {
				dbg_remove_breakpoint(dbg, (uint16_t)addr);
			}
		} else {
			dbg->num_breakpoints = 0;
			printf("All breakpoints cleared.\n");
		}
	} else if (cmd == 't') {
		printf("Control Flow Transitions (oldest to newest):\n");
		int idx = dbg->cpu->pc_trace_idx;
		for (int i = 0; i < 24; i++) {
			pc_edge_t edge = dbg->cpu->pc_trace[idx];
			if (edge.from != 0 || edge.to != 0) {
				printf("  $%04X -> $%04X\n", edge.from, edge.to);
			}
			idx = (idx + 1) % 24;
		}
	} else {
		printf("Unknown command: %c. Type 'h' or '?' for help.\n", cmd);
	}
}


void
dbg_interactive_loop(debugger_t *dbg)
{
	if (!dbg->cpu->bus->opts.headless) {
		io_cleanup();
	}
	char disasm_buf[64];
	cpu_disassemble(dbg->cpu->bus, dbg->cpu->pc, disasm_buf);

	// Check if this is a BRK instruction (BRK opcode is 0x00)
	uint8_t op = bus_read(dbg->cpu->bus, dbg->cpu->pc);
	if (op == 0x00) {
		printf("\n*** BRK INSTRUCTION DETECTED ***\n");
	}

	printf("\n[DEBUG] ");
	print_registers(dbg->cpu);
	printf("        $%04X: %s\n", dbg->cpu->pc, disasm_buf);

	// Print stack dump (top 8 values)
	printf("        Stack: ");
	if (dbg->cpu->s == 0xFF) {
		printf("[Empty]");
	} else {
		for (int i = 0x100 + dbg->cpu->s + 1; i <= 0x1FF; i++) {
			printf("%02X ", bus_read(dbg->cpu->bus, i));
			if (i > 0x100 + dbg->cpu->s + 8) {
				printf("... ");
				break;
			}
		}
	}
	printf("\n");


	while (1) {
		printf("dbg> ");
		fflush(stdout);

		char input[256];

		if (!fgets(input, sizeof(input), stdin)) {
			break;
		}

		size_t len = strlen(input);

		if (len > 0 && input[len - 1] == '\n') {
			input[len - 1] = '\0';
		}

		char *cmd_str = input;
		while (*cmd_str == ' ' || *cmd_str == '\t') {
			cmd_str++;
		}
		char cmd = cmd_str[0];
		if (cmd == '\0') cmd = 's';

		dbg_run_command(dbg, input);

		if (cmd == 's' || cmd == 'c') {
			break;
		}
	}

	if (!dbg->cpu->bus->opts.headless) {
		io_init(); // Re-enable raw mode when execution resumes
	}
}

extern Bus *g_bus;
void dbg_log_append(const char *str);

int
dbg_printf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	char buf[1024];
	int ret = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if (g_bus && g_bus->opts.headless) {
		fputs(buf, stdout);
		fflush(stdout);
	} else {
		dbg_log_append(buf);
	}
	return ret;
}
