#include "cli_config.h"

static void
config_warn(const char *fmt, ...)
{
	char buf[512];
	va_list args;

	va_start(args, fmt);
	port_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	port_term_write_buf(buf, port_strlen(buf));
}

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
    char **krusader_path)
{
	port_file_t fp;
	char line[1024];

	fp = port_vfs_default.open(path, PORT_VFS_READ);
	if (fp == PORT_FILE_INVALID) {
		config_warn("Warning: cannot open config '%s'\n", path);
		return;
	}

	while (port_vfs_default.read_line(fp, line, sizeof(line)) != 0) {
		char *ptr;
		char *val;
		bool has_val;
		port_size_t len;
		char flag;

		len = port_strlen(line);
		while (len > 0 &&
		    (line[len - 1] == '\r' || line[len - 1] == '\n' ||
			line[len - 1] == ' ' || line[len - 1] == '\t')) {
			line[--len] = '\0';
		}
		ptr = line;
		while (*ptr == ' ' || *ptr == '\t') {
			ptr++;
		}
		if (*ptr == '#' || *ptr == '\0') {
			continue;
		}
		if (ptr[0] != '-' || ptr[1] == '\0') {
			continue;
		}
		flag = ptr[1];
		val = ptr + 2;
		while (*val == ' ' || *val == '\t') {
			val++;
		}
		has_val = (*val != '\0');

		switch (flag) {
		case 'r':
			if (has_val != 0) {
				if (*rom_path != NULL) {
					port_free(*rom_path);
				}
				*rom_path = port_strdup(val);
			}
			break;
		case 'm':
			if (has_val != 0) {
				int kb;

				kb = (int)port_strtoul(val, (void *)0, 10);
				if (kb >= 4 && kb <= 64) {
					*ram_size = (uint32_t)(kb * 1024);
				}
			}
			break;
		case 'l':
			if (has_val != 0) {
				char *dup;
				char *at;

				dup = port_strdup(val);
				at = port_strchr(dup, '@');
				if (at != NULL) {
					*at = '\0';
					if (*bin_path != NULL) {
						port_free(*bin_path);
					}
					*bin_path = port_strdup(dup);
					*bin_address = (uint16_t)
					    port_strtol(at + 1, NULL, 16);
				}
				port_free(dup);
			}
			break;
		case 'c':
			*opt_uncapped = false;
			break;
		case 'p':
			*opt_throttle_pia = false;
			break;
		case 'd':
			*opt_emulate_dram = true;
			break;
		case 'b':
			*opt_emulate_bounce = true;
			break;
		case 's':
			*opt_randomize_cold = false;
			break;
		case 'F':
			*opt_flat_bus = true;
			break;
		case 'H':
			*opt_headless = true;
			break;
		case 'g':
			*opt_debug = true;
			break;
		case 't':
			*opt_trace = true;
			break;
		case 'a':
			if (has_val != 0) {
				if (*aci_path != NULL) {
					port_free(*aci_path);
				}
				*aci_path = port_strdup(val);
			}
			break;
		case 'e':
			if (has_val != 0) {
				if (*tape_path != NULL) {
					port_free(*tape_path);
				}
				*tape_path = port_strdup(val);
			}
			break;
		case 'E':
			if (has_val != 0) {
				if (*save_tape_path != NULL) {
					port_free(*save_tape_path);
				}
				*save_tape_path = port_strdup(val);
			}
			break;
		case 'k':
			if (has_val != 0) {
				if (*krusader_path != NULL) {
					port_free(*krusader_path);
				}
				*krusader_path = port_strdup(val);
			}
			break;
		default:
			break;
		}
	}
	port_vfs_default.close(fp);
}
