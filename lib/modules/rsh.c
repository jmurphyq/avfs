/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "remote.h"
#include "prog.h"
#include "parsels.h"
#include "filebuf.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define RSH_LIST_TIMEOUT 20000

struct rshlocalfile {
    int running;
    char *tmpfile;
    struct proginfo pri;
    const char *prog[4];
    int errfd;
    char *path;
    struct filebuf *errfb;
    avoff_t currsize;
};

static void rsh_parse_line(struct lscache *lc, const char *line,
                           struct dirlist *dl)
{
    int res;
    char *filename;
    char *linkname;
    struct avstat stbuf;

    res = __av_parse_ls(lc, line, &stbuf, &filename, &linkname);
    if(res != 1)
        return;

    __av_remote_add(dl, filename, linkname, &stbuf);

    __av_free(filename);
    __av_free(linkname);
}


static int rsh_read_list(struct filebuf *outfb, struct filebuf *errfb,
                         struct dirlist *dl)
{
    int res = 0;
    struct lscache *lc;
    struct filebuf *fbs[2];

    fbs[0] = outfb;
    fbs[1] = errfb;

    lc = __av_new_lscache();
    while(!__av_filebuf_eof(outfb) || !__av_filebuf_eof(errfb)) {
        char *line;

        res = __av_filebuf_check(fbs, 2, RSH_LIST_TIMEOUT);
        if(res < 0)
            break;
        if(res == 0) {
            __av_log(AVLOG_ERROR, "rsh read list: timeout");
            res = -EIO;
            break;
        }
        res = 0;

        while((res = __av_filebuf_readline(outfb, &line)) == 1) {
            rsh_parse_line(lc, line, dl);
            __av_free(line);
        }
        if(res < 0)
            break;

        while((res = __av_filebuf_readline(errfb, &line)) == 1) {
            __av_log(AVLOG_WARNING, "rsh stderr: %s", line);
            __av_free(line);
        }
        if(res < 0)
            break;

    }
    __av_unref_obj(lc);

    return res;
}

static int rsh_isspecial(int c)
{
    const char *normchars = "/.~@#%^-_=+:";

    if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || 
       (c >= 'a' && c <= 'z') || c >= 160 || strchr(normchars, c) != NULL)
        return 0;
    else
        return 1;
}

static char *rsh_code_name(const char *name)
{
    char *newname = (char *) __av_malloc(strlen(name) * 2 + 1);
    const char *s;
    char *d;
    
    for(s = name, d = newname; *s != '\0'; s++, d++) {
        if(rsh_isspecial((unsigned char) *s))
            *d++ = '\\';
        
        *d = *s;
    }
    *d = '\0';
    
    return newname;
}

static int rsh_list(struct remote *rem, struct dirlist *dl)
{
    int res;
    struct proginfo pri;
    const char *prog[6];
    int pipeout[2];
    int pipeerr[2];
    char *escaped_path;

    res = pipe(pipeout);
    if(res == -1)
        return -errno;

    res = pipe(pipeerr);
    if(res == -1) {
        res = -errno;
        close(pipeout[0]);
        close(pipeout[1]);
        return res;
    }

    __av_registerfd(pipeout[0]);
    __av_registerfd(pipeerr[0]);

    escaped_path = rsh_code_name(dl->hostpath.path);

    prog[0] = "rsh";
    prog[1] = dl->hostpath.host;
    prog[2] = "/bin/ls";
    if((dl->flags & REM_LIST_SINGLE) != 0)
        prog[3] = "-ldn";
    else
        prog[3] = "-lan";
    prog[4] = escaped_path;
    prog[5] = NULL;
  
    __av_init_proginfo(&pri);
    pri.prog = prog;

    pri.ifd = open("/dev/null", 0);
    pri.ofd = pipeout[1];
    pri.efd = pipeerr[1];

    res = __av_start_prog(&pri);
    close(pri.ifd);
    close(pri.ofd);
    close(pri.efd);

    if(res == 0) {
        struct filebuf *outfb = __av_filebuf_new(pipeout[0], 1);
        struct filebuf *errfb = __av_filebuf_new(pipeerr[0], 1);

        res = rsh_read_list(outfb, errfb, dl);
        if(res >= 0)
            res = __av_wait_prog(&pri, 0, 0);
        else
            __av_wait_prog(&pri, 1, 0);

        __av_unref_obj(outfb);
        __av_unref_obj(errfb);
    }

    close(pipeout[0]);
    close(pipeerr[0]);

    __av_free(escaped_path);

    if(res < 0)
        return res;

    return 0;
}


