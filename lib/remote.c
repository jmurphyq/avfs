/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "remote.h"
#include "namespace.h"
#include "cache.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define REM_ST_VALID 20
#define REM_DIR_VALID 10

struct signature {
    avtimestruc_t modif;
    avoff_t size;
};

struct direntry {
    char *name;
    int type;
    struct direntry *next;
};

struct dir {
    avtime_t valid;
    struct direntry *dirlist;
};

struct attr {
    avtime_t valid;
    int negative;
    struct avstat st;
    char *linkname;
};

struct file {
    struct signature sig;
    char *localname;
    void *data;
};

struct node {
    avmutex lock;
    avmutex filelock;
    struct node *next;
    struct node *prev;
    struct entry *ent;
    avino_t ino;
    struct attr attr;
    struct dir dir;
    struct cacheobj *file;
};

struct filesys {
    struct namespace *ns;
    struct node list;
    struct remote *rem;
    struct avfs *avfs;
};

static AV_LOCK_DECL(rem_lock);

static void rem_free_dir(struct dir *dir)
{
    struct direntry *de;
    
    while((de = dir->dirlist) != NULL) {
        dir->dirlist = de->next;
        __av_free(de->name);
        __av_free(de);
    }
}

static void rem_free_node(struct node *nod)
{
    __av_namespace_set(nod->ent, NULL);
    __av_unref_obj(nod->ent);
    rem_free_dir(&nod->dir);
    __av_free(nod->attr.linkname);
    __av_unref_obj(nod->file);

    AV_FREELOCK(nod->lock);
    AV_FREELOCK(nod->filelock);
}

static struct node *rem_new_node(struct filesys *fs)
{
    struct node *nod;

    AV_NEW_OBJ(nod, rem_free_node);
    AV_INITLOCK(nod->lock);
    AV_INITLOCK(nod->filelock);
    nod->ino = __av_new_ino(fs->avfs);
    nod->attr.valid = 0;
    nod->attr.linkname = NULL;
    nod->dir.valid = 0;
    nod->file = NULL;

    return nod;
}

static void rem_remove_node(struct node *nod)
{
    struct node *next = nod->next;
    struct node *prev = nod->prev;
    
    next->prev = prev;
    prev->next = next;
}

static void rem_insert_node(struct filesys *fs, struct node *nod)
{
    struct node *next = fs->list.next;
    struct node *prev = &fs->list;
    
    next->prev = nod;
    prev->next = nod;
    nod->prev = prev;
    nod->next = next;
}

static struct node *rem_get_node(struct filesys *fs, struct entry *ent)
{
    struct node *nod;

    AV_LOCK(rem_lock);
    nod = (struct node *) __av_namespace_get(ent);
    if(nod != NULL)
        rem_remove_node(nod);
    else {
        nod = rem_new_node(fs);
        nod->ent = ent;
        __av_namespace_set(ent, nod);
        __av_ref_obj(ent);
    }
    rem_insert_node(fs, nod);
    __av_ref_obj(nod);
    AV_UNLOCK(rem_lock);

    return nod;
}

static void rem_get_locked_node(struct filesys *fs, struct entry *ent,
                                struct node **nodep, struct node **parentp)
{
    struct node *nod;
    struct node *parent;
    struct entry *pent;

    pent = __av_namespace_lookup(fs->ns, ent, NULL);
    if(pent != NULL) {
        parent = rem_get_node(fs, pent);
        __av_unref_obj(pent);
    }
    else
        parent = NULL;

    nod = rem_get_node(fs, ent);

    if(parent != NULL)
        AV_LOCK(parent->lock);
    AV_LOCK(nod->lock);

    *nodep = nod;
    *parentp = parent;
}

static void rem_put_locked_node(struct node *nod, struct node *parent)
{
    AV_UNLOCK(nod->lock);
    if(parent != NULL)
        AV_UNLOCK(parent->lock);

    __av_unref_obj(nod);
    __av_unref_obj(parent);
}


static void rem_free_dirlist(struct dirlist *dl)
{
    int i;

    for(i = 0; i < dl->num; i++) {
        __av_free(dl->ents[i].name);
        __av_free(dl->ents[i].linkname);
    }

    __av_free(dl->ents);
    __av_free(dl->hostpath.host);
    __av_free(dl->hostpath.path);
}

