/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

struct cacheobj;

struct cacheobj *av_cacheobj_new(void *obj, const char *name);
void *av_cacheobj_get(struct cacheobj *cobj);
void av_cacheobj_setsize(struct cacheobj *cobj, avoff_t diskusage);
void av_cache_checkspace();
void av_cache_diskfull();
