/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    ZIP module
*/

#include "archive.h"
#include "zipconst.h"
#include "zfile.h"
#include "cache.h"
#include "oper.h"
#include "version.h"

struct ecrec {
    avushort this_disk;
    avushort cdir_disk;
    avushort this_entries;
    avushort total_entries;
    avuint cdir_size;
    avuint cdir_off;
    avushort comment_len;
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
    avushort version;
    avushort need_version;
    avushort flag;
    avushort method;
    avuint mod_time;
    avuint crc;
    avuint comp_size;
    avuint file_size;
    avushort fname_len;
    avushort extra_len;
    avushort comment_len;
    avushort start_disk;
    avushort int_attr;
    avuint attr;
    avuint file_off;         
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
    avushort need_version;
    avushort flag;
    avushort method;
    avuint mod_time;
    avuint crc;
    avuint comp_size;
    avuint file_size;
    avushort fname_len;
    avushort extra_len;
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

struct zipnode {
    avuint crc;
    avushort method;
    avoff_t headeroff;
    struct cacheobj *cache;
};

static void conv_tolower(char *s)
{
    for(; *s; s++) *s = tolower(*s);
}

static avoff_t find_ecrec(vfile *vf, long searchlen, struct ecrec *ecrec)
{
    int res;
    avoff_t bufstart;
    int pos;
    char buf[BUFSIZE+3];
    avoff_t sres;
    int found;
  
    sres = av_lseek(vf, 0, AVSEEK_END);
    if(sres < 0)
        return sres;
    if(sres < ECREC_SIZE) {
        av_log(AVLOG_ERROR, "UZIP: Broken archive");
        return -EIO;
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
            res = av_pread_all(vf, buf, pos, bufstart);
            if(res < 0)
                return res;
	}
	pos--;
	if(buf[pos] == 'P' && buf[pos+1] == 'K' && 
	   buf[pos+2] == 5 && buf[pos+3] == 6) {
	    found = 1;
	    break;
	}
    } 
  
    if(!found) {
        av_log(AVLOG_ERROR, 
               "UZIP: Couldn't find End of Central Directory Record");
        return -EIO;
    }

    bufstart += pos;
    res = av_pread_all(vf, buf, ECREC_SIZE, bufstart);
    if(res < 0)
        return res;
  
    ecrec->this_disk =     DBYTE(buf+ECREC_THIS_DISK);
    ecrec->cdir_disk =     DBYTE(buf+ECREC_CDIR_DISK);
    ecrec->this_entries =  DBYTE(buf+ECREC_THIS_ENTRIES);
    ecrec->total_entries = DBYTE(buf+ECREC_TOTAL_ENTRIES);
    ecrec->cdir_size =     QBYTE(buf+ECREC_CDIR_SIZE);
    ecrec->cdir_off =      QBYTE(buf+ECREC_CDIR_OFF);
    ecrec->comment_len =   DBYTE(buf+ECREC_COMMENT_LEN);

    return bufstart;
}

static avtime_t dos2unix_time(avuint dt)
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

static avmode_t dos2unix_attr(avuint da, avmode_t archmode)
{
    avmode_t mode = (archmode & 0666);
    if (da & 0x01) mode = mode & ~0222;
    if (da & 0x10) mode = mode | ((mode & 0444) >> 2) | AV_IFDIR;
    else mode |= AV_IFREG;

    return mode;
}

static avmode_t zip_get_mode(struct cdirentry *cent, const char *path,
                             avmode_t origmode)
{
    avmode_t mode;

    /* FIXME: Handle other architectures */
    if((cent->version & 0xFF00) >> 8 == OS_UNIX) 
	mode = (cent->attr >> 16) & 0xFFFF;
    else
	mode = dos2unix_attr(cent->attr & 0xFF, origmode);

    if(path[0] && path[strlen(path)-1] == '/')
        mode = (mode & 07777) | AV_IFDIR;

    return mode;
}

static void zipnode_delete(struct zipnode *nod)
{
    av_unref_obj(nod->cache);
}

static void fill_zipentry(struct archive *arch, const char *path, 
                          struct entry *ent, struct cdirentry *cent,
                          struct ecrec *ecrec)
{
    struct archnode *nod;
    struct zipnode *info;
    int isdir = AV_ISDIR(zip_get_mode(cent, path, 0));

