/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

struct namespace;
struct entry;

struct namespace *__av_namespace_new();
struct entry *__av_namespace_lookup(struct namespace *ns, struct entry *parent,
                                    const char *name);
struct entry *__av_namespace_resolve(struct namespace *ns, const char *path);
char *__av_namespace_getpath(struct entry *ent);
void __av_namespace_set(struct entry *ent, void *data);
void *__av_namespace_get(struct entry *ent);
char *__av_namespace_name(struct entry *ent);
struct entry *__av_namespace_next(struct entry *ent);
struct entry *__av_namespace_subdir(struct namespace *ns, struct entry *ent);

