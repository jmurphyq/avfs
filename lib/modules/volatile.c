/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

struct node {
    struct avstat st;
    struct entry *subdir;  /* only dir */
    struct entry *parent;  /* only dir */
    char *content;         /* only regular & symlink */
};

struct entry {
    char *name;
    struct node *node;
    struct entry *next;
    struct entry **prevp;
    struct entry *parent;
};

struct filesys {
    struct entry *root;
    struct avfs *avfs;
};

static void vol_unlink_entry(struct entry *ent)
{
    if(ent->prevp != NULL)
        *ent->prevp = ent->next;
    if(ent->next != NULL)
        ent->next->prevp = ent->prevp;
    __av_unref_obj(ent->parent);
    __av_free(ent->name);

    ent->prevp = NULL;
    ent->next = NULL;
    ent->parent = NULL;
    ent->name = NULL;
}

static struct entry *vol_new_entry(const char *name)
{
    struct entry *ent;

    AV_NEW_OBJ(ent, vol_unlink_entry);

    ent->node = NULL;
    ent->next = NULL;
    ent->prevp = NULL;
    ent->parent = NULL;
    ent->name = __av_strdup(name);

    return ent;
}

static void vol_free_node(struct node *nod)
{
    __av_free(nod->content);
}

static struct node *vol_new_node(struct avstat *initstat)
{
    struct node *nod;

    AV_NEW_OBJ(nod, vol_free_node);

    nod->st = *initstat;
    nod->subdir = NULL;
    nod->parent = NULL;
    nod->content = NULL;

    return nod;
}

static void vol_link_node(struct entry *ent, struct node *nod)
{
    __av_ref_obj(ent);
    __av_ref_obj(nod);
    ent->node = nod;
    
    if(AV_ISDIR(nod->st.mode)) {
        nod->st.nlink = 2;
        if(ent->parent != NULL) {
            nod->parent = ent->parent;
            ent->parent->node->st.nlink ++;
        }
        else 
            nod->parent = ent;
    }
    else
        nod->st.nlink ++;

    if(ent->parent != NULL)
        ent->parent->node->st.size ++;    
}

static void vol_unlink_node(struct entry *ent)
{
    struct node *nod = ent->node;
    
    if(AV_ISDIR(nod->st.mode)) {
        nod->st.nlink = 0;
        if(nod->parent != NULL)
            nod->parent->node->st.nlink --;
    }
    else
        nod->st.nlink --;

    if(ent->parent != NULL)
        ent->parent->node->st.size --;


    ent->node = NULL;
    __av_unref_obj(nod);
    __av_unref_obj(ent);
}

static void vol_free_tree(struct entry *ent)
{
    struct node *nod = ent->node;

    if(nod != NULL) {
        while(nod->subdir != NULL)
            vol_free_tree(nod->subdir);
        
        vol_unlink_entry(ent);
        vol_unlink_node(ent);
    }
}

static int vol_make_node(struct filesys *fs, struct entry *ent, avmode_t mode)
{
    struct node *nod;
    struct avstat initstat;

    if(ent->name == NULL)
        return -ENOENT;

    __av_default_stat(&initstat);
    
    initstat.dev = fs->avfs->dev;
    initstat.ino = __av_new_ino(fs->avfs);

    nod = vol_new_node(&initstat);
    nod->st.mode = mode;
    
    vol_link_node(ent, nod);
    __av_unref_obj(nod);

    return 0;
}

static struct entry *vol_ventry_entry(ventry *ve)
{
    return (struct entry *) ve->data;
}

static struct node *vol_vfile_node(vfile *vf)
{
    return (struct node *) vf->data;
}

static struct filesys *vol_ventry_filesys(ventry *ve)
{
    return (struct filesys *) ve->mnt->avfs->data;
}

static struct entry *vol_get_entry(struct entry *parent, const char *name)
{
    struct entry **entp;
    struct entry *ent;

    if(strcmp(name, ".") == 0) {
        ent = parent;
	__av_ref_obj(ent);
	return ent;
    }
    if(strcmp(name, "..") == 0) {
        ent = parent->parent;
	__av_ref_obj(ent);
	return ent;
    }
    for(entp = &parent->node->subdir; *entp != NULL; entp = &(*entp)->next)
	if(strcmp(name, (*entp)->name) == 0) {
	    ent = *entp;
	    __av_ref_obj(ent);
	    return ent;
	}

    ent = vol_new_entry(name);
    
    *entp = ent;
    ent->prevp = entp;
    ent->parent = parent;
    __av_ref_obj(parent);

    return ent;
}

static int vol_do_lookup(struct entry *parent, const char *name,
                         struct entry **entp)
{
    if(parent->node == NULL)
        return -ENOENT;

    if(name == NULL) {
        *entp = parent->parent;
        __av_ref_obj(*entp);
        return 0;
    }

