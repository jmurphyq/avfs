/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include "avfs.h"

struct cacheobj;

struct cacheobj *av_cacheobj_new(void *obj, const char *name);
void *av_cacheobj_get(struct cacheobj *cobj);
void av_cacheobj_setsize(struct cacheobj *cobj, avoff_t diskusage);
void av_cache_checkspace();
void av_cache_diskfull();
