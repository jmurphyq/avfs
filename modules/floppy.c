/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    FLOPPY module (interface for mtools)
*/

#include "remote.h"

#if 0

#define FLOPPY_PARAM_SEP '-'

struct floppy_fdidat {
    struct proginfo pri;
    char *tmpname;
    char *dosname;
    int rdonly;
    int cursize;
    int error;
};

static void strip_spaces(const char *buf, int *ip)
{
    int i = *ip;
    while(av_isspace((int) buf[i])) i++;
    *ip = i;
}

static void strip_nonspace(const char *buf, int *ip)
{
    int i = *ip;
    while(!av_isspace((int) buf[i])) i++;
    *ip = i;
}

#if 0
static void conv_upper(char *s)
{
    for(; *s; s++) *s = toupper((int) *s);
}
#endif

static int get_num(const char *s, int *ip)
{
    int i;
    int num;
  
    i = *ip;
  
    if(s[i] < '0' || s[i] > '9') return -1;

    num = 0;
    for(;; i++) {
        if(s[i] >= '0' && s[i] <= '9') num = (num * 10) + (s[i] - '0');
        else if(s[i] != ',' && s[i] != '.') break;
    }
  
    *ip = i;
    return num;
}

static int conv_date(const char *s, struct avtm *tms)
{
    int num;
    int i;
  
    i = 0;

    if((num = get_num(s, &i)) == -1 || num < 1 || num > 12) return -1;
    tms->mon = num - 1;
    i++;
  
    if((num = get_num(s, &i)) == -1 || num < 1 || num > 31) return -1;
    tms->day = num;
    i++;

    if((num = get_num(s, &i)) == -1) return -1;
    if(num >= 80 && num < 100) num += 1900;
    else if(num >= 0 && num < 80) num += 2000;

    if(num < 1900) return -1;
    tms->year = num - 1900;

    return 0;
}


static int conv_time(const char *s, struct avtm *tms)
{
    int num;
    int i;
  
    i = 0;

    if((num = get_num(s, &i)) == -1 || num < 0) return -1;
    tms->hour = num;
    i++;
  
    if((num = get_num(s, &i)) == -1 || num < 0 || num > 59) return -1;
    tms->min = num;

    if(s[i] == ':') {
        i++;
        if((num = get_num(s, &i)) == -1 || num < 0 ||  num > 59) return -1;

        tms->sec = num;
    }
    else tms->sec = 0;
  
    if((s[i] == 'p' || s[i] == 'P') && tms->hour < 12) tms->hour += 12;
    if(tms->hour > 24) return -1;
    if(tms->hour == 24) tms->hour = 0;

    return 0;
}


static int process_dir_line(const char *buf, int vollabel, char *name,
                            struct avstat *st)
{
    int i, start;
    int namelen;
    struct avtm tms;

    i = 0;

    if(av_strncmp(buf, " Volume in drive ", 17) == 0 && 
       buf[17] && av_strncmp(buf+18, " is ", 4) == 0 && buf[22]) {
        if(vollabel) {
            i = 22;
            namelen = av_strlen(buf+i);
            while(av_isspace((int) buf[i+namelen-1])) namelen--;
            av_strcpy(name, ".vol-");
            av_strncpy(name+5, buf+i, namelen);
            name[namelen+5] = '\0';
            st->mode = AV_IFREG | 0444;
            st->size = 0;
            st->mtime = av_gettime();
        }
        else return -1;
    }
    else {
        strip_nonspace(buf, &i);
        if(!buf[i] || i == 0 || i > 8 || buf[0] == '.') return -1;
  
        namelen = i;
        av_strncpy(name, buf, namelen);
        name[namelen] = '\0';
    
        strip_spaces(buf, &i);
        if(i == 9) {
            int extlen;
      
            strip_nonspace(buf, &i);
            extlen = i - 9;
      
            if(extlen > 3) return -1;
      
            name[namelen++] = '.';
            av_strncpy(name+namelen, buf+9, extlen);
            namelen += extlen;
            name[namelen] = '\0';
      
            strip_spaces(buf, &i);
        }
#if 0
        /* Acording to new mtools */
        conv_lower(name);
#endif    

        if(!buf[i] || i < 13) return -1;
    
        start = i;
        strip_nonspace(buf, &i);
    
        if(av_strncmp("<DIR>", buf + start, i - start) == 0) {
            st->size = 0;
            st->mode = AV_IFDIR | 0777;
        }
        else {
            int size;
            if((size = get_num(buf, &start)) == -1) return -1;
            st->size = size;
            st->mode = AV_IFREG | 0666;
        }
        strip_spaces(buf, &i);
        if(!buf[i]) return -1;
    
        start = i;
        strip_nonspace(buf, &i);
        if(conv_date(buf + start, &tms) == -1) return -1;
        strip_spaces(buf, &i);
        if(!buf[i]) return -1;
    
        start = i;
        strip_nonspace(buf, &i);
        if(conv_time(buf + start, &tms) == -1) return -1;
        strip_spaces(buf, &i);
    
        st->mtime = av_mktime(&tms);
    
        if(buf[i]) {
            namelen = av_strlen(buf+i);
            while(av_isspace((int) buf[i+namelen-1]))
                namelen--;
            if(namelen > 256) namelen = 256;
            av_strncpy(name, buf+i, namelen);
            name[namelen] = '\0';
        }
    }

