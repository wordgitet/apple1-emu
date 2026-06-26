#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "krusader.h"

struct krusader_card {
	uint8_t rom[4096];
	uint32_t size;
};

static uint8_t
krusader_read(void *ctx, uint16_t addr, bool is_dummy)
{
	struct krusader_card *kru;
	uint16_t offset;

	(void)is_dummy;
	kru = (struct krusader_card *)ctx;
	offset = addr & 0x0FFF;

	if (offset < kru->size) {
		return (kru->rom[offset]);
	}
	return (0x00);
}

struct expansion_card *
krusader_create(const char *rom_path)
{
	struct krusader_card *kru;
	struct expansion_card *card;
	FILE *f;
	long size;

	f = fopen(rom_path, "rb");
	if (f == NULL) {
		fprintf(stderr,
		    "Error: Could not open Krusader ROM file '%s'\n",
		    rom_path);
		return (NULL);
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 4096) {
		fprintf(stderr,
		    "Error: Krusader ROM file '%s' is %ld bytes. Must be "
		    "between 1 and 4096 bytes.\n",
		    rom_path,
		    size);
		fclose(f);
		return (NULL);
	}

	kru = malloc(sizeof(struct krusader_card));
	if (kru == NULL) {
		perror("Failed to allocate Krusader context");
		fclose(f);
		return (NULL);
	}
	memset(kru->rom, 0xFF, 4096);

	if (fread(kru->rom, 1, size, f) != (size_t)size) {
		fprintf(stderr,
		    "Error: Failed to read %ld bytes from Krusader ROM "
		    "'%s'\n",
		    size,
		    rom_path);
		free(kru);
		fclose(f);
		return (NULL);
	}
	fclose(f);
	kru->size = size;

	card = malloc(sizeof(struct expansion_card));
	if (card == NULL) {
		perror("Failed to allocate Krusader expansion card");
		free(kru);
		return (NULL);
	}

	card->name = "Krusader";
	card->base = 0xE000;
	card->mask = 0xF000;
	card->rom_only = true;
	card->read = krusader_read;
	card->write = NULL;
	card->tick = NULL;
	card->ctx = kru;

	printf("Registered Krusader card: ROM loaded at 0xE000 - 0x%04X\n",
	    (uint16_t)(0xE000 + size - 1));
	return (card);
}

void
krusader_free(struct expansion_card *card)
{
	if (card != NULL) {
		if (card->ctx != NULL) {
			free(card->ctx);
		}
		free(card);
	}
}