    nod = av_arch_new_node(arch, ent, isdir);
    
    nod->st.mode = zip_get_mode(cent, path, nod->st.mode);
    nod->st.size = cent->file_size;
    nod->st.blocks = AV_BLOCKS(cent->comp_size);
    nod->st.blksize = 4096;
    nod->st.mtime.sec = dos2unix_time(cent->mod_time);
    nod->st.mtime.nsec = 0;
    nod->st.atime = nod->st.mtime;
    nod->st.ctime = nod->st.mtime;
    nod->realsize = cent->comp_size;

    AV_NEW_OBJ(info, zipnode_delete);
    nod->data = info;

    info->cache = NULL;
    info->crc = cent->crc;
    info->method = 0;

    /* FIXME: multivolume archives */
    if(cent->start_disk != 0 || ecrec->cdir_disk != 0)
        info->headeroff = -1;
    else
        info->headeroff = cent->file_off;

}

static void insert_zipentry(struct archive *arch, char *path, 
                            struct cdirentry *cent, struct ecrec *ecrec)
{
    struct entry *ent;
    int entflags = 0;

    /* FIXME: option for uzip, not to convert filenames to lowercase */
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
 
	entflags |= NSF_NOCASE;
    }

    ent = av_arch_create(arch, path, entflags);
    if(ent == NULL)
        return;

    fill_zipentry(arch, path, ent, cent, ecrec);
    av_unref_obj(ent);
}

static avoff_t read_entry(vfile *vf, struct archive *arch, avoff_t pos,
                          struct ecrec *ecrec)
{
    int res;
    char buf[CDIRENT_SIZE];
    struct cdirentry ent;
    char *filename;

    res = av_pread_all(vf, buf, CDIRENT_SIZE, pos);
    if(res < 0)
        return res;
  
    if(buf[0] != 'P' || buf[1] != 'K' || buf[2] != 1 || buf[3] != 2) {
        av_log(AVLOG_ERROR, "UZIP: Broken archive");
        return -EIO;
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

    filename = av_malloc(ent.fname_len + 1);
    res = av_pread_all(vf, filename, ent.fname_len, pos + CDIRENT_SIZE);
    if(res < 0) {
        av_free(filename);
        return res;
    }
    filename[ent.fname_len] = '\0';

    insert_zipentry(arch, filename, &ent, ecrec);
    av_free(filename);

    return pos + CDIRENT_SIZE + ent.fname_len + ent.extra_len +
        ent.comment_len;
}


static int read_zipfile(vfile *vf, struct archive *arch)
{
    avoff_t ecrec_pos;
    struct ecrec ecrec;
    avoff_t extra_bytes;
    avoff_t cdir_end;
    avoff_t cdir_pos;
    int nument;

    ecrec_pos = find_ecrec(vf, SEARCHLEN, &ecrec);
    if(ecrec_pos < 0)
        return ecrec_pos;

    cdir_end = ecrec.cdir_size+ecrec.cdir_off;

    if(ecrec.this_disk != ecrec.cdir_disk) {
        av_log(AVLOG_ERROR, "UZIP: Cannot handle multivolume archives");
        return -EIO;
    }
  
    extra_bytes = ecrec_pos - cdir_end;
    if(extra_bytes < 0) {
        av_log(AVLOG_ERROR, "UZIP: Broken archive");
        return -EIO;
    }
  
    if(ecrec.cdir_off == 0 && ecrec.cdir_size == 0) {
	/* Empty zipfile */
	return 0;
    }
  
    cdir_pos = ecrec.cdir_off + extra_bytes;
  
    for(nument = 0; nument < ecrec.total_entries; nument++) {
	if(cdir_pos >= ecrec_pos) {
            av_log(AVLOG_ERROR, "UZIP: Broken archive");
            return -EIO;
	}
	cdir_pos = read_entry(vf, arch, cdir_pos, &ecrec);
	if(cdir_pos < 0) 
            return cdir_pos;
    }
  
    return 0;
}

static int parse_zipfile(void *data, ventry *ve, struct archive *arch)
{
    int res;
    vfile *vf;

    res = av_open(ve->mnt->base, AVO_RDONLY, 0, &vf);
    if(res < 0)
        return res;

    res = read_zipfile(vf, arch);
    av_close(vf);
    
    return res;  
}

static int zip_close(struct archfile *fil)
{
    struct zfile *zfil = fil->data;

    av_unref_obj(zfil);
    return 0;
}

static int zip_open(ventry *ve, struct archfile *fil)
{
    int res;
    char buf[LDIRENT_SIZE];
    struct ldirentry ent;
    int headersize;
    struct zipnode *info = (struct zipnode *) fil->nod->data;
    avoff_t offset = info->headeroff;

    if(offset == -1) {
        av_log(AVLOG_ERROR, "UZIP: Cannot handle multivolume archives");
        return -ENOENT;
    }

    res = av_pread_all(fil->basefile, buf, LDIRENT_SIZE, offset);
    if(res < 0)
        return res;

    if(buf[0] != 'P' || buf[1] != 'K' || buf[2] != 3 || buf[3] != 4) {
        av_log(AVLOG_ERROR, "UZIP: Broken archive");
        return -EIO;
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
        av_log(AVLOG_ERROR, "UZIP: Cannot handle compression method %i",
               ent.method);
        return -ENOENT;
    }

    if((ent.flag & 0x08) != 0) {
	/* can't trust local header, use central directory: */
    
	ent.comp_size = fil->nod->realsize;
	ent.file_size = fil->nod->st.size;
	ent.crc = info->crc;
    }

    info->method = ent.method;
    headersize = LDIRENT_SIZE + ent.fname_len + ent.extra_len;
    fil->nod->offset = offset + headersize;

    if(ent.method == METHOD_DEFLATE) {
        struct zfile *zfil;

        zfil = av_zfile_new(fil->basefile, fil->nod->offset, ent.crc);
        fil->data = zfil;
    }

    return 0;
}


static avssize_t zip_deflate_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct archfile *fil = arch_vfile_file(vf);
    struct zfile *zfil = (struct zfile *) fil->data;
    struct zipnode *info = (struct zipnode *) fil->nod->data;
    struct zcache *zc;

