/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "state.h"

struct stentry {
    char *param;
    struct entry *ent;
};

struct stfile {
    struct stentry *stent;
    char *contents;
    int modif;
};

static struct stentry *st_ventry_stentry(ventry *ve)
{
    return (struct stentry *) ve->data;
}

static struct namespace *st_ventry_namespace(ventry *ve)
{
    return (struct namespace *) ve->mnt->avfs->data;
}

static struct stfile *st_vfile_stfile(vfile *vf)
{
    return (struct stfile *) vf->data;
}

static struct namespace *st_vfile_namespace(vfile *vf)
{
    return (struct namespace *) vf->mnt->avfs->data;
}

static void st_free_stentry(struct stentry *stent)
{
    __av_free(stent->param);
    __av_unref_obj(stent->ent);
}

static int st_lookup(ventry *ve, const char *name, void **newp)
{
    struct stentry *stent = st_ventry_stentry(ve);
    struct namespace *ns = st_ventry_namespace(ve);
    struct stentry *newent;
 
    AV_NEW_OBJ(newent, st_free_stentry);
    if(stent != NULL) {
        newent->ent = __av_namespace_lookup(ns, stent->ent, name);
        newent->param = __av_strdup(stent->param);
    }
    else {
        newent->ent = NULL;
        newent->param = __av_strdup(name);
    }
    __av_unref_obj(stent);
    
    *newp = newent;

    return 0;
}

static int st_getpath(ventry *ve, char **resp)
{
    char *path;
    char *nspath;
    struct stentry *stent = st_ventry_stentry(ve);

    path = __av_strdup(stent->param);
    if(stent->ent != NULL) {
        nspath = __av_namespace_getpath(stent->ent);
        path = __av_stradd(path, "/", nspath);
        __av_free(nspath);
    }

    return 0;
}

static void st_putent(ventry *ve)
{
    struct stentry *stent = st_ventry_stentry(ve);

    __av_unref_obj(stent);
}

static int st_copyent(ventry *ve, void **resp)
{
    struct stentry *stent = st_ventry_stentry(ve);

    __av_ref_obj(stent);
    *resp = stent;

    return 0;
}

static int st_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    struct stentry *stent = st_ventry_stentry(ve);
    struct namespace *ns = st_ventry_namespace(ve);
    struct stfile *sf;
    struct entry *subdir;
    struct statefile *stf;
    char *contents;

    subdir = __av_namespace_subdir(ns, stent->ent);
    if(stent->ent != NULL)
        stf = (struct statefile *) __av_namespace_get(stent->ent);
    else
        stf = NULL;
    
    if(subdir == NULL && (stf == NULL || (flags & AVO_DIRECTORY) != 0))
        return -ENOENT;

    __av_unref_obj(subdir);

    contents = NULL;
    if(!(flags & AVO_DIRECTORY) && stf != NULL) {
        if(AV_ISWRITE(flags) && stf->set == NULL)
            return -EACCES;
            
        if((flags & AVO_TRUNC) != 0 || stf->get == NULL)
            contents = __av_strdup("");
        else {
            res = stf->get(stent->ent, stent->param, &contents);
            if(res < 0)
                return res;
        }
    }

    AV_NEW(sf);
    sf->stent = stent;
    sf->contents = contents;
    sf->modif = 0;
    __av_ref_obj(stent);

    if((flags & AVO_TRUNC) != 0)
        sf->modif = 1;

    *resp = sf;
    
    return 0;
}


static int st_close(vfile *vf)
{
    struct stfile *sf = st_vfile_stfile(vf);
    int res = 0;

    if(sf->modif && sf->stent->ent != NULL) {
        struct statefile *stf;

        stf = (struct statefile *) __av_namespace_get(sf->stent->ent);

        res = stf->set(sf->stent->ent, sf->stent->param, sf->contents);
    }

    __av_unref_obj(sf->stent);
    __av_free(sf->contents);
    __av_free(sf);

    return res;
}

static avssize_t st_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avoff_t nact;
    avoff_t size;
    struct stfile *sf = st_vfile_stfile(vf);

    if(sf->contents == NULL)
        return -EISDIR;

    size = strlen(sf->contents);
    if(vf->ptr >= size)
	return 0;
    
    nact = AV_MIN(nbyte, (avsize_t) (size - vf->ptr));
    
    memcpy(buf, sf->contents + vf->ptr, nact);
    
    vf->ptr += nact;
    
    return nact;
}

