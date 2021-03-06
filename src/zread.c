/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include "config.h"
#include "zfile.h"
#include "zlib.h"
#include "oper.h"

#include <stdlib.h>
#include <fcntl.h>

#define INDEXDISTANCE 1048576

#define INBUFSIZE 16384
#define OUTBUFSIZE 32768

/* This is the 'cost' of the restoration from the index cache */
#define ZCACHE_EXTRA_DIST 45000

/* It is not worth it to compress the state better */
#define STATE_COMPRESS_LEVEL 1

#define BI(ptr, i)  ((avbyte) (ptr)[i])
#define DBYTE(ptr) (BI(ptr,0) | (BI(ptr,1)<<8))
#define QBYTE(ptr) ((avuint) (BI(ptr,0) | (BI(ptr,1)<<8) | \
                   (BI(ptr,2)<<16) | (BI(ptr,3)<<24)))

struct streamcache {
    int id;
    z_stream s;
    int calccrc;
    int iseof;
};

static struct streamcache scache;
static int zread_nextid;
static AV_LOCK_DECL(zread_lock);

struct zindex {
    avoff_t offset;          /* The number of output bytes */
    avoff_t indexoffset;     /* Offset in the indexfile */
    avsize_t indexsize;      /* Size of state record */
    struct zindex *next;
};

struct zcache {
    char *indexfile;
    avoff_t filesize;
    avoff_t nextindex;
    avoff_t size;
    int id;
    struct zindex *indexes;
    avmutex lock;
    int crc_ok;
};

struct zfile {
    z_stream s;
    int iseof;
    int iserror;
    int id; /* Hack: the id of the last used zcache */
    int calccrc;
    enum av_zfile_data_type data_type;
    avuint crc;
    
    vfile *infile;
    avoff_t dataoff;
    char inbuf[INBUFSIZE];
};

#define GZHEADER_SIZE 10
#define GZMAGIC1 0x1f
#define GZMAGIC2 0x8b
/* gzip flag byte */
#define GZFL_ASCII        0x01 /* bit 0 set: file probably ascii text */
#define GZFL_CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define GZFL_EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define GZFL_ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define GZFL_COMMENT      0x10 /* bit 4 set: file comment present */
#define GZFL_RESERVED     0xE0 /* bits 5..7: reserved */

#define METHOD_DEFLATE 8

struct zfile_gzip_header_data {
};

struct zfile_gzip_trailer_data {
    avuint crc;
};

static int zfile_parse_gzip_header(struct zfile *fil, struct zfile_gzip_header_data *return_data);

#ifndef USE_SYSTEM_ZLIB
static int zfile_compress_state(char *state, int statelen, char **resp)
{
    int res;
    z_stream s;
    int bufsize = sizeof(int) + statelen + statelen / 1000 + 1 + 12;
    char *cstate = av_malloc(bufsize);
    
    s.next_in = (Bytef*)state;
    s.avail_in = statelen;
    s.next_out = (Bytef*)( cstate + sizeof(int) );
    s.avail_out = bufsize - sizeof(int);
    s.zalloc = NULL;
    s.zfree = NULL;

    ((int *) cstate)[0] = statelen;

    res = deflateInit(&s, STATE_COMPRESS_LEVEL);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: compress state failed");
        av_free(cstate);
        return -EIO;
    }

    res = deflate(&s, Z_FINISH);
    if(res != Z_STREAM_END) {
        av_log(AVLOG_ERROR, "ZFILE: compress state failed");
        av_free(cstate);
        return -EIO;
    }
    
    res = deflateEnd(&s);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: compress state failed");
        av_free(cstate);
        return -EIO;
    }
    
    *resp = cstate;
    return sizeof(int) + s.total_out;
}

static int zfile_uncompress_state(char *cstate, int cstatelen, char **resp)
{
    int res;
    z_stream s;
    int statelen = ((int *) cstate)[0];
    char *state = av_malloc(statelen);
    
    s.next_in = (Bytef*)( cstate + sizeof(int) );
    s.avail_in = cstatelen - sizeof(int);
    s.next_out = (Bytef*)state;
    s.avail_out = statelen;
    s.zalloc = NULL;
    s.zfree = NULL;

    res = inflateInit(&s);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: compress state failed");
        av_free(state);
        return -EIO;
    }

    res = inflate(&s, Z_FINISH);
    if(res != Z_STREAM_END) {
        av_log(AVLOG_ERROR, "ZFILE: compress state failed");
        av_free(state);
        return -EIO;
    }
    
    res = inflateEnd(&s);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: compress state failed");
        av_free(state);
        return -EIO;
    }
    
    *resp = state;
    return 0;
}
#endif

