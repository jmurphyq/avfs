/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#define REM_LIST_SINGLE (1 << 0)
#define REM_LIST_PARENT (1 << 1)

struct direlement {
    char *name;
    char *linkname;
    struct avstat attr;
};

struct hostpath {
    char *host;
    char *path;
};

struct dirlist {
    int flags;
    struct hostpath hostpath;
    avsize_t num;
    struct direlement *ents;
};

struct getparam {
    struct hostpath hostpath;
    char *localname;
    void *data;
};


struct remote {
    void *data;
    char *name;

    int (*list) (struct remote *rem, struct dirlist *dl);
    int (*get) (struct remote *rem, struct getparam *gp);
    int (*wait) (struct remote *rem, void *data, avoff_t end);
    void (*destroy) (struct remote *rem);
};

int __av_remote_init(struct vmodule *module, struct remote *rem,
                     struct avfs **resp);
void __av_remote_add(struct dirlist *dl, const char *name,
                     const char *linkname, struct avstat *attr);
