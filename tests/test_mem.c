#include "../port.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * test_mem.c -- Verify port_mem_config runtime dispatch before first use.
 */

static int g_alloc_count;
static int g_free_count;

static void *
test_malloc(port_size_t sz)
{
	(void)sz;
	g_alloc_count++;
	return (malloc((size_t)sz));
}

static void
test_free(void *ptr)
{
	g_free_count++;
	free(ptr);
}

static void *
test_realloc(void *ptr, port_size_t sz)
{
	return (realloc(ptr, (size_t)sz));
}

static port_result_t
test_init(void *app_data)
{
	(void)app_data;
	return (PORT_OK);
}

static void
test_shutdown(void *app_data)
{
	(void)app_data;
}

int
main(void)
{
	struct port_mem_methods custom;
	struct port_mem_methods got;
	void *p;

	custom.xMalloc = test_malloc;
	custom.xFree = test_free;
	custom.xRealloc = test_realloc;
	custom.xInit = test_init;
	custom.xShutdown = test_shutdown;
	custom.pAppData = NULL;

	assert(port_mem_config(NULL) == PORT_ERROR);
	assert(port_mem_config(&custom) == PORT_OK);
	assert(port_mem_get_config(NULL) == PORT_ERROR);
	assert(port_mem_get_config(&got) == PORT_OK);
	assert(got.xMalloc == test_malloc);
	assert(got.xFree == test_free);

	p = port_malloc(32);
	assert(p != NULL);
	assert(g_alloc_count == 1);

	port_free(p);
	assert(g_free_count == 1);

	assert(port_mem_config(&custom) == PORT_ERROR);

	printf("test_mem passed.\n");
	return (0);
}
