#ifndef KRUSADER_H
#define KRUSADER_H

#include "bus.h"

/* Create and initialize the Krusader card */
struct expansion_card *
krusader_create(const char *rom_path);

/* Free Krusader card resources */
void
krusader_free(struct expansion_card *card);

#endif /* KRUSADER_H */
