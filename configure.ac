AC_PREREQ([2.69])
AC_INIT([apple1-emu], [1.0.0], [bug-report@example.com])

# Portable compilers (tcc, lacc, …) reject gcc -MT/-MD dependency flags.
# Use plain $(CC) -c rules unless --enable-dependency-tracking is passed.
enable_dependency_tracking=no

AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])

AC_PROG_CC

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
