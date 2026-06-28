#include "port.h"

/*
 * port_string.c -- Freestanding port shims with zero system headers.
 *
 * String, memory, formatting, character classification, getopt, and RNG
 * helpers shared by every platform port file.
 */

/* ================================================================== */
/* Error code system                                                   */
/* ================================================================== */

const char *
port_error_string(port_result_t rc)
{
	switch (rc) {
	case PORT_OK:
		return ("ok");
	case PORT_ERROR:
		return ("error");
	case PORT_NOMEM:
		return ("out of memory");
	case PORT_IO:
		return ("I/O error");
	case PORT_CORRUPT:
		return ("corrupt");
	case PORT_FULL:
		return ("full");
	case PORT_CANTOPEN:
		return ("cannot open");
	case PORT_PROTOCOL:
		return ("protocol error");
	case PORT_INVALID:
		return ("invalid parameter");
	case PORT_MISUSE:
		return ("misuse");
	case PORT_RANGE:
		return ("out of range");
	case PORT_NOTFOUND:
		return ("not found");
	default:
		return ("unknown error");
	}
}

/* ================================================================== */
/* Character classification shims                                     */
/* ================================================================== */

int
port_isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
	    c == '\f');
}

int
port_isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

int
port_isxdigit(int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
	    (c >= 'A' && c <= 'F');
}

int
port_isalpha(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int
port_toupper(int c)
{
	if (c >= 'a' && c <= 'z') {
		return (c - 32);
	}
	return (c);
}

int
port_tolower(int c)
{
	if (c >= 'A' && c <= 'Z') {
		return (c + 32);
	}
	return (c);
}

int
port_isalnum(int c)
{
	return (port_isalpha(c) || port_isdigit(c));
}

/* ================================================================== */
/* String utility shims                                               */
/* ================================================================== */

port_size_t
port_strlen(const char *s)
{
	port_size_t len;

	if (s == (void *)0) {
		return (0);
	}
	len = 0;
	while (s[len] != '\0') {
		len++;
	}
	return (len);
}

char *
port_strncpy(char *dst, const char *src, port_size_t n)
{
	port_size_t i;

	if (dst == (void *)0 || src == (void *)0) {
		return (dst);
	}
	for (i = 0; i < n && src[i] != '\0'; i++) {
		dst[i] = src[i];
	}
	for (; i < n; i++) {
		dst[i] = '\0';
	}
	return (dst);
}

int
port_strcmp(const char *a, const char *b)
{
	if (a == (void *)0) {
		return (b == (void *)0 ? 0 : -1);
	}
	if (b == (void *)0) {
		return (1);
	}
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (*(const unsigned char *)a - *(const unsigned char *)b);
}

int
port_strncmp(const char *a, const char *b, port_size_t n)
{
	if (n == 0) {
		return (0);
	}
	if (a == (void *)0) {
		return (b == (void *)0 ? 0 : -1);
	}
	if (b == (void *)0) {
		return (1);
	}
	while (n > 1 && *a != '\0' && *a == *b) {
		a++;
		b++;
		n--;
	}
	return (*(const unsigned char *)a - *(const unsigned char *)b);
}

char *
port_strchr(const char *s, int c)
{
	if (s == (void *)0) {
		return ((void *)0);
	}
	while (*s != '\0') {
		if (*s == (char)c) {
			return ((char *)s);
		}
		s++;
	}
	if (c == '\0') {
		return ((char *)s);
	}
	return ((void *)0);
}

char *
port_strstr(const char *hay, const char *needle)
{
	port_size_t i, len;
	port_size_t hay_len;

	if (hay == (void *)0 || needle == (void *)0) {
		return ((void *)0);
	}
	if (*needle == '\0') {
		return ((char *)hay);
	}
	len = port_strlen(needle);
	hay_len = port_strlen(hay);
	if (len > hay_len) {
		return ((void *)0);
	}
	while (*hay != '\0' && hay_len >= len) {
		for (i = 0; i < len; i++) {
			if (hay[i] != needle[i]) {
				break;
			}
		}
		if (i == len) {
			return ((char *)hay);
		}
		hay++;
		hay_len--;
	}
	return ((void *)0);
}

long
port_strtol(const char *s, char **endptr, int base)
{
	long result;
	int sign;

	if (s == (void *)0) {
		if (endptr != (void *)0) {
			*endptr = (void *)0;
		}
		return (0);
	}
	if (base != 0 && (base < 2 || base > 36)) {
		if (endptr != (void *)0) {
			*endptr = (char *)s;
		}
		return (0);
	}
	result = 0;
	sign = 1;
	while (port_isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '-') {
		sign = -1;
		s++;
	} else if (*s == '+') {
		s++;
	}
	if (base == 0) {
		if (*s == '0') {
			s++;
			if (*s == 'x' || *s == 'X') {
				base = 16;
				s++;
			} else {
				base = 8;
			}
		} else {
			base = 10;
		}
	} else if (base == 16) {
		if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
			s += 2;
		}
	}
	while (*s != '\0') {
		int digit;
		char c;
		long new_result;

		c = *s;
		if (port_isdigit((unsigned char)c)) {
			digit = c - '0';
		} else if (c >= 'a' && c <= 'f') {
			digit = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			digit = c - 'A' + 10;
		} else {
			break;
		}
		if (digit >= base) {
			break;
		}
		new_result = result * base + digit;
		if (sign == 1 && new_result < result) {
			result = LONG_MAX;
			break;
		}
		if (sign == -1 && new_result < result) {
			result = LONG_MIN;
			break;
		}
		result = new_result;
		s++;
	}
	if (endptr != (void *)0) {
		*endptr = (char *)s;
	}
	return (result * sign);
}

