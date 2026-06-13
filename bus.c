#include "bus.h"
#include "io.h"
#include "embedded_roms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
delay_nanoseconds(long ns)
{
	struct timespec start, current;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &current);
		long elapsed = (current.tv_sec - start.tv_sec) * 1000000000L +
			(current.tv_nsec - start.tv_nsec);
		if (elapsed >= ns) {
			break;
		}
	}
}

static inline bool
bus_is_ram_address(Bus *bus, uint16_t address)
{
	if (bus->ram_size == 8192) {
		// 8KB split mode: 0x0000-0x0FFF and 0xE000-0xEFFF
		if (address < 0x1000) return true;
		if (address >= 0xE000 && address < 0xF000) return true;
		return false;
	}
	return (uint32_t)address < bus->ram_size;
}

bool
bus_init(Bus *bus, uint32_t ram_size)
{
	memset(bus, 0, sizeof(Bus));
	bus->ram_size = ram_size;
	bus->ram = malloc(65536);
	if (!bus->ram) {
		perror("Failed to allocate RAM");
		return false;
	}
	// Set default options
	bus->opts.uncapped = true;
	bus->opts.throttle_pia = true;
	bus->opts.emulate_dram_refresh = false;
	bus->opts.emulate_kbd_bounce = false;
	bus->opts.randomize_cold_boot = false;
	bus->opts.flat_bus = false;
	bus->opts.headless = false;

	memset(bus->ram, 0, 65536);
	// Default display control (bit 7 set = ready)
	bus->pia.dsp_control = 0x80;

	return true;
}

void
bus_free(Bus *bus)
{
	if (bus->ram) {
		free(bus->ram);
		bus->ram = NULL;
	}
}

bool
bus_load_rom(Bus *bus, const char *rom_path)
{
	if (!rom_path) {
		memcpy(bus->rom, embedded_wozmon, 256);
		bus->rom_loaded = true;
		return true;
	}

	FILE *f = fopen(rom_path, "rb");

	if (!f) {
		fprintf(stderr,
			"Warning: '%s' not found, using embedded Wozmon ROM.\n",
			rom_path);
		memcpy(bus->rom, embedded_wozmon, 256);
		bus->rom_loaded = true;
		return true;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);

	fseek(f, 0, SEEK_SET);

	if (size != 256) {
		fprintf(stderr,
			"Error: ROM file '%s' is %ld bytes. Apple 1 Monitor "
			"ROM must be exactly 256 bytes.\n",
			rom_path,
			size);
		fclose(f);
		return false;
	}

	size_t read_bytes = fread(bus->rom, 1, 256, f);

	fclose(f);

	if (read_bytes != 256) {
		fprintf(stderr,
			"Error: Failed to read 256 bytes from '%s'\n",
			rom_path);
		return false;
	}

	bus->rom_loaded = true;
	return true;
}

bool
bus_load_bin(Bus *bus, const char *bin_path, uint16_t address)
{
	FILE *f = fopen(bin_path, "rb");

	if (!f) {
		fprintf(stderr,
			"Error: Could not open binary file '%s'\n",
			bin_path);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);

	fseek(f, 0, SEEK_SET);

	if (size <= 0) {
		fprintf(stderr,
			"Error: Binary file '%s' is empty.\n",
			bin_path);
		fclose(f);
		return false;
	}

	for (uint32_t addr = address; addr < (uint32_t)address + size; addr++) {
		if (addr > 0xFFFF || !bus_is_ram_address(bus, (uint16_t)addr)) {
			fprintf(stderr,
				"Error: Binary file '%s' (size %ld bytes) loaded at "
				"0x%04X exceeds configured RAM size (%u KB).\n",
				bin_path,
				size,
				address,
				bus->ram_size / 1024);
			fclose(f);
			return false;
		}
	}

	size_t read_bytes = fread(bus->ram + address, 1, size, f);

	fclose(f);

	if (read_bytes != (size_t)size) {
		fprintf(stderr,
			"Error: Failed to read entire binary file '%s'\n",
			bin_path);
		return false;
	}

	printf("Loaded '%s' (%ld bytes) into RAM at 0x%04X - 0x%04X\n",
		bin_path,
		size,
		address,
		(uint16_t)(address + size - 1));
	return true;
}

