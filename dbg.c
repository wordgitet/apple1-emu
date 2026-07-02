#ifndef APPLE1_OMIT_DEBUGGER
#include "bus.h"
#include "dbg.h"
#include "disasm.h"
#include "io.h"
#include "port.h"
#include "term_apple1.h"

extern port_sig_flag g_quit_flag;

static debugger_t *s_active_dbg = NULL;

int
dbg_printf(const char *format, ...);

static void
print_help(void)
{
	dbg_printf("Debugger commands:\n");
	dbg_printf("  s, <Enter>        Step one instruction\n");
	dbg_printf("  c                 Continue execution\n");
	dbg_printf("  r                 Show struct cpu registers (A, X, Y, "
		   "PC, S, P "
		   "flags)\n");
	dbg_printf("  m [start] [end]   Dump memory in hex (e.g. 'm 0200 "
		   "0210')\n");
	dbg_printf("  w [addr] [val]    Write byte to memory (e.g. 'w 0200 "
		   "EA')\n");
	dbg_printf("  b [addr]          Add breakpoint (e.g. 'b FF00'). With "
		   "no "
		   "address, lists breakpoints\n");
	dbg_printf("  d [addr]          Delete breakpoint (e.g. 'd FF00'). "
		   "With no "
		   "address, clears all\n");
	dbg_printf("  wp [addr] [type]  Add watchpoint (type: r/w/rw, e.g. 'wp "
		   "0200 w'). "
		   "With no args, lists watchpoints\n");
	dbg_printf("  wd [addr]         Delete watchpoint (e.g. 'wd 0200'). "
		   "With "
		   "no "
		   "address, clears all\n");
	dbg_printf("  t                 Show recent control flow jumps (PC "
		   "trace)\n");
	dbg_printf("  h, ?              Show this help menu\n");
	dbg_printf("  q                 Quit emulator\n");
}

static int
dbg_parse_hex(const char *s, unsigned int *out)
{
	const char *p;
	char *end;
	unsigned long val;

	if (s == NULL || out == NULL) {
		return (0);
	}
	p = s;
	while (port_isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '\0') {
		return (0);
	}
	val = port_strtoul(p, &end, 16);
	if (end == p) {
		return (0);
	}
	*out = (unsigned int)val;
	return (1);
}

static int
dbg_parse_hex_pair(const char *s, unsigned int *a, unsigned int *b)
{
	const char *p;
	char *end;
	unsigned long v1;
	unsigned long v2;

	if (s == NULL || a == NULL || b == NULL) {
		return (0);
	}
	p = s;
	while (port_isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '\0') {
		return (0);
	}
	v1 = port_strtoul(p, &end, 16);
	if (end == p) {
		return (0);
	}
	*a = (unsigned int)v1;
	p = end;
	while (port_isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '\0') {
		return (1);
	}
	v2 = port_strtoul(p, &end, 16);
	if (end == p) {
		return (1);
	}
	*b = (unsigned int)v2;
	return (2);
}

static int
dbg_parse_hex_and_word(const char *s,
    unsigned int *addr,
    char *word,
    port_size_t word_sz)
{
	const char *p;
	char *end;
	unsigned long val;
	port_size_t i;

	if (s == NULL || addr == NULL || word == NULL || word_sz == 0) {
		return (0);
	}
	p = s;
	while (port_isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '\0') {
		return (0);
	}
	val = port_strtoul(p, &end, 16);
	if (end == p) {
		return (0);
	}
	*addr = (unsigned int)val;
	p = end;
	while (port_isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '\0') {
		return (1);
	}
	i = 0;
	while (
	    *p != '\0' && !port_isspace((unsigned char)*p) && i + 1 < word_sz) {
		word[i++] = *p++;
	}
	word[i] = '\0';
	return (word[0] != '\0' ? 2 : 1);
}

