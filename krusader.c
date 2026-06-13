#include "krusader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	uint8_t rom[4096];
	uint32_t size;
} krusader_card_t;

static uint8_t
krusader_read(void *ctx, uint16_t addr, bool is_dummy)
{
	(void)is_dummy;
	krusader_card_t *kru = (krusader_card_t *)ctx;
	uint16_t offset = addr & 0x0FFF;

	if (offset < kru->size) {
		return kru->rom[offset];
	}
	return 0x00;
}

expansion_card_t *
krusader_create(const char *rom_path)
{
	FILE *f = fopen(rom_path, "rb");

	if (!f) {
		fprintf(stderr,
			"Error: Could not open Krusader ROM file '%s'\n",
			rom_path);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);

	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 4096) {
		fprintf(stderr,
			"Error: Krusader ROM file '%s' is %ld bytes. Must be "
			"between 1 and 4096 bytes.\n",
			rom_path,
			size);
		fclose(f);
		return NULL;
	}

	krusader_card_t *kru = malloc(sizeof(krusader_card_t));

	if (!kru) {
		perror("Failed to allocate Krusader context");
		fclose(f);
		return NULL;
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
		return NULL;
	}
	fclose(f);
	kru->size = size;

	expansion_card_t *card = malloc(sizeof(expansion_card_t));

	if (!card) {
		perror("Failed to allocate Krusader expansion card");
		free(kru);
		return NULL;
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
	return card;
}

void
krusader_free(expansion_card_t *card)
{
	if (card) {
		if (card->ctx) {
			free(card->ctx);
		}
		free(card);
	}
}
