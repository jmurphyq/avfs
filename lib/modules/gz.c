/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    GZIP module
*/
#if 0
#include "vdev.h"
#include "filter.h"

extern int __av_init_module_gz(ave *v);

int __av_init_module_gz(ave *v)
{
    const char *gzip_args[2];

    gzip_args[0] = "gzip";
    gzip_args[1] = AVNULL;

    return __av_init_filt(v, "gz", gzip_args, AVNULL, AV_VER);
}
#endif