static avssize_t st_write(vfile *vf, const char *buf, avsize_t nbyte)
{
    avoff_t end;
    struct stfile *sf = st_vfile_stfile(vf);
    avoff_t size;

    size = strlen(sf->contents);
    if((vf->flags & AVO_APPEND) != 0)
        vf->ptr = size;

    end = vf->ptr + nbyte;
    if(end > size) {
        sf->contents = __av_realloc(sf->contents, end + 1);
        sf->contents[end] = '\0';
    }

    memcpy(sf->contents + vf->ptr, buf, nbyte);

    vf->ptr = end;
    sf->modif = 1;

    return nbyte;
}

static int st_truncate(vfile *vf, avoff_t length)
{
    struct stfile *sf = st_vfile_stfile(vf);
    avoff_t size;

    size = strlen(sf->contents);

    if(length < size)
        sf->contents[length] = '\0';

    sf->modif = 1;

    return 0;
}


static int st_readdir(vfile *vf, struct avdirent *buf)
{
    struct stfile *sf = st_vfile_stfile(vf);
    struct namespace *ns = st_vfile_namespace(vf);
    struct entry *ent;
    int n;

    ent = __av_namespace_subdir(ns, sf->stent->ent);
    for(n = vf->ptr; n > 0 && ent != NULL; n--) {
        struct entry *next;
        next = __av_namespace_next(ent);
        __av_unref_obj(ent);
        ent = next;
    }
    if(ent == NULL)
        return 0;
    
    buf->name = __av_namespace_name(ent);
    __av_unref_obj(ent);

    /* FIXME: Make ino be some hash function of param and entry */
    buf->ino = 0;
    buf->type = 0;
    
    vf->ptr ++;
    
    return 1;
}

static int st_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    struct stfile *sf = st_vfile_stfile(vf);
    struct statefile *stf;

    if(sf->stent->ent != NULL)
        stf = (struct statefile *) __av_namespace_get(sf->stent->ent);
    else
        stf = NULL;

    __av_default_stat(buf);
    /* FIXME: Make ino be some hash function of param and entry */
    buf->ino = 0;
    buf->dev = 0;
    if(stf != NULL) {
        if(stf->set != NULL)
            buf->mode = AV_IFREG | 0644;
        else
            buf->mode = AV_IFREG | 0444;
    }
    else
        buf->mode = AV_IFDIR | 0755;

    if(sf->contents != NULL) {
        buf->size = strlen(sf->contents);
        buf->blocks = AV_DIV(buf->size, 512);
    }
    buf->nlink = 1;

    return 0;
}

static int st_access(ventry *ve, int amode)
{
    struct stentry *stent = st_ventry_stentry(ve);
    struct namespace *ns = st_ventry_namespace(ve);
    struct entry *subdir;
    struct statefile *stf;

    subdir = __av_namespace_subdir(ns, stent->ent);
    if(stent->ent != NULL)
        stf = (struct statefile *) __av_namespace_get(stent->ent);
    else
        stf = NULL;
    
    if(subdir == NULL && stf == NULL)
        return -ENOENT;

    if((amode & AVW_OK) != 0 && stf != NULL && stf->set == NULL)
        return -EACCES;

    __av_unref_obj(subdir);

    return 0;
}

static void st_free_tree(struct namespace *ns, struct entry *ent)
{
    struct entry *next;
    void *data;

    ent = __av_namespace_subdir(ns, ent);
    while(ent != NULL) {
        st_free_tree(ns, ent);
        data = __av_namespace_get(ent);
        if(data != NULL) {
            __av_free(data);
            __av_unref_obj(ent);
        }
        next = __av_namespace_next(ent);
        __av_unref_obj(ent);
        ent = next;
    }
}


static void st_destroy(struct avfs *avfs)
{
    struct namespace *ns = (struct namespace *) avfs->data;

    st_free_tree(ns, NULL);

    __av_unref_obj(ns);
}

int __av_state_new(struct vmodule *module, const char *name,
                   struct namespace **resp, struct avfs **avfsp)
{
    int res;
    struct avfs *avfs;
    struct namespace *ns;

    res = __av_new_avfs(name, NULL, AV_VER, AVF_ONLYROOT, module, &avfs);
    if(res < 0)
        return res;

    ns = __av_namespace_new();

    __av_ref_obj(ns);
    avfs->data = ns;
    avfs->destroy = st_destroy;

    avfs->lookup    = st_lookup;
    avfs->putent    = st_putent;
    avfs->copyent   = st_copyent;
    avfs->getpath   = st_getpath;

    avfs->open      = st_open;
    avfs->close     = st_close;
    avfs->read      = st_read;
    avfs->write     = st_write;
    avfs->truncate  = st_truncate;
    avfs->readdir   = st_readdir;
    avfs->getattr   = st_getattr;
    avfs->access    = st_access;

    __av_add_avfs(avfs);

    *resp = ns;
    *avfsp = avfs;
    
    return 0;
}
