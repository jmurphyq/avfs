/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "serialfile.h"

struct filtdata {
    char **prog;
    char **revprog;
};

struct sfile *__av_filtprog_new(vfile *vf, struct filtdata *fitdat);
void __av_filtprog_change(struct sfile *sf, vfile *newvf);
