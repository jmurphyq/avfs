CC = gcc
CFLAGS = -Wall -g -fPIC
#CFLAGS = -Wall -g -pg
LDLIBS = -lsocket -lnsl -ldl -lposix4 -lpthread 
#LDLIBS = -pg -lsocket -lnsl -ldl -lposix4 
CPPFLAGS = -I../include -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS


all: avfs_server avfs_preload.so

#leak = leak.o

avfs_server: avfs_server.o server.o send.o ../lib/libavfs.a $(leak)

avfs_preload_objs = utils.o stat.o open.o file.o misc.o client.o send.o
avfs_preload.so: $(avfs_preload_objs)
	gcc -shared -o avfs_preload.so.tmp $(avfs_preload_objs) -lsocket
	mv avfs_preload.so.tmp avfs_preload.so

client: client.o send.o

avfs_server.o: send.h
send.o: send.h

clean:
	rm -f *.o

realclean: clean
	rm -f *.so
	rm -f avfs_server
