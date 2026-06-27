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
#  define APPLE1_MAX_CARDS 8
#endif
#if APPLE1_MAX_CARDS > 32
#  error "APPLE1_MAX_CARDS may not exceed 32"
#endif
#if APPLE1_MAX_CARDS < 1
#  error "APPLE1_MAX_CARDS must be at least 1"
#endif

/*
 * Maximum number of breakpoints in the debugger.
 */
#ifndef APPLE1_MAX_BREAKPOINTS
#  define APPLE1_MAX_BREAKPOINTS 16
#endif
#if APPLE1_MAX_BREAKPOINTS > 64
#  error "APPLE1_MAX_BREAKPOINTS may not exceed 64"
#endif

/*
 * Maximum number of watchpoints in the debugger.
 */
#ifndef APPLE1_MAX_WATCHPOINTS
#  define APPLE1_MAX_WATCHPOINTS 8
#endif
#if APPLE1_MAX_WATCHPOINTS > 32
#  error "APPLE1_MAX_WATCHPOINTS may not exceed 32"
#endif

/*
 * Maximum lines of debugger circular trace buffer.
 */
#ifndef APPLE1_MAX_TRACE_LINES
#  define APPLE1_MAX_TRACE_LINES 64
#endif
#if APPLE1_MAX_TRACE_LINES > 4096
#  error "APPLE1_MAX_TRACE_LINES may not exceed 4096"
#endif

/*
 * Maximum number of static tape pulse slots.
 * Define APPLE1_ACI_MAX_TAPE_PULSES to enable zero-malloc ACI mode
 * (analogous to SQLITE_ENABLE_MEMSYS5).  If undefined, aci.c uses
 * malloc/realloc as normal.
 */
#ifdef APPLE1_ACI_MAX_TAPE_PULSES
#  if APPLE1_ACI_MAX_TAPE_PULSES > 16000000
#    error "APPLE1_ACI_MAX_TAPE_PULSES may not exceed 16000000"
#  endif
#endif

/*
 * Default parameters.
 */
#ifndef APPLE1_DEFAULT_RAM_KB
#  define APPLE1_DEFAULT_RAM_KB 8
#endif

#ifndef APPLE1_DEFAULT_CPU_HZ
#  define APPLE1_DEFAULT_CPU_HZ 1000000
#endif

#ifndef APPLE1_DEFAULT_BAUD
#  define APPLE1_DEFAULT_BAUD 0
#endif

/*
 * Implied OMIT relationships.
 * APPLE1_OMIT_DEBUGGER implies APPLE1_OMIT_DISASM.
 * APPLE1_OMIT_ACI      implies APPLE1_OMIT_WAV.
 */
#if defined(APPLE1_OMIT_DEBUGGER) && !defined(APPLE1_OMIT_DISASM)
#  define APPLE1_OMIT_DISASM
#endif
#if defined(APPLE1_OMIT_ACI) && !defined(APPLE1_OMIT_WAV)
#  define APPLE1_OMIT_WAV
#endif

#endif /* APPLE1LIMIT_H */
