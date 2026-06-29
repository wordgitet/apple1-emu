#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include "port.h"

/*
 * load_config_file -- parse a line-oriented CLI flag file (see usage.md).
 *
 * Each non-comment line is -<flag>[ <value>].  Updates the out-parameters
 * in place; string paths are replaced with port_strdup() copies.
 */
void
load_config_file(const char *path,
    char **rom_path,
    uint32_t *ram_size,
    char **bin_path,
    uint16_t *bin_address,
    bool *opt_uncapped,
    bool *opt_throttle_pia,
    bool *opt_emulate_dram,
    bool *opt_emulate_bounce,
    bool *opt_randomize_cold,
    bool *opt_flat_bus,
    bool *opt_headless,
    bool *opt_debug,
    bool *opt_trace,
    char **aci_path,
    char **tape_path,
    char **save_tape_path,
    char **krusader_path);

#endif /* CLI_CONFIG_H */
