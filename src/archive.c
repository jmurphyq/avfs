/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "archint.h"
#include "namespace.h"
#include "filecache.h"
#include "internal.h"
#include "oper.h"

static AV_LOCK_DECL(archlock);

static struct archent *arch_ventry_entry(ventry *ve)
{
    return (struct archent *) ve->data;
}

static void arch_free_tree(struct entry *parent)
{
    struct entry *ent;
    struct archnode *nod;

    ent = av_namespace_subdir(NULL, parent);
    while(ent != NULL) {
        struct entry *next;
        
        arch_free_tree(ent);
        next = av_namespace_next(ent);
        av_unref_obj(ent);
        ent = next;
    }
    
    nod = av_namespace_get(parent);
    av_unref_obj(nod);
    av_unref_obj(parent);
}

static void arch_delete(struct archive *arch)
{
    struct entry *root;

    root = av_namespace_subdir(arch->ns, NULL);
    arch_free_tree(root);
    av_unref_obj(root);
    av_unref_obj(arch->ns);
    AV_FREELOCK(arch->lock);
}

static int new_archive(ventry *ve, struct archive *arch)
{
    int res;
    struct archparams *ap = (struct archparams *) ve->mnt->avfs->data;
    struct entry *root;
    
    arch->ns = av_namespace_new();
    arch->avfs = ve->mnt->avfs;

    root = av_namespace_lookup(arch->ns, NULL, "");
    av_arch_default_dir(arch, root);
    av_unref_obj(root);

    res = ap->parse(ap->data, ve, arch);
    if(res < 0)
        return res;

    arch->flags |= ARCHF_READY;

    return 0;
}

static int arch_same(struct archive *arch, struct avstat *stbuf)
{
    if(arch->st.ino == stbuf->ino &&
       arch->st.dev == stbuf->dev &&
       arch->st.size == stbuf->size &&
       AV_TIME_EQ(arch->st.mtime, stbuf->mtime))
        return 1;
    else
        return 0;
}

/* FIXME: function common to archive and filter */
static int arch_getkey(ventry *ve, char **resp)
{
    int res;
    char *key;

    res = av_generate_path(ve->mnt->base, &key);
    if(res < 0)
        return res;

    key = av_stradd(key, AVFS_SEP_STR, ve->mnt->avfs->name, NULL);

    *resp = key;
    return 0;
}

static struct archive *do_get_archive(const char *key, struct avstat *stbuf)
{
    struct archive *arch;

    arch = (struct archive *) av_filecache_get(key);
    if(arch != NULL) {
        if(!arch_same(arch, stbuf)) {
            av_unref_obj(arch);
            arch = NULL;
        }
    }

    if(arch == NULL) {
        AV_NEW_OBJ(arch, arch_delete);
        AV_INITLOCK(arch->lock);
        arch->flags = 0;
        av_filecache_set(key, arch);
    }

    arch->st = *stbuf;

    return arch;
}

static int get_archive(ventry *ve, struct archive **archp)
{
    int res;
    char *key;
    struct avstat stbuf;
    struct archive *arch;

    res = av_getattr(ve->mnt->base, &stbuf, AVA_ALL, 0);
    if(res < 0)
        return res;

    res = arch_getkey(ve, &key);
    if(res < 0)
        return res;

    AV_LOCK(archlock);
    arch = do_get_archive(key, &stbuf);
    AV_UNLOCK(archlock);

    AV_LOCK(arch->lock);
    if(!(arch->flags & ARCHF_READY)) {
        res = new_archive(ve, arch);
        if(res < 0) {
            AV_UNLOCK(arch->lock);
            av_unref_obj(arch);
            av_filecache_set(key, NULL);
            arch = NULL;
        }
    }
    else
        res = 0;

    av_free(key);

    *archp = arch;
    
    return res;
}

static int lookup_check_node(struct entry *ent, const char *name)
{
    struct archnode *nod = av_namespace_get(ent);
    
    if(nod == NULL)
        return -ENOENT;
    
    if(name != NULL && !AV_ISDIR(nod->st.mode))
        return -ENOTDIR;

    return 0;
}