static void
print_registers(struct cpu *cpu)
{
	dbg_printf("PC:%04X  A:%02X  X:%02X  Y:%02X  SP:%02X  P:%02X (",
	    cpu->pc,
	    cpu->a,
	    cpu->x,
	    cpu->y,
	    cpu->s,
	    cpu->p);
	dbg_printf("%c", (cpu->p & FLAG_NEGATIVE) ? 'N' : '-');
	dbg_printf("%c", (cpu->p & FLAG_OVERFLOW) ? 'V' : '-');
	dbg_printf("-");
	dbg_printf("%c", (cpu->p & FLAG_BREAK) ? 'B' : '-');
	dbg_printf("%c", (cpu->p & FLAG_DECIMAL) ? 'D' : '-');
	dbg_printf("%c", (cpu->p & FLAG_INTERRUPT) ? 'I' : '-');
	dbg_printf("%c", (cpu->p & FLAG_ZERO) ? 'Z' : '-');
	dbg_printf("%c", (cpu->p & FLAG_CARRY) ? 'C' : '-');
	dbg_printf(")\n");
}

static void
dump_memory(debugger_t *dbg, uint16_t start, uint16_t end)
{
	uint32_t current, i, line_end;
	uint8_t ch, val;

	current = start;
	while (current <= end) {
		dbg_printf("$%04X: ", current);
		line_end = current + 15;
		if (line_end > end) {
			line_end = end;
		}
		for (i = current; i <= current + 15; i++) {
			if (i <= line_end) {
				dbg_printf("%02X ", bus_read(dbg->cpu->bus, (uint16_t)i));
			} else {
				dbg_printf("   ");
			}
		}
		dbg_printf(" |");
		for (i = current; i <= line_end; i++) {
			val = bus_read(dbg->cpu->bus, (uint16_t)i);
			ch = val & 0x7F;
			dbg_printf("%c", (ch >= 0x20 && ch <= 0x7E) ? ch : '.');
		}
		dbg_printf("|\n");
		if (current >= 0xFFF0) {
			break;
		}
		current += 16;
	}
}

void
dbg_init(debugger_t *dbg, struct cpu *cpu)
{
	dbg->cpu = cpu;
	dbg->num_breakpoints = 0;
	dbg->num_watchpoints = 0;
	dbg->step_mode = false;
	dbg->repause = 0;
	dbg->current_instruction_pc = 0;
	dbg->out = NULL;
	dbg->out_ctx = NULL;
	s_active_dbg = dbg;
}

void
dbg_add_watchpoint(debugger_t *dbg, uint16_t addr, wp_type_t type)
{
	int i;

	for (i = 0; i < dbg->num_watchpoints; i++) {
		if (dbg->watchpoints[i].addr == addr) {
			dbg->watchpoints[i].type = type;
			dbg_printf("Updated watchpoint at $%04X to type %s.\n",
			    addr,
			    type == WP_READ ? "read"
					    : (type == WP_WRITE ? "write"
								: "read/"
								  "write"));
			return;
		}
	}

	if (dbg->num_watchpoints >= APPLE1_MAX_WATCHPOINTS) {
		dbg_printf("Error: Maximum number of watchpoints reached "
			   "(%d).\n",
		    APPLE1_MAX_WATCHPOINTS);
		return;
	}

	dbg->watchpoints[dbg->num_watchpoints].addr = addr;
	dbg->watchpoints[dbg->num_watchpoints].type = type;
	dbg->num_watchpoints++;

	dbg_printf("Watchpoint added at $%04X (%s).\n",
	    addr,
	    type == WP_READ ? "read"
			    : (type == WP_WRITE ? "write" : "read/write"));
}

void
dbg_remove_watchpoint(debugger_t *dbg, uint16_t addr)
{
	int i;
	int idx;

	idx = -1;
	for (i = 0; i < dbg->num_watchpoints; i++) {
		if (dbg->watchpoints[i].addr == addr) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		dbg_printf("No watchpoint found at $%04X.\n", addr);
		return;
	}

	for (i = idx; i < dbg->num_watchpoints - 1; i++) {
		dbg->watchpoints[i] = dbg->watchpoints[i + 1];
	}
	dbg->num_watchpoints--;
	dbg_printf("Watchpoint removed at $%04X.\n", addr);
}

