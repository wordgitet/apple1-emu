#ifndef BUS_H
#define BUS_H

#define PIA_BASE     0xD010
#define ROM_BASE     0xFF00
#define RESET_VECTOR 0xFFFC

#include "apple1limit.h"
#include "port.h"
#include <stddef.h>

/* Log severity levels */
#define BUS_LOG_INFO  0
#define BUS_LOG_WARN  1
#define BUS_LOG_ERROR 2

/*
 * Route a log message through the bus log callback.
 * Silently dropped when log is NULL (embedded targets with no output).
 */
#define BUS_LOG(bus, lvl, msg)                                    \
	do {                                                      \
		if ((bus)->log != NULL)                           \
			(bus)->log((bus)->log_ctx, (lvl), (msg)); \
	} while (0)

#ifndef APPLE1_OMIT_BUS_ACCESS_CB
/* Callback fired on every non-dummy bus read and write.
 * addr     - the final (post-alias-resolution) address accessed
 * is_write - true for writes, false for reads
 * val      - the byte written or read */
typedef void (
    *bus_access_cb_t)(void *ctx, uint16_t addr, bool is_write, uint8_t val);
#endif

struct pia_6821 {
	uint8_t kbd_data; /* 0xD010: Keyboard Data */
	uint8_t
	    kbd_control; /* 0xD011: Keyboard Control (Bit 7: Keyboard Strobe) */
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
	struct expansion_card *cards[APPLE1_MAX_CARDS];
	int num_cards;
	uint32_t kbd_bounce_cycles; /* remaining bounce window (in calls) */

#ifndef APPLE1_OMIT_BUS_ACCESS_CB
	/* Optional callback fired on every non-dummy bus access (read or write).
	 * Set to NULL to disable.  Used by the debugger for watchpoints. */
	bus_access_cb_t access_cb;
	void *access_cb_ctx;
#endif

	/* Optional log callback.  If NULL, all messages are silently dropped.
	 * Frontends set this to route errors to stderr, a GUI log, or a UART. */
	void (*log)(void *log_ctx, int level, const char *msg);
	void *log_ctx;
};

/* Initialize the memory bus with a configured RAM size and buffer */
port_result_t
bus_init(struct bus *bus, uint8_t *ram_buf, uint32_t ram_size);

/* Clean up */
void
bus_free(struct bus *bus);

/* Load binary from buffer into RAM at address */
port_result_t
bus_load_bin_buf(struct bus *bus,
    const uint8_t *data,
    size_t len,
    uint16_t address);

/* Load Woz Monitor formatted text from a buffer. */
port_result_t
bus_load_wozmon_txt_buf(struct bus *bus,
    const char *text,
    size_t len,
    uint16_t *run_address,
    bool *has_run_address);

/* Load exactly 256-byte ROM image (Woz Monitor) at 0xFF00-0xFFFF */
port_result_t
bus_load_rom(struct bus *bus, const char *rom_path);

#ifndef APPLE1_OMIT_DISKIO
/* Load arbitrary binary file into RAM at the specified starting address */
port_result_t
bus_load_bin(struct bus *bus, const char *bin_path, uint16_t address);

/* Load Woz Monitor formatted text file into RAM. Returns PORT_OK on success.
 * If run_address is not NULL, it will be set to the R (run) address if
 * specified in the file. */
port_result_t
bus_load_wozmon_txt(struct bus *bus,
    const char *txt_path,
    uint16_t *run_address,
    bool *has_run_address);
#endif

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
