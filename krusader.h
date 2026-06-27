#ifndef KRUSADER_H
#define KRUSADER_H

#include <stdint.h>

#include "bus.h"

/*
 * Internal ROM + state for one Krusader card.
 * Declared here so callers can embed it without malloc.
 */
struct krusader_card {
	uint8_t  rom[4096];
	uint32_t size;
};

/*
 * Create and initialise the Krusader assembler expansion card.
 * bus      - used for BUS_LOG error reporting (must not be NULL).
 * rom_path - path to the Krusader ROM binary (1-4096 bytes).
 * Returns a pointer to the static expansion_card on success, NULL on error.
 * Only one instance is supported at a time.
 */
struct expansion_card *
krusader_create(struct bus *bus, const char *rom_path);

/* Release the Krusader card (marks static slot as free). */
void
krusader_free(struct expansion_card *card);

#endif /* KRUSADER_H */
