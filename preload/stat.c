#include "utils.h"
#include <sys/stat.h>

static int real_stat64(const char *path, struct stat64 *buf, int deref,
                       int undersc)
{
    if(!deref) {
        if(undersc == 0) {
            static int (*prev)(const char *, struct stat64 *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat64 *))
                    __av_get_real("lstat64");
            
            return prev(path, buf);
        }
        else {
            static int (*prev)(const char *, struct stat64 *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat64 *))
                    __av_get_real("_lstat64");
            
            return prev(path, buf);
        }
    }
    else {
        if(undersc == 0) {
            static int (*prev)(const char *, struct stat64 *);
        
            if(!prev) 
                prev = (int (*)(const char *, struct stat64 *)) 
                    __av_get_real("stat64");
            
            return prev(path, buf);
        }
        else {
            static int (*prev)(const char *, struct stat64 *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat64 *)) 
                    __av_get_real("_stat64");
            
            return prev(path, buf);
        }
    }
}

static int real_fstat64(int fd, struct stat64 *buf, int undersc)
{
    if(undersc == 0) {
        static int (*prev)(int, struct stat64 *);
        
        if(!prev)
            prev = (int (*)(int, struct stat64 *)) __av_get_real("fstat64");
        
        return prev(fd, buf);
    }
    else {
        static int (*prev)(int, struct stat64 *);
        
        if(!prev)
            prev = (int (*)(int, struct stat64 *)) __av_get_real("_fstat64");
        
        return prev(fd, buf);        
    }
}

static int real_stat(const char *path, struct stat *buf, int deref,
                     int undersc)
{
    if(!deref) {
        if(undersc == 0) {
            static int (*prev)(const char *, struct stat *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat *))
                    __av_get_real("lstat");
            
            return prev(path, buf);
        }
        else {
            static int (*prev)(const char *, struct stat *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat *))
                    __av_get_real("_lstat");
            
            return prev(path, buf);
        }
    }
    else {
        if(undersc == 0) {
            static int (*prev)(const char *, struct stat *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat *))
                    __av_get_real("stat");
            
            return prev(path, buf);
        }
        else {
            static int (*prev)(const char *, struct stat *);
            
            if(!prev) 
                prev = (int (*)(const char *, struct stat *))
                    __av_get_real("_stat");
            
            return prev(path, buf);
        }
    }
}

static int real_fstat(int fd, struct stat *buf, int undersc)
{
    if(undersc == 0) {
        static int (*prev)(int, struct stat *);
        
        if(!prev)
            prev = (int (*)(int, struct stat *)) __av_get_real("fstat");
        
        return prev(fd, buf);
    }
    else {
        static int (*prev)(int, struct stat *);
        
        if(!prev)
            prev = (int (*)(int, struct stat *)) __av_get_real("_fstat");
        
        return prev(fd, buf);        
    }
}

static int cmd_stat(const char *path, struct avstat *buf, int deref,
                    char *pathbuf)
{
    int res;
    struct avfs_out_message outmsg;
    struct avfs_in_message inmsg;
    struct avfs_cmd cmd;
    struct avfs_result result;
    const char *abspath;

    res = __av_get_abs_path(path, pathbuf, &abspath);
    if(res < 0)
        return res;
    
    cmd.type = CMD_GETATTR;
    if(deref)
        cmd.u.getattr.flags = 0;
    else
        cmd.u.getattr.flags = AVO_NOFOLLOW;
    cmd.u.getattr.attrmask = AVA_ALL;
    
    outmsg.num = 2;
    outmsg.seg[0].len = sizeof(cmd);
    outmsg.seg[0].buf = &cmd;
    outmsg.seg[1].len = strlen(abspath) + 1;
    outmsg.seg[1].buf = abspath;

    inmsg.seg[0].buf = &result;
    inmsg.seg[1].buf = pathbuf;
    inmsg.seg[2].buf = buf;

    res = __av_send_message(&outmsg, &inmsg, 0);
    if(res == -1)
        return -EIO;

    if(inmsg.seg[1].len == 0)
        pathbuf[0] = '\0';

    return result.result;
}

static int fstat_server(int serverfh, struct avstat *buf)
{
    int res;
    struct avfs_out_message outmsg;
    struct avfs_in_message inmsg;
    struct avfs_cmd cmd;
    struct avfs_result result;

    cmd.type = CMD_FSTAT;
    cmd.u.fdops.serverfh = serverfh;
    
    outmsg.num = 1;
    outmsg.seg[0].len = sizeof(cmd);
    outmsg.seg[0].buf = &cmd;

    inmsg.seg[0].buf = &result;
    inmsg.seg[1].buf = buf;

    res = __av_send_message(&outmsg, &inmsg, 0);
    if(res == -1)
        return -EIO;

    return result.result;
}

