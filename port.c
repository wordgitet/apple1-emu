/*
 * port.c - Portable selector for platform-specific port implementations.
 *
 * This file is part of the Apple-1 emulator.
 */

#include "port.h"
#include "port_string.c"

#if defined(APPLE1_PORT_BARE)
#include "port_bare.c"
#elif defined(APPLE1_PORT_POSIX)
#include "port_posix.c"
#elif defined(APPLE1_PORT_WIN)
#include "port_win.c"
#elif defined(APPLE1_PORT_MSDOS)
#include "port_msdos.c"
#elif defined(APPLE1_PORT_PLAN9)
#include "port_plan9.c"
#elif defined(APPLE1_PORT_FREERTOS)
#include "port_freertos.c"
#elif defined(APPLE1_PORT_ZEPHYR)
#include "port_zephyr.c"
#elif defined(APPLE1_PORT_OS2)
#include "port_os2.c"
#elif defined(APPLE1_PORT_ELKS)
#include "port_elks.c"
#elif defined(APPLE1_PORT_VXWORKS)
#include "port_vxworks.c"
#else
/* Auto-detect */
#  if defined(_WIN32) || defined(_WIN64)
#    include "port_win.c"
#  elif defined(__MSDOS__) || defined(MSDOS) || defined(__dos__) || \
	(defined(__WATCOMC__) && defined(__DOS__))
#    include "port_msdos.c"
#  elif defined(__PLAN9__) || defined(__plan9__)
#    include "port_plan9.c"
#  elif defined(__OS2__) || defined(__os2__)
#    include "port_os2.c"
#  elif defined(__ELKS__)
#    include "port_elks.c"
#  elif defined(__RTP__) || defined(_WRS_KERNEL)
#    include "port_vxworks.c"
#  elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || \
        defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
        defined(__OpenBSD__) || defined(__DragonFly__) || defined(__haiku__) || \
        defined(__QNX__) || defined(__rtems__)
#    include "port_posix.c"
#  else
#    include "port_bare.c"
#  endif
#endif