static void rem_fill_attr(struct filesys *fs, struct node *nod,
                          struct direlement *de, avtime_t now)
{
    struct attr *attr = &nod->attr;

    attr->valid = now + REM_ST_VALID;
    attr->negative = 0;
    attr->st = de->attr;
    attr->st.ino = nod->ino;
    attr->st.dev = fs->avfs->dev;
    __av_free(attr->linkname);
    attr->linkname = __av_strdup(de->linkname);
}

static void rem_fill_root(struct filesys *fs, struct node *nod)
{
    struct attr *attr = &nod->attr;
    
    attr->valid = AV_MAXTIME;
    attr->negative = 0;
    attr->st.ino = nod->ino;
    attr->st.dev = fs->avfs->dev;
    attr->st.mode = AV_IFDIR | 0777;
    attr->st.nlink = 2;
    attr->st.uid = 0;
    attr->st.gid = 0;
    attr->st.size = 0;
    attr->st.blksize = 512;
    attr->st.blocks = 0;
    attr->st.atime.sec = 0;
    attr->st.atime.nsec = 0;
    attr->st.mtime = attr->st.atime;
    attr->st.ctime = attr->st.atime;
}

static int rem_list_single(struct filesys *fs, struct node *nod,
                           struct dirlist *dl)
{
    int i;

    for(i = 0; i < dl->num; i++) {
        struct direlement *de = &dl->ents[i];

        if(strcmp(de->name, dl->hostpath.path) == 0) {
            rem_fill_attr(fs, nod, de, __av_time());
            return 0;
        }
    }

    return -ENOENT;
}

static void rem_dir_add(struct dir *dir, struct direlement *dire)
{
    struct direntry **dp;
    struct direntry *de;
    
    for(dp = &dir->dirlist; *dp != NULL; dp = &(*dp)->next);
    
    AV_NEW(de);
    de->name = __av_strdup(dire->name);
    de->type = AV_TYPE(dire->attr.mode);
    de->next = NULL;
    
    *dp = de;
}

static void rem_dir_add_beg(struct dir *dir, const char *name, int type)
{
    struct direntry *de;

    AV_NEW(de);
    de->name = __av_strdup(name);
    de->type = type;
    de->next = dir->dirlist;

    dir->dirlist = de;
}

static int rem_list_dir(struct filesys *fs, struct node *nod,
                         struct node *child,  struct dirlist *dl,
                         struct node *need)
{
    int i;
    avtime_t now = __av_time();
    int found = 0;
    int gotdots = 0;

    rem_free_dir(&nod->dir);
    for(i = 0; i < dl->num; i++) {
        struct direlement *de = &dl->ents[i];
        
        rem_dir_add(&nod->dir, de);
        if(strcmp(de->name, ".") == 0) {
            rem_fill_attr(fs, nod, de, now);
            if(nod == need)
                found = 1;

            gotdots = 1;
        }
        else if(strcmp(de->name, "..") == 0)
            gotdots = 1;
        else {
            struct entry *cent;
            struct node *cnod;

            cent = __av_namespace_lookup(fs->ns, nod->ent, de->name);
            cnod = rem_get_node(fs, cent);
            __av_unref_obj(cent);
            if(cnod != child) {
                AV_LOCK(cnod->lock);
                rem_fill_attr(fs, cnod, de, now);
                AV_UNLOCK(cnod->lock);
            }
            else
                rem_fill_attr(fs, cnod, de, now);

            if(cnod == need)
                found = 1;

            __av_unref_obj(cnod);
        }
    }
    
    if(!gotdots) {
        rem_dir_add_beg(&nod->dir, "..", AV_TYPE(AV_IFDIR));
        rem_dir_add_beg(&nod->dir, ".", AV_TYPE(AV_IFDIR));
    }

    nod->dir.valid = now + REM_DIR_VALID;

    if(found)
        return 0;
    else
        return -ENOENT;
}

static void rem_get_hostpath(struct entry *ent, struct hostpath *hp)
{
    char *hostpath = __av_namespace_getpath(ent);
    char *s;

    s = strchr(hostpath, '/');
    if(s == NULL) {
        hp->host = __av_strdup(hostpath);
        hp->path = __av_strdup("/");
    }
    else {
        *s = '\0';
        hp->host = __av_strdup(hostpath);
        *s = '/';
        hp->path = __av_strdup(s);
    }
    __av_free(hostpath);
}

