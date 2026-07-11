#ifndef KRUSADER_H
#define KRUSADER_H

#include "apple1limit.h"

#ifndef APPLE1_OMIT_KRUSADER

#include "bus.h"
#include "port.h"

/* Internal ROM + state for one Krusader card. */
struct krusader_card {
	uint8_t rom[4096];
	uint32_t size;
};

/*
 * Create and initialise the Krusader assembler expansion card.
 * bus      - used for BUS_LOG error reporting (must not be NULL).
 * rom_path - path to the Krusader ROM binary (1-4096 bytes).
 * Returns a heap-allocated expansion_card on success, NULL on error.
 * Only one instance is supported at a time.
 */
struct expansion_card *
krusader_create(struct bus *bus, const char *rom_path);

/* Release a Krusader card created by krusader_create(). */
void
krusader_free(struct expansion_card *card);

#endif /* APPLE1_OMIT_KRUSADER */

#endif /* KRUSADER_H */