static uint8_t
pia_read(Bus *bus, uint16_t address)
{
	switch (address) {
	case 0xD010: {
		if (bus->pia.kbd_control & 0x04) {
			uint8_t data = bus->pia.kbd_data;

			// Reading Keyboard Data clears the keyboard strobe
			bus->pia.kbd_control &= ~0x80;
			return data;
		} else {
			// Accessing DDRB (DDR for Port A/Keyboard)
			return 0x00;
		}
	}
	case 0xD011:
		return bus->pia.kbd_control;
	case 0xD012:
		// Always return 0x00 so that Wozmon doesn't hang in a busy loop
		return 0x00;
	case 0xD013:
		return bus->pia.dsp_control;
	}
	return 0x00;
}

static void
pia_write(Bus *bus, uint16_t address, uint8_t value, bool is_dummy)
{
	switch (address) {
	case 0xD010:
		// Keyboard data is read-only
		break;
	case 0xD011:
		if (is_dummy) {
			// DCP $D011 dummy write clears key-ready bit (bit 7)
			bus->pia.kbd_control &= ~0x80;
		} else {
			// Real writes cannot modify bit 7 (status flag is read-only)
			bus->pia.kbd_control = (bus->pia.kbd_control & 0x80) |
				(value & 0x7F);
		}
		break;
	case 0xD012:
		if (bus->pia.dsp_control & 0x04) {
			if (is_dummy) {
				// SLO $D012 dummy write triggers display ready
				bus->pia.dsp_control |= 0x80;
			} else {
				bus->pia.dsp_data = value;
				io_write_display(value);
			}
		} else {
			// Accessing DDRA (DDR for Port B/Display)
		}
		break;
	case 0xD013:
		// Real writes cannot modify bit 7 (status flag is read-only)
		bus->pia.dsp_control = (bus->pia.dsp_control & 0x80) |
			(value & 0x7F);
		break;
	}
}

uint8_t
bus_read(Bus *bus, uint16_t address)
{
	if (bus->opts.flat_bus) {
		bus->last_bus_value = bus->ram[address];
		return bus->last_bus_value;
	}

	// PIA 6821 alias: the 74154 decoder selects the PIA for the full $D000-$DFFF range.
	// Only address lines A0-A1 are connected to the PIA's RS0-RS1 register select pins.
	if ((address & 0xF000) == 0xD000) {
		address = 0xD010 | (address & 0x03);
	}

	if (bus->opts.uncapped && bus->opts.throttle_pia &&
		!bus->opts.headless && address >= 0xD010 && address <= 0xD013) {
		delay_nanoseconds(977);
	}
	// 1. ROM mapped to 0xFF00 - 0xFFFF (256 bytes)
	if (address >= 0xFF00) {
		if (bus->rom_loaded) {
			bus->last_bus_value = bus->rom[address - 0xFF00];
		}
		return bus->last_bus_value;
	}
	// 2. PIA 6821 registers mapped to 0xD010 - 0xD013
	if (address >= 0xD010 && address <= 0xD013) {
		bus->last_bus_value = pia_read(bus, address);
		return bus->last_bus_value;
	}
	// 3. Expansion cards (checked before RAM so cards always win regardless
	//    of RAM size — matches real hardware address decoding priority).
	for (int i = 0; i < bus->num_cards; i++) {
		expansion_card_t *card = bus->cards[i];

		if ((address & card->mask) == card->base) {
			if (card->read) {
				bus->last_bus_value =
					card->read(card->ctx, address, false);
			}
			return bus->last_bus_value;
		}
	}
	// 4. RAM
	if (bus_is_ram_address(bus, address)) {
		bus->last_bus_value = bus->ram[address];
		return bus->last_bus_value;
	}
	// Open bus behavior
	return bus->last_bus_value;
}

void
bus_write(Bus *bus, uint16_t address, uint8_t value)
{
	// Calls standard write with is_dummy = false
	bus_write_ext(bus, address, value, false);
}

