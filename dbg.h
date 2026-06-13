#ifndef DBG_H
#define DBG_H

#include "cpu.h"
#include <stdbool.h>

#define MAX_BREAKPOINTS 32

typedef struct {
	CPU *cpu;
	uint16_t breakpoints[MAX_BREAKPOINTS];
	int num_breakpoints;
	bool step_mode;
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
dbg_interactive_loop(debugger_t *dbg);
void
dbg_run_command(debugger_t *dbg, const char *cmd_line);

#endif // DBG_H
