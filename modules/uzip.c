/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    ZIP module
*/
#if 0
#include "archives.h"
#include "zfilt.h"
#include "zipconst.h"

struct ecrec {
    avdbyte this_disk;
    avdbyte cdir_disk;
    avdbyte this_entries;
    avdbyte total_entries;
    avqbyte cdir_size;
    avqbyte cdir_off;
    avdbyte comment_len;
};

#define ECREC_THIS_DISK     4
#define ECREC_CDIR_DISK     6
#define ECREC_THIS_ENTRIES  8
#define ECREC_TOTAL_ENTRIES 10
#define ECREC_CDIR_SIZE     12
#define ECREC_CDIR_OFF      16
#define ECREC_COMMENT_LEN   20

#define ECREC_SIZE          22

struct cdirentry {
    avdbyte version;
    avdbyte need_version;
    avdbyte flag;
    avdbyte method;
    avqbyte mod_time;
    avqbyte crc;
    avqbyte comp_size;
    avqbyte file_size;
    avdbyte fname_len;
    avdbyte extra_len;
    avdbyte comment_len;
    avdbyte start_disk;
    avdbyte int_attr;
    avqbyte attr;
    avqbyte file_off;         
};

#define CDIRENT_VERSION       4
#define CDIRENT_NEED_VERSION  6
#define CDIRENT_FLAG          8
#define CDIRENT_METHOD        10
#define CDIRENT_MOD_TIME      12
#define CDIRENT_CRC           16
#define CDIRENT_COMP_SIZE     20
#define CDIRENT_FILE_SIZE     24
#define CDIRENT_FNAME_LEN     28
#define CDIRENT_EXTRA_LEN     30
#define CDIRENT_COMMENT_LEN   32
#define CDIRENT_START_DISK    34
#define CDIRENT_INT_ATTR      36
#define CDIRENT_ATTR          38
#define CDIRENT_FILE_OFF      42

#define CDIRENT_SIZE          46

struct ldirentry {
    avdbyte need_version;
    avdbyte flag;
    avdbyte method;
    avqbyte mod_time;
    avqbyte crc;
    avqbyte comp_size;
    avqbyte file_size;
    avdbyte fname_len;
    avdbyte extra_len;
};

#define LDIRENT_NEED_VERSION  4
#define LDIRENT_FLAG          6
#define LDIRENT_METHOD        8
#define LDIRENT_MOD_TIME      10
#define LDIRENT_CRC           14
#define LDIRENT_COMP_SIZE     18
#define LDIRENT_FILE_SIZE     22
#define LDIRENT_FNAME_LEN     26
#define LDIRENT_EXTRA_LEN     28

#define LDIRENT_SIZE          30

#define dos_ftsec(ft)   (int)( 2 * ((ft >>  0) & 0x1F))
#define dos_ftmin(ft)   (int)(     ((ft >>  5) & 0x3F))
#define dos_fthour(ft)  (int)(     ((ft >> 11) & 0x1F))
#define dos_ftday(ft)   (int)(     ((ft >> 16) & 0x1F))
#define dos_ftmonth(ft) (int)(-1 + ((ft >> 21) & 0x0F))
#define dos_ftyear(ft)  (int)(80 + ((ft >> 25) & 0x7F))

#define BUFSIZE 512

#define SEARCHLEN 66000

#define BI(ptr, i)  ((avbyte) (ptr)[i])
#define DBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8))
#define QBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8) | \
                   (BI(ptr,2)<<16) | (BI(ptr,3)<<24))

struct zipino_data {
    avqbyte crc;
    avqbyte comp_size;
};

static void conv_tolower(char *s)
{
    for(; *s; s++) *s = av_tolower(*s);
}

