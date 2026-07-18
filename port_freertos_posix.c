/*
 * port_freertos_posix.c - Compatibility adapter for the FreeRTOS POSIX demo.
 *
 * This adapter is deliberately separate from the generic FreeRTOS port.  It
 * is selected only by APPLE1_FREERTOS_POSIX_SIM and may use POSIX terminal
 * and VFS facilities.
 */

#include "port.h"

#include "port_posix.c"

#ifdef FREERTOS_DEMO

#include "FreeRTOS.h"
#include "task.h"

#if !defined(APPLE1_ZERO_MALLOC) && !defined(APPLE1_CUSTOM_MALLOC)
#ifndef PORT_DEFAULT_MEM_METHODS

static void *
freertos_posix_malloc(port_size_t sz)
{
	return (pvPortMalloc(sz));
}

static void
freertos_posix_free(void *ptr)
{
	vPortFree(ptr);
}

/*
 * FreeRTOS does not specify realloc().  The POSIX demo uses heap_4, which
 * likewise has no allocation-size query, so a resize cannot be implemented
 * safely here.  Returning NULL preserves ptr, as realloc() requires.
 */
static void *
freertos_posix_realloc(void *ptr, port_size_t sz)
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
freertos_posix_init(void *app_data)
{
	(void)app_data;
	return (PORT_OK);
}

static void
freertos_posix_shutdown(void *app_data)
{
	(void)app_data;
}

#define PORT_DEFAULT_MEM_METHODS \
	{ freertos_posix_malloc, freertos_posix_free, freertos_posix_realloc, \
		freertos_posix_init, freertos_posix_shutdown, NULL }

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

	ticks = (TickType_t)((us * configTICK_RATE_HZ + 999999UL) / 1000000UL);
	if (ticks > 0)
		vTaskDelay(ticks);
}

PORT_NORETURN void
port_exit(int code)
{
	(void)code;
	vTaskDelete(NULL);
	for (;;)
		;
}

#endif /* FREERTOS_DEMO */
