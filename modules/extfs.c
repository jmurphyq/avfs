/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    EXTFS module

    This module is partly based on the 'extfs.c' module of 
    Midnight Commander VFS, by Jakub Jelinek and Pavel Machek.
*/

#include "archive.h"
#include "version.h"
#include "filebuf.h"
#include "parsels.h"
#include "realfile.h"
#include "prog.h"

#include <unistd.h>
#include <fcntl.h>

struct extfsdata {
    int needbase;
    char *progpath;
};

struct extfsnode {
    char *fullpath;
};

static void fill_extfs_link(struct archive *arch, struct entry *ent,
                           char *linkname)
{
    struct entry *link;
    struct archnode *nod = NULL;

    link = av_arch_resolve(arch, linkname, 0, 0);
    if(link != NULL)
        nod = (struct archnode *) av_namespace_get(link);

    if(nod == NULL || AV_ISDIR(nod->st.mode))
        av_log(AVLOG_WARNING, "EXTFS: Illegal hard link");
    else {
        nod->st.nlink ++;
        av_namespace_set(ent, nod);
        av_ref_obj(ent);
        av_ref_obj(nod);
    }

    av_unref_obj(link);
}

static void extfsnode_delete(struct extfsnode *enod)
{
    av_free(enod->fullpath);
}

static void fill_extfs_node(struct archive *arch, struct entry *ent, 
                            struct avstat *stbuf, char *path, char *linkname)
{
    struct archnode *nod;
    struct extfsnode *enod;
    char *s;

    nod = av_arch_new_node(arch, ent, AV_ISDIR(stbuf->mode));
        
    stbuf->dev = nod->st.dev;
    stbuf->ino = nod->st.ino;

    nod->st = *stbuf;
    nod->offset = 0;
    nod->realsize = 0;

    AV_NEW_OBJ(enod, extfsnode_delete);
    nod->data = enod;

    /* Fullpath should be without leading slashes */
    for(s = path; *s && *s == '/'; s++);
    enod->fullpath = av_strdup(s);

    if(AV_ISLNK(stbuf->mode))
        nod->linkname = av_strdup(linkname);
}


static void insert_extfs_entry(struct archive *arch, struct avstat *stbuf,
			      char *path, char *linkname)
{
    struct entry *ent;

    if(!path[0])
        return;

    ent = av_arch_create(arch, path, 0);
    if(ent == NULL)
        return;

    if(linkname != NULL && !AV_ISDIR(stbuf->mode)) 
        fill_extfs_link(arch, ent, linkname);
    else
        fill_extfs_node(arch, ent, stbuf, path, linkname);

    av_unref_obj(ent);
}

static void parse_extfs_line(struct lscache *lc, char *line,
                             struct archive *arch)
{
    int res;
    char *filename;
    char *linkname;
    struct avstat stbuf;
    
    res = av_parse_ls(lc, line, &stbuf, &filename, &linkname);
    if(res != 1)
        return;

    
    insert_extfs_entry(arch, &stbuf, filename, linkname);
    av_free(filename);
    av_free(linkname);
}

static int read_extfs_list(struct filebuf *fbs[2], struct archive *arch,
                           ventry *ve)
{
    int res = 0;
    struct lscache *lc;

    lc = av_new_lscache();

    while(!av_filebuf_eof(fbs[0]) || !av_filebuf_eof(fbs[1])) {
        char *line;

        /* FIXME: timeout? */
        res = av_filebuf_check(fbs, 2, -1);
        if(res < 0)
            break;

        while((res = av_filebuf_readline(fbs[0], &line)) == 1) {
            parse_extfs_line(lc, line, arch);
            av_free(line);
        }
        if(res < 0)
            break;

        while((res = av_filebuf_readline(fbs[1], &line)) == 1) {
            av_log(AVLOG_WARNING, "%s: stderr: %s", ve->mnt->avfs->name, line);
            av_free(line);
        }
        if(res < 0)
            break;
    }
  
    av_unref_obj(lc);

    if(res < 0)
        return res;

    return 0;
}

