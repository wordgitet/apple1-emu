#include "apple1limit.h"
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
cli_config_init_defaults(struct cli_config_opts *opts)
{
	if (opts == NULL) {
		return;
	}
	opts->rom_path = NULL;
	opts->bin_path = NULL;
	opts->bin_address = 0;
	opts->wozmon_txt_path = NULL;
	opts->flat_bin_path = NULL;
	opts->aci_path = NULL;
	opts->tape_path = NULL;
	opts->save_tape_path = NULL;
	opts->krusader_path = NULL;
	opts->baud = 0;
	opts->opt_uncapped = false;
	opts->opt_throttle_pia = true;
	opts->opt_emulate_dram = false;
	opts->opt_emulate_bounce = false;
	opts->opt_randomize_cold = true;
	opts->opt_flat_bus = false;
	opts->opt_trace = false;
	opts->log_path = NULL;
	opts->log_level = 1; /* warn+errors by default */
}

void
cli_config_free_strings(struct cli_config_opts *opts)
{
	if (opts == NULL) {
		return;
	}
	if (opts->rom_path != NULL) {
		port_free(opts->rom_path);
		opts->rom_path = NULL;
	}
	if (opts->bin_path != NULL) {
		port_free(opts->bin_path);
		opts->bin_path = NULL;
	}
	if (opts->wozmon_txt_path != NULL) {
		port_free(opts->wozmon_txt_path);
		opts->wozmon_txt_path = NULL;
	}
	if (opts->flat_bin_path != NULL) {
		port_free(opts->flat_bin_path);
		opts->flat_bin_path = NULL;
	}
	if (opts->aci_path != NULL) {
		port_free(opts->aci_path);
		opts->aci_path = NULL;
	}
	if (opts->tape_path != NULL) {
		port_free(opts->tape_path);
		opts->tape_path = NULL;
	}
	if (opts->save_tape_path != NULL) {
		port_free(opts->save_tape_path);
		opts->save_tape_path = NULL;
	}
	if (opts->krusader_path != NULL) {
		port_free(opts->krusader_path);
		opts->krusader_path = NULL;
	}
	if (opts->log_path != NULL) {
		port_free(opts->log_path);
		opts->log_path = NULL;
	}
}

int
cli_path_is_config_file(const char *path)
{
	port_size_t len;
	const char *suf;
	int i;

	if (path == NULL)
		return (0);
	len = port_strlen(path);
	if (len >= 9) {
		suf = path + len - 9;
		if (port_tolower((unsigned char)suf[0]) == '.' &&
		    port_tolower((unsigned char)suf[1]) == 'c' &&
		    port_tolower((unsigned char)suf[2]) == 'o' &&
		    port_tolower((unsigned char)suf[3]) == 'n' &&
		    port_tolower((unsigned char)suf[4]) == 'f' &&
		    port_tolower((unsigned char)suf[5]) == '.' &&
		    port_tolower((unsigned char)suf[6]) == 't' &&
		    port_tolower((unsigned char)suf[7]) == 'n' &&
		    port_tolower((unsigned char)suf[8]) == 's')
			return (1);
	}
	if (len >= 5) {
		suf = path + len - 5;
		for (i = 0; i < 5; i++) {
			if (port_tolower((unsigned char)suf[i]) !=
			    (unsigned char)".conf"[i])
				return (0);
		}
		return (1);
	}
	return (0);
}

static void
trim_line(char *line)
{
	port_size_t len;
	char *start;

	len = port_strlen(line);
	while (len > 0 &&
	    (line[len - 1] == '\r' || line[len - 1] == '\n' ||
		line[len - 1] == ' ' || line[len - 1] == '\t')) {
		line[--len] = '\0';
	}
	start = line;
	while (*start == ' ' || *start == '\t') {
		start++;
	}
	if (start != line) {
		port_memmove(line, start, port_strlen(start) + 1);
	}
}

