/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    GUNZIP module
*/

#include "filter.h"

extern int __av_init_module_ugz(struct vmodule *module);

int __av_init_module_ugz(struct vmodule *module)
{
    struct avfs *avfs;
    struct ext_info ugz_exts[3];
    const char *ugz_args[3];

    ugz_exts[0].from = ".gz",  ugz_exts[0].to = NULL;
    ugz_exts[1].from = ".tgz", ugz_exts[1].to = ".tar";
    ugz_exts[2].from = NULL;
  
    ugz_args[0] = "gzip";
    ugz_args[1] = "-d";
    ugz_args[2] = NULL;

    return __av_init_filt(module, "ugz", ugz_args, ugz_exts, &avfs);
}
