/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "oper.h"
#include "operutil.h"
#include "internal.h"

static int check_file_access(vfile *vf, int access)
{
    if((vf->flags & AVO_DIRECTORY) != 0)
        return -EBADF;

    access = (access + 1) & AVO_ACCMODE;
    if(((vf->flags + 1) & access) == 0)
        return -EBADF;
    
    return 0;
}

int av_file_open(vfile *vf, ventry *ve, int flags, avmode_t mode)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;

    res = av_copy_vmount(ve->mnt, &vf->mnt);
    if(res < 0)
	return res;

    if((flags & AVO_EXCL) != 0 && (flags & AVO_CREAT) == 0)
        flags &= ~AVO_EXCL;

    if((flags & AVO_TRUNC) != 0 && !AV_ISWRITE(flags)) {
        if((flags & AVO_ACCMODE) == AVO_RDONLY)
            flags = (flags & ~AVO_ACCMODE) | AVO_RDWR;
        else
            flags = (flags & ~AVO_ACCMODE) | AVO_WRONLY;
    }        

    AVFS_LOCK(avfs);
    res = avfs->open(ve, flags, (mode & 07777), &vf->data);
    AVFS_UNLOCK(avfs);
    if(res < 0) {
	av_free_vmount(vf->mnt);
        vf->mnt = NULL;
        return res;
    }

    vf->ptr = 0;
    vf->flags = flags;

    return 0;
}

int av_file_close(vfile *vf)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;

    AVFS_LOCK(avfs);
    res = avfs->close(vf);
    AVFS_UNLOCK(avfs);

    av_free_vmount(vf->mnt);
    vf->mnt = NULL;

    return res;
}

avssize_t av_file_read(vfile *vf, char *buf, avsize_t nbyte)
{
    int res;

    res = check_file_access(vf, AVO_RDONLY);
    if(res == 0) {
        struct avfs *avfs = vf->mnt->avfs;

        AVFS_LOCK(avfs);
        res = avfs->read(vf, buf, nbyte);
        AVFS_UNLOCK(avfs);
    }

    return res;
}

avssize_t av_file_pread(vfile *vf, char *buf, avsize_t nbyte, avoff_t offset)
{
    int res;
    
    res = check_file_access(vf, AVO_RDONLY);
    if(res == 0) {
        avoff_t sres;
        struct avfs *avfs = vf->mnt->avfs;

        AVFS_LOCK(avfs);
        sres = avfs->lseek(vf, offset, AVSEEK_SET);
        if(sres < 0)
            res = sres;
        else
            res = avfs->read(vf, buf, nbyte);
        AVFS_UNLOCK(avfs);
    }

    return res;
}

avssize_t av_file_write(vfile *vf, const char *buf, avsize_t nbyte)
{
    int res;

    res = check_file_access(vf, AVO_WRONLY);
    if(res == 0) {
        struct avfs *avfs = vf->mnt->avfs;
        
        AVFS_LOCK(avfs);
        res = avfs->write(vf, buf, nbyte);
        AVFS_UNLOCK(avfs);
    }
    
    return res;
}

avssize_t av_file_pwrite(vfile *vf, const char *buf, avsize_t nbyte,
                         avoff_t offset)
{
    int res;

    res = check_file_access(vf, AVO_WRONLY);
    if(res == 0) {
        avoff_t sres;
        struct avfs *avfs = vf->mnt->avfs;

        AVFS_LOCK(avfs);
        sres = avfs->lseek(vf, offset, AVSEEK_SET);
        if(sres < 0)
            res = sres;
        else
            res = avfs->write(vf, buf, nbyte);
        AVFS_UNLOCK(avfs);
    }

    return res;
}

int av_file_truncate(vfile *vf, avoff_t length)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;

    if(length < 0)
        return -EINVAL;

    res = check_file_access(vf, AVO_WRONLY);
    if(res == 0) {
        AVFS_LOCK(avfs);
        res = avfs->truncate(vf, length);
        AVFS_UNLOCK(avfs);
    }

    return res;
}

int av_file_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->getattr(vf, buf, attrmask);
    AVFS_UNLOCK(avfs);

    return res;
}

int av_file_setattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;

    AVFS_LOCK(avfs);
    res = avfs->setattr(vf, buf, attrmask);
    AVFS_UNLOCK(avfs);

    return res;
}

avoff_t av_file_lseek(vfile *vf, avoff_t offset, int whence)
{
    avoff_t res;
    struct avfs *avfs = vf->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->lseek(vf, offset, whence);
    AVFS_UNLOCK(avfs);

    return res;
}

static void file_destroy(vfile *vf)
{
    if(vf->mnt != NULL)
        av_file_close(vf);
        
    AV_FREELOCK(vf->lock);
}

int av_open(ventry *ve, int flags, avmode_t mode, vfile **resp)
{
    int res;
    vfile *vf;

    AV_NEW_OBJ(vf, file_destroy);
    AV_INITLOCK(vf->lock);
    res = av_file_open(vf, ve, flags, mode);
    if(res < 0) {
        AV_FREELOCK(vf->lock);
        av_unref_obj(vf);
    }
    else 
        *resp = vf;

    return res;
}

