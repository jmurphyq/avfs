/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    GUNZIP module
*/

#include "zfilt.h"
#include "cache.h"
#include "zipconst.h"

#define BLOCKSIZE 1024

#define PARAM_NOCACHE '='

#define GZHEADER_SIZE 10
#define GZFOOTER_SIZE 8

#define BI(ptr, i)  ((avbyte) (ptr)[i])
#define DBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8))
#define QBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8) | \
                   (BI(ptr,2)<<16) | (BI(ptr,3)<<24))

struct gunzip {
    cache_obj *cobj;
    int fh;
    avoff_t lastoffset;
    int file_usage;
    vpath *savepath;
    vfile *vf;
    rep_file *rf;
    struct avstat stbuf;
    avtime_t basetime;
    int dirty;
};

typedef struct {
    struct gunzip *gunz;
    avoff_t ptr;
} gunzip_fdi;

static void setqbyte(avbyte *ptr, avqbyte num)
{
    ptr[0] = num         & 0xFF;
    ptr[1] = (num >> 8)  & 0xFF;
    ptr[2] = (num >> 16) & 0xFF;
    ptr[3] = (num >> 24) & 0xFF;
}

static int params_ok(ave *v, const char *params)
{
    if(params[0] && params[0] != PARAM_NOCACHE) {
        if(params[0] == DIR_SEP_CHAR) v->errn = ENOTDIR;
        else v->errn = ENOENT;
    
        return 0;
    }
    return 1;
}

static avssize_t orig_read(ave *v, void *devinfo, char *buf, avsize_t nbyte)
{
    struct gunzip *gunz = (struct gunzip *) devinfo;

    return __av_read(v, gunz->fh, buf, nbyte);
}

static avssize_t orig_write(ave *v, void *devinfo, const char *buf,
			    avsize_t nbyte)
{
    struct gunzip *gunz = (struct gunzip *) devinfo;

    return __av_write(v, gunz->rf->outfd, buf, nbyte);
}

static avoff_t orig_lseek(ave *v, void *devinfo, avoff_t offset, int whence)
{
    struct gunzip *gunz = (struct gunzip *) devinfo;

    return __av_lseek(v, gunz->fh, offset, whence);
}


static int read_sure(ave *v, int fh, char *buf, avsize_t nbyte)
{
    avssize_t rres;
  
    rres = __av_read(v, fh, buf, nbyte);
    if(rres == -1) return -1;
  
    if(rres != (avssize_t) nbyte) {
        __av_log(AVLOG_ERROR, "UGZ: Error: Premature end of file");
        v->errn = EIO;
        return -1;
    }
  
    return 0;
}

static int read_gzip_header(ave *v, struct gunzip *gunz)
{
    avbyte buf[GZHEADER_SIZE];
    int method, flags;
    int fh = gunz->fh;

    if(read_sure(v, fh, buf, GZHEADER_SIZE) == -1) return -1;

    if(buf[0] != GZMAGIC1 || buf[1] != GZMAGIC2) {
        __av_log(AVLOG_ERROR, "UGZ: Error: File not in GZIP format");
        v->errn = EIO;
        return -1;
    }

    method = buf[2];
    flags  = buf[3];
  
    if(method != METHOD_DEFLATE) {
        __av_log(AVLOG_ERROR, "UGZ: Error: File compression is not DEFLATE");
        v->errn = EIO;
        return -1;
    }

    if(flags & GZFL_RESERVED) {
        __av_log(AVLOG_ERROR, "UGZ: Error: Unknown flags");
        v->errn = EIO;
        return -1;
    }

    gunz->stbuf.mtime = QBYTE(buf+4);
    gunz->stbuf.ctime = gunz->stbuf.mtime;
    gunz->stbuf.atime = gunz->stbuf.mtime;
  
    /* What's the 9. and 10. byte of the header? */

    if(__av_lseek(v, fh, -4, AVSEEK_END) == -1) return -1;
    if(read_sure(v, fh, buf, 4) == -1) return -1;

    gunz->stbuf.size = QBYTE(buf);
  

    /* Let zfilt.c reread the whole header */
    if(__av_lseek(v, fh, 0, AVSEEK_SET) == -1) return -1;

    return 0;
}

