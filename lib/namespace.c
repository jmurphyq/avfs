/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "namespace.h"
#include "avfs.h"

static AV_LOCK_DECL(namespace_lock);

struct entry {
    char *name;
    struct entry *subdir;
    struct entry *next;
    struct entry **prevp;
    struct entry *parent;
    void *data;
};

struct namespace {
    struct entry *root;
};

struct namespace *av_namespace_new()
{
    struct namespace *ns;

    AV_NEW_OBJ(ns, NULL);
    ns->root = NULL;
    
    return ns;
}

static void free_entry(struct entry *ent)
{
    AV_LOCK(namespace_lock);
    if(ent->prevp != NULL)
        *ent->prevp = ent->next;
    if(ent->next != NULL)
        ent->next->prevp = ent->prevp;
    AV_UNLOCK(namespace_lock);

    av_free(ent->name);
    av_unref_obj(ent->parent);
}

static struct entry *lookup_name(struct entry **basep, struct entry *prev,
                                 const char *name, unsigned int namelen)
{
    struct entry **entp;
    struct entry *ent = NULL;

    for(entp = basep; *entp != NULL; entp = &(*entp)->next)
	if(strncmp(name, (*entp)->name, namelen) == 0) {
	    ent = *entp;
	    av_ref_obj(ent);
            break;
        }
    
    if(ent == NULL) {
        AV_NEW_OBJ(ent, free_entry);
        
        ent->name = av_strndup(name, namelen);
        ent->subdir = NULL;
        ent->next = NULL;
        ent->prevp = entp;
        ent->parent = prev;
        
        *entp = ent;
        av_ref_obj(ent->parent);
    }

    return ent;
}

struct entry *av_namespace_lookup(struct namespace *ns, struct entry *prev,
                                    const char *name)
{
    struct entry **basep;
    struct entry *ent;

    AV_LOCK(namespace_lock);
    if(name == NULL) {
        ent = prev->parent;
        av_ref_obj(ent);
    }
    else {
        if(prev == NULL)
            basep = &ns->root;
        else
            basep = &prev->subdir;
        
        ent = lookup_name(basep, prev, name, strlen(name));
    }
    AV_UNLOCK(namespace_lock);

    return ent;
}

struct entry *av_namespace_lookup_all(struct namespace *ns, struct entry *prev,
                                      const char *name)
{
    if(name != NULL) {
        if(strcmp(name, ".") == 0) {
            av_ref_obj(prev);
            return prev;
        }
        if(strcmp(name, "..") == 0)
            name = NULL;
    }
    
    return av_namespace_lookup(ns, prev, name);
}

struct entry *av_namespace_resolve(struct namespace *ns, const char *path)
{
    struct entry **basep;
    struct entry *ent;
    const char *s;
    
    AV_LOCK(namespace_lock);
    basep = &ns->root;
    ent = NULL;
    while(*path) {
        struct entry *next;

        for(s = path; *s && *s != '/'; s++);
        next = lookup_name(basep, ent, path, s - path);
        av_unref_obj(ent);
        ent = next;
        basep = &ent->subdir;
        for(path = s; *path == '/'; path++);
    }
    AV_UNLOCK(namespace_lock);

    return ent;
}

static char *getpath(struct entry *ent)
{
    char *path;
    
    if(ent->parent == NULL)
        return av_strdup(ent->name);
    
    path = getpath(ent->parent);

    return av_stradd(path, "/", ent->name, NULL);
}

char *av_namespace_getpath(struct entry *ent)
{
    char *path;

    AV_LOCK(namespace_lock);
    path = getpath(ent);
    AV_UNLOCK(namespace_lock);

    return path;
}

void av_namespace_set(struct entry *ent, void *data)
{
    AV_LOCK(namespace_lock);
    ent->data = data;
    AV_UNLOCK(namespace_lock);
}

void *av_namespace_get(struct entry *ent)
{
    void *data;
    
    AV_LOCK(namespace_lock);
    data = ent->data;
    AV_UNLOCK(namespace_lock);

    return data;
}

char *av_namespace_name(struct entry *ent)
{
    return av_strdup(ent->name);
}

struct entry *av_namespace_next(struct entry *ent)
{
    struct entry *rent;

    AV_LOCK(namespace_lock);
    rent = ent->next;
    av_ref_obj(rent);
    AV_UNLOCK(namespace_lock);

    return rent;
}

struct entry *av_namespace_subdir(struct namespace *ns, struct entry *ent)
{
    struct entry *rent;

    AV_LOCK(namespace_lock);
    if(ent == NULL)
        rent = ns->root;
    else
        rent = ent->subdir;
    av_ref_obj(rent);
    AV_UNLOCK(namespace_lock);

    return rent;
}
