/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "archive.h"
#include "namespace.h"
#include "filecache.h"
#include "internal.h"
#include "oper.h"


struct archive {
    struct namespace *ns;
    struct avstat sig;
};

struct archnode {
    struct avstat st;
};

struct archent {
    struct archive *arch;
    struct entry *ent;
};

static struct archent *arch_ventry_entry(ventry *ve)
{
    return (struct archent *) ve->data;
}

static void arch_destroy(struct archive *arch)
{
    av_unref_obj(arch->ns);
}

static int new_archive(ventry *ve, struct archive **archp, const char *key,
                       struct avstat *stbuf)
{
    int res;
    struct archive *arch;
    struct archparams *ap = (struct archparams *) ve->mnt->avfs->data;
    
    AV_NEW_OBJ(arch, arch_destroy);
    
    arch->ns = av_namespace_new();
    arch->sig = *stbuf;

    res = ap->parse(ap->data, ve->mnt->base, arch);
    if(res < 0) {
        av_unref_obj(arch);
        return res;
    }

    av_filecache_set(key, arch);

    *archp = arch;
    return 0;
}

static int arch_same(struct archive *arch, struct avstat *stbuf)
{
    if(arch->sig.ino == stbuf->ino &&
       arch->sig.dev == stbuf->dev &&
       arch->sig.size == stbuf->size &&
       AV_TIME_EQ(arch->sig.mtime, stbuf->mtime))
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


static int get_archive(ventry *ve, struct archive **archp)
{
    int res;
    char *key;
    struct avstat stbuf;
    int attrmask = AVA_INO | AVA_DEV | AVA_SIZE | AVA_MTIME;
    struct archive *arch;

    res = av_getattr(ve->mnt->base, &stbuf, attrmask, 0);
    if(res < 0)
        return res;

    res = arch_getkey(ve, &key);
    if(res < 0)
        return res;

    arch = (struct archive *) av_filecache_get(key);
    if(arch != NULL) {
        if(!arch_same(arch, &stbuf)) {
            av_unref_obj(arch);
            arch = NULL;
        }
    }

    if(arch == NULL)
        res = new_archive(ve, archp, key, &stbuf);
    
    av_free(key);
    
    return res;
}

static int arch_lookup(ventry *ve, const char *name, void **newp)
{
    int res;
    int type;
    struct archent *ae = arch_ventry_entry(ve);
    struct entry *ent;
 
    if(ae == NULL) {
        if(name[0] != '\0')
            return -ENOENT;

        AV_NEW(ae);
        ae->ent = NULL;
        res = get_archive(ve, &ae->arch);
        if(res < 0) {
            av_free(ae);
            return res;
        }
    }

    ent = av_namespace_lookup_all(ae->arch->ns, ae->ent, name);
    av_unref_obj(ae->ent);
    if(ent == NULL) {
        av_unref_obj(ae->arch);
        av_free(ae);
        ae = NULL;
        type = 0;
    }
    else {
        struct archnode *nod = av_namespace_get(ent);

        type = AV_TYPE(nod->st.mode);
        ae->ent = ent;        
    }

    *newp = ae;
    return 0;
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


void av_archive_init(struct avfs *avfs)
{
    avfs->lookup    = arch_lookup;
    avfs->putent    = arch_putent;
    avfs->copyent   = arch_copyent;
    avfs->getpath   = arch_getpath;
}

