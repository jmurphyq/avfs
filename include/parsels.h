/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

struct lscache;

struct lscache *av_new_lscache();
int av_parse_ls(struct lscache *cache,const char *line,
                  struct avstat *stbuf, char **filename, char **linkname);

