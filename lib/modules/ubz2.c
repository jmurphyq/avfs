/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    UBZ2 module
*/

#include "filter.h"

extern int av_init_module_ubz2(struct vmodule *module);

int av_init_module_ubz2(struct vmodule *module)
{
    struct avfs *avfs;
    struct ext_info ubz2_exts[5];
    const char *ubz2_args[3];
    const char *bz2_args[2];

    ubz2_exts[0].from = ".bz2",  ubz2_exts[0].to = NULL;
    ubz2_exts[1].from = ".bz",   ubz2_exts[1].to = NULL;
    ubz2_exts[2].from = ".tbz2", ubz2_exts[2].to = ".tar";
    ubz2_exts[3].from = ".tbz",  ubz2_exts[3].to = ".tar";
    ubz2_exts[4].from = NULL;
  
    ubz2_args[0] = "bzip2";
    ubz2_args[1] = "-d";
    ubz2_args[2] = NULL;

    bz2_args[0] = "bzip2";
    bz2_args[1] = NULL;

    return av_init_filt(module, "ubz2", ubz2_args, bz2_args, ubz2_exts,
                          &avfs);
}
