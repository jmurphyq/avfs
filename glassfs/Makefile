obj-m := glassfs.o

KERNDIR := /usr/src/kernel-headers-2.6.3-1-686
#KERNDIR := /store/linux/uml/linux-2.6.4
#EXTRA := ARCH=um

all:
	$(MAKE) -C $(KERNDIR) SUBDIRS=$(PWD) $(EXTRA) modules

clean:
	rm -f *~
	rm -f *.o *.ko *.mod.c .*.cmd
