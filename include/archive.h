/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "namespace.h"

struct archive;
struct archnode;

struct archparams {
    void *data;
    int (*parse) (void *data, ventry *ent, struct archive *arch);
    avssize_t (*read)  (vfile *vf, char *buf, avsize_t nbyte);
    int (*release) (struct archive *arch, struct archnode *nod);
};

#define ANOF_DIRTY    (1 << 0)
#define ANOF_CREATED  (1 << 1)
#define ANOF_AUTODIR  (1 << 2)

struct archnode {
    struct avstat st;
    char *linkname;
    int flags;
    
    avoff_t offset;
    avoff_t realsize;

    void *data;
};

struct archfile {
    vfile *basefile;
    struct archive *arch;
    struct archnode *nod;
    struct entry *ent;     /* Only for readdir */
};


int av_archive_init(const char *name, struct ext_info *exts, int version,
                    struct vmodule *module, struct avfs **avfsp);

avssize_t av_arch_read(vfile *vf, char *buf, avsize_t nbyte);
struct archnode *av_arch_new_node(struct archive *arch, struct entry *ent,
                                  int isdir);
void av_arch_del_node(struct entry *ent);
struct entry *av_arch_resolve(struct archive *arch, const char *path,
                              int create);
int av_arch_isroot(struct archive *arch, struct entry *ent);

static inline struct archfile *arch_vfile_file(vfile *vf)
{
    return (struct archfile *) vf->data;
}

