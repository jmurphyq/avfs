EXTFS_CONST =		\
	bpp		\
        ftplist		\
        rpm		\
        trpm		\
        ucpio

EXTFS_IN =	\
	a		\
	apt		\
	audio		\
	deb		\
	deba		\
	debd		\
	dpkg		\
	hp48		\
	iso9660		\
	lslR		\
	mailfs		\
	patchfs		\
	rpms		\
	u7z		\
	uar		\
	uarj		\
	uha		\
	ulha		\
	urar		\
	uzip		\
	uzoo

extfsscriptsdir = @moduledir@/extfs
extfsscripts_SCRIPTS = $(EXTFS_IN) $(EXTFS_CONST)
	
extfsdatadir = @moduledir@/extfs
extfsdata_DATA = extfs.ini README

EXTRA_DIST = $(extfsdata_DATA) $(EXTFS_CONST)
