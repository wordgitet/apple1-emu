#ifndef APPLE1LIMIT_CHECKS_H
#define APPLE1LIMIT_CHECKS_H

/*
 * Compile-time limit range validation (#error guards).
 * Omitted on Plan 9 / 9front builds (6c preprocessor limitations).
 */

#if APPLE1_MAX_CARDS > 32
#error "APPLE1_MAX_CARDS may not exceed 32"
#endif
#if APPLE1_MAX_CARDS < 1
#error "APPLE1_MAX_CARDS must be at least 1"
#endif

#if APPLE1_MAX_BREAKPOINTS > 64
#error "APPLE1_MAX_BREAKPOINTS may not exceed 64"
#endif

#if APPLE1_MAX_WATCHPOINTS > 32
#error "APPLE1_MAX_WATCHPOINTS may not exceed 32"
#endif

#if APPLE1_MAX_TRACE_LINES > 4096
#error "APPLE1_MAX_TRACE_LINES may not exceed 4096"
#endif

#ifdef APPLE1_ACI_MAX_TAPE_PULSES
#if APPLE1_ACI_MAX_TAPE_PULSES > 16000000
#error "APPLE1_ACI_MAX_TAPE_PULSES may not exceed 16000000"
#endif
#endif

#if APPLE1_STATIC_RAM_SIZE < 4096
#error "APPLE1_STATIC_RAM_SIZE must be at least 4096 (4KB)"
#endif
#if APPLE1_STATIC_RAM_SIZE > 65536
#error "APPLE1_STATIC_RAM_SIZE cannot exceed 65536 (64KB)"
#endif

#if (APPLE1_DEFAULT_RAM_KB * 1024) > APPLE1_STATIC_RAM_SIZE
#error "APPLE1_DEFAULT_RAM_KB is larger than APPLE1_STATIC_RAM_SIZE"
#endif

#endif /* APPLE1LIMIT_CHECKS_H */