static int rem_get_attr(struct filesys *fs, struct node *nod,
                        struct node *parent)
{
    int res;
    struct remote *rem = fs->rem;
    struct dirlist dl;
    
    dl.flags = REM_LIST_SINGLE;
    dl.num = 0;
    dl.ents = NULL;
    rem_get_hostpath(nod->ent, &dl.hostpath);
    
    res = rem->list(rem, &dl);
    if(res == 0) {
        if((dl.flags & REM_LIST_SINGLE) != 0)
            res = rem_list_single(fs, nod, &dl);
        else if((dl.flags & REM_LIST_PARENT) != 0)
            res = rem_list_dir(fs, parent, nod, &dl, nod);
        else
            res = rem_list_dir(fs, nod, NULL, &dl, nod);

        /* It can happen, that the root directory cannot be listed */
        if(parent == NULL && res == -ENOENT) {
            rem_fill_root(fs, nod);
            res = 0;
        }
    }

    rem_free_dirlist(&dl);
    
    if(res == -ENOENT) {
        nod->attr.valid = __av_time() + REM_ST_VALID;
        nod->attr.negative = 1;
    }

    return res;
}

static int rem_check_node(struct filesys *fs, struct node *nod,
                          struct node *parent)
{
    int res;
    avtime_t now = __av_time();

    if(now < nod->attr.valid) {
        if(nod->attr.negative)
            res = -ENOENT;
        else
            res = 0;
    }
    else {
        nod->attr.valid = 0;
        res = rem_get_attr(fs, nod, parent);
    }
    
    return res;
}

static int rem_signature_valid(struct signature *sig, struct avstat *stbuf)
{
    if(sig->modif.sec != stbuf->mtime.sec ||
       sig->modif.nsec != stbuf->mtime.nsec ||
       sig->size != stbuf->size)
        return 0;
    else
        return 1;
}

static int rem_get_dir(struct filesys *fs, struct node *nod)
{
    int res;
    struct remote *rem = fs->rem;
    struct dirlist dl;
    
    dl.flags = 0;
    dl.num = 0;
    dl.ents = NULL;
    rem_get_hostpath(nod->ent, &dl.hostpath);
    
    res = rem->list(rem, &dl);
    if(res == 0)
        rem_list_dir(fs, nod, NULL, &dl, NULL);

    rem_free_dirlist(&dl);

    return res;
}

static int rem_check_dir(struct filesys *fs, struct node *nod)
{
    int res;
    avtime_t now = __av_time();

    if(now < nod->dir.valid)
        res = 0;
    else
        res = rem_get_dir(fs, nod);
    
    return res;
}

static int rem_node_type(struct filesys *fs, struct entry *ent)
{
    int res;
    struct node *nod;
    struct node *parent;

    rem_get_locked_node(fs, ent, &nod, &parent);
    if(nod->attr.valid != 0 && !nod->attr.negative)
        res = 0;
    else 
        res = rem_check_node(fs, nod, parent);

    if(res == 0)
        res = AV_TYPE(nod->attr.st.mode);
    rem_put_locked_node(nod, parent);

    return res;
}

static struct entry *rem_ventry_entry(ventry *ve)
{
    return (struct entry *) ve->data;
}

static struct entry *rem_vfile_entry(vfile *vf)
{
    return (struct entry *) vf->data;
}

static struct filesys *rem_ventry_filesys(ventry *ve)
{
    return (struct filesys *) ve->mnt->avfs->data;
}

static struct filesys *rem_vfile_filesys(vfile *vf)
{
    return (struct filesys *) vf->mnt->avfs->data;
}

static struct entry *rem_do_lookup(struct namespace *ns, struct entry *prev,
                                   const char *name)
{
    if(name != NULL) {
        if(strcmp(name, ".") == 0) {
            __av_ref_obj(prev);
            return prev;
        }
        if(strcmp(name, "..") == 0)
            name = NULL;
    }

    return __av_namespace_lookup(ns, prev, name);
}

