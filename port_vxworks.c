/*
 * port_vxworks.c - VxWorks 7 RTP port for Apple-1 Emulator
 *
 * Uses VxWorks tick/task APIs for timing and POSIX RTP libc for terminal
 * I/O and stdio VFS.  Build with wr-cc -rtp (see vxworks_rtp_build.sh).
 */

#include "port.h"

#if defined(__RTP__) || defined(_WRS_KERNEL)

#include <errno.h>
#include <fcntl.h>
#include <ioLib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <taskLib.h>
#include <termios.h>
#include <tickLib.h>
#include <unistd.h>

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
/* Timing shims (tickGet + taskDelay)                                 */
/* ================================================================== */

uint32_t
port_gettime_us(void)
{
	_Vx_ticks_t ticks;
	_Vx_freq_t rate;

	ticks = tickGet();
	rate = tickRateGet();
	if (rate == 0) {
		return (0);
	}
	return ((uint32_t)(((uint32_t)ticks * 1000000UL) / (uint32_t)rate));
}

void
port_sleep_us(uint32_t us)
{
	_Vx_freq_t rate;
	_Vx_ticks_t ticks;

	if (us == 0) {
		return;
	}
	rate = tickRateGet();
	if (rate == 0) {
		return;
	}
	ticks = (us * (uint32_t)rate + 999999UL) / 1000000UL;
	if (ticks == 0) {
		ticks = 1;
	}
	(void)taskDelay(ticks);
}

/* ================================================================== */
/* Terminal I/O shims                                                 */
/* ================================================================== */

/*
 * VxWorks tty devices keep a separate option word (OPT_ECHO, OPT_LINE,
 * …) behind ioctl(FIOSETOPTIONS).  RTP termios alone often leaves
 * driver echo on, so typed keys appear twice and ^L/^R print literally.
 */
static struct termios orig_termios;
static struct termios dbg_termios;
static int vxworks_term_in = -1;
static int vxworks_term_out = -1;
static int vxworks_term_ctl = -1;
static int vxworks_term_ctl_owned = 0;
static int vxworks_saved_in_options = -1;
static int vxworks_saved_out_options = -1;
static int vxworks_saved_ctl_options = -1;
static int vxworks_ioctl_in_active = 0;
static int vxworks_ioctl_out_active = 0;
static int vxworks_ioctl_ctl_active = 0;
static int vxworks_raw_mode_active = 0;
static int vxworks_dbg_mode_active = 0;

static int
vxworks_term_ctl_fd(void)
{
	char *tty;

	if (vxworks_term_ctl >= 0) {
		return (vxworks_term_ctl);
	}
	tty = ttyname(STDIN_FILENO);
	if (tty == NULL) {
		tty = ttyname(STDOUT_FILENO);
	}
	if (tty == NULL) {
		tty = ttyname(STDERR_FILENO);
	}
	if (tty != NULL) {
		vxworks_term_ctl = open(tty, O_RDWR, 0);
	}
	if (vxworks_term_ctl < 0) {
		vxworks_term_ctl = open("/dev/tty", O_RDWR, 0);
	}
	if (vxworks_term_ctl >= 0) {
		vxworks_term_ctl_owned = 1;
	}
	return (vxworks_term_ctl);
}

static int
vxworks_term_in_fd(void)
{
	int ctl;

	if (vxworks_term_in >= 0) {
		return (vxworks_term_in);
	}
	if (isatty(STDIN_FILENO) != 0) {
		vxworks_term_in = STDIN_FILENO;
	} else {
		ctl = vxworks_term_ctl_fd();
		if (ctl >= 0) {
			vxworks_term_in = ctl;
		} else {
			vxworks_term_in = STDIN_FILENO;
		}
	}
	return (vxworks_term_in);
}

