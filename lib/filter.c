/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "filter.h"
#include "filtprog.h"
#include "filecache.h"
#include "cache.h"
#include "internal.h"
#include "oper.h"

struct filtid {
    avino_t ino;
    avdev_t dev;
};

struct filtmod {
    avsize_t size;
    avtimestruc_t mtime;
};

struct filtfile {
    avmutex lock;
    vfile *vf;
    struct sfile *sf;
    struct filtid id;
    struct filtmod mod;
    avino_t ino;
    unsigned int writers;
};

static char **filt_copy_prog(const char *prog[])
{
    int num;
    const char **curr;
    char **copyprog;
    int i;

    if(prog == NULL)
        return NULL;

    for(num = 0, curr = prog; *curr != NULL; curr++, num++);
    
    copyprog = (char **) av_malloc(sizeof(char *) * (num + 1));

    for(i = 0; i < num; i++)
        copyprog[i] = av_strdup(prog[i]);

    copyprog[i] = NULL;

    return copyprog;
}

static void filt_free_prog(char **prog)
{
    if(prog != NULL) {
        char **curr;

        for(curr = prog; *curr != NULL; curr++)
            av_free(*curr);
        
        av_free(prog);
    }
}

static int filt_lookup(ventry *ve, const char *name, void **newp)
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

static void filtfile_free(struct filtfile *ff)
{
    av_unref_obj(ff->sf);
    av_close(ff->vf);
    AV_FREELOCK(ff->lock)
}


static int filt_same_file(struct filtid *id, struct avstat *stbuf)
{
    if(id->ino == stbuf->ino && id->dev == stbuf->dev)
        return 1;
    else
        return 0;
}

static int filt_unmodif_file(struct filtmod *mod, struct avstat *stbuf)
{
    if(mod->size == stbuf->size && mod->mtime.sec == stbuf->mtime.sec &&
       mod->mtime.nsec == stbuf->mtime.nsec)
        return 1;
    else
        return 0;
}

static void filt_id_set(struct filtid *id, struct avstat *stbuf)
{
    id->ino = stbuf->ino;
    id->dev = stbuf->dev;
}

static void filt_mod_set(struct filtmod *mod, struct avstat *stbuf)
{
    mod->size = stbuf->size;
    mod->mtime = stbuf->mtime;
}


static struct filtfile *filt_newfile(ventry *ve, vfile *vf, const char *key,
                                     struct avstat *buf, int flags)
{
    struct filtfile *ff;
    struct filtdata *filtdat = (struct filtdata *) ve->mnt->avfs->data;
    struct cacheobj *cobj;

    AV_NEW_OBJ(ff, filtfile_free);
    AV_INITLOCK(ff->lock);
    ff->vf = vf;
    ff->sf = av_filtprog_new(vf, filtdat);
    filt_id_set(&ff->id, buf);
    filt_mod_set(&ff->mod, buf);
    ff->ino = av_new_ino(ve->mnt->avfs);
    ff->writers = 0;

    if((flags & AVO_TRUNC) != 0)
       av_sfile_truncate(ff->sf, 0);

    if(AV_ISWRITE(flags))
        ff->writers ++;

    cobj = av_cacheobj_new(ff, key);
    av_filecache_set(key, cobj);
    av_unref_obj(cobj);

    return ff;
}

static int filt_validate_file(struct filtfile *ff, ventry *ve, vfile *vf,
                              struct avstat *buf,  int flags)
{
    if(ff->writers == 0 && !filt_unmodif_file(&ff->mod, buf)) {
        struct filtdata *filtdat = (struct filtdata *) ve->mnt->avfs->data;
        
        av_unref_obj(ff->sf);
        av_close(ff->vf);
        ff->sf = av_filtprog_new(vf, filtdat);
        ff->vf = vf;
        filt_mod_set(&ff->mod, buf);
    }
    else if(ff->writers == 0 && AV_ISWRITE(flags)) {
        avoff_t pos = av_lseek(ff->vf, 0, AVSEEK_CUR);

        if(pos > 0)
            pos = av_lseek(vf, pos, AVSEEK_SET);
        
        if(pos < 0)
            return pos;
        
        av_filtprog_change(ff->sf, vf);
        av_close(ff->vf);
        ff->vf = vf;
    }
    else
        av_close(vf);

