AC_PREREQ([2.69])
AC_INIT([apple1-emu], [1.0.0], [bug-report@example.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99

# Checks for pkg-config.
PKG_PROG_PKG_CONFIG

# Checks for libraries.
PKG_CHECK_MODULES([SDL3], [sdl3])

# Generate Makefile
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