void
dbg_check_access(void *ctx, uint16_t addr, bool is_write, uint8_t val)
{
	debugger_t *dbg;
	int i;

	dbg = (debugger_t *)ctx;
	if (dbg == NULL)
		return;

	for (i = 0; i < dbg->num_watchpoints; i++) {
		watchpoint_t *wp = &dbg->watchpoints[i];
		if (wp->addr == addr) {
			bool trigger;

			trigger = false;
			if (is_write != 0 && (wp->type & WP_WRITE) != 0)
				trigger = true;
			if (is_write == 0 && (wp->type & WP_READ) != 0)
				trigger = true;

			if (trigger != 0) {
				dbg->step_mode = true;
				if (dbg->cpu->bus->opts.headless == 0) {
					term_open_debugger();
				}
				dbg_printf("\n*** WATCHPOINT TRIGGERED ***\n");
				if (is_write) {
					dbg_printf("Write to $%04X with value "
						   "$%02X at PC $%04X\n",
					    addr,
					    val,
					    dbg->current_instruction_pc);
				} else {
					dbg_printf("Read from $%04X: value "
						   "$%02X "
						   "at PC $%04X\n",
					    addr,
					    val,
					    dbg->current_instruction_pc);
				}
				break;
			}
		}
	}
}

void
dbg_add_breakpoint(debugger_t *dbg, uint16_t addr)
{
	if (dbg_has_breakpoint(dbg, addr)) {
		dbg_printf("Breakpoint at $%04X already exists.\n", addr);
		return;
	}
	if (dbg->num_breakpoints >= APPLE1_MAX_BREAKPOINTS) {
		dbg_printf("Error: Maximum number of breakpoints reached "
			   "(%d).\n",
		    APPLE1_MAX_BREAKPOINTS);
		return;
	}
	dbg->breakpoints[dbg->num_breakpoints++] = addr;
	dbg_printf("Breakpoint added at $%04X.\n", addr);
}

void
dbg_remove_breakpoint(debugger_t *dbg, uint16_t addr)
{
	int i;
	int idx;

	idx = -1;
	for (i = 0; i < dbg->num_breakpoints; i++) {
		if (dbg->breakpoints[i] == addr) {
			idx = i;
			break;
		}
	}
	if (idx == -1) {
		dbg_printf("No breakpoint found at $%04X.\n", addr);
		return;
	}
	for (i = idx; i < dbg->num_breakpoints - 1; i++) {
		dbg->breakpoints[i] = dbg->breakpoints[i + 1];
	}
	dbg->num_breakpoints--;
	dbg_printf("Breakpoint removed at $%04X.\n", addr);
}

bool
dbg_has_breakpoint(debugger_t *dbg, uint16_t addr)
{
	int i;

	for (i = 0; i < dbg->num_breakpoints; i++) {
		if (dbg->breakpoints[i] == addr) {
			return (true);
		}
	}
	return (false);
}

