#ifndef PORT_ATTRS_H
#define PORT_ATTRS_H

/*
 * Inline, attribute, and port_size_t helpers for toolchains with a full
 * preprocessor (not used on Plan 9 / 9front native 6c builds).
 */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define PORT_STATIC_INLINE static inline
#else
#define PORT_STATIC_INLINE static
#endif

#if defined(__MSDOS__) || defined(MSDOS) || defined(__dos__)
typedef unsigned int port_size_t;
#elif defined(__x86_64__) || defined(__aarch64__) || defined(__mips64__) || \
    defined(__riscv) || defined(__powerpc64__) || defined(__s390x__) || \
    defined(__ia64__) || defined(_M_X64) || defined(_M_AMD64) || \
    defined(__LP64__) || defined(_LP64)
typedef unsigned long port_size_t;
#else
typedef unsigned long port_size_t;
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PORT_UNUSED   __attribute__((unused))
#define PORT_NORETURN __attribute__((noreturn))
#else
#define PORT_UNUSED
#define PORT_NORETURN
#endif

#endif /* PORT_ATTRS_H */
