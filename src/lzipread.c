/*
    AVFS: A Virtual File System Library
    Copyright (C) 2021 Ralf Hoffmann <ralf@boomerangsworld.de>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    based on zstdread.c
*/

#include "config.h"
#include "lzipfile.h"
#include "oper.h"
#include "exit.h"

#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <lzlib.h>

#define INBUFSIZE 16384
#define OUTBUFSIZE 32768

static int lzipread_nextid;
static AV_LOCK_DECL(lzipread_lock);

struct lzipcache {
    int id;
    avoff_t size;
};

struct lzipfile {
    struct LZ_Decoder *decoder;
    int iseof;
    int iserror;
    int id; /* The id of the last used lzipcache */
    
    vfile *infile;

    avoff_t total_in;
    avoff_t total_out;

    char *outbuf;
    size_t outbuf_size;
    size_t output_pos;
};

static void lzip_delete_decoder(struct LZ_Decoder *decoder)
{
    if(decoder != NULL) {
        LZ_decompress_close(decoder);
    }
}

static int lzip_new_decoder(struct LZ_Decoder **resp)
{
    struct LZ_Decoder *decoder;

    decoder = LZ_decompress_open();

    if(decoder == NULL) {
        *resp = NULL;
        av_log(AVLOG_ERROR, "LZIP: could not create decompress decoder");
        return -EIO;
    }

    *resp = decoder;
    return 0;
}

static int lzipfile_reset(struct lzipfile *fil)
{
    lzip_delete_decoder(fil->decoder);

    fil->iseof = 0;
    fil->iserror = 0;
    fil->total_in = fil->total_out = 0;
    return lzip_new_decoder(&fil->decoder);
}

static int lzipfile_fill_inbuf(struct lzipfile *fil)
{
    avssize_t res;
    char buf[INBUFSIZE];
    int ret;
    int size = AV_MIN(sizeof(buf), LZ_decompress_write_size(fil->decoder));

    if (size <= 0) {
        return 0;
    }
    
    res = av_pread(fil->infile, buf, size, fil->total_in);
    if(res < 0)
        return res;

    ret = LZ_decompress_write(fil->decoder, (const uint8_t*)buf, res);
    if ( ret < 0 ) {
        return ret;
    }

    fil->total_in += ret;

    if ( res == 0 ) {
        LZ_decompress_finish(fil->decoder);
    }
    
    return 0;
}

static int lzipfile_decompress(struct lzipfile *fil, struct lzipcache *zc)
{
    int res;
    int ret;

    if (fil->outbuf_size == 0) return 0;
    
    for (;;) {
        res = lzipfile_fill_inbuf(fil);
        if(res < 0)
            return res;

        ret = LZ_decompress_read(fil->decoder, (uint8_t*)fil->outbuf + fil->output_pos, fil->outbuf_size - fil->output_pos);
        if( ret < 0 ) {
            av_log(AVLOG_ERROR, "LZIP: decompress error");
            return -EIO;
        }

        fil->total_out += ret;
        fil->output_pos += ret;

        if (ret == 0) {
            if (LZ_decompress_total_in_size(fil->decoder) == fil->total_in) {
                fil->iseof = 1;
                AV_LOCK(lzipread_lock);
                zc->size = fil->total_out;
                AV_UNLOCK(lzipread_lock);
                break;
            }
        }

        if (fil->output_pos == fil->outbuf_size) {
            // everything we are requested for is available
            break;
        }
    }

    return 0;
}


static int lzipfile_read(struct lzipfile *fil, struct lzipcache *zc, char *buf,
                         avsize_t nbyte)
{
    int res;
    int sum = 0;

    while (nbyte > 0 && !fil->iseof) {
        fil->outbuf = buf;
        fil->outbuf_size = nbyte;
        fil->output_pos = 0;

        res = lzipfile_decompress(fil, zc);
        if(res < 0)
            return res;

        if (fil->output_pos == 0) {
            fil->iseof = 1;
        } else {
            buf += fil->output_pos;
            nbyte -= fil->output_pos;
            sum += fil->output_pos;
        }
    }

    return sum;
}

