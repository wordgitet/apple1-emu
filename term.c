/*
 * term.c - Portable selector for terminal driver implementations.
 *
 * This file is part of the Apple-1 emulator.
 */

#include "port.h"
#include "term_apple1.h"

#ifdef APPLE1_TERM_VT100
#include "term_vt100.c"
#else
#ifdef APPLE1_TERM_DOS
#include "term_dos.c"
#else
#ifdef APPLE1_TERM_ANSI
#include "term_ansi.c"
#else
/* Auto-detect (nested #ifdef only — Plan 9 6c has no #if / #elif) */
#ifdef __MSDOS__
#include "term_dos.c"
#else
#ifdef MSDOS
#include "term_dos.c"
#else
#ifdef __dos__
#include "term_dos.c"
#else
#ifdef __WATCOMC__
#ifdef __DOS__
#include "term_dos.c"
#else
#ifdef __PLAN9__
#include "term_vt100.c"
#else
#ifdef __plan9__
#include "term_vt100.c"
#else
#include "term_ansi.c"
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
