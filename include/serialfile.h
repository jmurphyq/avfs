/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

#define SFILE_NOCACHE (1 << 0)

struct sfilefuncs {
    int       (*startget) (void *data, void **resp);
    avssize_t (*read)     (void *data, char *buf, avsize_t nbyte);
    int       (*startput) (void *data, void **resp);
    avssize_t (*write)    (void *data, const char *buf, avsize_t nbyte);
    int       (*endput)   (void *data);
};

struct sfile;

struct sfile *__av_sfile_new(struct sfilefuncs *func, void *data, int flags);
avssize_t __av_sfile_pread(struct sfile *fil, char *buf, avsize_t nbyte,
                           avoff_t offset);
avssize_t __av_sfile_pwrite(struct sfile *fil, const char *buf, avsize_t nbyte,
                            avoff_t offset);
avoff_t __av_sfile_size(struct sfile *fil);
int __av_sfile_truncate(struct sfile *fil, avoff_t length);
int __av_sfile_startget(struct sfile *fil);
int __av_sfile_flush(struct sfile *fil);
void *__av_sfile_getdata(struct sfile *fil);
