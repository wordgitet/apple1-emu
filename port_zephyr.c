#include "port.h"
#include <zephyr/kernel.h>

/* Use custom memory allocators or Zephyr malloc */
#if !defined(APPLE1_ZERO_MALLOC) && !defined(APPLE1_CUSTOM_MALLOC)
#include <stdlib.h>
void *
port_malloc(port_size_t sz)
{
	return (malloc(sz));
}

void
port_free(void *ptr)
{
	free(ptr);
}

void *
port_realloc(void *ptr, port_size_t sz)
{
	return (realloc(ptr, sz));
}
#endif

char *
port_strdup(const char *str)
{
	port_size_t len;
	char *dup;

	if (str == NULL) {
		return (NULL);
	}
	len = port_strlen(str) + 1;
	dup = (char *)port_malloc(len);
	if (dup != NULL) {
		port_memcpy(dup, str, len);
	}
	return (dup);
}

uint32_t
port_gettime_us(void)
{
	uint32_t cycles;

	cycles = k_cycle_get_32();
	return ((uint32_t)k_cyc_to_us_near32(cycles));
}

void
port_sleep_us(uint32_t us)
{
	k_usleep(us);
}

void
port_term_raw_enable(void)
{
}

void
port_term_raw_disable(void)
{
}

int
port_term_read_char(void)
{
	return (-1);
}

void
port_term_write_buf(const char *buf, port_size_t n)
{
	port_size_t i;

	for (i = 0; i < n; i++) {
		printk("%c", buf[i]);
	}
}

void
port_signal_setup(port_sig_flag *flag)
{
	(void)flag;
}

/* VFS is mock/fallback on Zephyr by default */
static void *
zephyr_xOpen(const char *path, int flags)
{
	(void)path;
	(void)flags;
	return (NULL);
}

static void
zephyr_xClose(void *file)
{
	(void)file;
}

static int
zephyr_xRead(void *file, void *buf, port_size_t sz, port_size_t *nread)
{
	(void)file;
	(void)buf;
	(void)sz;
	if (nread != NULL) {
		*nread = 0;
	}
	return (0);
}

static long
zephyr_xSize(void *file)
{
	(void)file;
	return (-1);
}

static int
zephyr_xSeek(void *file, long offset, int whence)
{
	(void)file;
	(void)offset;
	(void)whence;
	return (-1);
}

static long
zephyr_xWrite(void *file, const void *buf, port_size_t sz)
{
	(void)file;
	(void)buf;
	(void)sz;
	return (-1);
}

static int
zephyr_xReadLine(void *file, char *buf, port_size_t size)
{
	(void)file;
	(void)buf;
	(void)size;
	return (0);
}

static struct port_vfs zephyr_vfs = { zephyr_xOpen,
	zephyr_xClose,
	zephyr_xRead,
	zephyr_xSize,
	zephyr_xSeek,
	zephyr_xWrite,
	zephyr_xReadLine };

struct port_vfs *g_port_vfs = &zephyr_vfs;
