#include "server.h"
#include "cmd.h"
#include "send.h"
#include "internal.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <poll.h>

#define MULTITHREADED 1

#if MULTITHREADED
#include <pthread.h>
#endif

enum file_holder_state {
    FIH_DELETED = -1,
    FIH_UNUSED = 0,
    FIH_USED = 1,
};

struct file_holder {
    enum file_holder_state state;
    int serverfh;
};

static struct file_holder *file_holders;
static unsigned int file_holder_num;
static AV_LOCK_DECL(file_holder_lock);

static vfile **file_table;
static unsigned int file_table_size;
static AV_LOCK_DECL(files_lock);

static AV_LOCK_DECL(readcount_lock);
static unsigned int readcount;
static unsigned int readlen;
static int readfh;

struct cmdinfo {
    int fd;
    struct avfs_in_message inmsg;
    struct avfs_cmd cmd;
};

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

static void init_inmsg(struct avfs_in_message *inmsg)
{
    int i;

    for(i = 1; i < MAXSEG; i++)
        inmsg->seg[i].buf = NULL;
}

static void free_inmsg(struct avfs_in_message *inmsg)
{
    int i;

    for(i = 1; i < MAXSEG; i++) {
        if(inmsg->seg[i].buf)
            free(inmsg->seg[i].buf);
    }
}

static int entry_local(ventry *ve)
{
    if(ve->mnt->base == NULL)
        return 1;
    else
        return 0;
}

static int file_open(vfile *vf, ventry *ve, int flags, avmode_t mode)
{
    int res;
    struct avfs *avfs = ve->mnt->avfs;

    res = __av_copy_vmount(ve->mnt, &vf->mnt);
    if(res < 0)
	return res;

    AVFS_LOCK(avfs);
    res = avfs->open(ve, flags, mode, &vf->data);
    AVFS_UNLOCK(avfs);
    if(res < 0) {
	__av_free_vmount(vf->mnt);
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

static int getattr_entry(ventry *ve, struct avstat *stbuf, int attrmask,
                         int flags)
{
    int res;
    vfile vf;

    res = file_open(&vf, ve, AVO_NOPERM | flags, 0);
    if(res == 0) {
        res = file_getattr(&vf, stbuf, attrmask);
        file_close(&vf);
    }

    return res;
}

static void send_error(int fd, int error)
{
    int res;
    struct avfs_out_message outmsg;
    struct avfs_result result;
    
    outmsg.num = 1;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    
    result.result = error;
    res = __av_write_message(fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
}

static void free_vfile(vfile *vf)
{
    AV_FREELOCK(vf->lock);
}

static int open_entry(ventry *ve, int flags, avmode_t mode)
{
    int res;
    int fd;
    vfile *vf;

    AV_NEW_OBJ(vf, free_vfile);
    AV_INITLOCK(vf->lock);
    res = file_open(vf, ve, flags, mode);
    if(res < 0) {
        __av_unref_obj(vf);
        return res;
    }
    else {
	AV_LOCK(files_lock);
        fd = find_unused();
	file_table[fd] = vf;
	AV_UNLOCK(files_lock);
    }

    return fd;
}

static int do_close(int fd)
{
    int res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = file_close(vf);
        vf->mnt = NULL;
        put_file(vf);

        AV_LOCK(files_lock);
        file_table[fd] = NULL;
        AV_UNLOCK(files_lock);

        __av_unref_obj(vf);
    }

    return res;
}

static int do_fstat(int fd, struct avstat *buf)
{
    int res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        res = file_getattr(vf, buf, AVA_ALL);
	put_file(vf);
    }

    return res;
}

static int do_readdir(int fd, struct avfs_direntry *de, char *name)
{
    int res;
    struct avdirent buf;
    avoff_t n;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        struct avfs *avfs = vf->mnt->avfs;
	n = vf->ptr;
        AVFS_LOCK(avfs);
	res = avfs->readdir(vf, &buf);
        AVFS_UNLOCK(avfs);
	if(res > 0) {
            de->ino = buf.ino;
            de->type = buf.type;
            de->n = n;
            strncpy(name, buf.name, NAME_MAX);
            name[NAME_MAX] = '\0';
	    __av_free(buf.name);
	}
	put_file(vf);
    }

    return res;
}