// Overloaded write function supporting dummy writes
void
bus_write_ext(Bus *bus, uint16_t address, uint8_t value, bool is_dummy)
{
	bus->last_bus_value = value;

	if (bus->opts.flat_bus) {
		bus->ram[address] = value;
		return;
	}

	// PIA 6821 alias: the 74154 decoder selects the PIA for the full $D000-$DFFF range.
	// Only address lines A0-A1 are connected to the PIA's RS0-RS1 register select pins.
	if ((address & 0xF000) == 0xD000) {
		address = 0xD010 | (address & 0x03);
	}

	if (bus->opts.uncapped && bus->opts.throttle_pia &&
		!bus->opts.headless && address >= 0xD010 && address <= 0xD013) {
		delay_nanoseconds(977);
	}
	// 1. ROM is read-only
	if (address >= 0xFF00) {
		return;
	}
	// 2. PIA 6821
	if (address >= 0xD010 && address <= 0xD013) {
		pia_write(bus, address, value, is_dummy);
		return;
	}
	// 3. Expansion cards (checked before RAM — same priority rationale as read).
	for (int i = 0; i < bus->num_cards; i++) {
		expansion_card_t *card = bus->cards[i];

		if ((address & card->mask) == card->base) {
			if (!card->rom_only && card->write) {
				card->write(card->ctx, address, value, is_dummy);
			}
			return;
		}
	}
	// 4. RAM
	if (bus_is_ram_address(bus, address)) {
		bus->ram[address] = value;
		return;
	}
}

void
bus_update_keyboard(Bus *bus)
{
	bool key_avail = io_check_keyboard();

	/*
	 * Keyboard bounce emulation (hardware-accurate):
	 *
	 * On a real Apple-1, the keyboard (typically a Datanetics unit) used a
	 * National Semiconductor MM5740 encoder chip. The MM5740 has built-in
	 * debounce circuitry: it scans the key matrix, waits for contacts to
	 * stabilise, and only then asserts a single clean strobe pulse on its
	 * output. The Apple-1 PIA's CA1 input is rising-edge sensitive, so it
	 * latches exactly one keypress per strobe.
	 *
	 * The previous model was wrong — it randomly toggled the strobe bit,
	 * creating new rising edges that the CPU saw as extra keypresses. Real
	 * hardware never does this; bounce is absorbed inside the encoder before
	 * the strobe line is touched.
	 *
	 * We now model this as a lockout window (≈5–15 ms) after each keypress
	 * during which the encoder is still internally "settling" and will not
	 * assert a new strobe. New keys are simply ignored in this window. The
	 * strobe bit is left exactly as the CPU last left it (cleared after the
	 * $D010 read) — no toggling, no fake rising edges.
	 */
	if (bus->opts.emulate_kbd_bounce && bus->kbd_bounce_cycles > 0) {
		bus->kbd_bounce_cycles--;
		return; /* encoder still settling — ignore new input */
	}

	/* Check if keyboard strobe is clear */
	if (!(bus->pia.kbd_control & 0x80)) {
		if (key_avail) {
			uint8_t key = io_read_keyboard();

			if (key != 0) {
				bus->pia.kbd_data = key;
				bus->pia.kbd_control |= 0x80;
				/*
				 * Start lockout window: 5–15 ms at ~1 MHz
				 * ≈ 5 000–15 000 cycles. bus_update_keyboard is
				 * called once per cpu_step (~2–7 cycles on
				 * average), so divide by ~4 for call count.
				 * Result: ~1 250–3 750 calls ≈ realistic MM5740
				 * inter-key settle time.
				 */
				if (bus->opts.emulate_kbd_bounce) {
					bus->kbd_bounce_cycles =
						1250 + (uint32_t)(rand() % 2500);
				}
			}
		}
	}
}

void
bus_reset(Bus *bus)
{
	bus->pia.kbd_data = 0x00;
	bus->pia.kbd_control = 0x00;
	bus->pia.dsp_data = 0x00;
	bus->pia.dsp_control = 0x80; /* Default ready */
	bus->kbd_bounce_cycles = 0;
}

void
bus_add_card(Bus *bus, expansion_card_t *card)
{
	if (bus->num_cards < 8) {
		bus->cards[bus->num_cards++] = card;
	} else {
		fprintf(stderr,
			"Error: Maximum number of expansion cards (8) "
			"exceeded.\n");
	}
}
