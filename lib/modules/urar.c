/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    RAR module
    Copyright (C) 1998 David Hanak (dhanak@inf.bme.hu)
*/
#if 0
#include "archives.h"

#define DOS_DIR_SEP_CHAR  '\\'

static avbyte good_marker_head[] = 
{ 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00 };

#define LONG_HEAD_SIZE      11
#define SHORT_HEAD_SIZE      7
#define FILE_HEAD_SIZE      21
#define MARKER_HEAD_SIZE (sizeof(good_marker_head)/sizeof(avbyte))

typedef avbyte block_header[LONG_HEAD_SIZE];
typedef avbyte file_header[FILE_HEAD_SIZE];

#define BI(ptr, i)  ((avbyte) (ptr)[i])
#define BYTE(ptr)  (BI(ptr,0))
#define DBYTE(ptr) ((avdbyte)(BI(ptr,0) | (BI(ptr,1)<<8)))
#define QBYTE(ptr) ((avqbyte)(BI(ptr,0) | (BI(ptr,1)<<8) | \
                   (BI(ptr,2)<<16) | (BI(ptr,3)<<24)))

#define bh_CRC(bh)      DBYTE(bh     )
#define bh_type(bh)     BYTE (bh +  2)
#define bh_flags(bh)    DBYTE(bh +  3)
#define bh_headsize(bh) DBYTE(bh +  5)
#define bh_addsize(bh)  QBYTE(bh +  7)
#define bh_size(bh)     (bh_headsize(bh) + bh_addsize(bh))

#define fh_origsize(fh) QBYTE(fh     )
#define fh_hostos(fh)   BYTE (fh +  4)
#define fh_CRC(fh)      QBYTE(fh +  5)
#define fh_time(fh)     QBYTE(fh +  9)
#define fh_version(fh)  BYTE (fh + 13)
#define fh_method(fh)   BYTE (fh + 14)
#define fh_namelen(fh)  DBYTE(fh + 15)
#define fh_attr(fh)     QBYTE(fh + 17)

#define dos_ftsec(ft)   (int)( 2 * ((ft >>  0) & 0x1F))
#define dos_ftmin(ft)   (int)(     ((ft >>  5) & 0x3F))
#define dos_fthour(ft)  (int)(     ((ft >> 11) & 0x1F))
#define dos_ftday(ft)   (int)(     ((ft >> 16) & 0x1F))
#define dos_ftmonth(ft) (int)(-1 + ((ft >> 21) & 0x0F))
#define dos_ftyear(ft)  (int)(80 + ((ft >> 25) & 0x7F))

/* Block types */
#define B_MARKER             0x72
#define B_MAIN               0x73
#define B_FILE               0x74
#define B_COMMENT            0x75
#define B_EXTRA_INFO         0x76
#define B_SUB                0x77
#define B_RECOVERY           0x78

/* Block flags */
#define FB_OUTDATED        0x4000
#define FB_WITH_BODY       0x8000

/* Archive flags */
#define FA_IS_VOLUME         0x01
#define FA_WITH_COMMENT      0x02
#define FA_IS_SOLID          0x04
#define FA_WITH_AUTHENTICITY 0x20

/* File block flags */
#define FF_CONT_FROM_PREV    0x01
#define FF_CONT_IN_NEXT      0x02
#define FF_WITH_PASSWORD     0x04
#define FF_WITH_COMMENT      0x08
#define FF_IS_SOLID          0x10

/* Compression methods */
#define M_STORE              0x30
#define M_FASTEST            0x31
#define M_FAST               0x32
#define M_NORMAL             0x33
#define M_GOOD               0x34
#define M_BEST               0x35

/* Archiving OS */
#define OS_MSDOS                0
#define OS_OS2                  1
#define OS_WIN32                2
#define OS_UNIX                 3

#define CRC_START     0xFFFFFFFFUL
#define CRC_INIT      0xEDB88320UL

#define CRC_TABLESIZE 256
static avqbyte CRC_table[CRC_TABLESIZE];

typedef struct rar_inodat {
    avdbyte flags;
    avbyte hostos;
    avbyte packer_version;
    avbyte method;
    char *orig_path;
} rar_inodat;

static void initCRC(void)
{
    int i, j;
    avqbyte c;
  
    for (i = 0; i < CRC_TABLESIZE; i++)
    {
        for (c = i, j = 0; j < 8; j++)
            c = (c & 1) ? (c >> 1) ^ CRC_INIT : (c >> 1);
        CRC_table[i] = c;
    }
}

static avqbyte CRC_byte(avqbyte crc, avbyte byte)
{
    return CRC_table[(avbyte)crc ^ byte] ^ (crc >> 8);
}