static int read_from(ave *v, arch_file *file, 
		     char *buf, avoff_t start, avsize_t nbytes)
{
    avssize_t rres;
    avoff_t sres;

    sres = av_lseek(v, file->fh, start, AVSEEK_SET);
    if(sres == -1) return -1;  /* v->errn from lseek */
    file->ptr = sres;

    rres = av_read(v, file->fh, buf, nbytes);
    if(rres == -1) return -1;  /* v->errn from read */
    file->ptr += rres;

    if((avsize_t) rres != nbytes) {
	v->errn = EIO;
	return -1;
    }
    return 0;
}

static avoff_t find_ecrec(ave *v, arch_file *file, 
			  long searchlen, struct ecrec *ecrec)
{
    avoff_t bufstart;
    int pos;
    char buf[BUFSIZE+3];
    avoff_t sres;
    int found;
  
    sres = av_lseek(v, file->fh, 0, AVSEEK_END);
    if(sres == -1) return -1;
    if(sres < ECREC_SIZE) {
	v->errn = EIO;
	return -1;
    }
  
    pos = 0;
    bufstart = sres - (ECREC_SIZE - 4);
    buf[0] = buf[1] = buf[2] = 0;
    found = 0;

    for(;searchlen && (bufstart || pos); searchlen--) {
	if(!pos) {
	    pos = BUFSIZE;
	    if(bufstart < pos) pos = bufstart;
	    bufstart -= pos;
	    buf[pos]   = buf[0];
	    buf[pos+1] = buf[1];
	    buf[pos+2] = buf[2];
	    if(read_from(v, file, buf, bufstart, pos) == -1) return -1;
	}
	pos--;
	if(buf[pos] == 'P' && buf[pos+1] == 'K' && 
	   buf[pos+2] == 5 && buf[pos+3] == 6) {
	    found = 1;
	    break;
	}
    } 
  
    if(!found) {
	v->errn = EIO;
	return -1;
    }

    bufstart += pos;
    if(read_from(v, file, buf, bufstart, ECREC_SIZE) == -1) return -1;
  
    ecrec->this_disk =     DBYTE(buf+ECREC_THIS_DISK);
    ecrec->cdir_disk =     DBYTE(buf+ECREC_CDIR_DISK);
    ecrec->this_entries =  DBYTE(buf+ECREC_THIS_ENTRIES);
    ecrec->total_entries = DBYTE(buf+ECREC_TOTAL_ENTRIES);
    ecrec->cdir_size =     QBYTE(buf+ECREC_CDIR_SIZE);
    ecrec->cdir_off =      QBYTE(buf+ECREC_CDIR_OFF);
    ecrec->comment_len =   DBYTE(buf+ECREC_COMMENT_LEN);

    return bufstart;
}

static avtime_t dos2unix_time(avqbyte dt)
{
    struct avtm ut;

    ut.sec = dos_ftsec(dt);
    ut.min = dos_ftmin(dt);
    ut.hour = dos_fthour(dt);
    ut.day = dos_ftday(dt);
    ut.mon = dos_ftmonth(dt);
    ut.year = dos_ftyear(dt);

    return av_mktime(&ut);
}

static avmode_t dos2unix_attr(avqbyte da, avmode_t archmode)
{
    avmode_t mode = (archmode & 0666);
    if (da & 0x01) mode = mode & ~0222;
    if (da & 0x10) mode = mode | ((mode & 0444) >> 2) | AV_IFDIR;
    else mode |= AV_IFREG;

    return mode;
}

static int fill_zipentry(ave *v, arch_entry *ent, struct cdirentry *cent,
                         struct ecrec *ecrec)
{
    int res;
    archive *arch = ent->arch;
    struct zipino_data *zipd;
    struct avstat filestat;

    if(ent->ino != AVNULL)
	return 0;
  
    av_default_stat(&filestat);

    /* FIXME: Handle other architectures */
    if((cent->version & 0xFF00) >> 8 == OS_UNIX) 
	filestat.mode = (cent->attr >> 16) & 0xFFFF;
    else
	filestat.mode = dos2unix_attr(cent->attr & 0xFF, arch->mode);
  
