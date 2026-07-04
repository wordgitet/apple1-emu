#ifndef PORT_H
#define PORT_H

/*
 * port.h -- Apple-1 emulator portability interface
 *
 * This header contains ONLY type definitions, struct declarations,
 * and function prototypes.  Variadic support comes from port_stdarg.h
 * (system <stdarg.h> on hosted toolchains; a TinyCC-safe shim otherwise).
 *
 * All implementations live in a single platform-specific port_*.c
 * file selected at compile time (port_posix.c, port_win.c, etc.).
 * Core source files must include only this header, never any system
 * headers directly.
 */
#ifdef APPLE1_PORT_PLAN9
#ifndef APPLE1_PORT_PLAN9_APE
#include <libc.h>
#include <u.h>
#endif
#endif

#include "port_stdarg.h"

#ifdef __USLC__
#include <sys/types.h>
#endif

/* ================================================================== */
/* Integer type definitions -- zero dependency on <stdint.h>          */
/* ================================================================== */

/*
 * Exact-width types for C89.  Derivation:
 *
 *   char  -- exactly 8 bits on every platform we support.
 *   short -- exactly 16 bits universally.
 *   int   -- 32 bits on ILP32 / LP64 / LLP64.  The ILP64 model
 *             (some old Cray/SPARC variants) is not supported.
 *   long  -- 32 bits on ILP32 / LLP64; 64 bits on LP64.
 */

#ifndef __USLC__
#ifndef uint8_t
typedef unsigned char uint8_t;
#endif
#ifndef int8_t
typedef signed char int8_t;
#endif
#ifndef uint16_t
typedef unsigned short uint16_t;
#endif
#ifndef int16_t
typedef signed short int16_t;
#endif
#ifndef uint32_t
typedef unsigned int uint32_t;
#endif
#ifndef int32_t
typedef signed int int32_t;
#endif
#endif /* !__USLC__ */

#ifndef UINT8_MAX
#define UINT8_MAX 255U
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535U
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295UL
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef LONG_MAX
#define LONG_MAX 2147483647L
#endif
#ifndef LONG_MIN
#define LONG_MIN (-2147483647L - 1L)
#endif
#ifndef ULONG_MAX
#define ULONG_MAX 4294967295UL
#endif

/* ================================================================== */
/* Error code system                                                     */
/* ================================================================== */

/*
 * port_result_t - Standardized error codes following SQLite convention.
 * PORT_OK indicates success. All other values indicate failure.
 */
typedef int port_result_t;

#define PORT_OK	      0	 /* Successful operation */
#define PORT_ERROR    1	 /* Generic error */
#define PORT_NOMEM    2	 /* Memory allocation failed */
#define PORT_IO	      3	 /* I/O error */
#define PORT_CORRUPT  4	 /* Data corruption */
#define PORT_FULL     5	 /* Buffer/full condition */
#define PORT_CANTOPEN 6	 /* Unable to open file */
#define PORT_PROTOCOL 7	 /* Protocol/format error */
#define PORT_INVALID  8	 /* Invalid parameter */
#define PORT_MISUSE   9	 /* Library used incorrectly */
#define PORT_RANGE    10 /* Out of range */
#define PORT_NOTFOUND 11 /* Not found */

/*
 * port_error_string - Return human-readable error message for error code.
 * Never returns NULL; returns "unknown error" for unrecognized codes.
 */
const char *
port_error_string(port_result_t rc);

/*
 * bool: C89 has no native boolean type.
 */
#ifndef bool
#define bool  int
#define true  1
#define false 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef APPLE1_PORT_PLAN9
#define PORT_STATIC_INLINE static
typedef unsigned long port_size_t;
#define PORT_UNUSED
#define PORT_NORETURN
#else
#include "port_attrs.h"
#endif

/* ================================================================== */
/* Memory allocation shims                                            */
/* ================================================================== */

#ifdef APPLE1_ZERO_MALLOC
#define port_malloc(sz)	      ((void *)0)
#define port_free(ptr)	      ((void)(ptr))
#define port_realloc(ptr, sz) ((void *)0)
#else

struct port_mem_methods {
	void *(*xMalloc)(port_size_t sz);
	void (*xFree)(void *ptr);
	void *(*xRealloc)(void *ptr, port_size_t sz);
	port_result_t (*xInit)(void *app_data);
	void (*xShutdown)(void *app_data);
	void *pAppData;
};