unsigned long
port_strtoul(const char *s, char **endptr, int base)
{
	unsigned long result;

	if (s == (void *)0) {
		if (endptr != (void *)0) {
			*endptr = (void *)0;
		}
		return (0);
	}
	if (base != 0 && (base < 2 || base > 36)) {
		if (endptr != (void *)0) {
			*endptr = (char *)s;
		}
		return (0);
	}
	result = 0;
	while (port_isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '+') {
		s++;
	}
	if (base == 0) {
		if (*s == '0') {
			s++;
			if (*s == 'x' || *s == 'X') {
				base = 16;
				s++;
			} else {
				base = 8;
			}
		} else {
			base = 10;
		}
	} else if (base == 16) {
		if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
			s += 2;
		}
	}
	while (*s != '\0') {
		int digit;
		char c;
		unsigned long new_result;

		c = *s;
		if (port_isdigit((unsigned char)c)) {
			digit = c - '0';
		} else if (c >= 'a' && c <= 'f') {
			digit = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			digit = c - 'A' + 10;
		} else {
			break;
		}
		if (digit >= base) {
			break;
		}
		new_result = result * base + digit;
		if (new_result < result) {
			result = ULONG_MAX;
			break;
		}
		result = new_result;
		s++;
	}
	if (endptr != (void *)0) {
		*endptr = (char *)s;
	}
	return (result);
}

/* ================================================================== */
/* Memory utility shims                                               */
/* ================================================================== */

void *
port_memcpy(void *dst, const void *src, port_size_t n)
{
	char *d;
	const char *s;
	port_size_t i;

	if (dst == (void *)0 || src == (void *)0) {
		return (dst);
	}
	d = (char *)dst;
	s = (const char *)src;
	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return (dst);
}

void *
port_memset(void *dst, int c, port_size_t n)
{
	char *d;
	port_size_t i;

	if (dst == (void *)0) {
		return (dst);
	}
	d = (char *)dst;
	for (i = 0; i < n; i++) {
		d[i] = (char)c;
	}
	return (dst);
}

void *
port_memmove(void *dst, const void *src, port_size_t n)
{
	char *d;
	const char *s;
	port_size_t i;

	if (dst == (void *)0 || src == (void *)0) {
		return (dst);
	}
	d = (char *)dst;
	s = (const char *)src;
	if (d < s) {
		for (i = 0; i < n; i++) {
			d[i] = s[i];
		}
	} else if (d > s) {
		for (i = n; i > 0; i--) {
			d[i - 1] = s[i - 1];
		}
	}
	return (dst);
}

