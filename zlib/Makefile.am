AM_CFLAGS = -I$(top_srcdir)/include @ZLIB_CFLAGS@ @CPPFLAGS@

noinst_LTLIBRARIES = libzlib.la

libzlib_la_LDFLAGS = -module
libzlib_la_SOURCES = adler32.c   \
	crc32.c	    \
	deflate.c   \
	infblock.c  \
	infcodes.c  \
	inffast.c   \
	inflate.c   \
	inftrees.c  \
	infutil.c   \
	trees.c	    \
	zutil.c

noinst_HEADERS = \
	deflate.h \
	infblock.h \
	infcodes.h \
	inffast.h \
	inffixed.h \
	inftrees.h \
	infutil.h \
	trees.h \
	zconf.h \
	zlib.h \
	zutil.h