static void convert_stat64(struct avstat *vbuf, struct stat64 *lbuf)
{
    memset((void *) lbuf, 0, sizeof(*lbuf));
  
    lbuf->st_dev      = vbuf->dev;
    lbuf->st_ino      = vbuf->ino;
    lbuf->st_mode     = vbuf->mode;
    lbuf->st_nlink    = vbuf->nlink;
    lbuf->st_uid      = vbuf->uid;
    lbuf->st_gid      = vbuf->gid;
    lbuf->st_rdev     = vbuf->rdev;
    lbuf->st_size     = vbuf->size;
    lbuf->st_blksize  = vbuf->blksize;
    lbuf->st_blocks   = vbuf->blocks;
    lbuf->st_atime    = vbuf->atime.sec;
    lbuf->st_mtime    = vbuf->mtime.sec;
    lbuf->st_ctime    = vbuf->ctime.sec;
}

static void convert_stat(struct avstat *vbuf, struct stat *lbuf)
{
    memset((void *) lbuf, 0, sizeof(*lbuf));
  
    lbuf->st_dev      = vbuf->dev;
    lbuf->st_ino      = vbuf->ino;
    lbuf->st_mode     = vbuf->mode;
    lbuf->st_nlink    = vbuf->nlink;
    lbuf->st_uid      = vbuf->uid;
    lbuf->st_gid      = vbuf->gid;
    lbuf->st_rdev     = vbuf->rdev;
    lbuf->st_size     = vbuf->size;
    lbuf->st_blksize  = vbuf->blksize;
    lbuf->st_blocks   = vbuf->blocks;
    lbuf->st_atime    = vbuf->atime.sec;
    lbuf->st_mtime    = vbuf->mtime.sec;
    lbuf->st_ctime    = vbuf->ctime.sec;
}

static int virt_stat64(const char *path, struct stat64 *buf, int deref,
                       int undersc)
{
    int res = 0;
    int local = 0;

    if(__av_maybe_local(path)) {
        res = real_stat64(path, buf, deref, undersc);
        local = __av_is_local(res, path);
    }
    
    if(!local) {
        int errnosave;
        struct avstat vbuf;
        char pathbuf[PATHBUF_LEN];

        errnosave = errno;
        res = cmd_stat(path, &vbuf, deref, pathbuf);
        errno = errnosave;
        if(pathbuf[0])
            res = real_stat64(pathbuf, buf, deref, undersc);
        else if(res < 0)
            errno = -res, res = -1;
        else
            convert_stat64(&vbuf, buf);
    }

    return res;
}

static int virt_fstat64(int fd, struct stat64 *buf, int undersc)
{
    int res;

    if(!FD_OK(fd) || !ISVIRTUAL(fd))
        res =  real_fstat64(fd, buf, undersc);
    else {
        struct avstat vbuf;
        int errnosave = errno;
        res = fstat_server(SERVERFH(fd), &vbuf);
        if(res < 0)
            errno = -res, res = -1;
        else {
            errno = errnosave;
            convert_stat64(&vbuf, buf);
        }
    }

    return res;
}


static int virt_stat(const char *path, struct stat *buf, int deref, 
                     int undersc)
{
    int res = 0;
    int local = 0;

    if(__av_maybe_local(path)) {
        res = real_stat(path, buf, deref, undersc);
        local = __av_is_local(res, path);
    }
    
    if(!local) {
        int errnosave;
        struct avstat vbuf;
        char pathbuf[PATHBUF_LEN];

        errnosave = errno;
        res = cmd_stat(path, &vbuf, deref, pathbuf);
        errno = errnosave;
        if(pathbuf[0])
            res = real_stat(pathbuf, buf, deref, undersc);
        else if(res < 0)
            errno = -res, res = -1;
        else
            convert_stat(&vbuf, buf);
    }

    return res;
}

static int virt_fstat(int fd, struct stat *buf, int undersc)
{
    int res;

    if(!FD_OK(fd) || !ISVIRTUAL(fd))
        res =  real_fstat(fd, buf, undersc);
    else {
        struct avstat vbuf;
        int errnosave = errno;
        res = fstat_server(SERVERFH(fd), &vbuf);
        if(res < 0)
            errno = -res, res = -1;
        else {
            errno = errnosave;
            convert_stat(&vbuf, buf);
        }
    }

    return res;
}


int lstat64(const char *path, struct stat64 *buf)
{
    return virt_stat64(path, buf, 0, 0);
}

int _lstat64(const char *path, struct stat64 *buf)
{
    return virt_stat64(path, buf, 0, 1);
}

int stat64(const char *path, struct stat64 *buf)
{
    return virt_stat64(path, buf, 1, 0);
}

int _stat64(const char *path, struct stat64 *buf)
{
    return virt_stat64(path, buf, 1, 1);
}

int fstat64(int fd, struct stat64 *buf)
{
    return virt_fstat64(fd, buf, 0);
}

int _fstat64(int fd, struct stat64 *buf)
{
    return virt_fstat64(fd, buf, 1);
}

int lstat(const char *path, struct stat *buf)
{
    return virt_stat(path, buf, 0, 0);
}

int _lstat(const char *path, struct stat *buf)
{
    return virt_stat(path, buf, 0, 1);
}

int stat(const char *path, struct stat *buf)
{
    return virt_stat(path, buf, 1, 0);
}

int _stat(const char *path, struct stat *buf)
{
    return virt_stat(path, buf, 1, 1);
}

int fstat(int fd, struct stat *buf)
{
    return virt_fstat(fd, buf, 0);
}

int _fstat(int fd, struct stat *buf)
{
    return virt_fstat(fd, buf, 1);
}
