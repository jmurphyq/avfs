/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include "zfile.h"
#include "zlib.h"
#include "oper.h"

#include <stdlib.h>
#include <fcntl.h>

#define INDEXDISTANCE 1048576

#define INBUFSIZE 16384
#define OUTBUFSIZE 32768

struct zindex {
    avoff_t offset;          /* The number of output bytes */
    avoff_t indexoffset;     /* Offset in the indexfile */
    avsize_t indexsize;      /* Size of state record */
    struct zindex *next;
};

struct zcache {
    avmutex lock;
    char *indexfile;
    avoff_t filesize;
    avoff_t nextindex;
    struct zindex *indexes;
};

struct zfile {
    z_stream s;
    int iseof;
    
    vfile *infile;
    avoff_t dataoff;
    char inbuf[INBUFSIZE];
};

static int zfile_reset(struct zfile *fil)
{
    int res;

    res = inflateReset(&fil->s);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateReset: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        return -EIO;
    }
    fil->iseof = 0;

    return 0;
}

static int zfile_save_state(struct zcache *zc, char *state, int statesize,
                            avoff_t offset)
{
    int fd;
    int res;
    struct zindex **zp;
    struct zindex *zi;

    for(zp = &zc->indexes; *zp != NULL; zp = &(*zp)->next);

    fd = open(zc->indexfile, O_WRONLY | O_CREAT, 0600);
    if(fd == -1) {
        av_log(AVLOG_ERROR, "ZFILE: Error opening indexfile %s: %s",
               zc->indexfile, strerror(errno));
        return -EIO;
    }
    
    lseek(fd, zc->filesize, SEEK_SET);
    
    res = write(fd, state, statesize);
    close(fd);
    if(res == -1) {
        av_log(AVLOG_ERROR, "ZFILE: Error writing indexfile %s: %s",
               zc->indexfile, strerror(errno));
        return -EIO;
    }

    AV_NEW(zi);
    zi->offset = offset;
    zi->indexoffset = zc->filesize;
    zi->indexsize = statesize;
    zi->next = NULL;
    
    *zp = zi;

    zc->nextindex += INDEXDISTANCE;
    zc->filesize += statesize;
    
    return 0;
}

static int zfile_save_index(struct zfile *fil, struct zcache *zc)
{
    int res;
    char *state;

    res = inflateSave(&fil->s, &state);
    if(res < 0) {
        av_log(AVLOG_ERROR, "ZFILE: inflateSave: (%i)", res);
        return -EIO;
    }
    
    res = zfile_save_state(zc, state, res, fil->s.total_out);
    free(state);

    return res;
}

static int zfile_seek_index(struct zfile *fil, struct zcache *zc, 
                            struct zindex *zi)
{
    int fd;
    int res;
    char *state;

    res = inflateEnd(&fil->s);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateEnd: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        return -EIO;
    }
    memset(&fil->s, 0, sizeof(z_stream));

    fd = open(zc->indexfile, O_RDONLY, 0);
    if(fd == -1) {
        av_log(AVLOG_ERROR, "ZFILE: Error opening indexfile %s: %s",
               zc->indexfile, strerror(errno));
        return -EIO;
    }
    
    lseek(fd, zi->indexoffset, SEEK_SET);

    state = av_malloc(zi->indexsize);
    res = read(fd, state, zi->indexsize);
    close(fd);
    if(res != zi->indexsize) {
        av_free(state);
        av_log(AVLOG_ERROR, "ZFILE: Error in indexfile %s", zc->indexfile);
        return -EIO;
    }

    res = inflateRestore(&fil->s, state);
    av_free(state);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateRestore: (%i)", res);
        return -EIO;
    }
    fil->iseof = 0;

    return 0;
}

static struct zindex *zcache_find_index(struct zcache *zc, avoff_t offset)
{
    struct zindex *prevzi;
    struct zindex *zi;
    
    prevzi = NULL;
    for(zi = zc->indexes; zi != NULL; zi = zi->next) {
        if(zi->offset > offset)
            break;
        prevzi = zi;
    }

    return prevzi;
}

static int zfile_fill_inbuf(struct zfile *fil)
{
    avssize_t res;

    res = av_pread(fil->infile, fil->inbuf, INBUFSIZE,
                   fil->s.total_in + fil->dataoff);
    if(res < 0)
        return res;
    
    fil->s.next_in = fil->inbuf;
    fil->s.avail_in = res;

    return 0;
}