static int arch_lookup(ventry *ve, const char *name, void **newp)
{
    int res;
    int type;
    struct archent *ae = arch_ventry_entry(ve);
    struct entry *ent;
    struct archive *arch;
 
    if(ae == NULL) {
        if(name[0] != '\0')
            return -ENOENT;

        AV_NEW(ae);
        ae->ent = NULL;
        res = get_archive(ve, &arch);
        if(res < 0) {
            av_free(ae);
            return res;
        }
        ae->arch = arch;
    }
    else {
        arch = ae->arch;
        AV_LOCK(arch->lock);
        res = lookup_check_node(ae->ent, name);
        if(res < 0) {
            AV_UNLOCK(arch->lock);
            return res;
        }
    }

    ent = av_namespace_lookup_all(arch->ns, ae->ent, name);
    av_unref_obj(ae->ent);
    if(ent == NULL) {
        av_unref_obj(ae->arch);
        av_free(ae);
        ae = NULL;
        type = 0;
    }
    else {
        struct archnode *nod = av_namespace_get(ent);

        if(nod != NULL)
            type = AV_TYPE(nod->st.mode);
        else
            type = 0;

        ae->ent = ent;        
    }
    AV_UNLOCK(arch->lock);

    *newp = ae;
    return type;
}

static void arch_putent(ventry *ve)
{
    struct archent *ae = arch_ventry_entry(ve);

    av_unref_obj(ae->ent);
    av_unref_obj(ae->arch);

    av_free(ae);
}

static int arch_copyent(ventry *ve, void **resp)
{
    struct archent *ae = arch_ventry_entry(ve);
    struct archent *nae;

    AV_NEW(nae);
    nae->ent = ae->ent;
    nae->arch = ae->arch;
    
    av_ref_obj(nae->ent);
    av_ref_obj(nae->arch);

    return 0;
}

static int arch_getpath(ventry *ve, char **resp)
{
    struct archent *ae = arch_ventry_entry(ve);
    
    *resp = av_namespace_getpath(ae->ent);

    return 0;
}

static int arch_do_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    struct archent *ae = arch_ventry_entry(ve);
    struct archfile *fil;
    struct archnode *nod = av_namespace_get(ae->ent);
    struct archive *arch = ae->arch;
    vfile *basefile = NULL;
   
    if(nod == NULL)
        return -ENOENT;

    if(AV_ISWRITE(flags))
        return -EPERM;

    if((flags & AVO_DIRECTORY) != 0 && !AV_ISDIR(nod->st.mode))
        return -ENOTDIR;
    
    if((flags & AVO_DIRECTORY) == 0 && (flags & AVO_ACCMODE) != AVO_NOPERM) {
        if(arch->basefile == NULL) {
            res = av_open(ve->mnt->base, AVO_RDONLY, 0, &arch->basefile);
            if(res < 0)
                return res;
        }

        arch->numread ++;
        basefile = arch->basefile;
    }
    
    AV_NEW(fil);
    fil->basefile = basefile;
    fil->arch = arch;
    fil->nod = nod;
    
    if((flags & AVO_DIRECTORY))
        fil->ent = ae->ent;
    else
        fil->ent = NULL;

    av_ref_obj(fil->arch);
    av_ref_obj(fil->nod);
    av_ref_obj(fil->ent);

    *resp = fil;

    return 0;
}


static int arch_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    struct archent *ae = arch_ventry_entry(ve);
    struct archive *arch = ae->arch;
 
    AV_LOCK(arch->lock);
    res = arch_do_open(ve, flags, mode, resp);
    AV_UNLOCK(arch->lock);
    
    return res;
}

static int arch_do_close(vfile *vf)
{
    struct archfile *fil = arch_vfile_file(vf);
    struct archive *arch = fil->arch;

    if(fil->basefile != NULL) {
        arch->numread --;
        if(arch->numread == 0) {
            av_close(arch->basefile);
            arch->basefile = NULL;
        }
    }

    av_unref_obj(fil->arch);
    av_unref_obj(fil->nod);
    av_unref_obj(fil->ent);
    av_free(fil);

    return 0;
}

static int arch_close(vfile *vf)
{
    int res;
    struct archfile *fil = arch_vfile_file(vf);
    struct archive *arch = fil->arch;

    AV_LOCK(arch->lock);
    res = arch_do_close(vf);
    AV_UNLOCK(arch->lock);

    return res;
}


avssize_t av_arch_read(vfile *vf, char *buf, avsize_t nbyte)
{
    int res;
    avoff_t realoff;
    struct archfile *fil = arch_vfile_file(vf);
    struct archnode *nod = fil->nod;
    avsize_t nact;

    if(AV_ISDIR(nod->st.mode))
        return -EISDIR;

    if(nbyte == 0 || vf->ptr >= nod->realsize)
        return 0;

    realoff = vf->ptr + nod->offset;
    nact = AV_MIN(nbyte, (avsize_t) (nod->realsize - vf->ptr));

    res = av_pread(fil->basefile, buf, nact, realoff);
    if(res > 0)
        vf->ptr += res;

    return res;
}

