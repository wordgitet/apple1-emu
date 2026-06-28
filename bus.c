#include "bus.h"
#include "embedded_roms.h"
#include "io.h"
#include "port.h"
#include "term_apple1.h"

static void
delay_microseconds(long us)
{
	uint32_t now, start;

	start = port_gettime_us();
	for (;;) {
		now = port_gettime_us();
		if ((now - start) >= (uint32_t)us)
			break;
	}
}

PORT_STATIC_INLINE bool
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

/*
 * Translate a logical bus address to a physical index into bus->ram.
 *
 * In 8 KB split mode the Apple-1 maps 0x0000-0x0FFF to the first bank and
 * 0xE000-0xEFFF to the second bank (offset 0x1000 in the 8 KB array).
 * Without this translation any access to the upper window would index far
 * beyond the end of the allocated buffer and corrupt host memory.
 */
PORT_STATIC_INLINE uint16_t
bus_translate_ram_address(struct bus *bus, uint16_t address)
{
	if (bus->ram_size == 8192) {
		if (address >= 0xE000 && address < 0xF000)
			return (uint16_t)(address - 0xD000);
	}
	return (address);
}

port_result_t
bus_init(struct bus *bus, uint8_t *ram_buf, uint32_t ram_size)
{
	if (bus == (void *)0 || ram_buf == (void *)0) {
		return (PORT_INVALID);
	}
	port_memset(bus, 0, sizeof(struct bus));
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

	port_memset(bus->ram, 0, ram_size);
	/*
	 * Initialize PIA control registers as Woz Monitor does:
	 * KBD control: 0xA7 (input with handshake)
	 * DSP control: 0xA7 (output with handshake, bit 2 set for data mode)
	 */
	bus->pia.kbd_control = 0xA7;
	bus->pia.dsp_control = 0xA7;

	return (PORT_OK);
}

void
bus_free(struct bus *bus)
{
	bus->ram = NULL;
}

port_result_t
bus_load_bin_buf(struct bus *bus,
    const uint8_t *data,
    size_t len,
    uint16_t address)
{
	uint32_t addr;
	uint32_t i;

	if (bus == (void *)0 || data == (void *)0) {
		return (PORT_INVALID);
	}
	if (len == 0) {
		BUS_LOG(bus, BUS_LOG_ERROR, "Error: Binary buffer is empty.");
		return (PORT_INVALID);
	}

	for (addr = address; addr < (uint32_t)address + len; addr++) {
		if (addr > 0xFFFF ||
		    bus_is_ram_address(bus, (uint16_t)addr) == 0) {
			char msg[256];
			port_snprintf(msg,
			    sizeof(msg),
			    "Error: Binary buffer (%lu bytes) at 0x%04X "
			    "exceeds RAM size (%u KB).",
			    (unsigned long)len,
			    address,
			    bus->ram_size / 1024);
			BUS_LOG(bus, BUS_LOG_ERROR, msg);
			return (PORT_RANGE);
		}
	}

	/*
	 * Copy byte-by-byte through the address translator so that split-RAM
	 * configurations (e.g. 8 KB mode with 0xE000 window) are handled
	 * correctly.  A plain memcpy would use the raw logical address as a
	 * physical array index and overflow the backing buffer.
	 */
	for (i = 0; i < (uint32_t)len; i++) {
		uint16_t la;
		uint16_t pa;

		la = (uint16_t)(address + i);
		pa = bus_translate_ram_address(bus, la);
		bus->ram[pa] = data[i];
	}

	{
		char msg[256];
		port_snprintf(msg,
		    sizeof(msg),
		    "Loaded buffer (%lu bytes) into RAM at 0x%04X-0x%04X",
		    (unsigned long)len,
		    address,
		    (uint16_t)(address + len - 1));
		BUS_LOG(bus, BUS_LOG_INFO, msg);
	}
	return (PORT_OK);
}

PORT_STATIC_INLINE int
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

