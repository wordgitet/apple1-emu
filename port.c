/*
 * port.c - Platform port implementation (POSIX).
 *
 * This file is part of the Apple-1 emulator.
 *
 * Not passed through clang-format: nested #include "*.c" shims hang the
 * formatter.  Format the included port_*.c files instead.
 */

#include "port.h"

#ifdef __TINYC__
#ifdef __x86_64__
#ifndef _WIN64
#include "port_tcc_va.c"
#endif
#endif
#endif

#include "port_string.c"
#include "port_path.c"
#include "port_posix.c"

/* ================================================================== */
/* Memory allocator implementation                                    */
/* ================================================================== */

#ifndef APPLE1_ZERO_MALLOC
#ifndef APPLE1_CUSTOM_MALLOC

#ifndef PORT_DEFAULT_MEM_METHODS

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

#endif /* PORT_DEFAULT_MEM_METHODS */

static struct port_mem_methods g_port_mem = PORT_DEFAULT_MEM_METHODS;
static bool g_port_mem_initialized = false;

static port_result_t
port_mem_init(void)
{
	port_result_t rc;

	rc = PORT_OK;
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
