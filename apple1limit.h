/*
 * apple1limit.h - Compile-time limits and defaults for the Apple-1 emulator.
 *
 * This file is standard C89/C99 compatible and contains safety guards
 * to prevent compiler settings from exceeding safe architectural bounds.
 */
#ifndef APPLE1LIMIT_H
#define APPLE1LIMIT_H

/*
 * Maximum number of expansion cards supported in the bus.
 */
#ifndef APPLE1_MAX_CARDS
#define APPLE1_MAX_CARDS 8
#endif

/*
 * Maximum number of breakpoints in the debugger.
 */
#ifndef APPLE1_MAX_BREAKPOINTS
#define APPLE1_MAX_BREAKPOINTS 16
#endif

/*
 * Maximum number of watchpoints in the debugger.
 */
#ifndef APPLE1_MAX_WATCHPOINTS
#define APPLE1_MAX_WATCHPOINTS 8
#endif

/*
 * Maximum lines of debugger circular trace buffer.
 */
#ifndef APPLE1_MAX_TRACE_LINES
#define APPLE1_MAX_TRACE_LINES 64
#endif

/*
 * Maximum number of static tape pulse slots.
 * Define APPLE1_ACI_MAX_TAPE_PULSES to enable zero-malloc ACI mode
 * (analogous to SQLITE_ENABLE_MEMSYS5).  If undefined, aci.c uses
 * malloc/realloc as normal.
 */
#ifdef APPLE1_ACI_MAX_TAPE_PULSES
/* value supplied by build */
#endif

/*
 * Maximum static RAM size in bytes for the emulator (default 65536).
 * Can be overridden to compile for memory-constrained targets (e.g. 4096).
 */
#ifndef APPLE1_STATIC_RAM_SIZE
#define APPLE1_STATIC_RAM_SIZE 65536
#endif

/*
 * Default parameters.
 */
#ifndef APPLE1_DEFAULT_RAM_KB
#define APPLE1_DEFAULT_RAM_KB 8
#endif

#ifndef APPLE1_DEFAULT_CPU_HZ
#define APPLE1_DEFAULT_CPU_HZ 1000000
#endif

#ifndef APPLE1_DEFAULT_BAUD
#define APPLE1_DEFAULT_BAUD 0
#endif

#ifndef APPLE1_PORT_PLAN9
#include "apple1limit_host.h"
#endif

/*
 * Implied OMIT relationships.
 * APPLE1_OMIT_DEBUGGER implies APPLE1_OMIT_DISASM.
 * APPLE1_OMIT_ACI      implies APPLE1_OMIT_WAV.
 */
#ifdef APPLE1_OMIT_DEBUGGER
#ifndef APPLE1_OMIT_DISASM
#define APPLE1_OMIT_DISASM
#endif
#endif
#ifdef APPLE1_OMIT_ACI
#ifndef APPLE1_OMIT_WAV
#define APPLE1_OMIT_WAV
#endif
#endif

/*
 * Omit standard library I/O (printf, fprintf, stderr, stdout).
 * Useful for embedded platforms lacking console streams.
 */
#ifndef APPLE1_OMIT_STDIO
/* #define APPLE1_OMIT_STDIO */
#endif

#endif /* APPLE1LIMIT_H */