static int
key_eq(const char *key, const char *name)
{
	const char *a;
	const char *b;

	a = key;
	b = name;
	while (*a != '\0' && *b != '\0') {
		if (port_tolower((unsigned char)*a) !=
		    port_tolower((unsigned char)*b)) {
			return (0);
		}
		a++;
		b++;
	}
	if (*a == '\0' && *b == '\0') {
		return (1);
	}
	return (0);
}

static int
parse_bool(const char *val, bool *out)
{
	if (key_eq(val, "1") || key_eq(val, "yes") || key_eq(val, "true") ||
	    key_eq(val, "on")) {
		*out = true;
		return (1);
	}
	if (key_eq(val, "0") || key_eq(val, "no") || key_eq(val, "false") ||
	    key_eq(val, "off")) {
		*out = false;
		return (1);
	}
	return (0);
}

static void
trim_trailing(char *s)
{
	port_size_t len;

	len = port_strlen(s);
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
		s[--len] = '\0';
	}
}

static int
parse_load_value(const char *val,
    char **bin_path,
    uint16_t *bin_address,
    char *errbuf,
    port_size_t errbuf_sz)
{
	char *dup;
	char *at;
	char *path_part;
	char *addr_part;

	dup = port_strdup(val);
	if (dup == NULL) {
		return (0);
	}
	at = port_strchr(dup, '@');
	if (at == NULL) {
		port_free(dup);
		if (errbuf_sz > 0) {
			port_snprintf(errbuf,
			    errbuf_sz,
			    "load expects 'file @ address'");
		}
		return (0);
	}
	*at = '\0';
	path_part = dup;
	addr_part = at + 1;
	while (*path_part == ' ' || *path_part == '\t') {
		path_part++;
	}
	while (*addr_part == ' ' || *addr_part == '\t') {
		addr_part++;
	}
	trim_trailing(path_part);
	if (*path_part == '\0' || *addr_part == '\0') {
		port_free(dup);
		if (errbuf_sz > 0) {
			port_snprintf(errbuf,
			    errbuf_sz,
			    "load expects 'file @ address'");
		}
		return (0);
	}
	if (*bin_path != NULL) {
		port_free(*bin_path);
	}
	*bin_path = port_strdup(path_part);
	*bin_address = (uint16_t)port_strtol(addr_part, NULL, 16);
	port_free(dup);
	if (*bin_path == NULL) {
		return (0);
	}
	return (1);
}

static int
set_string(char **dst, const char *val)
{
	if (*dst != NULL) {
		port_free(*dst);
	}
	*dst = port_strdup(val);
	if (*dst == NULL) {
		return (0);
	}
	return (1);
}

