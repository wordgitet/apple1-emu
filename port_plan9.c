/* Plan 9 uses standard memory allocators via port.c runtime dispatch. */

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

#define PLAN9_KBD_QUEUE_SIZE 256

static int plan9_consctl_fd = -1;
static int plan9_kbd_rd;
static int plan9_kbd_wr;
static char plan9_kbd_queue[PLAN9_KBD_QUEUE_SIZE];
static int plan9_kbd_reader_started;

static void
plan9_kbd_reader(void *arg)
{
	char c;
	int next;
	long n;

	(void)arg;

	for (;;) {
		n = read(0, &c, 1);
		if (n != 1) {
			continue;
		}
		next = (plan9_kbd_wr + 1) % PLAN9_KBD_QUEUE_SIZE;
		if (next == plan9_kbd_rd) {
			continue;
		}
		plan9_kbd_queue[plan9_kbd_wr] = c;
		plan9_kbd_wr = next;
	}
}

static void
plan9_kbd_reader_start(void)
{
	int pid;

	if (plan9_kbd_reader_started != 0) {
		return;
	}
	pid = rfork(RFPROC | RFMEM | RFNOWAIT | RFFDG);
	if (pid == 0) {
		plan9_kbd_reader(nil);
		exits(nil);
	}
	if (pid > 0) {
		plan9_kbd_reader_started = 1;
	}
}

void
port_term_raw_enable(void)
{
	if (plan9_consctl_fd < 0) {
		plan9_consctl_fd = open("/dev/consctl", OWRITE);
		if (plan9_consctl_fd >= 0) {
			write(plan9_consctl_fd, "rawon", 5);
		}
	}
	plan9_kbd_reader_start();
}

void
port_term_raw_disable(void)
{
	if (plan9_consctl_fd >= 0) {
		write(plan9_consctl_fd, "rawoff", 6);
		close(plan9_consctl_fd);
		plan9_consctl_fd = -1;
	}
}

int
port_term_read_char(void)
{
	char c;
	int rd;

	rd = plan9_kbd_rd;
	if (rd == plan9_kbd_wr) {
		return (PORT_TERM_NODATA);
	}
	c = plan9_kbd_queue[rd];
	plan9_kbd_rd = (rd + 1) % PLAN9_KBD_QUEUE_SIZE;
	return ((int)(unsigned char)c);
}

void
port_term_dbg_enable(void)
{
}

void
port_term_dbg_disable(void)
{
}

void
port_term_write_buf(const char *buf, port_size_t n)
{
	write(1, buf, (long)n);
}

void
port_term_flush(void)
{
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	port_size_t i;
	int ch;

	if (buf == NULL || size == 0) {
		return (NULL);
	}
	for (i = 0; i < size - 1; i++) {
		ch = port_term_read_char();
		if (ch == PORT_TERM_EOF || ch == PORT_TERM_NODATA) {
			if (i == 0) {
				return (NULL);
			}
			break;
		}
		buf[i] = (char)ch;
		if (ch == '\n' || ch == '\r') {
			i++;
			break;
		}
	}
	buf[i] = '\0';
	return (buf);
}

void
port_signal_setup(port_sig_flag *flag)
{
	(void)flag;
	/* Plan 9 note handler could be wired here later */
}

void
port_signal_quit(void)
{
}

PORT_NORETURN void
port_exit(int code)
{
	if (code != 0) {
		exits("error");
	}
	exits(nil);
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

static int
plan9_xSize(void *file, port_size_t *size)
{
	int fd;
	Dir *d;

	if (file == NULL || size == NULL) {
		return (-1);
	}
	fd = (int)(long)file;
	d = dirfstat(fd);
	if (d == NULL) {
		return (-1);
	}
	*size = (port_size_t)d->length;
	free(d);
	return (0);
}

static int
plan9_xSeek(void *file, int32_t offset, int whence)
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
	return (seek(fd, (long)offset, w) >= 0 ? 0 : -1);
}

static int
plan9_xWrite(void *file, const void *buf, port_size_t sz, port_size_t *nwritten)
{
	int fd;
	long r;

	if (file == NULL || buf == NULL) {
		return (-1);
	}
	fd = (int)(long)file;
	r = write(fd, (void *)buf, (long)sz);
	if (r < 0) {
		return (-1);
	}
	if (nwritten != NULL) {
		*nwritten = (port_size_t)r;
	}
	if ((port_size_t)r != sz) {
		return (-1);
	}
	return (0);
}

static int
plan9_xReadLine(void *file, char *buf, port_size_t size)
{
	char ch;
	int fd;
	long r;
	port_size_t i;

	if (file == NULL || buf == NULL || size == 0) {
		return (0);
	}
	fd = (int)(long)file;
	for (i = 0; i + 1 < size; i++) {
		r = read(fd, &ch, 1);
		if (r <= 0) {
			break;
		}
		buf[i] = ch;
		if (ch == '\n') {
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

static struct port_vfs plan9_vfs = { "plan9", 1,
	plan9_xOpen,
	plan9_xClose,
	plan9_xRead,
	plan9_xSize,
	plan9_xSeek,
	plan9_xWrite,
	plan9_xReadLine };

struct port_vfs *g_port_vfs = &plan9_vfs;