    return 0;
}

static int get_drive(ave *v, vpath *path)
{
    int drive = -1;
    const char *params = PARAM(path);
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;

    if(dd->udata != AVNULL) 
        drive = ((char *) dd->udata)[0];
    else if(params[0] == FLOPPY_PARAM_SEP && 
            params[1] >= 'a' && params[1] <= 'z') {
        drive = params[1];
        params += 2;
    }
    
    if(drive == -1 || *params) {
        /* Invalid drive specified */
        v->errn = ENOENT;
        return -1;
    }
    return drive;
}

static char *create_dosname(ave *v, int drive, const char *name)
{
    char *dosname;

    dosname = av_malloc(v, av_strlen(name) + 4);
    if(dosname == AVNULL)
        return AVNULL;

    dosname[0] = drive;
    dosname[1] = ':';
    dosname[2] = '/';
    av_strcpy(dosname + 3, name);

    return dosname;
}

static char *get_dosname(ave *v, vpath *path)
{
    char *name;
    char *dosname;
    int drive;
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;

    drive = get_drive(v, path);
    if(drive == -1)
        return AVNULL;
    
    name = (*dd->vdev->getpath)(v, path);
    if(name == AVNULL)
        return AVNULL;

    dosname =  create_dosname(v, drive, name);
    av_free(name);

    return dosname;
}


static filebuf *start_mdir(ave *v, char drive, const char *path, 
			   struct proginfo *pri)
{
    const char *prog[4];
    filebuf *of;
    char *dirname;
    int pipeout[2];
    int res;

    dirname = create_dosname(v, drive, path);
    if(dirname == AVNULL)
        return AVNULL;

    prog[0] = "mdir";
    prog[1] = "-a";
    prog[2] = dirname;
    prog[3] = AVNULL;

    av_init_proginfo(pri);
    pri->prog = prog;

    if(av_pipe(v, pipeout) == -1) {
        av_free(dirname);
        return AVNULL;
    }

    av_registerfd(pipeout[0]);
  
    pri->ifd = av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri->ofd = pipeout[1];
    pri->efd = pipeout[1]; /* FIXME: Maybe this should be logged instead */
  
    res = av_start_prog(v, pri);
  
    av_localclose(DUMMYV, pri->ifd);
    av_localclose(DUMMYV, pipeout[1]);
    av_free(dirname);
  
    if(res == -1) {
        av_localclose(DUMMYV, pipeout[0]);
        return AVNULL;
    }

    of = av_filebuf_new(v, pipeout[0]);
    if(of == AVNULL) {
        av_localclose(DUMMYV, pipeout[0]);
        return AVNULL;
    }

    return of;
}

static void stop_mdir(struct proginfo *pri, filebuf *of)
{
    if(of->fd != -1)
        av_localclose(DUMMYV, of->fd);
    av_wait_prog(DUMMYV, pri, 1, 0);
    av_filebuf_free(of);
}

static int fill_floppy_entry(ave *v, arch_entry *ent, struct avstat *st)
{
    int res;
    archive *arch = ent->arch;

    if(ent->ino != AVNULL)
        return 0;
  
    st->dev = arch->dev;
    st->ino = arch->inoctr++;
    st->blksize = 1024;
    st->blocks = AV_DIV(st->size, 512);
    st->uid = arch->uid;
    st->gid = arch->gid;
    st->atime = st->mtime;
    st->ctime = st->mtime;
  
    res = av_new_inode(v, ent, st);
    if(res == -1)
        return -1;
  
    ent->flags |= ENTF_NOCASE;

    return 0;
}

