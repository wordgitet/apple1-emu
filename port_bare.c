#include "port.h"

/*
 * port_bare.c -- Bare-metal platform stubs (no system headers).
 *
 * Freestanding string, memory, formatting, getopt, and RNG shims live
 * in port_string.c and are linked alongside this file for every build.
 */

/* ================================================================== */
/* Default VFS implementation -- OS stub (no-op fallback)             */
/* ================================================================== */

static port_file_t
bare_vfs_open(const char *path, int flags)
{
	(void)path;
	(void)flags;
	return (PORT_FILE_INVALID);
}

static void
bare_vfs_close(port_file_t f)
{
	(void)f;
}

static int
bare_vfs_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
{
	(void)f;
	(void)buf;
	(void)sz;
	if (nread != (void *)0) {
		*nread = 0;
	}
	return (0);
}

static long
bare_vfs_size(port_file_t f)
{
	(void)f;
	return (-1);
}

static int
bare_vfs_seek(port_file_t f, long offset, int whence)
{
	(void)f;
	(void)offset;
	(void)whence;
	return (-1);
}

static long
bare_vfs_write(port_file_t f, const void *buf, port_size_t sz)
{
	(void)f;
	(void)buf;
	(void)sz;
	return (-1);
}

static int
bare_vfs_read_line(port_file_t f, char *buf, port_size_t size)
{
	(void)f;
	(void)buf;
	(void)size;
	return (0);
}

static struct port_vfs bare_vfs = {
	bare_vfs_open,
	bare_vfs_close,
	bare_vfs_read,
	bare_vfs_size,
	bare_vfs_seek,
	bare_vfs_write,
	bare_vfs_read_line
};

struct port_vfs *g_port_vfs = &bare_vfs;

/* ================================================================== */
/* Memory allocator shims                                             */
/* ================================================================== */

#if !defined(APPLE1_ZERO_MALLOC) && !defined(APPLE1_CUSTOM_MALLOC)
void *
port_malloc(port_size_t sz)
{
	(void)sz;
	return ((void *)0);
}

void
port_free(void *ptr)
{
	(void)ptr;
}

void *
port_realloc(void *ptr, port_size_t sz)
{
	(void)ptr;
	(void)sz;
	return ((void *)0);
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

/* ================================================================== */
/* Timing shims fallback                                              */
/* ================================================================== */

uint32_t
port_gettime_us(void)
{
	static uint32_t mock_ticks = 0;

	return (mock_ticks += 1000);
}

void
port_sleep_us(uint32_t us)
{
	volatile uint32_t count;

	for (count = 0; count < us; count++) {
		/* no-op */
	}
}

/* ================================================================== */
/* Terminal I/O shims fallback                                        */
/* ================================================================== */

void
port_term_raw_enable(void)
{
}

void
port_term_raw_disable(void)
{
}

void
port_term_dbg_enable(void)
{
}

void
port_term_dbg_disable(void)
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
	(void)buf;
	(void)n;
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	port_size_t i;
	int c;

	if (size == 0) {
		return ((void *)0);
	}
	i = 0;
	for (;;) {
		c = port_term_read_char();
		if (c < 0) {
			continue;
		}
		if (c == '\n' || c == '\r') {
			break;
		}
		if (i < size - 1) {
			buf[i++] = (char)c;
		}
	}
	buf[i] = '\0';
	return (buf);
}

/* ================================================================== */
/* Signal handling shim fallback                                      */
/* ================================================================== */

void
port_signal_setup(port_sig_flag *flag)
{
	(void)flag;
}

void
port_signal_quit(void)
{
}

PORT_NORETURN void
port_exit(int code)
{
	(void)code;
	for (;;) {
		/* halt */
	}
}
