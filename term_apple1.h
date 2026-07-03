#ifndef TERM_APPLE1_H
#define TERM_APPLE1_H

#include "port.h"

void
term_init(void);
void
term_shutdown(void);
void
term_write(uint8_t val);
/*
 * Returns true when the display is ready to accept another character.
 * In baud-rate mode this returns false until the inter-character delay
 * has elapsed; the PIA dsp_control bit 7 is driven from this flag so
 * that Wozmon's polling loop works correctly without blocking the CPU.
 */
bool
term_dsp_ready(void);
void
term_update(void);
uint8_t
term_poll(void);
bool
term_reset_pending(void);

#endif /* TERM_APPLE1_H */
