/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "archint.h"

static void archnode_destroy(struct archnode *nod)
{
    av_free(nod->linkname);
    av_unref_obj(nod->data);
}

struct archnode *av_arch_new_node(struct archive *arch, struct entry *ent)
{
    struct archnode *nod;

    nod = av_namespace_get(ent);
    if(nod != NULL) {
        av_unref_obj(nod);
        av_unref_obj(ent);
    }

    AV_NEW_OBJ(nod, archnode_destroy);

    av_default_stat(&nod->st);
    nod->linkname = NULL;
    nod->offset = 0;
    nod->realsize = 0;
    nod->data = NULL;
    nod->flags = 0;

    /* FIXME: This scheme will allocate the same device to a tar file
       inside a tarfile. While this is not fatal, 'find -xdev' would not do
       what is expected.  */

    nod->st.dev = arch->avfs->dev;
    nod->st.ino = av_new_ino(arch->avfs);

    nod->st.mode = 0644 | AV_IFREG;
    nod->st.uid = arch->st.uid;
    nod->st.gid = arch->st.gid;
    nod->st.mtime = arch->st.mtime;
    nod->st.atime = nod->st.mtime;
    nod->st.ctime = nod->st.mtime;

    av_namespace_set(ent, nod);
    av_ref_obj(ent);

    return nod;
}

struct archnode *av_arch_default_dir(struct archive *arch, struct entry *ent)
{
    struct archnode *nod;
    avmode_t mode;

    nod = av_arch_new_node(arch, ent);

    mode = (arch->st.mode & 0777) | AV_IFDIR;
    if (mode & 0400) mode |= 0100;
    if (mode & 0040) mode |= 0010;
    if (mode & 0004) mode |= 0001;

    nod->st.mode = mode;
    nod->flags |= ANOF_AUTODIR;

    return nod;
}


struct entry *av_arch_get_entry(struct archive *arch, const char *path)
{
    struct entry *ent;
    char *s, *p;
    char *pathdup = av_strdup(path);

    p = pathdup;
    ent = av_namespace_subdir(arch->ns, NULL);
    while(1) {
        struct entry *next;
        struct archnode *nod;
        char c;

        for(;*p == '/'; p++);
        for(s = p; *s && *s != '/'; s++);
        c = *s;
        *s = '\0';
        if(!*p)
            break;

        nod = av_namespace_get(ent);
        if(nod == NULL)
            av_arch_default_dir(arch, ent);
        else if(!AV_ISDIR(nod->st.mode)) {
            av_unref_obj(ent);
            ent = NULL;
            break;
        }
        
        next = av_namespace_lookup(NULL, ent, p);
        av_unref_obj(ent);
        ent = next;
        
        *s = c;
        p = s;
    }

    av_free(pathdup);

    return ent;
}
