/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "state.h"

#define AVFS_SEP_CHAR    '#'

#define AVFS_LOCK(avfs)   if(!(avfs->flags & AVF_NOLOCK)) AV_LOCK(avfs->lock)
#define AVFS_UNLOCK(avfs) if(!(avfs->flags & AVF_NOLOCK)) AV_UNLOCK(avfs->lock)

int __av_get_ventry(const char *path, int resolvelast, ventry **retp);
int __av_copy_vmount(struct vmount *mnt, struct vmount **retp);
void __av_free_vmount(struct vmount *mnt);
int __av_generate_path(ventry *ve, char **pathp);
void __av_default_avfs(struct avfs *avfs);
void __av_init_dynamic_modules();
void __av_close_all_files();
void __av_delete_tmpdir();
void __av_init_avfsstat();
void __av_init_logstat();
void __av_check_malloc();

void __av_avfsstat_register(const char *path, struct statefile *func);