static void gunzip_free(void *obj)
{
    struct gunzip *gunz = (struct gunzip *) obj;

    __av_free_vpath(gunz->savepath);
    if(gunz->vf != AVNULL) __av_vfile_destroy(DUMMYV, gunz->vf);
    if(gunz->fh != -1) __av_close(DUMMYV, gunz->fh);

    __av_free(gunz);
}

static int gunzip_valid(cache_obj *cobj, void *param)
{    
    struct gunzip *gunz = (struct gunzip *) cobj->obj;
    avtime_t *mtimep = (avtime_t *) param;

    if(gunz->basetime != *mtimep) return -1;
    return 1;
}

static int new_gunzip(ave *v, struct gunzip *gunz, vpath *path, int flags,
		      int empty)
		      
{
    struct zfilt_params fp;
  
    gunz->fh = -1;
    gunz->lastoffset = 0;
    gunz->file_usage = 0;
    gunz->vf = AVNULL;
    gunz->savepath = AVNULL;

    gunz->basetime = gunz->stbuf.mtime;

    gunz->stbuf.mode &= ~(07000);  /* Clear SETU/GID bits  */
    gunz->stbuf.blksize = BLOCKSIZE;
    gunz->stbuf.blocks  = 0;
    gunz->stbuf.size = 0;

    gunz->dirty = 0;

    gunz->savepath = __av_copy_vpath(v, path);
    if(gunz->savepath == AVNULL) return -1;

    gunz->fh = __av_open(v, BASE(path), AVO_RDONLY, 0);
    if(gunz->fh == -1) return -1;

    __av_registerfd(gunz->fh);
    gunz->file_usage = 1;

    if(!empty && read_gzip_header(v, gunz) == -1) return -1;

    fp.read = orig_read;
    fp.lseek = orig_lseek;
    fp.write = orig_write;
    fp.crc = 0;
    fp.isgzip = 1;
    if(PARAM(path)[0] == PARAM_NOCACHE)
        fp.vfileflags = VFILE_NOCACHE;
    else 
        fp.vfileflags = VFILE_CACHE;

    fp.devinfo = (void *) gunz;

    gunz->vf = __av_zfilt_create(v, &fp);
    if(gunz->vf == AVNULL) return -1;

    return 0;
}

static struct gunzip *get_gunzip(ave *v, vpath *path, int flags, int mode)
{
    cache_obj *cobj;
    struct gunzip *gunz;
    struct avstat stbuf;
    int major, minor;
    int empty = 0;
    int res;

    if(__av_stat(v, BASE(path), &stbuf, AVFS_FLAG_NOSIZE) == -1) {
        if(v->errn == ENOENT && (flags & AVO_CREAT)) {
            int tmpfd;

            tmpfd = __av_open(v, BASE(path), 
                              AVO_WRONLY | AVO_TRUNC | AVO_CREAT | 
                              (flags & AVO_EXCL), mode);
            if(tmpfd == -1) return AVNULL;
            res = __av_fstat(v, tmpfd, &stbuf, AVFS_FLAG_NOSIZE);
            __av_close(DUMMYV, tmpfd);
            if(res == -1) return AVNULL;
      
            empty = 1;
        }
        else return AVNULL;
    }
    else {
        if((flags & (AVO_EXCL | AVO_CREAT)) == (AVO_EXCL | AVO_CREAT)) {
            v->errn = EEXIST;
            return AVNULL;
        }

        if(flags & (AVO_ACCMODE | AVO_TRUNC) && 
           __av_access(v, BASE(path), AVW_OK) == -1) return AVNULL;
    }

    if(flags & AVO_TRUNC) empty = 1;

    major = __av_get_vdev(path)->major;
    minor = __av_getminor(major, stbuf.dev, stbuf.ino);
    cobj = __av_cache_find(v, major, minor, gunzip_valid, 
                           (void *) &stbuf.mtime);

    if(cobj == AVNULL) return AVNULL;