    if(!AV_ISDIR(parent->node->st.mode))
        return -ENOTDIR;

    *entp = vol_get_entry(parent, name);
    
    return 0;
}

static struct entry *vol_get_root(ventry *ve)
{
    struct filesys *fs = vol_ventry_filesys(ve);
    struct entry *root = fs->root;

    __av_ref_obj(root);

    return root;
}

static int vol_lookup(ventry *ve, const char *name, void **newp)
{
    int res = 0;
    struct entry *parent = vol_ventry_entry(ve);
    struct entry *ent;
    
    if(parent == NULL) {
        if(name[0] != '\0' || ve->mnt->opts[0] != '\0')
            return -ENOENT;

        ent = vol_get_root(ve);
    }
    else {
        res = vol_do_lookup(parent, name, &ent);
        if(res < 0)
            return res;
        
        __av_unref_obj(parent);
    }

    *newp = ent;

    if(ent != NULL && ent->node != NULL)
        return AV_TYPE(ent->node->st.mode);
    else
        return 0;
}

static char *vol_create_path(struct entry *ent)
{
    char *path;
    
    if(ent->parent == NULL)
        return __av_strdup("");
    
    path = vol_create_path(ent->parent);

    return __av_stradd(path, "/", ent->name, NULL);
}

static int vol_getpath(ventry *ve, char **resp)
{
    struct entry *ent = vol_ventry_entry(ve);

    *resp = vol_create_path(ent);

    return 0;
}

static void vol_putent(ventry *ve)
{
    struct entry *ent = vol_ventry_entry(ve);

    __av_unref_obj(ent);
}

static int vol_copyent(ventry *ve, void **resp)
{
    struct entry *ent = vol_ventry_entry(ve);
    
    __av_ref_obj(ent);

    *resp = (void *) ent;

    return 0;
}

static void vol_truncate_node(struct node *nod, avoff_t length)
{
    nod->st.size = length;
    nod->st.blocks = AV_DIV(nod->st.size, 512);
    __av_curr_time(&nod->st.mtime);
}

static int vol_need_write(int flags)
{
    if((flags & AVO_ACCMODE) == AVO_WRONLY ||
       (flags & AVO_ACCMODE) == AVO_RDWR ||
       (flags & AVO_TRUNC) != 0)
        return 1;
    
    return 0;
}

static int vol_open_check_type(avmode_t mode, int flags)
{
    if((flags & AVO_DIRECTORY) != 0 && !AV_ISDIR(mode))
        return -ENOTDIR;
    
    switch(mode & AV_IFMT) {
    case AV_IFREG:
        return 0;
        
    case AV_IFDIR:
        if(vol_need_write(flags))
            return -EISDIR;
        return 0;

    case AV_IFLNK:
        if((flags & AVO_ACCMODE) != AVO_NOPERM || !(flags & AVO_NOFOLLOW))
            return -ENOENT;
        return 0;

    default:
        if((flags & AVO_ACCMODE) != AVO_NOPERM)
            return -EPERM;
        return 0;
    }
}

static int vol_open_check(struct node *nod, int flags)
{
    if(nod == NULL) {
        if(!(flags & AVO_CREAT))
            return -ENOENT;
        return 0;
    }

    if((flags & AVO_EXCL) != 0)
        return -EEXIST;

    return vol_open_check_type(nod->st.mode, flags);
}

static int vol_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    struct filesys *fs = vol_ventry_filesys(ve);
    struct entry *ent = vol_ventry_entry(ve);
    
    res = vol_open_check(ent->node, flags);
    if(res < 0)
        return res;

    if(ent->node == NULL) {
        res = vol_make_node(fs, ent, mode | AV_IFREG);
        if(res < 0)
            return res;
    }
    else if((flags & AVO_TRUNC) != 0)
        vol_truncate_node(ent->node, 0);

    __av_ref_obj(ent->node);
    
    *resp = ent->node;

    return 0;
}

static int vol_close(vfile *vf)
{
    struct node *nod = vol_vfile_node(vf);

    __av_unref_obj(nod);

    return 0;
}

static avssize_t vol_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avoff_t nact;
    struct node *nod = vol_vfile_node(vf);

    if(AV_ISDIR(nod->st.mode))
        return -EISDIR;
    
    if(vf->ptr >= nod->st.size)
	return 0;
    
    nact = AV_MIN(nbyte, (avsize_t) (nod->st.size - vf->ptr));
    
    memcpy(buf, nod->content + vf->ptr, nact);
    
    vf->ptr += nact;
    
    return nact;
}

