#include "../aci.h"
#include "../bus.h"
#include "../cpu.h"
#include "../dbg.h"
#include "../io.h"
#include "../port.h"
#include "../port_posix_inc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t test_ram[65536];

/* Mock keyboard buffer queue */
static char kbd_queue[512];
static int kbd_read_idx = 0;
static int kbd_write_idx = 0;

static void
queue_key(char c)
{
	if (kbd_write_idx < (int)sizeof(kbd_queue)) {
		kbd_queue[kbd_write_idx++] = c;
	}
}

static void
queue_wozmon_command(const char *cmd)
{
	const char *p;

	for (p = cmd; *p; p++) {
		queue_key(*p);
	}
	queue_key('\r'); /* Apple-1 Return is CR */
}

/* IO Mock implementations */
void
io_init(void)
{
}

void
io_cleanup(void)
{
}

bool
io_check_keyboard(void)
{
	return (kbd_read_idx < kbd_write_idx);
}

uint8_t
io_read_keyboard(void)
{
	if (kbd_read_idx < kbd_write_idx) {
		return (kbd_queue[kbd_read_idx++] | 0x80);
	}
	return (0);
}

void
io_write_display(uint8_t value)
{
	(void)value;
}

void
io_set_welcome(const char *msg1, const char *msg2)
{
	(void)msg1;
	(void)msg2;
}

bool
io_reset_pending(void)
{
	return (false);
}

void
io_reset(void)
{
}

static void
reset_kbd_queue(void)
{
	kbd_read_idx = 0;
	kbd_write_idx = 0;
}