port_result_t
bus_load_wozmon_txt_buf(struct bus *bus,
    const char *text,
    size_t len,
    uint16_t *run_address,
    bool *has_run_address)
{
	char *cleaned;
	size_t addr_digits;
	size_t data_len;
	size_t hex_len;
	size_t hex_start;
	size_t i;
	size_t j;
	size_t peek;
	size_t w;
	char addr_str[5];
	bool first_addr;
	bool in_comment;
	int total_bytes;
	uint16_t current_addr;
	uint16_t raddr;
	uint8_t val;

	if (bus == (void *)0 || text == (void *)0) {
		return (PORT_INVALID);
	}
	if (len == 0) {
		BUS_LOG(bus, BUS_LOG_ERROR, "Error: Text buffer is empty.");
		return (PORT_INVALID);
	}

	/* Strip inline comments */
	cleaned = port_malloc(len + 1);
	if (cleaned == NULL) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Error: Failed to allocate wozmon parser buffer");
		return (PORT_NOMEM);
	}

	w = 0;
	in_comment = false;
	for (i = 0; i < len; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			in_comment = false;
			cleaned[w++] = text[i];
			continue;
		}
		if (in_comment != 0)
			continue;

		/* Start of comment */
		if (text[i] == '#' || text[i] == ';') {
			in_comment = true;
			continue;
		}
		if (text[i] == '/' && i + 1 < len && text[i + 1] == '/') {
			in_comment = true;
			continue;
		}
		cleaned[w++] = text[i];
	}
	cleaned[w] = '\0';

	current_addr = 0;
	first_addr = true;
	total_bytes = 0;
	i = 0;

	if (has_run_address != NULL)
		*has_run_address = false;

	while (i < w) {
		char c = cleaned[i];

		if (port_isspace((unsigned char)c)) {
			i++;
			continue;
		}

		/* 'T' prefix (turbo) - skip */
		if (port_toupper((unsigned char)c) == 'T' && i + 1 < w &&
		    port_isxdigit((unsigned char)cleaned[i + 1])) {
			i++;
			continue;
		}

		/* 'X' marker - skip X + hex addr */
		if (port_toupper((unsigned char)c) == 'X' && i + 1 < w &&
		    port_isxdigit((unsigned char)cleaned[i + 1])) {
			i++;
			while (
			    i < w && port_isxdigit((unsigned char)cleaned[i]))
				i++;
			continue;
		}

		if (c == ':') {
			i++;
			continue;
		}

		if (port_isxdigit((unsigned char)c)) {
			hex_start = i;
			while (
			    i < w && port_isxdigit((unsigned char)cleaned[i]))
				i++;
			hex_len = i - hex_start;

			peek = i;
			while (peek < w &&
			    port_isspace((unsigned char)cleaned[peek]))
				peek++;

			if (i < w &&
			    port_toupper((unsigned char)cleaned[i]) == 'R') {
				/* run command */
				data_len = hex_len > 4 ? hex_len - 4 : 0;
				for (j = 0; j + 1 < data_len; j += 2) {
					val = (hex_val(cleaned[hex_start + j])
						  << 4) |
					    hex_val(cleaned[hex_start + j + 1]);
					if (bus_is_ram_address(bus,
						current_addr) != 0) {
						bus->ram[bus_translate_ram_address(
						    bus,
						    current_addr)] = val;
					}
					current_addr++;
					total_bytes++;
				}
				if (hex_len >= 4) {
					port_strncpy(addr_str,
					    &cleaned[hex_start + data_len],
					    4);
					addr_str[4] = '\0';
					raddr = (uint16_t)port_strtol(addr_str,
					    NULL,
					    16);
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
				data_len = hex_len > 4 ? hex_len - 4 : 0;
				for (j = 0; j + 1 < data_len; j += 2) {
					val = (hex_val(cleaned[hex_start + j])
						  << 4) |
					    hex_val(cleaned[hex_start + j + 1]);
					if (bus_is_ram_address(bus,
						current_addr) != 0) {
						bus->ram[bus_translate_ram_address(
						    bus,
						    current_addr)] = val;
					}
					current_addr++;
					total_bytes++;
				}
				port_memset(addr_str, 0, sizeof(addr_str));
				addr_digits = hex_len > 4 ? 4 : hex_len;
				port_strncpy(addr_str,
				    &cleaned[hex_start + hex_len - addr_digits],
				    addr_digits);
				current_addr =
				    (uint16_t)port_strtol(addr_str, NULL, 16);
				if (first_addr != 0) {
					first_addr = false;
				}
				i = peek + 1; /* skip ':' */
				continue;
			}

			/* Data bytes */
			for (j = 0; j + 1 < hex_len; j += 2) {
				val = (hex_val(cleaned[hex_start + j]) << 4) |
				    hex_val(cleaned[hex_start + j + 1]);
				if (bus_is_ram_address(bus, current_addr) !=
				    0) {
					bus->ram[bus_translate_ram_address(bus,
					    current_addr)] = val;
				}
				current_addr++;
				total_bytes++;
			}
			continue;
		}

		i++;
	}

	port_free(cleaned);

	if (total_bytes == 0 && first_addr != 0) {
		/* Nothing loaded, completely invalid format or empty */
		return (PORT_PROTOCOL);
	}

	{
		char msg[256];
		port_snprintf(msg,
		    sizeof(msg),
		    "Loaded %d bytes via Woz Monitor text format",
		    total_bytes);
		BUS_LOG(bus, BUS_LOG_INFO, msg);
	}
	return (PORT_OK);
}

