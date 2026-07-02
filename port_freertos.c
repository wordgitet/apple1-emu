/*
 * port_freertos.c - FreeRTOS POSIX simulator port for Apple-1 Emulator
 *
 * Reuses port_posix.c for terminal I/O and VFS on a POSIX host.  When
 * FREERTOS_DEMO is defined (Posix_GCC demo link), heap and timing call
 * FreeRTOS APIs instead of libc.
 */

#include "port.h"

#if defined(__linux__) || defined(__APPLE__)

#include "port_posix_inc.h"
#include "port_posix.c"

#ifdef FREERTOS_DEMO

#include "FreeRTOS.h"
#include "task.h"

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

static void *
freertos_realloc(void *ptr, port_size_t sz)
{
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
	{ freertos_malloc, freertos_free, freertos_realloc, \
	freertos_init, freertos_shutdown, NULL }

#endif
#endif

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
	if (ticks > 0) {
		vTaskDelay(ticks);
	}
}

PORT_NORETURN void
port_exit(int code)
{
	(void)code;
	vTaskDelete(NULL);
	for (;;);
}

#endif /* FREERTOS_DEMO */

#else
#error "APPLE1_PORT_FREERTOS requires a POSIX host (FreeRTOS Posix_GCC simulator)"
#endif
