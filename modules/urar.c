/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    RAR module
    Copyright (C) 1998 David Hanak (dhanak@inf.bme.hu)
*/

#include "archive.h"
#include "oper.h"
#include "version.h"

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
#define DBYTE(ptr) ((avushort)(BI(ptr,0) | (BI(ptr,1)<<8)))
#define QBYTE(ptr) ((avuint)(BI(ptr,0) | (BI(ptr,1)<<8) | \
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
static avuint CRC_table[CRC_TABLESIZE];

struct rarnode {
    avushort flags;
    avbyte hostos;
    avbyte packer_version;
    avbyte method;
    char *path;
};

struct rar_entinfo {
    char *name;
    char *linkname;
    avoff_t datastart;
    block_header bh;
    file_header fh;
};

static void initCRC(void)
{
    int i, j;
    avuint c;
  
    for (i = 0; i < CRC_TABLESIZE; i++)
    {
        for (c = i, j = 0; j < 8; j++)
            c = (c & 1) ? (c >> 1) ^ CRC_INIT : (c >> 1);
        CRC_table[i] = c;
    }
}

static avuint CRC_byte(avuint crc, avbyte byte)
{
    return CRC_table[(avbyte)crc ^ byte] ^ (crc >> 8);
}

static avuint CRC_string(avuint crc, avbyte *buf, long size)
{
    long i;

    for (i = 0; i < size; i++)
        crc = CRC_byte(crc, buf[i]);
    return crc;
}

static int read_block_header(vfile *vf, block_header bh, int all)
{
    int res;
    int size = SHORT_HEAD_SIZE;
    int i;

    for(i = SHORT_HEAD_SIZE; i < LONG_HEAD_SIZE; i++) bh[i] = 0;

    if(all)
        res = av_read_all(vf, bh, SHORT_HEAD_SIZE);
    else
        res = av_read(vf, bh, SHORT_HEAD_SIZE);
    if(res < 0)
        return res;
    if(res < SHORT_HEAD_SIZE)
        return 0;

    if ((bh_flags(bh) & FB_WITH_BODY) != 0) {
        res = av_read_all(vf, bh+SHORT_HEAD_SIZE, 4);
        if(res < 0)
            return res;

        size += 4;
    }

    return size;
}

static int read_marker_block(vfile *vf)
{
    int res;
    avbyte buf[MARKER_HEAD_SIZE], *pos = buf;
    int readsize = MARKER_HEAD_SIZE;

    /* An SFX module starts with the extraction header. Skip that part by
       searching for the marker head. */
    while(1) {
        res = av_read_all(vf, buf + MARKER_HEAD_SIZE - readsize, readsize);
        if(res < 0)
            return res;

        if (memcmp(buf, good_marker_head, MARKER_HEAD_SIZE) == 0) return 0;

        pos = memchr(buf + 1, good_marker_head[0], MARKER_HEAD_SIZE-1);
        if (pos == NULL) readsize = MARKER_HEAD_SIZE;
        else {
            readsize = pos - buf;
            memmove(buf, pos, MARKER_HEAD_SIZE - readsize);
        }
    }
    return 0; /* Just to avoid warnings. Never reaches this line. */
}

static int read_archive_header(vfile *vf)
{
    int res;
    block_header main_head;
    avuint crc;
    avbyte tmpbuf[6];
    int headlen;

    headlen = read_block_header(vf, main_head, 1);
    if(headlen < 0)
        return headlen;

    if (bh_type(main_head) != B_MAIN) {
        av_log(AVLOG_ERROR, "URAR: Bad archive header");
        return -EIO;
    }

    crc = CRC_string(CRC_START, main_head + 2, headlen - 2);

    /* Read reserved bytes. */
    res = av_read_all(vf, tmpbuf, 6);
    if(res < 0)
        return res;
    crc = CRC_string(crc, tmpbuf, 6);

    if ((avushort)~crc != bh_CRC(main_head)) {
        av_log(AVLOG_ERROR, "URAR: Bad archive header CRC");
        return -EIO;
    }

    av_lseek(vf, bh_size(main_head) - headlen - 6, AVSEEK_CUR);

    return 0;
}

static void conv_tolower(char *s)
{
    for(; *s; s++) *s = tolower((int) *s);
}


static void dos2unix_path(char *path)
{
    char *pos = path;

    while((pos = strchr(pos, DOS_DIR_SEP_CHAR)) != NULL)
        *pos = '/';
}

static avtime_t dos2unix_time(avuint dt)
{
    struct avtm ut;

    ut.sec = dos_ftsec(dt);
    ut.min = dos_ftmin(dt);
    ut.hour = dos_fthour(dt);
    ut.day = dos_ftday(dt);
    ut.mon = dos_ftmonth(dt);
    ut.year = dos_ftyear(dt);

    return av_mktime(&ut);
}