static int zfile_inflate(struct zfile *fil, struct zcache *zc)
{
    int res;

    if(fil->s.avail_in == 0) {
        res = zfile_fill_inbuf(fil);
        if(res < 0)
            return res;
    }
    
    res = inflate(&fil->s, Z_NO_FLUSH);
    if(res == Z_STREAM_END) {
        fil->iseof = 1;
        return 0;
    }
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflate: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        return -EIO;
    }
    
    AV_LOCK(zc->lock);
    if(fil->s.total_out >= zc->nextindex)
        res = zfile_save_index(fil, zc);
    else
        res = 0;
    AV_UNLOCK(zc->lock);
    if(res < 0)
        return res;

    return 0;
}

static int zfile_read(struct zfile *fil, struct zcache *zc, char *buf,
                      avsize_t nbyte)
{
    int res;

    fil->s.next_out = buf;
    fil->s.avail_out = nbyte;
    while(fil->s.avail_out != 0 && !fil->iseof) {
        res = zfile_inflate(fil, zc);
        if(res < 0)
            return res;
    }

    return nbyte - fil->s.avail_out;
}

static int zfile_skip_to(struct zfile *fil, struct zcache *zc, avoff_t offset)
{
    int res;
    char outbuf[OUTBUFSIZE];
    
    while(fil->s.total_out < offset && !fil->iseof) {
        /* FIXME: Save us just a little of this rubbish :(,
           That would do much good to tar */
        fil->s.next_out = outbuf;
        fil->s.avail_out = AV_MIN(OUTBUFSIZE, offset - fil->s.total_out);

        res = zfile_inflate(fil, zc);
        if(res < 0)
            return res;
    }

    return 0;
}

static int zfile_seek(struct zfile *fil, struct zcache *zc, avoff_t offset)
{
    int res;
    struct zindex *zi;
    avoff_t curroff = fil->s.total_out;

    AV_LOCK(zc->lock);
    zi = zcache_find_index(zc, offset);
    if(offset < curroff || (zi != NULL && zi->offset > curroff)) {
        if(zi == NULL) {
            av_log(AVLOG_DEBUG, "seek to %lli, reseting", offset);
            res = zfile_reset(fil);
        }
        else {
            av_log(AVLOG_DEBUG, "seek to %lli, using %lli", offset,
                   zi->offset);
            res = zfile_seek_index(fil, zc, zi);
        }
    }
    else
        res = 0;
    AV_UNLOCK(zc->lock);

    if(res < 0)
        return res;
    
    return zfile_skip_to(fil, zc, offset);
}

avssize_t av_zfile_pread(struct zfile *fil, struct zcache *zc, char *buf,
                         avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    if(offset != fil->s.total_out) {
        res = zfile_seek(fil, zc, offset);
        if(res < 0)
            return res;
    }

    res = zfile_read(fil, zc, buf, nbyte);
    
    return res;
}

static void zfile_destroy(struct zfile *fil)
{
    if(fil->s.state != NULL) {
        int res;
        
        res = inflateEnd(&fil->s);
        if(res != Z_OK) {
            av_log(AVLOG_ERROR, "ZFILE: inflateEnd: %s (%i)",
                   fil->s.msg == NULL ? "" : fil->s.msg, res);
        }
    }
}

struct zfile *av_zfile_new(vfile *vf, avoff_t dataoff)
{
    int res;
    struct zfile *fil;

    AV_NEW_OBJ(fil, zfile_destroy);
    memset(&fil->s, 0, sizeof(z_stream));
    fil->iseof = 0;
    fil->infile = vf;
    fil->dataoff = dataoff;

    res = inflateInit2(&fil->s, -MAX_WBITS);
    if(res != Z_OK)
        av_log(AVLOG_ERROR, "ZFILE: inflateInit: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);

    return fil;
}

static void zcache_destroy(struct zcache *zc)
{
    struct zindex *zi;
    struct zindex *nextzi;

    AV_FREELOCK(zc->lock);
    av_del_tmpfile(zc->indexfile);
    
    for(zi = zc->indexes; zi != NULL; zi = nextzi) {
        nextzi = zi->next;
        av_free(zi);
    }
}

struct zcache *av_zcache_new()
{
    struct zcache *zc;

    AV_NEW_OBJ(zc, zcache_destroy);
    AV_INITLOCK(zc->lock);
    zc->indexfile = NULL;
    zc->nextindex = 0;
    zc->indexes = NULL;
    zc->filesize = 0;
    
    av_get_tmpfile(&zc->indexfile);
    
    return zc;
}

avoff_t av_zcache_size(struct zcache *zc)
{
    return zc->filesize;
}