static int rem_lookup(ventry *ve, const char *name, void **newp)
{
    int res;
    int type;
    struct entry *prev = rem_ventry_entry(ve);
    struct filesys *fs = rem_ventry_filesys(ve);
    struct entry *ent;
 
    if(prev != NULL) {
        res = rem_node_type(fs, prev);
        if(res < 0)
            return res;
        
        if(name != NULL && res != AV_TYPE(AV_IFDIR))
            return -ENOTDIR;
    }

    ent = rem_do_lookup(fs->ns, prev, name);
    
    if(ent == NULL)
        type = 0;
    else {
        type = rem_node_type(fs, ent);
        if(type < 0) {
            __av_unref_obj(ent);
            return type;
        }
    }
    __av_unref_obj(prev);
    
    *newp = ent;
    return type;
}

static int rem_getpath(ventry *ve, char **resp)
{
    struct entry *ent = rem_ventry_entry(ve);

    *resp = __av_namespace_getpath(ent);

    return 0;
}

static void rem_putent(ventry *ve)
{
    struct entry *ent = rem_ventry_entry(ve);

    __av_unref_obj(ent);
}

static int rem_copyent(ventry *ve, void **resp)
{
    struct entry *ent = rem_ventry_entry(ve);
    
    __av_ref_obj(ent);
    *resp =  (void *) ent;

    return 0;
}

static void rem_check_file(struct filesys * fs, struct entry *ent)
{
    int res;
    struct node *nod;
    struct node *parent;
    struct file *fil;

    rem_get_locked_node(fs, ent, &nod, &parent);
    fil = __av_cacheobj_get(nod->file);
    if(fil != NULL) {
        res = rem_check_node(fs, nod, parent);
        if(res < 0 || !rem_signature_valid(&fil->sig, &nod->attr.st)) {
            __av_unref_obj(nod->file);
            nod->file = NULL;
        }
        __av_unref_obj(fil);
    }
    rem_put_locked_node(nod, parent);
}

static int rem_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    struct entry *ent = rem_ventry_entry(ve);
    struct filesys *fs = rem_ventry_filesys(ve);
    int accmode = (flags & AVO_ACCMODE);

    res = rem_node_type(fs, ent);
    if(res < 0)
        return res;

    if((flags & AVO_DIRECTORY) != 0) {
        if(res != AV_TYPE(AV_IFDIR))
            return -ENOTDIR;
    }
    else {
        if(accmode == AVO_WRONLY || accmode == AVO_RDWR)
            return -EPERM;

        rem_check_file(fs, ent);
    }

    __av_ref_obj(ent);

    *resp = (void *) ent;
    return 0;
}

static struct entry *rem_dirent_lookup(struct namespace *ns,
                                       struct entry *parent, const char *name)
{
    struct entry *ent;

    ent = rem_do_lookup(ns, parent, name);
    if(ent == NULL) {
        ent = parent;
        __av_ref_obj(ent);
    }

    return ent;
}

static struct direntry *rem_nth_entry(struct dir *dir, int n)
{
    struct direntry *de;
    int i;

    de = dir->dirlist;
    for(i = 0; i < n && de != NULL; i++)
        de = de->next;
    
    return de;
}

static int rem_get_direntry(struct filesys *fs, struct node *nod,
                            vfile *vf, struct avdirent *buf)
{
    struct direntry *de;
    struct entry *cent;
    struct node *cnod;

    de = rem_nth_entry(&nod->dir, vf->ptr);
    if(de == NULL)
        return 0;
    
    cent = rem_dirent_lookup(fs->ns, nod->ent, de->name);
    cnod = rem_get_node(fs, cent);
    __av_unref_obj(cent);
    
    buf->name = __av_strdup(de->name);
    buf->type = de->type;
    buf->ino = cnod->ino;
    __av_unref_obj(cnod);
    
    vf->ptr ++;

    return 1;
}

static int rem_readdir(vfile *vf, struct avdirent *buf)
{
    int res;
    struct entry *ent = rem_vfile_entry(vf);
    struct filesys *fs = rem_vfile_filesys(vf);
    struct node *nod;

    nod = rem_get_node(fs, ent);
    AV_LOCK(nod->lock);
    res = rem_check_dir(fs, nod);
    if(res == 0)
        res = rem_get_direntry(fs, nod, vf, buf);
    AV_UNLOCK(nod->lock);
    __av_unref_obj(nod);

    return res;
}

