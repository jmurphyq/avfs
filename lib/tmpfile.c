/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

struct tmpdir {
    char *path;
    int ctr;
};

static AV_LOCK_DECL(tmplock);
static struct tmpdir *tmpdir;

static int unlink_recursive(const char *file)
{
    int res;
    DIR *dirp;
    struct dirent *ent;
    char *name;

    res = unlink(file);
    if(res == 0)
        return 0;

    res = rmdir(file);
    if(res == 0)
        return 0;

    dirp = opendir(file);
    if(dirp == NULL)
        return -1;

    while((ent = readdir(dirp)) != NULL) {
        name = ent->d_name;
    
        if(name[0] != '.' || (name[1] && (name[1] != '.' || name[2]))) {
            char *newname;

            newname = __av_stradd(NULL, file, "/", name, NULL);
            unlink_recursive(newname);
            __av_free(newname);
        }
    }
    closedir(dirp);

    return rmdir(file);
}


void __av_delete_tmpdir()
{
    AV_LOCK(tmplock);
    if(tmpdir != NULL) {
        unlink_recursive(tmpdir->path);
        __av_free(tmpdir->path);
        __av_free(tmpdir);
        tmpdir = NULL;
    }
    AV_UNLOCK(tmplock);
}

int __av_get_tmpfile(char **retp)
{
    int ret = 0;
    char buf[64];
  
    AV_LOCK(tmplock);
    if(tmpdir == NULL) {
        char *path;

        path = __av_strdup("/tmp/.avfs_tmp_XXXXXX");
        mktemp(path);
        if(path[0] == '\0') {
	    ret = -EIO;
	    __av_log(AVLOG_ERROR, "mktemp failed for temporary directory");
	}
	else if(mkdir(path, 0700) == -1) {
	    ret = -EIO;
	    __av_log(AVLOG_ERROR, "mkdir(%s) failed: %s", path,
		     strerror(errno));
	}
	else {
	    AV_NEW(tmpdir);
	    tmpdir->path = path;
	    tmpdir->ctr = 0;
	}
    }
    if(tmpdir != NULL) {
	sprintf(buf, "/atmp%06i", tmpdir->ctr++);
	*retp = __av_stradd(NULL, tmpdir->path, buf, NULL);
    }
    AV_UNLOCK(tmplock);

    return ret;
}


void __av_del_tmpfile(char *tmpf)
{
    if(tmpf != NULL) {
	if(unlink(tmpf) == -1)
	    rmdir(tmpf);
	
	__av_free(tmpf);
    }
}


int __av_get_tmpfd()
{
    char *tmpf;
    int res;

    res = __av_get_tmpfile(&tmpf);
    if(res < 0)
        return res;
  
    res = open(tmpf, O_RDWR | O_CREAT | O_EXCL, 0600);
    if(res == -1)
        res = -errno;
    __av_del_tmpfile(tmpf);
  
    return res;
}

avoff_t __av_tmp_free()
{
    int res;
    struct statvfs stbuf;
    avoff_t freebytes = -1;

    AV_LOCK(tmplock);
    if(tmpdir != NULL) {
        res = statvfs(tmpdir->path, &stbuf);
        if(res != -1)
            freebytes = (avoff_t) stbuf.f_bavail * (avoff_t) stbuf.f_frsize;
    }
    AV_UNLOCK(tmplock);

#if 0    
    if(freebytes != -1)
        __av_log(AVLOG_DEBUG, "free bytes in tmp directory: %lli", freebytes);
#endif

    return freebytes;
}