static int insert_floppy_entry(ave *v, const char *name, struct avstat *st,
			       arch_entry *parentdir)
{
    int res;
    arch_entry *ent;
 
    ent = av_find_entry(v, parentdir, name, FIND_CREATE, 0);
    if(ent == AVNULL)
        return -1;

    res = fill_floppy_entry(v, ent, st);
    av_unref_entry(ent);
    
    return res;
}

static int read_floppydir(ave *v, char drive, const char *path,
                          arch_entry *parentdir)
{
    int vollabel;
    struct proginfo pri;
    struct avstat st;
    filebuf *of;
    int res;
    char *line;
    char name[257];
    arch_entry *ent;
 
    of = start_mdir(v, drive, path, &pri);
    if(of == AVNULL)
        return -1;

    if(!path[0])
        vollabel = 1;
    else
        vollabel = 0;
  
    while(1) {
        line = av_filebuf_readline(v, of);
        if(line == AVNULL)
            break;

        av_log(AVLOG_DEBUG, "line: %s", line);

        res = process_dir_line(line, vollabel, name, &st);
        av_free(line);
        if(res != -1 && av_strcmp(name, ".") != 0 &&
                                    av_strcmp(name, "..") != 0) {
            if(insert_floppy_entry(v, name, &st, parentdir) == -1) {
                stop_mdir(&pri, of);
                return -1;
            }
        }
    }
  
    stop_mdir(&pri, of);

    for(ent = parentdir->ino->subdir; ent != AVNULL; ent = ent->next) {
        if(AV_ISDIR(ent->ino->st.mode)) {
            char *newpath;
      
            newpath = av_strconcat(v, path, ent->name, "/", AVNULL);
            if(newpath == AVNULL)
                return -1;

            if(av_strlen(newpath) < 1024) 
                res = read_floppydir(v, drive, newpath, ent);
            else {
                res = 0;
                av_log(AVLOG_WARNING, "floppy: Too long path found");
            }

            av_free(newpath);
            if(res == -1)
                return -1;
        }
    }

    return 0;
}

static int read_floppytree(ave *v, vpath *path, archive *arch)
{
    int drive;

    drive = get_drive(v, path);
    if(drive == -1)
        return -1;

    return read_floppydir(v, drive, "", arch->root);
}

static int get_floppy_minor(ave *v, vpath *path)
{
    return get_drive(v, path);
}


/* FIXME: IMPORTANT: do not start mcopy while another mcopy is still
   running, because it will return with "device busy" error */

static int start_mcopy(ave *v, const char *from, const char *to, 
		       struct proginfo *pri)
{
    const char *prog[4];
    int res;

    prog[0] = "mcopy";
    prog[1] = from;
    prog[2] = to;
    prog[3] = AVNULL;

    av_init_proginfo(pri);
    pri->prog = prog;
    pri->ifd  = av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri->ofd  = av_get_logfile(v);
    pri->efd  = pri->ofd;

    res = av_start_prog(v, pri);
  
    av_localclose(DUMMYV, pri->ifd);
    av_localclose(DUMMYV, pri->ofd);

    if(res == -1) {
        av_log(AVLOG_ERROR, "Could not start %s", prog[0]);
        return -1;
    }
  
    return 0;
}

static int wait_until(ave *v, arch_fdi *di, avoff_t needsize)
{
    struct floppy_fdidat *ffd = (struct floppy_fdidat *) di->udata;
    struct avstat stbuf;
    int res;
    int end;
  
    end = 0;
    do {    
        res = av_wait_prog(DUMMYV, &ffd->pri, 0, 1);
        if(res != 0) end = 1; /* Program has stopped, we wait no longer */
    
        if(di->file.fh == -1) {
            /* FIXME: could be read only if umask is stupid */
            di->file.fh = av_localopen(v, ffd->tmpname, 
                                         ffd->rdonly ? AVO_RDONLY : AVO_RDWR, 0);
            di->file.ptr = 0;

            if(ffd->rdonly && di->file.fh != -1) {
                av_del_tmpfile(ffd->tmpname);
                ffd->tmpname = AVNULL;
            }
        }
    
        if(di->file.fh != -1) {
            res = av_fstat(v, di->file.fh, &stbuf, 0);
            if(res == -1) {
                av_wait_prog(v, &ffd->pri, 1, 0);
                return -1;
            }
    
            ffd->cursize = stbuf.size;
            if(stbuf.size >= needsize) return 0;
        }

        if(!end) av_sleep(1);    
    } while(!end);

    return -1;
}

