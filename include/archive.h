/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "namespace.h"

struct archive;

struct archparams {
    void *data;
    int (*parse) (void *data, ventry *ent, struct archive *arch);
};

#define ANOF_DIRTY    (1 << 0)
#define ANOF_CREATED  (1 << 1)
#define ANOF_AUTODIR  (1 << 2)

struct archnode {
    struct avstat st;
    char *linkname;
    int flags;
    
    avoff_t offset;
    avoff_t realsize;

    void *data;
};

void av_archive_init(struct avfs *avfs);
struct archnode *av_arch_new_node(struct archive *arch, struct entry *ent);
struct entry *av_arch_get_entry(struct archive *arch, const char *path);
