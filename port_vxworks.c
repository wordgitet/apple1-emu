#include "port.h"
#include <sysLib.h>
#include <taskLib.h>
#include <tickLib.h>

/* Use POSIX for memory, terminal, signals, and VFS */
#include "port_posix.c"

/* VxWorks specific timing override */
#undef port_gettime_us
#undef port_sleep_us

uint32_t
port_gettime_us(void)
{
	return ((uint32_t)tickGet() * 1000000 / sysClkRateGet());
}

void
port_sleep_us(uint32_t us)
{
	taskDelay((int)(us * sysClkRateGet() / 1000000) + 1);
}
