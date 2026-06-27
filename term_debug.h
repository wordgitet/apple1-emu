#ifndef TERM_DEBUG_H
#define TERM_DEBUG_H

#include <SDL3/SDL.h>
#include "port.h"

void
term_debug_init(void);
void
term_debug_shutdown(void);

void
term_debug_toggle(void);
bool
term_debug_is_open(void);
void
term_debug_render(void);
bool
term_debug_is_window(SDL_Window *win);
void
term_debug_handle_event(const SDL_Event *ev);

void
term_trace_toggle(void);
bool
term_trace_is_open(void);
void
term_trace_render(void);
bool
term_trace_is_window(SDL_Window *win);
void
term_trace_handle_event(const SDL_Event *ev);
void
term_trace_export(void);

#endif /* TERM_DEBUG_H */