static avssize_t vol_write(vfile *vf, const char *buf, avsize_t nbyte)
{
    avoff_t end;
    struct node *nod = vol_vfile_node(vf);

    if((vf->flags & AVO_APPEND) != 0)
        vf->ptr = nod->st.size;

    end = vf->ptr + nbyte;
    if(end > nod->st.size) {
        nod->content = __av_realloc(nod->content, end);
        nod->st.size = end;
        nod->st.blocks = AV_DIV(nod->st.size, 512);
    }

    memcpy(nod->content + vf->ptr, buf, nbyte);

    __av_curr_time(&nod->st.mtime);

    vf->ptr = end;

    return nbyte;
}

static int vol_truncate(vfile *vf, avoff_t length)
{
    struct node *nod = vol_vfile_node(vf);

    if(length < nod->st.size)
        vol_truncate_node(nod, length);

    return 0;
}

static struct node *vol_special_entry(int n, struct node *nod,
                                      const char **namep)
{
    if(n == 0) {
        *namep = ".";
        return nod;
    }
    else {
        *namep = "..";
        return nod->parent->node;
    }
}

static struct node *vol_nth_entry(int n, struct node *nod, const char **namep)
{
    struct entry *ent;
    int i;

    if(nod->parent != NULL) {
        if(n  < 2)
            return vol_special_entry(n, nod, namep);

        n -= 2;
    }

    ent = nod->subdir;
    for(i = 0; i < n && ent != NULL; i++)
        ent = ent->next;
    
    if(ent == NULL)
        return NULL;

    *namep = ent->name;
    return ent->node;
}


static int vol_readdir(vfile *vf, struct avdirent *buf)
{
    struct node *parent = vol_vfile_node(vf);
    struct node *nod;
    const char *name;
    
    if(!AV_ISDIR(parent->st.mode))
        return -ENOTDIR;
    
    nod = vol_nth_entry(vf->ptr, parent, &name);
    if(nod == NULL)
        return 0;

    buf->name = __av_strdup(name);
    buf->ino = nod->st.ino;
    buf->type = AV_TYPE(nod->st.mode);
    
    vf->ptr ++;
    
    return 1;
}

static int vol_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    struct node *nod = vol_vfile_node(vf);

    *buf = nod->st;

    return 0;
}

static void vol_set_attributes(struct avstat *dest, const struct avstat *src,
                               int attrmask)
{
    if((attrmask & AVA_ATIME) != 0)
        dest->atime = src->atime;
    if((attrmask & AVA_MTIME) != 0)
        dest->mtime = src->mtime;
    if((attrmask & AVA_MODE) != 0)
        dest->mode = (dest->mode & AV_IFMT) | src->mode;
    if((attrmask & AVA_UID) != 0)
        dest->uid = src->uid;
    if((attrmask & AVA_GID) != 0)
        dest->gid = src->gid;
}

static int vol_setattr(vfile *vf, struct avstat *buf, int attrmask)
{
    struct node *nod = vol_vfile_node(vf);

    vol_set_attributes(&nod->st, buf, attrmask);
    
    return 0;
}

static int vol_access(ventry *ve, int amode)
{
    struct node *nod = vol_ventry_entry(ve)->node;

    if(nod == NULL) 
        return -ENOENT;
    
    return 0;
}

static int vol_readlink(ventry *ve, char **bufp)
{
    struct node *nod = vol_ventry_entry(ve)->node;

    if(nod == NULL)
        return -ENOENT;

    if(!AV_ISLNK(nod->st.mode))
        return -EINVAL;

    *bufp = __av_strdup(nod->content);

    return 0;
}

static int vol_unlink(ventry *ve)
{
    struct entry *ent = vol_ventry_entry(ve);

    if(ent->node == NULL)
        return -ENOENT;

    if(AV_ISDIR(ent->node->st.mode))
        return -EISDIR;
    
    vol_unlink_node(ent);

    return 0;
}

static int vol_check_rmdir(struct entry *ent)
{
    struct node *nod = ent->node;

    if(nod == NULL)
        return -ENOENT;

    if(!AV_ISDIR(nod->st.mode)) 
        return -ENOTDIR;

    if(nod->subdir != NULL)
        return -ENOTEMPTY;

    if(ent->parent == NULL)
        return -EBUSY;

    return 0;
}

static int vol_rmdir(ventry *ve)
{
    int res;
    struct entry *ent = vol_ventry_entry(ve);

    res = vol_check_rmdir(ent);
    if(res < 0) 
        return res;

    vol_unlink_node(ent);
    
    return 0;
}

static int vol_mkdir(ventry *ve, avmode_t mode)
{
    int res;
    struct filesys *fs = vol_ventry_filesys(ve);
    struct entry *ent = vol_ventry_entry(ve);
    
    if(ent->node != NULL)
        return -EEXIST;
    
    res = vol_make_node(fs, ent, mode | AV_IFDIR);
    if(res < 0)
        return res;

    return 0;
}