static avqbyte CRC_string(avqbyte crc, avbyte *buf, long size)
{
    long i;

    for (i = 0; i < size; i++)
        crc = CRC_byte(crc, buf[i]);
    return crc;
}

static avssize_t read_file(ave *v, arch_file *file, char *buf, avsize_t nbyte)
{
    /* FIXME: error checking */
    avssize_t res = __av_read(v, file->fh, buf, nbyte);
    file->ptr += res;

    return res;
}
static avoff_t seek_file(ave *v, arch_file *file, avoff_t offset, int whence)
{
    avoff_t res = __av_lseek(v, file->fh, offset, whence);
    file->ptr = res;

    return res;
}

static int read_block_header(ave *v, arch_file *file, block_header bh)
{
    int size = SHORT_HEAD_SIZE;
    int i;

    for(i = SHORT_HEAD_SIZE; i < LONG_HEAD_SIZE; i++) bh[i] = 0;
  
    if (read_file(v, file, bh, SHORT_HEAD_SIZE) != SHORT_HEAD_SIZE)
        return -1;

    if ((bh_flags(bh) & FB_WITH_BODY) != 0) {
        if (read_file(v, file, bh+SHORT_HEAD_SIZE, 4) != 4)
            return -1;
        size += 4;
    }

    return size;
}

static int read_marker_block(ave *v, arch_file *file)
{
    avbyte buf[MARKER_HEAD_SIZE], *pos = buf;
    int readsize = MARKER_HEAD_SIZE;

    /* An SFX module starts with the extraction header. Skip that part by
       searching for the marker head. */
    while(1) {
        if (read_file(v, file, buf + MARKER_HEAD_SIZE - readsize,
                      readsize) != readsize) return -1;

        if (__av_memcmp(buf, good_marker_head, MARKER_HEAD_SIZE) == 0) return 0;

        pos = __av_memchr(buf + 1, good_marker_head[0], MARKER_HEAD_SIZE-1);
        if (pos == AVNULL) readsize = MARKER_HEAD_SIZE;
        else {
            readsize = pos - buf;
            __av_memmove(buf, pos, MARKER_HEAD_SIZE - readsize);
        }
    }
    return 0; /* Just to avoid warnings. Never reaches this line. */
}

static int read_archive_header(ave *v, arch_file *file, archive *arch)
{
    block_header main_head;
    avqbyte crc;
    avbyte tmpbuf[6];
    avdbyte *flags;
    int headlen;

    if ((headlen = read_block_header(v, file, main_head)) == -1)
        return -1;

    if (bh_type(main_head) != B_MAIN) {
        v->errn = EIO;
        return -1;
    }

    crc = CRC_string(CRC_START, main_head + 2, headlen - 2);

    /* Read reserved bytes. */
    if (read_file(v, file, tmpbuf, 6) != 6)
        return -1;
    crc = CRC_string(crc, tmpbuf, 6);

    if ((avdbyte)~crc != bh_CRC(main_head)) {
        v->errn = EIO;
        return -1;
    }

    if (seek_file(v, file, bh_size(main_head) - headlen - 6,
                  AVSEEK_CUR) == -1)
        return -1;

    AV_NEW(v, flags);
    if (flags == AVNULL) return -1;
    *flags = bh_flags(main_head);
    arch->udata = flags;

    return 0;
}

static void conv_tolower(char *s)
{
    for(; *s; s++) *s = __av_tolower(*s);
}


static void dos2unix_path(char *path)
{
    char *pos = path;

    while((pos = __av_strchr(pos, DOS_DIR_SEP_CHAR)) != AVNULL)
        *pos = DIR_SEP_CHAR;
}

static avtime_t dos2unix_time(avqbyte dt)
{
    struct avtm ut;

    ut.sec = dos_ftsec(dt);
    ut.min = dos_ftmin(dt);
    ut.hour = dos_fthour(dt);
    ut.day = dos_ftday(dt);
    ut.mon = dos_ftmonth(dt);
    ut.year = dos_ftyear(dt);

    return __av_mktime(&ut);
}

static avmode_t dos2unix_attr(avqbyte da, avmode_t archmode)
{
    avmode_t mode = (archmode & 0666);
    if (da & 0x01) mode = mode & ~0222;
    if (da & 0x10) mode = mode | ((mode & 0444) >> 2) | AV_IFDIR;
    else mode |= AV_IFREG;

    return mode;
}

