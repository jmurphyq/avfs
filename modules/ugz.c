/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    GUNZIP module
*/

#include "zfile.h"
#include "zipconst.h"
#include "filecache.h"
#include "cache.h"
#include "oper.h"
#include "version.h"

struct gznode {
    struct avstat sig;
    struct cacheobj *cache;
    avino_t ino;
    avoff_t size;
    avoff_t dataoff;
    avtime_t mtime;
};

struct gzfile {
    struct zfile *zfil;
    vfile *base;
    struct gznode *node;
};

#define GZBUFSIZE 1024

struct gzbuf {
    vfile *vf;
    unsigned char buf[GZBUFSIZE];
    unsigned char *next;
    unsigned int avail;
    unsigned int total;
};

#define GZHEADER_SIZE 10
#define GZFOOTER_SIZE 8

#define GZMAGIC1 0x1f
#define GZMAGIC2 0x8b

/* gzip flag byte */
#define GZFL_ASCII        0x01 /* bit 0 set: file probably ascii text */
#define GZFL_HEAD_CRC     0x02 /* bit 1 set: header CRC present */
#define GZFL_EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define GZFL_ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define GZFL_COMMENT      0x10 /* bit 4 set: file comment present */
#define GZFL_RESERVED     0xE0 /* bits 5..7: reserved */


#define BI(ptr, i)  ((avbyte) (ptr)[i])
#define DBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8))
#define QBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8) | \
                   (BI(ptr,2)<<16) | (BI(ptr,3)<<24))

static int gzbuf_getbyte(struct gzbuf *gb)
{
    if(gb->avail == 0) {
        avssize_t res = av_read(gb->vf, gb->buf, GZBUFSIZE);
        if(res < 0)
            return res;

        gb->next = gb->buf;
        gb->avail = res;
    }
    
    if(gb->avail == 0) {
        av_log(AVLOG_ERROR, "UGZ: Premature end of file");
        return -EIO;
    }
    
    gb->avail --;
    gb->total ++;
    return *gb->next ++;
}

static int gzbuf_skip_string(struct gzbuf *gb)
{
    int c;

    do {
        c = gzbuf_getbyte(gb);
        if(c < 0)
            return c;
    } while(c != '\0');

    return 0;
}

static int gzbuf_read(struct gzbuf *gb, char *buf, avsize_t nbyte)
{
    for(; nbyte; nbyte--) {
        int c = gzbuf_getbyte(gb);
        if(c < 0)
            return c;

        *buf ++ = c;
    }
    return 0;
}


static int gz_read_header(vfile *vf, struct gznode *nod)
{
    int res;
    struct gzbuf gb;
    unsigned char buf[GZHEADER_SIZE];
    int method, flags;
    
    gb.vf = vf;
    gb.avail = 0;
    gb.total = 0;

    res = gzbuf_read(&gb, buf, GZHEADER_SIZE);
    if(res < 0)
        return res;

    if(buf[0] != GZMAGIC1 || buf[1] != GZMAGIC2) {
        av_log(AVLOG_ERROR, "UGZ: File not in GZIP format");
        return -EIO;
    }

    method = buf[2];
    flags  = buf[3];
  
    if(method != METHOD_DEFLATE) {
        av_log(AVLOG_ERROR, "UGZ: File compression is not DEFLATE");
        return -EIO;
    }

    if(flags & GZFL_RESERVED) {
        av_log(AVLOG_ERROR, "UGZ: Unknown flags");
        return -EIO;
    }

    nod->mtime = QBYTE(buf + 4);
  
    /* Ignore bytes 8 and 9 */

    if((flags & GZFL_EXTRA_FIELD) != 0) {
        avsize_t len;

        res = gzbuf_read(&gb, buf, 2);
        if(res < 0)
            return res;

        for(len = DBYTE(buf); len; len--) {
            res = gzbuf_getbyte(&gb);
            if(res < 0)
                return res;
        }
    }

    if((flags & GZFL_ORIG_NAME) != 0) {
        res = gzbuf_skip_string(&gb);
        if(res < 0)
            return res;
    }

    if((flags & GZFL_COMMENT) != 0) {
        res = gzbuf_skip_string(&gb);
        if(res < 0)
            return res;
    }
    if((flags & GZFL_HEAD_CRC) != 0) {
        res = gzbuf_read(&gb, buf, 2);
        if(res < 0)
            return res;
    }

    nod->dataoff = gb.total;

    res = av_lseek(vf, -4, AVSEEK_END);
    if(res < 0)
        return res;

    res = av_read(vf, buf, 4);
    if(res < 0)
        return res;

    if(res != 4) {
        av_log(AVLOG_ERROR, "UGZ: Short read");
        return -EIO;
    }

    nod->size = QBYTE(buf);

    return 0;
}

static void gznode_destroy(struct gznode *nod)
{
    av_unref_obj(nod->cache);
}

static int gz_new_node(ventry *ve, vfile *base, struct avstat *stbuf,
                       struct gznode **resp)
{
    int res;
    struct gznode *nod;

    AV_NEW_OBJ(nod, gznode_destroy);
    nod->sig = *stbuf;
    nod->cache = NULL;
    nod->ino = av_new_ino(ve->mnt->avfs);
    
    res = gz_read_header(base, nod);
    if(res < 0) {
        av_unref_obj(nod);
        return res;
    }

    *resp = nod;    
    return 0;
}

static int gz_same(struct gznode *nod, struct avstat *stbuf)
{
    if(nod->sig.ino == stbuf->ino &&
       nod->sig.dev == stbuf->dev &&
       nod->sig.size == stbuf->size &&
       AV_TIME_EQ(nod->sig.mtime, stbuf->mtime))
        return 1;
    else
        return 0;
}

