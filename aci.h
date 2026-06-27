#ifndef ACI_H
#define ACI_H

#include "apple1limit.h"
#include "bus.h"

#ifndef APPLE1_OMIT_ACI

#define ACI_CLOCK 1022727

/* Create and initialize the ACI cassette interface card */
struct expansion_card *
aci_create(struct bus *bus, const char *rom_path);

/* Load a .aci tape file into the ACI card */
bool
aci_load_tape(struct expansion_card *card, const char *tape_path);

/* Save recorded ACI tape output to a .aci file */
bool
aci_save_tape(struct expansion_card *card, const char *tape_path);

/* Free ACI card resources */
void
aci_free(struct expansion_card *card);

/* Get recorded pulse transition count */
uint32_t
aci_get_recorded_count(struct expansion_card *card);

#endif /* APPLE1_OMIT_ACI */

#endif /* ACI_H */