static int fill_rarentry(ave *v, arch_entry *ent, char *path, avdbyte flags,
                         file_header fh, arch_file *file)
{
    int res;
    archive *arch = ent->arch;
    char *lnkname = AVNULL;
    rar_inodat *info = AVNULL;
    struct avstat filestat;

    if(ent->ino != AVNULL)
        return 0; /* FIXME: what should we do in case of duplicate files? */

    info = __av_malloc(v, sizeof(*info) + __av_strlen(path) + 1);
    if(info == AVNULL)
        return -1;

    __av_default_stat(&filestat);

    filestat.uid = arch->uid;
    filestat.gid = arch->gid;
    if (flags & FF_WITH_PASSWORD)
        filestat.mode = AV_IFREG; /* FIXME */
    else {
        if (fh_hostos(fh) == OS_UNIX)
            filestat.mode = fh_attr(fh);
        else 
            filestat.mode = dos2unix_attr(fh_attr(fh), arch->mode);
    }

    filestat.blocks = AV_DIV(fh_origsize(fh), VBLOCKSIZE);
    filestat.blksize = VBLOCKSIZE;
    filestat.dev = arch->dev;
    filestat.ino = arch->inoctr++;
    filestat.mtime = dos2unix_time(fh_time(fh));
    filestat.atime = filestat.mtime;
    filestat.ctime = filestat.mtime;
    filestat.size = fh_origsize(fh);

    info->flags = flags;
    info->hostos = fh_hostos(fh);
    info->packer_version = fh_version(fh);
    info->method = fh_method(fh);
    info->orig_path = ((char *) info) + sizeof(*info);
    __av_strcpy(info->orig_path, path);

    if (AV_ISLNK(filestat.mode)) {
        lnkname = (char *)__av_malloc(v, filestat.size+1);
        if (lnkname == AVNULL || 
            read_file(v, file, lnkname, filestat.size) != filestat.size)
            goto error;

        lnkname[filestat.size] = '\0';
    }
    else
        lnkname = AVNULL;

    res = __av_new_inode(v, ent, &filestat);
    if(res == -1)
        goto error;

    ent->ino->offset = file->ptr;
    ent->ino->realsize = fh_origsize(fh); /* FIXME: Not the origsize!!! */
    ent->ino->syml = lnkname;
    ent->ino->udata = info;

    return 0;

  error:
    __av_free(info);
    __av_free(lnkname);

    return -1;

}

static int insert_rarentry(ave *v, archive *arch, char *path, avdbyte flags,
                           file_header fh, arch_file *file)
{
    int res;
    arch_entry *ent;
    int entflags = 0;

    if (path[0] == '\0') {
        v->errn = EIO;
        return -1; /* FIXME: warning: empty name */
    }

    dos2unix_path(path);

    if(fh_hostos(fh) == OS_MSDOS) {
        conv_tolower(path);
        entflags |= ENTF_NOCASE;
    }

    ent = __av_find_entry(v, arch->root, path, FIND_CREATE, entflags);
    if(ent == AVNULL)
        return -1;

    if(ent->ino == AVNULL)
        ent->flags = entflags;
    
    res = fill_rarentry(v, ent, path, flags, fh, file);
    __av_unref_entry(ent);

    return res;
}

static int read_rarfile(ave *v, arch_file *file, archive *arch)
{
    block_header bh, ch;
    file_header fh;
    avqbyte crc;
    char *name;
    avoff_t headlen;
    int res;

#define CLEANUP_EXIT() ({ return -1; })

    if (read_marker_block(v, file) != 0 ||
        read_archive_header(v, file, arch) != 0)
        CLEANUP_EXIT();

    headlen = file->ptr;
    while ((res = read_block_header(DUMMYV, file, bh)) != -1) {
        if (bh_type(bh) != B_FILE)
            seek_file(DUMMYV, file, bh_size(bh) - res, AVSEEK_CUR);
        else {
            if (bh_size(bh) < LONG_HEAD_SIZE + FILE_HEAD_SIZE ||
                read_file(v, file, fh, FILE_HEAD_SIZE) != FILE_HEAD_SIZE)
                CLEANUP_EXIT();

            name = __av_malloc(v, fh_namelen(fh)+1);
            if (!name) CLEANUP_EXIT();

#undef CLEANUP_EXIT
#define CLEANUP_EXIT() ({ __av_free(name);                  \
			        return -1;                        \
			     })

            if (read_file(v, file, name, fh_namelen(fh)) != fh_namelen(fh))
                CLEANUP_EXIT();

            name[fh_namelen(fh)] = '\0';

            crc = CRC_string(CRC_START, bh + 2, LONG_HEAD_SIZE - 2);
            crc = CRC_string(crc, fh, FILE_HEAD_SIZE);
            crc = CRC_string(crc, name, fh_namelen(fh));

            if ((avdbyte)~crc != bh_CRC(bh)) {
                v->errn = EIO;
                CLEANUP_EXIT();
            }

            if ((bh_flags(bh) & FF_WITH_COMMENT) != 0) {
                if ((res = read_block_header(v, file, ch)) == -1) CLEANUP_EXIT();
                seek_file(DUMMYV, file, bh_size(ch) - res, AVSEEK_CUR);
            }

            res = insert_rarentry(v, arch, name, bh_flags(bh), fh, file);
            if (res == -1) CLEANUP_EXIT();
            __av_free(name);

            headlen = file->ptr - headlen;

            seek_file(DUMMYV, file, bh_size(bh) - headlen, AVSEEK_CUR);
        }
        headlen = file->ptr;
    }

    return 0;
}
#undef CLEANUP_EXIT