static int gz_getnode(ventry *ve, vfile *base, struct gznode **resp)
{
    int res;
    struct avstat stbuf;
    int attrmask = AVA_INO | AVA_DEV | AVA_SIZE | AVA_MTIME;
    struct gznode *nod;
    char *key;

    res = av_fgetattr(base, &stbuf, attrmask);
    if(res < 0)
        return res;

    res = av_filecache_getkey(ve, &key);
    if(res < 0)
        return res;
    
    AV_LOCK(ve->mnt->avfs->lock);
    nod = (struct gznode *) av_filecache_get(key);
    if(nod != NULL) {
        if(!gz_same(nod, &stbuf)) {
            av_unref_obj(nod);
            nod = NULL;
        }
    }
    
    if(nod == NULL) {
        res = gz_new_node(ve, base, &stbuf, &nod);
        if(res == 0)
            av_filecache_set(key, nod);
    }
    else
        res = 0;
    AV_UNLOCK(ve->mnt->avfs->lock);

    av_free(key);

    *resp = nod;
    return res;
}

static struct zcache *gz_getcache(ventry *base, struct gznode *nod)
{
    struct zcache *cache;
    
    cache = (struct zcache * ) av_cacheobj_get(nod->cache);
    if(cache == NULL) {
        int res;
        char *name;

        res = av_generate_path(base, &name);
        if(res < 0)
            name = NULL;
        else
            name = av_stradd(name, "(index)", NULL);

        cache = av_zcache_new();
        av_unref_obj(nod->cache);
        nod->cache = av_cacheobj_new(cache, name);
        av_free(name);
    }

    return cache;
}

static int gz_lookup(ventry *ve, const char *name, void **newp)
{
    char *path = (char *) ve->data;
    
    if(path == NULL) {
        if(name[0] != '\0')
            return -ENOENT;
        path = av_strdup(name);
    }
    else if(name == NULL) {
        av_free(path);
        path = NULL;
    }
    else 
        return -ENOENT;
    
    *newp = path;
    return 0;
}

static int gz_access(ventry *ve, int amode)
{
    return av_access(ve->mnt->base, amode);
}

static int gz_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    vfile *base;
    struct gznode *nod;
    struct gzfile *fil;

    if(flags & AVO_DIRECTORY)
        return -ENOTDIR;

    if(AV_ISWRITE(flags))
        return -EPERM;

    res = av_open(ve->mnt->base, AVO_RDONLY, 0, &base);
    if(res < 0)
        return res;

    res = gz_getnode(ve, base, &nod);
    if(res < 0) {
        av_close(base);
        return res;
    }

    AV_NEW(fil);
    if((flags & AVO_ACCMODE) != AVO_NOPERM)
        fil->zfil = av_zfile_new(base, nod->dataoff);
    else
        fil->zfil = NULL;

    fil->base = base;
    fil->node = nod;
    
    *resp = fil;
    return 0;
}

static int gz_close(vfile *vf)
{
    struct gzfile *fil = (struct gzfile *) vf->data;

    av_unref_obj(fil->zfil);
    av_unref_obj(fil->node);
    av_close(fil->base);
    av_free(fil);

    return 0;
}

static avssize_t gz_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct gzfile *fil = (struct gzfile *) vf->data;
    struct zcache *zc;
    struct cacheobj *cobj;

    AV_LOCK(vf->mnt->avfs->lock);
    zc = gz_getcache(vf->mnt->base, fil->node);
    cobj = fil->node->cache;
    av_ref_obj(cobj);
    AV_UNLOCK(vf->mnt->avfs->lock);

    res = av_zfile_pread(fil->zfil, zc, buf, nbyte, vf->ptr);
    if(res >= 0) {
        vf->ptr += res;

        /* FIXME: should only be set when changed, ugly, UGLY, etc... */
        av_cacheobj_setsize(cobj, av_zcache_size(zc));
    }

    av_unref_obj(zc);
    av_unref_obj(cobj);

    return res;
}


static int gz_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct gzfile *fil = (struct gzfile *) vf->data;
    struct gznode *nod = fil->node;

    res = av_fgetattr(fil->base, buf, AVA_ALL & ~AVA_SIZE);
    if(res < 0)
        return res;

    buf->mode &= ~(07000);
    buf->blksize = 4096;
    buf->dev = vf->mnt->avfs->dev;
    buf->ino = nod->ino;
    buf->size = nod->size;
    buf->blocks = AV_BLOCKS(nod->size);
    buf->mtime.sec = nod->mtime;
    buf->mtime.nsec = 0;
    buf->atime = buf->mtime;
    buf->ctime = buf->mtime;
    
    return 0;
}

extern int av_init_module_ugz(struct vmodule *module);

int av_init_module_ugz(struct vmodule *module)
{
    int res;
    struct avfs *avfs;
    struct ext_info ugz_exts[3];

    ugz_exts[0].from = ".gz",  ugz_exts[0].to = NULL;
    ugz_exts[1].from = ".tgz", ugz_exts[1].to = ".tar";
    ugz_exts[2].from = NULL;

    res = av_new_avfs("ugz", ugz_exts, AV_VER, AVF_NOLOCK, module, &avfs);
    if(res < 0)
        return res;

    avfs->lookup   = gz_lookup;
    avfs->access   = gz_access;
    avfs->open     = gz_open;
    avfs->close    = gz_close; 
    avfs->read     = gz_read;
    avfs->getattr  = gz_getattr;

    av_add_avfs(avfs);

    return 0;
}