void
dbg_run_command(debugger_t *dbg, const char *cmd_line)
{
	char input[256];
	char type_str[16];
	pc_edge_t edge;
	const char *t;
	char *args;
	char *cmd_str;
	unsigned int addr;
	unsigned int end_val;
	unsigned int start_val;
	unsigned int val;
	int i;
	int idx;
	int parsed;
	char cmd;

	port_strncpy(input, cmd_line, sizeof(input) - 1);
	input[sizeof(input) - 1] = '\0';

	cmd_str = input;

	while (*cmd_str == ' ' || *cmd_str == '\t') {
		cmd_str++;
	}
	if (*cmd_str == '\0') {
		cmd_str = "s";
	}

	args = cmd_str;
	if (port_strncmp(cmd_str, "wp", 2) == 0 &&
	    (cmd_str[2] == '\0' || cmd_str[2] == ' ' || cmd_str[2] == '\t')) {
		args = cmd_str + 2;
		while (*args == ' ' || *args == '\t') {
			args++;
		}
		addr = 0;
		type_str[0] = '\0';
		parsed = dbg_parse_hex_and_word(args,
		    &addr,
		    type_str,
		    sizeof(type_str));
		if (parsed >= 1) {
			if (addr > 0xFFFF) {
				dbg_printf("Error: Address must be a 16-bit "
					   "hex "
					   "value.\n");
			} else {
				wp_type_t type = WP_WRITE;
				if (parsed == 2) {
					if (port_strcmp(type_str, "r") == 0) {
						type = WP_READ;
					} else if (port_strcmp(type_str, "w") ==
					    0) {
						type = WP_WRITE;
					} else if (port_strcmp(type_str,
						       "rw") == 0) {
						type = WP_ACCESS;
					} else {
						dbg_printf("Warning: Unknown "
							   "type "
							   "'%s'. Defaulting "
							   "to "
							   "'w'.\n",
						    type_str);
					}
				}
				dbg_add_watchpoint(dbg, (uint16_t)addr, type);
			}
		} else {
			if (dbg->num_watchpoints == 0) {
				dbg_printf("No active watchpoints.\n");
			} else {
				dbg_printf("Active watchpoints:\n");
				for (i = 0; i < dbg->num_watchpoints; i++) {
					t = "unknown";
					if (dbg->watchpoints[i].type == WP_READ)
						t = "read";
					else if (dbg->watchpoints[i].type ==
					    WP_WRITE)
						t = "write";
					else if (dbg->watchpoints[i].type ==
					    WP_ACCESS)
						t = "read/write";
					dbg_printf("  Watchpoint %d at $%04X "
						   "(%s)\n",
					    i + 1,
					    dbg->watchpoints[i].addr,
					    t);
				}
			}
		}
	} else if (port_strncmp(cmd_str, "wd", 2) == 0 &&
	    (cmd_str[2] == '\0' || cmd_str[2] == ' ' || cmd_str[2] == '\t')) {
		args = cmd_str + 2;
		while (*args == ' ' || *args == '\t') {
			args++;
		}
		addr = 0;
		if (dbg_parse_hex(args, &addr) == 1) {
			if (addr > 0xFFFF) {
				dbg_printf("Error: Address must be a 16-bit "
					   "hex "
					   "value.\n");
			} else {
				dbg_remove_watchpoint(dbg, (uint16_t)addr);
			}
		} else {
			dbg->num_watchpoints = 0;
			dbg_printf("All watchpoints cleared.\n");
		}
	} else {
		cmd = cmd_str[0];
		args = cmd_str + 1;

		while (*args == ' ' || *args == '\t') {
			args++;
		}

		if (cmd == 'q') {
			dbg_printf("Exiting emulator...\n");
			port_exit(0);
		} else if (cmd == 'h' || cmd == '?') {
			print_help();
		} else if (cmd == 's') {
			dbg->repause = 1;
			dbg->step_mode = false;
			term_request_step();
		} else if (cmd == 'c') {
			dbg->step_mode = false;
			term_close_debugger();
			dbg_printf("Continuing...\n");
		} else if (cmd == 'r') {
			print_registers(dbg->cpu);
		} else if (cmd == 'm') {
			start_val = dbg->cpu->pc;
			end_val = 0;
			parsed = dbg_parse_hex_pair(args, &start_val, &end_val);
			if (parsed == 1) {
				end_val = start_val + 15;
			} else if (parsed == 0) {
				start_val = dbg->cpu->pc;
				end_val = start_val + 15;
			}
			if (start_val > 0xFFFF || end_val > 0xFFFF) {
				dbg_printf("Error: Addresses must be 16-bit "
					   "hex "
					   "values.\n");
			} else if (end_val < start_val) {
				dbg_printf("Error: End address cannot be less "
					   "than "
					   "start address.\n");
			} else {
				if (end_val - start_val > 255) {
					end_val = start_val + 255;
					dbg_printf("Warning: Capping memory "
						   "dump "
						   "to 256 bytes.\n");
				}
				dump_memory(dbg,
				    (uint16_t)start_val,
				    (uint16_t)end_val);
			}
		} else if (cmd == 'w') {
			addr = 0;
			val = 0;

			if (dbg_parse_hex_pair(args, &addr, &val) == 2) {
				if (addr > 0xFFFF || val > 0xFF) {
					dbg_printf("Error: Invalid address "
						   "($%04X) "
						   "or value ($%02X).\n",
					    addr,
					    val);
				} else {
					bus_write(dbg->cpu->bus,
					    (uint16_t)addr,
					    (uint8_t)val);
					dbg_printf("Wrote $%02X to $%04X.\n",
					    val,
					    addr);
				}
			} else {
				dbg_printf("Usage: w [hex_addr] [hex_val]\n");
			}
		} else if (cmd == 'b') {
			addr = 0;

			if (dbg_parse_hex(args, &addr) == 1) {
				if (addr > 0xFFFF) {
					dbg_printf("Error: Address must be a "
						   "16-bit hex value.\n");
				} else {
					dbg_add_breakpoint(dbg, (uint16_t)addr);
				}
			} else {
				if (dbg->num_breakpoints == 0) {
					dbg_printf("No active breakpoints.\n");
				} else {
					dbg_printf("Active breakpoints:\n");
					for (i = 0; i < dbg->num_breakpoints;
					    i++) {
						dbg_printf("  Breakpoint %d at "
							   "$%04X\n",
						    i + 1,
						    dbg->breakpoints[i]);
					}
				}
			}
		} else if (cmd == 'd') {
			addr = 0;

			if (dbg_parse_hex(args, &addr) == 1) {
				if (addr > 0xFFFF) {
					dbg_printf("Error: Address must be a "
						   "16-bit hex value.\n");
				} else {
					dbg_remove_breakpoint(dbg,
					    (uint16_t)addr);
				}
			} else {
				dbg->num_breakpoints = 0;
				dbg_printf("All breakpoints cleared.\n");
			}
		} else if (cmd == 't') {
			dbg_printf("Control Flow Transitions (oldest to "
				   "newest):\n");
			idx = dbg->cpu->pc_trace_idx;
			for (i = 0; i < 24; i++) {
				edge = dbg->cpu->pc_trace[idx];
				if (edge.from != 0 || edge.to != 0) {
					dbg_printf("  $%04X -> $%04X\n",
					    edge.from,
					    edge.to);
				}
				idx = (idx + 1) % 24;
			}
		} else {
			dbg_printf("Unknown command: %c. Type 'h' or '?' for "
				   "help.\n",
			    cmd);
		}
	}
}

