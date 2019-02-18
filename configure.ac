dnl required version of autoconf
AC_PREREQ([2.53])

m4_define([gstopenhevc_major_version], [0])
m4_define([gstopenhevc_minor_version], [0])
m4_define([gstopenhevc_micro_version], [1])
m4_define([gstopenhevc_nano_version], [1])

m4_define([gstopenhevc_version], [gstopenhevc_major_version.gstopenhevc_minor_version.gstopenhevc_micro_version.gstopenhevc_nano_version])

AC_INIT([gstopenhevc],[gstopenhevc_version])

GSTOPENHEVC_MAJOR_VERSION=gstopenhevc_major_version
GSTOPENHEVC_MINOR_VERSION=gstopenhevc_minor_version
GSTOPENHEVC_MICRO_VERSION=gstopenhevc_micro_version
GSTOPENHEVC_NANO_VERSION=gstopenhevc_nano_version

AC_SUBST(GSTOPENHEVC_MAJOR_VERSION)
AC_SUBST(GSTOPENHEVC_MINOR_VERSION)
AC_SUBST(GSTOPENHEVC_MICRO_VERSION)
AC_SUBST(GSTOPENHEVC_NANO_VERSION)

GST_REQUIRED=1.14
OPENHEVC_REQUIRED=1.00.000

AC_CONFIG_SRCDIR([ext/openhevc/gstopenhevc.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar subdir-objects]) 

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl Pretty Printing
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl check for tools (compiler etc.)
AC_PROG_CC

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-base-1.0 >= $GST_REQUIRED
  gstreamer-video-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
      on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

PKG_CHECK_MODULES(OPENHEVC, [
  libopenhevc >= $OPENHEVC_REQUIRED
], [
  AC_SUBST(OPENHEVC_CFLAGS)
  AC_SUBST(OPENHEVC_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the openHEVC library on your system"
  ])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for ext/openhevc/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-1.0/plugins"
else
  plugindir="`pkg-config gstreamer-1.0 --variable pluginsdir`"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

if test "$GSTOPENHEVC_NANO_VERSION" = "0"; then
  GST_PACKAGE_NAME="GStreamer OpenHEVC Plug-ins source release"
else if test "$GSTOPENHEVC_NANO_VERSION" = "1"; then
  GST_PACKAGE_NAME="GStreamer OpenHEVC Plug-ins git"
else
  GST_PACKAGE_NAME="GStreamer OpenHEVC Plug-ins prerelease"
fi
fi

AC_SUBST(GST_PACKAGE_NAME)

GST_PACKAGE_ORIGIN="Unknown package origin"
AC_SUBST(GST_PACKAGE_ORIGIN)

AC_DEFINE_UNQUOTED(GST_PACKAGE_NAME, "$GST_PACKAGE_NAME", [Package name])
AC_DEFINE_UNQUOTED(GST_PACKAGE_ORIGIN, "$GST_PACKAGE_ORIGIN", [Package origin])

AC_CONFIG_FILES([Makefile ext/Makefile ext/openhevc/Makefile])
AC_OUTPUT