static void zfile_scache_cleanup(void)
{
    inflateEnd(&scache.s);
}

static void zfile_scache_save(int id, z_stream *s, int calccrc, int iseof)
{
    int res;

    if(id == 0 || iseof) {
        res = inflateEnd(s);
        if(res != Z_OK) {
            av_log(AVLOG_ERROR, "ZFILE: inflateEnd: %s (%i)",
                   s->msg == NULL ? "" : s->msg, res);
        }
        return;
    }

    if(scache.id != 0) {
        res = inflateEnd(&scache.s);
        if(res != Z_OK) {
            av_log(AVLOG_ERROR, "ZFILE: inflateEnd: %s (%i)",
                   scache.s.msg == NULL ? "" : scache.s.msg, res);
        }
    }
    if(scache.id == 0)
        atexit(zfile_scache_cleanup);

    scache.id = id;
    scache.s = *s;
    scache.calccrc = calccrc;
    scache.iseof = iseof;
}

static int zfile_reset(struct zfile *fil)
{
    int res;

    /* FIXME: Is it a good idea to save the previous state or not? */
    zfile_scache_save(fil->id, &fil->s, fil->calccrc, fil->iseof);
    memset(&fil->s, 0, sizeof(z_stream));
    res = inflateInit2(&fil->s, -MAX_WBITS);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateInit: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        return -EIO;
    }
    fil->s.adler = 0;
    fil->iseof = 0;
    fil->calccrc = 0;

    if (fil->data_type == AV_ZFILE_DATA_GZIP_ENCAPSULATED) {
        struct zfile_gzip_header_data header_data;
        res = zfile_parse_gzip_header(fil, &header_data);
        if (res != 0) {
            av_log(AVLOG_ERROR, "gzip header error");
            fil->iserror = 1;
        }
    }

    return 0;
}

#ifndef USE_SYSTEM_ZLIB
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
    char *cstate;

    res = inflateSave(&fil->s, &state);
    if(res < 0) {
        av_log(AVLOG_ERROR, "ZFILE: inflateSave: (%i)", res);
        return -EIO;
    }
    
    res = zfile_compress_state(state, res, &cstate);
    free(state);
    if(res < 0)
        return res;

    res = zfile_save_state(zc, cstate, res, fil->s.total_out);
    av_free(cstate);

    return res;
}

static int zfile_seek_index(struct zfile *fil, struct zcache *zc, 
                            struct zindex *zi)
{
    int fd;
    int res;
    char *cstate;
    char *state;

    /* FIXME: Is it a good idea to save the previous state or not? */
    zfile_scache_save(fil->id, &fil->s, fil->calccrc, fil->iseof);
    memset(&fil->s, 0, sizeof(z_stream));

    fd = open(zc->indexfile, O_RDONLY, 0);
    if(fd == -1) {
        av_log(AVLOG_ERROR, "ZFILE: Error opening indexfile %s: %s",
               zc->indexfile, strerror(errno));
        return -EIO;
    }
    
    lseek(fd, zi->indexoffset, SEEK_SET);

    cstate = av_malloc(zi->indexsize);
    res = read(fd, cstate, zi->indexsize);
    close(fd);
    if(res != zi->indexsize) {
        av_free(cstate);
        av_log(AVLOG_ERROR, "ZFILE: Error in indexfile %s", zc->indexfile);
        return -EIO;
    }

    res = zfile_uncompress_state(cstate, zi->indexsize, &state);
    av_free(cstate);
    if(res < 0)
        return res;

    res = inflateRestore(&fil->s, state);
    av_free(state);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateRestore: (%i)", res);
        return -EIO;
    }
    fil->iseof = 0;
    fil->calccrc = 0;

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
#endif

