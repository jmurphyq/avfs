/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include "internal.h"

#include "config.h"
#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif

#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros>
#endif

#define DEFAULT_LOGMASK (AVLOG_ERROR | AVLOG_WARNING)

static int loginited;
static int logmask;
static char *logfile;
static AV_LOCK_DECL(loglock);

static int logstat_get(struct entry *ent, const char *param, char **retp)
{
    char buf[32];
    
    AV_LOCK(loglock);
    sprintf(buf, "%02o\n", logmask);
    AV_UNLOCK(loglock);

    *retp = av_strdup(buf);
    return 0;
}

static int logstat_set(struct entry *ent, const char *param, const char *val)
{
    int mask;

    if(val[0] < '0' || val[0] > '7' || val[1] < '0' || val[1] > '7' ||
       (val[2] != '\0' && !isspace((int) val[2]))) 
        return -EIO;

    mask = (val[0] - '0') * 8 + (val[1] - '0');
    
    AV_LOCK(loglock);
    logmask = mask;
    AV_UNLOCK(loglock);

    return 0;
}

static int get_logmask()
{
    int mask;

    AV_LOCK(loglock);
    if(!loginited) {
	char *logenv;

        logmask = DEFAULT_LOGMASK;
	logenv = getenv("AVFS_DEBUG");
	if(logenv != NULL &&
	   logenv[0] >= '0' && logenv[0] <= '7' &&
	   logenv[1] >= '0' && logenv[1] <= '7' &&
	   logenv[2] == '\0') 
	    logmask = (logenv[0] - '0') * 8 + (logenv[1] - '0');

        logfile = getenv("AVFS_LOGFILE");
        if(logfile == NULL)
            openlog("avfs", LOG_CONS | LOG_PID, LOG_USER);

        loginited = 1;
    }
    mask = logmask;
    AV_UNLOCK(loglock);

    return mask;
}

