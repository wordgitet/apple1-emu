/*
 * krusader.c - Krusader 6502 assembler/disassembler expansion card.
 *
 * No hardwired stderr.  All errors routed through BUS_LOG.
 * One Krusader card instance at a time, which is all the real
 * hardware ever allowed.
 */
#ifndef APPLE1_OMIT_KRUSADER
#include "bus.h"
#include "krusader.h"
#include "port.h"

static bool s_in_use = false;

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
krusader_create(struct bus *bus, const char *rom_path)
{
	struct expansion_card *card;
	struct krusader_card *kru;
	void *f;
	char msg[128];
	port_size_t nread;
	port_size_t size;

	if (s_in_use != 0) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Krusader: only one instance supported");
		return (NULL);
	}

	f = port_vfs_default.open(rom_path, PORT_VFS_READ);
	if (f == PORT_FILE_INVALID) {
		port_snprintf(msg,
		    sizeof(msg),
		    "Krusader: cannot open ROM '%s'",
		    rom_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		return (NULL);
	}

	if (port_vfs_default.size(f, &size) != 0) {
		port_snprintf(msg,
		    sizeof(msg),
		    "Krusader: cannot size ROM '%s'",
		    rom_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		port_vfs_default.close(f);
		return (NULL);
	}

	if (size == 0 || size > 4096) {
		port_snprintf(msg,
		    sizeof(msg),
		    "Krusader: ROM '%s' is %lu bytes; must be 1-4096",
		    rom_path,
		    (unsigned long)size);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		port_vfs_default.close(f);
		return (NULL);
	}

	kru = (struct krusader_card *)port_malloc(sizeof(struct krusader_card));
	if (kru == NULL) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Krusader: out of memory");
		port_vfs_default.close(f);
		return (NULL);
	}

	card = (struct expansion_card *)port_malloc(
	    sizeof(struct expansion_card));
	if (card == NULL) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Krusader: out of memory");
		port_free(kru);
		port_vfs_default.close(f);
		return (NULL);
	}

	port_memset(kru->rom, 0xFF, 4096);
	nread = 0;
	if (port_vfs_default.read(f, kru->rom, (port_size_t)size, &nread) !=
		0 ||
	    nread != (port_size_t)size) {
		port_snprintf(msg,
		    sizeof(msg),
		    "Krusader: short read on '%s' (%lu of %lu bytes)",
		    rom_path,
		    (unsigned long)nread,
		    (unsigned long)size);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		port_vfs_default.close(f);
		port_free(kru);
		port_free(card);
		return (NULL);
	}
	port_vfs_default.close(f);

	kru->size = (uint32_t)size;

	card->name = "Krusader";
	card->base = 0xE000;
	card->mask = 0xF000;
	card->rom_only = true;
	card->read = krusader_read;
	card->write = NULL;
	card->tick = NULL;
	card->ctx = kru;

	s_in_use = true;

	port_snprintf(msg,
	    sizeof(msg),
	    "Registered Krusader card: ROM at 0xE000-0x%04X",
	    (uint16_t)(0xE000 + size - 1));
	BUS_LOG(bus, BUS_LOG_INFO, msg);

	return (card);
}

void
krusader_free(struct expansion_card *card)
{
	struct krusader_card *kru;

	if (card == NULL) {
		return;
	}
	kru = (struct krusader_card *)card->ctx;
	port_free(kru);
	port_free(card);
	s_in_use = false;
}

#endif /* APPLE1_OMIT_KRUSADER */
