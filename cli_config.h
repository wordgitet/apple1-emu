#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include "port.h"

/*
 * cli_config_opts -- emulator setup loaded from a .conf file (see usage.md).
 * Headless (-H) and debugger (-g) are CLI-only and never appear here.
 */
struct cli_config_opts {
	char *rom_path;
	uint32_t ram_size;
	char *bin_path;
	uint16_t bin_address;
	char *wozmon_txt_path;
	char *flat_bin_path;
	char *aci_path;
	char *tape_path;
	char *save_tape_path;
	char *krusader_path;
	uint32_t baud;
	bool opt_uncapped;
	bool opt_throttle_pia;
	bool opt_emulate_dram;
	bool opt_emulate_bounce;
	bool opt_randomize_cold;
	bool opt_flat_bus;
	bool opt_trace;
	char *log_path;
	int  log_level; /* 0=errors, 1=warn+errors, 2=info+warn+errors */
};

#define CLI_CONFIG_OK	    0
#define CLI_CONFIG_CANTOPEN 1
#define CLI_CONFIG_PARSE    2

void
cli_config_init_defaults(struct cli_config_opts *opts);

void
cli_config_free_strings(struct cli_config_opts *opts);

/*
 * cli_path_is_config_file -- true if path ends with ".conf" (case insensitive).
 */
int
cli_path_is_config_file(const char *path);

/*
 * load_config_file -- parse key = value lines via port_vfs (portable C89).
 * On CLI_CONFIG_PARSE, errbuf receives a short message if errbuf_sz > 0.
 */
int
load_config_file(const char *path,
    struct cli_config_opts *opts,
    char *errbuf,
    port_size_t errbuf_sz);

#endif /* CLI_CONFIG_H */
