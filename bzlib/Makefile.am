cppflags = -I$(top_srcdir)/include @CPPFLAGS@ -DBZ_NO_STDIO

noinst_LTLIBRARIES = libbz.la

libbz_la_LDFLAGS = -module
libbz_la_CFLAGS = @ZLIB_CFLAGS@ $(cppflags)
libbz_la_SOURCES = blocksort.c  \
	huffman.c    \
	crctable.c   \
	randtable.c  \
	compress.c   \
	decompress.c \
	bzlib.c

noinst_HEADERS = \
	bzlib.h \
	bzlib_private.h \
	bzconf.h