static int rem_close(vfile *vf)
{
    struct entry *ent = rem_vfile_entry(vf);

    __av_unref_obj(ent);

    return 0;
}

static void rem_get_signature(struct filesys *fs, struct entry *ent,
                              struct signature *sig)
{
    int res;
    struct node *nod;
    struct node *parent;

    rem_get_locked_node(fs, ent, &nod, &parent);
    res = rem_check_node(fs, nod, parent);
    if(res == 0) {
        sig->modif = nod->attr.st.mtime;
        sig->size = nod->attr.st.size;
    }
    else
        sig->size = -1;
    rem_put_locked_node(nod, parent);
}

static void rem_delete_file(struct file *fil)
{
    __av_del_tmpfile(fil->localname);
    __av_unref_obj(fil->data);
}

static avoff_t rem_local_size(const char *localname)
{
    int res;
    struct stat stbuf;
    
    res = stat(localname, &stbuf);
    if(res == 0)
        return stbuf.st_blocks * 512;
    else
        return -1;
    
}

static int rem_get_file(struct filesys *fs, struct node *nod,
                        struct file **resp)
{
    int res;
    struct file *fil;
    struct remote *rem = fs->rem;
    struct getparam gp;
    char *objname;
    
    fil = (struct file *) __av_cacheobj_get(nod->file);
    if(fil != NULL) {
        *resp = fil;
        return 0;
    }

    rem_get_hostpath(nod->ent, &gp.hostpath);
    objname = __av_stradd(NULL, rem->name, ":", gp.hostpath.host,
                          gp.hostpath.path, NULL);
    
    res = rem->get(rem, &gp);
    __av_free(gp.hostpath.host);
    __av_free(gp.hostpath.path);

    if(res < 0) {
        __av_free(objname);
        return res;
    }

    AV_NEW_OBJ(fil, rem_delete_file);
    fil->localname = gp.localname;
    fil->data = gp.data;
    rem_get_signature(fs, nod->ent, &fil->sig);

    __av_unref_obj(nod->file);
    nod->file = __av_cacheobj_new(fil, objname);
    __av_free(objname);

    if(res == 0)
        __av_cacheobj_setsize(nod->file, rem_local_size(fil->localname));

    *resp = fil;

    return 0;
}

static int rem_wait_data(struct filesys *fs, struct node *nod,
                         struct file *fil, avoff_t end)
{
    int res;
    struct remote *rem = fs->rem;
    
    if(fil->data == NULL)
        return 0;

    res = rem->wait(rem, fil->data, end);
    if(res < 0)
        return res;

    if(res == 0) {
        __av_unref_obj(fil->data);
        fil->data = NULL;
        __av_cacheobj_setsize(nod->file, rem_local_size(fil->localname));
    }

    return 0;
}

static avssize_t rem_real_read(struct file *fil, vfile *vf, char *buf,
                               avsize_t nbyte)
{
    avssize_t res;
    avoff_t sres;
    int fd;

    fd = open(fil->localname, O_RDONLY);
    if(fd == -1)
        return -errno;
    
    sres = lseek(fd, vf->ptr, SEEK_SET);
    if(sres == -1)
        res = -errno;
    else {
	res = read(fd, buf, nbyte);
        if(res == -1)
            res = -errno;
        else 
            vf->ptr += res;
    }
    close(fd);

    return res;
}

static avssize_t rem_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct filesys *fs = rem_vfile_filesys(vf);
    struct entry *ent = rem_vfile_entry(vf);
    struct node *nod;
    struct file *fil = NULL;

    nod = rem_get_node(fs, ent);
    AV_LOCK(nod->filelock);
    res = rem_get_file(fs, nod, &fil);
    if(res == 0) {
        res = rem_wait_data(fs, nod, fil, vf->ptr + nbyte);
        if(res == 0)
            res = rem_real_read(fil, vf, buf, nbyte);

        if(res < 0) {
            __av_unref_obj(nod->file);
            nod->file = NULL;
        }
        __av_unref_obj(fil);
    }
    AV_UNLOCK(nod->filelock);
    __av_unref_obj(nod);

    return res;
}


