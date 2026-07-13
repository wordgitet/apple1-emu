#include "port.h"
#include "port_posix_inc.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __VMS
#include <socket.h>
#else
#include <sys/select.h>
#endif
#ifndef __VMS
#include <termios.h>
#endif
#include <time.h>
#include <unistd.h>
#ifdef __USLC__
#define PORT_USE_GETTIMEOFDAY
#endif
#ifdef __ia16__
#define PORT_USE_GETTIMEOFDAY
#endif
#ifdef __VBCC__
#define PORT_USE_GETTIMEOFDAY
#endif
#ifdef __VMS
#define PORT_USE_GETTIMEOFDAY
#endif

#ifdef PORT_USE_GETTIMEOFDAY
#include <sys/time.h>
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
/* Timing shims                                                       */
/* ================================================================== */

#if (!defined(APPLE1_PORT_FREERTOS) || !defined(FREERTOS_DEMO))

uint32_t
port_gettime_us(void)
{
#ifdef PORT_USE_GETTIMEOFDAY
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == 0) {
		return ((uint32_t)tv.tv_sec * 1000000 + (uint32_t)tv.tv_usec);
	}
	return (0);
#else
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		return ((uint32_t)ts.tv_sec * 1000000 +
		    (uint32_t)(ts.tv_nsec / 1000));
	}
	return (0);
#endif
}

void
port_sleep_us(uint32_t us)
{
#ifdef PORT_USE_GETTIMEOFDAY
	unsigned sec;
	unsigned long rem;

	sec = (unsigned)(us / 1000000);
	rem = (unsigned long)(us % 1000000);
	if (sec > 0)
		sleep(sec);
	if (rem > 0)
		usleep(rem);
#else
	struct timespec req;

	req.tv_sec = (time_t)(us / 1000000);
	req.tv_nsec = (long)((us % 1000000) * 1000);
	nanosleep(&req, NULL);
#endif
}

#endif /* timing: not FreeRTOS demo override */

/* ================================================================== */
/* Terminal I/O shims                                                 */
/* ================================================================== */

#ifndef __VMS
static struct termios orig_termios;
static struct termios dbg_termios;
#endif
static int posix_raw_mode_active = 0;
static int posix_dbg_mode_active = 0;

void
port_term_raw_enable(void)
{
#ifndef __VMS
	struct termios raw;

	if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
		raw = orig_termios;
		raw.c_lflag &= ~(ICANON | ECHO | ISIG);
		raw.c_iflag &= ~(IXON | ICRNL);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
			posix_raw_mode_active = 1;
#ifndef __USLC__
#ifndef APPLE1_OMIT_PASTE
			/* Bracketed paste — xterm-like hosts only. */
			port_term_write_buf("\x1b[?2004h", 8);
#endif
#endif
		}
	}
#endif
}

void
port_term_raw_disable(void)
{
#ifndef __VMS
	if (posix_raw_mode_active != 0) {
#ifndef __USLC__
#ifndef APPLE1_OMIT_PASTE
		port_term_write_buf("\x1b[?2004l", 8);
#endif
#endif
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
		posix_raw_mode_active = 0;
	}
#endif
}

void
port_term_dbg_enable(void)
{
#ifndef __VMS
	struct termios t;

	if (tcgetattr(STDIN_FILENO, &dbg_termios) == 0) {
		t = dbg_termios;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_iflag &= ~(IXON | ICRNL);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) {
			posix_dbg_mode_active = 1;
		}
	}
#endif
}

void
port_term_dbg_disable(void)
{
#ifndef __VMS
	if (posix_dbg_mode_active != 0) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &dbg_termios);
		posix_dbg_mode_active = 0;
	}
#endif
}

int
port_term_read_char(void)
{
	char c;
	ssize_t r;

	r = read(STDIN_FILENO, &c, 1);
	if (r == 1) {
		return ((int)(unsigned char)c);
	}
	if (r == 0) {
		return (PORT_TERM_EOF);
	}
	if (r < 0 && errno == EINTR) {
		return (PORT_TERM_NODATA);
	}
	return (PORT_TERM_NODATA);
}

void
port_term_write_buf(const char *buf, port_size_t n)
{
	(void)write(STDOUT_FILENO, buf, n);
}

void
port_term_flush(void)
{
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

static void
handle_sigint(int sig)
{
	(void)sig;
	if (g_sig_flag != NULL) {
		*g_sig_flag = 1;
	}
}

void
port_signal_setup(port_sig_flag *flag)
{
#ifndef __USLC__
#ifndef __VBCC__
	struct sigaction sa;
#endif
#endif

	g_sig_flag = flag;
#ifdef __USLC__
	signal(SIGINT, handle_sigint);
#else
#ifdef __VBCC__
	signal(SIGINT, handle_sigint);
#else
	port_memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0; /* Don't restart fgets — Ctrl-C reaches dbg prompt */
	sigaction(SIGINT, &sa, NULL);
#endif
#endif
}

void
port_signal_quit(void)
{
	if (g_sig_flag != NULL) {
		*g_sig_flag = 1;
	}
}

/* ================================================================== */
/* POSIX Virtual File System (VFS)                                    */
/*                                                                    */
/* QNX and Haiku are fully POSIX-certified; no proprietary headers    */
/* are needed.  Both are covered by the platform guard in port_posix  */
/* and share this implementation without modification.                */
/* ================================================================== */

static port_file_t
posix_vfs_open(const char *path, int flags)
{
	const char *mode;

	mode = (flags == PORT_VFS_WRITE) ? "w+b" : "rb";
	return ((port_file_t)fopen(path, mode));
}

static void
posix_vfs_close(port_file_t f)
{
	if (f != PORT_FILE_INVALID) {
		fclose((FILE *)f);
	}
}

static int
posix_vfs_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
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
posix_vfs_size(port_file_t f, port_size_t *size)
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
posix_vfs_seek(port_file_t f, int32_t offset, int whence)
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
posix_vfs_write(port_file_t f,
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
posix_vfs_read_line(port_file_t f, char *buf, port_size_t size)
{
	if (f == PORT_FILE_INVALID || size == 0) {
		return (0);
	}
	if (fgets(buf, (int)size, (FILE *)f) == NULL) {
		return (0);
	}
	return (1);
}

static struct port_vfs posix_vfs = { "posix", 1,
	posix_vfs_open,
	posix_vfs_close,
	posix_vfs_read,
	posix_vfs_size,
	posix_vfs_seek,
	posix_vfs_write,
	posix_vfs_read_line };

struct port_vfs *g_port_vfs = &posix_vfs;

/* ================================================================== */
/* Process exit                                                       */
/* ================================================================== */

#if !defined(APPLE1_PORT_FREERTOS) || !defined(FREERTOS_DEMO)

PORT_NORETURN void
port_exit(int code)
{
	exit(code);
}

#endif
