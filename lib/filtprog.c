/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "filtprog.h"
#include "filebuf.h"
#include "prog.h"

#include <unistd.h>
#include <sys/time.h>

struct filtprog {
    vfile *vf;
    char **prog;
};

#define wbufsize 16384

struct filtconn {
    struct filtprog *fp;
    struct filebuf *fbs[3];
    struct proginfo pri;
    int wbufat;
    int wbuflen;
    char wbuf[wbufsize];
};

static int filtprog_fill_wbuf(struct filtconn *fc)
{
    avssize_t res;

    res = __av_read(fc->fp->vf, fc->wbuf, wbufsize);
    if(res < 0)
        return res;

    if(res == 0) {
        __av_unref_obj(fc->fbs[0]);
        fc->fbs[0] = NULL;
    }
    else {
        fc->wbuflen = res;
        fc->wbufat = 0;
    }

    return 0;
}

static int filtprog_check_error(struct filtconn *fc)
{
    char *line;
    int res;
    int gotsome = 0;

    do {
        res = __av_filebuf_readline(fc->fbs[2], &line);
        if(res < 0)
            return res;

        if(res == 1) {
            __av_log(AVLOG_ERROR, "%s stderr: %s", fc->fp->prog[0], line);
            __av_free(line);
            gotsome = 1;
        }
    } while(res == 1);

    return gotsome;
}

static int filtprog_write_input(struct filtconn *fc)
{
    int res;

    if(fc->wbuflen == 0) {
        res = filtprog_fill_wbuf(fc);
        if(res < 0)
            return res;

        if(fc->fbs[0] == NULL)
            return 0;
    }
    
    res = __av_filebuf_write(fc->fbs[0], fc->wbuf + fc->wbufat,
                             fc->wbuflen);
    if(res < 0)
        return res;
    
    fc->wbufat += res;
    fc->wbuflen -= res;

    return 0;
}

static avssize_t filtprog_read(void *data, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct filtconn *fc = (struct filtconn *) data;

    while(1) {
        res = filtprog_check_error(fc);
        if(res < 0)
            return res;

        if(res == 0) {
            res = __av_filebuf_read(fc->fbs[1], buf, nbyte);
            if(res != 0)
                return res;
            
            if(__av_filebuf_eof(fc->fbs[1])) {
                res = __av_wait_prog(&fc->pri, 0, 0);
                if(res < 0)
                    return res;

                return 0;
            }
            
            if(fc->fbs[0] != NULL) {
                res = filtprog_write_input(fc);
                if(res < 0)
                    return res;
            }
        }

        res = __av_filebuf_check(fc->fbs, 3, -1);
        if(res < 0)
            return res;
    }
}

static void filtprog_stop(struct filtconn *fc)
{
    __av_unref_obj(fc->fbs[0]);
    __av_unref_obj(fc->fbs[1]);   
    __av_unref_obj(fc->fbs[2]);
    __av_wait_prog(&fc->pri, 1, 0);
    __av_lseek(fc->fp->vf, 0, AVSEEK_SET);
}

static int filtprog_init_pipes(int pipein[2], int pipeout[2], int pipeerr[2])
{
    int res;

    pipein[0] = -1,  pipein[1] = -1;
    pipeout[0] = -1, pipeout[1] = -1;
    pipeerr[0] = -1, pipeerr[1] = -1;

    if(pipe(pipein) == -1 || pipe(pipeout) == -1 || pipe(pipeerr) == -1) {
        res = -errno;
        close(pipein[0]), close(pipein[1]);
        close(pipeout[0]), close(pipeout[1]);
        return res;
    }

    __av_registerfd(pipein[1]);
    __av_registerfd(pipeout[0]);
    __av_registerfd(pipeerr[0]);

    return 0;
}

static int filtprog_start(void *data, void **resp)
{
    struct filtprog *fp = (struct filtprog *) data;
    struct filtconn *fc;
    int res;
    int pipein[2];
    int pipeout[2];
    int pipeerr[2];
    struct proginfo pri;

    res = filtprog_init_pipes(pipein, pipeout, pipeerr);
    if(res < 0)
        return res;

    __av_init_proginfo(&pri);
    
    pri.prog = (const char **) fp->prog;
    pri.ifd = pipein[0];
    pri.ofd = pipeout[1];
    pri.efd = pipeerr[1];

    res = __av_start_prog(&pri);
    close(pri.ifd);
    close(pri.ofd);
    close(pri.efd);

    if(res < 0) {
       close(pipein[1]);
       close(pipeout[0]);
       close(pipeerr[0]);
       return res;
    }

    AV_NEW_OBJ(fc, filtprog_stop);
    
    fc->fp = fp;
    fc->fbs[0] = __av_filebuf_new(pipein[1], FILEBUF_NONBLOCK | FILEBUF_WRITE);
    fc->fbs[1] = __av_filebuf_new(pipeout[0], FILEBUF_NONBLOCK);
    fc->fbs[2] = __av_filebuf_new(pipeerr[0], FILEBUF_NONBLOCK);
    fc->pri = pri;
    fc->wbufat = 0;
    fc->wbuflen = 0;
    
    *resp = fc;

    return 0;
}


struct sfile *__av_filtprog_new(vfile *vf, char **prog)
{
    struct filtprog *fp;
    struct sfile *sf;
    static struct sfilefuncs func = {
        filtprog_start,
        filtprog_read
    };

    AV_NEW_OBJ(fp, NULL);
    fp->vf = vf;
    fp->prog = prog;

    sf = __av_sfile_new(&func, fp, 0);

    return sf;
}
