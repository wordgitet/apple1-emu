#include "../cli_config.h"
#include "../port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * test_config.c -- load_config_file() via in-memory VFS (cli_config.c).
 */

#define MOCK_CFG_PATH "ram:apple1.conf"
#define MOCK_MAX_DATA 1024

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

static long
mock_size(port_file_t f)
{
	(void)f;
	return (-1);
}

static int
mock_seek(port_file_t f, long offset, int whence)
{
	(void)f;
	(void)offset;
	(void)whence;
	return (-1);
}

static long
mock_write(port_file_t f, const void *buf, port_size_t sz)
{
	(void)f;
	(void)buf;
	(void)sz;
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
test_sample_config(void)
{
	char *rom_path;
	uint32_t ram_size;
	char *bin_path;
	uint16_t bin_address;
	bool opt_uncapped;
	bool opt_throttle_pia;
	bool opt_emulate_dram;
	bool opt_emulate_bounce;
	bool opt_randomize_cold;
	bool opt_flat_bus;
	bool opt_headless;
	bool opt_debug;
	bool opt_trace;
	char *aci_path;
	char *tape_path;
	char *save_tape_path;
	char *krusader_path;

	rom_path = NULL;
	ram_size = 8 * 1024;
	bin_path = NULL;
	bin_address = 0;
	opt_uncapped = true;
	opt_throttle_pia = true;
	opt_emulate_dram = false;
	opt_emulate_bounce = false;
	opt_randomize_cold = true;
	opt_flat_bus = false;
	opt_headless = false;
	opt_debug = false;
	opt_trace = false;
	aci_path = NULL;
	tape_path = NULL;
	save_tape_path = NULL;
	krusader_path = NULL;

	mock_install(
	    "# comment\n"
	    "-r wozmon.bin\n"
	    "-m 16\n"
	    "-l myprog.bin@0300\n"
	    "-c\n"
	    "-F\n"
	    "-H\n"
	    "-d\n"
	    "-a aci_rom.bin\n"
	    "-e tape.wav\n"
	    "-E out.wav\n"
	    "-k krusader.bin\n");

	load_config_file(MOCK_CFG_PATH,
	    &rom_path,
	    &ram_size,
	    &bin_path,
	    &bin_address,
	    &opt_uncapped,
	    &opt_throttle_pia,
	    &opt_emulate_dram,
	    &opt_emulate_bounce,
	    &opt_randomize_cold,
	    &opt_flat_bus,
	    &opt_headless,
	    &opt_debug,
	    &opt_trace,
	    &aci_path,
	    &tape_path,
	    &save_tape_path,
	    &krusader_path);

	assert(rom_path != NULL);
	assert(port_strcmp(rom_path, "wozmon.bin") == 0);
	assert(ram_size == 16U * 1024U);
	assert(bin_path != NULL);
	assert(port_strcmp(bin_path, "myprog.bin") == 0);
	assert(bin_address == 0x0300);
	assert(opt_uncapped == false);
	assert(opt_flat_bus == true);
	assert(opt_headless == true);
	assert(opt_emulate_dram == true);
	assert(aci_path != NULL);
	assert(port_strcmp(aci_path, "aci_rom.bin") == 0);
	assert(tape_path != NULL);
	assert(port_strcmp(tape_path, "tape.wav") == 0);
	assert(save_tape_path != NULL);
	assert(port_strcmp(save_tape_path, "out.wav") == 0);
	assert(krusader_path != NULL);
	assert(port_strcmp(krusader_path, "krusader.bin") == 0);

	port_free(rom_path);
	port_free(bin_path);
	port_free(aci_path);
	port_free(tape_path);
	port_free(save_tape_path);
	port_free(krusader_path);
	mock_restore();
}

int
main(void)
{
	test_sample_config();
	printf("test_config passed.\n");
	return (0);
}