port_result_t
bus_load_rom(struct bus *bus, const char *rom_path)
{
	if (bus == (void *)0) {
		return (PORT_INVALID);
	}
	if (rom_path == NULL) {
#ifndef APPLE1_OMIT_WOZMON
		port_memcpy(bus->rom, embedded_wozmon, 256);
		bus->rom_loaded = true;
		return (PORT_OK);
#else
		BUS_LOG(bus, BUS_LOG_ERROR, "Error: Monitor ROM omitted");
		return (PORT_MISUSE);
#endif
	}

#ifndef APPLE1_OMIT_DISKIO
	{
		void *f;
		port_size_t read_bytes;
		long size;

		f = port_vfs_default.open(rom_path, PORT_VFS_READ);
		if (f == NULL) {
			char msg[512];
			port_snprintf(msg,
			    sizeof(msg),
			    "Warning: '%s' not found, using embedded Wozmon "
			    "ROM.",
			    rom_path);
			BUS_LOG(bus, BUS_LOG_WARN, msg);
#ifndef APPLE1_OMIT_WOZMON
			port_memcpy(bus->rom, embedded_wozmon, 256);
			bus->rom_loaded = true;
			return (PORT_OK);
#else
			BUS_LOG(bus,
			    BUS_LOG_ERROR,
			    "Error: Monitor ROM omitted");
			return (PORT_MISUSE);
#endif
		}

		size = port_vfs_default.size(f);

		if (size != 256) {
			char msg[512];
			port_snprintf(msg,
			    sizeof(msg),
			    "Error: ROM file '%s' is %ld bytes. "
			    "Apple 1 Monitor ROM must be exactly 256 bytes.",
			    rom_path,
			    size);
			BUS_LOG(bus, BUS_LOG_ERROR, msg);
			port_vfs_default.close(f);
			return (PORT_PROTOCOL);
		}

		read_bytes = 0;
		if (port_vfs_default.read(f, bus->rom, 256, &read_bytes) != 0 ||
		    read_bytes != 256) {
			char msg[512];
			port_snprintf(msg,
			    sizeof(msg),
			    "Error: Failed to read 256 bytes from '%s'",
			    rom_path);
			BUS_LOG(bus, BUS_LOG_ERROR, msg);
			port_vfs_default.close(f);
			return (PORT_IO);
		}
		port_vfs_default.close(f);

		bus->rom_loaded = true;
		return (PORT_OK);
	}
#else
	BUS_LOG(bus,
	    BUS_LOG_ERROR,
	    "Error: Disk I/O disabled, cannot load ROM from file.");
	return (PORT_MISUSE);
#endif
}

