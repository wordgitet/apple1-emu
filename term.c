/*
 * term.c - Portable selector for terminal driver implementations.
 *
 * This file is part of the Apple-1 emulator.
 */

#include "port.h"
#include "term_apple1.h"

#if defined(APPLE1_TERM_DOS)
#include "term_dos.c"
#elif defined(APPLE1_TERM_ANSI)
#include "term_ansi.c"
#else
/* Auto-detect */
#if defined(__MSDOS__) || defined(MSDOS) || defined(__dos__) || \
    (defined(__WATCOMC__) && defined(__DOS__))
#include "term_dos.c"
#else
#include "term_ansi.c"
#endif
#endif
