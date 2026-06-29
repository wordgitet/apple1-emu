/*
 * port_tcc_va.c - TinyCC x86_64 variadic helper (__va_arg).
 *
 * TCC's <stdarg.h> declares __va_arg() in a way that clashes with the
 * compiler builtin, and some installs ship a broken libtcc1.a.  va_start
 * is handled by a macro in port_stdarg.h; this file supplies __va_arg.
 *
 * Derived from TinyCC lib/va_list.c (LGPL).
 */
#if defined(__TINYC__) && defined(__x86_64__) && !defined(_WIN64)

enum port_tcc_va_arg_type {
	PORT_TCC_VA_GEN_REG = 0,
	PORT_TCC_VA_FLOAT_REG = 1,
	PORT_TCC_VA_STACK = 2
};

void *
port_tcc_va_arg(__va_list_struct *ap, int arg_type, int size, int align)
{
	size = (size + 7) & ~7;
	align = (align + 7) & ~7;
	switch ((enum port_tcc_va_arg_type)arg_type) {
	case PORT_TCC_VA_GEN_REG:
		if (ap->gp_offset + (unsigned)size <= 48U) {
			ap->gp_offset += (unsigned)size;
			return (
			    ap->reg_save_area + ap->gp_offset - (unsigned)size);
		}
		goto use_overflow_area;

	case PORT_TCC_VA_FLOAT_REG:
		if (ap->fp_offset < 128U + 48U) {
			ap->fp_offset += 16U;
			if (size == 8) {
				return (
				    ap->reg_save_area + ap->fp_offset - 16U);
			}
			if (ap->fp_offset < 128U + 48U) {
				port_memmove(ap->reg_save_area + ap->fp_offset -
					8U,
				    ap->reg_save_area + ap->fp_offset,
				    8);
				ap->fp_offset += 16U;
				return (
				    ap->reg_save_area + ap->fp_offset - 32U);
			}
		}
		goto use_overflow_area;

	case PORT_TCC_VA_STACK:
	use_overflow_area:
		ap->overflow_arg_area += size;
		ap->overflow_arg_area =
		    (char *)((long long)(ap->overflow_arg_area + align - 1) &
			-(long long)align);
		return (ap->overflow_arg_area - size);

	default:
		port_exit(1);
		return ((void *)0);
	}
}

#endif /* __TINYC__ && __x86_64__ && !_WIN64 */