static int vol_mknod(ventry *ve, avmode_t mode, avdev_t dev)
{
    int res;
    struct filesys *fs = vol_ventry_filesys(ve);
    struct entry *ent = vol_ventry_entry(ve);
    
    if(ent->node != NULL)
        return -EEXIST;
    
    res = vol_make_node(fs, ent, mode);
    if(res < 0)
        return res;

    ent->node->st.rdev = dev;

    return 0;
}

static int vol_is_subdir(struct entry *dir, struct entry *basedir)
{
    while(1) {
        if(dir == basedir)
            return 1;

        if(dir->parent == NULL)
            break;

        dir = dir->parent;
    }

    return 0;
}

static int vol_check_rename(struct entry *ent, struct entry *newent)
{
    if(ent->node == NULL)
        return -ENOENT;

    if(newent->name == NULL)
        return -ENOENT;

    if(AV_ISDIR(ent->node->st.mode) && vol_is_subdir(newent, ent))
        return -EINVAL;

    if(newent->node != NULL) {
        if(AV_ISDIR(ent->node->st.mode)) {
            if(!AV_ISDIR(newent->node->st.mode))
                return -ENOTDIR;

            if(newent->node->subdir != NULL)
                return -ENOTEMPTY;
        }
        else {
            if(AV_ISDIR(newent->node->st.mode))
               return -EISDIR;
        }
        vol_unlink_node(newent);
    }

    return 0;
}

static int vol_rename(ventry *ve, ventry *newve)
{
    int res;
    struct entry *ent = vol_ventry_entry(ve);
    struct entry *newent = vol_ventry_entry(newve);

    if(ent->node != NULL && ent == newent)
        return 0;

    res = vol_check_rename(ent, newent);
    if(res < 0)
        return res;

    vol_link_node(newent, ent->node);
    vol_unlink_node(ent);

    return 0;
}

static int vol_check_link(struct entry *ent, struct entry *newent)
{
    if(ent->node == NULL)
        return -ENOENT;

    if(newent->name == NULL)
        return -ENOENT;

    if(AV_ISDIR(ent->node->st.mode))
        return -EPERM;
    
    if(newent->node != NULL)
        return -EEXIST;
    
    return 0;
}

static int vol_link(ventry *ve, ventry *newve)
{
    int res;
    struct entry *ent = vol_ventry_entry(ve);
    struct entry *newent = vol_ventry_entry(newve);
    
    res = vol_check_link(ent, newent);
    if(res < 0)
        return res;

    vol_link_node(newent, ent->node);
    
    return 0;
}

static int vol_symlink(const char *path, ventry *newve)
{
    int res;
    struct filesys *fs = vol_ventry_filesys(newve);
    struct entry *ent = vol_ventry_entry(newve);
    
    if(ent->node != NULL)
        return -EEXIST;

    res = vol_make_node(fs, ent, 0777 | AV_IFLNK);
    if(res < 0)
        return res;
    
    ent->node->content = __av_strdup(path);
    ent->node->st.size = strlen(path);

    return 0;
}

static void vol_destroy(struct avfs *avfs)
{
    struct filesys *fs = (struct filesys *) avfs->data;

    vol_free_tree(fs->root);
    __av_unref_obj(fs->root);
    __av_free(fs);
}

extern int __av_init_module_volatile(struct vmodule *module);

int __av_init_module_volatile(struct vmodule *module)
{
    int res;
    struct avfs *avfs;
    struct filesys *fs;

    res = __av_new_avfs("volatile", NULL, AV_VER, AVF_ONLYROOT, module, &avfs);
    if(res < 0)
        return res;

    avfs->destroy = vol_destroy;

    AV_NEW(fs);

    avfs->data = (void *) fs;

    fs->root = vol_new_entry("/");
    fs->avfs = avfs;

    vol_make_node(fs, fs->root, 0755 | AV_IFDIR);

    avfs->lookup    = vol_lookup;
    avfs->putent    = vol_putent;
    avfs->copyent   = vol_copyent;
    avfs->getpath   = vol_getpath;
    
    avfs->open      = vol_open;
    avfs->close     = vol_close;
    avfs->read      = vol_read;
    avfs->write     = vol_write;
    avfs->readdir   = vol_readdir;
    avfs->getattr   = vol_getattr;
    avfs->setattr   = vol_setattr;
    avfs->truncate  = vol_truncate;

    avfs->access    = vol_access;
    avfs->readlink  = vol_readlink;
    avfs->unlink    = vol_unlink;
    avfs->rmdir     = vol_rmdir;
    avfs->mkdir     = vol_mkdir;
    avfs->mknod     = vol_mknod;
    avfs->rename    = vol_rename;
    avfs->link      = vol_link;
    avfs->symlink   = vol_symlink;

    __av_add_avfs(avfs);
    
    return 0;
}