static int rem_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    int res;
    struct filesys *fs = rem_vfile_filesys(vf);
    struct entry *ent = rem_vfile_entry(vf);
    struct node *nod;
    struct node *parent;

    rem_get_locked_node(fs, ent, &nod, &parent);
    res = rem_check_node(fs, nod, parent);
    if(res == 0)
        *buf = nod->attr.st;
    rem_put_locked_node(nod, parent);
    
    return res;
}

static int rem_access(ventry *ve, int amode)
{
    int res;
    struct filesys *fs = rem_ventry_filesys(ve);
    struct entry *ent = rem_ventry_entry(ve);

    res = rem_node_type(fs, ent);
    if(res < 0)
        return res;

    if((amode & AVW_OK) != 0)
        return -EACCES;

    return 0;
}


static int rem_readlink(ventry *ve, char **bufp)
{
    int res;
    struct filesys *fs = rem_ventry_filesys(ve);
    struct entry *ent = rem_ventry_entry(ve);
    struct node *nod;
    struct node *parent;

    rem_get_locked_node(fs, ent, &nod, &parent);
    res = rem_check_node(fs, nod, parent);
    if(res == 0) {
        if(!AV_ISLNK(nod->attr.st.mode))
            res = -EINVAL;
        else
            *bufp = __av_strdup(nod->attr.linkname);
    }
    rem_put_locked_node(nod, parent);

    return res;
}

static void rem_log_tree(struct namespace *ns, struct entry *ent)
{
    char *path;
    struct entry *next;

    while(ent != NULL) {
        path = __av_namespace_getpath(ent);
        __av_log(AVLOG_ERROR, "    %s", path);
        __av_free(path);
        rem_log_tree(ns, __av_namespace_subdir(ns, ent));
        next = __av_namespace_next(ent);
        __av_unref_obj(ent);
        ent = next;
    }
}

static void rem_destroy(struct avfs *avfs)
{
    struct filesys *fs = (struct filesys *) avfs->data;
    struct remote *rem = fs->rem;
    struct node *nod;
    struct entry *root;

    AV_LOCK(rem_lock);
    nod = fs->list.next;
    while(nod != &fs->list) {
        struct node *next = nod->next;
        
        __av_unref_obj(nod);
        nod = next;
    }
    AV_UNLOCK(rem_lock);
    
    root = __av_namespace_subdir(fs->ns, NULL);
    if(root != NULL) {
        __av_log(AVLOG_ERROR, "%s: busy entries after destroy:", avfs->name);
        rem_log_tree(fs->ns, root);
    }
    __av_unref_obj(fs->ns);

    rem->destroy(rem);
    __av_free(fs);
}

void __av_remote_add(struct dirlist *dl, const char *name,
                     const char *linkname, struct avstat *attr)
{
    struct direlement *de;

    dl->ents = __av_realloc(dl->ents, sizeof(*dl->ents) * (dl->num + 1));
    de = &dl->ents[dl->num];
    dl->num++;

    de->name = __av_strdup(name);
    de->linkname = __av_strdup(linkname);
    de->attr = *attr;
}


int __av_remote_init(struct vmodule *module, struct remote *rem,
                     struct avfs **resp)
{
    int res;
    struct avfs *avfs;
    struct filesys *fs;

    res = __av_new_avfs(rem->name, NULL, AV_VER, AVF_ONLYROOT | AVF_NOLOCK,
                        module, &avfs);
    if(res < 0) {
        rem->destroy(rem);
        return res;
    }

    AV_NEW(fs);
    fs->ns = __av_namespace_new();
    fs->list.next = fs->list.prev = &fs->list;
    fs->rem = rem;
    fs->avfs = avfs;

    avfs->data = fs;
    avfs->destroy = rem_destroy;

    avfs->lookup    = rem_lookup;
    avfs->putent    = rem_putent;
    avfs->copyent   = rem_copyent;
    avfs->getpath   = rem_getpath;

    avfs->open      = rem_open;

    avfs->close     = rem_close;
    avfs->read      = rem_read;
    avfs->readdir   = rem_readdir;
    avfs->getattr   = rem_getattr;

    avfs->access    = rem_access;
    avfs->readlink  = rem_readlink;

    __av_add_avfs(avfs);
    
    *resp = avfs;

    return 0;
}
