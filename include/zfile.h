/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/


#include "avfs.h"

struct zfile;
struct zcache;

avssize_t av_zfile_pread(struct zfile *fil, struct zcache *zc, char *buf,
                         avsize_t nbyte, avoff_t offset);

struct zfile *av_zfile_new(vfile *vf, avoff_t dataoff);
struct zcache *av_zcache_new();
avoff_t av_zcache_size(struct zcache *zc);