static int zfile_fill_inbuf(struct zfile *fil)
{
    avssize_t res;

    res = av_pread(fil->infile, fil->inbuf, INBUFSIZE,
                   fil->s.total_in + fil->dataoff);
    if(res < 0)
        return res;
    
    fil->s.next_in = (Bytef*)( fil->inbuf );
    fil->s.avail_in = res;

    return 0;
}

static int zfile_parse_gzip_header(struct zfile *fil, struct zfile_gzip_header_data *return_data)
{
    int res = 0;

    if (fil->s.avail_in < GZHEADER_SIZE) {
        res = zfile_fill_inbuf(fil);
        if(res < 0) {
            return res;
        }
    }

    if (fil->s.avail_in < GZHEADER_SIZE) {
        return -EIO;
    }

    const unsigned char *buf = fil->s.next_in;

    if (buf[0] != GZMAGIC1 || buf[1] != GZMAGIC2) {
        av_log(AVLOG_ERROR, "ZREAD: File not in GZIP format");
        return -EIO;
    }

    int method = buf[2];
    int flags = buf[3];
    avsize_t offset = GZHEADER_SIZE;

    if(method != METHOD_DEFLATE) {
        av_log(AVLOG_ERROR, "ZREAD: File compression is not DEFLATE");
        return -EIO;
    }

    if ((flags & GZFL_CONTINUATION) != 0) {
        if(fil->s.avail_in < offset + 2) {
            return -EIO;
        }

        offset += 2;
    }
    if ((flags & GZFL_EXTRA_FIELD) != 0) {
        if(fil->s.avail_in < offset + 2) {
            return -EIO;
        }

        avsize_t len = DBYTE(&buf[offset]);

        if(fil->s.avail_in < offset + 2 + len) {
            return -EIO;
        }

        offset += 2 + len;
    }
    if ((flags & GZFL_ORIG_NAME) != 0) {
        int c;

        do {
            if(fil->s.avail_in < offset + 1) {
                return -EIO;
            }

            c = buf[offset];
            offset++;
        } while (c != '\0');
    }

    if ((flags & GZFL_COMMENT) != 0) {
        int c;

        do {
            if(fil->s.avail_in < offset + 1) {
                return -EIO;
            }

            c = buf[offset];
            offset++;
        } while (c != '\0');
    }

    fil->s.total_in += offset;
    fil->s.avail_in -= offset;
    fil->s.next_in += offset;

    return 0;
}

static int zfile_parse_gzip_trailer(struct zfile *fil, struct zfile_gzip_trailer_data *return_data)
{
    if (fil->s.avail_in < 8) {
        int res = zfile_fill_inbuf(fil);
        if(res < 0) {
            return res;
        }
        if(fil->s.avail_in < 8) {
            return -EIO;
        }
    }

    // the last 8 bytes are the CRC and the uncompressed file size
    return_data->crc = QBYTE(fil->s.next_in);

    fil->s.total_in += 8;
    fil->s.avail_in -= 8;
    fil->s.next_in += 8;

    return 0;
}

static int zfile_reinit_state(struct zfile *fil)
{
    avoff_t total_in = fil->s.total_in;
    Bytef *next_out = fil->s.next_out;
    avoff_t avail_out = fil->s.avail_out;
    avoff_t total_out = fil->s.total_out;

    int res = inflateEnd(&fil->s);

    if (res != Z_OK) {
        return -EIO;
    }

    memset(&fil->s, 0, sizeof(z_stream));
    res = inflateInit2(&fil->s, -MAX_WBITS);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateInit: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        return -EIO;
    }

    fil->s.adler = 0;
    fil->s.total_in = total_in;
    fil->s.avail_in = 0;
    fil->s.next_out = next_out;
    fil->s.avail_out = avail_out;
    fil->s.total_out = total_out;
    fil->iseof = 0;

    struct zfile_gzip_header_data header_data;
    res = zfile_parse_gzip_header(fil, &header_data);
    if (res != 0) {
        av_log(AVLOG_ERROR, "gzip header error");
        return -EIO;
    }

    return 0;
}

