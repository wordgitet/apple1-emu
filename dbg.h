#ifndef DBG_H
#define DBG_H

#include "cpu.h"
#include <stdbool.h>

#define MAX_BREAKPOINTS 32
#define MAX_WATCHPOINTS 32

typedef enum { WP_READ = 1, WP_WRITE = 2, WP_ACCESS = 3 } wp_type_t;

typedef struct {
	uint16_t addr;
	wp_type_t type;
} watchpoint_t;

typedef struct {
	CPU *cpu;
	uint16_t breakpoints[MAX_BREAKPOINTS];
	int num_breakpoints;
	watchpoint_t watchpoints[MAX_WATCHPOINTS];
	int num_watchpoints;
	bool step_mode;
	uint16_t current_instruction_pc;
} debugger_t;

void
dbg_init(debugger_t *dbg, CPU *cpu);
void
dbg_add_breakpoint(debugger_t *dbg, uint16_t addr);
void
dbg_remove_breakpoint(debugger_t *dbg, uint16_t addr);
bool
dbg_has_breakpoint(debugger_t *dbg, uint16_t addr);

void
dbg_add_watchpoint(debugger_t *dbg, uint16_t addr, wp_type_t type);
void
dbg_remove_watchpoint(debugger_t *dbg, uint16_t addr);
void
dbg_check_access(void *ctx, uint16_t addr, bool is_write, uint8_t val);

void
dbg_interactive_loop(debugger_t *dbg);
void
dbg_run_command(debugger_t *dbg, const char *cmd_line);

#endif // DBG_H
