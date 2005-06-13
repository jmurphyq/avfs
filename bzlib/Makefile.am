cppflags = -I$(top_srcdir)/include @CPPFLAGS@ -DBZ_NO_STDIO

noinst_LTLIBRARIES = libbz_read.la libbz.la

libbz_la_LDFLAGS = -module
libbz_la_CFLAGS = @ZLIB_CFLAGS@ $(cppflags)
libbz_la_LIBADD = libbz_read.la
libbz_la_DEPENDENCIES = libbz_read.la
libbz_la_SOURCES = blocksort.c  \
	huffman.c    \
	crctable.c   \
	randtable.c  \
	compress.c   \
	decompress.c \
	bzlib.c

libbz_read_la_LDFLAGS = -module
libbz_read_la_CFLAGS = @CFLAGS@ $(cppflags)
libbz_read_la_SOURCES = bzread.c

noinst_HEADERS = \
	bzlib.h \
	bzlib_private.h \
	bzconf.h