static int extfs_do_list(struct extfsdata *info, ventry *ve,
                         struct archive *arch, int pipeout[2], int pipeerr[2])

{
    int res;
    struct proginfo pri;
    struct realfile *rf;
    char *prog[4];
    struct filebuf *fbs[2];
    
    if(pipe(pipeout) == -1 || pipe(pipeerr) == -1)
        return -errno;

    av_registerfd(pipeout[0]);
    av_registerfd(pipeerr[0]);

    if(info->needbase) {
        res = av_get_realfile(ve->mnt->base, &rf);
        if(res < 0)
            return res;
    }
    else
        rf = NULL;
    
    av_init_proginfo(&pri);

    prog[0] = info->progpath;
    prog[1] = "list";
    prog[2] = rf == NULL ? NULL : rf->name;
    prog[3] = NULL;
  
    pri.prog = (const char **) prog;

    pri.ifd = open("/dev/null", O_RDONLY);
    pri.ofd = pipeout[1];
    pri.efd = pipeerr[1];
    pipeout[1] = -1;
    pipeerr[1] = -1;

    res = av_start_prog(&pri);
    close(pri.ifd);
    close(pri.ofd);
    close(pri.efd);

    if(res == 0) {
        fbs[0] = av_filebuf_new(pipeout[0], FILEBUF_NONBLOCK);
        fbs[1] = av_filebuf_new(pipeerr[0], FILEBUF_NONBLOCK);
        pipeout[0] = -1;
        pipeerr[0] = -1;
    
        res = read_extfs_list(fbs, arch, ve);
        av_unref_obj(fbs[0]);
        av_unref_obj(fbs[1]);
    
        if(res == 0)
            res = av_wait_prog(&pri, 0, 0);
        else
            av_wait_prog(&pri, 1, 0);
    }
    av_unref_obj(rf);

    return res;
}

static int extfs_list(void *data, ventry *ve, struct archive *arch)
{
    int res;
    int pipeout[2];
    int pipeerr[2];
    struct extfsdata *info = (struct extfsdata *) data;

    pipeerr[0] = -1;
    pipeerr[1] = -1;
    pipeout[0] = -1;
    pipeout[1] = -1;
    
    res = extfs_do_list(info, ve, arch, pipeout, pipeerr);

    close(pipeout[0]);
    close(pipeout[1]);
    close(pipeerr[0]);
    close(pipeerr[1]);

    return res;
}

#if 0
static int get_extfs_file(ave *v, vpath *path, arch_fdi *di)
{
    arch_devd *dd = di->arch->dd;
    struct proginfo pri;
    const char *prog[6];
    real_file *basefile;
    char *destfile;
    int res;

    if(di->file.fh != -1) av_close(DUMMYV, di->file.fh);
    di->offset = 0;
    di->size = di->ino->st.size;
  
    destfile = av_get_tmpfile(v);
    if(destfile == NULL) return -1;

    if(!(dd->flags & DEVF_NOFILE)) {
        basefile = av_get_realfile(v, BASE(path));
        if(basefile == NULL) {
            av_del_tmpfile(destfile);
            return -1;
        }
    }
    else basefile = NULL;
  
    prog[0] = (char *) dd->udata; /* Program name */
    prog[1] = "copyout";
    prog[2] = basefile == NULL ? "/" : basefile->name;
    prog[3] = (char *) di->ino->udata; /* The full path */
    prog[4] = destfile;
    prog[5] = NULL;
  
    av_init_proginfo(&pri);
    pri.prog = prog;
    pri.ifd = av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri.ofd = av_get_logfile(v);
    pri.efd = pri.ofd;

    res = av_start_prog(v, &pri);
 
    av_localclose(DUMMYV, pri.ifd);
    av_localclose(DUMMYV, pri.ofd);

    if(res != -1) {
        res = av_wait_prog(v, &pri, 0, 0);
        if(res == -1) 
            av_log(AVLOG_WARNING, "EXTFS: Prog %s returned with an error", 
                     prog[0]);
    }

    av_free_realfile(basefile);
    if(res != -1) {
        di->file.fh = av_localopen(v, destfile, AVO_RDONLY, 0);

        if(di->file.fh == -1) {
            av_log(AVLOG_WARNING, "EXTFS: Could not open destination file %s", 
                     destfile);
            res = -1;
        }
    }
    av_del_tmpfile(destfile);

    di->file.ptr = 0;

    return res;
}
#endif