static int
vxworks_term_out_fd(void)
{
	int ctl;
	int in;

	if (vxworks_term_out >= 0) {
		return (vxworks_term_out);
	}
	if (isatty(STDOUT_FILENO) != 0) {
		vxworks_term_out = STDOUT_FILENO;
	} else {
		in = vxworks_term_in_fd();
		ctl = vxworks_term_ctl_fd();
		if (in >= 0) {
			vxworks_term_out = in;
		} else if (ctl >= 0) {
			vxworks_term_out = ctl;
		} else {
			vxworks_term_out = STDOUT_FILENO;
		}
	}
	return (vxworks_term_out);
}

static int
vxworks_termios_fd(void)
{
	return (vxworks_term_in_fd());
}

static void
vxworks_tty_ioctl_raw_one(int fd, int *saved, int *active)
{
	int opts;

	if (fd < 0 || saved == NULL || active == NULL) {
		return;
	}
	if (*active != 0) {
		return;
	}
	if (ioctl(fd, FIOGETOPTIONS, &opts) == 0) {
		*saved = opts;
	} else {
		*saved = OPT_ECHO | OPT_CRMOD | OPT_LINE;
	}
	if (ioctl(fd, FIOSETOPTIONS, OPT_RAW) == 0) {
		*active = 1;
	}
}

static void
vxworks_tty_ioctl_raw_enable_all(void)
{
	int ctl;
	int in;
	int out;

	in = vxworks_term_in_fd();
	out = vxworks_term_out_fd();
	ctl = vxworks_term_ctl_fd();

	vxworks_tty_ioctl_raw_one(in,
	    &vxworks_saved_in_options,
	    &vxworks_ioctl_in_active);
	if (out != in) {
		vxworks_tty_ioctl_raw_one(out,
		    &vxworks_saved_out_options,
		    &vxworks_ioctl_out_active);
	}
	if (ctl >= 0 && ctl != in && ctl != out) {
		vxworks_tty_ioctl_raw_one(ctl,
		    &vxworks_saved_ctl_options,
		    &vxworks_ioctl_ctl_active);
	}
}

static void
vxworks_tty_ioctl_raw_one_disable(int fd, int saved, int *active)
{
	if (fd < 0 || active == NULL || *active == 0 || saved < 0) {
		return;
	}
	(void)ioctl(fd, FIOSETOPTIONS, saved);
	*active = 0;
}

static void
vxworks_tty_ioctl_raw_disable_all(void)
{
	int ctl;
	int in;
	int out;

	in = vxworks_term_in_fd();
	out = vxworks_term_out_fd();
	ctl = vxworks_term_ctl_fd();

	vxworks_tty_ioctl_raw_one_disable(in,
	    vxworks_saved_in_options,
	    &vxworks_ioctl_in_active);
	if (out != in) {
		vxworks_tty_ioctl_raw_one_disable(out,
		    vxworks_saved_out_options,
		    &vxworks_ioctl_out_active);
	}
	if (ctl >= 0 && ctl != in && ctl != out) {
		vxworks_tty_ioctl_raw_one_disable(ctl,
		    vxworks_saved_ctl_options,
		    &vxworks_ioctl_ctl_active);
	}
	vxworks_saved_in_options = -1;
	vxworks_saved_out_options = -1;
	vxworks_saved_ctl_options = -1;
}

void
port_term_raw_enable(void)
{
	struct termios raw;
	int fd;

	(void)vxworks_term_in_fd();
	(void)vxworks_term_out_fd();
	vxworks_tty_ioctl_raw_enable_all();
	fd = vxworks_termios_fd();
	if (tcgetattr(fd, &orig_termios) == 0) {
		raw = orig_termios;
		(void)cfmakeraw(&raw);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		if (tcsetattr(fd, TCSAFLUSH, &raw) == 0) {
			vxworks_raw_mode_active = 1;
		}
	}
	(void)ioctl(fd, FIOFLUSH, 0);
}

void
port_term_raw_disable(void)
{
	int fd;

	fd = vxworks_termios_fd();
	if (vxworks_raw_mode_active != 0) {
		tcsetattr(fd, TCSAFLUSH, &orig_termios);
		vxworks_raw_mode_active = 0;
	}
	vxworks_tty_ioctl_raw_disable_all();
	if (vxworks_term_ctl_owned != 0 && vxworks_term_ctl >= 0) {
		close(vxworks_term_ctl);
		vxworks_term_ctl = -1;
		vxworks_term_ctl_owned = 0;
	}
	vxworks_term_in = -1;
	vxworks_term_out = -1;
}

