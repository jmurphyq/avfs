noinst_PROGRAMS = runtest

AM_CFLAGS = -I$(top_srcdir)/include @CFLAGS@ @CPPFLAGS@

runtest_LDFLAGS = @LDFLAGS@ @LIBS@
runtest_LDADD = ../lib/libavfs_static.la
runtest_SOURCES = runtest.c
