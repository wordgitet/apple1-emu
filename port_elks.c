#include "port.h"
#include <sys/time.h>
#include <unistd.h>

/* Use POSIX for standard memory, terminal, signals, and VFS */
#include "port_posix.c"

/* ELKS timing overrides */
#undef port_gettime_us
#undef port_sleep_us

uint32_t
port_gettime_us(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return ((uint32_t)(tv.tv_sec * 1000000UL + tv.tv_usec));
}

void
port_sleep_us(uint32_t us)
{
	usleep((useconds_t)us);
}