#ifdef APPLE1_CUSTOM_MALLOC
extern void *
port_malloc(port_size_t sz);
extern void
port_free(void *ptr);
extern void *
port_realloc(void *ptr, port_size_t sz);
#else
void *
port_malloc(port_size_t sz);
void
port_free(void *ptr);
void *
port_realloc(void *ptr, port_size_t sz);

port_result_t
port_mem_config(const struct port_mem_methods *methods);
port_result_t
port_mem_get_config(struct port_mem_methods *methods);
#endif
#endif

char *
port_strdup(const char *str);

/* ================================================================== */
/* Memory utility shims                                               */
/* ================================================================== */

void *
port_memcpy(void *dst, const void *src, port_size_t n);
void *
port_memset(void *dst, int c, port_size_t n);
void *
port_memmove(void *dst, const void *src, port_size_t n);
int
port_memcmp(const void *a, const void *b, port_size_t n);

/* ================================================================== */
/* String utility shims                                               */
/* ================================================================== */

port_size_t
port_strlen(const char *s);
char *
port_strncpy(char *dst, const char *src, port_size_t n);
int
port_strcmp(const char *a, const char *b);
int
port_strncmp(const char *a, const char *b, port_size_t n);
char *
port_strchr(const char *s, int c);
char *
port_strstr(const char *hay, const char *needle);
long
port_strtol(const char *s, char **endptr, int base);
unsigned long
port_strtoul(const char *s, char **endptr, int base);

/* ================================================================== */
/* Character classification shims                                     */
/* ================================================================== */

int
port_isspace(int c);
int
port_isdigit(int c);
int
port_isxdigit(int c);
int
port_isalpha(int c);
int
port_isalnum(int c);
int
port_toupper(int c);
int
port_tolower(int c);

/* ================================================================== */
/* Formatted output shims                                             */
/* ================================================================== */

/*
 * port_snprintf / port_vsnprintf:
 *   Always NUL-terminate buf (even when truncating).
 *   Return value is the number of characters written, not counting the
 *   NUL terminator; negative on error.
 *
 * Supported specifiers: %s %d %i %u %x %X %c %ld %lu %lx %lX %%
 * Flags:                width, '0' (zero-pad), '-' (left-align)
 * No floating point.
 */
int
port_snprintf(char *buf, port_size_t n, const char *fmt, ...);
int
port_vsnprintf(char *buf, port_size_t n, const char *fmt, va_list ap);

/* ================================================================== */
/* Timing shims                                                       */
/* ================================================================== */

uint32_t
port_gettime_us(void);
void
port_sleep_us(uint32_t us);

/* ================================================================== */
/* Terminal I/O shims                                                 */
/* ================================================================== */

/*
 * port_term_raw_enable:  put the controlling terminal into raw/cbreak
 *   mode (no echo, no line buffering, non-blocking read).
 * port_term_raw_disable: restore the terminal to its original mode.
 * port_term_read_char:   read one byte; PORT_TERM_NODATA or PORT_TERM_EOF.
 * port_term_write_buf:   write n bytes to the terminal output.
 */
void
port_term_raw_enable(void);
void
port_term_raw_disable(void);
void
port_term_dbg_enable(void);
void
port_term_dbg_disable(void);
int
port_term_read_char(void);
void
port_term_write_buf(const char *buf, port_size_t n);

#define PORT_TERM_NODATA (-1)
#define PORT_TERM_EOF	 (-2)

/*
 * port_sig_flag: set to 1 by the platform on Ctrl-C / SIGINT.
 * Declared here so debugger input can reference it without system headers.
 */
typedef volatile int port_sig_flag;

/*
 * port_term_read_line: read a line of input into buf (up to size-1
 * characters), appending a NUL terminator.  Returns buf on success,
 * NULL on end-of-input / error.  Blocks until a newline or EOF.
 */
char *
port_term_read_line(char *buf, port_size_t size);

/*
 * port_term_read_line_dbg: read one debugger command line using only
 * port_term_* hooks.  Ctrl-C (0x03) or *quit_flag sets quit and returns
 * NULL.  Call with raw mode enabled where the platform requires it.
 */
char *
port_term_read_line_dbg(char *buf, port_size_t size, port_sig_flag *quit_flag);

/* ================================================================== */
/* Signal handling shim                                               */
/* ================================================================== */

/*
 * port_signal_setup: install a handler so that SIGINT increments
 * *flag.  Called once at startup before the main emulation loop.
 */
