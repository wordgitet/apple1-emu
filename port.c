/*
 * port.c - Portable selector for platform-specific port implementations.
 *
 * This file is part of the Apple-1 emulator.
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