static avoff_t do_lseek(int fd, avoff_t offset, int whence)
{
    avoff_t res;
    vfile *vf;

    res = get_file(fd, &vf);
    if(res == 0) {
        if(vf->flags & AVO_DIRECTORY) {
            switch(whence) {
            case SEEK_SET:
                if(offset < 0 || (offset % AVFS_DIR_RECLEN) != 0)
                    res = -EINVAL;
                else {
                    vf->ptr = offset / AVFS_DIR_RECLEN;
                    res = offset;
                }
                break;
                
            case SEEK_CUR:
                if(offset != 0)
                    res = -EINVAL;
                else
                    res = vf->ptr * AVFS_DIR_RECLEN;
                break;
                
            default:
                res = -EINVAL;
            }
        }
        else {
            struct avfs *avfs = vf->mnt->avfs;

            AVFS_LOCK(avfs);
            res = avfs->lseek(vf, offset, whence);
            AVFS_UNLOCK(avfs);
        }

	put_file(vf);
    }

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

static ssize_t do_read(int fd, void *buf, size_t nbyte)
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

    return res;
}

static ssize_t do_write(int fd, const void *buf, size_t nbyte)
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

    return res;
}

static void process_getattr(struct cmdinfo *ci)
{
    int res;
    char *path = ci->inmsg.seg[1].buf;
    int flags = ci->cmd.u.getattr.flags;
    int attrmask = ci->cmd.u.getattr.attrmask;
    struct avstat stbuf;
    ventry *ve;
    struct avfs_out_message outmsg;
    struct avfs_result result;

    __av_log(AVLOG_SYSCALL, "getattr(\"%s\", 0%o, 0%o)",
             path, flags, attrmask);
    
    outmsg.num = 3;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;
    outmsg.seg[2].len = 0;

    res = __av_get_ventry(path, !(flags & AVO_NOFOLLOW), &ve);
    if(res < 0)
        result.result = res;
    else {
        if(entry_local(ve)) {
            result.result = -EPERM;
            outmsg.seg[1].buf = (char *) ve->data;
            outmsg.seg[1].len = strlen(outmsg.seg[1].buf) + 1;
            if(outmsg.seg[1].len > PATHBUF_LEN) {
                outmsg.seg[1].len = 0;
                result.result = -ENAMETOOLONG;
            }
        }
        else {
            res = getattr_entry(ve, &stbuf, attrmask, flags);
            result.result = res;
            if(res == 0) {
                outmsg.seg[2].buf = &stbuf;
                outmsg.seg[2].len = sizeof(stbuf);
            }
        }
    }

    __av_log(AVLOG_SYSCALL, "   getattr(\"%s\", 0%o, 0%o) = %i (%s)",
             path, flags, attrmask, result.result,
             outmsg.seg[1].len ? (char *) ve->data : "");

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");

    __av_free_ventry(ve);    
}

static void register_holder(int holderfd, int serverfh)
{
    int needclose = 0;
    int state;

    AV_LOCK(file_holder_lock);
    state = file_holders[holderfd].state;

    __av_log(AVLOG_DEBUG, "register_holder: %i, state: %i/%i, serverfh: %i",
             holderfd, state,  file_holders[holderfd].serverfh, serverfh);
    
    if(state == FIH_USED) {
        if(serverfh < 0)
            file_holders[holderfd].state = FIH_UNUSED;
        else
            file_holders[holderfd].serverfh = serverfh;
    }
    else {
        if(serverfh >= 0)
            needclose = 1;

        file_holders[holderfd].state = FIH_UNUSED;
        close(holderfd);
    }
    AV_UNLOCK(file_holder_lock);

    if(needclose)
        do_close(serverfh);
}

