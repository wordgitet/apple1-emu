#include "port.h"
#include <libc.h>
#include <u.h>

/* Plan 9 uses standard memory allocators and functions */

#if !defined(APPLE1_ZERO_MALLOC) && !defined(APPLE1_CUSTOM_MALLOC)
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
	return ((uint32_t)(nsec() / 1000L));
}

void
port_sleep_us(uint32_t us)
{
	sleep((long)((us + 999) / 1000));
}

void
port_term_raw_enable(void)
{
	/* Plan 9 raw terminal stub */
}

void
port_term_raw_disable(void)
{
	/* Plan 9 raw terminal stub */
}

int
port_term_read_char(void)
{
	/* Plan 9 input stub */
	return (-1);
}

void
port_term_write_buf(const char *buf, port_size_t n)
{
	write(1, buf, n);
}

void
port_signal_setup(port_sig_flag *flag)
{
	(void)flag;
	/* Plan 9 signal handler stub */
}

/* VFS using Plan 9 libc */
static void *
plan9_xOpen(const char *path, int flags)
{
	int fd;
	int mode;

	mode = (flags == PORT_VFS_WRITE) ? ORDWR : OREAD;
	fd = open(path, mode);
	if (fd < 0) {
		return (NULL);
	}
	/* Use fd directly, boxed as pointer */
	return ((void *)(long)fd);
}

static void
plan9_xClose(void *file)
{
	int fd;

	fd = (int)(long)file;
	close(fd);
}

static int
plan9_xRead(void *file, void *buf, port_size_t sz, port_size_t *nread)
{
	int fd;
	long r;

	fd = (int)(long)file;
	r = read(fd, buf, sz);
	if (r < 0) {
		return (-1);
	}
	if (nread != NULL) {
		*nread = (port_size_t)r;
	}
	return (0);
}

static long
plan9_xSize(void *file)
{
	int fd;
	Dir *d;
	long size;

	fd = (int)(long)file;
	d = dirfstat(fd);
	if (d == NULL) {
		return (-1);
	}
	size = (long)d->length;
	free(d);
	return (size);
}

static int
plan9_xSeek(void *file, long offset, int whence)
{
	int fd;
	int w;

	fd = (int)(long)file;
	switch (whence) {
	case PORT_VFS_SEEK_SET:
		w = 0;
		break;
	case PORT_VFS_SEEK_CUR:
		w = 1;
		break;
	case PORT_VFS_SEEK_END:
		w = 2;
		break;
	default:
		return (-1);
	}
	return (seek(fd, offset, w) >= 0 ? 0 : -1);
}

static long
plan9_xWrite(void *file, const void *buf, port_size_t sz)
{
	int fd;
	long r;

	fd = (int)(long)file;
	r = write(fd, (void *)buf, (long)sz);
	return (r);
}

static int
plan9_xReadLine(void *file, char *buf, port_size_t size)
{
	int fd;
	long r;

	fd = (int)(long)file;
	r = read(fd, buf, (long)(size - 1));
	if (r <= 0) {
		return (0);
	}
	buf[r] = '\0';
	return (1);
}

static struct port_vfs plan9_vfs = { plan9_xOpen,
	plan9_xClose,
	plan9_xRead,
	plan9_xSize,
	plan9_xSeek,
	plan9_xWrite,
	plan9_xReadLine };

struct port_vfs *g_port_vfs = &plan9_vfs;
