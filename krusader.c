/*
 * krusader.c - Krusader 6502 assembler/disassembler expansion card.
 *
 * No malloc, no hardwired stderr.  All errors routed through BUS_LOG.
 * Static storage supports one Krusader card instance at a time, which
 * is all the real hardware ever allowed.
 */
#ifndef APPLE1_OMIT_KRUSADER
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "krusader.h"

static struct krusader_card  s_kru;
static struct expansion_card s_card;
static bool                  s_in_use = false;

static uint8_t
krusader_read(void *ctx, uint16_t addr, bool is_dummy)
{
	struct krusader_card *kru;
	uint16_t offset;

	(void)is_dummy;
	kru    = (struct krusader_card *)ctx;
	offset = addr & 0x0FFF;

	if (offset < kru->size) {
		return (kru->rom[offset]);
	}
	return (0x00);
}

struct expansion_card *
krusader_create(struct bus *bus, const char *rom_path)
{
	FILE *f;
	char msg[128];
	long size;
	size_t nread;

	if (s_in_use != 0) {
		BUS_LOG(bus, BUS_LOG_ERROR,
		    "Krusader: only one instance supported");
		return (NULL);
	}

	f = fopen(rom_path, "rb");
	if (f == NULL) {
		snprintf(msg, sizeof(msg),
		    "Krusader: cannot open ROM '%s'", rom_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		return (NULL);
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0 || size > 4096) {
		snprintf(msg, sizeof(msg),
		    "Krusader: ROM '%s' is %ld bytes; must be 1-4096",
		    rom_path, size);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		fclose(f);
		return (NULL);
	}

	memset(s_kru.rom, 0xFF, 4096);
	nread = fread(s_kru.rom, 1, (size_t)size, f);
	fclose(f);

	if (nread != (size_t)size) {
		snprintf(msg, sizeof(msg),
		    "Krusader: short read on '%s' (%lu of %ld bytes)",
		    rom_path, (unsigned long)nread, size);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		return (NULL);
	}

	s_kru.size = (uint32_t)size;

	s_card.name    = "Krusader";
	s_card.base    = 0xE000;
	s_card.mask    = 0xF000;
	s_card.rom_only = true;
	s_card.read    = krusader_read;
	s_card.write   = NULL;
	s_card.tick    = NULL;
	s_card.ctx     = &s_kru;

	s_in_use = true;

	snprintf(msg, sizeof(msg),
	    "Registered Krusader card: ROM at 0xE000-0x%04X",
	    (uint16_t)(0xE000 + size - 1));
	BUS_LOG(bus, BUS_LOG_INFO, msg);

	return (&s_card);
}

void
krusader_free(struct expansion_card *card)
{
	if (card == NULL) {
		return;
	}
	/* Static storage — nothing to free; just mark slot available. */
	s_in_use = false;
	memset(&s_kru,  0, sizeof(s_kru));
	memset(&s_card, 0, sizeof(s_card));
}

#endif /* APPLE1_OMIT_KRUSADER */