static void process_open(struct cmdinfo *ci)
{
    int res;
    char *path = ci->inmsg.seg[1].buf;
    int flags = ci->cmd.u.open.flags;
    mode_t mode = ci->cmd.u.open.mode;
    ventry *ve;
    struct avfs_out_message outmsg;
    struct avfs_result result;

    __av_log(AVLOG_SYSCALL, "open(\"%s\", 0%o, 0%lo)", path, flags, mode);
    
    outmsg.num = 2;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;

    res = __av_get_ventry(path, 1, &ve);
    if(res < 0)
        result.result = res;
    else {
        if(entry_local(ve)) {
            result.result = -EPERM;
            outmsg.seg[1].buf = (char *) ve->data;
            outmsg.seg[1].len = strlen(outmsg.seg[1].buf) + 1;
            if(outmsg.seg[1].len > PATHBUF_LEN) {
                outmsg.seg[1].len = 0;
                result.result = -ENAMETOOLONG;
            }
        }
        else {
            struct avstat stbuf;

            res = getattr_entry(ve, &stbuf, AVA_MODE, 0);
            if(res == 0) {
                if(AV_ISDIR(stbuf.mode))
                    res = open_entry(ve, flags | AVO_DIRECTORY, mode);
                else
                    res = open_entry(ve, flags, mode);
            }
            else if(res == -ENOENT && (flags & AVO_CREAT) != 0)
                res = open_entry(ve, flags, mode);

            result.result = res;
        }
    }
    __av_log(AVLOG_SYSCALL, "   open(\"%s\", 0%o, 0%lo) = %i (%s)",
             path, flags, mode, result.result,
             outmsg.seg[1].len ? (char *) ve->data : "");

    register_holder(ci->fd, result.result);

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");

    __av_free_ventry(ve);    
}


static void process_close(struct cmdinfo *ci)
{
    int res;
    int fh = ci->cmd.u.fdops.serverfh;
    struct avfs_out_message outmsg;
    struct avfs_result result;

    __av_log(AVLOG_SYSCALL, "close(%i)", fh);
    
    outmsg.num = 1;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;

    result.result = do_close(fh);
    __av_log(AVLOG_SYSCALL, "   close(%i) = %i", fh, result.result);

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
}

static void process_fstat(struct cmdinfo *ci)
{
    int res;
    int fh = ci->cmd.u.fdops.serverfh;
    struct avfs_out_message outmsg;
    struct avfs_result result;
    struct avstat stbuf;

    __av_log(AVLOG_SYSCALL, "fstat(%i)", fh);
    
    outmsg.num = 2;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;

    result.result = do_fstat(fh, &stbuf);
    if(result.result == 0) {
        outmsg.seg[1].len = sizeof(stbuf);
        outmsg.seg[1].buf = &stbuf;
    }

    __av_log(AVLOG_SYSCALL, "   fstat(%i) = %i", fh, result.result);

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
}

static void process_readdir(struct cmdinfo *ci)
{
    int res;
    int fh = ci->cmd.u.fdops.serverfh;
    struct avfs_out_message outmsg;
    struct avfs_result result;
    char name[NAME_MAX + 1];
    struct avfs_direntry de;

    __av_log(AVLOG_SYSCALL, "readdir(%i, ...)", fh);
    
    outmsg.num = 3;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;
    outmsg.seg[2].len = 0;

    result.result = do_readdir(fh, &de, name);
    if(result.result > 0) {
        outmsg.seg[1].len = sizeof(de);
        outmsg.seg[1].buf = &de;
        outmsg.seg[2].len = strlen(name) + 1;
        outmsg.seg[2].buf = name;
    }

    __av_log(AVLOG_SYSCALL, "   readdir(%i, {%s, %lli, %i}) = %i",
             fh, result.result > 0 ? name : "", de.ino, de.n, result.result);

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
}

static void process_lseek(struct cmdinfo *ci)
{
    int res;
    int fh = ci->cmd.u.lseek.serverfh;
    avoff_t offset = ci->cmd.u.lseek.offset;
    int whence = ci->cmd.u.lseek.whence;
    struct avfs_out_message outmsg;
    struct avfs_result result;

    __av_log(AVLOG_SYSCALL, "lseek(%i, %lli, %i)", fh, offset, whence);
    
    outmsg.num = 1;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;

    result.u.lseek.offset = do_lseek(fh, offset, whence);

    __av_log(AVLOG_SYSCALL, "   lseek(%i, %lli, %i) == %lli",
             fh, offset, whence, result.u.lseek.offset);

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
}

