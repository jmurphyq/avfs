/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "internal.h"
#include "version.h"
#include <stdarg.h>
#include <string.h>

#define NEED_VER    90

/* FIXME: This is just a random value */
#define AVFS_MAJOR 0xa5f

static struct namespace *avfsstat_ns;

struct av_obj {
    int refctr;
    void (*destr)(void *);
};

static AV_LOCK_DECL(objlock);

int av_check_version(const char *modname, const char *name,
                       int version, int need_ver, int provide_ver)
{
    if(version < need_ver || version > provide_ver) {
        if(version < need_ver) 
            av_log(AVLOG_WARNING, 
                     "%s: %s has version %i. Needs to be at least %i.",
                     modname, name, version, need_ver);
        else
            av_log(AVLOG_WARNING, 
                     "%s: %s has version %i. Cannot handle above %i.",
                     modname, name, version, provide_ver);
    
        return -ENODEV;
    }
  
    return 0;
}

static struct ext_info *copy_exts(struct ext_info *exts)
{
    int i, num, len;
    struct ext_info *newexts;
    char *pp;

    if(exts == NULL)
        return NULL;

    len = 0;
    for(i = 0; exts[i].from != NULL; i++) {
        len += (strlen(exts[i].from) + 1);
        if(exts[i].to != NULL) len += (strlen(exts[i].to) + 1);
    }
    num = i;

    newexts = av_malloc((num + 1) * sizeof(*newexts) + len);

    pp = (char *) (&newexts[num + 1]);
  
    for(i = 0; i < num; i++) {
        strcpy(pp, exts[i].from);
        newexts[i].from = pp;
        pp += (strlen(pp) + 1);
        if(exts[i].to != NULL) {
            strcpy(pp, exts[i].to);
            newexts[i].to = pp;
            pp += (strlen(pp) + 1);
        }
        else newexts[i].to = NULL;
    }
    newexts[i].from = NULL;
    newexts[i].to = NULL;

    return newexts;
}

static void free_avfs(struct avfs *avfs)
{
    AVFS_LOCK(avfs);
    avfs->destroy(avfs);
    AVFS_UNLOCK(avfs);
    
    av_free(avfs->name);
    av_free(avfs->exts);

    av_unref_obj(avfs->module);
    if(!(avfs->flags & AVF_NOLOCK))
        AV_FREELOCK(avfs->lock);
}

static int new_minor()
{
    static AV_LOCK_DECL(lock);
    static int minor = 1;
    int res;

    AV_LOCK(lock);
    res = minor;
    minor++;
    AV_UNLOCK(lock);

    return res;
}

avino_t av_new_ino(struct avfs *avfs)
{
    static AV_LOCK_DECL(lock);
    avino_t res;

    AV_LOCK(lock);
    res = avfs->inoctr;
    avfs->inoctr++;
    AV_UNLOCK(lock);

    return res;
}

int av_new_avfs(const char *name, struct ext_info *exts, int version,
                  int flags, struct vmodule *module, struct avfs **retp)
{
    int ret;
    struct avfs *avfs;

    ret = av_check_version("CoreLib", name, version, NEED_VER, AV_VER);
    if(ret < 0)
        return ret;

    AV_NEW_OBJ(avfs, free_avfs);
    if(!(flags & AVF_NOLOCK))
        AV_INITLOCK(avfs->lock);

    avfs->name = av_strdup(name);

    avfs->exts = copy_exts(exts);
    avfs->data = NULL;
    avfs->version = version;
    avfs->flags = flags;
    avfs->module = module;
    avfs->dev = av_mkdev(AVFS_MAJOR, new_minor());
    avfs->inoctr = 2;

    av_ref_obj(module);
    
    av_default_avfs(avfs);

    *retp = avfs;
    return 0;
}

void av_init_avfsstat()
{
    struct avfs *avfs;

    av_state_new(NULL, "avfsstat", &avfsstat_ns, &avfs);
    av_unref_obj(avfsstat_ns);
}

void av_avfsstat_register(const char *path, struct statefile *func)
{
    struct entry *ent;
    struct statefile *stf;

    ent = av_namespace_resolve(avfsstat_ns, path);
    AV_NEW(stf);

    *stf = *func;
    av_namespace_set(ent, stf);
}

char *av_strdup(const char *s)
{
    char *ns;

    if(s == NULL)
        return NULL;
  
    ns = (char *) av_malloc(strlen(s) + 1);
    strcpy(ns, s);

    return ns;
}

char *av_strndup(const char *s, avsize_t len)
{
    char *ns;

    if(s == NULL)
        return NULL;
  
    ns = (char *) av_malloc(len + 1);
    strncpy(ns, s, len);
    
    ns[len] = '\0';

    return ns;
}

char *av_stradd(char *str, ...)
{
    va_list ap;
    unsigned int origlen;
    unsigned int len;
    char *s, *ns;

    origlen = 0;
    if(str != NULL)
        origlen = strlen(str);

    len = origlen;
    va_start(ap, str);
    do {
	s = va_arg(ap, char *);
	if(s != NULL)
            len += strlen(s);
    } while(s != NULL);
    va_end(ap);
  
    str = av_realloc(str, len + 1);
    ns = str + origlen;
    ns[0] = '\0';
    va_start(ap, str);
    do {
	s = va_arg(ap, char *);
	if(s != NULL) {
	    strcpy(ns, s);
            ns += strlen(ns);
        }
    } while(s != NULL);
    va_end(ap);
  
    return str;
}

void *av_new_obj(avsize_t nbyte, void (*destr)(void *))
{
    struct av_obj *ao;

    ao = (struct av_obj *) av_calloc(sizeof(*ao) + nbyte);
    ao->refctr = 1;
    ao->destr = destr;
    
    return (void *) (ao + 1);
}

void av_ref_obj(void *obj)
{
    if(obj != NULL) {
        struct av_obj *ao = ((struct av_obj *) obj) - 1;
        int refctr;
        
        AV_LOCK(objlock);
        if(ao->refctr > 0)
            ao->refctr ++;
        refctr = ao->refctr;
        AV_UNLOCK(objlock);

        if(refctr <= 0)
            av_log(AVLOG_ERROR, "Referencing deleted object (%p)", obj);
    }
}

void av_unref_obj(void *obj)
{
    if(obj != NULL) {
        struct av_obj *ao = ((struct av_obj *) obj) - 1;
        int refctr;

        AV_LOCK(objlock);
        if(ao->refctr >= 0)
            ao->refctr --;
        refctr = ao->refctr;
        AV_UNLOCK(objlock);
        
        if(refctr == 0) {
            if(ao->destr != NULL)
                ao->destr(obj);

            av_free(ao);
        }
        else if(refctr < 0)
            av_log(AVLOG_ERROR, "Unreferencing deleted object (%p)", obj);
    }
}