static int
apply_key(const char *key,
    const char *val,
    struct cli_config_opts *opts,
    char *errbuf,
    port_size_t errbuf_sz)
{
	int kb;
	bool bval;

	if (key_eq(key, "rom")) {
		return (set_string(&opts->rom_path, val));
	}
	if (key_eq(key, "load")) {
		return (parse_load_value(val,
		    &opts->bin_path,
		    &opts->bin_address,
		    errbuf,
		    errbuf_sz));
	}
	if (key_eq(key, "wozmon_txt")) {
		return (set_string(&opts->wozmon_txt_path, val));
	}
	if (key_eq(key, "flat_bin")) {
		opts->opt_flat_bus = true;
		return (set_string(&opts->flat_bin_path, val));
	}
	if (key_eq(key, "aci_rom")) {
		return (set_string(&opts->aci_path, val));
	}
	if (key_eq(key, "tape_in")) {
		return (set_string(&opts->tape_path, val));
	}
	if (key_eq(key, "tape_out")) {
		return (set_string(&opts->save_tape_path, val));
	}
	if (key_eq(key, "krusader")) {
		return (set_string(&opts->krusader_path, val));
	}
	if (key_eq(key, "baud")) {
		kb = (int)port_strtoul(val, NULL, 10);
		if (kb > 0) {
			opts->baud = (uint32_t)kb;
			return (1);
		}
		if (errbuf_sz > 0) {
			port_snprintf(errbuf, errbuf_sz, "baud must be > 0");
		}
		return (0);
	}
	if (key_eq(key, "speed_cap")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_uncapped = (bval != 0) ? false : true;
		return (1);
	}
	if (key_eq(key, "flat_bus")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_flat_bus = bval;
		return (1);
	}
	if (key_eq(key, "throttle_pia")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_throttle_pia = bval;
		return (1);
	}
	if (key_eq(key, "dram_refresh")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_emulate_dram = bval;
		return (1);
	}
	if (key_eq(key, "keyboard_bounce")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_emulate_bounce = bval;
		return (1);
	}
	if (key_eq(key, "randomize_ram")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_randomize_cold = bval;
		return (1);
	}
	if (key_eq(key, "trace")) {
		if (parse_bool(val, &bval) == 0) {
			goto bad_bool;
		}
		opts->opt_trace = bval;
		return (1);
	}
	if (key_eq(key, "log_file")) {
		return (set_string(&opts->log_path, val));
	}
	if (key_eq(key, "log_level")) {
		int lvl;

		lvl = (int)port_strtoul(val, NULL, 10);
		if (lvl >= 0 && lvl <= 2) {
			opts->log_level = lvl;
			return (1);
		}
		if (errbuf_sz > 0) {
			port_snprintf(errbuf,
			    errbuf_sz,
			    "log_level must be 0, 1, or 2");
		}
		return (0);
	}
	if (key_eq(key, "headless") || key_eq(key, "debugger")) {
		if (errbuf_sz > 0) {
			port_snprintf(errbuf,
			    errbuf_sz,
			    "'%s' is CLI-only; use -H or -g'",
			    key);
		}
		return (0);
	}
	if (errbuf_sz > 0) {
		port_snprintf(errbuf, errbuf_sz, "unknown key '%s'", key);
	}
	return (0);

bad_bool:
	if (errbuf_sz > 0) {
		port_snprintf(errbuf,
		    errbuf_sz,
		    "boolean value for '%s' must be yes/no",
		    key);
	}
	return (0);
}

int
load_config_file(const char *path,
    struct cli_config_opts *opts,
    char *errbuf,
    port_size_t errbuf_sz)
{
	port_file_t fp;
	char line[1024];
	int line_no;

	if (opts == NULL) {
		return (CLI_CONFIG_PARSE);
	}
	if (errbuf_sz > 0) {
		errbuf[0] = '\0';
	}

	fp = port_vfs_default.open(path, PORT_VFS_READ);
	if (fp == PORT_FILE_INVALID) {
		config_warn("Warning: cannot open config '%s'\n", path);
		return (CLI_CONFIG_CANTOPEN);
	}

	line_no = 0;
	while (port_vfs_default.read_line(fp, line, sizeof(line)) != 0) {
		char *eq;
		char *key;
		char *val;

		line_no++;
		trim_line(line);
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}
		eq = port_strchr(line, '=');
		if (eq == NULL) {
			if (errbuf_sz > 0) {
				port_snprintf(errbuf,
				    errbuf_sz,
				    "line %d: expected key = value",
				    line_no);
			}
			port_vfs_default.close(fp);
			return (CLI_CONFIG_PARSE);
		}
		*eq = '\0';
		key = line;
		val = eq + 1;
		trim_line(key);
		trim_line(val);
		if (key[0] == '\0' || val[0] == '\0') {
			if (errbuf_sz > 0) {
				port_snprintf(errbuf,
				    errbuf_sz,
				    "line %d: empty key or value",
				    line_no);
			}
			port_vfs_default.close(fp);
			return (CLI_CONFIG_PARSE);
		}
		if (apply_key(key, val, opts, errbuf, errbuf_sz) == 0) {
			if (errbuf_sz > 0 && errbuf[0] == '\0') {
				port_snprintf(errbuf,
				    errbuf_sz,
				    "line %d: invalid setting",
				    line_no);
			}
			port_vfs_default.close(fp);
			return (CLI_CONFIG_PARSE);
		}
	}
	port_vfs_default.close(fp);
	return (CLI_CONFIG_OK);
}