static struct ext_info *create_exts(char *line)
{
    struct ext_info *exts;
    char *elist, *newelist;
    int i, n;
  
    while(*line && !isspace((int) *line)) line++;
    if(*line) *line++ = '\0';
    while(isspace((int) *line)) line++;
    elist = line;

    for(n = 0; *line && *line != '#'; n++) {
        while(*line && !isspace((int) *line)) line++;
        while(isspace((int) *line)) line++;
    }
    if(!n) return NULL;  /* No extensions */
  
    exts = av_malloc((n + 1) * sizeof(*exts) + strlen(elist) + 1);

    newelist = (char *) (&exts[n+1]);
    strcpy(newelist, elist);
  
    for(i = 0; i < n; i++) {
        exts[i].from = newelist;
        exts[i].to   = NULL;
        while(*newelist && !isspace((int) *newelist)) newelist++;
        if(*newelist) *newelist++ = '\0';
        while(isspace((int) *newelist)) newelist++;

    }
    exts[n].from = NULL;
    exts[n].to   = NULL;

    return exts;
}

static void extfsdata_delete(struct extfsdata *info)
{
    av_free(info->progpath);
}

static int create_extfs_handler(struct vmodule *module, const char *extfs_dir,
                                char *name)
{
    int res;
    struct avfs *avfs;
    struct archparams *ap;
    struct extfsdata *info;
    struct ext_info *extlist;
    int needbase;
    int end;

    /* Creates extension list, and strips name of the extensions */
    extlist = create_exts(name);
    end = strlen(name) - 1;

    if(name[end] == ':') {
        needbase = 0;
        name[end] = '\0';
    }
    else
        needbase = 1;

    res = av_archive_init(name, extlist, AV_VER, module, &avfs);
    av_free(extlist);
    if(res < 0)
        return res;

    ap = (struct archparams *) avfs->data;

    AV_NEW_OBJ(info, extfsdata_delete);
    ap->data = info;
    ap->parse = extfs_list;
#if 0
    ap->read = extfs_read;
    ap->release = extfs_release;
#endif

    if(!needbase)
        ap->flags |= ARF_NOBASE;
  
    info->progpath = av_stradd(NULL, extfs_dir, "/", name, NULL);
    info->needbase = needbase;
    
    av_add_avfs(avfs);

    return 0;
}

static int extfs_init(struct vmodule *module)
{
    char *extfs_dir, *extfs_conf;
    struct filebuf *fb;
    int fd;
    int res;
    char *line;
    char *c;

    extfs_dir = av_get_config("moduledir");
    extfs_dir = av_stradd(extfs_dir, "/extfs", NULL);
    extfs_conf = av_stradd(NULL, extfs_dir, "/extfs.ini", NULL);

    fd = open(extfs_conf, O_RDONLY);
    if(fd == -1) {
        res = -errno;
        av_log(AVLOG_WARNING, "Could not open extfs config file %s: %s", 
                 extfs_conf, strerror(errno));
        av_free(extfs_conf);
        av_free(extfs_dir);
        return res;
    }
    av_free(extfs_conf);
  
    fb = av_filebuf_new(fd, 0);

    while(1) {
        res = av_filebuf_getline(fb, &line, -1);
        if(res < 0 || line == NULL)
            break;

        if (*line != '#') {
            c = line + strlen(line) - 1;
            if(*c == '\n') *c-- = '\0';

            if(*line) 
                res = create_extfs_handler(module, extfs_dir, line);
        }
        av_free(line);
        if(res < 0) 
            break;
    }
    av_unref_obj(fb);
    av_free(extfs_dir);
    
    if(res < 0)
        return res;

    return 0;
}


extern int av_init_module_extfs(struct vmodule *module);

int av_init_module_extfs(struct vmodule *module)
{
    return extfs_init(module);
}