#define LOGMSG_SIZE 1024
static void filelog(const char *filename, const char *msg)
{
    int fd;
    char buf[LOGMSG_SIZE + 128];

    fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if(fd != -1) {
        struct avtm tmbuf;

        av_localtime(time(NULL), &tmbuf);
        sprintf(buf, "%02i/%02i %02i:%02i:%02i avfs[%lu]: %s\n", 
                tmbuf.mon + 1, tmbuf.day, tmbuf.hour, tmbuf.min, tmbuf.sec,
                (unsigned long) getpid(), msg);
        
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

void av_init_logstat()
{
    struct statefile logstat;
    
    logstat.data = NULL;
    logstat.get = logstat_get;
    logstat.set = logstat_set;
    
    av_avfsstat_register("debug", &logstat);
}

void av_log(int type, const char *format, ...)
{
    va_list ap;
    char buf[LOGMSG_SIZE+1];
    int loglevel;
    int logmask;

    logmask = get_logmask();

    if((type & logmask) == 0)
        return;

    va_start(ap, format);
#ifdef HAVE_VSNPRINTF
    vsnprintf(buf, LOGMSG_SIZE, format, ap);
#else
    strncpy(buf, format, LOGMSG_SIZE);
#endif  
    buf[LOGMSG_SIZE] = '\0';
    va_end(ap);

    if((type & AVLOG_ERROR) != 0)
        loglevel = LOG_ERR;
    else if((type & AVLOG_WARNING) != 0)
        loglevel = LOG_WARNING;
    else
        loglevel = LOG_INFO;
    
    if(logfile == NULL)
        syslog(loglevel, buf);
    else
        filelog(logfile, buf);
}

avdev_t av_mkdev(int major, int minor)
{
    return makedev(major, minor);
}

void av_splitdev(avdev_t dev, int *majorp, int *minorp)
{
    *majorp = major(dev);
    *minorp = minor(dev);
}


char *av_get_config(const char *param)
{
    const char *val;

    val = NULL;

    if(strcmp(param, "moduledir") == 0) 
        val = MODULE_DIR;
    else if(strcmp(param, "compiledate") == 0) 
        val = COMPILE_DATE;
    else if(strcmp(param, "compilesystem") == 0) 
        val = COMPILE_SYSTEM;
  
    if(val == NULL)
        return NULL;

    return av_strdup(val);
}

void av_default_stat(struct avstat *stbuf)
{
    stbuf->dev = 0;
    stbuf->ino = 0;
    stbuf->mode = 0;
    stbuf->nlink = 0;
    stbuf->uid = getuid();
    stbuf->gid = getgid();
    stbuf->rdev = 0;
    stbuf->size = 0;
    stbuf->blksize = 512;
    stbuf->blocks = 0;
    av_curr_time(&stbuf->atime);
    stbuf->mtime = stbuf->atime;
    stbuf->ctime = stbuf->atime;
}

void av_curr_time(avtimestruc_t *tim)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    tim->sec = tv.tv_sec;
    tim->nsec = tv.tv_usec * 1000;
}

avtime_t av_time()
{
    return time(NULL);
}

void av_sleep(unsigned long msec)
{
    struct timespec rem;
    int res;

    rem.tv_sec = msec / 1000;
    rem.tv_nsec = (msec % 1000) * 1000 * 1000;

    do {
        struct timespec req;

        req = rem;
        res = nanosleep(&req, &rem);
    } while(res == -1 && errno == EINTR);
}


avtime_t av_mktime(struct avtm *tp)
{
    struct tm tms;
  
    tms.tm_sec  = tp->sec;
    tms.tm_min  = tp->min;
    tms.tm_hour = tp->hour;
    tms.tm_mday = tp->day;
    tms.tm_mon  = tp->mon;
    tms.tm_year = tp->year;
    tms.tm_isdst = -1;

    return mktime(&tms);
}

void av_localtime(avtime_t t, struct avtm *tp)
{
    struct tm tms;
  
    localtime_r(&t, &tms);
  
    tp->sec  = tms.tm_sec;
    tp->min  = tms.tm_min;
    tp->hour = tms.tm_hour;
    tp->day  = tms.tm_mday;
    tp->mon  = tms.tm_mon;
    tp->year = tms.tm_year;
}


void av_registerfd(int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}

#if 0
rep_file  *av_get_replacement(ave *v, ventry *vent, int needold)
{
    rep_file *rf;
  
    AV_NEW(rf);

    if(needold) {
        rf->outfd = av_get_tmpfd(v);
        rf->ve = vent;
    }
    else {
        rf->outfd = av_open(v, vent, AVO_WRONLY | AVO_TRUNC, 0);
        rf->ve = NULL;
    }
  
    if(rf->outfd == -1) {
        av_free(rf);
        return NULL;
    }
  
    return rf;
}

void av_del_replacement(rep_file *rf)
{
    if(rf != NULL) {
        av_close(AV_DUMMYV, rf->outfd);
        av_free(rf);
    }
}

#define BS 16384
static int copy_file(ave *v, int srcfd, int destfd)
{
    avbyte buf[BS];
    avssize_t res, wres;
    avoff_t offset;
  
    offset = 0;
    do {
        res = av_read(v, srcfd, buf, BS, offset);
        if(res > 0) {
            wres = av_write(v, destfd, buf, res, offset);
            if(wres == -1) return -1;
            if(wres != res) { /* Impossible */
                v->errn = EIO;
                return -1;
            }
            offset += res;
        }
    } while(res > 0);

    return res;
}


/* FIXME: Replace with rename() for local files (quicker, and more reliable) */
int av_replace_file(ave *v, rep_file *rf)
{
    int res;

    if(rf->ve != NULL) {
        int destfd;

        destfd = av_open(v, rf->ve, AVO_WRONLY | AVO_TRUNC, 0);
        if(destfd == -1) {
            av_del_replacement(rf);
            return -1;
        }
    
        res = copy_file(v, rf->outfd, destfd);
    
        av_close(AV_DUMMYV, destfd);
    }
    else
        res = 0;

    av_del_replacement(rf);

    return res;
}
#endif