    if(cobj->obj != AVNULL) {
        __av_cache_op(cobj, COBJ_LOCK);
        gunz = (struct gunzip *) cobj->obj;
        if(gunz->file_usage == 0) {
            gunz->fh = __av_open(v, BASE(path), AVO_RDONLY, 0);
            if(gunz->fh == -1 ||
               __av_lseek(v, gunz->fh, gunz->lastoffset, AVSEEK_SET) == -1) {
                __av_cache_op(cobj, COBJ_FREE);
                return AVNULL;
            }
        }
        gunz->file_usage ++;
        __av_cache_op(cobj, COBJ_UNLOCK);
    }
    else {
        AV_NEW(v, gunz);
        if(gunz == AVNULL) {
            __av_cache_op(cobj, COBJ_FREE);
            return AVNULL;
        }
        cobj->obj = (void *) gunz;

        cobj->destroy = gunzip_free;
        /* cobj->keep_time = ???; */

        gunz->cobj = cobj;
        gunz->stbuf = stbuf;
        gunz->stbuf.dev = __av_vmakedev(major, minor);
        gunz->stbuf.ino = ((minor >> 8) & 0xFFF) + 3;

        res = new_gunzip(v, gunz, path, flags, empty);
        if(res == -1) {
            __av_cache_op(cobj, COBJ_FREE);
            return AVNULL;
        }

        __av_cache_op(cobj, COBJ_UNLOCK);
    }

    if(empty) {
        __av_cache_op(cobj, COBJ_LOCK);
        res = __av_vfile_truncate(v, gunz->vf, 0);
        gunz->dirty = 1;
        gunz->stbuf.size = 0;
        __av_cache_op(cobj, COBJ_UNLOCK);

        if(res == -1) {
            __av_cache_op(cobj, COBJ_FREE);
            return AVNULL;
        }
    }

    return gunz;
}


static void *gunzip_open(ave *v, vpath *path, int flags, int mode)
{
    gunzip_fdi *di = AVNULL;

    if(!params_ok(v, PARAM(path))) return AVNULL;

    AV_NEW(v, di);
    if(di == AVNULL) return AVNULL;

    di->gunz = get_gunzip(v, path, flags, mode);
    if(di->gunz == AVNULL) {
        __av_free(di);
        return AVNULL;
    }
    di->ptr = 0;

    return (void *) di;
}

static int write_gzip_header(ave *v, struct gunzip *gunz)
{
    avbyte buf[GZHEADER_SIZE];
    avtime_t currtime;
    avssize_t wres;

    currtime = __av_gettime();
  
    buf[0] = GZMAGIC1;
    buf[1] = GZMAGIC2;
    buf[2] = METHOD_DEFLATE;
    buf[3] = 0;                        /* flags */

    setqbyte(buf+4, currtime);

    buf[8] = 0;                        /* xflags ??? */
    buf[9] = OS_UNIX;

    wres = __av_write(v, gunz->rf->outfd, buf, GZHEADER_SIZE);
    if(wres == -1) return -1;

    return 0;
}

static int write_gzip_footer(ave *v, struct gunzip *gunz, avqbyte crc)
{
    avbyte buf[GZFOOTER_SIZE];
    avssize_t wres;
  
    setqbyte(buf, crc);
    setqbyte(buf+4, gunz->stbuf.size);
  
    wres = __av_write(v, gunz->rf->outfd, buf, GZFOOTER_SIZE);
    if(wres == -1) return -1;
  
    return 0;
}

static int flush_gzip(ave *v, struct gunzip *gunz)
{
    int res;
    avqbyte crc;

    gunz->rf = __av_get_replacement(v, BASE(gunz->savepath), 0);
    if(gunz->rf == AVNULL) return -1;
  
    res = write_gzip_header(v, gunz);
    if(res != -1)
        res = __av_zfilt_flush(v, gunz->vf, &crc);
    if(res != -1)
        res = write_gzip_footer(v, gunz, crc);

    if(res == -1) {
        __av_del_replacement(gunz->rf);
        return -1;
    }
  
    res = __av_replace_file(v, gunz->rf);
  
    return res;
}

static int gunzip_close(ave *v, void *devinfo)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;
    struct gunzip *gunz = di->gunz;
    int res = 0;

    __av_cache_op(gunz->cobj, COBJ_LOCK);
    if(gunz->dirty) {
        res = flush_gzip(v, gunz);
        if(res != -1) gunz->dirty = 0;
    }
    
    gunz->file_usage --;
    if(gunz->file_usage == 0) {
        gunz->lastoffset = __av_lseek(DUMMYV, gunz->fh, 0, AVSEEK_CUR);
        __av_close(DUMMYV, gunz->fh);
        gunz->fh = -1;
    }
    __av_cache_op(gunz->cobj, COBJ_UNLOCK);

    __av_cache_op(gunz->cobj, COBJ_RELEASE);
    __av_free(di);

    return res;
}

