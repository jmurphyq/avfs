/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    BUNZIP2 module
*/
#if 0
#include "vdev.h"
#include "filter.h"

extern int __av_init_module_ubz2(ave *v);

int __av_init_module_ubz2(ave *v)
{
    struct ext_info bunzip2_exts[5];
    const char *bunzip2_args[3];

    INIT_EXT(bunzip2_exts[0], ".bz2", AVNULL);
    INIT_EXT(bunzip2_exts[1], ".bz", AVNULL);
    INIT_EXT(bunzip2_exts[2], ".tbz2", ".tar");
    INIT_EXT(bunzip2_exts[3], ".tbz", ".tar");
    INIT_EXT(bunzip2_exts[4], AVNULL, AVNULL);

    bunzip2_args[0] = "bzip2";
    bunzip2_args[1] = "-d";
    bunzip2_args[2] = AVNULL;

    return __av_init_filt(v, "ubz2", bunzip2_args, bunzip2_exts, AV_VER);
}

#endif
