/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    UNZ (uncompress) module 
    using gzip
*/
#if 0
#include "vdev.h"
#include "filter.h"

extern int __av_init_module_uz(ave *v);

int __av_init_module_uz(ave *v)
{
    struct ext_info unz_exts[5];
    const char *unz_args[3];

    INIT_EXT(unz_exts[0], ".Z", AVNULL);
    INIT_EXT(unz_exts[1], ".tpz", ".tar");
    INIT_EXT(unz_exts[2], ".tz", ".tar");
    INIT_EXT(unz_exts[3], ".taz", ".tar");
    INIT_EXT(unz_exts[4], AVNULL, AVNULL);

    unz_args[0] = "gzip";
    unz_args[1] = "-d";
    unz_args[2] = AVNULL;

    return __av_init_filt(v, "uz", unz_args, unz_exts, AV_VER);
}
#endif
