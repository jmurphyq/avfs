#! /bin/sh

#gcc -I../include -Wall avfsd.c -o avfsd ../lib/avfs.o -L../libneon -lneon -lxmltok -lxmlparse -ldl -lpthread -lfuse

gcc -I../include -Wall -W -g -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS avfsd.c -o avfsd ../lib/avfs.o -ldl -lfuse

