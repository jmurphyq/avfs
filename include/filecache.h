/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

void *__av_filecache_get(const char *key);
void __av_filecache_set(const char *key, void *obj);
