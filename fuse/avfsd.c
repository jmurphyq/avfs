/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/


#include <fuse.h>
#include <virtual.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

struct fuse *fuse;

static int fdcache;
static char *pathcache;


static int avfsd_getattr(const char *path, struct stat *stbuf)
{
    int res;

    res = virt_lstat(path, stbuf);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_readlink(const char *path, char *buf, size_t size)
{
    int res;

    res = virt_readlink(path, buf, size - 1);
    if(res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int avfsd_getdir(const char *path, fuse_dirh_t h, fuse_dirfil_t filler)
{
    DIR *dp;
    struct dirent *de;
    int res = 0;

    dp = virt_opendir(path);
    if(dp == NULL)
        return -errno;

    while((de = virt_readdir(dp)) != NULL) {
        res = filler(h, de->d_name, de->d_type);
        if(res != 0)
            break;
    }

    virt_closedir(dp);
    return res;
}

static int avfsd_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    res = virt_mknod(path, mode, rdev);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_mkdir(const char *path, mode_t mode)
{
    int res;

    res = virt_mkdir(path, mode);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_unlink(const char *path)
{
    int res;

    res = virt_unlink(path);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_rmdir(const char *path)
{
    int res;

    res = virt_rmdir(path);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_symlink(const char *from, const char *to)
{
    int res;

    res = virt_symlink(from, to);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_rename(const char *from, const char *to)
{
    int res;

    res = virt_rename(from, to);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_link(const char *from, const char *to)
{
    int res;

    res = virt_link(from, to);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_chmod(const char *path, mode_t mode)
{
    int res;

    res = virt_chmod(path, mode);
    if(res == -1)
        return -errno;
    
    return 0;
}

static int avfsd_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

    res = virt_lchown(path, uid, gid);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_truncate(const char *path, off_t size)
{
    int res;
    
    res = virt_truncate(path, size);
    if(res == -1)
        return -errno;

    return 0;
}

static int avfsd_utime(const char *path, struct utimbuf *buf)
{
    int res;
    
    res = virt_utime(path, buf);
    if(res == -1)
        return -errno;

    return 0;
}


static int avfsd_open(const char *path, int flags)
{
    int res;

    res = virt_open(path, flags, 0);
    if(res == -1) 
        return -errno;

    virt_close(res);
    return 0;
}

static void pathcache_close()
{
    virt_close(fdcache);
    free(pathcache);
    pathcache = NULL;
}

static int avfsd_read(const char *path, char *buf, size_t size, off_t offset)
{
    int res;

    if(pathcache == NULL || strcmp(pathcache, path) != 0) {
        if(pathcache != NULL)
            pathcache_close();
        
        fdcache = virt_open(path, O_RDONLY, 0);
        if(fdcache == -1)
            return -errno;
        
        pathcache = strdup(path);
    }

    if(virt_lseek(fdcache, offset, SEEK_SET) == -1)
        res = -errno;
    else {
        res = virt_read(fdcache, buf, size);
        if(res == -1)
            res = -errno;
    }

    if(res < 0)
        pathcache_close();
    
    return res;
}

static int avfsd_write(const char *path, const char *buf, size_t size,
                     off_t offset)
{
    int fd;
    int res;

    fd = virt_open(path, O_WRONLY, 0);
    if(fd == -1)
        return -errno;

    if(virt_lseek(fd, offset, SEEK_SET) == -1)
        res = -errno;
    else {
        res = virt_write(fd, buf, size);
        if(res == -1)
            res = -errno;
    }
        
    virt_close(fd);
    return res;
}

static struct fuse_operations avfsd_oper = {
    getattr:	avfsd_getattr,
    readlink:	avfsd_readlink,
    getdir:     avfsd_getdir,
    mknod:	avfsd_mknod,
    mkdir:	avfsd_mkdir,
    symlink:	avfsd_symlink,
    unlink:	avfsd_unlink,
    rmdir:	avfsd_rmdir,
    rename:     avfsd_rename,
    link:	avfsd_link,
    chmod:	avfsd_chmod,
    chown:	avfsd_chown,
    truncate:	avfsd_truncate,
    utime:	avfsd_utime,
    open:	avfsd_open,
    read:	avfsd_read,
    write:	avfsd_write,
    statfs:     NULL,
};

static void exit_handler()
{
    fuse_exit(fuse);
}

static void set_signal_handlers()
{
    struct sigaction sa;

    sa.sa_handler = exit_handler;
    sigemptyset(&(sa.sa_mask));
    sa.sa_flags = 0;

    if (sigaction(SIGHUP, &sa, NULL) == -1 || 
	sigaction(SIGINT, &sa, NULL) == -1 || 
	sigaction(SIGTERM, &sa, NULL) == -1) {
	
	perror("Cannot set exit signal handlers");
        exit(1);
    }

    sa.sa_handler = SIG_IGN;
    
    if(sigaction(SIGPIPE, &sa, NULL) == -1) {
	perror("Cannot set ignored signals");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    int flags = 0;
    int fd;
    char *mountpoint;

    mountpoint = argv[1];
    fd = fuse_mount(mountpoint, NULL);
    if(fd == -1)
        exit(1);

    set_signal_handlers();

    if(argc > 2 && strcmp(argv[2], "-d") == 0)
        flags |= FUSE_DEBUG;

    fuse = fuse_new(fd, flags, &avfsd_oper);
    fuse_loop(fuse);

    close(fd);
    fuse_unmount(mountpoint);
    
    fuse_destroy(fuse);
    pathcache_close();

    return 0;
}


