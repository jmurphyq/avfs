/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>

static vfile **file_table;
static unsigned int file_table_size;
static AV_LOCK_DECL(files_lock);

static int file_open(vfile *vf, ventry *ve, int flags, avmode_t mode)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;

    res = __av_copy_vmount(ve->mnt, &vf->mnt);
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
    res = avfs->open(ve, flags, mode, &vf->data);
    AVFS_UNLOCK(avfs);
    if(res < 0) {
	__av_free_vmount(vf->mnt);
        vf->mnt = NULL;
        return res;
    }

    vf->ptr = 0;
    vf->flags = flags;

    return 0;
}

static int file_close(vfile *vf)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;

    AVFS_LOCK(avfs);
    res = avfs->close(vf);
    AVFS_UNLOCK(avfs);

    __av_free_vmount(vf->mnt);
    vf->mnt = NULL;

    return res;
}

static int check_file_access(vfile *vf, int access)
{
    if((vf->flags & AVO_DIRECTORY) != 0)
        return -EBADF;

    access = (access + 1) & AVO_ACCMODE;
    if(((vf->flags + 1) & access) == 0)
        return -EBADF;
    
    return 0;
}

static int file_truncate(vfile *vf, avoff_t length)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;

    res = check_file_access(vf, AVO_WRONLY);
    if(res < 0)
        return res;
    
    AVFS_LOCK(avfs);
    res = avfs->truncate(vf, length);
    AVFS_UNLOCK(avfs);

    return res;
}

static int file_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->getattr(vf, buf, attrmask);
    AVFS_UNLOCK(avfs);

    return res;
}

static int file_setattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct avfs *avfs = vf->mnt->avfs;

    AVFS_LOCK(avfs);
    res = avfs->setattr(vf, buf, attrmask);
    AVFS_UNLOCK(avfs);

    return res;
}

static avoff_t file_lseek(vfile *vf, avoff_t offset, int whence)
{
    avoff_t res;
    struct avfs *avfs = vf->mnt->avfs;
    
    AVFS_LOCK(avfs);
    res = avfs->lseek(vf, offset, whence);
    AVFS_UNLOCK(avfs);

    return res;
}

static void av_close(vfile *vf)
{
    if(vf->mnt != NULL)
        file_close(vf);
        
    AV_FREELOCK(vf->lock);
}

int __av_open(ventry *ve, int flags, avmode_t mode, vfile **resp)
{
    int res;
    vfile *vf;

    AV_NEW_OBJ(vf, av_close);
    AV_INITLOCK(vf->lock);
    res = file_open(vf, ve, flags, mode);
    if(res < 0) {
        AV_FREELOCK(vf->lock);
        __av_unref_obj(vf);
    }
    else 
        *resp = vf;

    return res;
}

/* You only need to call this if you need the return value of close.
   Otherwise it is enough to unreferece the vfile
*/
int __av_close(vfile *vf)
{
    int res = 0;

    if(vf != NULL) {
        res = file_close(vf);
        __av_unref_obj(vf);
    }

    return res;
}

avssize_t __av_pread(vfile *vf, char *buf, avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    AV_LOCK(vf->lock);
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
    AV_UNLOCK(vf->lock);

    return res;
}

avssize_t __av_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;

    AV_LOCK(vf->lock);
    res = check_file_access(vf, AVO_RDONLY);
    if(res == 0) {
        struct avfs *avfs = vf->mnt->avfs;

        AVFS_LOCK(avfs);
        res = avfs->read(vf, buf, nbyte);
        AVFS_UNLOCK(avfs);
    }
    AV_UNLOCK(vf->lock);

    return res;
}

avoff_t __av_lseek(vfile *vf, avoff_t offset, int whence)
{
    avoff_t res;

    AV_LOCK(vf->lock);
    res = file_lseek(vf, offset, whence);
    AV_UNLOCK(vf->lock);
    
    return res;
}

avssize_t __av_pwrite(vfile *vf, const char *buf, avsize_t nbyte,
                      avoff_t offset)
{
    avssize_t res;

    AV_LOCK(vf->lock);
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
    AV_UNLOCK(vf->lock);

    return res;
}