void
port_term_dbg_enable(void)
{
	struct termios t;
	int fd;

	fd = vxworks_termios_fd();
	if (tcgetattr(fd, &dbg_termios) == 0) {
		t = dbg_termios;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_iflag &= ~(IXON | ICRNL);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		if (tcsetattr(fd, TCSAFLUSH, &t) == 0) {
			vxworks_dbg_mode_active = 1;
		}
	}
}

void
port_term_dbg_disable(void)
{
	int fd;

	fd = vxworks_termios_fd();
	if (vxworks_dbg_mode_active != 0) {
		tcsetattr(fd, TCSAFLUSH, &dbg_termios);
		vxworks_dbg_mode_active = 0;
	}
}

int
port_term_read_char(void)
{
	fd_set readfds;
	struct timeval tv;
	char c;
	int fd;
	int rc;
	ssize_t r;

	fd = vxworks_term_in_fd();

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	rc = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (rc <= 0) {
		return (PORT_TERM_NODATA);
	}
	r = read(fd, &c, 1);
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
	int fd;

	fd = vxworks_term_out_fd();
	(void)write(fd, buf, n);
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	if (fgets(buf, (int)size, stdin) == NULL) {
		return (NULL);
	}
	return (buf);
}

/* ================================================================== */
/* Signal handling shim                                               */
/* ================================================================== */

static port_sig_flag *g_sig_flag = NULL;

static void
vxworks_handle_sigint(int sig)
{
	(void)sig;
	if (g_sig_flag != NULL) {
		*g_sig_flag = 1;
	}
}

void
port_signal_setup(port_sig_flag *flag)
{
	/*
	 * Install a real SIGINT handler so Ctrl+C (converted to SIGINT by
	 * the VxWorks tty driver) sets the quit flag and lets the main loop
	 * exit cleanly.  We use signal(2) instead of sigaction(2) because
	 * sigaction during early RTP startup faulted (SIGILL) on the QEMU
	 * SDK; signal() does not have that problem.
	 */
	g_sig_flag = flag;
	(void)signal(SIGINT, vxworks_handle_sigint);
}

void
port_signal_quit(void)
{
	if (g_sig_flag != NULL) {
		*g_sig_flag = 1;
	}
}

/* ================================================================== */
/* stdio VFS                                                          */
/* ================================================================== */

static port_file_t
vxworks_vfs_open(const char *path, int flags)
{
	const char *mode;

	mode = (flags == PORT_VFS_WRITE) ? "w+b" : "rb";
	return ((port_file_t)fopen(path, mode));
}

static void
vxworks_vfs_close(port_file_t f)
{
	if (f != PORT_FILE_INVALID) {
		fclose((FILE *)f);
	}
}

static int
vxworks_vfs_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
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
vxworks_vfs_size(port_file_t f, port_size_t *size)
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
vxworks_vfs_seek(port_file_t f, int32_t offset, int whence)
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
vxworks_vfs_write(port_file_t f,
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
vxworks_vfs_read_line(port_file_t f, char *buf, port_size_t size)
{
	if (f == PORT_FILE_INVALID || size == 0) {
		return (0);
	}
	if (fgets(buf, (int)size, (FILE *)f) == NULL) {
		return (0);
	}
	return (1);
}

static struct port_vfs vxworks_vfs = { 1,
	vxworks_vfs_open,
	vxworks_vfs_close,
	vxworks_vfs_read,
	vxworks_vfs_size,
	vxworks_vfs_seek,
	vxworks_vfs_write,
	vxworks_vfs_read_line };

struct port_vfs *g_port_vfs = &vxworks_vfs;

PORT_NORETURN void
port_exit(int code)
{
	exit(code);
}

#else
#error "APPLE1_PORT_VXWORKS requires a VxWorks RTP or kernel build (wr-cc -rtp)"
#endif
