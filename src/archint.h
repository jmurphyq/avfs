/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "archive.h"

struct archive {
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

struct archfile {
    struct archive *arch;
    struct archnode *nod;
    struct entry *ent;     /* Only for readdir */
};

struct archnode *av_arch_default_dir(struct archive *arch, struct entry *ent);
