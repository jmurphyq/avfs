/*
    AVFS: A Virtual File System Library
    Copyright (C) 2000-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"
#include "cache.h"
#include "filebuf.h"
#include "socket.h"
#include "serialfile.h"

#include <stdlib.h>
#include <unistd.h>

#define HTTP_READ_TIMEOUT 20000

struct localfile {
    struct filebuf *sockfb;
    struct entry *ent;
};

struct entry {
    char *url;
    struct cacheobj *cobj;
    avoff_t size;
    struct entry *next;
};

struct filesys {
    struct entry *ents;
    char *proxyname;
};

struct file {
    struct filesys *fs;
    struct entry *ent;
};

static int write_socket(int sock, const char *buf, avsize_t buflen)
{
    int res;

    while(buflen > 0) {
        res = write(sock, buf, buflen);
        if(res == -1)
            return -errno;
        
        buf += res;
        buflen -= res;
    }

    return 0;
}

static void strip_crlf(char *line)
{
    avsize_t len = strlen(line);
    
    if(len > 0 && line[len-1] == '\n') {
        if(len > 1 && line[len-2] == '\r')
            line[len-2] = '\0';
        else
            line[len-1] = '\0';
    }
}

static int http_get_line(struct localfile *lf, char **linep)
{
    int res;
    char *line;

    while(1) {        
        res = __av_filebuf_readline(lf->sockfb, &line);
        if(res < 0)
            return res;
        if(res == 1)
            break;

        if(__av_filebuf_eof(lf->sockfb)) {
            __av_log(AVLOG_ERROR, "HTTP: connection closed in header");
            return -EIO;
        }

        res = __av_filebuf_check(&lf->sockfb, 1, HTTP_READ_TIMEOUT);
        if(res < 0)
            return res;

        if(res == 0) {
            __av_log(AVLOG_ERROR, "HTTP: timeout in header");
            return -EIO;
        }
    }
    
    strip_crlf(line);

    __av_log(AVLOG_DEBUG, "HTTP: %s", line);
    *linep = line;

    return 0;
}

static char *http_split_header(char *line)
{
    char *s;

    for(s = line; *s && !isspace((int) *s); s++);
    if(*s) {
        do {
            *s = '\0';
            s++;
        } while(isspace((int) *s));
    }
    
    return s;
}

static void http_process_header_line(struct localfile *lf, char *line)
{
    char *s;

    s = http_split_header(line);

    if(strcasecmp("content-length:", line) == 0) {
        char *end;
        avoff_t size;
        size = strtol(s, &end, 10);
        while(*end && isspace((int) *end))
            end++;
        
        if(!*end)
            lf->ent->size = size;
    }
}

static int http_check_header_line(struct localfile *lf)
{
    int res;
    char *line;

    res = http_get_line(lf, &line);
    if(res < 0)
        return res;

    if(line[0] == '\0')
        res = 0;
    else {
        http_process_header_line(lf, line);
        res = 1;
    }
    __av_free(line);

    return res;
}

static int http_ignore_header(struct localfile *lf)
{
    int res;
    char *line;
    int end = 0;

    do {
        res = http_get_line(lf, &line);
        if(res < 0)
            return res;

        if(line[0] == '\0')
            end = 1;

        __av_free(line);
    } while(!end);

    return 0;
}

static int http_process_status_line(struct localfile *lf, char *line)
{
    const char *s;
    int statuscode;
    int res;

    for(s = line; *s && *s != ' '; s++);

    if(s[0] != ' ' || !isdigit((int) s[1]) || !isdigit((int) s[2]) || 
       !isdigit((int) s[3])) {
        __av_log(AVLOG_ERROR, "HTTP: bad status code: %s", s);
        return -EIO;
    }
    
    statuscode = (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    
    __av_log(AVLOG_DEBUG, "HTTP: status code: %i", statuscode);

    if(statuscode / 100 == 1) {
        res = http_ignore_header(lf);
        if(res < 0)
            return res;
        
        return 0;
    }
    
    if(statuscode / 100 == 2)
        return 1;

    __av_log(AVLOG_WARNING, "HTTP: error: %s", s);
    http_ignore_header(lf);

    if(statuscode / 100 == 3 || statuscode / 100 == 4)
        return -ENOENT;

    return -EIO;
}

static int http_check_status_line(struct localfile *lf)
{
    int res;
    char *line;

    do {
        res = http_get_line(lf, &line);
        if(res < 0)
            return res;
        
        res = http_process_status_line(lf, line);
        __av_free(line);
    } while(res == 0);

    if(res < 0)
        return res;

    return 0;
}

static int http_wait_response(struct localfile *lf)
{
    int res;

    res = http_check_status_line(lf);
    if(res < 0)
        return res;

    do res = http_check_header_line(lf);
    while(res == 1);

    return res;
}

static const char *http_strip_resource_type(const char *url)
{
    const char *s;

    for(s = url; *s && *s != ':'; s++);
    if(*s)
        s++;
    for(; *s == '/'; s++);
    
    return s;
}

static char *http_url_path(const char *url)
{
    const char *s;

    s = http_strip_resource_type(url);
    s = strchr(s, '/');
    if(s == NULL)
        return __av_strdup("/");
    else
        return __av_strdup(s);
}

static char *http_url_host(const char *url)
{
    const char *s;
    const char *t;
    
    s = http_strip_resource_type(url);
    t = strchr(s, '/');
    if(t == NULL)
        return __av_strdup(s);
    else
        return __av_strndup(s, t - s);
}

static int http_request_get(int sock, struct file *fil)
{
    int res;
    char *req;
    char *url;
    char *host;
    
    if(fil->fs->proxyname != NULL)
        url = __av_strdup(fil->ent->url);
    else
        url = http_url_path(fil->ent->url);

    host = http_url_host(fil->ent->url);

    req = __av_stradd(NULL, 
                      "GET ", url, " HTTP/1.1\r\n",
                      "Host: ", host, "\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      NULL);

    __av_free(url);
    __av_free(host);

    __av_log(AVLOG_DEBUG, "HTTP: %s", req);

    res = write_socket(sock, req, strlen(req));
    __av_free(req);

    return res;
}


static void http_stop(struct localfile *lf)
{
    __av_unref_obj(lf->sockfb);
}

static int http_start(void *data, void **resp)
{
    int res;
    int sock;
    int defaultport;
    char *host;
    struct file *fil = (struct file *) data;
    struct localfile *lf;

    if(fil->fs->proxyname != NULL) {
        host = __av_strdup(fil->fs->proxyname);
        defaultport = 8000;
    }
    else {
        host = http_url_host(fil->ent->url);
        defaultport = 80;
    }

    res = __av_sock_connect(host, defaultport);
    __av_free(host);
    if(res < 0)
        return res;

    sock = res;
    __av_registerfd(sock);

    res = http_request_get(sock, fil);
    if(res < 0) {
        close(sock);
        return res;
    }

    fil->ent->size = -1;

    AV_NEW_OBJ(lf, http_stop);
    lf->sockfb = __av_filebuf_new(sock, 0);
    lf->ent = fil->ent;

    res = http_wait_response(lf);
    if(res < 0) {
        __av_unref_obj(lf);
        return res;
    }

    *resp = lf;
    
    return 0;
}

static avssize_t http_sread(void *data, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct localfile *lf = (struct localfile *) data;

    do {
        res = __av_filebuf_read(lf->sockfb, buf, nbyte);
        if(res != 0)
            return res;
        
        if(__av_filebuf_eof(lf->sockfb))
            return 0;
        
        res = __av_filebuf_check(&lf->sockfb, 1, HTTP_READ_TIMEOUT);
        if(res < 0)
            return res;
        
    } while(res == 1);

    __av_log(AVLOG_ERROR, "HTTP: timeout in body");
    return -EIO;
}

static struct sfile *http_get_serialfile(struct file *fil)
{
    struct sfile *sf;
    struct file *filcpy;
    struct entry *ent = fil->ent;
    static struct sfilefuncs func = {
        http_start,
        http_sread
    };

    sf = (struct sfile *) __av_cacheobj_get(ent->cobj);
    if(sf != NULL)
        return sf;

    AV_NEW_OBJ(filcpy, NULL);
    *filcpy = *fil;

    sf = __av_sfile_new(&func, filcpy, 0);

    __av_unref_obj(ent->cobj);
    ent->cobj = __av_cacheobj_new(sf, ent->url);

    return sf;
}

static struct entry *http_get_entry(struct filesys *fs, const char *url)
{
    struct entry **ep;
    struct entry *ent;

    for(ep = &fs->ents; *ep != NULL; ep = &(*ep)->next) {
        ent = *ep;
        if(strcmp(ent->url, url) == 0)
            return ent;
    }

    AV_NEW(ent);
    ent->url = __av_strdup(url);
    ent->cobj = NULL;
    ent->next = NULL;
    
    *ep = ent;

    return ent;
}

static int begins_with(const char *str, const char *beg)
{
    if(strncmp(str, beg, strlen(beg)) == 0)
        return 1;
    else
        return 0;
}

static char *http_ventry_url(ventry *ve)
{
    char *url = __av_strdup((char *) ve->data);
    char *s;

    for(s = url; *s; s++) {
        if(*s == '|')
            *s = '/';
    }
    
    if(!begins_with(url, "http://") && !begins_with(url, "ftp://")) {
        char *newurl;

        newurl = __av_stradd(NULL, "http://", url, NULL);
        __av_free(url);
        url = newurl;
    }

    return url;
}

static int http_open(ventry *ve, int flags, avmode_t mode, void **resp)
{
    int res;
    char *url;
    struct filesys *fs = (struct filesys *) ve->mnt->avfs->data;
    struct file *fil;
    struct sfile *sf;

    url = http_ventry_url(ve);
    if(url == NULL)
        return -ENOENT;

    AV_NEW(fil);
    fil->ent = http_get_entry(fs, url);
    fil->fs = fs;
    __av_free(url);

    sf = http_get_serialfile(fil);
    res = __av_sfile_startget(sf);
    __av_unref_obj(sf);

    if(res == 0) 
        *resp = (void *) fil;
    else 
        __av_free(fil);

    return res;
}

static int http_close(vfile *vf)
{
    struct file *fil = (struct file *) vf->data;

    __av_free(fil);

    return 0;
}


static avssize_t http_read(vfile *vf, char *buf, avsize_t nbyte)
{
    avssize_t res;
    struct file *fil = (struct file *) vf->data;
    struct sfile *sf;

    sf = http_get_serialfile(fil);
    res = __av_sfile_pread(sf, buf, nbyte, vf->ptr);
    __av_unref_obj(sf);

    if(res > 0)
        vf->ptr += res;

    return res;
}

static int http_getattr(vfile *vf, struct avstat *buf, int attrmask)
{
    avoff_t size = -1;
    struct file *fil = (struct file *) vf->data;

    if(attrmask & AVA_SIZE) {
        int res;
        struct sfile *sf;

        sf = http_get_serialfile(fil);
        res = __av_sfile_startget(sf);
        if(res < 0)
            return res;

        size = fil->ent->size;
        if(size == -1)
            size = __av_sfile_size(sf);
        __av_unref_obj(sf);
    }

    buf->dev = 1;
    buf->ino = 1;
    buf->mode = AV_IFREG | 0777;
    buf->nlink = 1;
    buf->uid = 0;
    buf->gid = 0;
    buf->size = size;
    buf->blksize = 512;
    buf->blocks = AV_BLOCKS(size);
    buf->atime.sec = 0;
    buf->atime.nsec = 0;
    buf->mtime = buf->atime;
    buf->ctime = buf->atime;

    return 0;
}

static int http_access(ventry *ve, int amode)
{
    if((amode & AVW_OK) != 0)
        return -EACCES;
    
    return 0;
}

static void http_destroy(struct avfs *avfs)
{
    struct entry *ent;
    struct entry *nextent;
    struct filesys *fs = (struct filesys *) avfs->data;

    ent = fs->ents;
    while(ent != NULL) {
        nextent = ent->next;
        __av_free(ent->url);
        __av_unref_obj(ent->cobj);
        __av_free(ent);
        ent = nextent;
    }

    __av_free(fs->proxyname);
    __av_free(fs);
}

static void http_get_proxy(struct filesys *fs)
{
    const char *proxyenv;

    proxyenv = getenv("http_proxy");
    if(proxyenv == NULL)
        return;

    if(begins_with(proxyenv, "http://"))
        proxyenv = http_strip_resource_type(proxyenv);
    
    fs->proxyname = __av_strdup(proxyenv);

    __av_log(AVLOG_DEBUG, "HTTP: proxy = %s", fs->proxyname);
}

extern int __av_init_module_http(struct vmodule *module);

int __av_init_module_http(struct vmodule *module)
{
    int res;
    struct avfs *avfs;
    struct filesys *fs;

    res = __av_new_avfs("http", NULL, AV_VER, AVF_ONLYROOT, module, &avfs);
    if(res < 0)
        return res;

    AV_NEW(fs);
    fs->ents = NULL;
    fs->proxyname = NULL;
    
    http_get_proxy(fs);

    avfs->data = (void *) fs;

    avfs->destroy   = http_destroy;
    avfs->open      = http_open;
    avfs->close     = http_close;
    avfs->getattr   = http_getattr;
    avfs->read      = http_read;
    avfs->access    = http_access;

    __av_add_avfs(avfs);
    
    return 0;
}