void
port_signal_setup(port_sig_flag *flag);

/*
 * port_signal_quit: platform-independent Ctrl-C / quit request.
 * Sets the flag registered by port_signal_setup (if any).
 * term_poll calls this when the user presses Ctrl-C.
 */
void
port_signal_quit(void);

/* ================================================================== */
/* Process exit shim                                                  */
/* ================================================================== */

PORT_NORETURN void
port_exit(int code);

/* ================================================================== */
/* Command-line argument parsing shims                                */
/* ================================================================== */

extern char *port_optarg;
extern int port_optind;
extern int port_opterr;
extern int port_optopt;

int
port_getopt(int argc, char *const argv[], const char *optstring);

/* ================================================================== */
/* Pseudo-random number generator shims                               */
/* ================================================================== */

uint32_t
port_rand(void);
void
port_srand(uint32_t seed);

/* ================================================================== */
/* Virtual File System (VFS)                                          */
/* ================================================================== */

/*
 * The VFS abstraction decouples all file operations from the host OS.
 * Core emulator code never calls fopen/fread/fclose/fseek/ftell
 * directly; it calls through the function pointers in port_vfs_default.
 *
 * To embed the emulator on a system with no file system (bare-metal,
 * ROM-only), replace port_vfs_default with a custom struct port_vfs
 * before calling any bus or ACI initialisation functions.
 *
 * Design mirrors SQLite's sqlite3_vfs: a vtable of function pointers
 * behind a single global pointer that host code can swap out.
 */

/*
 * VFS abstraction.  All file operations go through this vtable so the
 * emulator core never touches fopen/fread/fclose directly.
 *
 * Mirrors SQLite's sqlite3_vfs design: swap g_port_vfs before any
 * bus/ACI init to use a custom filesystem (ROM, EEPROM, RAM-disk …).
 */

#define PORT_VFS_READ	  0
#define PORT_VFS_WRITE	  1

#define PORT_VFS_SEEK_SET 0
#define PORT_VFS_SEEK_CUR 1
#define PORT_VFS_SEEK_END 2

/*
 * port_file_t: opaque file handle returned by port_vfs->open.
 * PORT_FILE_INVALID is the sentinel for a failed open.
 */
typedef void *port_file_t;
#define PORT_FILE_INVALID ((void *)0)

struct port_vfs {
	/*
     * open: open a file.  Returns an opaque handle on success,
     * PORT_FILE_INVALID on failure.
     * flags: PORT_VFS_READ or PORT_VFS_WRITE.
     */
	port_file_t (*open)(const char *path, int flags);

	/* close: release a file handle returned by open. */
	void (*close)(port_file_t f);

	/*
     * read: read up to sz bytes into buf.  Stores bytes actually
     * read in *nread.  Returns 0 on success (including short reads
     * at EOF), -1 on error.
     */
	int (*read)(port_file_t f,
	    void *buf,
	    port_size_t sz,
	    port_size_t *nread);

	/*
     * size: store total file size in bytes without altering the
     * current position.  Returns 0 on success, -1 on error.
     */
	int (*size)(port_file_t f, port_size_t *size);

	/*
     * seek: reposition the file pointer.  whence is one of
     * PORT_VFS_SEEK_SET / _CUR / _END.  offset is signed.
     * Returns 0 on success, -1 on failure.
     */
	int (*seek)(port_file_t f, int32_t offset, int whence);

	/*
     * write: write up to sz bytes from buf.  Stores bytes actually
     * written in *nwritten.  Returns 0 on success, -1 on error.
     */
	int (*write)(port_file_t f,
	    const void *buf,
	    port_size_t sz,
	    port_size_t *nwritten);

	/*
     * read_line: read one text line into buf (up to size-1 chars),
     * NUL-terminating the result.  Returns 1 if a line was read,
     * 0 at EOF / error.
     */
	int (*read_line)(port_file_t f, char *buf, port_size_t size);
};

/*
 * g_port_vfs: global VFS pointer.  Platform port files point this at
 * their implementation struct before any file-loading calls are made.
 * Host apps may replace it for custom storage backends.
 *
 * Use the port_vfs_default convenience alias in call sites so that
 * the code reads like:  port_vfs_default.open(path, PORT_VFS_READ)
 */
extern struct port_vfs *g_port_vfs;
#define port_vfs_default (*g_port_vfs)

#endif /* PORT_H */
