/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    EXTFS module

    This module is partly based on the 'extfs.c' module of 
    Midnight Commander VFS, by Jakub Jelinek and Pavel Machek.
*/
#if 0
#include "archives.h"
#include "filebuf.h"
#include "parsels.h"

static int fill_extfs_link(ave *v, arch_entry *ent, char *linkname)
{
    arch_entry *entlink;
    archive *arch = ent->arch;
    
    entlink = av_find_entry(DUMMYV, arch->root, linkname, FIND_POSITIVE, 0);
    if(entlink == AVNULL) {
        av_log(AVLOG_WARNING, "EXTFS: Illegal hard link: %s", linkname);
        return 0; 
    }
    ent->ino = entlink->ino;

    av_link_inode(ent);
    av_unref_entry(entlink);
    
    return 0;
}

static int fill_extfs_node(ave *v, arch_entry *ent, struct avstat *stbuf,
                            char *path, char **linknamep)
{
    int res;
    char *s;
    char *fullpath;
    
    stbuf->ino = ent->arch->inoctr++;
    res = av_new_inode(v, ent, stbuf);
    if(res == -1)
        return -1;
    
    /* Fullpath should be without leading slashes */
    for(s = path; *s && *s == DIR_SEP_CHAR; s++);
    fullpath = av_strdup(v, s);
    if(fullpath == AVNULL)
        return -1;
    
    ent->ino->udata = (void *) fullpath;
    
    if(AV_ISLNK(stbuf->mode)) {
        ent->ino->syml = *linknamep;
        *linknamep = AVNULL; /* Used it, don't free it */
    }
    
    return 0;
}

static int fill_extfs_entry(ave *v, arch_entry *ent, struct avstat *stbuf,
                            char *path, char **linknamep)
{
    int res;

    if(ent->ino != AVNULL) {
        if(!AV_ISDIR(ent->ino->st.mode))
            av_log(AVLOG_WARNING, "EXTFS: Duplicated file %s", path);

        return 0;
    }

    if(*linknamep != AVNULL && !AV_ISLNK(stbuf->mode)) 
        res = fill_extfs_link(v, ent, *linknamep);
    else
        res = fill_extfs_node(v, ent, stbuf, path, linknamep);

    return res;
}

static int insert_extfs_entry(ave *v, archive *arch, struct avstat *stbuf,
			      char *path, char **linknamep)
{
    int res;
    arch_entry *ent;

    if(!path[0]) {
        /* Probably impossible, but checking doesn't hurt. */
        v->errn = EIO;
        return -1;
    }

    ent = av_find_entry(v, arch->root, path, FIND_CREATE, 0);
    if(ent == AVNULL)
        return -1;

    res = fill_extfs_entry(v, ent, stbuf, path, linknamep);
    av_unref_entry(ent);

    return res;
}

static int read_extfs_list(ave *v, filebuf *fb, archive *arch)
{
    struct lscache *lc;
    char *line;
    char *filename;
    char *linkname;
    struct avstat stbuf;
    int res = 0;

    lc = av_init_lscache(v);

    while((line = av_filebuf_readline(v, fb)) != AVNULL) {
        res = av_parse_ls(v, line, &stbuf, &filename, &linkname, lc);

        if(res == 1) {
      
            res = insert_extfs_entry(v, arch, &stbuf, filename, &linkname);
            av_free(filename);
            av_free(linkname);
        }

        if(res == -1) break;
    }
  
    av_free(lc);

    if(res == -1) return -1;
    return 0;
}

static int extfs_list(ave *v, vpath *path, archive *arch)
{
    real_file *rf = AVNULL;
    struct proginfo pri;
    const char *prog[4];
    int pipeout[2];
    filebuf *fb;
    int res;

    if(PARAM(path)[0]) {
        v->errn = ENOENT;
        return -1;
    }

    if(av_pipe(v, pipeout) == -1) return -1;

    if(!(arch->dd->flags & DEVF_NOFILE)) {
        rf = av_get_realfile(v, BASE(path));
        if(rf == AVNULL) {
            av_localclose(DUMMYV, pipeout[0]);
            av_localclose(DUMMYV, pipeout[1]);
            return -1;
        }
    }

    av_registerfd(pipeout[0]);

    av_init_proginfo(&pri);

    prog[0] = (char *) arch->dd->udata; /* Program name */
    prog[1] = "list";
    prog[2] = rf == AVNULL ? AVNULL : rf->name;
    prog[3] = AVNULL;
  
    pri.prog = prog;

    pri.ifd = av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri.ofd = pipeout[1];
    pri.efd = av_get_logfile(v);

    res = av_start_prog(v, &pri);
    av_localclose(DUMMYV, pri.ifd);
    av_localclose(DUMMYV, pri.ofd);
    av_localclose(DUMMYV, pri.efd);

    if(res != -1) {
        fb = av_filebuf_new(v, pipeout[0]);
        if(fb != AVNULL) {
            res = read_extfs_list(v, fb, arch);
            av_filebuf_free(fb);
        }
        else res = -1;
        av_wait_prog(DUMMYV, &pri, 1, 0);
    }

    av_localclose(DUMMYV, pipeout[0]);
    av_free_realfile(rf);

    return res;
}

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
    if(destfile == AVNULL) return -1;

    if(!(dd->flags & DEVF_NOFILE)) {
        basefile = av_get_realfile(v, BASE(path));
        if(basefile == AVNULL) {
            av_del_tmpfile(destfile);
            return -1;
        }
    }
    else basefile = AVNULL;
  
    prog[0] = (char *) dd->udata; /* Program name */
    prog[1] = "copyout";
    prog[2] = basefile == AVNULL ? "/" : basefile->name;
    prog[3] = (char *) di->ino->udata; /* The full path */
    prog[4] = destfile;
    prog[5] = AVNULL;
  
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


