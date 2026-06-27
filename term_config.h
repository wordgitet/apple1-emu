#ifndef TERM_CONFIG_H
#define TERM_CONFIG_H

#include <SDL3/SDL.h>
#include "port.h"

void
term_config_init(void);
void
term_config_modal_render(SDL_Renderer *rend);
void
term_config_modal_handle_click(int mx, int my, bool is_wizard, bool *done);
void
term_config_modal_handle_key(const SDL_KeyboardEvent *key);
void
term_config_modal_handle_text_input(const char *text);
void
term_config_scroll(int delta);
void
term_run_config_wizard(void);

#endif /* TERM_CONFIG_H */
