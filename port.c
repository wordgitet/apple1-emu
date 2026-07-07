/*
 * port.c - Portable selector for platform-specific port implementations.
 *
 * This file is part of the Apple-1 emulator.
 *
 * Not passed through clang-format: nested #include "*.c" shims hang the
 * formatter.  Format the included port_*.c files instead.
 */

#include "port.h"

#ifndef APPLE1_PORT_PLAN9
#ifdef __TINYC__
#ifdef __x86_64__
#ifndef _WIN64
#include "port_tcc_va.c"
#endif
#endif
#endif
#endif

#include "port_string.c"

#ifdef APPLE1_PORT_BARE
#include "port_bare.c"
#else
#ifdef APPLE1_PORT_POSIX
#include "port_posix.c"
#else
#ifdef APPLE1_PORT_WIN
#include "port_win.c"
#else
#ifdef APPLE1_PORT_MSDOS
#include "port_msdos.c"
#else
#ifdef APPLE1_PORT_PLAN9
#include "port_plan9.c"
#else
#ifdef APPLE1_PORT_FREERTOS
#include "port_freertos.c"
#else
#ifdef APPLE1_PORT_ZEPHYR
#include "port_zephyr.c"
#else
#ifdef APPLE1_PORT_OS2
#include "port_os2.c"
#else
#ifdef APPLE1_PORT_NSPIRE
#include "port_nspire.c"
#else
#ifdef APPLE1_PORT_VXWORKS
#include "port_vxworks.c"
#else
/* Auto-detect (nested #ifdef only — Plan 9 6c has no #if / #elif) */
#ifdef _WIN32
#include "port_win.c"
#else
#ifdef _WIN64
#include "port_win.c"
#else
#ifdef __MSDOS__
#include "port_msdos.c"
#else
#ifdef MSDOS
#include "port_msdos.c"
#else
#ifdef __dos__
#include "port_msdos.c"
#else
#ifdef __WATCOMC__
#ifdef __DOS__
#include "port_msdos.c"
#else
#ifdef __PLAN9__
#include "port_plan9.c"
#else
#ifdef __plan9__
#include "port_plan9.c"
#else
#ifdef __OS2__
#include "port_os2.c"
#else
#ifdef __os2__
#include "port_os2.c"
#else
#ifdef __TINSPIRE__
#include "port_nspire.c"
#else
#ifdef __RTP__
#include "port_vxworks.c"
#else
#ifdef _WRS_KERNEL
#include "port_vxworks.c"
#else
#ifdef __unix__
#include "port_posix.c"
#else
#ifdef __unix
#include "port_posix.c"
#else
#ifdef __APPLE__
#include "port_posix.c"
#else
#ifdef __linux__
#include "port_posix.c"
#else
#ifdef __FreeBSD__
#include "port_posix.c"
#else
#ifdef __NetBSD__
#include "port_posix.c"
#else
#ifdef __OpenBSD__
#include "port_posix.c"
#else
#ifdef __DragonFly__
#include "port_posix.c"
#else
#ifdef __haiku__
#include "port_posix.c"
#else
#ifdef __QNX__
#include "port_posix.c"
#else
#ifdef __rtems__
#include "port_posix.c"
#else
#include "port_bare.c"
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

#ifndef APPLE1_ZERO_MALLOC
#ifndef APPLE1_CUSTOM_MALLOC

/*
 * Default libc mem methods
 */
#ifndef PORT_DEFAULT_MEM_METHODS

#ifdef APPLE1_PORT_PLAN9
/*
 * Native Plan 9: malloc/free/realloc come from libc.h (via port.h).
 * Do not include <stdlib.h> — 6c builds do not use it.
 */
static void *
libc_malloc(port_size_t sz)
{
	return (malloc(sz));
}

static void
libc_free(void *ptr)
{
	free(ptr);
}

static void *
libc_realloc(void *ptr, port_size_t sz)
{
	return (realloc(ptr, sz));
}

static port_result_t
libc_init(void *app_data)
{
	(void)app_data;
	return (PORT_OK);
}

static void
libc_shutdown(void *app_data)
{
	(void)app_data;
}

#define PORT_DEFAULT_MEM_METHODS \
	{ libc_malloc, libc_free, libc_realloc, libc_init, libc_shutdown, NULL }

#else /* !APPLE1_PORT_PLAN9 */

#include <stdlib.h>

static void *
libc_malloc(port_size_t sz)
{
	return (malloc(sz));
}

static void
libc_free(void *ptr)
{
	free(ptr);
}

static void *
libc_realloc(void *ptr, port_size_t sz)
{
	return (realloc(ptr, sz));
}

static port_result_t
libc_init(void *app_data)
{
	(void)app_data;
	return (PORT_OK);
}

static void
libc_shutdown(void *app_data)
{
	(void)app_data;
}

#define PORT_DEFAULT_MEM_METHODS \
	{ libc_malloc, libc_free, libc_realloc, libc_init, libc_shutdown, NULL }

#endif /* APPLE1_PORT_PLAN9 */

#endif

static struct port_mem_methods g_port_mem = PORT_DEFAULT_MEM_METHODS;
static bool g_port_mem_initialized = false;

static port_result_t
port_mem_init(void)
{
	port_result_t rc = PORT_OK;

	if (g_port_mem_initialized == 0) {
		if (g_port_mem.xInit != NULL) {
			rc = g_port_mem.xInit(g_port_mem.pAppData);
		}
		if (rc == PORT_OK) {
			g_port_mem_initialized = true;
		}
	}
	return (rc);
}

void *
port_malloc(port_size_t sz)
{
	if (g_port_mem_initialized == 0) {
		if (port_mem_init() != PORT_OK) {
			return (NULL);
		}
	}
	return (g_port_mem.xMalloc(sz));
}

void
port_free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}
	if (g_port_mem_initialized == 0) {
		if (port_mem_init() != PORT_OK) {
			return;
		}
	}
	g_port_mem.xFree(ptr);
}

void *
port_realloc(void *ptr, port_size_t sz)
{
	if (g_port_mem_initialized == 0) {
		if (port_mem_init() != PORT_OK) {
			return (NULL);
		}
	}
	return (g_port_mem.xRealloc(ptr, sz));
}

port_result_t
port_mem_config(const struct port_mem_methods *methods)
{
	if (g_port_mem_initialized != 0) {
		/* SQLite rule: Cannot reconfigure memory subsystem once initialized */
		return (PORT_ERROR);
	}
	if (methods == NULL || methods->xMalloc == NULL ||
	    methods->xFree == NULL || methods->xRealloc == NULL) {
		return (PORT_ERROR);
	}
	g_port_mem = *methods;
	return (PORT_OK);
}

port_result_t
port_mem_get_config(struct port_mem_methods *methods)
{
	if (methods == NULL) {
		return (PORT_ERROR);
	}
	*methods = g_port_mem;
	return (PORT_OK);
}

#endif /* APPLE1_CUSTOM_MALLOC */
#endif /* APPLE1_ZERO_MALLOC */

void *
port_malloc_zero(port_size_t sz)
{
	void *p;

	p = port_malloc(sz);
	if (p != NULL) {
		port_memset(p, 0, sz);
	}
	return (p);
}