avssize_t __av_write(vfile *vf, const char *buf, avsize_t nbyte)
{
    avssize_t res;

    AV_LOCK(vf->lock);
    res = check_file_access(vf, AVO_WRONLY);
    if(res == 0) {
        struct avfs *avfs = vf->mnt->avfs;

        AVFS_LOCK(avfs);
        res = avfs->write(vf, buf, nbyte);
        AVFS_UNLOCK(avfs);
    }
    AV_UNLOCK(vf->lock);

    return res;
}

int __av_truncate(vfile *vf, avoff_t length)
{
    int res;

    AV_LOCK(vf->lock);
    res = file_truncate(vf, length);
    AV_UNLOCK(vf->lock);

    return res;
}

int __av_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    
    AV_LOCK(vf->lock);
    res = file_getattr(vf, buf, attrmask);
    AV_UNLOCK(vf->lock);

    return res;
}

int __av_setattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;

    AV_LOCK(vf->lock);
    res = file_setattr(vf, buf, attrmask);
    AV_UNLOCK(vf->lock);

    return res;
}

static int find_unused()
{
    int i;
    int newsize;

    for(i = 0; i < file_table_size; i++)
	if(file_table[i] == NULL)
	    return i;

    newsize = file_table_size + 16;
    file_table = __av_realloc(file_table, sizeof(*file_table) * newsize);
    for(i = file_table_size; i < newsize; i++)
	file_table[i] = NULL;
    
    i = file_table_size;
    file_table_size = newsize;
    
    return i;
}


static void put_file(vfile *vf)
{
    AV_UNLOCK(vf->lock);

    __av_unref_obj(vf);

}

static int get_file(int fd, vfile **resp)
{
    vfile *vf = NULL;

    AV_LOCK(files_lock);
    if(fd >= 0 && fd < file_table_size) {
        vf = file_table[fd];
        if(vf != NULL)
            __av_ref_obj(vf);
    }
    AV_UNLOCK(files_lock);

    if(vf == NULL)
        return -EBADF;

    AV_LOCK(vf->lock);
    if(vf->mnt == NULL) {
        put_file(vf);
        return -EBADF;
    }
    
    *resp = vf;
    
    return 0;
}

static int open_path(vfile *vf, const char *path, int flags, avmode_t mode)
{
    int res;
    ventry *ve;

    res = __av_get_ventry(path, !(flags & AVO_NOFOLLOW), &ve);
    if(res < 0)
        return res;

    res = file_open(vf, ve, flags, mode);
    __av_free_ventry(ve);

    return res;
}

static void free_vfile(vfile *vf)
{
    AV_FREELOCK(vf->lock);
}

static int common_open(const char *path, int flags, avmode_t mode)
{
    int res;
    int fd;
    vfile *vf;

    AV_NEW_OBJ(vf, free_vfile);
    AV_INITLOCK(vf->lock);
    res = open_path(vf, path, flags, mode);
    if(res < 0) {
        __av_unref_obj(vf);
        errno = -res;
        return -1;
    }
    else {
	AV_LOCK(files_lock);
        fd = find_unused();
	file_table[fd] = vf;
	AV_UNLOCK(files_lock);
    }

    return fd;
}

static int oflags_to_avfs(int flags)
{
    int avflags;
  
    avflags = flags & O_ACCMODE;
    if(avflags == AVO_NOPERM)
	avflags = AVO_RDWR;

    if(flags & O_CREAT)    avflags |= AVO_CREAT;
    if(flags & O_EXCL)     avflags |= AVO_EXCL;
    if(flags & O_TRUNC)    avflags |= AVO_TRUNC;
    if(flags & O_APPEND)   avflags |= AVO_APPEND;
    if(flags & O_NONBLOCK) avflags |= AVO_NONBLOCK;
    if(flags & O_SYNC)     avflags |= AVO_SYNC;

    return avflags;
}

int virt_open(const char *path, int flags, mode_t mode)
{
    return common_open(path, oflags_to_avfs(flags), mode & 07777);
}

