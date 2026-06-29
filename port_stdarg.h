#ifndef PORT_STDARG_H
#define PORT_STDARG_H

/*
 * port_stdarg.h — va_list for port.h
 *
 * Hosted toolchains (gcc, clang, Watcom, DJGPP, MSVC, …): use <stdarg.h>.
 * TinyCC on most targets: TCC's own <stdarg.h> is fine.
 *
 * Exception: TCC x86_64 (non-Windows) — its <stdarg.h> declares __va_arg()
 * in a way that clashes with the compiler builtin, and some installs ship a
 * broken libtcc1.a.  Only that combo gets a local shim; va_start is a macro
 * in this header and port_tcc_va_arg() lives in port_tcc_va.c.
 */
#if defined(__TINYC__) && defined(__x86_64__) && !defined(_WIN64)

#ifndef _STDARG_H
#define _STDARG_H

typedef struct {
	unsigned int gp_offset;
	unsigned int fp_offset;
	union {
		unsigned int overflow_offset;
		char *overflow_arg_area;
	};
	char *reg_save_area;
} __va_list_struct;

typedef __va_list_struct va_list[1];

void *
port_tcc_va_arg(__va_list_struct *ap, int arg_type, int size, int align);

#define va_start(ap, last) \
	(*(ap) = *(__va_list_struct *)((char *)__builtin_frame_address(0) - 24))
#define va_arg(ap, type)                  \
	(*(type *)(port_tcc_va_arg((ap),  \
	    __builtin_va_arg_types(type), \
	    (int)sizeof(type),            \
	    (int)__alignof__(type))))
#define va_copy(dest, src) (*(dest) = *(src))
#define va_end(ap)

#define _VA_LIST_T
typedef va_list __gnuc_va_list;
#define _VA_LIST_DEFINED

#endif /* _STDARG_H */

#else

#include <stdarg.h>

#endif

#endif /* PORT_STDARG_H */
