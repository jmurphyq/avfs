/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

/* For pread */
#define _XOPEN_SOURCE 500

#include "serialfile.h"
#include "cache.h"

#include <fcntl.h>
#include <unistd.h>

struct sfile {
    struct sfilefuncs *func;
    void *data;
    int flags;
    void *conndata;
    char *localfile;
    avoff_t numbytes;
    int fd;
    enum { SF_BEGIN, SF_READ, SF_IDLE } state;
};

static void sfile_init(struct sfile *fil)
{
    fil->conndata = NULL;
    fil->localfile = NULL;
    fil->numbytes = 0;
    fil->fd = -1;
    fil->state = SF_BEGIN;
}

static void sfile_end(struct sfile *fil)
{
    close(fil->fd);
    __av_del_tmpfile(fil->localfile);
    __av_unref_obj(fil->conndata);
}

static void sfile_reset(struct sfile *fil)
{
    sfile_end(fil);
    sfile_init(fil);
}

static void sfile_reset_usecache(struct sfile *fil)
{
    fil->flags &= ~SFILE_NOCACHE;
    sfile_reset(fil);
}

static void sfile_delete(struct sfile *fil)
{
    sfile_end(fil);
    __av_unref_obj(fil->data);
}

struct sfile *__av_sfile_new(struct sfilefuncs *func, void *data, int flags)
{
    struct sfile *fil;

    AV_NEW_OBJ(fil, sfile_delete);
    fil->func = func;
    fil->data = data;
    fil->flags = flags;

    sfile_init(fil);

    return fil;
}

static int sfile_open_localfile(struct sfile *fil)
{
    int res;
    int openfl;

    res = __av_get_tmpfile(&fil->localfile);
    if(res < 0)
        return res;
    
    openfl = O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
    fil->fd = open(fil->localfile, openfl, 0600);
    if(fil->fd == -1) {
        __av_log(AVLOG_ERROR, "Error opening file %s: %s", fil->localfile,
                 strerror(errno));
        return -EIO;
    }
    
    return 0;
}

static int sfile_startget(struct sfile *fil)
{
    int res;

    if(!(fil->flags & SFILE_NOCACHE)) {
        res = sfile_open_localfile(fil);
        if(res < 0)
            return res;
    }
    
    res = fil->func->startget(fil->data, &fil->conndata);
    if(res < 0)
        return res;

    fil->state = SF_READ;
    
    return 0;
}


static avssize_t sfile_do_read(struct sfile *fil, char *buf, avssize_t nbyte)
{
    avsize_t numbytes;

    numbytes = 0;
    while(nbyte > 0) {
        avssize_t res;

        res = fil->func->read(fil->conndata, buf, nbyte);
        if(res < 0)
            return res;
        
        if(res == 0) {
            __av_unref_obj(fil->conndata);
            fil->conndata = NULL;
            fil->state = SF_IDLE;
            break;
        }
        
        nbyte -= res;
        buf += res;
        numbytes += res;
    }

    return numbytes;
}


static avssize_t sfile_read(struct sfile *fil, char *buf, avssize_t nbyte)
{
    avssize_t res;
    avssize_t wres;

    res = sfile_do_read(fil, buf, nbyte);
    if(res <= 0)
        return res;

    if(!(fil->flags & SFILE_NOCACHE)) {
        wres = write(fil->fd, buf, res);
        if(wres < 0) {
            __av_log(AVLOG_ERROR, "Error writing file %s: %s", fil->localfile,
                     strerror(errno));
            return -EIO;
        }
        if(wres != res) {
            __av_log(AVLOG_ERROR, "Error writing file %s: short write",
                     fil->localfile);
            return -EIO;
        }
    }
    
    fil->numbytes += res;

    return res;
}

static int sfile_dummy_read(struct sfile *fil, avoff_t offset)
{
    avssize_t res;
    avsize_t nact;
    const int tmpbufsize = 8192;
    char tmpbuf[tmpbufsize];

    if((fil->flags & SFILE_NOCACHE) != 0)
        nact = AV_MIN(tmpbufsize, offset - fil->numbytes);
    else
        nact = tmpbufsize;

    res = sfile_read(fil, tmpbuf, tmpbufsize);
    
    if(res < 0)
        return res;
    
    return 0;
}

static avssize_t sfile_cached_read(struct sfile *fil, char *buf,
                                   avssize_t nbyte, avoff_t offset)
{
    avssize_t res;

    if(nbyte == 0)
        return 0;

    res = pread(fil->fd, buf, nbyte, offset);
    if(res < 0) {
        __av_log(AVLOG_ERROR, "Error reading file %s: %s",
                 fil->localfile, strerror(errno));
        return -EIO;
    }
    if(res != nbyte) {
        __av_log(AVLOG_ERROR, "Error reading file %s: short read",
                 fil->localfile);
        return -EIO;
    }

    return res;
}

static avssize_t sfile_finished_read(struct sfile *fil, char *buf,
                                     avsize_t nbyte, avoff_t offset)
{
    avsize_t nact;
    
    if(offset >= fil->numbytes)
        return 0;
        
    nact = AV_MIN(nbyte, fil->numbytes - offset);

    return sfile_cached_read(fil, buf, nact, offset);
}

static avssize_t sfile_pread(struct sfile *fil, char *buf, avsize_t nbyte,
                             avoff_t offset)
{
    int res;

    while(fil->state == SF_READ) {
        if(offset + nbyte <= fil->numbytes)
            return sfile_cached_read(fil, buf, nbyte, offset);
        
        if(offset == fil->numbytes)
            return sfile_read(fil, buf, nbyte);
        
        res = sfile_dummy_read(fil, offset);
        if(res < 0)
            return res;
    }

    return sfile_finished_read(fil, buf, nbyte, offset);
}

static avssize_t sfile_pread_start(struct sfile *fil, char *buf,
                                   avsize_t nbyte, avoff_t offset)
{
    int res;

    if((fil->flags & SFILE_NOCACHE) != 0 && offset < fil->numbytes)
        sfile_reset_usecache(fil);

    if(fil->state == SF_BEGIN) {
        res = sfile_startget(fil);
        if(res < 0)
            return res;
    }

    return sfile_pread(fil, buf, nbyte, offset);
}

static avssize_t sfile_pread_force(struct sfile *fil, char *buf,
                                   avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    res = sfile_pread_start(fil, buf, nbyte, offset);
    if(res < 0) {
        if(res == -EAGAIN && fil->numbytes > 0) {
            sfile_reset(fil);
            res = sfile_pread_start(fil, buf, nbyte, offset);
        }
        if(res < 0) {
            if(res == -EAGAIN)
                res = -EIO;
            
            sfile_reset(fil);
        }
    }

    return res;
}

avssize_t __av_sfile_pread(struct sfile *fil, char *buf, avsize_t nbyte,
                           avoff_t offset)
{
    if(nbyte == 0)
        return 0;
    
    return sfile_pread_force(fil, buf, nbyte, offset);
}


avoff_t __av_sfile_size(struct sfile *fil)
{
    avssize_t res;

    res = sfile_pread_force(fil, NULL, 0, AV_MAXOFF);
    if(res < 0)
        return res;

    return fil->numbytes;
}

int __av_sfile_startget(struct sfile *fil)
{
    return sfile_pread_force(fil, NULL, 0, 0);
}

avssize_t __av_sfile_truncate(struct sfile *fil, avoff_t length)
{
    return -ENOSYS;
}

avssize_t __av_sfile_pwrite(struct sfile *fil, const char *buf, avsize_t nbyte,
                            avoff_t offset)
{
    return -ENOSYS;
}
