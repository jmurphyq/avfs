all:
	$(MAKE) -C lib all
	$(MAKE) -C avfscoda all
	$(MAKE) -C preload all

install:
	$(MAKE) -C lib install
	$(MAKE) -C avfscoda install
	$(MAKE) -C preload install
	$(MAKE) -C extfs install
	$(MAKE) -C scripts install

start:
	$(MAKE) -C scripts start

clean:
	rm -f lib/mod_static.c lib/info.h
	rm -f avfscoda/avfscoda
	rm -f preload/avfs_server
	rm -f `find . \( -name "*.o" -o -name "*.so" -o -name "*.a" \
        -o -name ".*~" -o -name "*~" -o -name "*.s" -o -name "*.orig" \
        -o -name t -o  -name core -o -name gmon.out \) -print`

depend:
	$(MAKE) -C lib depend
	$(MAKE) -C avfscoda depend

TAGS:
	etags `find . -name "*.[ch]"`

realclean: clean
	rm -f config.status config.cache config.log
	rm -f include/config.h
	rm -f */Makefile */Makefile.old
	rm -f */*/Makefile */*/Makefile.old
	rm -f TAGS

distclean: realclean
