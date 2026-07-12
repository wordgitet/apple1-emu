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
#include "port_path.c"

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
# ifdef _WIN32
#  define PORT_DETECTED_WIN
# endif
# ifdef _WIN64
#  define PORT_DETECTED_WIN
# endif

# ifdef __MSDOS__
#  define PORT_DETECTED_MSDOS
# endif
# ifdef MSDOS
#  define PORT_DETECTED_MSDOS
# endif
# ifdef __dos__
#  define PORT_DETECTED_MSDOS
# endif
# ifdef __WATCOMC__
#  ifdef __DOS__
#   define PORT_DETECTED_MSDOS
#  endif
# endif

# ifdef __PLAN9__
#  define PORT_DETECTED_PLAN9
# endif
# ifdef __plan9__
#  define PORT_DETECTED_PLAN9
# endif

# ifdef __OS2__
#  define PORT_DETECTED_OS2
# endif
# ifdef __os2__
#  define PORT_DETECTED_OS2
# endif

# ifdef __TINSPIRE__
#  define PORT_DETECTED_NSPIRE
# endif

# ifdef __RTP__
#  define PORT_DETECTED_VXWORKS
# endif
# ifdef _WRS_KERNEL
#  define PORT_DETECTED_VXWORKS
# endif

# ifdef __unix__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __unix
#  define PORT_DETECTED_POSIX
# endif
# ifdef __APPLE__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __linux__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __FreeBSD__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __NetBSD__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __OpenBSD__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __DragonFly__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __haiku__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __QNX__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __rtems__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __ia16__
#  define PORT_DETECTED_POSIX
# endif
# ifdef __VMS
#  define PORT_DETECTED_POSIX
# endif

# ifdef PORT_DETECTED_WIN
#  include "port_win.c"
# else
#  ifdef PORT_DETECTED_MSDOS
#   include "port_msdos.c"
#  else
#   ifdef PORT_DETECTED_PLAN9
#    include "port_plan9.c"
#   else
#    ifdef PORT_DETECTED_OS2
#     include "port_os2.c"
#    else
#     ifdef PORT_DETECTED_NSPIRE
#      include "port_nspire.c"
#     else
#      ifdef PORT_DETECTED_VXWORKS
#       include "port_vxworks.c"
#      else
#       ifdef PORT_DETECTED_POSIX
#        include "port_posix.c"
#       else
#        include "port_bare.c"
#       endif
#      endif
#     endif
#    endif
#   endif
#  endif
# endif

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

