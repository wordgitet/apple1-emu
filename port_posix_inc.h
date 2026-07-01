#ifndef PORT_POSIX_INC_H
#define PORT_POSIX_INC_H

/*
 * port_posix_inc.h -- Feature-test macros for the POSIX port layer.
 *
 * Include this before any system header in port_posix.c,
 * or any translation unit that needs POSIX libc interfaces for the
 * hosted Unix port.  Core emulator sources and other port backends
 * must not include this header.
 *
 * Request POSIX.1-2008 explicitly instead of glibc _DEFAULT_SOURCE or
 * _XOPEN_SOURCE so every toolchain sees the same libc surface
 * (sigaction, clock_gettime, termios, nanosleep) without GNU-only
 * deprecated declarations that break limited preprocessors.
 */

#undef _XOPEN_SOURCE
#undef _DEFAULT_SOURCE
#undef _GNU_SOURCE
#undef _BSD_SOURCE
#undef _SVID_SOURCE
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#endif /* PORT_POSIX_INC_H */