static avssize_t arch_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct archfile *fil = arch_vfile_file(vf);
    struct archive *arch = fil->arch;
    struct archparams *ap = (struct archparams *) vf->mnt->avfs->data;
    
    AV_LOCK(arch->lock);
    res =  ap->read(vf, buf, nbyte);
    AV_UNLOCK(arch->lock);

    return res;
}

static struct archnode *arch_special_entry(int n, struct entry *ent,
                                           char **namep)
{
    struct archnode *nod;

    if(n == 0) {
        *namep = av_strdup(".");
        nod = av_namespace_get(ent);
        return nod;
    }
    else {
        struct entry *parent;

        *namep = av_strdup("..");
        parent = av_namespace_parent(ent);
        if(parent != NULL)
            nod = av_namespace_get(parent);
        else
            nod = av_namespace_get(ent);

        av_unref_obj(parent);
        return nod;
    }
}

static struct archnode *arch_nth_entry(int n, struct entry *parent,
                                       char **namep)
{
    struct entry *ent;
    struct archnode *nod;
    int i;

    if(n  < 2)
        return arch_special_entry(n, parent, namep);
    
    n -= 2;

    ent = av_namespace_subdir(NULL, parent);
    for(i = 0; i < n && ent != NULL; i++) {
        struct entry *nextent = av_namespace_next(ent);
        av_unref_obj(ent);
        ent = nextent;
    }

    if(ent == NULL)
        return NULL;

    *namep = av_namespace_name(ent);
    nod = av_namespace_get(ent);
    av_unref_obj(ent);

    return nod;
}

static int arch_readdir(vfile *vf, struct avdirent *buf)
{
    int res;
    struct archfile *fil = arch_vfile_file(vf);
    struct archive *arch = fil->arch;
    struct archnode *nod;
    char *name;

    AV_LOCK(arch->lock);
    nod = arch_nth_entry(vf->ptr, fil->ent, &name);
    if(nod == NULL)
        res = 0;
    else {
        buf->name = name;
        buf->ino = nod->st.ino;
        buf->type = AV_TYPE(nod->st.mode);
        
        vf->ptr ++;
        res = 1;
    }
    AV_UNLOCK(arch->lock);

    return res;
}

static int arch_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
     struct archfile *fil = arch_vfile_file(vf);
     struct archnode *nod = fil->nod;
     struct archive *arch = fil->arch;
    
     AV_LOCK(arch->lock);
     *buf = nod->st;
     AV_UNLOCK(arch->lock);

     return 0;
}

static int arch_access(ventry *ve, int amode)
{
    if((amode & AVW_OK) != 0)
        return -EACCES;

    return 0;
}

static int arch_readlink(ventry *ve, char **bufp)
{
    int res;
    struct archent *ae = arch_ventry_entry(ve);
    struct archnode *nod;
    struct archive *arch = ae->arch;

    AV_LOCK(arch->lock);
    nod = av_namespace_get(ae->ent);
    if(nod == NULL)
        res = -ENOENT;
    else if(!AV_ISLNK(nod->st.mode))
        res = -EINVAL;
    else {
        *bufp = av_strdup(nod->linkname);
        res = 0;
    }
    AV_UNLOCK(arch->lock);    

    return res;
}

static void arch_destroy(struct avfs *avfs)
{
    struct archparams *ap = (struct archparams *) avfs->data;

    av_unref_obj(ap->data);
    av_free(ap);
}


int av_archive_init(const char *name, struct ext_info *exts, int version,
                    struct vmodule *module, struct avfs **avfsp)
{
    int res;
    struct avfs *avfs;
    struct archparams *ap;

    res = av_new_avfs(name, exts, version, AVF_NOLOCK, module, &avfs);
    if(res < 0)
        return res;

    avfs->lookup    = arch_lookup;
    avfs->putent    = arch_putent;
    avfs->copyent   = arch_copyent;
    avfs->getpath   = arch_getpath;
    avfs->open      = arch_open;
    avfs->close     = arch_close;
    avfs->read      = arch_read;
    avfs->readdir   = arch_readdir;
    avfs->getattr   = arch_getattr;
    avfs->access    = arch_access;
    avfs->readlink  = arch_readlink;
    avfs->destroy   = arch_destroy;

    AV_NEW(ap);
    ap->data = NULL;
    ap->parse = NULL;
    ap->read = av_arch_read;
    ap->release = NULL;

    avfs->data = ap;

    *avfsp = avfs;
    
    return 0;
}

