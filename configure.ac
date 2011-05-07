#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([dbus-test-service], [0.0.1], [chienhua@gmail.com])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
have_event=no
AC_SEARCH_LIBS([event_init],[event],[have_event=yes])

have_dbus=no
AC_SEARCH_LIBS([dbus_threads_init_default],[dbus-1],[have_dbus=yes])

# Checks for header files.
if test "x${have_event}" = xyes; then
	AC_CHECK_HEADERS([event.h],[],[have_event=no])
else 
	AC_MSG_ERROR([No event support])
fi

if test "x${have_dbus}" = xyes; then
	AC_CHECK_HEADERS([dbus/dbus.h],[],[have_dbus=no])
else 
	AC_MSG_ERROR([No dbus support])
fi

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
