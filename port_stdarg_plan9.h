#ifndef PORT_STDARG_PLAN9_H
#define PORT_STDARG_PLAN9_H

/*
 * Plan 9 native va_list fallback when <u.h> was not included first.
 * Normal mk builds pull in <u.h> from port.h before this header.
 */
#ifndef va_start
#ifndef __STDARG
#define __STDARG
typedef char *va_list;
#define va_start(list, start) list = (char *)(&(start) + 1)
#define va_end(list)
#define va_arg(list, mode) (sizeof(mode) == 1 ? ((mode *)(list += 4))[-4] : \
	sizeof(mode) == 2 ? ((mode *)(list += 4))[-2] : \
	((mode *)(list += sizeof(mode)))[-1])
#define va_copy(dst, src) ((dst) = (src))
#endif
#endif

#endif /* PORT_STDARG_PLAN9_H */
