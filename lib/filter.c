/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "filter.h"
#include "filtprog.h"
#include "filecache.h"

struct filtsig {
    avino_t ino;
    avdev_t dev;
    avsize_t size;
    avtimestruc_t mtime;
};

struct filtfile {
    avmutex lock;
    vfile *vf;
    struct sfile *sf;
    struct filtsig sig;
    avino_t ino;
};

static char **filt_copy_prog(const char *prog[])
{
    int num;
    const char **curr;
    char **copyprog;
    int i;

    for(num = 0, curr = prog; *curr != NULL; curr++, num++);
    
    copyprog = (char **) __av_malloc(sizeof(char *) * (num + 1));

    for(i = 0; i < num; i++)
        copyprog[i] = __av_strdup(prog[i]);

    copyprog[i] = NULL;

    return copyprog;
}

static int filt_lookup(ventry *ve, const char *name, void **newp)
{
    char *path = (char *) ve->data;
    
    if(path == NULL) {
        if(name[0] != '\0')
            return -ENOENT;
        path = __av_strdup(name);
    }
    else if(name == NULL) {
        __av_free(path);
        path = NULL;
    }
    else 
        return -ENOENT;
    
    *newp = path;

    return 0;
}

static void filtfile_free(struct filtfile *ff)
{
    __av_unref_obj(ff->sf);
    __av_close(ff->vf);
    AV_FREELOCK(ff->lock)
}

static int filt_sig_equal(struct filtsig *sig, struct avstat *stbuf)
{
    if(sig->ino != stbuf->ino || sig->dev != stbuf->dev ||
       sig->size != stbuf->size || sig->mtime.sec != stbuf->mtime.sec ||
       sig->mtime.nsec != stbuf->mtime.nsec)
        return 0;
    else
        return 1;
}

static void filt_sig_set(struct filtsig *sig, struct avstat *stbuf)
{
    sig->ino = stbuf->ino;
    sig->dev = stbuf->dev;
    sig->size = stbuf->size;
    sig->mtime = stbuf->mtime;
}

static int filt_getfile(ventry *ve, struct filtfile **resp)
{
    int res;
    vfile *vf;
    struct filtfile *ff;
    struct avstat buf;
    int attrmask = AVA_INO | AVA_DEV | AVA_SIZE | AVA_MTIME;

    res = __av_open(ve->mnt->base, AVO_RDONLY, 0, &vf);
    if(res < 0)
        return res;

    res = __av_getattr(vf, &buf, attrmask);
    if(res < 0) {
        __av_close(vf);
        return res;
    }

    ff = (struct filtfile *) __av_filecache_get(ve->mnt->avfs, ve->mnt->base);
    
    if(ff != NULL && filt_sig_equal(&ff->sig, &buf)) {
        __av_close(vf);
        *resp = ff;
        return 0;
    }
    __av_filecache_del(ff);

    AV_NEW_OBJ(ff, filtfile_free);
    AV_INITLOCK(ff->lock);
    ff->vf = vf;
    ff->sf = __av_filtprog_new(vf, (char **) ve->mnt->avfs->data);
    filt_sig_set(&ff->sig, &buf);
    ff->ino = __av_new_ino(ve->mnt->avfs);

    __av_filecache_set(ve->mnt->avfs, ve->mnt->base, ff);

    *resp = ff;
    return 0;
}

static int filt_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    struct filtfile *ff;
    int res;

    if(flags & AVO_DIRECTORY)
        return -ENOTDIR;

    if(AV_ISWRITE(flags))
        return -EPERM;
    
    res = filt_getfile(ve, &ff);
    if(res < 0)
        return res;
    
    *resp = ff;

    return 0;
}

static int filt_close(vfile *vf)
{
    struct filtfile *ff = (struct filtfile *) vf->data;

    __av_unref_obj(ff);

    return 0;
}

static avssize_t filt_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct filtfile *ff = (struct filtfile *) vf->data;
    
    AV_LOCK(ff->lock);
    res = __av_sfile_pread(ff->sf, buf, nbyte, vf->ptr);
    AV_UNLOCK(ff->lock);

    if(res > 0)
        vf->ptr += res;

    return res;
}

static int filt_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    struct filtfile *ff = (struct filtfile *) vf->data;
    struct avstat origbuf;
    int res;
    avoff_t size = -1;
    avino_t ino;

    AV_LOCK(ff->lock);
    ino = ff->ino;
    res = __av_getattr(ff->vf, &origbuf, AVA_ALL & ~AVA_SIZE);
    if(res == 0) { 
        size = __av_sfile_size(ff->sf);
        if(size < 0)
            res = size;
    }
    AV_UNLOCK(ff->lock);

    if(res < 0)
        return res;

    *buf = origbuf;
    buf->mode &= ~(07000);
    buf->blksize = 4096;
    buf->dev = vf->mnt->avfs->dev;
    buf->ino = ino;
    buf->size = size;
    buf->blocks = AV_BLOCKS(size);
    
    return 0;
}

static int filt_access(ventry *ve, int amode)
{
    return __av_access(ve->mnt->base, amode);
}


static void filt_destroy(struct avfs *avfs)
{
    char **prog = (char **) avfs->data;
    char **curr;

    __av_filecache_freeall(avfs);
    for(curr = prog; *curr != NULL; curr++)
        __av_free(*curr);

    __av_free(prog);
}

int __av_init_filt(struct vmodule *module, const char *name,
                   const char *prog[], struct ext_info *exts,
                   struct avfs **resp)
{
    int res;
    struct avfs *avfs;
    char **progcopy;

    res = __av_new_avfs(name, exts, AV_VER, AVF_NOLOCK, module, &avfs);
    if(res < 0)
        return res;

    progcopy = filt_copy_prog(prog);

    avfs->data = progcopy;

    avfs->destroy  = filt_destroy;
    avfs->lookup   = filt_lookup;
    avfs->open     = filt_open;
    avfs->close    = filt_close; 
    avfs->read     = filt_read;
    avfs->getattr  = filt_getattr;
    avfs->access   = filt_access;

    __av_add_avfs(avfs);
    
    *resp = avfs;

    return 0;
}