int virt_close(int fd)
{
    int res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = file_close(vf);
        put_file(vf);

        AV_LOCK(files_lock);
        file_table[fd] = NULL;
        AV_UNLOCK(files_lock);

        __av_unref_obj(vf);

    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

static void avstat_to_stat(struct stat *buf, struct avstat *avbuf)
{
    buf->st_dev     = avbuf->dev;
    buf->st_ino     = avbuf->ino;
    buf->st_mode    = avbuf->mode;
    buf->st_nlink   = avbuf->nlink;
    buf->st_uid     = avbuf->uid;
    buf->st_gid     = avbuf->gid;
    buf->st_rdev    = avbuf->rdev;
    buf->st_size    = avbuf->size;
    buf->st_blksize = avbuf->blksize;
    buf->st_blocks  = avbuf->blocks;
    buf->st_atime   = avbuf->atime.sec;
    buf->st_mtime   = avbuf->mtime.sec;
    buf->st_ctime   = avbuf->ctime.sec;
}

static int common_stat(const char *path, struct stat *buf, int flags)
{
    int res;
    vfile vf;
    struct avstat avbuf;

    res = open_path(&vf, path, AVO_NOPERM | flags, 0);
    if(res == 0) {
        res = file_getattr(&vf, &avbuf, AVA_ALL);
        file_close(&vf);
	if(res == 0)
	    avstat_to_stat(buf, &avbuf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }
    
    return 0;
}

int virt_stat(const char *path, struct stat *buf)
{
    return common_stat(path, buf, 0); 
}

int virt_lstat(const char *path, struct stat *buf)
{
    return common_stat(path, buf, AVO_NOFOLLOW); 
}

int virt_truncate(const char *path, off_t length)
{
    int res;
    vfile vf;

    res = open_path(&vf, path, AVO_WRONLY, 0);
    if(res == 0) {
        struct avfs *avfs = vf.mnt->avfs;        
        
        AVFS_LOCK(avfs);
	res = avfs->truncate(&vf, length);
        AVFS_UNLOCK(avfs);
        file_close(&vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return 0;
}

static int common_setattr(const char *path, struct avstat *buf, int attrmask,
			  int flags)
{
    int res;
    vfile vf;

    res = open_path(&vf, path, AVO_NOPERM | flags, 0);
    if(res == 0) {
        res = file_setattr(&vf, buf, attrmask);
	file_close(&vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return 0;
}

int virt_utime(const char *path, struct utimbuf *buf)
{
    struct avstat stbuf;

    if(buf == NULL) {
	__av_curr_time(&stbuf.mtime);
	stbuf.atime = stbuf.mtime;
    }
    else {
	stbuf.mtime.sec = buf->modtime;
	stbuf.mtime.nsec = 0;
	stbuf.atime.sec = buf->actime;
	stbuf.atime.nsec = 0;
    }
    
    return common_setattr(path, &stbuf, AVA_MTIME | AVA_ATIME, 0);
}

int virt_chmod(const char *path, mode_t mode)
{
    struct avstat stbuf;

    stbuf.mode = mode & 07777;

    return common_setattr(path, &stbuf, AVA_MODE, 0);
}

static int common_chown(const char *path, uid_t owner, gid_t grp, int flags)
{
    struct avstat stbuf;
    int attrmask = 0;
    
    stbuf.uid = owner;
    stbuf.gid = grp;

    if(owner != (uid_t) -1)
	attrmask |= AVA_UID;
    if(grp != (gid_t) -1)
	attrmask |= AVA_GID;

    return common_setattr(path, &stbuf, attrmask, flags);
}

int virt_chown(const char *path, uid_t owner, gid_t grp)
{
    return common_chown(path, owner, grp, 0);
}

int virt_lchown(const char *path, uid_t owner, gid_t grp)
{
    return common_chown(path, owner, grp, AVO_NOFOLLOW);
}


ssize_t virt_read(int fd, void *buf, size_t nbyte)
{
    ssize_t res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = check_file_access(vf, AVO_RDONLY);
        if(res == 0) {
            struct avfs *avfs = vf->mnt->avfs;
            
            AVFS_LOCK(avfs);
            res = avfs->read(vf, buf, nbyte);
            AVFS_UNLOCK(avfs);
        }
        put_file(vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return res;
}

ssize_t virt_write(int fd, const void *buf, size_t nbyte)
{
    ssize_t res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = check_file_access(vf, AVO_WRONLY);
        if(res == 0) {
            struct avfs *avfs = vf->mnt->avfs;
            
            AVFS_LOCK(avfs);
            res = avfs->write(vf, buf, nbyte);
            AVFS_UNLOCK(avfs);
        }
	put_file(vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return res;
}

off_t virt_lseek(int fd, off_t offset, int whence)
{
    off_t res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = file_lseek(vf, offset, whence);
	put_file(vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return res;
}

int virt_ftruncate(int fd, off_t length)
{
    int res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
	res = file_truncate(vf, length);
	put_file(vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return 0;
}

int virt_fstat(int fd, struct stat *buf)
{
    int res;
    struct avstat avbuf;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = file_getattr(vf, &avbuf, AVA_ALL);
	put_file(vf);
	if(res == 0)
	    avstat_to_stat(buf, &avbuf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return 0;
}

static int common_fsetattr(int fd, struct avstat *stbuf, int attrmask)
{
    int res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
	res = file_setattr(vf, stbuf, attrmask);
	put_file(vf);
    }
    if(res < 0) {
        errno = -res;
        return -1;
    }

    return 0;
}

int virt_fchmod(int fd, mode_t mode)
{
    struct avstat stbuf;
    
    stbuf.mode = mode & 07777;

    return common_fsetattr(fd, &stbuf, AVA_MODE);
}

int virt_fchown(int fd, uid_t owner, gid_t grp)
{
    struct avstat stbuf;
    int attrmask = 0;
        
    stbuf.uid = owner;
    stbuf.gid = grp;

    if(owner != (uid_t) -1)
	attrmask |= AVA_UID;
    if(grp != (gid_t) -1)
	attrmask |= AVA_GID;

    return common_fsetattr(fd, &stbuf, attrmask);
}

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

typedef struct {
    int fd;
    struct dirent entry;
    char _trail[NAME_MAX + 1];
} AVDIR;

DIR *virt_opendir(const char *path)
{
    AVDIR *dp;
    int fd;

    fd = common_open(path, AVO_DIRECTORY, 0);
    if(fd == -1)
	return NULL;

    AV_NEW(dp);
    dp->fd = fd;

    return (DIR *) dp;
}

int virt_closedir(DIR *dirp)
{
    int res;
    AVDIR *dp = (AVDIR *) dirp;
    
    if(dp == NULL) {
	errno = EINVAL;
	return -1;
    }
    
    res = virt_close(dp->fd);
    __av_free(dp);

    return res;
}

#define AVFS_DIR_RECLEN 256 /* just an arbitary number */

static void avdirent_to_dirent(struct dirent *ent, struct avdirent *avent,
			       avoff_t n)
{
    ent->d_ino = avent->ino;
    ent->d_off = n * AVFS_DIR_RECLEN; 
    ent->d_reclen = AVFS_DIR_RECLEN;
#ifdef HAVE_D_TYPE
    ent->d_type = avent->type;
#endif
    strncpy(ent->d_name, avent->name, NAME_MAX);
    ent->d_name[NAME_MAX] = '\0';
}

struct dirent *virt_readdir(DIR *dirp)
{
    int res;
    struct avdirent buf;
    avoff_t n;
    AVDIR *dp = (AVDIR *) dirp;
    vfile *vf;

    if(dp == NULL) {
	errno = EINVAL;
	return NULL;
    }

    res = get_file(dp->fd, &vf);
    if(res == 0) {
        struct avfs *avfs = vf->mnt->avfs;
	n = vf->ptr;
        AVFS_LOCK(avfs);
	res = avfs->readdir(vf, &buf);
        AVFS_UNLOCK(avfs);
	if(res > 0) {
	    avdirent_to_dirent(&dp->entry, &buf, n);
	    __av_free(buf.name);
	}
	put_file(vf);
    }
    if(res < 0) {
        errno = -res;
        return NULL;
    }
    if(res == 0)
        return NULL;

    return &dp->entry;
}

void virt_rewinddir(DIR *dirp)
{
    int res;
    AVDIR *dp = (AVDIR *) dirp;
    vfile *vf;

    if(dp == NULL) {
	errno = EINVAL;
	return;
    }

    res = get_file(dp->fd, &vf);
    if(res == 0) {
	vf->ptr = 0;
	put_file(vf);
    }
    else
        errno = -res;
}


void __av_close_all_files()
{
    int fd;
    vfile *vf;
    
    AV_LOCK(files_lock);
    for(fd = 0; fd < file_table_size; fd++) {
        vf = file_table[fd];
        if(vf != NULL) {
            file_close(vf);
            __av_unref_obj(vf);
        }
    }
    __av_free(file_table);
    file_table = NULL;
    AV_UNLOCK(files_lock);
}