static void process_read(struct cmdinfo *ci)
{
    int res;
    int fh = ci->cmd.u.readwrite.serverfh;
    avsize_t nbyte = ci->cmd.u.readwrite.nbyte;
    struct avfs_out_message outmsg;
    struct avfs_result result;
    void *buf;
    int fulllog = 0;;
    
    AV_LOCK(readcount_lock);
    if(readcount == 0 || readlen != nbyte || fh != readfh) {
        if(readcount > 1)
            __av_log(AVLOG_SYSCALL, "* Repeated %i times", readcount);

        fulllog = 1;
        readcount = 0;
        readlen = nbyte;
        readfh = fh;
        __av_log(AVLOG_SYSCALL, "read(%i, ..., %i)", fh, nbyte);
    }
    else if(readcount == 1) 
        __av_log(AVLOG_SYSCALL, "* [...]");

    readcount ++;
    AV_UNLOCK(readcount_lock);
    
    outmsg.num = 2;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;
    
    buf = __av_malloc(nbyte);

    result.result = do_read(fh, buf, nbyte);
    if(result.result > 0) {
        outmsg.seg[1].len = result.result;
        outmsg.seg[1].buf = buf;
    }
   
    if(fulllog) {
        __av_log(AVLOG_SYSCALL, "   read(%i, ..., %u) = %i",
                 fh, nbyte, result.result);
    }

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");

    __av_free(buf);
}

static void process_write(struct cmdinfo *ci)
{
    int res;
    int fh = ci->cmd.u.readwrite.serverfh;
    avsize_t nbyte = ci->cmd.u.readwrite.nbyte;
    struct avfs_out_message outmsg;
    struct avfs_result result;
    const void *buf = ci->inmsg.seg[1].buf;

    __av_log(AVLOG_SYSCALL, "write(%i, ..., %u)", fh, nbyte);
    
    outmsg.num = 1;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    
    result.result = do_write(fh, buf, nbyte);
   
    __av_log(AVLOG_SYSCALL, "   write(%i, ..., %u) = %i",
             fh, nbyte, result.result);


    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
}


static void process_resolve(struct cmdinfo *ci)
{
    int res;
    char *path = ci->inmsg.seg[1].buf;
    ventry *ve;
    struct avfs_out_message outmsg;
    struct avfs_result result;
    char *newpath = NULL;
    

    __av_log(AVLOG_SYSCALL, "resolve(\"%s\")", path);
    
    outmsg.num = 2;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;

    res = __av_get_ventry(path, 1, &ve);
    if(res < 0)
        result.result = res;
    else {
        if(entry_local(ve))
            result.u.resolve.isvirtual = 0;
        else
            result.u.resolve.isvirtual = 1;

        res = __av_generate_path(ve, &newpath);
        result.result = res;
        if(res == 0) {
            outmsg.seg[1].buf = newpath;
            outmsg.seg[1].len = strlen(outmsg.seg[1].buf) + 1;
            if(outmsg.seg[1].len > PATHBUF_LEN) {
                outmsg.seg[1].len = 0;
                result.result = -ENAMETOOLONG;
            }
        }
        __av_free_ventry(ve);    
    }

    __av_log(AVLOG_SYSCALL, "   resolve(\"%s\", \"%s\", %i) = %i",
             path, outmsg.seg[1].len ? newpath : "", 
             result.u.resolve.isvirtual, result.result);

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");
    
    __av_free(newpath);
}

static void process_readlink(struct cmdinfo *ci)
{
    int res;
    char *path = ci->inmsg.seg[1].buf;
    avsize_t bufsize = ci->cmd.u.readlink.bufsize;
    char *buf = NULL;
    ventry *ve;
    struct avfs_out_message outmsg;
    struct avfs_result result;

    __av_log(AVLOG_SYSCALL, "readlink(\"%s\", ..., %u)", path, bufsize);
    
    outmsg.num = 3;
    outmsg.seg[0].len = sizeof(result);
    outmsg.seg[0].buf = &result;
    outmsg.seg[1].len = 0;
    outmsg.seg[2].len = 0;

    res = __av_get_ventry(path, 0, &ve);
    if(res < 0)
        result.result = res;
    else {
        if(entry_local(ve)) {
            result.result = -EPERM;
            outmsg.seg[1].buf = (char *) ve->data;
            outmsg.seg[1].len = strlen(outmsg.seg[1].buf) + 1;
            if(outmsg.seg[1].len > PATHBUF_LEN) {
                outmsg.seg[1].len = 0;
                result.result = -ENAMETOOLONG;
            }
        }
        else {
            struct avfs *avfs = ve->mnt->avfs;
            
            AVFS_LOCK(avfs);
            res = avfs->readlink(ve, &buf);
            AVFS_UNLOCK(avfs);
            
            if(res == 0) {
                avsize_t linklen = strlen(buf);

                result.result = AV_MIN(linklen, bufsize);
                outmsg.seg[2].len = AV_MIN(linklen + 1, bufsize);
                outmsg.seg[2].buf = buf;
            }
            else
                result.result = res;
        }
    }

    __av_log(AVLOG_SYSCALL, "   readlink(\"%s\", \"%.*s\", %i) = %i (%s)",
             path, 
             result.result < 0 ? 0 : result.result, buf,
             bufsize, result.result, 
             outmsg.seg[1].len ? (char *) ve->data : "");

    res = __av_write_message(ci->fd, &outmsg);
    if(res == -1)
        __av_log(AVLOG_ERROR, "Error sending message\n");

    __av_free_ventry(ve);    
    __av_free(buf);
}