    zc = (struct zcache *) av_cacheobj_get(info->cache);
    if(zc == NULL) {
        av_unref_obj(info->cache);
        info->cache = NULL;
        zc = av_zcache_new();
    }
    
    res = av_zfile_pread(zfil, zc, buf, nbyte, vf->ptr);
    if(res >= 0) {
        avoff_t cachesize;

        vf->ptr += res;
        cachesize = av_zcache_size(zc);
        if(cachesize != 0) {
            /* FIXME: name of this cacheobj? */
            if(info->cache == NULL)
                info->cache = av_cacheobj_new(zc, "(uzip:index)");
            av_cacheobj_setsize(info->cache, cachesize);
        }
    }
    else {
        av_unref_obj(info->cache);
        info->cache = NULL;
    }
    av_unref_obj(zc);

    return res;
}

static avssize_t zip_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct archfile *fil = arch_vfile_file(vf);
    struct zfile *zfil = (struct zfile *) fil->data;

    if(zfil != NULL)
        res = zip_deflate_read(vf, buf, nbyte);
    else
        res = av_arch_read(vf, buf, nbyte);

    return res;
}

extern int av_init_module_uzip(struct vmodule *module);

int av_init_module_uzip(struct vmodule *module)
{
    int res;
    struct avfs *avfs;
    struct ext_info zipexts[5];
    struct archparams *ap;

    zipexts[0].from = ".zip",   zipexts[0].to = NULL;
    zipexts[1].from = ".jar",   zipexts[1].to = NULL;
    zipexts[2].from = ".ear",   zipexts[2].to = NULL;
    zipexts[3].from = ".war",   zipexts[3].to = NULL;
    zipexts[4].from = NULL;

    res = av_archive_init("uzip", zipexts, AV_VER, module, &avfs);
    if(res < 0)
        return res;

    ap = (struct archparams *) avfs->data;
    ap->parse = parse_zipfile;
    ap->open = zip_open;
    ap->close = zip_close;
    ap->read = zip_read;

    av_add_avfs(avfs);

    return 0;
}