static int parse_rarfile(ave *v, vpath *path, archive *arch)
{
    int res;
    arch_file file;

    if(PARAM(path)[0]) {
        v->errn = ENOENT;
        return -1;
    }
  
    file.fh = __av_open(v, BASE(path), AVO_RDONLY, 0);
    if(file.fh == -1) return -1;
    file.ptr = 0;

    res = read_rarfile(v, &file, arch);
  
    __av_close(DUMMYV, file.fh);
    
    return res;  
}

/* FIXME: Because we use the 'rar' program to extract the contents of
   each file individually , we get _VERY_ poor performance */

static int do_unrar(ave *v, vpath *path, arch_fdi *di)
{
    struct proginfo pri;
    rar_inodat *info = (rar_inodat *) di->ino->udata;
    const char *prog[10];
    real_file *basefile;
    int res;

    if(di->file.fh != -1) __av_close(DUMMYV, di->file.fh);
    di->offset = 0;
    di->size = di->ino->st.size;

    di->file.fh = __av_get_tmpfd(v);
    if(di->file.fh == -1) return -1;

    basefile = __av_get_realfile(v, BASE(path));
    if(basefile == AVNULL) return -1;
  
    prog[0] = "rar";
    prog[1] = "p";
    prog[2] = "-c-";
    prog[3] = "-ierr";
    prog[4] = basefile->name;
    prog[5] = info->orig_path;
    prog[6] = AVNULL;
  
    __av_init_proginfo(&pri);
    pri.prog = prog;
    pri.ifd = __av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri.ofd = di->file.fh;
    pri.efd = __av_get_logfile(v);

    res = __av_start_prog(v, &pri);
 
    __av_localclose(DUMMYV, pri.ifd);
    __av_localclose(DUMMYV, pri.efd);

    if(res == -1) 
        __av_log(AVLOG_WARNING, "URAR: Could not start %s", prog[0]);
    else {
        res = __av_wait_prog(v, &pri, 0, 0);
        if(res == -1) 
            __av_log(AVLOG_WARNING, "URAR: Prog %s returned with an error", prog[0]);
    }

    __av_free_realfile(basefile);
    __av_lseek(v, di->file.fh, 0, AVSEEK_SET);
    di->file.ptr = 0;

    return res;
}

static void *rar_open(ave *v,  vpath *path, int flags, int mode)
{
    arch_fdi *di;
    arch_devd *dd = (arch_devd *) __av_get_vdev(path)->devdata;
    rar_inodat *info;

    di = (arch_fdi *) (*dd->vdev->open)(v, path, flags, mode);
    if(di == AVNULL) return AVNULL;

    if(AV_ISDIR(di->ino->st.mode))
        return di;

    info = (rar_inodat *) di->ino->udata;
    if(info->flags & FF_WITH_PASSWORD) {
        __av_log(AVLOG_WARNING, "Sorry, can't open password protected RAR-file");
        v->errn = EACCES;
        goto error;
    }

    if(info->method != M_STORE) {
        int res;

        res = do_unrar(v, path, di);
        if(res == -1) goto error;
    }
  
    return (void *) di;

  error:
    (*dd->vdev->close)(v, (void *) di);
    return AVNULL;
}

/* FIXME: no CRC checking is done while reading files. How to do it? */

extern int __av_init_module_urar(ave *v);

int __av_init_module_urar(ave *v)
{
    struct ext_info rarexts[3];
    struct vdev_info *vdev;
    arch_devd *dd;

    INIT_EXT(rarexts[0], ".rar", AVNULL);
    INIT_EXT(rarexts[1], ".sfx", AVNULL);
    INIT_EXT(rarexts[2], AVNULL, AVNULL);

    vdev = __av_init_arch(v, "urar", rarexts, AV_VER);
    if(vdev == AVNULL) return -1;
  
    dd = (arch_devd *) vdev->devdata;
    dd->parsefunc = parse_rarfile;
    vdev->open = rar_open;

    /* Getfile is now OK */

    initCRC();

    return __av_add_vdev(v, vdev);
}
#endif
