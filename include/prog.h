/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

struct proginfo {
    const char **prog;

    int ifd;
    int ofd;
    int efd;
  
    int pid;

    const char *wd;
};

void       __av_init_proginfo(struct proginfo *pi);
int        __av_start_prog(struct proginfo *pi);
int        __av_wait_prog(struct proginfo *pi, int tokill, int check);
                    
