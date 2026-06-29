#include "../port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bus.h"

static uint8_t test_ram[65536];

int
main(void)
{
	struct bus bus;
	const char *tmp_txt;
	FILE *f;
	uint16_t run_addr;
	bool has_run;
	bool ok;

	if (bus_init(&bus, test_ram, 4096) != PORT_OK) {
		fprintf(stderr, "Failed to init bus\n");
		return (1);
	}

	/* Create a temporary wozmon dump */
	tmp_txt = "test_wozmon.txt";
	f = fopen(tmp_txt, "w");
	if (f == NULL)
		return (1);

	/* Wozmon dump with spaces, address change, and R command */
	fprintf(f, "0300: A9 01 8D 00  04\n");
	fprintf(f, "0305: A9 02 8D 01 04\n");
	fprintf(f, "  # A comment\n");
	fprintf(f, "030AR\n"); /* Just R with address */
	fclose(f);

	run_addr = 0;
	has_run = false;

	ok = bus_load_wozmon_txt(&bus, tmp_txt, &run_addr, &has_run);
	assert(ok == PORT_OK);
	assert(has_run == true);
	assert(run_addr == 0x030A);

	assert(bus.ram[0x0300] == 0xA9);
	assert(bus.ram[0x0301] == 0x01);
	assert(bus.ram[0x0302] == 0x8D);
	assert(bus.ram[0x0303] == 0x00);
	assert(bus.ram[0x0304] == 0x04);

	assert(bus.ram[0x0305] == 0xA9);
	assert(bus.ram[0x0306] == 0x02);
	assert(bus.ram[0x0307] == 0x8D);
	assert(bus.ram[0x0308] == 0x01);
	assert(bus.ram[0x0309] == 0x04);

	remove(tmp_txt);

	/* Create another dump with merged address+data and turbo/X blocks to test edge cases */
	f = fopen(tmp_txt, "w");
	fprintf(f, "0200:00 01 02\n");
	fprintf(f, "X03000203:040506\n"); /* X marker and merged address */
	fclose(f);

	ok = bus_load_wozmon_txt(&bus, tmp_txt, &run_addr, &has_run);
	assert(ok == PORT_OK);
	assert(has_run == false);
	assert(bus.ram[0x0200] == 0x00);
	assert(bus.ram[0x0201] == 0x01);
	assert(bus.ram[0x0202] == 0x02);
	assert(bus.ram[0x0203] == 0x04);
	assert(bus.ram[0x0204] == 0x05);
	assert(bus.ram[0x0205] == 0x06);

	remove(tmp_txt);

	bus_free(&bus);

	printf("test_bus passed.\n");
	return (0);
}
