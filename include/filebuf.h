/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998-2001  Miklos Szeredi (mszeredi@inf.bme.hu)

    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 
*/

#include "avfs.h"

#define FILEBUF_NONBLOCK (1 << 0)
#define FILEBUF_WRITE    (1 << 1)

struct filebuf;

struct filebuf *__av_filebuf_new(int fd, int flags);
int __av_filebuf_eof(struct filebuf *fb);
int __av_filebuf_check(struct filebuf *fbs[], unsigned int numfbs,
                       long timeoutms);

int __av_filebuf_readline(struct filebuf *fb, char **linep);
int __av_filebuf_getline(struct filebuf *fb, char **linep, long timeoutms);
avssize_t __av_filebuf_read(struct filebuf *fb, char *buf, avsize_t nbytes);
avssize_t __av_filebuf_write(struct filebuf *fb, const char *buf,
                             avsize_t nbytes);


/* __av_filebuf_getline() will return:
   1 and *linep != NULL  -- success
   1 and *linep == NULL  -- eof
   0                     -- timeout
   < 0                   -- read error
*/