int
port_memcmp(const void *a, const void *b, port_size_t n)
{
	const unsigned char *sa;
	const unsigned char *sb;
	port_size_t i;

	if (a == (void *)0 || b == (void *)0) {
		return (a == b ? 0 : (a == (void *)0 ? -1 : 1));
	}
	sa = (const unsigned char *)a;
	sb = (const unsigned char *)b;
	for (i = 0; i < n; i++) {
		if (sa[i] != sb[i]) {
			return (sa[i] - sb[i]);
		}
	}
	return (0);
}

/* ================================================================== */
/* Custom formatting engine                                           */
/* ================================================================== */

static void
format_unsigned(char *buf,
    port_size_t *written,
    port_size_t limit,
    unsigned long val,
    int base,
    int is_upper,
    int min_width,
    int zero_pad)
{
	char temp[32];
	int i;
	int digits;
	int pad;

	i = 0;
	if (val == 0) {
		temp[i++] = '0';
	} else {
		while (val > 0) {
			int rem;

			rem = val % base;
			if (rem < 10) {
				temp[i++] = '0' + rem;
			} else {
				temp[i++] = (is_upper ? 'A' : 'a') + (rem - 10);
			}
			val /= base;
		}
	}
	digits = i;
	pad = min_width - digits;
	if (pad > 0 && zero_pad == 0) {
		while (pad-- > 0) {
			if (*written < limit) {
				buf[(*written)++] = ' ';
			}
		}
	}
	if (pad > 0 && zero_pad != 0) {
		while (pad-- > 0) {
			if (*written < limit) {
				buf[(*written)++] = '0';
			}
		}
	}
	while (digits > 0) {
		if (*written < limit) {
			buf[(*written)++] = temp[--digits];
		} else {
			digits--;
		}
	}
}

int
port_vsnprintf(char *buf, port_size_t n, const char *fmt, va_list ap)
{
	port_size_t written;
	port_size_t limit;

	if (fmt == (void *)0) {
		if (buf != (void *)0 && n > 0) {
			buf[0] = '\0';
		}
		return (0);
	}
	written = 0;
	limit = (n > 0) ? (n - 1) : 0;
	while (*fmt != '\0') {
		if (*fmt == '%') {
			int zero_pad;
			int min_width;
			int is_long;

			fmt++;
			zero_pad = 0;
			min_width = 0;
			is_long = 0;
			if (*fmt == '0') {
				zero_pad = 1;
				fmt++;
			}
			while (port_isdigit((unsigned char)*fmt)) {
				min_width = min_width * 10 + (*fmt - '0');
				fmt++;
			}
			if (*fmt == 'l') {
				is_long = 1;
				fmt++;
			}
			if (*fmt == '\0') {
				if (written < limit) {
					buf[written++] = '%';
				}
				break;
			}
			switch (*fmt) {
			case 's': {
				const char *str;
				port_size_t len;
				port_size_t i;

				str = va_arg(ap, const char *);
				if (str == (void *)0) {
					str = "(null)";
				}
				len = port_strlen(str);
				for (i = 0; i < len; i++) {
					if (written < limit) {
						buf[written++] = str[i];
					}
				}
				break;
			}
			case 'd':
			case 'i': {
				long val;

				if (is_long != 0) {
					val = va_arg(ap, long);
				} else {
					val = va_arg(ap, int);
				}
				if (val < 0) {
					if (written < limit) {
						buf[written++] = '-';
					}
					val = -val;
				}
				format_unsigned(buf,
				    &written,
				    limit,
				    (unsigned long)val,
				    10,
				    0,
				    min_width,
				    zero_pad);
				break;
			}
			case 'u': {
				unsigned long val;

				if (is_long != 0) {
					val = va_arg(ap, unsigned long);
				} else {
					val = va_arg(ap, unsigned int);
				}
				format_unsigned(buf,
				    &written,
				    limit,
				    val,
				    10,
				    0,
				    min_width,
				    zero_pad);
				break;
			}
			case 'x':
			case 'X': {
				unsigned long val;

				if (is_long != 0) {
					val = va_arg(ap, unsigned long);
				} else {
					val = va_arg(ap, unsigned int);
				}
				format_unsigned(buf,
				    &written,
				    limit,
				    val,
				    16,
				    (*fmt == 'X'),
				    min_width,
				    zero_pad);
				break;
			}
			case 'c': {
				int c;

				c = va_arg(ap, int);
				if (written < limit) {
					buf[written++] = (char)c;
				}
				break;
			}
			case '%':
				if (written < limit) {
					buf[written++] = '%';
				}
				break;
			default:
				/* Unrecognised specifier: print verbatim */
				if (written < limit) {
					buf[written++] = '%';
				}
				if (*fmt != '\0' && written < limit) {
					buf[written++] = *fmt;
				}
				break;
			}
		} else {
			if (written < limit) {
				buf[written++] = *fmt;
			}
		}
		fmt++;
	}
	if (n > 0) {
		buf[written] = '\0';
	}
	return ((int)written);
}