    if((flags & AVO_TRUNC) != 0)
       av_sfile_truncate(ff->sf, 0);

    if(AV_ISWRITE(flags))
        ff->writers ++;

    return 0;
}

static int filt_getfile(ventry *ve, vfile *vf, int flags, const char *key,
                        struct filtfile **resp)
{
    int res;
    struct filtfile *ff;
    struct cacheobj *cobj;
    struct avstat buf;
    int attrmask = AVA_INO | AVA_DEV | AVA_SIZE | AVA_MTIME;

    res = av_getattr(vf, &buf, attrmask);
    if(res < 0)
        return res;

    cobj = (struct cacheobj *) av_filecache_get(key);
    if(cobj == NULL)
        ff = NULL;
    else {
        ff = (struct filtfile *) av_cacheobj_get(cobj);
        av_unref_obj(cobj);
    }

    if(ff == NULL || !filt_same_file(&ff->id, &buf)) {
        ff = filt_newfile(ve, vf, key, &buf, flags);
        *resp = ff;
        return 0;
    }

    AV_LOCK(ff->lock);
    res = filt_validate_file(ff, ve, vf, &buf, flags);
    AV_UNLOCK(ff->lock);
    if(res < 0)
        return res;

    *resp = ff;
    return 0;
}

static int filt_baseflags(int flags)
{
    int baseflags = 0;
    int accmode = flags & AVO_ACCMODE;

    if(accmode == AVO_NOPERM)
        baseflags = AVO_RDONLY;
    else if(accmode == AVO_WRONLY)
        baseflags = AVO_RDWR;
    else
        baseflags = accmode;
        
    if((flags & AVO_CREAT) != 0)
        baseflags |= AVO_CREAT;
    if((flags & AVO_EXCL) != 0)
        baseflags |= AVO_EXCL;
    if((flags & AVO_TRUNC) != 0)
        baseflags |= AVO_TRUNC;

    return baseflags;
}


static int filt_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    struct filtfile *ff;
    vfile *vf;
    int res;
    int baseflags;
    char *key;
    int maybecreat = 0;

    if(flags & AVO_DIRECTORY)
        return -ENOTDIR;

    baseflags = filt_baseflags(flags);
    if((flags & AVO_CREAT) != 0) {
        if((flags & (AVO_EXCL | AVO_TRUNC)) == 0) {
            baseflags &= ~AVO_CREAT;
            maybecreat = 1;
        }
        else
            flags |= AVO_TRUNC;
    }
    res = av_open(ve->mnt->base, baseflags, mode, &vf);
    if(res == -ENOENT && maybecreat) {
        baseflags |= AVO_CREAT;
        flags |= AVO_TRUNC;
        res = av_open(ve->mnt->base, baseflags, mode, &vf);
    }
    if(res < 0)
        return res;

    res = av_generate_path(ve->mnt->base, &key);
    if(res == 0) {
        key = av_stradd(key, AVFS_SEP_STR, ve->mnt->avfs->name, NULL);
        res = filt_getfile(ve, vf, flags, key, &ff);
        av_free(key);
    }
    if(res < 0) {
        av_close(vf);
        return res;
    }
    
    *resp = ff;

    return 0;
}

static avssize_t filt_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct filtfile *ff = (struct filtfile *) vf->data;
    
    AV_LOCK(ff->lock);
    res = av_sfile_pread(ff->sf, buf, nbyte, vf->ptr);
    AV_UNLOCK(ff->lock);

    if(res > 0)
        vf->ptr += res;

    return res;
}

