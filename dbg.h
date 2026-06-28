#ifndef DBG_H
#define DBG_H

#include "port.h"
#include "apple1limit.h"

#include "cpu.h"

typedef enum { WP_READ = 1, WP_WRITE = 2, WP_ACCESS = 3 } wp_type_t;

typedef struct {
	uint16_t addr;
	wp_type_t type;
} watchpoint_t;

typedef void (*dbg_out_cb_t)(void *ctx, const char *msg);

typedef struct {
#ifndef APPLE1_OMIT_DEBUGGER
	struct cpu *cpu;
	uint16_t breakpoints[APPLE1_MAX_BREAKPOINTS];
	int num_breakpoints;
	watchpoint_t watchpoints[APPLE1_MAX_WATCHPOINTS];
	int num_watchpoints;
	bool step_mode;
	int  repause;
	uint16_t current_instruction_pc;
	dbg_out_cb_t out;
	void *out_ctx;
#else
	int dummy;
#endif
} debugger_t;

void
dbg_init(debugger_t *dbg, struct cpu *cpu);
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

/* Returns non-zero if the user requested full emulator exit (q or Ctrl-C).
 * empty_step: if non-zero, Enter on the first prompt acts like 's'. */
int
dbg_interactive_loop(debugger_t *dbg, int empty_step);
void
dbg_run_command(debugger_t *dbg, const char *cmd_line);

#endif /* DBG_H */