static void rsh_check_error(struct filebuf *errfb)
{
    if(!__av_filebuf_eof(errfb)) {
        int res;
        
        res = __av_filebuf_check(&errfb, 1, 0);
        if(res == 1) {
            char *line;
            while(__av_filebuf_readline(errfb, &line) == 1) {
                __av_log(AVLOG_WARNING, "rcp output: %s", line);
                __av_free(line);
            }
        }
    }
}

static void rsh_free_localfile(struct rshlocalfile *lf)
{
    if(lf->running) {
        rsh_check_error(lf->errfb);
        __av_wait_prog(&lf->pri, 1, 0);
    }
    __av_unref_obj(lf->errfb);
    close(lf->errfd);
    __av_free(lf->path);
}

static int rsh_get(struct remote *rem, struct getparam *gp)
{
    int res;
    struct rshlocalfile *lf;
    char *tmpfile;
    int pipeerr[2];
    char *codedpath;

    res = pipe(pipeerr);
    if(res == -1)
        return -errno;

    res = __av_get_tmpfile(&tmpfile);
    if(res < 0) {
        close(pipeerr[0]);
        close(pipeerr[1]);
	return res;
    }

    __av_registerfd(pipeerr[0]);

    AV_NEW_OBJ(lf, rsh_free_localfile);

    __av_init_proginfo(&lf->pri);

    codedpath = rsh_code_name(gp->hostpath.path);
    
    lf->path = __av_stradd(NULL, gp->hostpath.host, ":", codedpath, NULL);
    __av_free(codedpath);

    lf->errfd = pipeerr[0];
    lf->tmpfile = tmpfile;

    lf->prog[0] = "rcp";
    lf->prog[1] = lf->path;
    lf->prog[2] = lf->tmpfile;
    lf->prog[3] = NULL;
  
    lf->pri.prog = lf->prog;

    lf->pri.ifd = open("/dev/null", 0);
    lf->pri.ofd = pipeerr[1];
    lf->pri.efd = lf->pri.ofd;

    res = __av_start_prog(&lf->pri);
    close(lf->pri.ifd);
    close(lf->pri.ofd);

    if(res < 0) { 
        close(lf->errfd);
        __av_free(lf->path);
        __av_del_tmpfile(lf->tmpfile);
        return res;
    }
    
    lf->errfb = __av_filebuf_new(lf->errfd, 1);
    lf->currsize = 0;
    lf->running = 1;

    gp->data = lf;
    gp->localname = lf->tmpfile;

    return 0;
}


static int rsh_wait(struct remote *rem, void *data, avoff_t end)
{
    int res;
    struct rshlocalfile *lf = (struct rshlocalfile *) data;

    /* FIXME: timeout */
    do {
        struct stat stbuf;
        
        rsh_check_error(lf->errfb);

        res = __av_wait_prog(&lf->pri, 0, 1);
        if(res < 0)
            return res;

        if(res == 1) {
            lf->running = 0;
            return 0;
        }

        res = stat(lf->tmpfile, &stbuf);
        if(res == 0)
            lf->currsize = stbuf.st_size;

        if(lf->currsize < end)
            __av_sleep(250);

    } while(lf->currsize < end);

    return 1;
}


static void rsh_destroy(struct remote *rem)
{
    __av_free(rem->name);
    __av_free(rem);
}

extern int __av_init_module_rsh(struct vmodule *module);

int __av_init_module_rsh(struct vmodule *module)
{
    int res;
    struct remote *rem;
    struct avfs *avfs;

    AV_NEW(rem);

    rem->data    = NULL;
    rem->name    = __av_strdup("rsh");
    rem->list    = rsh_list;
    rem->get     = rsh_get;
    rem->wait    = rsh_wait;
    rem->destroy = rsh_destroy;
    
    res = __av_remote_init(module, rem, &avfs);

    return res;
}

