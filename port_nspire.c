/*
 * port_nspire.c - TI-Nspire (Ndless) platform port for the Apple-1 emulator.
 *
 * Provides timing, VFS (via newlib's fopen/fread/fwrite), and memory
 * (standard malloc/free via PORT_DEFAULT_MEM_METHODS in port.c).
 * Terminal I/O is in term_nspire.c.
 */

#include "port.h"

#include <libndls.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================================================================== */
/* strdup                                                               */
/* ================================================================== */

char *
port_strdup(const char *str)
{
	port_size_t len;
	char *dup;

	if (str == NULL)
		return (NULL);
	len = port_strlen(str) + 1;
	dup = (char *)port_malloc(len);
	if (dup != NULL)
		port_memcpy(dup, str, len);
	return (dup);
}

/* ================================================================== */
/* Timing                                                               */
/* ================================================================== */

uint32_t
port_gettime_us(void)
{
	/*
	 * clock() advances with CPU work between polls and is fine for
	 * keyboard cadence and speed-cap sleeps.  Do not use gettimeofday()
	 * here: Ndless only exposes a 1 Hz hardware tick, which makes
	 * now_kbd - last_kbd_poll stall for whole seconds.
	 *
	 * PIA throttle (delay_microseconds) is disabled on Nspire via
	 * APPLE1_OMIT_PIA_THROTTLE — clock() must not back busy-wait loops.
	 */
	return ((uint32_t)(clock() * (1000000UL / CLOCKS_PER_SEC)));
}

void
port_sleep_us(uint32_t us)
{
	/* msleep() is Ndless's millisecond sleep; round up. */
	msleep((unsigned)((us + 999U) / 1000U));
}

/* ================================================================== */
/* Signal setup (no-op — ESC handled in main_nspire.c)                */
/* ================================================================== */

void
port_signal_setup(port_sig_flag *flag)
{
	(void)flag;
}

/* ================================================================== */
/* VFS — mapped to stdio (identical to port_posix.c)                   */
/* ================================================================== */

static port_file_t
nspire_vfs_open(const char *path, int flags)
{
	const char *mode;

	mode = (flags == PORT_VFS_WRITE) ? "w+b" : "rb";
	return ((port_file_t)fopen(path, mode));
}

static void
nspire_vfs_close(port_file_t f)
{
	if (f != PORT_FILE_INVALID) {
		fclose((FILE *)f);
	}
}

static int
nspire_vfs_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
{
	size_t r;

	if (f == PORT_FILE_INVALID) {
		return (-1);
	}
	r = fread(buf, 1, sz, (FILE *)f);
	if (nread != NULL) {
		*nread = (port_size_t)r;
	}
	return (0);
}

static int
nspire_vfs_size(port_file_t f, port_size_t *size)
{
	long current;
	long sz;
	FILE *fp;

	if (f == PORT_FILE_INVALID || size == NULL) {
		return (-1);
	}
	fp = (FILE *)f;
	current = ftell(fp);
	if (current < 0) {
		return (-1);
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		return (-1);
	}
	sz = ftell(fp);
	if (fseek(fp, current, SEEK_SET) != 0) {
		return (-1);
	}
	*size = (port_size_t)sz;
	return (0);
}

static int
nspire_vfs_seek(port_file_t f, int32_t offset, int whence)
{
	int w;

	if (f == PORT_FILE_INVALID) {
		return (-1);
	}
	switch (whence) {
	case PORT_VFS_SEEK_SET:
		w = SEEK_SET;
		break;
	case PORT_VFS_SEEK_CUR:
		w = SEEK_CUR;
		break;
	case PORT_VFS_SEEK_END:
		w = SEEK_END;
		break;
	default:
		return (-1);
	}
	return (fseek((FILE *)f, (long)offset, w) == 0 ? 0 : -1);
}

static int
nspire_vfs_write(port_file_t f,
    const void *buf,
    port_size_t sz,
    port_size_t *nwritten)
{
	size_t w;

	if (f == PORT_FILE_INVALID || buf == NULL) {
		return (-1);
	}
	w = fwrite(buf, 1, (size_t)sz, (FILE *)f);
	if (nwritten != NULL) {
		*nwritten = (port_size_t)w;
	}
	if (w != (size_t)sz) {
		return (-1);
	}
	return (0);
}

static int
nspire_vfs_read_line(port_file_t f, char *buf, port_size_t size)
{
	if (f == PORT_FILE_INVALID || size == 0) {
		return (0);
	}
	if (fgets(buf, (int)size, (FILE *)f) == NULL) {
		return (0);
	}
	return (1);
}

static struct port_vfs nspire_vfs = {
	1,
	nspire_vfs_open,
	nspire_vfs_close,
	nspire_vfs_read,
	nspire_vfs_size,
	nspire_vfs_seek,
	nspire_vfs_write,
	nspire_vfs_read_line
};

struct port_vfs *g_port_vfs = &nspire_vfs;

/* ================================================================== */
/* Config data paths (Ndless documents + locate fallback)               */
/* ================================================================== */

#define NSPIRE_DATA_PREFIX "/ndless/"

char *
port_resolve_data_path(const char *path)
{
	char buf[384];
	const char *base;
	const char *docs;
	const char *rel;
	port_file_t f;

	if (path == NULL || path[0] == '\0') {
		return (NULL);
	}
	if (path[0] == '/') {
		return (port_strdup(path));
	}

	docs = get_documents_dir();
	rel = path;
	if (rel[0] == '.' && rel[1] == '/') {
		rel += 2;
	}
	if (port_strncmp(rel, "ndless/", 7) == 0) {
		rel += 7;
	}
	port_snprintf(buf,
	    sizeof(buf),
	    "%s%s%s",
	    docs,
	    NSPIRE_DATA_PREFIX,
	    rel);
	f = port_vfs_default.open(buf, PORT_VFS_READ);
	if (f != PORT_FILE_INVALID) {
		port_vfs_default.close(f);
		return (port_strdup(buf));
	}

	base = strrchr(path, '/');
	if (base != NULL) {
		base++;
	} else {
		base = path;
	}
	if (locate(base, buf, sizeof(buf)) == 0) {
		return (port_strdup(buf));
	}

	return (port_strdup(path));
}