static int zfile_inflate(struct zfile *fil, struct zcache *zc)
{
    int res;
    unsigned char *start;

    if(fil->s.avail_in == 0) {
        res = zfile_fill_inbuf(fil);
        if(res < 0)
            return res;
    }

    start = fil->s.next_out;
    res = inflate(&fil->s, Z_NO_FLUSH);
    if(fil->calccrc) {
        AV_LOCK(zread_lock);
        if(zc->crc_ok)
            fil->calccrc = 0;
        AV_UNLOCK(zread_lock);

        if(fil->calccrc)
            fil->s.adler = crc32(fil->s.adler, start, fil->s.next_out - start);
    }
    if(res == Z_STREAM_END) {
        fil->iseof = 1;

        if (fil->data_type == AV_ZFILE_DATA_GZIP_ENCAPSULATED) {
            struct zfile_gzip_trailer_data trailer_data;
            int parse_res = zfile_parse_gzip_trailer(fil, &trailer_data);
            if (parse_res != 0) {
                return -EIO;
            }
            if (fil->calccrc) {
                fil->crc = trailer_data.crc;
            }
        }
        if(fil->calccrc && fil->s.adler != fil->crc) {
            av_log(AVLOG_ERROR, "ZFILE: CRC error");
            return -EIO;
        }

        int crc_ok = 1;
        int cont = 0;

        // if data is gzip encapsulated and there is some data left,
        // reset deflate state and continue with next gzip member
        if (fil->data_type == AV_ZFILE_DATA_GZIP_ENCAPSULATED) {
            if (fil->s.avail_in > 3) {
                // check gzip method
                if (fil->s.next_in[2] == METHOD_DEFLATE) {
                    int reinit_res = zfile_reinit_state(fil);

                    if (reinit_res != 0) {
                        return reinit_res;
                    }

                    crc_ok = 0;
                    cont = 1;
                    res = Z_OK;
                }
            }
        }

        AV_LOCK(zread_lock);
        if(fil->calccrc)
            zc->crc_ok = crc_ok;
        zc->size = fil->s.total_out;
        AV_UNLOCK(zread_lock);

        if (!cont) {
            return 0;
        }
    }
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflate: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        return -EIO;
    }
    
    AV_LOCK(zread_lock);
#ifdef USE_SYSTEM_ZLIB
    res = 0;
#else
    if(fil->s.total_out >= zc->nextindex)
        res = zfile_save_index(fil, zc);
    else
        res = 0;
#endif
    AV_UNLOCK(zread_lock);
    if(res < 0)
        return res;

    return 0;
}


static int zfile_read(struct zfile *fil, struct zcache *zc, char *buf,
                      avsize_t nbyte)
{
    int res;

    fil->s.next_out = (Bytef*)buf;
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
        /* FIXME: Maybe cache some data as well */
        fil->s.next_out = (Bytef*)outbuf;
        fil->s.avail_out = AV_MIN(OUTBUFSIZE, offset - fil->s.total_out);

        res = zfile_inflate(fil, zc);
        if(res < 0)
            return res;
    }

    return 0;
}

#ifndef USE_SYSTEM_ZLIB
static int zfile_seek(struct zfile *fil, struct zcache *zc, avoff_t offset)
{
    struct zindex *zi;
    avoff_t curroff = fil->s.total_out;
    avoff_t zcdist;
    avoff_t scdist;
    avoff_t dist;

    if(offset >= curroff)
        dist = offset - curroff;
    else
        dist = -1;

    zi = zcache_find_index(zc, offset);
    if(zi != NULL)
        zcdist = offset - zi->offset + ZCACHE_EXTRA_DIST;
    else
        zcdist = offset;

    if(scache.id == zc->id && offset >= scache.s.total_out) {
        scdist = offset - scache.s.total_out;
        if((dist == -1 || scdist < dist) && scdist < zcdist) {
            z_stream tmp = fil->s;
            int tmpcc = fil->calccrc;
            int tmpiseof = fil->iseof;
            fil->s = scache.s;
            fil->s.avail_in = 0;
            fil->calccrc = scache.calccrc;
            fil->iseof = scache.iseof;
            scache.s = tmp;
            scache.calccrc = tmpcc;
            scache.iseof = tmpiseof;
            return 0;
        }
    }

    if(dist == -1 || zcdist < dist) {
        if(zi == NULL)
            return zfile_reset(fil);
        else
            return zfile_seek_index(fil, zc, zi);
    }
    
    return 0;
}
#endif