static avmode_t dos2unix_attr(avuint da, avmode_t archmode)
{
    avmode_t mode = (archmode & 0666);
    if (da & 0x01) mode = mode & ~0222;
    if (da & 0x10) mode = mode | ((mode & 0444) >> 2) | AV_IFDIR;
    else mode |= AV_IFREG;

    return mode;
}

static void rarnode_delete(struct rarnode *info)
{
    av_free(info->path);
}

static avmode_t rar_get_mode(struct rar_entinfo *ei, avmode_t origmode)
{
    if (bh_flags(ei->bh) & FF_WITH_PASSWORD)
        return AV_IFREG; /* FIXME */
    else {
        if (fh_hostos(ei->fh) == OS_UNIX)
            return fh_attr(ei->fh);
        else 
            return dos2unix_attr(fh_attr(ei->fh), origmode);
    }
}

static void fill_rarentry(struct archive *arch, struct entry *ent,
                         struct rar_entinfo *ei)
{
    struct rarnode *info;
    struct archnode *nod;
    int isdir = AV_ISDIR(rar_get_mode(ei, 0));

    nod = av_arch_new_node(arch, ent, isdir);

    nod->st.mode = rar_get_mode(ei, nod->st.mode);
    nod->st.mtime.sec = dos2unix_time(fh_time(ei->fh));
    nod->st.mtime.nsec = 0;
    nod->st.atime = nod->st.mtime;
    nod->st.ctime = nod->st.mtime;
    nod->st.size = fh_origsize(ei->fh);
    nod->st.blocks = AV_BLOCKS(nod->st.size);
    nod->st.blksize = 4096;

    nod->offset = ei->datastart;
    if(fh_method(ei->fh) == M_STORE)
        nod->realsize = fh_origsize(ei->fh);
    else
        nod->realsize = 0;

    nod->linkname = av_strdup(ei->linkname);

    AV_NEW_OBJ(info, rarnode_delete);
    nod->data = info;

    info->flags = bh_flags(ei->bh);
    info->hostos = fh_hostos(ei->fh);
    info->packer_version = fh_version(ei->fh);
    info->method = fh_method(ei->fh);
    info->path = av_strdup(ei->name);
}

static void insert_rarentry(struct archive *arch, struct rar_entinfo *ei)
{
    struct entry *ent;
    int entflags = 0;
    char *path = ei->name;

    dos2unix_path(path);

    if(fh_hostos(ei->fh) == OS_MSDOS) {
        conv_tolower(path);
        entflags |= NSF_NOCASE;
    }

    ent = av_arch_create(arch, path, entflags);
    if(ent == NULL)
        return;

    fill_rarentry(arch, ent, ei);
    av_unref_obj(ent);
}

static int read_rarentry(vfile *vf, struct rar_entinfo *ei)
{
    int res;
    block_header ch;
    avuint crc;

    if (bh_size(ei->bh) < LONG_HEAD_SIZE + FILE_HEAD_SIZE) {
        av_log(AVLOG_ERROR, "URAR: bad header");
        return -EIO;
    }
            
    res = av_read_all(vf, ei->fh, FILE_HEAD_SIZE);
    if(res < 0)
        return res;

    ei->name = av_malloc(fh_namelen(ei->fh)+1);
    res = av_read_all(vf, ei->name, fh_namelen(ei->fh));
    if(res < 0)
        return res;
    ei->name[fh_namelen(ei->fh)] = '\0';
    
    crc = CRC_string(CRC_START, ei->bh + 2, LONG_HEAD_SIZE - 2);
    crc = CRC_string(crc, ei->fh, FILE_HEAD_SIZE);
    crc = CRC_string(crc, ei->name, fh_namelen(ei->fh));
    
    if ((avushort)~crc != bh_CRC(ei->bh)) {
        av_log(AVLOG_ERROR, "URAR: bad CRC");
        return -EIO;
    }
    
    if ((bh_flags(ei->bh) & FF_WITH_COMMENT) != 0) {
        res = read_block_header(vf, ch, 1);
        if(res < 0)
            return res;

        av_lseek(vf, bh_size(ch) - res, AVSEEK_CUR);
    }
    
    if(fh_hostos(ei->fh) == OS_UNIX && AV_ISLNK(fh_attr(ei->fh))) {
        ei->linkname = av_malloc(fh_origsize(ei->fh) + 1);
        res = av_read_all(vf, ei->linkname, fh_origsize(ei->fh));
        if(res < 0)
            return res;

        ei->linkname[fh_origsize(ei->fh)] = '\0';
    }
    
    return 0;
}

