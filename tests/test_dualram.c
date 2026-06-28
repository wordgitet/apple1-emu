#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../bus.h"

static uint8_t test_ram[65536];

static void
expect_writable(struct bus *bus, uint16_t address, uint8_t value)
{
	bus_write(bus, address, value);
	assert(bus_read(bus, address) == value);
}

static void
expect_unmapped(struct bus *bus, uint16_t address, uint8_t value)
{
	uint8_t hi_bank_probe;

	/*
	 * In 8 KB split mode, logical $E000 maps to physical ram[0x1000].
	 * Gap addresses ($1000-$DFFF) must not touch that cell — verify
	 * using the byte left by expect_writable(..., 0xE000, 0x13).
	 */
	hi_bank_probe = bus->ram[0x1000];

	bus_write(bus, address, value);

	assert(bus->ram[0x1000] == hi_bank_probe);

	bus_write(bus, 0x0000, 0xAA);
	assert(bus_read(bus, address) == 0xAA);
}

static void
test_pia_mirroring(void)
{
	struct bus bus;

	if (bus_init(&bus, test_ram, 8192) == false) {
		fprintf(stderr,
		    "Failed to initialize bus for PIA mirroring test\n");
		exit(1);
	}

	// 1. Write to D011 (KBDCR) and read from mirrors
	bus_write(&bus, 0xD011, 0x55);
	assert(bus_read(&bus, 0xD011) == 0x55);
	assert(bus_read(&bus, 0xD0F1) == 0x55);
	assert(bus_read(&bus, 0xDF11) == 0x55);

	// 2. Write to mirror D0F1 and read from D011 / DF11
	bus_write(&bus, 0xD0F1, 0x33);
	assert(bus_read(&bus, 0xD011) == 0x33);
	assert(bus_read(&bus, 0xDF11) == 0x33);

	// 3. Write to D013 (DSPCR) and read from mirrors
	bus_write(&bus, 0xD013, 0x66);
	assert(bus_read(&bus, 0xD013) == 0xE6);
	assert(bus_read(&bus, 0xDF13) == 0xE6);

	// 4. Write to mirror DF13 and read from D013
	bus_write(&bus, 0xDF13, 0x22);
	assert(bus_read(&bus, 0xD013) == 0xA2);

	bus_free(&bus);
	printf("Test PASS: PIA 6821 Address Mirroring\n");
}

int
main(void)
{
	printf("Starting Dual-Bank 8KB RAM Tests...\n");

	struct bus bus;

	/* Initialize with 8 KB (8192 bytes) RAM size */
	if (bus_init(&bus, test_ram, 8192) == false) {
		fprintf(stderr, "Failed to initialize bus with 8KB RAM\n");
		return (1);
	}

	// Standard Apple-1 / Replica dual-bank RAM: low 4 KB plus high 4 KB.
	expect_writable(&bus, 0x0000, 0x11);
	expect_writable(&bus, 0x0FFF, 0x12);
	expect_writable(&bus, 0xE000, 0x13);
	expect_writable(&bus, 0xEFFF, 0x14);

	// The gap between the two banks is not RAM.
	expect_unmapped(&bus, 0x1000, 0x21);
	expect_unmapped(&bus, 0x1FFF, 0x22);
	expect_unmapped(&bus, 0x7FFF, 0x23);

	bus_free(&bus);

	printf("Test PASS: Dual-Bank 8KB RAM\n");

	// Run mirroring test
	test_pia_mirroring();

	return (0);
}
