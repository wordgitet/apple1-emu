#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "embedded_roms.h"
#include "io.h"

static void
delay_nanoseconds(long ns)
{
	struct timespec current, start;
	long elapsed;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &current);
		elapsed = (current.tv_sec - start.tv_sec) * 1000000000L +
		    (current.tv_nsec - start.tv_nsec);
		if (elapsed >= ns) {
			break;
		}
	}
}

static inline bool
bus_is_ram_address(struct bus *bus, uint16_t address)
{
	if (bus->ram_size == 8192) {
		/* 8KB split mode: 0x0000-0x0FFF and 0xE000-0xEFFF */
		if (address < 0x1000)
			return (true);
		if (address >= 0xE000 && address < 0xF000)
			return (true);
		return (false);
	}
	return ((uint32_t)address < bus->ram_size);
}

bool
bus_init(struct bus *bus, uint8_t *ram_buf, uint32_t ram_size)
{
	memset(bus, 0, sizeof(struct bus));
	bus->ram_size = ram_size;
	bus->ram = ram_buf;
	/* Set default options */
	bus->opts.uncapped = true;
	bus->opts.throttle_pia = true;
	bus->opts.emulate_dram_refresh = false;
	bus->opts.emulate_kbd_bounce = false;
	bus->opts.randomize_cold_boot = false;
	bus->opts.flat_bus = false;
	bus->opts.headless = false;

	memset(bus->ram, 0, ram_size);
	/* Default display control (bit 7 set = ready) */
	bus->pia.dsp_control = 0x80;

	return (true);
}

void
bus_free(struct bus *bus)
{
	bus->ram = NULL;
}

bool
bus_load_rom(struct bus *bus, const char *rom_path)
{
	FILE *f;
	size_t read_bytes;
	long size;

	if (rom_path == NULL) {
		memcpy(bus->rom, embedded_wozmon, 256);
		bus->rom_loaded = true;
		return (true);
	}

	f = fopen(rom_path, "rb");
	if (f == NULL) {
		fprintf(stderr,
		    "Warning: '%s' not found, using embedded Wozmon ROM.\n",
		    rom_path);
		memcpy(bus->rom, embedded_wozmon, 256);
		bus->rom_loaded = true;
		return (true);
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size != 256) {
		fprintf(stderr,
		    "Error: ROM file '%s' is %ld bytes. Apple 1 Monitor "
		    "ROM must be exactly 256 bytes.\n",
		    rom_path,
		    size);
		fclose(f);
		return (false);
	}

	read_bytes = fread(bus->rom, 1, 256, f);
	fclose(f);

	if (read_bytes != 256) {
		fprintf(stderr,
		    "Error: Failed to read 256 bytes from '%s'\n",
		    rom_path);
		return (false);
	}

	bus->rom_loaded = true;
	return (true);
}

bool
bus_load_bin(struct bus *bus, const char *bin_path, uint16_t address)
{
	FILE *f;
	size_t read_bytes;
	long size;
	uint32_t addr;

	f = fopen(bin_path, "rb");
	if (f == NULL) {
		fprintf(stderr,
		    "Error: Could not open binary file '%s'\n",
		    bin_path);
		return (false);
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0) {
		fprintf(stderr,
		    "Error: Binary file '%s' is empty.\n",
		    bin_path);
		fclose(f);
		return (false);
	}

	for (addr = address; addr < (uint32_t)address + size; addr++) {
		if (addr > 0xFFFF ||
		    bus_is_ram_address(bus, (uint16_t)addr) == 0) {
			fprintf(stderr,
			    "Error: Binary file '%s' (size %ld bytes) "
			    "loaded at "
			    "0x%04X exceeds configured RAM size (%u KB).\n",
			    bin_path,
			    size,
			    address,
			    bus->ram_size / 1024);
			fclose(f);
			return (false);
		}
	}

	read_bytes = fread(bus->ram + address, 1, size, f);
	fclose(f);

	if (read_bytes != (size_t)size) {
		fprintf(stderr,
		    "Error: Failed to read entire binary file '%s'\n",
		    bin_path);
		return (false);
	}

	printf("Loaded '%s' (%ld bytes) into RAM at 0x%04X - 0x%04X\n",
	    bin_path,
	    size,
	    address,
	    (uint16_t)(address + size - 1));
	return (true);
}

static inline int
hex_val(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	return (0);
}