static int read_rarfile(vfile *vf, struct archive *arch)
{
    avoff_t headstart;
    int res;

    res = read_marker_block(vf);
    if(res < 0)
        return res;

    res = read_archive_header(vf);
    if(res < 0)
        return res;

    headstart = vf->ptr;
    while(1) {
        struct rar_entinfo ei;
    
        res = read_block_header(vf, ei.bh, 0);
        if(res < 0)
            return res;
        if(res == 0)
            break;

        if (bh_type(ei.bh) == B_FILE) {
            ei.name = NULL;
            ei.linkname = NULL;

            res = read_rarentry(vf, &ei);
            if(res < 0) {
                av_free(ei.name);
                av_free(ei.linkname);
                return res;
            }
            ei.datastart = vf->ptr;

            insert_rarentry(arch, &ei);
            av_free(ei.name);
            av_free(ei.linkname);
        }
        av_lseek(vf, headstart + bh_size(ei.bh), AVSEEK_SET);
        headstart = vf->ptr;
    }

    return 0;
}

static int parse_rarfile(void *data, ventry *ve, struct archive *arch)
{
    int res;
    vfile *vf;

    res = av_open(ve->mnt->base, AVO_RDONLY, 0, &vf);
    if(res < 0)
        return res;

    res = read_rarfile(vf, arch);

    av_close(vf);
    
    return res;  
}

#if 0
/* FIXME: Because we use the 'rar' program to extract the contents of
   each file individually , we get _VERY_ poor performance */

static int do_unrar(ave *v, vpath *path, arch_fdi *di)
{
    struct proginfo pri;
    rar_inodat *info = (rar_inodat *) di->ino->udata;
    const char *prog[10];
    real_file *basefile;
    int res;

    if(di->file.fh != -1) av_close(DUMMYV, di->file.fh);
    di->offset = 0;
    di->size = di->ino->st.size;

    di->file.fh = av_get_tmpfd(v);
    if(di->file.fh == -1) return -1;

    basefile = av_get_realfile(v, BASE(path));
    if(basefile == NULL) return -1;
  
    prog[0] = "rar";
    prog[1] = "p";
    prog[2] = "-c-";
    prog[3] = "-ierr";
    prog[4] = basefile->name;
    prog[5] = info->orig_path;
    prog[6] = NULL;
  
    av_init_proginfo(&pri);
    pri.prog = prog;
    pri.ifd = av_localopen(DUMMYV, "/dev/null", AVO_RDONLY, 0);
    pri.ofd = di->file.fh;
    pri.efd = av_get_logfile(v);

    res = av_start_prog(v, &pri);
 
    av_localclose(DUMMYV, pri.ifd);
    av_localclose(DUMMYV, pri.efd);

    if(res == -1) 
        av_log(AVLOG_WARNING, "URAR: Could not start %s", prog[0]);
    else {
        res = av_wait_prog(v, &pri, 0, 0);
        if(res == -1) 
            av_log(AVLOG_WARNING, "URAR: Prog %s returned with an error", prog[0]);
    }

    av_free_realfile(basefile);
    av_lseek(v, di->file.fh, 0, AVSEEK_SET);
    di->file.ptr = 0;

    return res;
}

static void *rar_open(ave *v,  vpath *path, int flags, int mode)
{
    arch_fdi *di;
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    rar_inodat *info;

    di = (arch_fdi *) (*dd->vdev->open)(v, path, flags, mode);
    if(di == NULL) return NULL;

    if(AV_ISDIR(di->ino->st.mode))
        return di;

    info = (rar_inodat *) di->ino->udata;
    if(info->flags & FF_WITH_PASSWORD) {
        av_log(AVLOG_WARNING, "Sorry, can't open password protected RAR-file");
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
    return NULL;
}

/* FIXME: no CRC checking is done while reading files. How to do it? */

#endif

extern int av_init_module_urar(struct vmodule *module);

int av_init_module_urar(struct vmodule *module)
{
    int res;
    struct avfs *avfs;
    struct ext_info rarexts[3];
    struct archparams *ap;

    rarexts[0].from = ".rar",  rarexts[0].to = NULL;
    rarexts[1].from = ".sfx",  rarexts[0].to = NULL;
    rarexts[2].from = NULL;

    res = av_archive_init("urar", rarexts, AV_VER, module, &avfs);
    if(res < 0)
        return res;
    
    ap = (struct archparams *) avfs->data;
    ap->parse = parse_rarfile;

    initCRC();

    av_add_avfs(avfs);

    return 0;
}
