/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "namespace.h"

struct statefile {
    void *data;

    int   (*get) (struct entry *ent, const char *param, char **resp);
    int   (*set) (struct entry *ent, const char *param, const char *val);
};

int av_state_new(struct vmodule *module, const char *name,
                   struct namespace **resp, struct avfs **avfsp);