bool
bus_load_wozmon_txt(struct bus *bus,
    const char *txt_path,
    uint16_t *run_address,
    bool *has_run_address)
{
	char *cleaned, *content;
	size_t read_bytes, w;
	long size;
	uint16_t current_addr;
	int total_bytes;
	bool first_addr, in_comment;
	size_t i;
	FILE *f;

	f = fopen(txt_path, "r");
	if (f == NULL) {
		fprintf(stderr,
		    "Error: Could not open text file '%s'\n",
		    txt_path);
		return (false);
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size <= 0) {
		fprintf(stderr, "Error: Text file '%s' is empty.\n", txt_path);
		fclose(f);
		return (false);
	}

	content = malloc(size + 1);
	if (content == NULL) {
		fclose(f);
		return (false);
	}

	read_bytes = fread(content, 1, size, f);
	content[read_bytes] = '\0';
	fclose(f);

	/* Strip inline comments */
	cleaned = malloc(size + 1);
	if (cleaned == NULL) {
		free(content);
		return (false);
	}

	w = 0;
	in_comment = false;
	for (i = 0; i < read_bytes; i++) {
		if (content[i] == '\n' || content[i] == '\r') {
			in_comment = false;
			cleaned[w++] = content[i];
			continue;
		}
		if (in_comment != 0)
			continue;

		/* Start of comment */
		if (content[i] == '#' || content[i] == ';') {
			in_comment = true;
			continue;
		}
		if (content[i] == '/' && i + 1 < read_bytes &&
		    content[i + 1] == '/') {
			in_comment = true;
			continue;
		}
		cleaned[w++] = content[i];
	}
	cleaned[w] = '\0';
	free(content);

	current_addr = 0;
	first_addr = true;
	total_bytes = 0;
	i = 0;

	if (has_run_address != NULL)
		*has_run_address = false;

	while (i < w) {
		char c = cleaned[i];

		if (isspace((unsigned char)c)) {
			i++;
			continue;
		}

		/* 'T' prefix (turbo) - skip */
		if (toupper((unsigned char)c) == 'T' && i + 1 < w &&
		    isxdigit((unsigned char)cleaned[i + 1])) {
			i++;
			continue;
		}

		/* 'X' marker - skip X + hex addr */
		if (toupper((unsigned char)c) == 'X' && i + 1 < w &&
		    isxdigit((unsigned char)cleaned[i + 1])) {
			i++;
			while (i < w && isxdigit((unsigned char)cleaned[i]))
				i++;
			continue;
		}

		if (c == ':') {
			i++;
			continue;
		}

		if (isxdigit((unsigned char)c)) {
			size_t hex_start = i;
			while (i < w && isxdigit((unsigned char)cleaned[i]))
				i++;
			size_t hex_len = i - hex_start;

			size_t peek = i;
			while (
			    peek < w && isspace((unsigned char)cleaned[peek]))
				peek++;

			if (i < w &&
			    toupper((unsigned char)cleaned[i]) == 'R') {
				/* run command */
				size_t data_len = hex_len > 4 ? hex_len - 4 : 0;
				for (size_t j = 0; j + 1 < data_len; j += 2) {
					uint8_t val =
					    (hex_val(cleaned[hex_start + j])
						<< 4) |
					    hex_val(cleaned[hex_start + j + 1]);
					if (bus_is_ram_address(bus,
						current_addr) != 0) {
						bus->ram[current_addr] = val;
					}
					current_addr++;
					total_bytes++;
				}
				if (hex_len >= 4) {
					char addr_str[5];
					strncpy(addr_str,
					    &cleaned[hex_start + data_len],
					    4);
					addr_str[4] = '\0';
					uint16_t raddr = (uint16_t)
					    strtol(addr_str, NULL, 16);
					if (run_address != NULL)
						*run_address = raddr;
					if (has_run_address != NULL)
						*has_run_address = true;
				}
				i++; /* skip R */
				continue;
			}

			if (peek < w && cleaned[peek] == ':' && hex_len >= 3) {
				/* Address change (and possibly some data bytes before it in merged strings) */
				size_t data_len = hex_len > 4 ? hex_len - 4 : 0;
				for (size_t j = 0; j + 1 < data_len; j += 2) {
					uint8_t val =
					    (hex_val(cleaned[hex_start + j])
						<< 4) |
					    hex_val(cleaned[hex_start + j + 1]);
					if (bus_is_ram_address(bus,
						current_addr) != 0) {
						bus->ram[current_addr] = val;
					}
					current_addr++;
					total_bytes++;
				}
				char addr_str[5] = { 0 };
				size_t addr_digits = hex_len > 4 ? 4 : hex_len;
				strncpy(addr_str,
				    &cleaned[hex_start + hex_len - addr_digits],
				    addr_digits);
				current_addr =
				    (uint16_t)strtol(addr_str, NULL, 16);
				if (first_addr != 0) {
					first_addr = false;
				}
				i = peek + 1; /* skip ':' */
				continue;
			}

			/* Data bytes */
			for (size_t j = 0; j + 1 < hex_len; j += 2) {
				uint8_t val =
				    (hex_val(cleaned[hex_start + j]) << 4) |
				    hex_val(cleaned[hex_start + j + 1]);
				if (bus_is_ram_address(bus, current_addr) != 0) {
					bus->ram[current_addr] = val;
				} else {
					/* write exceeds bounds or write is not to RAM */
				}
				current_addr++;
				total_bytes++;
			}
			continue;
		}

		i++;
	}

	free(cleaned);

	if (total_bytes == 0 && first_addr != 0) {
		/* Nothing loaded, completely invalid format or empty */
		return (false);
	}

	printf("Loaded '%s' (%d bytes) via Woz Monitor text format\n",
	    txt_path,
	    total_bytes);
	return (true);
}

