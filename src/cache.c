/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

/* 
   TODO:
   
   Virtual filesystem where all the cached files can be found.
*/

#include "cache.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>

struct cacheobj {
    void *obj;
    avoff_t diskusage;
    char *name;

    struct cacheobj *next;
    struct cacheobj *prev;
};

#define MBYTE (1024 * 1024)

static AV_LOCK_DECL(cachelock);
static struct cacheobj cachelist;
static avoff_t disk_cache_limit = 100 * MBYTE;
static avoff_t disk_keep_free = 10 * MBYTE;
static avoff_t disk_usage = 0;

static int cache_clear();

static int cache_getfunc(struct entry *ent, const char *param, char **retp)
{
    *retp = av_strdup("");
    return 0;
}

static int cache_setfunc(struct entry *ent, const char *param, const char *val)
{
    struct statefile *sf = (struct statefile *) av_namespace_get(ent);
    int (*func)() = (int (*)()) sf->data;
    
    if(strlen(val) > 0)
        return func();

    return 0;
}

static int cache_getoff(struct entry *ent, const char *param, char **retp)
{
    char buf[64];
    struct statefile *sf = (struct statefile *) av_namespace_get(ent);
    avoff_t *offp = (avoff_t *)  sf->data;

    AV_LOCK(cachelock);
    sprintf(buf, "%llu\n", *offp);
    AV_UNLOCK(cachelock);

    *retp = av_strdup(buf);
    return 0;
}

static int cache_setoff(struct entry *ent, const char *param, const char *val)
{
    struct statefile *sf = (struct statefile *) av_namespace_get(ent);
    avoff_t *offp = (avoff_t *) sf->data;
    avoff_t offval;
    char *end;
    
    offval = strtoll(val, &end, 0);
    if(end == val)
        return -EINVAL;
    if(*end == '\n')
        end ++;
    if(*end != '\0')
        return -EINVAL;
    if(offval < 0)
        return -EINVAL;

    AV_LOCK(cachelock);
    *offp = offval;
    AV_UNLOCK(cachelock);

    return 0;
}

void av_init_cache()
{
    struct statefile statf;

    cachelist.next = &cachelist;
    cachelist.prev = &cachelist;

    statf.get = cache_getoff;
    statf.set = cache_setoff;
 
    statf.data = &disk_cache_limit;
    av_avfsstat_register("cache/limit", &statf);
    
    statf.data = &disk_keep_free;
    av_avfsstat_register("cache/keep_free", &statf);

    statf.set = NULL;
    statf.data = &disk_usage;
    av_avfsstat_register("cache/usage", &statf);

    statf.set = cache_setfunc;
    statf.get = cache_getfunc;
    statf.data = cache_clear;
    av_avfsstat_register("cache/clear", &statf);
}

static void cacheobj_remove(struct cacheobj *cobj)
{
    struct cacheobj *next;
    struct cacheobj *prev;
    
    next = cobj->next;
    prev = cobj->prev;
    next->prev = prev;
    prev->next = next;
}

static void cacheobj_insert(struct cacheobj *cobj)
{
    struct cacheobj *next;
    struct cacheobj *prev;

    next = cachelist.next;
    prev = &cachelist;
    next->prev = cobj;
    prev->next = cobj;
    cobj->next = next;
    cobj->prev = prev;
}

static void cacheobj_free(struct cacheobj *cobj)
{
    av_unref_obj(cobj->obj);
    av_log(AVLOG_DEBUG, "got rid of cached object <%s> size %lli",
             cobj->name != NULL ? cobj->name : "?", cobj->diskusage);
    av_free(cobj->name);
}

static void cacheobj_delete(struct cacheobj *cobj)
{
    AV_LOCK(cachelock);
    if(cobj->obj != NULL) {
        cacheobj_remove(cobj);
        disk_usage -= cobj->diskusage;
    }
    AV_UNLOCK(cachelock);

    if(cobj->obj != NULL)
        cacheobj_free(cobj);
}

struct cacheobj *av_cacheobj_new(void *obj, const char *name)
{
    struct cacheobj *cobj;

    if(obj == NULL)
        return NULL;

    AV_NEW_OBJ(cobj, cacheobj_delete);
    cobj->obj = obj;
    cobj->diskusage = 0;
    cobj->name = av_strdup(name);
    av_ref_obj(obj);

    AV_LOCK(cachelock);
    cacheobj_insert(cobj);
    AV_UNLOCK(cachelock);

    return cobj;
}

static int cache_free_one()
{
    struct cacheobj *cobj;
    struct cacheobj tmpcobj;

    cobj = cachelist.prev;
    if(cobj == &cachelist)
        return 0;

    cacheobj_remove(cobj);
    disk_usage -= cobj->diskusage;
    tmpcobj = *cobj;
    cobj->obj = NULL;
    AV_UNLOCK(cachelock);
    cacheobj_free(&tmpcobj);
    AV_LOCK(cachelock);

    return 1;
}

static int cache_clear()
{
    AV_LOCK(cachelock);
    while(cache_free_one());
    AV_UNLOCK(cachelock);
    
    return 0;
}

static void cache_checkspace(int full)
{
    avoff_t tmpfree;
    avoff_t limit;
    avoff_t keepfree;
    
    if(full)
        tmpfree = 0;
    else
        tmpfree = av_tmp_free();
    
    keepfree = disk_keep_free;
    if(keepfree < 100 * 1024)
        keepfree = 100 * 1024;

    limit = disk_usage - disk_keep_free + tmpfree;
    if(disk_cache_limit < limit)
        limit = disk_cache_limit;
    
    while(disk_usage > limit)
        if(!cache_free_one())
            break;        
}


void av_cache_checkspace()
{
    AV_LOCK(cachelock);
    cache_checkspace(0);
    AV_UNLOCK(cachelock);
}

void av_cache_diskfull()
{
    AV_LOCK(cachelock);
    cache_checkspace(1);
    AV_UNLOCK(cachelock);
}


void av_cacheobj_setsize(struct cacheobj *cobj, avoff_t diskusage)
{
    AV_LOCK(cachelock);
    if(cobj->obj != NULL && cobj->diskusage != diskusage) {
        disk_usage -= cobj->diskusage;
        cobj->diskusage = diskusage;
        disk_usage += cobj->diskusage;
        
        cache_checkspace(0);
    }
    AV_UNLOCK(cachelock);
}

void *av_cacheobj_get(struct cacheobj *cobj)
{
    void *obj;

    if(cobj == NULL)
        return NULL;

    AV_LOCK(cachelock);
    obj = cobj->obj;
    if(obj != NULL) {
        cacheobj_remove(cobj);
        cacheobj_insert(cobj);
        av_ref_obj(obj);
    }
    AV_UNLOCK(cachelock);

    return obj;
}