static avssize_t filt_write(vfile *vf, const char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct filtfile *ff = (struct filtfile *) vf->data;
    
    AV_LOCK(ff->lock);
    if((vf->flags & AVO_APPEND) != 0) {
        avoff_t pos;

        pos = av_sfile_size(ff->sf);
        if(pos < 0) {
            AV_UNLOCK(ff->lock);
            return pos;
        }
        
        vf->ptr = pos;
    }
    res = av_sfile_pwrite(ff->sf, buf, nbyte, vf->ptr);
    if(res >= 0)
        av_curr_time(&ff->mod.mtime);
    AV_UNLOCK(ff->lock);
        
    if(res > 0)
        vf->ptr += res;

    return res;
}

static int filt_truncate(vfile *vf, avoff_t length)
{
    int res;
    struct filtfile *ff = (struct filtfile *) vf->data;
    
    AV_LOCK(ff->lock);
    res = av_sfile_truncate(ff->sf, length);
    AV_UNLOCK(ff->lock);

    return res;
}

static int filt_close(vfile *vf)
{
    int res = 0;
    struct filtfile *ff = (struct filtfile *) vf->data;

    AV_LOCK(ff->lock);
    if(AV_ISWRITE(vf->flags)) {
        ff->writers --;

        if(ff->writers == 0) {
            res = av_sfile_flush(ff->sf);
            av_close(ff->vf);
            ff->vf = NULL;
            ff->mod.size = -1;
        }
    }
    AV_UNLOCK(ff->lock);

    av_unref_obj(ff);

    return res;
}

static int filt_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    struct filtfile *ff = (struct filtfile *) vf->data;
    struct avstat origbuf;
    int res;
    avoff_t size = -1;
    avino_t ino;
    avtimestruc_t mtime;

    AV_LOCK(ff->lock);
    ino = ff->ino;
    res = av_getattr(ff->vf, &origbuf, AVA_ALL & ~AVA_SIZE);
    if(res == 0) { 
        size = av_sfile_size(ff->sf);
        if(size < 0)
            res = size;
    }
    if(ff->writers != 0)
        mtime = ff->mod.mtime;
    else
        mtime = origbuf.mtime;
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
    buf->mtime = mtime;
    
    return 0;
}

static int filt_setattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct filtfile *ff = (struct filtfile *) vf->data;

    AV_LOCK(ff->lock);
    res = av_setattr(ff->vf, buf, attrmask);
    AV_UNLOCK(ff->lock);

    return res;    
}

static int filt_access(ventry *ve, int amode)
{
    return av_access(ve->mnt->base, amode);
}

static int filt_rename(ventry *ve, ventry *newve)
{
    return -EXDEV;
}

static int filt_unlink(ventry *ve)
{
    return av_unlink(ve->mnt->base);
}

static void filt_destroy(struct avfs *avfs)
{
    struct filtdata *filtdat = (struct filtdata *) avfs->data;

    filt_free_prog(filtdat->prog);
    filt_free_prog(filtdat->revprog);
    av_free(filtdat);
}

int av_init_filt(struct vmodule *module, const char *name,
                   const char *prog[], const char *revprog[],
                   struct ext_info *exts, struct avfs **resp)
{
    int res;
    struct avfs *avfs;
    struct filtdata *filtdat;
    

    res = av_new_avfs(name, exts, AV_VER, AVF_NOLOCK, module, &avfs);
    if(res < 0)
        return res;

    AV_NEW(filtdat);
    filtdat->prog = filt_copy_prog(prog);
    filtdat->revprog = filt_copy_prog(revprog);

    avfs->data = filtdat;

    avfs->destroy  = filt_destroy;
    avfs->lookup   = filt_lookup;
    avfs->access   = filt_access;
    avfs->unlink   = filt_unlink;
    avfs->rename   = filt_rename;  
    avfs->open     = filt_open;
    avfs->close    = filt_close; 
    avfs->read     = filt_read;
    avfs->write    = filt_write;
    avfs->getattr  = filt_getattr;
    avfs->setattr  = filt_setattr;
    avfs->truncate = filt_truncate;

    av_add_avfs(avfs);
    
    *resp = avfs;

    return 0;
}
