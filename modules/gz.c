/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    GZ module
*/

#include "filter.h"

extern int av_init_module_gz(struct vmodule *module);

int av_init_module_gz(struct vmodule *module)
{
    struct avfs *avfs;
    const char *ugz_args[3];
    const char *gz_args[2];

    ugz_args[0] = "gzip";
    ugz_args[1] = "-d";
    ugz_args[2] = NULL;

    gz_args[0] = "gzip";
    gz_args[1] = NULL;

    return av_init_filt(module, "gz", gz_args, ugz_args, NULL, &avfs);
}