static int
dbg_want_quit(void)
{
	if (g_quit_flag != 0) {
		g_quit_flag = 0;
		return (1);
	}
	return (0);
}

static int
dbg_read_line(char *buf, port_size_t size)
{
	if (port_term_read_line_dbg(buf, size, &g_quit_flag) != NULL) {
		return (1);
	}
	return (0);
}

int
dbg_interactive_loop(debugger_t *dbg, int empty_step)
{
	bus_access_cb_t old_cb;
	char disasm_buf[64];
	uint8_t op;
	int empty_step_ok;
	uint16_t stack_top;
	int n;
	uint16_t addr;

	if (dbg->cpu->bus->opts.headless == 0) {
		io_cleanup();
		port_term_write_buf("\x1b[0m\r\n", 6);
		port_term_dbg_enable();
	}

	empty_step_ok = empty_step;

	/* Suspend access callback to avoid infinite recursion/re-triggering
	 * from debug reads */
	old_cb = dbg->cpu->bus->access_cb;
	dbg->cpu->bus->access_cb = NULL;
	cpu_disassemble(dbg->cpu->bus, dbg->cpu->pc, disasm_buf);

	/* Check if this is a BRK instruction (BRK opcode is 0x00) */
	op = bus_read(dbg->cpu->bus, dbg->cpu->pc);
	if (op == 0x00) {
		dbg_printf("\n*** BRK INSTRUCTION DETECTED ***\n");
	}

	dbg_printf("--- Apple-1 debugger ---\r\n");
	print_registers(dbg->cpu);
	dbg_printf("  $%04X: %s\r\n", dbg->cpu->pc, disasm_buf);

	/* Print stack dump (top 8 bytes below SP) */
	dbg_printf("  Stack: ");
	if (dbg->cpu->s == 0xFF) {
		dbg_printf("(empty)\r\n");
	} else {
		stack_top = (uint16_t)(0x100u + dbg->cpu->s);

		for (n = 0; n < 8; n++) {
			addr = stack_top - (uint16_t)n;

			if (addr < 0x0100u) {
				break;
			}
			dbg_printf("%02X ", bus_read(dbg->cpu->bus, addr));
		}
		dbg_printf("\r\n");
	}

	for (;;) {
		char input[256];
		char *cmd_str;
		char cmd;
		port_size_t len;

		if (dbg_want_quit()) {
			dbg_printf("Exiting emulator...\r\n");
			return (1);
		}

		dbg_printf("dbg> ");

		if (dbg_read_line(input, sizeof(input)) == 0) {
			if (dbg_want_quit()) {
				dbg_printf("Exiting emulator...\r\n");
				return (1);
			}
			dbg_printf("Exiting emulator...\r\n");
			return (1);
		}
		if (dbg_want_quit()) {
			dbg_printf("Exiting emulator...\r\n");
			return (1);
		}

		len = port_strlen(input);

		if (len > 0 && input[len - 1] == '\n') {
			input[len - 1] = '\0';
		}

		cmd_str = input;
		while (*cmd_str == ' ' || *cmd_str == '\t') {
			cmd_str++;
		}
		cmd = cmd_str[0];
		if (cmd == '\0') {
			if (empty_step_ok != 0) {
				cmd = 's';
				empty_step_ok = 0;
			} else {
				continue;
			}
		}

		dbg_run_command(dbg, input);

		if (cmd == 's' || cmd == 'c' ||
		    port_strcmp(cmd_str, "s") == 0 ||
		    port_strcmp(cmd_str, "c") == 0) {
			break;
		}
	}

	/* Restore access callback when resuming execution */
	dbg->cpu->bus->access_cb = old_cb;

	if (dbg->cpu->bus->opts.headless == 0) {
		port_term_dbg_disable();
		io_init();
	}
	return (0);
}