static void *extfs_open(ave *v, vpath *path, int flags, int mode)
{
    arch_fdi *di;
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    int res;

    di = (arch_fdi *) (*dd->vdev->open)(v, path, flags, mode);
    if(di == AVNULL) return AVNULL;

    if(AV_ISDIR(di->ino->st.mode)) return di;

    res = get_extfs_file(v, path, di);
    if(res == -1) {
        (*dd->vdev->close)(v, (void *) di);
        return AVNULL;
    }
  
    return (void *) di;
}

static avssize_t simple_read(ave *v, void *devinfo, char *buf, avsize_t nbyte)
{
    arch_fdi *di = (arch_fdi *) devinfo;

    if(AV_ISDIR(di->ino->st.mode)) {
        v->errn = EISDIR;
        return -1;
    }

    return av_localread(v, di->file.fh, buf, nbyte);
}


static avoff_t simple_lseek(ave *v, void *devinfo, avoff_t offset, int whence)
{
    arch_fdi *di = (arch_fdi *)devinfo;

    if(AV_ISDIR(di->ino->st.mode))
        return av_generic_lseek(v, &di->ptr, di->size, offset, whence);
    else
        return av_locallseek(v, di->file.fh, offset, whence);
}



static char *add_dir_file(ave *v, const char *dir, const char *file)
{
    return av_strconcat(v, dir, "/", file, AVNULL);
}

static struct ext_info *create_exts(ave *v, char *line)
{
    struct ext_info *exts;
    char *elist, *newelist;
    int i, n;
  
    while(*line && !av_isspace((int) *line)) line++;
    if(*line) *line++ = '\0';
    while(av_isspace((int) *line)) line++;
    elist = line;

    for(n = 0; *line && *line != '#'; n++) {
        while(*line && !av_isspace((int) *line)) line++;
        while(av_isspace((int) *line)) line++;
    }
    if(!n) return AVNULL;  /* No extensions */
  
    exts = av_malloc(v, (n + 1) * sizeof(*exts) + av_strlen(elist) + 1);
    if(exts == AVNULL) return AVNULL;

    newelist = (char *) (&exts[n+1]);
    av_strcpy(newelist, elist);
  
    for(i = 0; i < n; i++) {
        exts[i].from = newelist;
        exts[i].to   = AVNULL;
        while(*newelist && !av_isspace((int) *newelist)) newelist++;
        if(*newelist) *newelist++ = '\0';
        while(av_isspace((int) *newelist)) newelist++;

    }
    exts[n].from = AVNULL;
    exts[n].to   = AVNULL;

    return exts;
}

static int create_extfs_handler(ave *v, const char *extfs_dir, char *name)
{
    struct vdev_info *vdev;
    arch_devd *dd;
    char *progpath;
    struct ext_info *extlist;
    int need_archive;
    int end;

    /* Creates extension list, and strips name of the extensions */
    extlist = create_exts(v, name);
    end = av_strlen(name) - 1;

    if(name[end] == ':') {
        need_archive = 0;
        name[end] = '\0';
    }
    else need_archive = 1;

    vdev = av_init_arch(v, name, extlist, AV_VER);
    av_free(extlist);
    if(vdev == AVNULL) return -1;
  
    dd = (arch_devd *) vdev->devdata;
    dd->parsefunc = extfs_list;
    if(!need_archive) dd->flags |= DEVF_NOFILE;

    vdev->open  = extfs_open;
    vdev->read  = simple_read;
    vdev->lseek = simple_lseek;

    progpath = add_dir_file(v, extfs_dir, name);
    if(progpath == AVNULL) {
        av_destroy_vdev(vdev);
        return -1;
    }
    dd->udata = (void *) progpath;

    return av_add_vdev(v, vdev);
}

static int extfs_init(ave *v)
{
    char *moddir, *extfs_dir, *extfs_conf;
    filebuf *fb;
    int fd;
    int res;
    char *line;
    char *c;

    moddir = av_get_config(v, "moduledir");
    if(moddir == AVNULL) return -1;

    extfs_dir = add_dir_file(v, moddir, "extfs");
    av_free(moddir);
    if(extfs_dir == AVNULL) return -1;

    extfs_conf = add_dir_file(v, extfs_dir, "extfs.ini");
    if(extfs_conf == AVNULL) {
        av_free(extfs_dir);
        return -1;
    }

    fd = av_localopen(v, extfs_conf, AVO_RDONLY, 0);
    av_free(extfs_conf);
    if(fd == -1) {
        av_log(AVLOG_WARNING, "Could not open extfs config file, %s", 
                 extfs_conf);
        av_free(extfs_dir);
        return -1;
    }
  
    fb = av_filebuf_new(v, fd);
    if(fb == AVNULL) {
        av_free(extfs_dir);
        av_localclose(DUMMYV, fd);
    }

    res = 0;
    while((line = av_filebuf_readline(v, fb)) != AVNULL) {
        if (*line != '#') {
            c = line + av_strlen(line) - 1;
            if(*c == '\n') *c-- = '\0';

            if(*line) 
                res = create_extfs_handler(v, extfs_dir, line);
        }
        av_free(line);
        if(res == -1) break;
    }

    av_localclose(DUMMYV, fb->fd);
    av_filebuf_free(fb);
    av_free(extfs_dir);
    return res;
}


extern int av_init_module_extfs(ave *v);

int av_init_module_extfs(ave *v)
{
    return extfs_init(v);
}
#endif