static avssize_t gunzip_read(ave *v, void *devinfo, char *buf, avsize_t nbyte)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;
    avssize_t res;

    __av_cache_op(di->gunz->cobj, COBJ_LOCK);  
    res = __av_vfile_read(v, di->gunz->vf, buf, di->ptr, nbyte);
    __av_cache_op(di->gunz->cobj, COBJ_UNLOCK);  

    if(res != -1) di->ptr += res;
  
    return res;
}

static avssize_t gunzip_write(ave *v, void *devinfo, const char *buf,
			      avsize_t nbyte)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;
    struct gunzip *gunz = di->gunz;
    avssize_t res;

    if(nbyte == 0) return 0;

    __av_cache_op(gunz->cobj, COBJ_LOCK);
    res = __av_vfile_write(v, gunz->vf, buf, di->ptr, nbyte);
    if(res != -1) {
        gunz->dirty = 1;
        if(gunz->vf->size != -1) gunz->stbuf.size = gunz->vf->size;
    }
    __av_cache_op(gunz->cobj, COBJ_UNLOCK);  

    if(res != -1) di->ptr += res;

    return res;
}

static int gunzip_ftruncate(ave *v, void *devinfo, avoff_t length)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;
    struct gunzip *gunz = di->gunz;
    int res;

    __av_cache_op(gunz->cobj, COBJ_LOCK);
    res = __av_vfile_truncate(v, gunz->vf, length);
    if(res != -1) {
        gunz->dirty = 1;
        if(gunz->vf->size != -1) gunz->stbuf.size = gunz->vf->size;
    }
    __av_cache_op(gunz->cobj, COBJ_UNLOCK);

    return res;
}

static avoff_t gunzip_lseek(ave *v, void *devinfo, avoff_t offset, int whence)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;

    return __av_generic_lseek(v, &di->ptr, di->gunz->stbuf.size, offset,
                              whence);
}

static int gunzip_fstat(ave *v, void *devinfo, struct avstat *buf, int flags)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;

    *buf = di->gunz->stbuf;
    return 0;
}

static int gunzip_access(ave *v, vpath *path, int amode)
{
    if(!params_ok(v, PARAM(path))) return -1;

    if(amode & AVX_OK) {
        v->errn = EACCES;
        return -1;
    }
  
    return __av_access(v, BASE(path), amode);
}

static int gunzip_chmod(ave *v, vpath *path, avmode_t mode)
{
    if(!params_ok(v, PARAM(path))) return -1;
  
    return __av_chmod(v, BASE(path), mode);
}

static int gunzip_chown(ave *v, vpath *path, avuid_t uid, avgid_t gid,
                        int deref)
{
    if(!params_ok(v, PARAM(path))) return -1;
  
    return __av_chown(v, BASE(path), uid, gid);
}

static int gunzip_fchmod(ave *v, void *devinfo, avmode_t mode)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;
  
    return __av_fchmod(v, di->gunz->fh, mode);
}

static int gunzip_fchown(ave *v, void *devinfo, avuid_t uid, avgid_t gid)
{
    gunzip_fdi *di = (gunzip_fdi *) devinfo;

    return __av_fchown(v, di->gunz->fh, uid, gid);
}


extern int __av_init_module_ugz(ave *v);

int __av_init_module_ugz(ave *v)
{
    struct vdev_info *vdev;
    struct ext_info gunzip_exts[3];

    INIT_EXT(gunzip_exts[0], ".gz", AVNULL);
    INIT_EXT(gunzip_exts[1], ".tgz", ".tar");
    INIT_EXT(gunzip_exts[2], AVNULL, AVNULL);
  
    vdev = __av_new_vdev(v, "ugz", gunzip_exts, AV_VER);
    if(vdev == AVNULL) return -1;

    vdev->open      = gunzip_open;
    vdev->close     = gunzip_close; 
    vdev->read      = gunzip_read;
    vdev->write     = gunzip_write;
    vdev->ftruncate = gunzip_ftruncate;
    vdev->lseek     = gunzip_lseek;
    vdev->fstat     = gunzip_fstat;
    vdev->access    = gunzip_access;
    vdev->chmod     = gunzip_chmod;
    vdev->chown     = gunzip_chown;
    vdev->fchmod    = gunzip_fchmod;
    vdev->fchown    = gunzip_fchown;
  
    vdev->flags = 0;

    return __av_add_vdev(v, vdev);
}