static uint8_t
pia_read(struct bus *bus, uint16_t address)
{
	switch (address) {
	case PIA_BASE: {
		if (bus->pia.kbd_control & 0x04) {
			uint8_t data = bus->pia.kbd_data;

			/* Reading Keyboard Data clears the keyboard strobe */
			bus->pia.kbd_control &= ~0x80;
			return (data);
		} else {
			/* Accessing DDRB (DDR for Port A/Keyboard) */
			return (0x00);
		}
	}
	case PIA_BASE + 1:
		return (bus->pia.kbd_control);
	case PIA_BASE + 2:
		/* Always return 0x00 so that Wozmon doesn't hang in a busy loop */
		return (0x00);
	case PIA_BASE + 3:
		return (bus->pia.dsp_control);
	}
	return (0x00);
}

static void
pia_write(struct bus *bus, uint16_t address, uint8_t value, bool is_dummy)
{
	switch (address) {
	case PIA_BASE:
		/* Keyboard data is read-only */
		break;
	case PIA_BASE + 1:
		if (is_dummy != 0) {
			/* DCP $D011 dummy write clears key-ready bit (bit 7) */
			bus->pia.kbd_control &= ~0x80;
		} else {
			/* Real writes cannot modify bit 7 (status flag is read-only) */
			bus->pia.kbd_control = (bus->pia.kbd_control & 0x80) |
			    (value & 0x7F);
		}
		break;
	case PIA_BASE + 2:
		if (bus->pia.dsp_control & 0x04) {
			if (is_dummy != 0) {
				/* SLO $D012 dummy write triggers display ready */
				bus->pia.dsp_control |= 0x80;
			} else {
				bus->pia.dsp_data = value;
				io_write_display(value);
			}
		} else {
			/* Accessing DDRA (DDR for Port B/Display) */
		}
		break;
	case PIA_BASE + 3:
		/* Real writes cannot modify bit 7 (status flag is read-only) */
		bus->pia.dsp_control = (bus->pia.dsp_control & 0x80) |
		    (value & 0x7F);
		break;
	}
}

uint8_t
bus_read(struct bus *bus, uint16_t address)
{
	uint8_t result = bus->last_bus_value; /* default: open-bus float */

	if (bus->opts.flat_bus != 0) {
		result = bus->ram[address];
		bus->last_bus_value = result;
	} else {
		/* PIA 6821 alias: only A0-A1 reach the chip's RS pins. */
		if ((address & 0xF000) == 0xD000)
			address = PIA_BASE | (address & 0x03);

		if (bus->opts.uncapped != 0 && bus->opts.throttle_pia != 0 &&
		    bus->opts.headless == 0 && address >= PIA_BASE &&
		    address <= (PIA_BASE + 3))
			delay_nanoseconds(977);

		if (address >= ROM_BASE) {
			if (bus->rom_loaded != 0)
				bus->last_bus_value =
				    bus->rom[address - ROM_BASE];
			result = bus->last_bus_value;
		} else if (address >= PIA_BASE && address <= (PIA_BASE + 3)) {
			result = pia_read(bus, address);
			bus->last_bus_value = result;
		} else {
			bool card_hit = false;
			for (int i = 0; i < bus->num_cards; i++) {
				struct expansion_card *card = bus->cards[i];
				if ((address & card->mask) == card->base) {
					if (card->read != NULL)
						bus->last_bus_value =
						    card->read(card->ctx,
							address,
							false);
					result = bus->last_bus_value;
					card_hit = true;
					break;
				}
			}
			if (card_hit == 0) {
				if (bus_is_ram_address(bus, address) != 0)
					bus->last_bus_value = bus->ram[address];
				result = bus->last_bus_value;
			}
		}
	}

	if (bus->access_cb != NULL)
		bus->access_cb(bus->access_cb_ctx, address, false, result);
	return (result);
}

