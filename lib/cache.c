/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "cache.h"

struct _cacheobj {
    void *obj;
    avoff_t diskusage;
    avtimestruc_t lastaccess;
    char *name;

    cacheobj *next;
    cacheobj **prevp;
};

#define MBYTE (1024 * 1024)

static AV_LOCK_DECL(cachelock);
static cacheobj *cachefirst;
static avoff_t disk_cache_limit = 100 * MBYTE;
static avoff_t disk_keep_free = 10 * MBYTE;
static avoff_t disk_usage = 0;

cacheobj *__av_new_cacheobj(void *obj)
{
    cacheobj *cobj;

    AV_NEW(cobj);
    cobj->obj = obj;
    cobj->diskusage = 0;
    __av_curr_time(&cobj->lastaccess);
    __av_ref_obj(obj);

    AV_LOCK(cachelock);
    cobj->prevp = &cachefirst;
    cobj->next = cachefirst;
    if(cachefirst != NULL)
        cachefirst->prevp = &cobj->next;
    cachefirst = cobj;
    AV_UNLOCK(cachelock);

    return cobj;
}

static void unlink_cacheobj(cacheobj *cobj)
{
    if(cobj->next != NULL)
        cobj->next->prevp = cobj->prevp;
    if(cobj->prevp != NULL)
        *cobj->prevp = cobj->next;

    disk_usage -= cobj->diskusage;
}

void __av_del_cacheobj(cacheobj *cobj)
{
    if(cobj != NULL) {
        AV_LOCK(cachelock);
        unlink_cacheobj(cobj);
        AV_UNLOCK(cachelock);
        
        __av_unref_obj(cobj->obj);
        
        if(cobj->obj != NULL) 
            __av_log(AVLOG_DEBUG, "got rid of cached object <%s> size %lli",
                     cobj->name != NULL ? cobj->name : "", cobj->diskusage);
        
        __av_free(cobj->name);
        __av_free(cobj);
    }
}

static int cache_try_free(cacheobj *keepobj)
{
    cacheobj *cobj;
    cacheobj *oldest;
    void *obj;
    char *name;
    avoff_t size;

    oldest = NULL;

    for(cobj = cachefirst; cobj != NULL; cobj = cobj->next) {
        if(cobj != keepobj && cobj->diskusage != 0 &&
           (oldest == NULL || 
            AV_TIME_LESS(cobj->lastaccess, oldest->lastaccess)))
            oldest = cobj;
    }
    if(oldest == NULL)
        return 0;
    
    unlink_cacheobj(oldest);
    obj = oldest->obj;
    name = oldest->name;
    size = oldest->diskusage;
    
    oldest->obj = NULL;
    oldest->name = NULL;
    oldest->diskusage = 0;
    
    AV_UNLOCK(cachelock);

    __av_unref_obj(obj);

    if(obj != NULL) 
        __av_log(AVLOG_DEBUG, "got rid of cached object <%s> size %lli",
                 name != NULL ? name : "", size);
    
    __av_free(name);

    AV_LOCK(cachelock);
    return 1;
}

void __av_cache_init_params(struct cache_params *params)
{
    params->diskusage = 0;
    params->name = NULL;
}

static void cacheobj_set_diskusage(cacheobj *cobj, avoff_t diskusage)
{
    /* under cachelock */
    static avoff_t tmpfree;
    static avtime_t lastcheck;
    avoff_t limit;
    avtime_t now;

    now = __av_time();
    if(lastcheck != now) {
        tmpfree = __av_tmp_free();
        lastcheck = now;
    }

    disk_usage -= cobj->diskusage;
    cobj->diskusage = diskusage;
    disk_usage += cobj->diskusage;
    
    limit = disk_usage - disk_keep_free + tmpfree;
    if(disk_cache_limit < limit)
        limit = disk_cache_limit;
    
    while(disk_usage > limit)
        if(!cache_try_free(cobj))
            break;        
}

static void cacheobj_set_name(cacheobj *cobj, const char *name)
{
    __av_free(cobj->name);
    cobj->name = __av_strdup(name);
}

void __av_cacheobj_set_params(cacheobj *cobj, struct cache_params *params)
{
    AV_LOCK(cachelock);
    if(params->diskusage > 0) {
        avoff_t rounddiskusage = AV_DIV(params->diskusage, 4096) * 4096;

        if(cobj->diskusage != rounddiskusage)
            cacheobj_set_diskusage(cobj, rounddiskusage);
    }
    if(params->name != NULL) 
        cacheobj_set_name(cobj, params->name);
    AV_UNLOCK(cachelock);
}

void *__av_cacheobj_get(cacheobj *cobj)
{
    void *obj;

    if(cobj == NULL)
        return NULL;

    AV_LOCK(cachelock);
    __av_curr_time(&cobj->lastaccess);
    obj = cobj->obj;
    __av_ref_obj(obj);
    AV_UNLOCK(cachelock);

    return obj;
}
