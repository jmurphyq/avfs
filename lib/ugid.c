/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define NAMLEN 256

struct ugidcache {
    int uid;
    char *uname;
    int myuid;

    int gid;
    char *gname;
    int mygid;
};

static void free_ugidcache(struct ugidcache *cache)
{
    __av_free(cache->uname);
    __av_free(cache->gname);
}

struct ugidcache *__av_new_ugidcache()
{
    struct ugidcache *cache;

    AV_NEW_OBJ(cache, free_ugidcache);

    cache->uname = NULL;
    cache->myuid = getuid();
  
    cache->gname = NULL;
    cache->mygid = getgid();

    return cache;
}

char *__av_finduname(struct ugidcache *cache, int uid, const char *deflt)
{
    if(cache->uname == NULL || uid != cache->uid) {
        int res;
        struct passwd pw;
        struct passwd *pwres;
        char *buf = NULL;
        size_t bufsize = 0;

        do {
            bufsize += 256;
            buf = __av_realloc(buf, bufsize);
            res = getpwuid_r(uid, &pw, buf, bufsize, &pwres);
        } while(res == ERANGE);

        if(res != 0 || pwres == NULL) {
            __av_free(buf);
            return __av_strdup(deflt);
        }

        __av_free(cache->uname);
        cache->uid = pwres->pw_uid;
        cache->uname = __av_strdup(pwres->pw_name);
        __av_free(buf);
    }
    
    return __av_strdup(cache->uname);
}

int __av_finduid(struct ugidcache *cache, const char *uname, int deflt)
{
    if(uname == NULL || !uname[0])
        return deflt == -1 ? cache->myuid : deflt;

    if(cache->uname == NULL || strcmp(uname, cache->uname) != 0) {
        int res;
        struct passwd pw;
        struct passwd *pwres;
        char *buf = NULL;
        size_t bufsize = 0;

        do {
            bufsize += 256;
            buf = __av_realloc(buf, bufsize);
            res = getpwnam_r(uname, &pw, buf, bufsize, &pwres);
        } while(res == ERANGE);

        if(res != 0 || pwres == NULL) {
            __av_free(buf);
            return deflt == -1 ? cache->myuid : deflt;
        }

        __av_free(cache->uname);
        cache->uid = pwres->pw_uid;
        cache->uname = __av_strdup(pwres->pw_name);
        __av_free(buf);
    }

    return cache->uid;
}

char *__av_findgname(struct ugidcache *cache, int gid, const char *deflt)
{
    if(cache->gname == NULL || gid != cache->gid) {
        int res;
        struct group gr;
        struct group *grres;
        char *buf = NULL;
        size_t bufsize = 0;

        do {
            bufsize += 256;
            buf = __av_realloc(buf, bufsize);
            res = getgrgid_r(gid, &gr, buf, bufsize, &grres);
        } while(res == ERANGE);

        if(res != 0 || grres == NULL) {
            __av_free(buf);
            return __av_strdup(deflt);
        }

        __av_free(cache->gname);
        cache->gid = grres->gr_gid;
        cache->gname = __av_strdup(grres->gr_name);
        __av_free(buf);
    }
    
    return __av_strdup(cache->gname);
}

int __av_findgid(struct ugidcache *cache, const char *gname, int deflt)
{
    if(gname == NULL || !gname[0])
        return deflt == -1 ? cache->mygid : deflt;

    if(cache->gname == NULL || strcmp(gname, cache->gname) != 0) {
        int res;
        struct group gr;
        struct group *grres;
        char *buf = NULL;
        size_t bufsize = 0;

        do {
            bufsize += 256;
            buf = __av_realloc(buf, bufsize);
            res = getgrnam_r(gname, &gr, buf, bufsize, &grres);
        } while(res == ERANGE);

        if(res != 0 || grres == NULL) {
            __av_free(buf);
            return deflt == -1 ? cache->mygid : deflt;
        }

        __av_free(cache->gname);
        cache->gid = grres->gr_gid;
        cache->gname = __av_strdup(grres->gr_name);
        __av_free(buf);
    }

    return cache->gid;
}

