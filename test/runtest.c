/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "virtual.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define TESTDIR "/tmp/avfstest"

typedef int (*testfunc) (void *);

struct test {
    char *name;
    struct test *next;

    struct test *sub;
    void *data;
    testfunc func;
};

struct filetest {
    const char *filename;
    int fd;
};

static struct test *test_new(struct test *tg, const char *name)
{
    struct test *ng;
    struct test **tgp;

    for(tgp = &tg->sub; *tgp != NULL; tgp = &(*tgp)->next);

    ng = malloc(sizeof(*ng));
    ng->sub = NULL;
    ng->next = NULL;
    ng->name = strdup(name);
    ng->data = NULL;
    ng->func = NULL;
    
    *tgp = ng;

    return ng;
}

static void test_add(struct test *tg, const char *name, void *data,
                     testfunc func)
{
    struct test *tc;

    tc = test_new(tg, name);
    tc->data = data;
    tc->func = func;
}

static int test_run_path(struct test *tg, const char *path)
{
    int ok;
    int res;
    char *newpath;

    if(tg == NULL)
        return 0;

    newpath = malloc(strlen(path) + 1 + strlen(tg->name) + 1);

    sprintf(newpath, "%s.%s", path, tg->name);
    ok = test_run_path(tg->sub, newpath);
    if(tg->func != NULL) {
        printf("%s:\t", newpath);
        res = tg->func(tg->data);
        printf("%s\n", res ? "OK" : "FAILED");
        if(!res)
            ok = 0;
    }

    free(newpath);

    res = test_run_path(tg->next, path);
    if(!res)
        ok = 0;

    return ok;
}

static int test_run(struct test *tg)
{
    return test_run_path(tg, "");
}

static int test_rmr(const char *file)
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

            newname = malloc(strlen(file) + 1 + strlen(name) + 1);
            sprintf(newname, "%s/%s", file, name);
            test_rmr(newname);
            free(newname);
        }
    }
    closedir(dirp);

    return rmdir(file);
}

static void test_init()
{
    test_rmr(TESTDIR);
    mkdir(TESTDIR, 0777);
}

static char *test_file(const char *name)
{
    char *fullname = malloc(strlen(TESTDIR) + 1 + strlen(name) + 1);

    sprintf(fullname, "%s/%s", TESTDIR, name);

    return fullname;
}

static int file_create(struct filetest *ft)
{
    int res;

    res = virt_open(ft->filename, O_WRONLY | O_CREAT | O_EXCL, 0777);
    if(res == -1)
        return 0;

    ft->fd = res;

    return 1;
}

static int file_write(struct filetest *ft)
{
    int res;

    res = virt_write(ft->fd, "testtext\n", 9);
    if(res != 9)
        return 0;
    
    return 1;
}

static int file_close(struct filetest *ft)
{
    int res;

    res = virt_close(ft->fd);
    if(res == -1)
        return 0;

    ft->fd = -1;
    
    return 1;
}

static void add_filetest(struct test *tg, const char *filename)
{
    struct filetest ft;
    struct test *ftg;

    ftg = test_new(tg, filename);

    ft.filename = test_file(filename);

    test_add(ftg, "create", &ft, (testfunc) file_create);
    test_add(ftg, "write", &ft, (testfunc) file_write);
    test_add(ftg, "close", &ft, (testfunc) file_close);
}


int main(int argc, char *argv[])
{
    int res;
    struct test root;
    struct test *tg;

    test_init();

    root.sub = NULL;
    tg = test_new(&root, "filetest");
    add_filetest(tg, "t.gz#");
    
    res = test_run(tg);

    if(res == 0)
        return 1;
    else
        return 0;
}
