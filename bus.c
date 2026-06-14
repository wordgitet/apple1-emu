#include "bus.h"
#include "io.h"
#include "embedded_roms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

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

static inline int
hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return 0;
}

bool
bus_load_wozmon_txt(Bus *bus, const char *txt_path, uint16_t *run_address, bool *has_run_address)
{
	FILE *f = fopen(txt_path, "r");
	if (!f) {
		fprintf(stderr, "Error: Could not open text file '%s'\n", txt_path);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0) {
		fprintf(stderr, "Error: Text file '%s' is empty.\n", txt_path);
		fclose(f);
		return false;
	}

	char *content = malloc(size + 1);
	if (!content) {
		fclose(f);
		return false;
	}

	size_t read_bytes = fread(content, 1, size, f);
	content[read_bytes] = '\0';
	fclose(f);

	// Strip inline comments
	char *cleaned = malloc(size + 1);
	if (!cleaned) {
		free(content);
		return false;
	}

	size_t w = 0;
	bool in_comment = false;
	for (size_t i = 0; i < read_bytes; i++) {
		if (content[i] == '\n' || content[i] == '\r') {
			in_comment = false;
			cleaned[w++] = content[i];
			continue;
		}
		if (in_comment) continue;

		// Start of comment
		if (content[i] == '#' || content[i] == ';') {
			in_comment = true;
			continue;
		}
		if (content[i] == '/' && i + 1 < read_bytes && content[i+1] == '/') {
			in_comment = true;
			continue;
		}
		cleaned[w++] = content[i];
	}
	cleaned[w] = '\0';
	free(content);

	uint16_t current_addr = 0;
	bool first_addr = true;
	int total_bytes = 0;
	size_t i = 0;

	if (has_run_address) *has_run_address = false;

	while (i < w) {
		char c = cleaned[i];

		if (isspace((unsigned char)c)) { i++; continue; }

		// 'T' prefix (turbo) - skip
		if (toupper((unsigned char)c) == 'T' && i + 1 < w && isxdigit((unsigned char)cleaned[i + 1])) {
			i++; continue;
		}

		// 'X' marker - skip X + hex addr
		if (toupper((unsigned char)c) == 'X' && i + 1 < w && isxdigit((unsigned char)cleaned[i + 1])) {
			i++;
			while (i < w && isxdigit((unsigned char)cleaned[i])) i++;
			continue;
		}

		if (c == ':') { i++; continue; }

		if (isxdigit((unsigned char)c)) {
			size_t hex_start = i;
			while (i < w && isxdigit((unsigned char)cleaned[i])) i++;
			size_t hex_len = i - hex_start;

			size_t peek = i;
			while (peek < w && isspace((unsigned char)cleaned[peek])) peek++;

			if (i < w && toupper((unsigned char)cleaned[i]) == 'R') {
				// run command
				size_t data_len = hex_len > 4 ? hex_len - 4 : 0;
				for (size_t j = 0; j + 1 < data_len; j += 2) {
					uint8_t val = (hex_val(cleaned[hex_start + j]) << 4) | hex_val(cleaned[hex_start + j + 1]);
					if (bus_is_ram_address(bus, current_addr)) {
						bus->ram[current_addr] = val;
					}
					current_addr++;
					total_bytes++;
				}
				if (hex_len >= 4) {
					char addr_str[5];
					strncpy(addr_str, &cleaned[hex_start + data_len], 4);
					addr_str[4] = '\0';
					uint16_t raddr = (uint16_t)strtol(addr_str, NULL, 16);
					if (run_address) *run_address = raddr;
					if (has_run_address) *has_run_address = true;
				}
				i++; // skip R
				continue;
			}

			if (peek < w && cleaned[peek] == ':' && hex_len >= 3) {
				// Address change (and possibly some data bytes before it in merged strings)
				size_t data_len = hex_len > 4 ? hex_len - 4 : 0;
				for (size_t j = 0; j + 1 < data_len; j += 2) {
					uint8_t val = (hex_val(cleaned[hex_start + j]) << 4) | hex_val(cleaned[hex_start + j + 1]);
					if (bus_is_ram_address(bus, current_addr)) {
						bus->ram[current_addr] = val;
					}
					current_addr++;
					total_bytes++;
				}
				char addr_str[5] = {0};
				size_t addr_digits = hex_len > 4 ? 4 : hex_len;
				strncpy(addr_str, &cleaned[hex_start + hex_len - addr_digits], addr_digits);
				current_addr = (uint16_t)strtol(addr_str, NULL, 16);
				if (first_addr) {
					first_addr = false;
				}
				i = peek + 1; // skip ':'
				continue;
			}

			// Data bytes
			for (size_t j = 0; j + 1 < hex_len; j += 2) {
				uint8_t val = (hex_val(cleaned[hex_start + j]) << 4) | hex_val(cleaned[hex_start + j + 1]);
				if (bus_is_ram_address(bus, current_addr)) {
					bus->ram[current_addr] = val;
				} else {
					// Also print warning if write exceeds bounds or write is not to RAM?
				}
				current_addr++;
				total_bytes++;
			}
			continue;
		}

		i++;
	}

	free(cleaned);

	if (total_bytes == 0 && first_addr) {
		// Nothing loaded, completely invalid format or empty
		return false;
	}

	printf("Loaded '%s' (%d bytes) via Woz Monitor text format\n", txt_path, total_bytes);
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
	uint8_t result = bus->last_bus_value; /* default: open-bus float */

	if (bus->opts.flat_bus) {
		result = bus->ram[address];
		bus->last_bus_value = result;
	} else {
		/* PIA 6821 alias: only A0-A1 reach the chip's RS pins. */
		if ((address & 0xF000) == 0xD000)
			address = 0xD010 | (address & 0x03);

		if (bus->opts.uncapped && bus->opts.throttle_pia &&
		    !bus->opts.headless &&
		    address >= 0xD010 && address <= 0xD013)
			delay_nanoseconds(977);

		if (address >= 0xFF00) {
			if (bus->rom_loaded)
				bus->last_bus_value = bus->rom[address - 0xFF00];
			result = bus->last_bus_value;
		} else if (address >= 0xD010 && address <= 0xD013) {
			result = pia_read(bus, address);
			bus->last_bus_value = result;
		} else {
			bool card_hit = false;
			for (int i = 0; i < bus->num_cards; i++) {
				expansion_card_t *card = bus->cards[i];
				if ((address & card->mask) == card->base) {
					if (card->read)
						bus->last_bus_value =
							card->read(card->ctx, address, false);
					result = bus->last_bus_value;
					card_hit = true;
					break;
				}
			}
			if (!card_hit) {
				if (bus_is_ram_address(bus, address))
					bus->last_bus_value = bus->ram[address];
				result = bus->last_bus_value;
			}
		}
	}

	if (bus->access_cb)
		bus->access_cb(bus->access_cb_ctx, address, false, result);
	return result;
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
	} else {
		/* PIA 6821 alias: only A0-A1 reach the chip's RS pins. */
		if ((address & 0xF000) == 0xD000)
			address = 0xD010 | (address & 0x03);

		if (bus->opts.uncapped && bus->opts.throttle_pia &&
		    !bus->opts.headless &&
		    address >= 0xD010 && address <= 0xD013)
			delay_nanoseconds(977);

		if (address >= 0xFF00) {
			/* ROM is read-only — silently ignore */
		} else if (address >= 0xD010 && address <= 0xD013) {
			pia_write(bus, address, value, is_dummy);
		} else {
			bool card_hit = false;
			for (int i = 0; i < bus->num_cards; i++) {
				expansion_card_t *card = bus->cards[i];
				if ((address & card->mask) == card->base) {
					if (!card->rom_only && card->write)
						card->write(card->ctx, address, value,
						            is_dummy);
					card_hit = true;
					break;
				}
			}
			if (!card_hit && bus_is_ram_address(bus, address))
				bus->ram[address] = value;
		}
	}

	/* Fire watchpoint callback for real (non-phantom) writes only */
	if (!is_dummy && bus->access_cb)
		bus->access_cb(bus->access_cb_ctx, address, true, value);
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