void
bus_write(struct bus *bus, uint16_t address, uint8_t value)
{
	/* Calls standard write with is_dummy = false */
	bus_write_ext(bus, address, value, false);
}

/* Overloaded write function supporting dummy writes */
void
bus_write_ext(struct bus *bus, uint16_t address, uint8_t value, bool is_dummy)
{
	bus->last_bus_value = value;

	if (bus->opts.flat_bus != 0) {
		bus->ram[address] = value;
	} else {
		/* PIA 6821 alias: only A0-A1 reach the chip's RS pins. */
		if ((address & 0xF000) == 0xD000)
			address = PIA_BASE | (address & 0x03);

		if (bus->opts.uncapped != 0 && bus->opts.throttle_pia != 0 &&
		    bus->opts.headless == 0 && address >= PIA_BASE &&
		    address <= (PIA_BASE + 3))
			delay_nanoseconds(977);

		if (address >= ROM_BASE) {
			/* ROM is read-only - silently ignore */
		} else if (address >= PIA_BASE && address <= (PIA_BASE + 3)) {
			pia_write(bus, address, value, is_dummy);
		} else {
			bool card_hit = false;
			for (int i = 0; i < bus->num_cards; i++) {
				struct expansion_card *card = bus->cards[i];
				if ((address & card->mask) == card->base) {
					if (card->rom_only == 0 &&
					    card->write != NULL)
						card->write(card->ctx,
						    address,
						    value,
						    is_dummy);
					card_hit = true;
					break;
				}
			}
			if (card_hit == 0 &&
			    bus_is_ram_address(bus, address) != 0)
				bus->ram[address] = value;
		}
	}

	/* Fire watchpoint callback for real (non-phantom) writes only
	 * (unless flat_bus is enabled) */
	if ((is_dummy == 0 || bus->opts.flat_bus != 0) &&
	    bus->access_cb != NULL)
		bus->access_cb(bus->access_cb_ctx, address, true, value);
}

void
bus_update_keyboard(struct bus *bus)
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
	 * The previous model was wrong - it randomly toggled the strobe bit,
	 * creating new rising edges that the struct cpu saw as extra keypresses. Real
	 * hardware never does this; bounce is absorbed inside the encoder before
	 * the strobe line is touched.
	 *
	 * We now model this as a lockout window (~5-15 ms) after each keypress
	 * during which the encoder is still internally "settling" and will not
	 * assert a new strobe. New keys are simply ignored in this window. The
	 * strobe bit is left exactly as the struct cpu last left it (cleared after the
	 * $D010 read) - no toggling, no fake rising edges.
	 */
	if (bus->opts.emulate_kbd_bounce != 0 &&
	    bus->kbd_bounce_cycles > 0) {
		bus->kbd_bounce_cycles--;
		return; /* encoder still settling - ignore new input */
	}

	/* Check if keyboard strobe is clear */
	if ((bus->pia.kbd_control & 0x80) == 0) {
		if (key_avail != 0) {
			uint8_t key = io_read_keyboard();

			if (key != 0) {
				bus->pia.kbd_data = key;
				bus->pia.kbd_control |= 0x80;
				/*
				 * Start lockout window: 5-15 ms at ~1 MHz
				 * = 5 000-15 000 cycles. bus_update_keyboard is
				 * called once per cpu_step (~2-7 cycles on
				 * average), so divide by ~4 for call count.
				 * Result: ~1 250-3 750 calls = realistic MM5740
				 * inter-key settle time.
				 */
				if (bus->opts.emulate_kbd_bounce != 0) {
					bus->kbd_bounce_cycles = 1250 +
					    (uint32_t)(rand() % 2500);
				}
			}
		}
	}
}

void
bus_reset(struct bus *bus)
{
	bus->pia.kbd_data = 0x00;
	bus->pia.kbd_control = 0x00;
	bus->pia.dsp_data = 0x00;
	bus->pia.dsp_control = 0x80; /* Default ready */
	bus->kbd_bounce_cycles = 0;
}

void
bus_add_card(struct bus *bus, struct expansion_card *card)
{
	if (bus->num_cards < 8) {
		bus->cards[bus->num_cards++] = card;
	} else {
		fprintf(stderr,
		    "Error: Maximum number of expansion cards (8) "
		    "exceeded.\n");
	}
}
