/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "internal.h"

#include <sys/types.h>

int __av_access(ventry *ve, int amode)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->access(ve, amode);
    AVFS_UNLOCK(avfs);

    return res;
}

int virt_access(const char *path, int amode)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(path, 1, &ve);
    if(res == 0) {
	res = __av_access(ve, amode);
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return 0;
}

static int copy_readlink(char *buf, size_t bsiz, const char *avbuf)
{
    size_t nact;

    nact = strlen(avbuf);
    nact = AV_MIN(nact, bsiz);

    strncpy(buf, avbuf, nact);

    return (int) nact;
}

int virt_readlink(const char *path, char *buf, size_t bsiz)
{
    int res;
    ventry *ve;
    char *avbuf;
   
    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;

        AVFS_LOCK(avfs);
	res = avfs->readlink(ve, &avbuf);
        AVFS_UNLOCK(avfs);
	if(res == 0) {
	    res = copy_readlink(buf, bsiz, avbuf);
	    __av_free(avbuf);
	}
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return res;
}

int __av_unlink(ventry *ve)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->unlink(ve);
    AVFS_UNLOCK(avfs);

    return res;
}

int virt_unlink(const char *path)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
        __av_unlink(ve);
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

int virt_rmdir(const char *path)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;

        AVFS_LOCK(avfs);
	res = avfs->rmdir(ve);
        AVFS_UNLOCK(avfs);
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

int virt_mkdir(const char *path, mode_t mode)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;

        AVFS_LOCK(avfs);
	res = avfs->mkdir(ve, mode);
        AVFS_UNLOCK(avfs);
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

int virt_mknod(const char *path, mode_t mode, dev_t dev)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;

        AVFS_LOCK(avfs);
	res = avfs->mknod(ve, mode, dev);
        AVFS_UNLOCK(avfs);
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

int virt_symlink(const char *path, const char *newpath)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(newpath, 0, &ve);
    if(res == 0) {
        struct avfs *avfs = ve->mnt->avfs;

        AVFS_LOCK(avfs);
	res = avfs->symlink(path, ve);
        AVFS_UNLOCK(avfs);
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

static int compare_mount(struct vmount *mnt1, struct vmount *mnt2)
{
    int res;
    ventry tmpve;
    char *path1;
    char *path2;

    /* FIXME: checking should be done mount per mount, not with
       __av_generate_path() */

    tmpve.data = NULL;
    tmpve.mnt = mnt1;
    res = __av_generate_path(&tmpve, &path1);
    if(res == 0) {
        tmpve.mnt = mnt2;
        res = __av_generate_path(&tmpve, &path2);
        if(res == 0) {
            if(strcmp(path1, path2) != 0) 
                res = -EXDEV;

            __av_free(path2);
        }
        __av_free(path1);
    }

    return res;
}


int virt_rename(const char *path, const char *newpath)
{
    int res;
    ventry *ve;
    ventry *newve;

    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
	res = __av_get_ventry(newpath, 0, &newve);
	if(res == 0) {
            res = compare_mount(ve->mnt, newve->mnt);
            if(res == 0) {
                struct avfs *avfs = ve->mnt->avfs;

                AVFS_LOCK(avfs);
                res = avfs->rename(ve, newve);
                AVFS_UNLOCK(avfs);
            }
	    __av_free_ventry(newve);
	}
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

int virt_link(const char *path, const char *newpath)
{
    int res;
    ventry *ve;
    ventry *newve;

    res = __av_get_ventry(path, 0, &ve);
    if(res == 0) {
	res = __av_get_ventry(newpath, 0, &newve);
	if(res == 0) {
            res = compare_mount(ve->mnt, newve->mnt);
            if(res == 0) {
                struct avfs *avfs = ve->mnt->avfs;

                AVFS_LOCK(avfs);
                res = avfs->link(ve, newve);
                AVFS_UNLOCK(avfs);
            }
	    __av_free_ventry(newve);
	}
	__av_free_ventry(ve);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}