#ifndef APPLE1_OMIT_DISKIO

port_result_t
bus_load_bin(struct bus *bus, const char *bin_path, uint16_t address)
{
	void *f;
	uint8_t *buf;
	long size;
	port_result_t ret;

	if (bus == (void *)0 || bin_path == (void *)0) {
		return (PORT_INVALID);
	}
	f = port_vfs_default.open(bin_path, PORT_VFS_READ);
	if (f == NULL) {
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Could not open binary file '%s'",
		    bin_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		return (PORT_CANTOPEN);
	}

	size = port_vfs_default.size(f);

	if (size <= 0) {
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Binary file '%s' is empty.",
		    bin_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		port_vfs_default.close(f);
		return (PORT_IO);
	}

	buf = (uint8_t *)port_malloc(size);
	if (buf == NULL) {
		port_vfs_default.close(f);
		return (PORT_NOMEM);
	}

	{
		port_size_t read_bytes;
		read_bytes = 0;
		if (port_vfs_default.read(f, buf, size, &read_bytes) != 0 ||
		    read_bytes != (port_size_t)size) {
			char msg[512];
			port_snprintf(msg,
			    sizeof(msg),
			    "Error: Failed to read entire binary file '%s'",
			    bin_path);
			BUS_LOG(bus, BUS_LOG_ERROR, msg);
			port_free(buf);
			port_vfs_default.close(f);
			return (PORT_IO);
		}
	}
	port_vfs_default.close(f);

	ret = bus_load_bin_buf(bus, buf, size, address);
	port_free(buf);
	return (ret);
}

port_result_t
bus_load_wozmon_txt(struct bus *bus,
    const char *txt_path,
    uint16_t *run_address,
    bool *has_run_address)
{
	char *content;
	port_size_t read_bytes;
	long size;
	void *f;
	port_result_t ret;

	if (bus == (void *)0 || txt_path == (void *)0) {
		return (PORT_INVALID);
	}
	f = port_vfs_default.open(txt_path, PORT_VFS_READ);
	if (f == NULL) {
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Could not open text file '%s'",
		    txt_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		return (PORT_CANTOPEN);
	}

	size = port_vfs_default.size(f);

	if (size <= 0) {
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Text file '%s' is empty.",
		    txt_path);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		port_vfs_default.close(f);
		return (PORT_IO);
	}

	content = (char *)port_malloc(size + 1);
	if (content == NULL) {
		port_vfs_default.close(f);
		return (PORT_NOMEM);
	}

	read_bytes = 0;
	if (port_vfs_default.read(f, content, size, &read_bytes) != 0) {
		port_free(content);
		port_vfs_default.close(f);
		return (PORT_IO);
	}
	content[read_bytes] = '\0';
	port_vfs_default.close(f);

	ret = bus_load_wozmon_txt_buf(bus,
	    content,
	    read_bytes,
	    run_address,
	    has_run_address);
	port_free(content);
	return (ret);
}
#endif

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
	case PIA_BASE + 2: {
		uint8_t val;

		/*
		 * Wozmon OUT ($FFEF) waits on BIT $D012 / BMI: bit 7 set means
		 * busy, clear means ready.  Do not always return 0x00 — that
		 * bypasses -B baud throttling entirely.  term_dsp_ready()
		 * drives the inter-character window when -B is active.
		 */
		val = bus->pia.dsp_data & 0x7F;
		if (term_dsp_ready() == 0)
			val |= 0x80;
		return (val);
	}
	case PIA_BASE + 3: {
		uint8_t ctrl = bus->pia.dsp_control;
		/*
		 * Bit 7 is the display-ready flag.  In baud-rate mode it is
		 * cleared until term_dsp_ready() reports that the inter-
		 * character window has expired.  This is non-blocking: the
		 * CPU keeps running and polling, exactly as Wozmon does on
		 * real hardware.
		 */
		if (term_dsp_ready() == 0)
			ctrl &= ~0x80;
		else
			ctrl |= 0x80;
		return (ctrl);
	}
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
	struct expansion_card *card;
	int i;
	uint8_t result;
	bool card_hit;

	result = bus->last_bus_value; /* default: open-bus float */

	if (bus->opts.flat_bus != 0) {
		result = bus->ram[bus_translate_ram_address(bus, address)];
		bus->last_bus_value = result;
	} else {
		/* PIA 6821 alias: only A0-A1 reach the chip's RS pins. */
		if ((address & 0xF000) == 0xD000)
			address = PIA_BASE | (address & 0x03);

		if (bus->opts.uncapped != 0 && bus->opts.throttle_pia != 0 &&
		    bus->opts.headless == 0 && address >= PIA_BASE &&
		    address <= (PIA_BASE + 3))
			delay_microseconds(1);

		if (address >= ROM_BASE) {
			if (bus->rom_loaded != 0)
				bus->last_bus_value =
				    bus->rom[address - ROM_BASE];
			result = bus->last_bus_value;
		} else if (address >= PIA_BASE && address <= (PIA_BASE + 3)) {
			result = pia_read(bus, address);
			bus->last_bus_value = result;
		} else {
			card_hit = false;
			for (i = 0; i < bus->num_cards; i++) {
				card = bus->cards[i];
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
					bus->last_bus_value =
					    bus->ram[bus_translate_ram_address(
						bus,
						address)];
				result = bus->last_bus_value;
			}
		}
	}