static int
run_roundtrip_test(const char *tape_path,
    const uint8_t *pattern,
    int size,
    uint16_t from,
    uint16_t to)
{
	struct bus bus;
	struct bus read_bus;
	struct cpu cpu;
	struct cpu read_cpu;
	char cmd_buf[64];
	struct expansion_card *aci_card;
	struct expansion_card *read_aci_card;
	long cycles_since_growth;
	long total_cycles;
	int current_slice;
	int i;
	int read_slice;
	int slice_cycles;
	uint32_t last_transition_count;
	bool matched;
	uint8_t c;
	struct expansion_card *card;
	uint32_t now;

	printf("Running ACI roundtrip test for path: %s\n", tape_path);

	/* -------------------------------------------------------------
	 * PHASE 1: Write RAM to tape (using ACI WRITE ROM routine)
	 * ------------------------------------------------------------- */
	printf("  Phase 1: Starting ACI WRITE execution...\n");
	if (bus_init(&bus, test_ram, 16 * 1024) != PORT_OK) {
		fprintf(stderr, "Failed to init bus for ACI write test\n");
		return (1);
	}
	bus.opts.uncapped = true;
	bus.opts.flat_bus = false;
	bus.opts.headless = true;

	if (bus_load_rom(&bus, "wozmon.bin") != PORT_OK) {
		fprintf(stderr, "Failed to load wozmon.bin\n");
		bus_free(&bus);
		return (1);
	}

	aci_card = aci_create(&bus, "roms/ACI.rom");
	if (aci_card == NULL) {
		fprintf(stderr, "Failed to create ACI card\n");
		bus_free(&bus);
		return (1);
	}
	bus_add_card(&bus, aci_card);

	/* Put pattern in RAM */
	memcpy(bus.ram + from, pattern, size);

	cpu_init(&cpu, &bus);
	cpu_reset(&cpu);

	reset_kbd_queue();
	queue_wozmon_command("C100R");
	sprintf(cmd_buf, "%04X.%04XW", from, to);
	queue_wozmon_command(cmd_buf);

	/* Run WRITE routine until transition count stops growing */
	last_transition_count = 0;
	cycles_since_growth = 0;
	total_cycles = 0;
	slice_cycles = 50000;

	while (total_cycles < 200000000L) {
		current_slice = 0;
		while (current_slice < slice_cycles && cpu.halted == false) {
			bus_update_keyboard(&bus);
			c = cpu_step(&cpu);
			current_slice += c;

			for (i = 0; i < bus.num_cards; i++) {
				card = bus.cards[i];
				if (card->tick != NULL) {
					card->tick(card->ctx, c);
				}
			}
		}
		total_cycles += current_slice;

		now = aci_get_recorded_count(aci_card);
		if (now != last_transition_count) {
			last_transition_count = now;
			cycles_since_growth = 0;
		} else {
			cycles_since_growth += current_slice;
			if (now > 100 && cycles_since_growth > 5000000) {
				break;
			}
		}
	}

	printf("  Phase 1 Complete: Ran %ld cycles, captured %u "
	       "transitions.\n",
	    total_cycles,
	    last_transition_count);

	if (last_transition_count < 100) {
		fprintf(stderr,
		    "FAIL: ACI WRITE failed to record enough "
		    "transitions.\n");
		aci_free(aci_card);
		bus_free(&bus);
		return (1);
	}

	/* -------------------------------------------------------------
	 * PHASE 2: Save the recorded tape
	 * ------------------------------------------------------------- */
	printf("  Phase 2: Saving tape to '%s'...\n", tape_path);
	if (aci_save_tape(aci_card, tape_path) != PORT_OK) {
		fprintf(stderr, "FAIL: Failed to save recorded ACI tape.\n");
		aci_free(aci_card);
		bus_free(&bus);
		return (1);
	}

	aci_free(aci_card);
	bus_free(&bus);

	/* -------------------------------------------------------------
	 * PHASE 3: Read back the tape in a fresh system
	 * ------------------------------------------------------------- */
	printf("  Phase 3: Restoring tape in a fresh struct cpu/struct bus "
	       "instance...\n");
	if (bus_init(&read_bus, test_ram, 16 * 1024) != PORT_OK) {
		fprintf(stderr, "Failed to init read bus\n");
		unlink(tape_path);
		return (1);
	}
	read_bus.opts.uncapped = true;
	read_bus.opts.flat_bus = false;
	read_bus.opts.headless = true;

	if (bus_load_rom(&read_bus, "wozmon.bin") != PORT_OK) {
		fprintf(stderr, "Failed to load read wozmon.bin\n");
		bus_free(&read_bus);
		unlink(tape_path);
		return (1);
	}

	read_aci_card = aci_create(&read_bus, "roms/ACI.rom");
	if (read_aci_card == NULL) {
		fprintf(stderr, "Failed to create read ACI card\n");
		bus_free(&read_bus);
		unlink(tape_path);
		return (1);
	}
	bus_add_card(&read_bus, read_aci_card);

	if (aci_load_tape(read_aci_card, tape_path) != PORT_OK) {
		fprintf(stderr,
		    "FAIL: Failed to load ACI tape file '%s'\n",
		    tape_path);
		aci_free(read_aci_card);
		bus_free(&read_bus);
		unlink(tape_path);
		return (1);
	}

	memset(read_bus.ram + from, 0x00, size);

	cpu_init(&read_cpu, &read_bus);
	cpu_reset(&read_cpu);

	reset_kbd_queue();
	queue_wozmon_command("C100R");
	sprintf(cmd_buf, "%04X.%04XR", from, to);
	queue_wozmon_command(cmd_buf);

	total_cycles = 0;
	matched = false;
	read_slice = 50000;

	while (total_cycles < 200000000L) {
		current_slice = 0;
		while (current_slice < read_slice && read_cpu.halted == false) {
			bus_update_keyboard(&read_bus);
			c = cpu_step(&read_cpu);
			current_slice += c;

			for (i = 0; i < read_bus.num_cards; i++) {
				card = read_bus.cards[i];
				if (card->tick != NULL) {
					card->tick(card->ctx, c);
				}
			}
		}
		total_cycles += current_slice;

		if (memcmp(read_bus.ram + from, pattern, size) == 0) {
			matched = true;
			break;
		}
	}

	printf("  Phase 3 Complete: Ran %ld cycles, matched=%s\n",
	    total_cycles,
	    matched ? "yes" : "no");

	if (matched == false) {
		fprintf(stderr, "FAIL: Memory pattern mismatch after read.\n");
		aci_free(read_aci_card);
		bus_free(&read_bus);
		unlink(tape_path);
		return (1);
	}

	aci_free(read_aci_card);
	bus_free(&read_bus);
	unlink(tape_path);

	printf("ACI Save/Load Roundtrip Test (%s): PASS\n", tape_path);
	return (0);
}

int
main(void)
{
	const uint16_t from = 0x0300;
	const uint16_t to = 0x033F;
	const int size = (to - from) + 1;
	int i;
	uint8_t pattern[64];

	/* Pattern that avoids trivial constant runs */
	for (i = 0; i < size; i++) {
		pattern[i] = (uint8_t)((0xA5 ^ (i * 37)) & 0xFF);
	}

	/* WAV roundtrip test (only supported format) */
	if (run_roundtrip_test("/tmp/test_tape.wav", pattern, size, from, to) !=
	    0) {
		return (1);
	}

	printf("All ACI roundtrip tests completed successfully!\n");
	return (0);
}