int
port_snprintf(char *buf, port_size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = port_vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return (r);
}

/* ================================================================== */
/* Pseudo-random number generator shims                               */
/* ================================================================== */

static uint32_t g_rand_state = 0xACE1;

uint32_t
port_rand(void)
{
	uint32_t x;

	x = g_rand_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g_rand_state = x;
	return (x);
}

void
port_srand(uint32_t seed)
{
	g_rand_state = seed ? seed : 0xACE1;
}

/* ================================================================== */
/* Getopt arguments parsing implementation                            */
/* ================================================================== */

char *port_optarg = (void *)0;
int port_optind = 1;
int port_opterr = 1;
int port_optopt = 0;

static int optpos = 1;

int
port_getopt(int argc, char *const argv[], const char *optstring)
{
	char *arg;
	char *optchar;
	int ch;

	port_optarg = (void *)0;
	if (port_optind >= argc || argv[port_optind] == (void *)0 ||
	    argv[port_optind][0] != '-' || argv[port_optind][1] == '\0') {
		return (-1);
	}
	arg = argv[port_optind];
	ch = arg[optpos];
	optchar = port_strchr(optstring, ch);
	if (ch == '\0' || optchar == (void *)0) {
		port_optopt = ch;
		optpos = 1;
		port_optind++;
		return ('?');
	}
	if (optchar[1] == ':') {
		if (arg[optpos + 1] != '\0') {
			port_optarg = &arg[optpos + 1];
		} else if (port_optind + 1 < argc) {
			port_optind++;
			port_optarg = argv[port_optind];
		} else {
			port_optopt = ch;
			optpos = 1;
			port_optind++;
			return (optstring[0] == ':' ? ':' : '?');
		}
		optpos = 1;
		port_optind++;
	} else {
		optpos++;
		if (arg[optpos] == '\0') {
			optpos = 1;
			port_optind++;
		}
	}
	return (ch);
}

/* ================================================================== */
/* Debugger line input (freestanding — uses port_term_* hooks only)   */
/* ================================================================== */

char *
port_term_read_line_dbg(char *buf, port_size_t size, port_sig_flag *quit_flag)
{
	port_size_t n = 0;
	int c;
	char ch;

	if (buf == (void *)0 || size < 2) {
		return ((void *)0);
	}
	buf[0] = '\0';

	for (;;) {
		if (quit_flag != (void *)0 && *quit_flag != 0) {
			return ((void *)0);
		}

		c = port_term_read_char();
		if (c == PORT_TERM_EOF) {
			if (n > 0) {
				buf[n] = '\0';
				return (buf);
			}
			return ((void *)0);
		}
		if (c == PORT_TERM_NODATA || c < 0) {
			port_sleep_us(5000);
			continue;
		}
		if (c == 0x03) {
			if (quit_flag != (void *)0) {
				*quit_flag = 1;
			}
			return ((void *)0);
		}
		if (c == '\r' || c == '\n') {
			buf[n] = '\0';
			port_term_write_buf("\r\n", 2);
			return (buf);
		}
		if (c == 0x7F || c == 0x08) {
			if (n > 0) {
				n--;
				port_term_write_buf("\b \b", 3);
			}
			continue;
		}
		if (c >= 0x20 && n + 1 < size) {
			ch = (char)c;
			buf[n++] = ch;
			buf[n] = '\0';
			port_term_write_buf(&ch, 1);
		}
	}
}
