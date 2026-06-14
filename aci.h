#ifndef ACI_H
#define ACI_H

#define ACI_CLOCK 1022727

#include "bus.h"

// Create and initialize the ACI cassette interface card
expansion_card_t *
aci_create(const char *rom_path);

// Load a .aci tape file into the ACI card
bool
aci_load_tape(expansion_card_t *card, const char *tape_path);

// Save recorded ACI tape output to a .aci file
bool
aci_save_tape(expansion_card_t *card, const char *tape_path);

// Free ACI card resources
void
aci_free(expansion_card_t *card);

// Get recorded pulse transition count
uint32_t
aci_get_recorded_count(expansion_card_t *card);

#endif // ACI_H