static int zfile_goto(struct zfile *fil, struct zcache *zc, avoff_t offset)
{
    int res;

    AV_LOCK(zc->lock);
    AV_LOCK(zread_lock);
#ifdef USE_SYSTEM_ZLIB
    if ( offset < fil->s.total_out ) {
        res = zfile_reset(fil);
    } else res = 0;
#else
    res = zfile_seek(fil, zc, offset);
#endif
    AV_UNLOCK(zread_lock);
    if(res == 0)
        res = zfile_skip_to(fil, zc, offset);
    AV_UNLOCK(zc->lock);

    return res;
}

static avssize_t av_zfile_do_pread(struct zfile *fil, struct zcache *zc,
                                   char *buf, avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    fil->id = zc->id;

    if(offset != fil->s.total_out) {
        res = zfile_goto(fil, zc, offset);
        if(res < 0)
            return res;
    }

    res = zfile_read(fil, zc, buf, nbyte);
    
    return res;
}

avssize_t av_zfile_pread(struct zfile *fil, struct zcache *zc, char *buf,
                         avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    if(fil->iserror)
        return -EIO;

    res = av_zfile_do_pread(fil, zc, buf, nbyte, offset);
    if(res < 0)
        fil->iserror = 1;

    return res;
}

int av_zfile_size(struct zfile *fil, struct zcache *zc, avoff_t *sizep)
{
    int res;
    avoff_t size;

    AV_LOCK(zread_lock);
    size = zc->size;
    AV_UNLOCK(zread_lock);

    if(size != -1 || fil == NULL) {
        *sizep = size;
        return 0;
    }

    fil->id = zc->id;

    res  = zfile_goto(fil, zc, AV_MAXOFF);
    if(res < 0)
        return res;
    
    AV_LOCK(zread_lock);
    size = zc->size;
    AV_UNLOCK(zread_lock);
    
    if(size == -1) {
        av_log(AVLOG_ERROR, "ZFILE: Internal error: could not find size");
        return -EIO;
    }
    
    *sizep = size;
    return 0;

}

static void zfile_destroy(struct zfile *fil)
{
    AV_LOCK(zread_lock);
    zfile_scache_save(fil->id, &fil->s, fil->calccrc, fil->iseof);
    AV_UNLOCK(zread_lock);
}

struct zfile *av_zfile_new(vfile *vf, avoff_t dataoff, avuint crc, enum av_zfile_data_type data_type)
{
    int res;
    struct zfile *fil;

    AV_NEW_OBJ(fil, zfile_destroy);
    fil->iseof = 0;
    fil->iserror = 0;
    fil->infile = vf;
    fil->dataoff = dataoff;
    fil->id = 0;
    fil->crc = crc;
    fil->calccrc = 1;
    fil->data_type = data_type;

    memset(&fil->s, 0, sizeof(z_stream));
    res = inflateInit2(&fil->s, -MAX_WBITS);
    if(res != Z_OK) {
        av_log(AVLOG_ERROR, "ZFILE: inflateInit: %s (%i)",
               fil->s.msg == NULL ? "" : fil->s.msg, res);
        fil->iserror = 1;
    }
    fil->s.adler = 0;

    if (fil->data_type == AV_ZFILE_DATA_GZIP_ENCAPSULATED) {
        struct zfile_gzip_header_data header_data;
        res = zfile_parse_gzip_header(fil, &header_data);
        if (res != 0) {
            av_log(AVLOG_ERROR, "gzip header error");
            fil->iserror = 1;
        }
    }

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
    zc->indexfile = NULL;
    zc->nextindex = INDEXDISTANCE;
    zc->indexes = NULL;
    zc->filesize = 0;
    zc->size = -1;
    zc->crc_ok = 0;
    AV_INITLOCK(zc->lock);

    AV_LOCK(zread_lock);
    if(zread_nextid == 0)
        zread_nextid = 1;

    zc->id = zread_nextid ++;
    AV_UNLOCK(zread_lock);
    
    av_get_tmpfile(&zc->indexfile);
    
    return zc;
}

avoff_t av_zcache_size(struct zcache *zc)
{
    return zc->filesize;
}
