/*  
    AVFS: A Virtual File System Library
    Copyright (C) 2018 Ralf Hoffmann <ralf@boomerangsworld.de>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
    
    UZSTDE module
*/

#include "filter.h"
#include "version.h"

extern int av_init_module_uzstde(struct vmodule *module);

int av_init_module_uzstde(struct vmodule *module)
{
    struct avfs *avfs;
    const char *uzstde_args[4];
    const char *zstde_args[3];
    struct ext_info uzstde_exts[2];

    uzstde_args[0] = "zstd";
    uzstde_args[1] = "-d";
    uzstde_args[2] = "-c";
    uzstde_args[3] = NULL;

    zstde_args[0] = "zstd";
    zstde_args[1] = "-c";
    zstde_args[2] = NULL;

    uzstde_exts[0].from = ".zstd",   uzstde_exts[2].to = NULL;
    uzstde_exts[1].from = NULL;

    return av_init_filt(module, AV_VER, "uzstde", uzstde_args, zstde_args,
                        uzstde_exts, &avfs);
}