extern struct bus *g_bus;

int
dbg_printf(const char *format, ...)
{
	va_list args;
	char buf[1024];
	int ret;

	va_start(args, format);
	ret = port_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if (s_active_dbg != NULL && s_active_dbg->out != NULL) {
		s_active_dbg->out(s_active_dbg->out_ctx, buf);
	} else {
		port_term_write_buf(buf, port_strlen(buf));
	}
	return (ret);
}
#else /* APPLE1_OMIT_DEBUGGER */

#include "dbg.h"

void
dbg_init(debugger_t *dbg, struct cpu *cpu)
{
	(void)dbg;
	(void)cpu;
}

void
dbg_add_breakpoint(debugger_t *dbg, uint16_t addr)
{
	(void)dbg;
	(void)addr;
}

void
dbg_remove_breakpoint(debugger_t *dbg, uint16_t addr)
{
	(void)dbg;
	(void)addr;
}

bool
dbg_has_breakpoint(debugger_t *dbg, uint16_t addr)
{
	(void)dbg;
	(void)addr;
	return (false);
}

void
dbg_add_watchpoint(debugger_t *dbg, uint16_t addr, wp_type_t type)
{
	(void)dbg;
	(void)addr;
	(void)type;
}

void
dbg_remove_watchpoint(debugger_t *dbg, uint16_t addr)
{
	(void)dbg;
	(void)addr;
}

void
dbg_check_access(void *ctx, uint16_t addr, bool is_write, uint8_t val)
{
	(void)ctx;
	(void)addr;
	(void)is_write;
	(void)val;
}

int
dbg_interactive_loop(debugger_t *dbg, int empty_step)
{
	(void)dbg;
	(void)empty_step;
	return (0);
}

void
dbg_run_command(debugger_t *dbg, const char *cmd_line)
{
	(void)dbg;
	(void)cmd_line;
}

#endif /* APPLE1_OMIT_DEBUGGER */
