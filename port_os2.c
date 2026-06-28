#include "port.h"

#define INCL_DOS
#define INCL_DOSPROFILE
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Use POSIX for memory, terminal, signals, and VFS */
#include "port_posix.c"

/* OS/2 specific timing override */
#undef port_gettime_us
#undef port_sleep_us

uint32_t
port_gettime_us(void)
{
	QWORD timer;
	ULONG freq;
	double ticks;
	ULONG ms;

	freq = 0;
	if (DosTmrQueryFreq(&freq) == 0 && freq != 0 &&
	    DosTmrQueryTime(&timer) == 0) {
		ticks = (double)timer.ulLo +
		    ((double)timer.ulHi * 4294967296.0);
		return ((uint32_t)((ticks * 1000000.0) / freq));
	}
	ms = 0;
	DosQuerySysInfo(QSV_MS_COUNT, QSV_MS_COUNT, &ms, sizeof(ms));
	return ((uint32_t)(ms * 1000));
}

void
port_sleep_us(uint32_t us)
{
	ULONG ms;

	ms = (ULONG)((us + 999) / 1000);
	DosSleep(ms);
}
