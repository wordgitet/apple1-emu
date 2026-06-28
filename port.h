#ifndef PORT_H
#define PORT_H

/*
 * port.h -- Apple-1 emulator portability interface
 *
 * This header contains ONLY type definitions, struct declarations,
 * and function prototypes.  It includes exactly one standard header:
 * <stdarg.h>, which is a compiler intrinsic available in all C89
 * environments including freestanding implementations.
 *
 * All implementations live in a single platform-specific port_*.c
 * file selected at compile time (port_posix.c, port_win.c, etc.).
 * Core source files must include only this header, never any system
 * headers directly.
 */
#include <stdarg.h>

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

/*
 * bool: C89 has no native boolean type.
 */
#ifndef bool
#define bool  int
#define true  1
#define false 0
#endif

/*
 * inline is C99; use plain static helpers on strict C89 compilers.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define PORT_STATIC_INLINE static inline
#else
#define PORT_STATIC_INLINE static
#endif

/*
 * port_size_t -- a type large enough to hold the size of any object
 * on the host platform.  We cannot rely on <stddef.h> in freestanding
 * builds, so we derive the type from ABI characteristics:
 *
 *  LP64  (64-bit Linux/macOS/Android, *BSD, AIX):
 *      sizeof(void*) = 8, sizeof(long) = 8 → unsigned long (8 bytes)
 *  LLP64 (64-bit Windows):
 *      sizeof(void*) = 8, sizeof(long) = 4.
 *      C89 has no 'unsigned long long', so we use unsigned long.
 *      Objects in this emulator never exceed 4 GB, so this is safe.
 *  ILP32 (32-bit Unix / embedded / DOS / Haiku):
 *      sizeof(void*) = 4, sizeof(long) = 4 → unsigned long (4 bytes)
 *  16-bit (ELKS / MS-DOS near model):
 *      sizeof(void*) = 2, sizeof(int) = 2 → unsigned int (2 bytes)
 */
#if defined(__x86_64__) || defined(__aarch64__) || defined(__mips64) || \
    defined(__riscv) || defined(__powerpc64__) || defined(__s390x__) || \
    defined(__ia64__) || defined(_M_X64) || defined(_M_AMD64) ||        \
    defined(__LP64__) || defined(_LP64)
typedef unsigned long port_size_t;
#elif defined(__ELKS__) || \
    (defined(__MSDOS__) || defined(MSDOS) || defined(__dos__))
typedef unsigned int port_size_t;
#else
typedef unsigned long port_size_t;
#endif

/* ================================================================== */
/* Compiler portability helpers                                        */
/* ================================================================== */

#if defined(__GNUC__) || defined(__clang__)
#define PORT_UNUSED   __attribute__((unused))
#define PORT_NORETURN __attribute__((noreturn))
#else
#define PORT_UNUSED
#define PORT_NORETURN
#endif

/* ================================================================== */
/* Memory allocation shims                                            */
/* ================================================================== */

#if defined(APPLE1_ZERO_MALLOC)
#define port_malloc(sz)	      ((void *)0)
#define port_free(ptr)	      ((void)(ptr))
#define port_realloc(ptr, sz) ((void *)0)
#elif defined(APPLE1_CUSTOM_MALLOC)
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
     * size: return total file size in bytes without altering the
     * current position.  Returns -1 on error.
     */
	long (*size)(port_file_t f);

	/*
     * seek: reposition the file pointer.  whence is one of
     * PORT_VFS_SEEK_SET / _CUR / _END.  Returns 0 on success, -1
     * on failure.
     */
	int (*seek)(port_file_t f, long offset, int whence);

	/*
     * write: write sz bytes from buf.  Returns number of bytes
     * written, or -1 on error.
     */
	long (*write)(port_file_t f, const void *buf, port_size_t sz);

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
