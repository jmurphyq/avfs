/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    BZIP2 module
*/

#if 0
#include "vdev.h"
#include "filter.h"

extern int __av_init_module_bz2(ave *v);

int __av_init_module_bz2(ave *v)
{
    const char *bzip2_args[2];

    bzip2_args[0] = "bzip2";
    bzip2_args[1] = AVNULL;

    return __av_init_filt(v, "bz2", bzip2_args, AVNULL, AV_VER);
}
#endif