    filestat.uid = arch->uid;
    filestat.gid = arch->gid;
    filestat.blocks = AV_DIV(cent->comp_size, 512);
    filestat.blksize = VBLOCKSIZE;
    filestat.dev = arch->dev;
    filestat.ino = arch->inoctr++;
    filestat.mtime = dos2unix_time(cent->mod_time);
    filestat.atime = filestat.mtime;
    filestat.ctime = filestat.mtime;
    filestat.size = cent->file_size;
  
    AV_NEW(v, zipd);
    if(zipd == AVNULL) 
	return -1;

    zipd->crc = cent->crc;
    zipd->comp_size = cent->comp_size;

    res = av_new_inode(v, ent, &filestat);
    if(res == -1) {
        av_free(zipd);
        return -1;
    }
        
    ent->ino->udata = (void *) zipd;

    /* FIXME: multivolume archives */
    if(cent->start_disk != 0 || ecrec->cdir_disk != 0)
        ent->ino->offset = -1;
    else
        ent->ino->offset = cent->file_off;

    return 0;
}

static int insert_zipentry(ave *v, archive *arch, char *path, 
			   struct cdirentry *cent, struct ecrec *ecrec)
{
    int res;
    arch_entry *ent;
    int entflags = 0;

    switch((cent->version & 0xFF00) >> 8) {
    case OS_MSDOS:
    case OS_CPM:
    case OS_VM_CMS:
    case OS_MVS:
    case OS_TANDEM:
    case OS_TOPS20:
    case OS_VMS:
	conv_tolower(path);

	/* fall through */
    case OS_NT:
    case OS_WIN95:
 
	entflags |= ENTF_NOCASE;
    }

    if (path[0] == '\0') {
	v->errn = EIO;
	return -1; /* FIXME: warning: empty name */
    }

    ent = av_find_entry(v, arch->root, path, FIND_CREATE, entflags);
    if(ent == AVNULL)
        return -1;

    if(ent->ino == AVNULL)
        ent->flags = entflags;

    res = fill_zipentry(v, ent, cent, ecrec);
    av_unref_entry(ent);

    return res;
}

static avoff_t read_entry(ave *v, arch_file *file, archive *arch, avoff_t pos,
			  struct ecrec *ecrec)
{
    char buf[CDIRENT_SIZE];
    struct cdirentry ent;
    char *filename;

    if(read_from(v, file, buf, pos, CDIRENT_SIZE) == -1) return -1;
  
    if(buf[0] != 'P' || buf[1] != 'K' || buf[2] != 1 || buf[3] != 2) {
	v->errn = EIO;
	return -1;
    }

    ent.version      = DBYTE(buf+CDIRENT_VERSION);
    ent.need_version = DBYTE(buf+CDIRENT_NEED_VERSION);
    ent.flag         = DBYTE(buf+CDIRENT_FLAG);
    ent.method       = DBYTE(buf+CDIRENT_METHOD);
    ent.mod_time     = QBYTE(buf+CDIRENT_MOD_TIME);
    ent.crc          = QBYTE(buf+CDIRENT_CRC);
    ent.comp_size    = QBYTE(buf+CDIRENT_COMP_SIZE);
    ent.file_size    = QBYTE(buf+CDIRENT_FILE_SIZE);
    ent.fname_len    = DBYTE(buf+CDIRENT_FNAME_LEN);
    ent.extra_len    = DBYTE(buf+CDIRENT_EXTRA_LEN);
    ent.comment_len  = DBYTE(buf+CDIRENT_COMMENT_LEN);
    ent.start_disk   = DBYTE(buf+CDIRENT_START_DISK);
    ent.int_attr     = DBYTE(buf+CDIRENT_INT_ATTR);
    ent.attr         = QBYTE(buf+CDIRENT_ATTR);
    ent.file_off     = QBYTE(buf+CDIRENT_FILE_OFF);

