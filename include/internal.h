/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "state.h"

#define AVFS_SEP_CHAR    '#'
#define AVFS_SEP_STR     "#"

#define AVFS_LOCK(avfs)   if(!(avfs->flags & AVF_NOLOCK)) AV_LOCK(avfs->lock)
#define AVFS_UNLOCK(avfs) if(!(avfs->flags & AVF_NOLOCK)) AV_UNLOCK(avfs->lock)

int av_get_ventry(const char *path, int resolvelast, ventry **retp);
int av_copy_vmount(struct vmount *mnt, struct vmount **retp);
void av_free_vmount(struct vmount *mnt);
int av_generate_path(ventry *ve, char **pathp);
void av_default_avfs(struct avfs *avfs);
void av_init_dynamic_modules();
void av_close_all_files();
void av_delete_tmpdir();
void av_init_avfsstat();
void av_init_logstat();
void av_init_cache();
void av_check_malloc();
void av_init_filecache();
void av_destroy_filecache();

void av_avfsstat_register(const char *path, struct statefile *func);
