/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    STRING module (vfs layer)
*/

#include "avfs.h"

typedef struct _cacheobj cacheobj;

struct cache_params {
    avoff_t diskusage;
    const char *name;
};

cacheobj *__av_new_cacheobj(void *obj);
void __av_del_cacheobj(cacheobj *cobj);
void __av_cache_init_params(struct cache_params *params);
void __av_cacheobj_set_params(cacheobj *cobj, struct cache_params *params);
void *__av_cacheobj_get(cacheobj *cobj);
void __av_cacheobj_put(cacheobj *cobj);
