/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

void *__av_filecache_get(void *id, ventry *ent);
void __av_filecache_del(void *obj);
void __av_filecache_set(void *id, ventry *ent, void *obj);
void __av_filecache_freeall(void *id);
