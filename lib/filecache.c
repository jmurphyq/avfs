/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "filecache.h"
#include "internal.h"

struct filecache {
    struct filecache *next;
    struct filecache *prev;
    
    char *key;
    void *obj;
};

static struct filecache fclist;
static AV_LOCK_DECL(fclock);

static void filecache_remove(struct filecache *fc)
{
    struct filecache *prev = fc->prev;
    struct filecache *next = fc->next;

    prev->next = next;
    next->prev = prev;
}

static void filecache_insert(struct filecache *fc)
{
    struct filecache *prev = &fclist;
    struct filecache *next = fclist.next;
    
    prev->next = fc;
    next->prev = fc;
    fc->prev = prev;
    fc->next = next;
}

static void filecache_delete(struct filecache *fc)
{
    filecache_remove(fc);

    __av_unref_obj(fc->obj);
    __av_free(fc->key);
    __av_free(fc);
}

static struct filecache *filecache_find(const char *key)
{
    struct filecache *fc;
    
    for(fc = fclist.next; fc != &fclist; fc = fc->next) {
        if(strcmp(fc->key, key) == 0)
            break;
    }

    if(fc->obj == NULL)
        return NULL;

    return fc;
}

void *__av_filecache_get(const char *key)
{
    struct filecache *fc;
    void *obj = NULL;
    
    AV_LOCK(fclock);
    fc = filecache_find(key);
    if(fc != NULL) {
        filecache_remove(fc);
        filecache_insert(fc);
        obj = fc->obj;
        __av_ref_obj(obj);
    }
    AV_UNLOCK(fclock);

    return obj;
}

void __av_filecache_set(const char *key, void *obj)
{
    struct filecache *oldfc;
    struct filecache *fc;

    if(obj == NULL)
        return;

    AV_NEW(fc);
    fc->key = __av_strdup(key);
    fc->obj = obj;
    __av_ref_obj(obj);
    
    AV_LOCK(fclock);
    oldfc = filecache_find(key);
    if(oldfc != NULL)
        filecache_delete(oldfc);
    
    filecache_insert(fc);
    AV_UNLOCK(fclock);
}

void __av_init_filecache()
{
    fclist.next = &fclist;
    fclist.prev = &fclist;
    fclist.obj = NULL;
    fclist.key = NULL;
}

void __av_destroy_filecache()
{
    AV_LOCK(fclock);
    while(fclist.next != &fclist)
        filecache_delete(fclist.next);
    AV_UNLOCK(fclock);
}
