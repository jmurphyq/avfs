all:
	$(MAKE) -C src all
	$(MAKE) -C modules all
	$(MAKE) -C lib all
	$(MAKE) -C libneon all
	$(MAKE) -C avfscoda all
	$(MAKE) -C preload all

install:
	$(MAKE) -C avfscoda install
	$(MAKE) -C preload install
	$(MAKE) -C extfs install
	$(MAKE) -C scripts install

start:
	$(MAKE) -C scripts start

clean:
	rm -f modules/mod_static.c src/info.h
	rm -f avfscoda/avfscoda
	rm -f preload/avfs_server
	rm -f test/runtest
	rm -f `find . \( -name "*.o" -o -name "*.so" -o -name "*.a" \
        -o -name ".*~" -o -name "*~" -o -name "*.s" -o -name "*.orig" \
        -o -name t -o  -name core -o -name gmon.out \) -print`

depend:
	$(MAKE) -C src depend
	$(MAKE) -C modules depend
	$(MAKE) -C avfscoda depend
	$(MAKE) -C preload depend

TAGS:
	etags `find . -name "*.[ch]"`

distclean: clean
	rm -f config.status config.cache config.log
	rm -f include/config.h
	rm -f */Makefile */Makefile.old
	rm -f */*/Makefile */*/Makefile.old
	rm -f TAGS

