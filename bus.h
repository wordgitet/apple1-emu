#ifndef BUS_H
#define BUS_H

#define PIA_BASE      0xD010
#define ROM_BASE      0xFF00
#define RESET_VECTOR  0xFFFC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Callback fired on every non-dummy bus read and write.
 * addr     - the final (post-alias-resolution) address accessed
 * is_write - true for writes, false for reads
 * val      - the byte written or read */
typedef void (
    *bus_access_cb_t)(void *ctx, uint16_t addr, bool is_write, uint8_t val);

struct pia_6821 {
	uint8_t kbd_data;    /* 0xD010: Keyboard Data */
	uint8_t kbd_control; /* 0xD011: Keyboard Control (Bit 7: Keyboard Strobe) */
	uint8_t dsp_data;    /* 0xD012: Display Data */
	uint8_t dsp_control; /* 0xD013: Display Control (Bit 7: Display Ready) */
};

struct emu_opts {
	bool uncapped;
	bool throttle_pia;
	bool emulate_dram_refresh;
	bool emulate_kbd_bounce;
	bool randomize_cold_boot;
	bool flat_bus;
	bool headless;
};

struct expansion_card {
	const char *name;
	uint16_t base;
	uint16_t mask;
	bool rom_only;

	uint8_t (*read)(void *ctx, uint16_t addr, bool is_dummy);
	void (*write)(void *ctx, uint16_t addr, uint8_t val, bool is_dummy);
	void (*tick)(void *ctx, uint8_t cycles);

	void *ctx;
};

struct bus {
	uint8_t *ram;
	uint32_t ram_size;
	uint8_t rom[256];
	bool rom_loaded;
	struct pia_6821 pia;
	struct emu_opts opts;
	uint8_t last_bus_value;
	struct expansion_card *cards[8];
	int num_cards;
	uint32_t kbd_bounce_cycles; /* remaining bounce window (in calls) */

	/* Optional callback fired on every non-dummy bus access (read or write).
	 * Set to NULL to disable.  Used by the debugger for watchpoints. */
	bus_access_cb_t access_cb;
	void *access_cb_ctx;
};

/* Initialize the memory bus with a configured RAM size */
bool
bus_init(struct bus *bus, uint32_t ram_size);

/* Clean up and free allocated RAM */
void
bus_free(struct bus *bus);

/* Load exactly 256-byte ROM image (Woz Monitor) at 0xFF00-0xFFFF */
bool
bus_load_rom(struct bus *bus, const char *rom_path);

/* Load arbitrary binary file into RAM at the specified starting address */
bool
bus_load_bin(struct bus *bus, const char *bin_path, uint16_t address);

/* Load Woz Monitor formatted text file into RAM. Returns true on success.
 * If run_address is not NULL, it will be set to the R (run) address if
 * specified in the file. */
bool
bus_load_wozmon_txt(struct bus *bus,
    const char *txt_path,
    uint16_t *run_address,
    bool *has_run_address);

/* Read a byte from the bus */
uint8_t
bus_read(struct bus *bus, uint16_t address);

/* Write a byte to the bus */
void
bus_write(struct bus *bus, uint16_t address, uint8_t value);

/* Write a byte to the bus with dummy write flag */
void
bus_write_ext(struct bus *bus, uint16_t address, uint8_t value, bool is_dummy);

/* Poll for keyboard input and update the PIA keyboard registers */
void
bus_update_keyboard(struct bus *bus);

/* Reset the PIA to default/boot state */
void
bus_reset(struct bus *bus);

/* Register an expansion card on the bus */
void
bus_add_card(struct bus *bus, struct expansion_card *card);

/* Resolve relative data file paths using CWD and XDG share paths */
void
resolve_data_path(const char *rel_path, char *out_path, size_t max_len);

#endif /* BUS_H */
