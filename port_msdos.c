#include "port.h"
#include <conio.h>
#if defined(__WATCOMC__)
#include <i86.h>
#else
#include <dos.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
	return ((uint32_t)(clock() * (1000000 / CLOCKS_PER_SEC)));
}

void
port_sleep_us(uint32_t us)
{
	delay((unsigned int)((us + 999) / 1000));
}

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
	if (kbhit() != 0) {
		return (getch());
	}
	return (-1);
}

#if defined(__WATCOMC__) && !defined(_M_I86)
static void
msdos_putc(char c)
{
	union REGS regs;

	port_memset(&regs, 0, sizeof(regs));
	regs.h.ah = 0x0E;
	regs.h.al = (unsigned char)c;
	regs.h.bh = 0;
	int386(0x10, &regs, &regs);
}
#endif

void
port_term_write_buf(const char *buf, port_size_t n)
{
	port_size_t i;

	for (i = 0; i < n; i++) {
#if defined(__WATCOMC__) && !defined(_M_I86)
		msdos_putc(buf[i]);
#else
		putch(buf[i]);
#endif
	}
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	return (fgets(buf, (int)size, stdin));
}

static port_sig_flag *g_sig_flag = NULL;

void
port_signal_setup(port_sig_flag *flag)
{
	g_sig_flag = flag;
}

void
port_signal_quit(void)
{
	if (g_sig_flag != NULL) {
		*g_sig_flag = 1;
	}
}

PORT_NORETURN void
port_exit(int code)
{
	exit(code);
}

static void *
msdos_xOpen(const char *path, int flags)
{
	const char *mode;

	mode = (flags == PORT_VFS_WRITE) ? "w+b" : "rb";
	return ((void *)fopen(path, mode));
}

static void
msdos_xClose(void *file)
{
	if (file != NULL) {
		fclose((FILE *)file);
	}
}

static int
msdos_xRead(void *file, void *buf, port_size_t sz, port_size_t *nread)
{
	port_size_t r;

	if (file == NULL) {
		return (-1);
	}
	r = fread(buf, 1, sz, (FILE *)file);
	if (nread != NULL) {
		*nread = (port_size_t)r;
	}
	return (0);
}

static long
msdos_xSize(void *file)
{
	long current;
	long size;
	FILE *f;

	if (file == NULL) {
		return (-1);
	}
	f = (FILE *)file;
	current = ftell(f);
	if (current < 0) {
		return (-1);
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		return (-1);
	}
	size = ftell(f);
	if (fseek(f, current, SEEK_SET) != 0) {
		return (-1);
	}
	return (size);
}

static int
msdos_xSeek(void *file, long offset, int whence)
{
	int w;

	if (file == NULL) {
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
	return (fseek((FILE *)file, offset, w) == 0 ? 0 : -1);
}

static long
msdos_xWrite(void *file, const void *buf, port_size_t sz)
{
	if (file == NULL) {
		return (-1);
	}
	return ((long)fwrite(buf, 1, sz, (FILE *)file));
}

static int
msdos_xReadLine(void *file, char *buf, port_size_t size)
{
	if (file == NULL || size == 0) {
		return (0);
	}
	if (fgets(buf, (int)size, (FILE *)file) == NULL) {
		return (0);
	}
	return (1);
}

static struct port_vfs msdos_vfs = { msdos_xOpen,
	msdos_xClose,
	msdos_xRead,
	msdos_xSize,
	msdos_xSeek,
	msdos_xWrite,
	msdos_xReadLine };

struct port_vfs *g_port_vfs = &msdos_vfs;