    filename = av_malloc(v, ent.fname_len + 1);
    if(filename == AVNULL) return -1;

    if(read_from(v, file, filename, pos + CDIRENT_SIZE, ent.fname_len) == -1) {
	av_free(filename);
	return -1;
    }
    filename[ent.fname_len] = '\0';

    if(insert_zipentry(v, arch, filename, &ent, ecrec) == -1) {
	av_free(filename);
	return -1;
    }
  
    av_free(filename);

    return pos + CDIRENT_SIZE + ent.fname_len + ent.extra_len + ent.comment_len;
}


static int read_zipfile(ave *v, arch_file *file, archive *arch)
{
    avoff_t ecrec_pos;
    struct ecrec ecrec;
    avoff_t extra_bytes;
    avoff_t cdir_end;
    avoff_t cdir_pos;
    int nument;

    ecrec_pos = find_ecrec(v, file, SEARCHLEN, &ecrec);
    if(ecrec_pos == -1) {
#if 0
	fprintf(stderr, "Couldn't find End of Central Directory Record\n");
#endif
	return -1;
    }

    cdir_end = ecrec.cdir_size+ecrec.cdir_off;

    if(ecrec.this_disk != ecrec.cdir_disk) {
	v->errn = EIO;
	return -1;
    }
  
    extra_bytes = ecrec_pos - cdir_end;
    if(extra_bytes < 0) {
	v->errn = EIO;
	return -1;
    }
  
    if(ecrec.cdir_off == 0 && ecrec.cdir_size == 0) {
	/* Empty zipfile */
	return 0;
    }
  
    cdir_pos = ecrec.cdir_off + extra_bytes;
  
    for(nument = 0; nument < ecrec.total_entries; nument++) {
	if(cdir_pos >= ecrec_pos) {
	    v->errn = EIO;
	    return -1;
	}
	cdir_pos = read_entry(v, file, arch, cdir_pos, &ecrec);
	if(cdir_pos == -1) return -1;
    }
  
    return 0;
}

static int parse_zipfile(ave *v, vpath *path, archive *arch)
{
    int res;
    arch_file file;
  
    if(PARAM(path)[0]) {
        v->errn = ENOENT;
        return -1;
    }

    file.fh = av_open(v, BASE(path), AVO_RDONLY, 0);
    if(file.fh == -1) return -1;
    file.ptr = 0;

    res = read_zipfile(v, &file, arch);
  
    av_close(DUMMYV, file.fh);
    
    return res;  
}


static int zip_close(ave *v, void *devinfo)
{
    arch_fdi *di = (arch_fdi *) devinfo;
  
    if(di->ino->typeflag == METHOD_DEFLATE && di->udata != AVNULL) {
	av_vfile_destroy(v, di->udata);
	di->udata = AVNULL;
    }
    return (*di->vdev->close)(v, devinfo);
}