#ifndef APPLE1_OMIT_BUS_ACCESS_CB
	if (bus->access_cb != NULL)
		bus->access_cb(bus->access_cb_ctx, address, false, result);
#endif
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
	struct expansion_card *card;
	int i;
	bool card_hit;

	bus->last_bus_value = value;

	if (bus->opts.flat_bus != 0) {
		bus->ram[bus_translate_ram_address(bus, address)] = value;
	} else {
		/* PIA 6821 alias: only A0-A1 reach the chip's RS pins. */
		if ((address & 0xF000) == 0xD000)
			address = PIA_BASE | (address & 0x03);

#ifndef APPLE1_OMIT_PIA_THROTTLE
		if (bus->opts.uncapped != 0 && bus->opts.throttle_pia != 0 &&
		    bus->opts.headless == 0 && address >= PIA_BASE &&
		    address <= (PIA_BASE + 3))
			delay_microseconds(1);
#endif

		if (address >= ROM_BASE) {
			/* ROM is read-only - silently ignore */
		} else if (address >= PIA_BASE && address <= (PIA_BASE + 3)) {
			pia_write(bus, address, value, is_dummy);
		} else {
			card_hit = false;
			for (i = 0; i < bus->num_cards; i++) {
				card = bus->cards[i];
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
				bus->ram[bus_translate_ram_address(bus,
				    address)] = value;
		}
	}

	/* Fire watchpoint callback for real (non-phantom) writes only
	 * (unless flat_bus is enabled) */
#ifndef APPLE1_OMIT_BUS_ACCESS_CB
	if ((is_dummy == 0 || bus->opts.flat_bus != 0) &&
	    bus->access_cb != NULL)
		bus->access_cb(bus->access_cb_ctx, address, true, value);
#endif
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
#ifndef APPLE1_OMIT_KBD_BOUNCE
	if (bus->opts.emulate_kbd_bounce != 0 && bus->kbd_bounce_cycles > 0) {
		bus->kbd_bounce_cycles--;
		return; /* encoder still settling - ignore new input */
	}
#endif

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
#ifndef APPLE1_OMIT_KBD_BOUNCE
				if (bus->opts.emulate_kbd_bounce != 0) {
					bus->kbd_bounce_cycles = 1250 +
					    (uint32_t)(port_rand() % 2500);
				}
#endif
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
	if (bus->num_cards < APPLE1_MAX_CARDS) {
		bus->cards[bus->num_cards++] = card;
	} else {
		char msg[128];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Maximum number of expansion cards (%d) exceeded.",
		    APPLE1_MAX_CARDS);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
	}
}
