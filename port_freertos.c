/*
 * port_freertos.c - Generic Apple-1 FreeRTOS port.
 *
 * FreeRTOS supplies only kernel and heap APIs.  Console and filesystem I/O
 * are supplied by the board through port_freertos_board.h.  Define
 * APPLE1_FREERTOS_POSIX_SIM only for the official POSIX simulator adapter.
 */

#include "port.h"

#ifdef APPLE1_FREERTOS_POSIX_SIM

#include "port_freertos_posix.c"

#else

#include "FreeRTOS.h"
#include "task.h"
#include "port_freertos_board.h"

#if !defined(APPLE1_ZERO_MALLOC) && !defined(APPLE1_CUSTOM_MALLOC)
#ifndef PORT_DEFAULT_MEM_METHODS

static void *
freertos_malloc(port_size_t sz)
{
	return (pvPortMalloc(sz));
}

static void
freertos_free(void *ptr)
{
	vPortFree(ptr);
}

/* FreeRTOS provides no portable way to resize an allocated block. */
static void *
freertos_realloc(void *ptr, port_size_t sz)
{
	if (ptr == NULL)
		return (pvPortMalloc(sz));
	if (sz == 0) {
		vPortFree(ptr);
		return (NULL);
	}
	return (NULL);
}

static port_result_t
freertos_init(void *app_data)
{
	(void)app_data;
	return (PORT_OK);
}

static void
freertos_shutdown(void *app_data)
{
	(void)app_data;
}

#define PORT_DEFAULT_MEM_METHODS \
	{ freertos_malloc, freertos_free, freertos_realloc, freertos_init, \
		freertos_shutdown, NULL }

#endif
#endif

static port_file_t
freertos_vfs_open(const char *path, int flags)
{
	(void)path;
	(void)flags;
	return (PORT_FILE_INVALID);
}

static void
freertos_vfs_close(port_file_t file)
{
	(void)file;
}

static int
freertos_vfs_read(port_file_t file, void *buf, port_size_t size,
    port_size_t *nread)
{
	(void)file;
	(void)buf;
	(void)size;
	if (nread != NULL)
		*nread = 0;
	return (-1);
}

static int
freertos_vfs_size(port_file_t file, port_size_t *size)
{
	(void)file;
	(void)size;
	return (-1);
}

static int
freertos_vfs_seek(port_file_t file, int32_t offset, int whence)
{
	(void)file;
	(void)offset;
	(void)whence;
	return (-1);
}

static int
freertos_vfs_write(port_file_t file, const void *buf, port_size_t size,
    port_size_t *nwritten)
{
	(void)file;
	(void)buf;
	(void)size;
	if (nwritten != NULL)
		*nwritten = 0;
	return (-1);
}

static int
freertos_vfs_read_line(port_file_t file, char *buf, port_size_t size)
{
	(void)file;
	(void)buf;
	(void)size;
	return (0);
}

static struct port_vfs freertos_vfs = { "freertos", 1,
	freertos_vfs_open,
	freertos_vfs_close,
	freertos_vfs_read,
	freertos_vfs_size,
	freertos_vfs_seek,
	freertos_vfs_write,
	freertos_vfs_read_line };

struct port_vfs *g_port_vfs = &freertos_vfs;

uint32_t
port_gettime_us(void)
{
	TickType_t ticks;

	ticks = xTaskGetTickCount();
	return ((uint32_t)((ticks * 1000000UL) / configTICK_RATE_HZ));
}

void
port_sleep_us(uint32_t us)
{
	TickType_t ticks;

	ticks = (TickType_t)((us * configTICK_RATE_HZ + 999999UL) / 1000000UL);
	if (ticks > 0)
		vTaskDelay(ticks);
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
	return (apple1_freertos_console_read_char());
}

void
port_term_write_buf(const char *buf, port_size_t size)
{
	apple1_freertos_console_write(buf, size);
}

void
port_term_flush(void)
{
	apple1_freertos_console_flush();
}

char *
port_term_read_line(char *buf, port_size_t size)
{
	int ch;
	port_size_t i;

	if (buf == NULL || size == 0)
		return (NULL);
	i = 0;
	for (;;) {
		ch = port_term_read_char();
		if (ch == PORT_TERM_NODATA) {
			vTaskDelay((TickType_t)1);
			continue;
		}
		if (ch == PORT_TERM_EOF)
			return (NULL);
		if (i < size - 1)
			buf[i++] = (char)ch;
		if (ch == '\n' || ch == '\r')
			break;
	}
	buf[i] = '\0';
	return (buf);
}

static port_sig_flag *freertos_quit_flag;

void
port_signal_setup(port_sig_flag *flag)
{
	freertos_quit_flag = flag;
}

void
port_signal_quit(void)
{
	if (freertos_quit_flag != NULL)
		*freertos_quit_flag = 1;
}

PORT_NORETURN void
port_exit(int code)
{
	(void)code;
	vTaskDelete(NULL);
	for (;;)
		;
}

#endif /* APPLE1_FREERTOS_POSIX_SIM */