static void *process_message(void *arg)
{
    struct cmdinfo *ci = (struct cmdinfo *) arg;

    AV_LOCK(readcount_lock);
    if(ci->cmd.type != CMD_READ && readcount != 0) {
        if(readcount > 1)
            __av_log(AVLOG_SYSCALL, "* Repeated %i times", readcount);
        
        readcount = 0;
    }
    AV_UNLOCK(readcount_lock);
    
    switch(ci->cmd.type) {
    case CMD_GETATTR:
        process_getattr(ci);
        break;
        
    case CMD_OPEN:
        process_open(ci);
        break;
        
    case CMD_CLOSE:
        process_close(ci);
        break;
        
    case CMD_FSTAT:
        process_fstat(ci);
        break;
        
    case CMD_READDIR:
        process_readdir(ci);
        break;
        
    case CMD_LSEEK:
        process_lseek(ci);
        break;
        
    case CMD_READ:
        process_read(ci);
        break;

    case CMD_WRITE:
        process_write(ci);
        break;
        
    case CMD_RESOLVE:
        process_resolve(ci);
        break;
        
    case CMD_READLINK:
        process_readlink(ci);
        break;
        
    default:
        __av_log(AVLOG_ERROR, "Unknown command: %i", ci->cmd.type);
        send_error(ci->fd, -ENOSYS);
    }

    if(ci->cmd.type != CMD_OPEN)
        close(ci->fd);
    free_inmsg(&ci->inmsg);
    __av_free(ci);

    return NULL;
}

static void mark_file_holder(int holderfd)
{
    AV_LOCK(file_holder_lock);
    if(holderfd >= file_holder_num) {
        int i;
        unsigned int newnum = holderfd + 1;
        unsigned int newsize = newnum  * sizeof(struct file_holder);

        file_holders = __av_realloc(file_holders, newsize);
        for(i = file_holder_num; i <= holderfd; i++)
            file_holders[i].state = FIH_UNUSED;

        file_holder_num = newnum;
    }
    
    if(file_holders[holderfd].state != FIH_UNUSED)
        __av_log(AVLOG_ERROR, "Internal Error: file holder %i already used",
                 holderfd);

    file_holders[holderfd].state = FIH_USED;
    file_holders[holderfd].serverfh = -1;
    
    AV_UNLOCK(file_holder_lock);
}

static void unmark_file_holder(int serverfh)
{
    int i;

    AV_LOCK(file_holder_lock);
    for(i = 0; i < file_holder_num; i++) {
        if(file_holders[i].state == FIH_USED &&
           file_holders[i].serverfh == serverfh) {
            file_holders[i].state = FIH_UNUSED;
            close(i);
            break;
        }
    }
    if(i == file_holder_num)
        __av_log(AVLOG_DEBUG, "File holder not found for %i", serverfh);

    AV_UNLOCK(file_holder_lock);    
}

