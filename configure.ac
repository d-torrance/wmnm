AC_INIT([wmnm], [0.1], [dtorrance@piedmont.edu])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([wmnm.c])
AC_PROG_CC
PKG_CHECK_MODULES([x11], [x11])
PKG_CHECK_MODULES([dockapp], [dockapp])
PKG_CHECK_MODULES([glib], [glib-2.0 gobject-2.0 gio-2.0])
PKG_CHECK_MODULES([libnm], [libnm >= 1.24])
PKG_CHECK_MODULES([xft], [xft])

dnl wmnm-askpass uses WINGs, Window Maker's own widget set.  Every Window
dnl Maker user already has the runtime library, but the headers are a
dnl separate package, so treat the helper as optional: without it wmnm falls
dnl back to $SSH_ASKPASS at runtime.
AC_ARG_WITH([wings],
	[AS_HELP_STRING([--without-wings],
		[do not build the bundled wmnm-askpass helper])],
	[], [with_wings=auto])

have_wings=no
AS_IF([test "x$with_wings" != xno],
	[PKG_CHECK_MODULES([WINGS], [WINGs], [have_wings=yes],
		[AS_IF([test "x$with_wings" = xyes],
			[AC_MSG_ERROR([WINGs not found; install libwings-dev])],
			[AC_MSG_WARN([WINGs not found: wmnm-askpass will not be built])])])])
AM_CONDITIONAL([BUILD_ASKPASS], [test "x$have_wings" = xyes])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_NOTICE([bundled wmnm-askpass (WINGs): $have_wings])
