/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "archive.h"

#define ARCHF_READY (1 << 0)

struct archive {
    int flags;
    avmutex lock;
    struct namespace *ns;
    struct avstat st;
    unsigned int numread;
    vfile *basefile;
    struct avfs *avfs;
};

struct archent {
    struct archive *arch;
    struct entry *ent;
};

struct archnode *av_arch_default_dir(struct archive *arch, struct entry *ent);
