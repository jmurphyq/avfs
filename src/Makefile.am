EXTRA_DIST = make_info

AM_CFLAGS = -I$(top_srcdir)/include @ZLIB_INCLUDE@ @BZLIB_INCLUDE@ @CFLAGS@ @CPPFLAGS@ -DBZ_NO_STDIO

noinst_LTLIBRARIES = libavfscore.la
libavfscore_la_LDFLAGS = -module
libavfscore_la_SOURCES = utils.c      \
	sysdeps.c    \
	tmpfile.c    \
	alloc.c      \
	parsels.c    \
	ugid.c       \
	parse.c      \
	prog.c       \
	runprog.c    \
	cache.c      \
	filebuf.c    \
	local.c      \
	default.c    \
	oper.c       \
	fdops.c      \
	virtual.c    \
	modload.c    \
	remote.c     \
	archive.c    \
	archutil.c   \
	namespace.c  \
	state.c      \
	serialfile.c \
	filtprog.c   \
	filter.c     \
	filecache.c  \
	socket.c     \
	passwords.c  \
	zread.c      \
	exit.c       \
	realfile.c   \
	bzread.c

if USE_LIBLZMA
libavfscore_la_SOURCES += xzread.c
endif

noinst_HEADERS = \
	archint.h \
	filtprog.h \
	local.h \
	mod_static.h

$(srcdir)/utils.c: info.h

info.h:
	rm -f info.h
	$(srcdir)/make_info $(moduledir) > info.h

CLEANFILES = info.h
