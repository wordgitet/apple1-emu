#include "port.h"
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004

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
/* Timing shims                                                       */
/* ================================================================== */

uint32_t
port_gettime_us(void)
{
	LARGE_INTEGER freq;
	LARGE_INTEGER counter;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	return ((uint32_t)(counter.QuadPart * 1000000 / freq.QuadPart));
}

void
port_sleep_us(uint32_t us)
{
	Sleep((DWORD)(us / 1000));
}

/* ================================================================== */
/* Terminal I/O shims                                                 */
/* ================================================================== */

static DWORD orig_console_mode;
static int raw_mode_active = 0;

void
port_term_raw_enable(void)
{
	HANDLE h_out;
	DWORD mode;

	h_out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h_out == INVALID_HANDLE_VALUE)
		return;
	if (GetConsoleMode(h_out, &orig_console_mode) == 0)
		return;
	mode = orig_console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (SetConsoleMode(h_out, mode) == 0)
		return;
	raw_mode_active = 1;
}

void
port_term_raw_disable(void)
{
	HANDLE h_out;

	if (raw_mode_active != 0) {
		h_out = GetStdHandle(STD_OUTPUT_HANDLE);
		if (h_out != INVALID_HANDLE_VALUE) {
			SetConsoleMode(h_out, orig_console_mode);
		}
		raw_mode_active = 0;
	}
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
	if (_kbhit() != 0) {
		return (_getch());
	}
	return (-1);
}

void
port_term_write_buf(const char *buf, port_size_t n)
{
	HANDLE h_out;
	DWORD written;

	h_out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h_out == INVALID_HANDLE_VALUE)
		return;
	if (WriteConsoleA(h_out, buf, (DWORD)n, &written, NULL) != 0)
		return;
	if (WriteFile(h_out, buf, (DWORD)n, &written, NULL) != 0)
		return;
	fwrite(buf, 1, (size_t)n, stdout);
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	return (fgets(buf, (int)size, stdin));
}

/* ================================================================== */
/* Signal handling shim                                               */
/* ================================================================== */

static port_sig_flag *g_sig_flag = NULL;

static BOOL WINAPI
ctrl_handler(DWORD type)
{
	if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
		if (g_sig_flag != NULL) {
			*g_sig_flag = 1;
		}
		return (TRUE);
	}
	return (FALSE);
}

void
port_signal_setup(port_sig_flag *flag)
{
	g_sig_flag = flag;
	SetConsoleCtrlHandler(ctrl_handler, TRUE);
}

/* ================================================================== */
/* Windows Virtual File System (VFS)                                  */
/* ================================================================== */

static void *
win_xOpen(const char *path, int flags)
{
	const char *mode;

	mode = (flags == PORT_VFS_WRITE) ? "w+b" : "rb";
	return ((void *)fopen(path, mode));
}

static void
win_xClose(void *file)
{
	if (file != NULL) {
		fclose((FILE *)file);
	}
}

static int
win_xRead(void *file, void *buf, port_size_t sz, port_size_t *nread)
{
	size_t r;

	if (file == NULL) {
		return (-1);
	}
	r = fread(buf, 1, sz, (FILE *)file);
	if (nread != NULL) {
		*nread = (port_size_t)r;
	}
	return (0);
}

static int
win_xSize(void *file, port_size_t *size)
{
	long current;
	long sz;
	FILE *f;

	if (file == NULL || size == NULL) {
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
	sz = ftell(f);
	if (fseek(f, current, SEEK_SET) != 0) {
		return (-1);
	}
	*size = (port_size_t)sz;
	return (0);
}

static int
win_xSeek(void *file, int32_t offset, int whence)
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
	return (fseek((FILE *)file, (long)offset, w) == 0 ? 0 : -1);
}

static int
win_xWrite(void *file, const void *buf, port_size_t sz, port_size_t *nwritten)
{
	size_t w;

	if (file == NULL || buf == NULL) {
		return (-1);
	}
	w = fwrite(buf, 1, (size_t)sz, (FILE *)file);
	if (nwritten != NULL) {
		*nwritten = (port_size_t)w;
	}
	if (w != (size_t)sz) {
		return (-1);
	}
	return (0);
}

static int
win_xReadLine(void *file, char *buf, port_size_t size)
{
	if (file == NULL || size == 0) {
		return (0);
	}
	if (fgets(buf, (int)size, (FILE *)file) == NULL) {
		return (0);
	}
	return (1);
}

static struct port_vfs win_vfs = { win_xOpen,
	win_xClose,
	win_xRead,
	win_xSize,
	win_xSeek,
	win_xWrite,
	win_xReadLine };

struct port_vfs *g_port_vfs = &win_vfs;
