#ifndef KRUSADER_H
#define KRUSADER_H

#include "bus.h"

// Create and initialize the Krusader card
expansion_card_t *
krusader_create(const char *rom_path);

// Free Krusader card resources
void
krusader_free(expansion_card_t *card);

#endif // KRUSADER_H