static void *zip_open(ave *v, vpath *path, int flags, int mode)
{
    char buf[LDIRENT_SIZE];
    struct ldirentry ent;
    int headersize;
    arch_fdi *di;
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    avoff_t offset;
    struct zipino_data *zipd;

    di = (arch_fdi *) (*dd->vdev->open)(v,  path, flags, mode);
    if(di == AVNULL) return AVNULL;
    offset = di->ino->offset;
    zipd = (struct zipino_data *) di->ino->udata;

    if(AV_ISDIR(di->ino->st.mode)) return di;
  
    if(offset == -1) {
	v->errn = ENODEV;
	goto error;
    }

    if(read_from(v, &di->file, buf, di->ino->offset, LDIRENT_SIZE) == -1) 
	goto error;

    if(buf[0] != 'P' || buf[1] != 'K' || buf[2] != 3 || buf[3] != 4) {
	v->errn = EIO;
	goto error;
    }

    ent.need_version = DBYTE(buf+LDIRENT_NEED_VERSION);
    ent.flag         = DBYTE(buf+LDIRENT_FLAG);
    ent.method       = DBYTE(buf+LDIRENT_METHOD);
    ent.mod_time     = QBYTE(buf+LDIRENT_MOD_TIME);
    ent.crc          = QBYTE(buf+LDIRENT_CRC);
    ent.comp_size    = QBYTE(buf+LDIRENT_COMP_SIZE);
    ent.file_size    = QBYTE(buf+LDIRENT_FILE_SIZE);
    ent.fname_len    = DBYTE(buf+LDIRENT_FNAME_LEN);
    ent.extra_len    = DBYTE(buf+LDIRENT_EXTRA_LEN);

    if(ent.method != METHOD_STORE && ent.method != METHOD_DEFLATE) {
	v->errn = ENODEV;
	goto error;
    }

    if((ent.flag & 0x08) != 0) {
	/* can't trust local header, use central directory: */
    
	ent.comp_size = zipd->comp_size;
	ent.file_size = di->ino->st.size;
	ent.crc = zipd->crc;
    }

    di->ino->typeflag = ent.method;
    headersize = LDIRENT_SIZE + ent.fname_len + ent.extra_len;

    di->ino->realsize = headersize + ent.comp_size;

    if(ent.method == METHOD_DEFLATE) {
	struct zfilt_params fp;

	di->offset = di->ino->offset + headersize;
	/* The extra 4 bytes are to make inflate happy */
	di->size = ent.comp_size + 4; 
	di->lptr = 0;

	fp.read = di->vdev->read;
	fp.lseek = di->vdev->lseek;
	fp.crc = ent.crc;
	fp.isgzip = 0;
	fp.vfileflags = VFILE_CACHE_2;
	fp.devinfo = (void *) di;

	di->udata = av_zfilt_create(v, &fp);
	if(di->udata == AVNULL) goto error;
    }
    else {
	/* STORE */
    
	di->offset = di->ino->offset + headersize;
	di->size = ent.comp_size;
    }

    return (void *) di;

 error:
    zip_close(v, (void *) di);
    return AVNULL;
}

static avssize_t zip_read(ave *v, void *devinfo, char *buf, avsize_t nbyte)
{
    arch_fdi *di = (arch_fdi *) devinfo;
    avssize_t res;

    if(di->ino->typeflag == METHOD_DEFLATE) {
	res = av_vfile_read(v, di->udata, buf, di->lptr, nbyte);
	if(res != -1) di->lptr += res;
    }
    else
	res = (*di->vdev->read)(v, devinfo, buf, nbyte);

    return res;
}

static avoff_t zip_lseek(ave *v, void *devinfo, avoff_t offset, int whence)
{
    arch_fdi *di = (arch_fdi *) devinfo;

    if(di->ino->typeflag == METHOD_DEFLATE) 
	return av_generic_lseek(v, &di->lptr, di->ino->st.size, offset, whence);
    else
	return (*di->vdev->lseek)(v, devinfo, offset, whence);
}

extern int av_init_module_uzip(ave *v);

int av_init_module_uzip(ave *v)
{
    struct ext_info zipexts[3];
    struct vdev_info *vdev;
    arch_devd *dd;

    INIT_EXT(zipexts[0], ".zip", AVNULL);
    INIT_EXT(zipexts[1], ".jar", AVNULL);
    INIT_EXT(zipexts[2], AVNULL, AVNULL);

    vdev = av_init_arch(v, "uzip", zipexts, AV_VER);
    if(vdev == AVNULL) return -1;
  
    dd = (arch_devd *) vdev->devdata;
    dd->parsefunc = parse_zipfile;

    vdev->open = zip_open;
    vdev->close = zip_close;
    vdev->read = zip_read;
    vdev->lseek = zip_lseek;

    return av_add_vdev(v, vdev);
}
#endif