/* You only need to call this if you need the return value of close.
   Otherwise it is enough to unreferece the vfile
*/
int av_close(vfile *vf)
{
    int res = 0;

    if(vf != NULL) {
        res = av_file_close(vf);
        av_unref_obj(vf);
    }

    return res;
}

avssize_t av_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;

    AV_LOCK(vf->lock);
    res = av_file_read(vf, buf, nbyte);
    AV_UNLOCK(vf->lock);

    return res;
}

avssize_t av_pread(vfile *vf, char *buf, avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    AV_LOCK(vf->lock);
    res = av_file_pread(vf, buf, nbyte, offset);
    AV_UNLOCK(vf->lock);

    return res;
}

avssize_t av_write(vfile *vf, const char *buf, avsize_t nbyte)
{
    avssize_t res;

    AV_LOCK(vf->lock);
    res = av_file_write(vf, buf, nbyte);
    AV_UNLOCK(vf->lock);

    return res;
}

avssize_t av_pwrite(vfile *vf, const char *buf, avsize_t nbyte,
                      avoff_t offset)
{
    avssize_t res;

    AV_LOCK(vf->lock);
    res = av_file_pwrite(vf, buf, nbyte, offset);
    AV_UNLOCK(vf->lock);

    return res;
}

avoff_t av_lseek(vfile *vf, avoff_t offset, int whence)
{
    avoff_t res;

    AV_LOCK(vf->lock);
    res = av_file_lseek(vf, offset, whence);
    AV_UNLOCK(vf->lock);
    
    return res;
}

int av_ftruncate(vfile *vf, avoff_t length)
{
    int res;

    AV_LOCK(vf->lock);
    res = av_file_truncate(vf, length);
    AV_UNLOCK(vf->lock);

    return res;
}

int av_fgetattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    
    AV_LOCK(vf->lock);
    res = av_file_getattr(vf, buf, attrmask);
    AV_UNLOCK(vf->lock);

    return res;
}

int av_fsetattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;

    AV_LOCK(vf->lock);
    res = av_file_setattr(vf, buf, attrmask);
    AV_UNLOCK(vf->lock);

    return res;
}

int av_getattr(ventry *ve, struct avstat *buf, int attrmask, int flags)
{
    int res;
    vfile vf;

    res = av_file_open(&vf, ve, AVO_NOPERM | flags, 0);
    if(res == 0) {
        res = av_file_getattr(&vf, buf, attrmask);
        av_file_close(&vf);
    }

    return res;
}

int av_access(ventry *ve, int amode)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->access(ve, amode);
    AVFS_UNLOCK(avfs);

    return res;
}

int av_readlink(ventry *ve, char **bufp)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;

    AVFS_LOCK(avfs);
    res = avfs->readlink(ve, bufp);
    AVFS_UNLOCK(avfs);
    
    return res;
}


int av_unlink(ventry *ve)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->unlink(ve);
    AVFS_UNLOCK(avfs);

    return res;
}

int av_rmdir(ventry *ve)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->rmdir(ve);
    AVFS_UNLOCK(avfs);
    
    return res;
}

int av_mkdir(ventry *ve, avmode_t mode)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->mkdir(ve, (mode & 07777));
    AVFS_UNLOCK(avfs);
    
    return res;
}

int av_mknod(ventry *ve, avmode_t mode, avdev_t dev)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->mknod(ve, mode, dev);
    AVFS_UNLOCK(avfs);
    
    return res;
}

int av_symlink(const char *path, ventry *newve)
{
    int res;
    struct avfs *avfs = newve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->symlink(path, newve);
    AVFS_UNLOCK(avfs);
    
    return res;
}

static int compare_mount(struct vmount *mnt1, struct vmount *mnt2)
{
    int res;
    ventry tmpve;
    char *path1;
    char *path2;

    /* FIXME: checking should be done mount per mount, not with
       av_generate_path() */

    tmpve.data = NULL;
    tmpve.mnt = mnt1;
    res = av_generate_path(&tmpve, &path1);
    if(res == 0) {
        tmpve.mnt = mnt2;
        res = av_generate_path(&tmpve, &path2);
        if(res == 0) {
            if(strcmp(path1, path2) != 0) 
                res = -EXDEV;

            av_free(path2);
        }
        av_free(path1);
    }

    return res;
}

int av_rename(ventry *ve, ventry *newve)
{
    int res;

    res = compare_mount(ve->mnt, newve->mnt);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;
        
        AVFS_LOCK(avfs);
        res = avfs->rename(ve, newve);
        AVFS_UNLOCK(avfs);
    }
    
    return res;
}

int av_link(ventry *ve, ventry *newve)
{
    int res;

    res = compare_mount(ve->mnt, newve->mnt);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;
        
        AVFS_LOCK(avfs);
        res = avfs->link(ve, newve);
        AVFS_UNLOCK(avfs);
    }
    
    return res;
}

