dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.60)
AC_INIT([play], [1.0])
AC_CONFIG_SRCDIR([src/play.c])
AC_CONFIG_HEADERS([src/config.h])

dnl initialize automake
AM_INIT_AUTOMAKE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],
         [AM_SILENT_RULES([yes])],
         [AM_DEFAULT_VERBOSITY=1 AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl find a compiler and required programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

dnl Header checks
AC_HEADER_STDC
AC_CHECK_HEADERS(sys/ioctl.h errno.h time.h termios.h signal.h, , [
                  AC_MSG_RESULT(no)
                  AC_MSG_ERROR([
A required header file is missing.
])])

dnl Check for glib
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.18 gio-2.0 gobject-2.0], , [
                  AC_MSG_RESULT(no)
                  AC_MSG_ERROR([
You must have the glib2 development headers installed.
])])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for libxml2
PKG_CHECK_MODULES(LIBXML2, [libxml-2.0], , [
                  AC_MSG_RESULT(no)
                  AC_MSG_ERROR([
You must have the libxml2 development headers installed.
])])
AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

dnl Check for gstreamer
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-1.0], , [
                  AC_MSG_RESULT(no)
                  AC_MSG_ERROR([
You must have the gstreamer-1.0 development headers installed.
])])
AC_SUBST(GSTREAMER_CFLAGS)
AC_SUBST(GSTREAMER_LIBS)

# Write the Makefiles
AC_CONFIG_FILES([
Makefile
src/Makefile
])
AC_OUTPUT
