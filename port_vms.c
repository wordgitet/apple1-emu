/*
 * port_vms.c - OpenVMS port layer implementation.
 *
 * This file is part of the Apple-1 emulator.
 */

#include "port.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <iodef.h>
#include <stsdef.h>
#include <descrip.h>

#ifdef __cplusplus
extern "C" {
#endif
unsigned int sys$assign(void *devnam, unsigned short *chan, unsigned int acmode, void *mbxnam);
unsigned int sys$dassgn(unsigned short chan);
unsigned int sys$qiow(unsigned int efn, unsigned short chan, unsigned int func,
                      void *iosb, void *astadr, void *astprm,
                      void *p1, unsigned int p2, unsigned int p3,
                      unsigned int p4, unsigned int p5, unsigned int p6);
#ifdef __cplusplus
}
#endif

struct vms_iosb {
	unsigned short status;
	unsigned short count;
	unsigned int dev_info;
};

static unsigned short vms_channel = 0;

/* Declare POSIX functions that OpenVMS headers might not prototype */
int gettimeofday(struct timeval *tp, void *tzp);
int usleep(unsigned int useconds);

static port_sig_flag *g_sig_flag = NULL;

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
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == 0) {
		return ((uint32_t)tv.tv_sec * 1000000 + (uint32_t)tv.tv_usec);
	}
	return (0);
}

void
port_sleep_us(uint32_t us)
{
	unsigned sec;
	unsigned long rem;

	sec = (unsigned)(us / 1000000);
	rem = (unsigned long)(us % 1000000);
	if (sec > 0) {
		sleep(sec);
	}
	if (rem > 0) {
		usleep(rem);
	}
}

/* ================================================================== */
/* Terminal I/O shims                                                 */
/* ================================================================== */

static unsigned char vms_orig_char[12];
static int vms_char_saved = 0;

static int
vms_term_write_block(const char *buf, unsigned int n)
{
	struct vms_iosb iosb;
	unsigned int status;

	if (vms_channel == 0) {
		return (-1);
	}
	iosb.status = 0;
	iosb.count = 0;
	iosb.dev_info = 0;
	/*
	 * IO$_WRITEVBLK (48): write raw bytes to the TT channel, bypassing
	 * DECC$ record-oriented stdout carriage control.
	 */
	status = sys$qiow(0, vms_channel, 48, &iosb, 0, 0,
	    (void *)buf, n, 0, 0, 0, 0);
	if ((status & 1) == 0 || (iosb.status & 1) == 0) {
		return (-1);
	}
	return (0);
}

static void
vms_term_write_expand(const char *buf, port_size_t n)
{
	const char crlf[2] = { '\r', '\n' };
	port_size_t i;

	for (i = 0; i < n; i++) {
		if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r')) {
			if (vms_term_write_block(crlf, 2) != 0) {
				(void)fwrite(crlf, 1, 2, stdout);
			}
		} else if (vms_term_write_block(buf + i, 1) != 0) {
			(void)fwrite(buf + i, 1, 1, stdout);
		}
	}
	(void)fflush(stdout);
}

void
port_term_raw_enable(void)
{
	if (vms_channel == 0) {
		struct dsc$descriptor_s dev;
		struct vms_iosb iosb;
		unsigned char new_char[12];
		unsigned int status;

		dev.dsc$w_length = 2;
		dev.dsc$b_dtype = DSC$K_DTYPE_T;
		dev.dsc$b_class = DSC$K_CLASS_S;
		dev.dsc$a_pointer = "TT";
		status = sys$assign(&dev, &vms_channel, 0, 0);
		if (status & 1) {
			/* Get current characteristics using IO$_SENSEMODE (39) */
			iosb.status = 0;
			iosb.count = 0;
			iosb.dev_info = 0;
			status = sys$qiow(0, vms_channel, 39, &iosb, 0, 0,
			                  vms_orig_char, 12, 0, 0, 0, 0);
			if ((status & 1) && (iosb.status & 1)) {
				vms_char_saved = 1;
				port_memcpy(new_char, vms_orig_char, 12);
				/*
				 * Set TT$M_PASSALL (0x01) and TT$M_NOECHO (0x02)
				 * in basic characteristics (bytes 4-7).
				 */
				new_char[4] |= 0x03;
				/* Apply new characteristics using IO$_SETMODE (35) */
				iosb.status = 0;
				status = sys$qiow(0, vms_channel, 35, &iosb, 0, 0,
				                  new_char, 12, 0, 0, 0, 0);
			}
		}
	}
}