static int lzipfile_skip_to(struct lzipfile *fil, struct lzipcache *zc,
                            avoff_t offset)
{
    int res;
    char outbuf[OUTBUFSIZE];
    
    while(!fil->iseof) {
        avoff_t curroff = fil->total_out;

        if(curroff == offset)
            break;

        fil->outbuf = outbuf;
        fil->outbuf_size = AV_MIN(OUTBUFSIZE, offset - curroff);;
        fil->output_pos = 0;

        res = lzipfile_decompress(fil, zc);
        if(res < 0)
            return res;

        if (fil->output_pos == 0) {
            fil->iseof = 1;
        }
    }

    return 0;
}

static avssize_t av_lzipfile_do_pread(struct lzipfile *fil, struct lzipcache *zc,
                                      char *buf, avsize_t nbyte, avoff_t offset)
{
    avssize_t res;
    avoff_t curroff;

    fil->id = zc->id;

    curroff = fil->total_out;
    if(offset != curroff) {
        AV_LOCK(lzipread_lock);
        if ( curroff > offset ) {
            res = lzipfile_reset( fil );
        } else {
            res = 0;
        }
        AV_UNLOCK(lzipread_lock);
        if(res < 0)
            return res;

        res = lzipfile_skip_to(fil, zc, offset);
        if(res < 0)
            return res;
    }

    res = lzipfile_read(fil, zc, buf, nbyte);
    
    return res;
}

avssize_t av_lzipfile_pread(struct lzipfile *fil, struct lzipcache *zc, char *buf,
                            avsize_t nbyte, avoff_t offset)
{
    avssize_t res;

    if(fil->iserror)
        return -EIO;

    res = av_lzipfile_do_pread(fil, zc, buf, nbyte, offset);
    if(res < 0)
        fil->iserror = 1;

    return res;
}

int av_lzipfile_size(struct lzipfile *fil, struct lzipcache *zc, avoff_t *sizep)
{
    int res;
    avoff_t size;

    AV_LOCK(lzipread_lock);
    size = zc->size;
    AV_UNLOCK(lzipread_lock);

    if(size != -1 || fil == NULL) {
        *sizep = size;
        return 0;
    }

    fil->id = zc->id;

    AV_LOCK(lzipread_lock);
    res = lzipfile_reset( fil );
    AV_UNLOCK(lzipread_lock);
    if(res < 0)
        return res;

    res = lzipfile_skip_to(fil, zc, AV_MAXOFF);
    if(res < 0)
        return res;
    
    AV_LOCK(lzipread_lock);
    size = zc->size;
    AV_UNLOCK(lzipread_lock);
    
    if(size == -1) {
        av_log(AVLOG_ERROR, "LZIP: Internal error: could not find size");
        return -EIO;
    }
    
    *sizep = size;
    return 0;
}

static void lzipfile_destroy(struct lzipfile *fil)
{
    AV_LOCK(lzipread_lock);
    lzip_delete_decoder(fil->decoder);
    AV_UNLOCK(lzipread_lock);
}

struct lzipfile *av_lzipfile_new(vfile *vf)
{
    int res;
    struct lzipfile *fil;

    AV_NEW_OBJ(fil, lzipfile_destroy);
    fil->iseof = 0;
    fil->iserror = 0;
    fil->infile = vf;
    fil->id = 0;
    fil->total_in = fil->total_out = 0;

    res = lzip_new_decoder(&fil->decoder);
    if(res < 0)
        fil->iserror = 1;

    return fil;
}

static void lzipcache_destroy(struct lzipcache *zc)
{
}

struct lzipcache *av_lzipcache_new()
{
    struct lzipcache *zc;

    AV_NEW_OBJ(zc, lzipcache_destroy);
    zc->size = -1;

    AV_LOCK(lzipread_lock);
    if(lzipread_nextid == 0)
        lzipread_nextid = 1;

    zc->id = lzipread_nextid++;
    AV_UNLOCK(lzipread_lock);
    
    return zc;
}