static int do_prog(ave *v, const char **prog)
{
    struct proginfo pri;
    int res;
    
    av_init_proginfo(&pri);
    pri.prog = prog;
    pri.ifd = av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri.ofd = av_get_logfile(v);
    pri.efd = pri.ofd;

    res = av_start_prog(v, &pri);
  
    av_localclose(DUMMYV, pri.ifd);
    av_localclose(DUMMYV, pri.ofd);

    if(res == -1) return -1;

    res = av_wait_prog(DUMMYV, &pri, 0, 0);

    return 0;
}

static int do_prog2(ave *v, const char *progname, const char *arg1)
{
    const char *prog[3];

    prog[0] = progname;
    prog[1] = arg1;
    prog[2] = AVNULL;

    return do_prog(v, prog);
}

static int do_prog3(ave *v, const char *progname, const char *arg1, 
		    const char *arg2)
{
    const char *prog[4];

    prog[0] = progname;
    prog[1] = arg1;
    prog[2] = arg2;
    prog[3] = AVNULL;

    return do_prog(v, prog);
}

static int do_mdel(ave *v, char *dosname)
{
    do_prog3(v, "mattrib", "-r", dosname);
    return do_prog2(v, "mdel", dosname);
}

static int do_mrd(ave *v, char *dosname)
{
    do_prog3(v, "mattrib", "-r", dosname);
    return
        do_prog2(v, "mrd", dosname);
}


static int floppy_unlink(ave *v, vpath *path)
{
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    char *dosname;
    int res;
  
    res = (*dd->vdev->unlink)(v, path);
    if(res == -1)
        return -1;

    dosname = get_dosname(v, path);
    if(dosname == AVNULL)
        return -1;

    res = do_mdel(v, dosname);
    av_free(dosname);

    return res;
}

static int floppy_mkdir(ave *v, vpath *path, avmode_t mode)
{
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    char *dosname;
    int res;
  
    res = (*dd->vdev->mkdir)(v, path, mode);
    if(res == -1) return -1;

    dosname = get_dosname(v, path);
    if(dosname == AVNULL) return -1;

    res = do_prog2(v, "mmd", dosname);
    av_free(dosname);

    return res;
}

static int floppy_rmdir(ave *v, vpath *path)
{
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    char *dosname;
    int res;

    res = (*dd->vdev->rmdir)(v, path);
    if(res == -1)
        return -1;
  
    dosname = get_dosname(v, path);
    if(dosname == AVNULL) return -1;

    res = do_mrd(v, dosname);
    av_free(dosname);

    return res;
}

static int floppy_rename(ave *v, vpath *path, vpath *newpath)
{
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    char *olddosname, *newdosname;
    struct avstat avstbuf;
    int statres, res;

    statres = (*dd->vdev->stat)(DUMMYV, newpath, &avstbuf, 0, 1);

    res = (*dd->vdev->rename)(v, path, newpath);
    if(res == -1)
        return -1;

    olddosname = get_dosname(v, path);
    newdosname = get_dosname(v, newpath);

    if(newdosname == AVNULL || olddosname == AVNULL) {
        av_free(newdosname);
        av_free(olddosname);
        return -1;
    }

    if(statres != -1) {
        if(AV_ISDIR(avstbuf.mode)) 
            do_mrd(DUMMYV, newdosname);
        else
            do_mdel(DUMMYV, newdosname);
    }

    res = do_prog3(v, "mmove", olddosname, newdosname);

    av_free(newdosname);
    av_free(olddosname);

    return res;
}


#endif

static void floppy_destroy(struct remote *rem)
{
    av_free(rem->name);
    av_free(rem);
}

extern int av_init_module_floppy(struct vmodule *module);

int av_init_module_floppy(struct vmodule *module)
{
    int res;
    struct remote *rem;
    struct avfs *avfs;
    
    AV_NEW(rem);
    
    rem->data    = NULL;
    rem->name    = av_strdup("floppy");
    rem->destroy = floppy_destroy;
//    rem->list    = floppy_list;
//    rem->get     = floppy_get;
//    rem->wait    = floppy_wait;

    res = av_remote_init(module, rem, &avfs);

    return res;
}