static int wait_message(int sock)
{
    int i;
    int nfds;
    struct pollfd *fds;
    int canaccept;
    int res;

    while(1) {
        AV_LOCK(file_holder_lock);
        nfds = 1;
        for(i = 0; i < file_holder_num; i++) {
            if(file_holders[i].state == FIH_USED)
                nfds++;
        }
        /* This is not __av_malloc(), because exit will usually happen
           during poll(), and then there would be one unfreed memory. */
        fds = malloc(sizeof(struct pollfd) * nfds);
        nfds = 1;
        for(i = 0; i < file_holder_num; i++) {
            if(file_holders[i].state == FIH_USED) {
                fds[nfds].fd = i;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }
        AV_UNLOCK(file_holder_lock);
        fds[0].fd = sock;
        fds[0].events = POLLIN;
        
        do res = poll(fds, nfds, -1);
        while(res == -1 && (errno == EAGAIN || errno == EINTR));

        if(res == -1) {
            __av_log(AVLOG_ERROR, "poll(): %s", strerror(errno));
            exit(1);
        }

        for(i = 1; i < nfds; i++) {
            if(fds[i].revents != 0) {
                int holderfd = fds[i].fd;
                int serverfh;

                AV_LOCK(file_holder_lock);
                serverfh = file_holders[holderfd].serverfh;

                {
                    char c;
                    __av_log(AVLOG_DEBUG, "File holder closed: %i", holderfd);
                    __av_log(AVLOG_DEBUG, "serverfh: %i", serverfh);
                    __av_log(AVLOG_DEBUG, "state: %i",
                             file_holders[holderfd].state);
                    __av_log(AVLOG_DEBUG, "revents: 0%o", fds[i].revents);
                    res = read(holderfd, &c, 1);
                    __av_log(AVLOG_DEBUG, "read: %i, (%i)", res, c);
                }
                
                if(file_holders[holderfd].state == FIH_USED) {
                    if(serverfh == -1)
                        file_holders[holderfd].state = FIH_DELETED;
                    else {
                        close(holderfd);
                        file_holders[holderfd].state = FIH_UNUSED;
                    }
                }
                else 
                    close(holderfd);

                AV_UNLOCK(file_holder_lock);

                __av_log(AVLOG_DEBUG, "File holder for %i closed", serverfh);
                                
                if(serverfh != -1)
                    do_close(serverfh);
            }
        }

        canaccept = fds[0].revents;

        free(fds);
        
        if(canaccept) {
            int fd;

            fd = accept(sock, NULL, NULL);
            if(fd == -1) {
                __av_log(AVLOG_ERROR, "accept(): %s", strerror(errno));
                exit(1);
            }

            return fd;
        }
    }
}

int main()
{
    int sock;

#if MULTITHREADED
    pthread_attr_t attr;
    pthread_t thrid;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#endif

    sock = server_init();

    while(1) {
        int fd;
        int res;
        struct cmdinfo *ci;

        fd = wait_message(sock);
        
        AV_NEW(ci);
        init_inmsg(&ci->inmsg);
        ci->inmsg.seg[0].buf = &ci->cmd;

        res = __av_read_message(fd, &ci->inmsg);
        if(res == -1)
            __av_log(AVLOG_ERROR, "Error reading message");
        else {
            ci->fd = fd;

            if(ci->cmd.type == CMD_OPEN)
                mark_file_holder(ci->fd);
            else if(ci->cmd.type == CMD_CLOSE)
                unmark_file_holder(ci->cmd.u.fdops.serverfh);
            
#if MULTITHREADED
            res = pthread_create(&thrid, &attr, process_message, ci);
            if(res != 0) 
                __av_log(AVLOG_ERROR, "Error creating thread: %i",
                         res);
#else
            process_message(ci);
#endif
        }
    }
    
    return 0;
}

void __av_close_all_files()
{
    int fd;
    vfile *vf;

    __av_log(AVLOG_DEBUG, "Server exiting");
    
    AV_LOCK(files_lock);
    for(fd = 0; fd < file_table_size; fd++) {
        vf = file_table[fd];
        if(vf != NULL) {
            __av_log(AVLOG_WARNING, "File handle still in use: %i", fd);
            file_close(vf);
            vf->mnt = NULL;
            __av_unref_obj(vf);
        }
    }
    __av_free(file_table);
    file_table = NULL;
    AV_UNLOCK(files_lock);
    
    AV_LOCK(file_holder_lock);
    __av_free(file_holders);
    file_holders = NULL;
    AV_UNLOCK(file_holder_lock);
}
