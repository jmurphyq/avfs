/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

struct archive;

struct archparams {
    void *data;
    int (*parse) (void *data, ventry *base, struct archive *arch);
};

void av_archive_init(struct avfs *avfs);
