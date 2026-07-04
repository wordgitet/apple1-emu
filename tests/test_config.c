#include "../apple1limit.h"
#include "../cli_config.h"
#include "../port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * test_config.c -- human-readable .conf parsing via port_vfs.
 */

#define MOCK_CFG_PATH "ram:apple1.conf"
#define MOCK_MAX_DATA 2048

struct mock_cfg_file {
	char data[MOCK_MAX_DATA];
	long size;
	long pos;
};

static struct mock_cfg_file mock_cfg;
static struct port_vfs *saved_vfs;

static port_file_t
mock_open(const char *path, int flags)
{
	(void)flags;
	if (path == NULL || port_strcmp(path, MOCK_CFG_PATH) != 0) {
		return (PORT_FILE_INVALID);
	}
	mock_cfg.pos = 0;
	return ((port_file_t)&mock_cfg);
}

static void
mock_close(port_file_t f)
{
	(void)f;
}

static int
mock_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
{
	(void)f;
	(void)buf;
	(void)sz;
	if (nread != NULL) {
		*nread = 0;
	}
	return (0);
}

static int
mock_size(port_file_t f, port_size_t *size)
{
	(void)f;
	(void)size;
	return (-1);
}

static int
mock_seek(port_file_t f, int32_t offset, int whence)
{
	(void)f;
	(void)offset;
	(void)whence;
	return (-1);
}

static int
mock_write(port_file_t f, const void *buf, port_size_t sz, port_size_t *nwritten)
{
	(void)f;
	(void)buf;
	(void)sz;
	(void)nwritten;
	return (-1);
}

static int
mock_read_line(port_file_t f, char *buf, port_size_t size)
{
	struct mock_cfg_file *cfg;
	port_size_t i;
	char c;

	if (f == PORT_FILE_INVALID || buf == NULL || size == 0) {
		return (0);
	}
	cfg = (struct mock_cfg_file *)f;
	for (i = 0; i + 1 < size; i++) {
		if (cfg->pos >= cfg->size) {
			break;
		}
		c = cfg->data[cfg->pos++];
		buf[i] = c;
		if (c == '\n') {
			i++;
			break;
		}
	}
	if (i == 0) {
		return (0);
	}
	buf[i] = '\0';
	return (1);
}

static struct port_vfs mock_vfs = { mock_open,
	mock_close,
	mock_read,
	mock_size,
	mock_seek,
	mock_write,
	mock_read_line };

static void
mock_install(const char *content)
{
	port_size_t len;

	len = port_strlen(content);
	assert(len < MOCK_MAX_DATA);
	port_memcpy(mock_cfg.data, content, len);
	mock_cfg.size = (long)len;
	mock_cfg.pos = 0;
	saved_vfs = g_port_vfs;
	g_port_vfs = &mock_vfs;
}

static void
mock_restore(void)
{
	g_port_vfs = saved_vfs;
}

static void
test_empty_config(void)
{
	struct cli_config_opts opts;
	char errbuf[128];
	int rc;

	cli_config_init_defaults(&opts);
	mock_install("# only comments\n\n# rom = wozmon.bin\n");
	rc = load_config_file(MOCK_CFG_PATH, &opts, errbuf, sizeof(errbuf));
	assert(rc == CLI_CONFIG_OK);
	assert(opts.rom_path == NULL);
	assert(opts.ram_size == (uint32_t)(APPLE1_DEFAULT_RAM_KB * 1024));
	assert(opts.opt_uncapped == false);

	cli_config_free_strings(&opts);
	mock_restore();
}

static void
test_sample_config(void)
{
	struct cli_config_opts opts;
	char errbuf[128];
	int rc;

	cli_config_init_defaults(&opts);

	mock_install("# comment\n"
		     "rom = wozmon.bin\n"
		     "ram_kb = 16\n"
		     "load = myprog.bin @ 0300\n"
		     "speed_cap = yes\n"
		     "flat_bus = true\n"
		     "dram_refresh = on\n"
		     "keyboard_bounce = off\n"
		     "aci_rom = aci.bin\n"
		     "tape_in = in.wav\n"
		     "tape_out = out.wav\n"
		     "krusader = k.rom\n"
		     "baud = 1200\n"
		     "trace = 0\n");

	rc = load_config_file(MOCK_CFG_PATH, &opts, errbuf, sizeof(errbuf));
	assert(rc == CLI_CONFIG_OK);
	assert(port_strcmp(opts.rom_path, "wozmon.bin") == 0);
	assert(opts.ram_size == 16U * 1024U);
	assert(port_strcmp(opts.bin_path, "myprog.bin") == 0);
	assert(opts.bin_address == 0x0300);
	assert(opts.opt_uncapped == false);
	assert(opts.opt_flat_bus == true);
	assert(opts.opt_emulate_dram == true);
	assert(opts.opt_emulate_bounce == false);
	assert(opts.opt_trace == false);
	assert(opts.baud == 1200U);

	cli_config_free_strings(&opts);
	mock_restore();
}

static void
test_reject_cli_only_keys(void)
{
	struct cli_config_opts opts;
	char errbuf[128];
	int rc;

	cli_config_init_defaults(&opts);
	mock_install("headless = yes\n");
	rc = load_config_file(MOCK_CFG_PATH, &opts, errbuf, sizeof(errbuf));
	assert(rc == CLI_CONFIG_PARSE);

	cli_config_init_defaults(&opts);
	mock_install("debugger = yes\n");
	rc = load_config_file(MOCK_CFG_PATH, &opts, errbuf, sizeof(errbuf));
	assert(rc == CLI_CONFIG_PARSE);

	cli_config_free_strings(&opts);
	mock_restore();
}

static void
test_path_suffix(void)
{
	assert(cli_path_is_config_file("apple1.conf") != 0);
	assert(cli_path_is_config_file("APPLE1.CONF") != 0);
	assert(cli_path_is_config_file("prog.bin") == 0);
}

int
main(void)
{
	test_path_suffix();
	test_empty_config();
	test_sample_config();
	test_reject_cli_only_keys();
	printf("test_config passed.\n");
	return (0);
}
