AC_PREREQ([2.69])
AC_INIT([apple1-emu], [1.0.0], [bug-report@example.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# Checks for programs.
AC_PROG_CC

# Platform target selection
AC_ARG_WITH([target],
    [AS_HELP_STRING([--with-target=TARGET],
        [Target platform: posix, dos, windows, freertos, bare, elks, os2, plan9, vxworks, zephyr @<:@default=posix@:>@])],
    [target_platform=$withval],
    [target_platform=posix])

AM_CONDITIONAL([TARGET_POSIX], [test "$target_platform" = "posix"])
AM_CONDITIONAL([TARGET_DOS], [test "$target_platform" = "dos"])
AM_CONDITIONAL([TARGET_WINDOWS], [test "$target_platform" = "windows"])
AM_CONDITIONAL([TARGET_FREERTOS], [test "$target_platform" = "freertos"])
AM_CONDITIONAL([TARGET_BARE], [test "$target_platform" = "bare"])
AM_CONDITIONAL([TARGET_ELKS], [test "$target_platform" = "elks"])
AM_CONDITIONAL([TARGET_OS2], [test "$target_platform" = "os2"])
AM_CONDITIONAL([TARGET_PLAN9], [test "$target_platform" = "plan9"])
AM_CONDITIONAL([TARGET_VXWORKS], [test "$target_platform" = "vxworks"])
AM_CONDITIONAL([TARGET_ZEPHYR], [test "$target_platform" = "zephyr"])

# Generate Makefile
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
