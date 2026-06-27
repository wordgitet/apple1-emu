#ifndef PORT_H
#define PORT_H

/*
 * C89 / C99 Type Portability Shim
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  include <stdbool.h>
#  include <stdint.h>
#else
#  ifndef uint8_t
     typedef unsigned char uint8_t;
#  endif
#  ifndef uint16_t
     typedef unsigned short uint16_t;
#  endif
#  ifndef uint32_t
     typedef unsigned int uint32_t;
#  endif
#  ifndef uint64_t
     typedef unsigned long long uint64_t;
#  endif
#  ifndef int8_t
     typedef signed char int8_t;
#  endif
#  ifndef int16_t
     typedef signed short int16_t;
#  endif
#  ifndef int32_t
     typedef signed int int32_t;
#  endif
#  ifndef int64_t
     typedef signed long long int64_t;
#  endif
#  ifndef bool
#    define bool int
#    define true 1
#    define false 0
#  endif
#endif

/*
 * Memory Allocator Portability Shim
 */
#if defined(APPLE1_ZERO_MALLOC)
   /* Stub allocator: always returns NULL, never calls system heap.
    * Allows linking on bare-metal systems with no allocator. */
#  define port_malloc(sz)       (NULL)
#  define port_free(ptr)        ((void)0)
#  define port_realloc(ptr, sz) (NULL)
#elif defined(APPLE1_CUSTOM_MALLOC)
   /* Pluggable allocator: the user defines these three functions
    * elsewhere in their system (e.g., custom pool allocator). */
   extern void *port_malloc(size_t sz);
   extern void port_free(void *ptr);
   extern void *port_realloc(void *ptr, size_t sz);
#else
   /* Default allocator: standard C library malloc/free/realloc. */
#  include <stdlib.h>
#  define port_malloc(sz)       malloc(sz)
#  define port_free(ptr)        free(ptr)
#  define port_realloc(ptr, sz) realloc(ptr, sz)
#endif

/*
 * Portable Xorshift pseudo-random number generator (PRNG) shims
 */
uint32_t port_rand(void);
void port_srand(uint32_t seed);

/*
 * Getopt Shim Declaration
 */
extern char *port_optarg;
extern int port_optind;
extern int port_opterr;
extern int port_optopt;

int port_getopt(int argc, char *const argv[], const char *optstring);

/*
 * High-resolution timer and sleep shims
 */
uint64_t port_gettime_ns(void);
void port_sleep_ns(uint64_t ns);

/*
 * Implementations
 */
#ifdef PORT_IMPLEMENT_SHIMS

#include <stdio.h>
#include <string.h>

char *port_optarg = NULL;
int port_optind = 1;
int port_opterr = 1;
int port_optopt = 0;

static int optpos = 1;
static uint32_t g_rand_state = 0xACE1; /* Default seed */

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

int
port_getopt(int argc, char *const argv[], const char *optstring)
{
	char *arg;
	char *optchar;
	int ch;

	port_optarg = NULL;
	if (port_optind >= argc || argv[port_optind] == NULL ||
	    argv[port_optind][0] != '-' || argv[port_optind][1] == '\0') {
		return (-1);
	}

	arg = argv[port_optind];
	ch = arg[optpos];
	optchar = strchr(optstring, ch);

	if (ch == '\0' || optchar == NULL) {
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

/*
 * Platform-specific timing implementation
 */
#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
uint64_t
port_gettime_ns(void)
{
	LARGE_INTEGER freq;
	LARGE_INTEGER counter;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	return ((uint64_t)(counter.QuadPart * 1000000000LL / freq.QuadPart));
}

void
port_sleep_ns(uint64_t ns)
{
	Sleep((DWORD)(ns / 1000000ULL));
}

#elif defined(__HAIKU__)
#  include <kernel/OS.h>
uint64_t
port_gettime_ns(void)
{
	return ((uint64_t)system_time() * 1000ULL);
}

void
port_sleep_ns(uint64_t ns)
{
	snooze((bigtime_t)(ns / 1000ULL));
}

#elif defined(__QNXNTO__)
#  include <sys/neutrino.h>
#  include <sys/syspage.h>
uint64_t
port_gettime_ns(void)
{
	uint64_t cycles;

	cycles = ClockCycles();
	return ((uint64_t)(cycles * 1000000000ULL / SYSPAGE_ENTRY(qtime)->cycles_per_sec));
}

void
port_sleep_ns(uint64_t ns)
{
	struct timespec req;

	req.tv_sec = (time_t)(ns / 1000000000ULL);
	req.tv_nsec = (long)(ns % 1000000000ULL);
	nanosleep(&req, NULL);
}

#elif defined(__RTP__) || defined(_WRS_KERNEL)
/* VxWorks */
#  include <tickLib.h>
#  include <sysLib.h>
#  include <taskLib.h>
uint64_t
port_gettime_ns(void)
{
	return ((uint64_t)tickGet() * 1000000000ULL / sysClkRateGet());
}

void
port_sleep_ns(uint64_t ns)
{
	taskDelay((int)(ns * sysClkRateGet() / 1000000000ULL) + 1);
}
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__sun) || defined(__rtems__)
/* Standard POSIX default */
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 199309L
#  endif
#  include <time.h>
uint64_t
port_gettime_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		return ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec);
	}
	return (0);
}

void
port_sleep_ns(uint64_t ns)
{
	struct timespec req;

	req.tv_sec = (time_t)(ns / 1000000000ULL);
	req.tv_nsec = (long)(ns % 1000000000ULL);
	nanosleep(&req, NULL);
}
#else
/* Bare-metal / custom RTOS mock fallback */
uint64_t
port_gettime_ns(void)
{
	static uint64_t mock_ticks = 0;
	/* Pretend 1 millisecond passes each check */
	return (mock_ticks += 1000000ULL);
}

void
port_sleep_ns(uint64_t ns)
{
	/* Busy loop fallback */
	volatile uint64_t count;
	for (count = 0; count < ns / 10ULL; count++) {
		/* no-op */
	}
}
#endif

#endif /* PORT_IMPLEMENT_SHIMS */

#endif /* PORT_H */
