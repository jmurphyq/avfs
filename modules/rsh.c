/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include "remote.h"
#include "runprog.h"
#include "parsels.h"
#include "filebuf.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define RSH_LIST_TIMEOUT 20000

struct rshlocalfile {
    char *tmpfile;
    struct program *pr;
    avoff_t currsize;
};

static void rsh_parse_line(struct lscache *lc, const char *line,
                           struct remdirlist *dl)
{
    int res;
    char *filename;
    char *linkname;
    struct avstat stbuf;

    res = av_parse_ls(lc, line, &stbuf, &filename, &linkname);
    if(res != 1)
        return;

    av_remote_add(dl, filename, linkname, &stbuf);

    av_free(filename);
    av_free(linkname);
}

static int rsh_read_list(struct program *pr, struct lscache *lc,
                         struct remdirlist *dl)
{
    int res;

    while(1) {
        char *line;

        res = av_program_getline(pr, &line, RSH_LIST_TIMEOUT);
        if(res < 0)
            return res;
        if(res == 0) {
            av_log(AVLOG_ERROR, "RSH: timeout");
            return -EIO;
        }
        if(line == NULL)
            return 0;

        rsh_parse_line(lc, line, dl);
        av_free(line);
    }
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
    char *newname = (char *) av_malloc(strlen(name) * 2 + 1);
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

static int rsh_list(struct remote *rem, struct remdirlist *dl)
{
    int res;
    struct program *pr;
    const char *prog[6];
    char *escaped_path;

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
  
    res = av_start_program(prog, &pr);
    if(res == 0) {
        struct lscache *lc = av_new_lscache();
        res = rsh_read_list(pr, lc, dl);
        av_unref_obj(lc);
        av_unref_obj(pr);
    }
    av_free(escaped_path);

    return res;
}

static void rsh_free_localfile(struct rshlocalfile *lf)
{
    
    if(lf->pr != NULL) {
        av_program_log_output(lf->pr);
        av_unref_obj(lf->pr);
    }
}

static int rsh_get(struct remote *rem, struct remgetparam *gp)
{
    int res;
    struct rshlocalfile *lf;
    char *tmpfile;
    char *codedpath;
    char *path;
    const char *prog[4];

    res = av_get_tmpfile(&tmpfile);
    if(res < 0)
	return res;

    AV_NEW_OBJ(lf, rsh_free_localfile);
    lf->pr = NULL;

    codedpath = rsh_code_name(gp->hostpath.path);
    path = av_stradd(NULL, gp->hostpath.host, ":", codedpath, NULL);
    av_free(codedpath);

    lf->tmpfile = tmpfile;

    prog[0] = "rcp";
    prog[1] = path;
    prog[2] = lf->tmpfile;
    prog[3] = NULL;

    res = av_start_program(prog, &lf->pr);
    av_free(path);
    if(res < 0) {
        av_unref_obj(lf);
        av_del_tmpfile(tmpfile);
        return res;
    }

    lf->currsize = 0;

    gp->data = lf;
    gp->localname = lf->tmpfile;

    return 0;
}

static int rsh_wait(struct remote *rem, void *data, avoff_t end)
{
    int res;
    struct rshlocalfile *lf = (struct rshlocalfile *) data;

    /* FIXME: timeout? */
    do {
        struct stat stbuf;
        
        res = av_program_log_output(lf->pr);
        if(res <= 0)
            return res;

        res = stat(lf->tmpfile, &stbuf);
        if(res == 0)
            lf->currsize = stbuf.st_size;

        if(lf->currsize < end)
            av_sleep(250);

    } while(lf->currsize < end);

    return 1;
}

static void rsh_destroy(struct remote *rem)
{
    av_free(rem->name);
    av_free(rem);
}

extern int av_init_module_rsh(struct vmodule *module);

int av_init_module_rsh(struct vmodule *module)
{
    int res;
    struct remote *rem;
    struct avfs *avfs;

    AV_NEW(rem);

    rem->data    = NULL;
    rem->name    = av_strdup("rsh");
    rem->list    = rsh_list;
    rem->get     = rsh_get;
    rem->wait    = rsh_wait;
    rem->destroy = rsh_destroy;
    
    res = av_remote_init(module, rem, &avfs);

    return res;
}