void
port_term_raw_disable(void)
{
	if (vms_channel != 0) {
		if (vms_char_saved != 0) {
			struct vms_iosb iosb;
			iosb.status = 0;
			/* Restore original characteristics using IO$_SETMODE (35) */
			(void)sys$qiow(0, vms_channel, 35, &iosb, 0, 0,
			               vms_orig_char, 12, 0, 0, 0, 0);
			vms_char_saved = 0;
		}
		(void)sys$dassgn(vms_channel);
		vms_channel = 0;
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
	struct vms_iosb iosb;
	unsigned int status;
	unsigned short term;
	char c;

	if (vms_channel == 0) {
		port_term_raw_enable();
	}
	if (vms_channel == 0) {
		return (PORT_TERM_NODATA);
	}

	iosb.status  = 0;
	iosb.count   = 0;
	iosb.dev_info = 0;

	/*
	 * IO$_READVBLK (49) | IO$M_TIMED (128) = 177.
	 *
	 * Since we permanently enabled TT$M_PASSALL and TT$M_NOECHO on the
	 * channel via IO$_SETMODE, standard read virtual block performs raw,
	 * non-echoing input without driver-side formatting.
	 */
	status = sys$qiow(0, vms_channel,
	    IO$_READVBLK | IO$M_TIMED,
	    &iosb, 0, 0, &c, 1, 0, 0, 0, 0);

	if (status & 1) {
		if (iosb.status & 1) {
			if (iosb.count == 1) {
				return ((int)(unsigned char)c);
			}
			if (iosb.count == 0) {
				term = (unsigned short)(iosb.dev_info & 0xFFFFU);
				if (term != 0) {
					return ((int)term);
				}
			}
		}
		if (iosb.status == 556) { /* SS$_TIMEOUT — no key pressed */
			return (PORT_TERM_NODATA);
		}
	}
	return (PORT_TERM_EOF);
}

void
port_term_write_buf(const char *buf, port_size_t n)
{
	if (buf == NULL || n == 0) {
		return;
	}
	if (vms_channel == 0) {
		port_term_raw_enable();
	}
	/*
	 * Lone LF from dumb-terminal output must become CR+LF on the raw TT
	 * channel so the cursor returns to column 0 after each line.
	 */
	vms_term_write_expand(buf, n);
}

void
port_term_flush(void)
{
	/* Channel writes are synchronous; no RTL buffer to flush. */
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	return (fgets(buf, (int)size, stdin));
}

/* ================================================================== */
/* Signal handling shim                                               */
/* ================================================================== */

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
	g_sig_flag = flag;
	signal(SIGINT, handle_sigint);
}

void
port_signal_quit(void)
{
	if (g_sig_flag != NULL) {
		*g_sig_flag = 1;
	}
}

/* ================================================================== */
/* OpenVMS Virtual File System (VFS)                                  */
/* ================================================================== */

static port_file_t
vms_vfs_open(const char *path, int flags)
{
	const char *mode;

	mode = (flags == PORT_VFS_WRITE) ? "w+b" : "rb";
	return ((port_file_t)fopen(path, mode));
}

static void
vms_vfs_close(port_file_t f)
{
	if (f != PORT_FILE_INVALID) {
		fclose((FILE *)f);
	}
}

static int
vms_vfs_read(port_file_t f, void *buf, port_size_t sz, port_size_t *nread)
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
vms_vfs_size(port_file_t f, port_size_t *size)
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
vms_vfs_seek(port_file_t f, int32_t offset, int whence)
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
vms_vfs_write(port_file_t f,
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
vms_vfs_read_line(port_file_t f, char *buf, port_size_t size)
{
	if (f == PORT_FILE_INVALID || size == 0) {
		return (0);
	}
	if (fgets(buf, (int)size, (FILE *)f) == NULL) {
		return (0);
	}
	return (1);
}

static struct port_vfs vms_vfs = { "vms", 1,
	vms_vfs_open,
	vms_vfs_close,
	vms_vfs_read,
	vms_vfs_size,
	vms_vfs_seek,
	vms_vfs_write,
	vms_vfs_read_line };

struct port_vfs *g_port_vfs = &vms_vfs;

/* ================================================================== */
/* Process exit                                                       */
/* ================================================================== */

PORT_NORETURN void
port_exit(int code)
{
	exit(code);
}
