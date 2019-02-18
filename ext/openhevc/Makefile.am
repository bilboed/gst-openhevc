plugin_LTLIBRARIES = libgstopenhevc.la

libgstopenhevc_la_SOURCES = \
   gstopenhevc.c \
	 gstopenhevcviddec.c

libgstopenhevc_la_CFLAGS = $(GST_CFLAGS) $(OPENHEVC_CFLAGS) -I$(top_srcdir)
libgstopenhevc_la_LIBADD = $(GST_LIBS) $(OPENHEVC_LIBS)
libgstopenhevc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopenhevc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstopenhevc.h gstopenhevcviddec.h
