#ifndef TERM_APPLE1_H
#define TERM_APPLE1_H

#include <stdbool.h>
#include <stdint.h>

void
term_init(void);
void
term_shutdown(void);
void
term_write(uint8_t val);
void
term_update(void);
uint8_t
term_poll(void);
void
term_set_welcome(const char *msg1, const char *msg2);
bool
term_reset_pending(void);
/* Blocking SDL config wizard — runs before bus init when no config exists.
 * Displays the config modal fullscreen until the user saves and exits. */
void
term_run_config_wizard(void);

/* Power state — when powered off the CRT goes dark and the CPU loop idles. */
bool
term_is_powered(void);

/* Returns true if the emulation is paused. */
bool
term_is_paused(void);

/* Push a single trace line (max 128 chars) into the trace ring buffer.
 * Called by main.c every instruction when tracing is active. */
void
term_trace_push(const char *line);

/* Returns true if a CPU step was requested from the GUI debugger. */
bool
term_should_step(void);

/* Returns true if the trace window is currently open. */
bool
term_trace_active(void);

/* Request a single CPU step from the GUI debugger. */
void
term_request_step(void);

/* Closes the debugger window if it is open. */
void
term_close_debugger(void);

#endif // TERM_APPLE1_H
