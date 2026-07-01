#include "FreeRTOS.h"
#include "port.h"
#include "task.h"

#if !defined(APPLE1_ZERO_MALLOC) && !defined(APPLE1_CUSTOM_MALLOC)
void *
port_malloc(port_size_t sz)
{
	return (pvPortMalloc(sz));
}

void
port_free(void *ptr)
{
	vPortFree(ptr);
}

void *
port_realloc(void *ptr, port_size_t sz)
{
	/* FreeRTOS pvPortMalloc has no native realloc. Handle manually. */
	void *nptr;

	if (sz == 0) {
		vPortFree(ptr);
		return (NULL);
	}
	nptr = pvPortMalloc(sz);
	if (nptr != NULL && ptr != NULL) {
		/*
		 * Copy minimum of old size and new size. Because we do not
		 * store the allocated chunk size, we copy sz bytes.
		 */
		port_memcpy(nptr, ptr, sz);
		vPortFree(ptr);
	}
	return (nptr);
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
	TickType_t ticks;

	ticks = xTaskGetTickCount();
	return ((uint32_t)((ticks * 1000000UL) / configTICK_RATE_HZ));
}

void
port_sleep_us(uint32_t us)
{
	TickType_t ticks;

	ticks = (us * configTICK_RATE_HZ + 999999UL) / 1000000UL;
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

void
port_signal_setup(port_sig_flag *flag)
{
	(void)flag;
}

static void *
freertos_xOpen(const char *path, int flags)
{
	(void)path;
	(void)flags;
	return (NULL);
}

static void
freertos_xClose(void *file)
{
	(void)file;
}

static int
freertos_xRead(void *file, void *buf, port_size_t sz, port_size_t *nread)
{
	(void)file;
	(void)buf;
	(void)sz;
	if (nread != NULL) {
		*nread = 0;
	}
	return (0);
}

static int
freertos_xSize(void *file, port_size_t *size)
{
	(void)file;
	(void)size;
	return (-1);
}

static int
freertos_xSeek(void *file, int32_t offset, int whence)
{
	(void)file;
	(void)offset;
	(void)whence;
	return (-1);
}

static int
freertos_xWrite(void *file, const void *buf, port_size_t sz, port_size_t *nwritten)
{
	(void)file;
	(void)buf;
	(void)sz;
	(void)nwritten;
	return (-1);
}

static int
freertos_xReadLine(void *file, char *buf, port_size_t size)
{
	(void)file;
	(void)buf;
	(void)size;
	return (0);
}

static struct port_vfs freertos_vfs = { freertos_xOpen,
	freertos_xClose,
	freertos_xRead,
	freertos_xSize,
	freertos_xSeek,
	freertos_xWrite,
	freertos_xReadLine };

struct port_vfs *g_port_vfs = &freertos_vfs;
