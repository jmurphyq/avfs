/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    Based on the GNU tar sources (C) Free Software Foundation
    
    This file can be distributed under the GNU GPL. 
    See the file COPYING. 

    TAR module
*/

#include "gtar.h"
#include "archive.h"
#include "oper.h"
#include "ugid.h"
#include "version.h"

#define COPYBUFSIZE 16384
#define BIGBLOCKSIZE (20 * BLOCKSIZE)

/* Some constants from POSIX are given names.  */
#define NAME_FIELD_SIZE   100
#define PREFIX_FIELD_SIZE 155
#define UNAME_FIELD_SIZE   32

/* FIXME: Not any more: inode udata is used for saving filenames temporarily at archive creation */


struct tar_entinfo {
    char *name;
    char *linkname;
    avoff_t size;
    avoff_t datastart;

    union block header;
};

struct sp_array
{
    avoff_t offset;
    int numbytes;
};

struct tarnode {
    int type;
    struct sp_array *sparsearray;
    int sp_array_len;
    avoff_t headeroff;
    avuid_t uid;
    avgid_t gid;
    char uname[UNAME_FIELD_SIZE];
    char gname[UNAME_FIELD_SIZE];
};


#define ISSPACE(x) isspace(x)
#define ISODIGIT(x) ((x) >= '0' && (x) < '8')

/*------------------------------------------------------------------------.
| Quick and dirty octal conversion.  Result is -1 if the field is invalid |
| (all blank, or nonoctal).						  |
`------------------------------------------------------------------------*/

static long from_oct(int digs, char *where)
{
    long value;

    while (ISSPACE ((int) *where))
    {				/* skip spaces */
        where++;
        if (--digs <= 0)
            return -1;		/* all blank field */
    }
    value = 0;
    while (digs > 0 && ISODIGIT (*where))
    {
        /* Scan til nonoctal.  */

        value = (value << 3) | (*where++ - '0');
        --digs;
    }

    if (digs > 0 && *where && !ISSPACE ((int) *where))
        return -1;			/* ended on non-space/nul */

    return value;
}

/*------------------------------------------------------------------------.
| Converts long VALUE into a DIGS-digit field at WHERE, including a       |
| trailing space and room for a NUL.  For example, 3 for DIGS 3 means one |
| digit, a space, and room for a NUL.                                     |
|                                                                         |
| We assume the trailing NUL is already there and don't fill it in.  This |
| fact is used by start_header and finish_header, so don't change it!     |
`------------------------------------------------------------------------*/

#if 0
static void to_oct (long value, int digs, char *where)
{
    --digs;			/* Trailing null slot is left alone */

    do
    {
        where[--digs] = '0' + (char) (value & 7);	/* one octal digit */
        value >>= 3;
    }
    while (digs > 0);
}

#endif

static int find_next_block(vfile *vf, union block *blk)
{
    int res;

    res = av_read(vf, blk->buffer, BLOCKSIZE);
    if(res <= 0)
        return res;
    if(res < BLOCKSIZE) {
        av_log(AVLOG_WARNING, "TAR: Broken archive");
        return -EIO;
    }

    return 1;
}

static int get_next_block(vfile *vf, union block *blk)
{
    int res;
  
    res = find_next_block(vf, blk);
    if(res < 0)
        return res;
    if(res == 0) {
         av_log(AVLOG_WARNING, "TAR: Broken archive");
         return -EIO;
    }
  
    return 0;
}

/* return values: < 0: fatal, 0 eof, 1 bad header, 2 OK */
static int read_entry(vfile *vf, struct tar_entinfo *tinf)
{
    int i;
    long unsigned_sum;		/* the POSIX one :-) */
    long signed_sum;		/* the Sun one :-( */
    long recorded_sum;
    char *p;
    char **longp;
    char *bp;
    union block data_block;
    int size, written;
    int res;
    avoff_t sres;
    char *next_long_name = NULL, *next_long_link = NULL;
    union block *header = &tinf->header;

    while (1)
    {
        res = find_next_block(vf, header);
        if(res <= 0) break; /* HEADER_END_OF_FILE */

        recorded_sum
            = from_oct (sizeof header->header.chksum, header->header.chksum);

        unsigned_sum = 0;
        signed_sum = 0;
        p = header->buffer;
        for (i = sizeof (*header); --i >= 0;)
	{
            /* We can't use unsigned char here because of old compilers,
               e.g. V7.  */

            unsigned_sum += 0xFF & *p;
            signed_sum += *p++;
	}

        /* Adjust checksum to count the "chksum" field as blanks.  */

        for (i = sizeof (header->header.chksum); --i >= 0;)
	{
            unsigned_sum -= 0xFF & header->header.chksum[i];
            signed_sum -= header->header.chksum[i];
	}
        unsigned_sum += ' ' * sizeof header->header.chksum;
        signed_sum += ' ' * sizeof header->header.chksum;

        if (unsigned_sum == sizeof header->header.chksum * ' ')
	{
            /* This is a zeroed block...whole block is 0's except for the
               blanks we faked for the checksum field.  */

            res = 0;
            break; /* HEADER_ZERO_BLOCK */
	}

        if (unsigned_sum != recorded_sum && signed_sum != recorded_sum) {
            res = 1;
            av_log(AVLOG_WARNING, "TAR: Bad header");
            break; /* HEADER_FAILURE */
        }

        /* Good block.  Decode file size and return.  */

        if (header->header.typeflag == LNKTYPE)
            tinf->size  = 0;	/* links 0 size on tape */
        else
            tinf->size = from_oct (1 + 12, header->header.size);

        header->header.name[NAME_FIELD_SIZE - 1] = '\0';
        if (header->header.typeflag == GNUTYPE_LONGNAME
            || header->header.typeflag == GNUTYPE_LONGLINK)
	{
            longp = ((header->header.typeflag == GNUTYPE_LONGNAME)
                     ? &next_long_name
                     : &next_long_link);

            if (*longp) av_free (*longp);
            bp = *longp = (char *) av_malloc ((avsize_t) tinf->size);

            for (size = tinf->size; size > 0; size -= written)
	    {
                res = get_next_block (vf, &data_block);
                if (res < 0) break;
                written = BLOCKSIZE;
                if (written > size)
                    written = size;

                memcpy (bp, data_block.buffer, (avsize_t) written);
                bp += written;
	    }
            if(res < 0) break;

            /* Loop!  */

	}
        else
	{
            tinf->datastart = vf->ptr;

            if (header->oldgnu_header.isextended) {
                do {
                    res = get_next_block (vf, &data_block);
                    if(res < 0) break;
                }
                while(data_block.sparse_header.isextended);
            }
            if(res < 0) break;

            sres = av_lseek(vf, AV_DIV(tinf->size, BLOCKSIZE) * BLOCKSIZE, 
                            AVSEEK_CUR);
            if(sres < 0)
                break;

            tinf->name = av_strdup (next_long_name ? next_long_name
                                      : header->header.name);
            tinf->linkname = av_strdup (next_long_link ? next_long_link
                                          : header->header.linkname);
            res = 2;
            break; /* HEADER_SUCCESS */
	}
    }

    av_free(next_long_name);
    av_free(next_long_link);
    return res;
}


static void decode_header (union block *header, struct avstat *stat_info,
			   enum archive_format *format_pointer, 
			   struct ugidcache *cache)
{
    enum archive_format format;
    char ugname[UNAME_FIELD_SIZE+1];

    if (strcmp (header->header.magic, TMAGIC) == 0)
        format = POSIX_FORMAT;
    else if (strcmp (header->header.magic, OLDGNU_MAGIC) == 0)
        format = OLDGNU_FORMAT;
    else
        format = V7_FORMAT;
    *format_pointer = format;

    stat_info->mode = from_oct (8, header->header.mode);
    stat_info->mode &= 07777;
    stat_info->mtime.sec = from_oct (1 + 12, header->header.mtime);
    stat_info->mtime.nsec = 0;

    if(header->header.typeflag == GNUTYPE_SPARSE) 
        stat_info->size = 
            from_oct (1 + 12, header->oldgnu_header.realsize);
    else stat_info->size = from_oct (1 + 12, header->header.size);
  
    switch(header->header.typeflag) {
    case GNUTYPE_SPARSE:
    case REGTYPE:
    case AREGTYPE:
    case LNKTYPE:
    case CONTTYPE:
        stat_info->mode |= AV_IFREG;
        break;

    case GNUTYPE_DUMPDIR:
    case DIRTYPE:
        stat_info->mode |= AV_IFDIR;
        break;

    case SYMTYPE:
        stat_info->mode |= AV_IFLNK;
        break;
    
    case BLKTYPE:
        stat_info->mode |= AV_IFBLK;
        break;

    case CHRTYPE:
        stat_info->mode |= AV_IFCHR;
        break;

    case FIFOTYPE:
        stat_info->mode |= AV_IFIFO;
        break;
    }

    if (format == V7_FORMAT)
    {
        stat_info->uid = from_oct (8, header->header.uid);
        stat_info->gid = from_oct (8, header->header.gid);
        stat_info->rdev = 0;
    }
    else
    {
        ugname[UNAME_FIELD_SIZE] = '\0';

        strncpy(ugname, header->header.uname, UNAME_FIELD_SIZE);
        stat_info->uid =
            av_finduid(cache, ugname, from_oct (8, header->header.uid));

        strncpy(ugname, header->header.gname, UNAME_FIELD_SIZE);
        stat_info->gid =
            av_findgid(cache, ugname, from_oct (8, header->header.gid));

        switch (header->header.typeflag)
	{
	case BLKTYPE:
	case CHRTYPE:
            stat_info->rdev = 
                av_mkdev (from_oct (8, header->header.devmajor),
                          from_oct (8, header->header.devminor));
            break;

	default:
            stat_info->rdev = 0;
	}
    }
}


static int check_existing(struct entry *ent, struct avstat *tarstat)
{
    struct archnode *nod;
    
    nod = (struct archnode *) av_namespace_get(ent);

    if(AV_ISDIR(nod->st.mode)) {
        if(AV_ISDIR(tarstat->mode)) {
            nod->st.mode = tarstat->mode;
            nod->st.uid = tarstat->uid;
            nod->st.gid = tarstat->gid;
            nod->st.mtime = tarstat->mtime;
#if 0
            /* FIXME */
            nod->origst = nod->st;
#endif
            return 0;
        }
        else {
            av_log(AVLOG_WARNING, "TAR: Overwriting directory with file");
            return 0;
        }
    }
    
    av_arch_del_node(ent);

    return 1;
}

static void fill_link(struct archive *arch, struct entry *ent,
                      const char *linkname)
{
    struct entry *link;
    struct archnode *nod = NULL;

    link = av_arch_resolve(arch, linkname, 0, 0);
    if(link != NULL)
        nod = (struct archnode *) av_namespace_get(link);

    if(nod == NULL || AV_ISDIR(nod->st.mode))
        av_log(AVLOG_WARNING, "utar: Illegal hard link");
    else {
        nod->st.nlink ++;
        av_namespace_set(ent, nod);
        av_ref_obj(ent);
        av_ref_obj(nod);
    }

    av_unref_obj(link);
}

static void tarnode_delete(struct tarnode *tn)
{
    av_free(tn->sparsearray);
}

static void fill_node(struct archive *arch, struct entry *ent,
                      struct tar_entinfo *tinf, struct avstat *tarstat)
{
    struct archnode *nod;
    struct tarnode *tn;
    union block *header = &tinf->header;

    nod = av_arch_new_node(arch, ent, AV_ISDIR(tarstat->mode));

    /* keep dev, ino, nlink */
    nod->st.mode = tarstat->mode;
    nod->st.uid = tarstat->uid;
    nod->st.gid = tarstat->gid;
    nod->st.rdev = tarstat->rdev;
    nod->st.size = tarstat->size;
    nod->st.blksize = BLOCKSIZE;
    nod->st.blocks = AV_BLOCKS(tinf->size);
    nod->st.atime = tarstat->mtime; /* FIXME */
    nod->st.mtime = tarstat->mtime;
    nod->st.ctime = tarstat->mtime;

    nod->offset = tinf->datastart;
    nod->realsize = tinf->size;

    AV_NEW_OBJ(tn, tarnode_delete);
    nod->data = tn;

    tn->sparsearray = NULL;
    tn->headeroff = tinf->datastart - BLOCKSIZE;
    tn->uid = from_oct (8, header->header.uid);
    tn->gid = from_oct (8, header->header.gid);
    strncpy(tn->uname, header->header.uname, UNAME_FIELD_SIZE);
    strncpy(tn->gname, header->header.gname, UNAME_FIELD_SIZE);
    tn->type =  header->header.typeflag;

    if(tn->type == SYMTYPE) {
        nod->linkname = tinf->linkname;
        nod->st.size = strlen(nod->linkname);
        tinf->linkname = NULL;
    }
}

static void fill_tarentry(struct archive *arch, struct entry *ent,
                          struct tar_entinfo *tinf, struct avstat *tarstat)
{
    int res;
    union block *header = &tinf->header;
    struct archnode *nod;

    nod = (struct archnode *) av_namespace_get(ent);
    if(nod != NULL) {
        res = check_existing(ent, tarstat);
        if(res != 1)
            return;
    }

    if(header->header.typeflag == LNKTYPE) 
        fill_link(arch, ent, tinf->linkname);
    else
        fill_node(arch, ent, tinf, tarstat);
}

static void insert_tarentry(struct archive *arch, struct tar_entinfo *tinf,
                            struct avstat *tarstat)
{
    struct entry *ent;

    if(tinf->header.header.typeflag == GNUTYPE_SPARSE) {
#if 0 /* FIXME */
        arch->flags |= ARCHF_RDONLY;
        if(arch->readonly_reason == NULL)
            arch->readonly_reason =
                av_strdup("TAR: Cannot modify archive containing sparsefiles");
#endif
    }

    /* Appears to be a file.  But BSD tar uses the convention that a
       slash suffix means a directory.  */
    if(AV_ISREG(tarstat->mode) && tinf->name[strlen(tinf->name)-1] == '/') 
        tarstat->mode = (tarstat->mode & 07777) | AV_IFDIR;
    
    ent = av_arch_resolve(arch, tinf->name, 1, 0);
    if(ent == NULL)
        return;

    if(av_arch_isroot(arch, ent))
        av_log(AVLOG_WARNING, "TAR: Empty filename");
    else
        fill_tarentry(arch, ent, tinf, tarstat);

    av_unref_obj(ent);
}

static int read_tarfile(vfile *vf, struct archive *arch,
                        struct ugidcache *cache)
{
    struct tar_entinfo tinf;
    enum archive_format format;
    struct avstat tarstat;
    int res;

    while(1) {
        res = read_entry(vf, &tinf);
        if(res < 0)
            return res;
        else if(res == 1) {
#if 0  /* FIXME */
            arch->flags |= ARCHF_RDONLY; /* Broken archive */

            if(arch->readonly_reason == NULL) 
                arch->readonly_reason = 
                    av_strdup("TAR: Cannot modify archive with errors");

#endif
            continue;
        }
        else if(res == 0)
            break;

        av_default_stat(&tarstat);
        decode_header(&tinf.header, &tarstat, &format, cache);

        insert_tarentry(arch, &tinf, &tarstat);
        av_free(tinf.name);
        av_free(tinf.linkname);
    }

    return 0;
}


static int parse_tarfile(void *data, ventry *ve, struct archive *arch)
{
    int res;
    vfile *vf;
    struct ugidcache *cache;

    res = av_open(ve->mnt->base, AVO_RDONLY, 0, &vf);
    if(res < 0)
        return res;

    cache = av_new_ugidcache();
    res = read_tarfile(vf, arch, cache);
    av_unref_obj(cache);

    av_close(vf);
    
    return res;  
}

#if 0
static int write_out(ave *v, int outfd, arch_file *file, avsize_t size)
{
    avssize_t rres, wres, len;
    char buf[COPYBUFSIZE];
    avsize_t at;

    for(at = 0; at < size;) {
        rres = av_read(v, file->fh, buf, AV_MIN(COPYBUFSIZE, size-at));
        if(rres == -1) return -1;
        at += rres;
        file->ptr += rres;

        if(rres != COPYBUFSIZE && at != size) {
            v->errn = EIO;
            return -1;
        }
    
        if(rres < COPYBUFSIZE) {
            len = AV_DIV(rres, BLOCKSIZE) * BLOCKSIZE;
            if(len > rres) av_memset(buf + rres, 0, len - rres);
        }
        else len = COPYBUFSIZE;
    
        wres = av_write(v, outfd, buf, len);
        if(wres == -1) return -1;
    }

    return 0;
}

static void finish_header(union block *blk)
{
    int i, sum;
    char *p;

    av_memset(blk->header.chksum, ' ', 8);

    /* Fill in the checksum field.  It's formatted differently from the
       other fields: it has [6] digits, a null, then a space -- rather than
       digits, a space, then a null.  We use to_oct then write the null in
       over to_oct's space.  The final space is already there, from
       checksumming, and to_oct doesn't modify it.
    */

    sum = 0;
    p = blk->buffer;
    for (i = BLOCKSIZE; --i >= 0; )
        /* We can't use unsigned char here because of old compilers, e.g. V7.  */
        sum += 0xFF & *p++;

    to_oct ((long) sum, 7, blk->header.chksum);
    blk->header.chksum[6] = '\0';	/* zap the space */

}

static int long_name(ave *v, int outfd, const char *name, int type)
{
    union block blk;
    int size;
    int at;

    size = av_strlen(name) + 1;

    av_memset(blk.buffer, 0, BLOCKSIZE);

    av_strcpy(blk.header.name, "././@LongLink");
    to_oct ((long) 0, 8, blk.header.mode);
    to_oct ((long) 0, 8, blk.header.uid);
    to_oct ((long) 0, 8, blk.header.gid);
    to_oct ((long) 0, 12, blk.header.mtime);
    av_strcpy(blk.header.uname, "root");
    av_strcpy(blk.header.gname, "root");
    av_strcpy(blk.header.magic, OLDGNU_MAGIC);
    blk.header.typeflag = type;
    to_oct ((long) size, 12, blk.header.size);

    finish_header(&blk);

    if(av_write(v, outfd, blk.buffer, BLOCKSIZE) == -1) return -1;
  
    for(at = 0; at < size; at += BLOCKSIZE) {
        av_memset(blk.buffer, 0, BLOCKSIZE);
        av_strncpy(blk.buffer, name + at, BLOCKSIZE);

        if(av_write(v, outfd, blk.buffer, BLOCKSIZE) == -1) return -1;    
    }
  
    return 0;
}

static int create_entry(ave *v, arch_entry *ent, const char *path, 
			arch_file *file, int outfd, struct ugidcache *cache)
{
    union block blk;
    arch_inode *ino = ent->ino;
    struct tar_entdat *ted = (struct tar_entdat *) ent->udata;
    char *name;
    int type;
    avsize_t size;
    int res;
    char ugname[AV_TUNMLEN];

    av_memset(blk.buffer, 0, BLOCKSIZE);

    to_oct ((long) ino->st.mode, 8, blk.header.mode);
    to_oct ((long) ino->st.mtime, 12, blk.header.mtime);

    if(!(ino->flags & INOF_CREATED) &&  ted != NULL && 
       ino->st.uid == ino->origst.uid && ino->st.gid == ino->origst.gid) {
    
        to_oct ((long) ted->uid, 8, blk.header.uid);
        to_oct ((long) ted->gid, 8, blk.header.gid);
        av_strncpy(blk.header.uname, ted->uname, UNAME_FIELD_SIZE);
        av_strncpy(blk.header.gname, ted->gname, UNAME_FIELD_SIZE);
    }
    else {
        to_oct ((long) ino->st.uid, 8, blk.header.uid);
        to_oct ((long) ino->st.gid, 8, blk.header.gid);
    
        av_finduname(ugname, ino->st.uid, cache);
        av_strncpy(blk.header.uname, ugname, UNAME_FIELD_SIZE);
    
        av_findgname(ugname, ino->st.gid, cache);
        av_strncpy(blk.header.gname, ugname, UNAME_FIELD_SIZE);
    }

    /* We only do OLDGNU for the moment */
    av_strcpy(blk.header.magic, OLDGNU_MAGIC);

    if(AV_ISDIR(ino->st.mode)) 
        type = DIRTYPE;
    else if(AV_ISLNK(ino->st.mode))
        type = SYMTYPE;
    else if(AV_ISCHR(ino->st.mode)) 
        type = CHRTYPE;
    else if(AV_ISBLK(ino->st.mode))
        type = BLKTYPE;
    else if(AV_ISFIFO(ino->st.mode) || AV_ISSOCK(ino->st.mode))
        type = FIFOTYPE;
    else 
        type = REGTYPE;

    if(ino->udata != NULL)
        type = LNKTYPE;

    blk.header.typeflag = type;

    if(type == REGTYPE) size = ino->st.size;
    else size = 0;

    to_oct ((long) size, 12, blk.header.size);

    if(type == CHRTYPE || type == BLKTYPE) {
        int major, minor;

        av_splitdev(ino->st.rdev, &major, &minor);
        to_oct ((long) major, 8, blk.header.devmajor);
        to_oct ((long) minor, 8, blk.header.devminor);
    }
  
    if(type == LNKTYPE || type == SYMTYPE) {
        char *linkname;

        if(type == LNKTYPE)
            linkname = (char *) ino->udata;
        else
            linkname = ino->syml;


        if(av_strlen(linkname) >= NAME_FIELD_SIZE && 
           long_name(v, outfd, linkname, GNUTYPE_LONGLINK) == -1) return -1;
    
        av_strncpy(blk.header.linkname, linkname, NAME_FIELD_SIZE);
        blk.header.linkname[NAME_FIELD_SIZE-1] = '\0';
    }

    if(!AV_ISDIR(ino->st.mode)) 
        name = av_strconcat(v, path, ent->name, NULL);
    else
        name = av_strconcat(v, path, ent->name, "/", NULL);

    if(name == NULL) return -1;

    if(av_strlen(name) >= NAME_FIELD_SIZE && 
       long_name(v, outfd, name, GNUTYPE_LONGNAME) == -1) return -1;

    av_strncpy(blk.header.name, name, NAME_FIELD_SIZE);
    blk.header.name[NAME_FIELD_SIZE-1] = '\0';
    av_free(name);


    finish_header(&blk);

    if(av_write(v, outfd, blk.buffer, BLOCKSIZE) == -1) return -1;

    /* FIXME: sparse files */
    if(ino->typeflag == GNUTYPE_SPARSE) {
        v->errn = EFAULT;
        return -1;
    }
  
    if(size != 0) {
        if(ino->tmpfile != NULL) {
            arch_file f;
      
            f.ptr = 0;
            f.fh = av_localopen(v, ino->tmpfile, AVO_RDONLY, 0);
            if(f.fh == -1) return -1;

            res = write_out(v, outfd, &f, size);
            av_localclose(DUMMYV, f.fh);

            if(res == -1) return -1;
        }
        else {
            file->ptr = av_lseek(v, file->fh, ino->offset, AVSEEK_SET);
            if(file->ptr == -1) return -1;

            res = write_out(v, outfd, file, size);
            if(res == -1) return -1;
        }
    }

    return 0;
}

static int write_tardir(ave *v, arch_file *file, arch_inode *dir, int outfd, 
			const char *path, int pathchanged, 
			struct ugidcache *cache)
{
    arch_entry *ent;
    arch_inode *ino;
    struct tar_entdat *ted;
    int res;

    for(ent = dir->subdir; ent != NULL; ent = ent->next) {
        ted = (struct tar_entdat *) ent->udata;
        ino = ent->ino;

        if(!(ino->flags & INOF_AUTODIR)) {
            if(create_entry(v, ent, path, file, outfd, cache) == -1) return -1;
      
            if(!AV_ISDIR(ino->st.mode) && ino->st.nlink > 1 && 
               ino->udata == NULL) {
                ino->udata = av_strconcat(v, path, ent->name, NULL);
                if(ino->udata == NULL) return -1;
            }
        }

        if(AV_ISDIR(ino->st.mode)) {
            int dirchanged;
            char *newpath;

            if(ted == NULL) dirchanged = 1; /* Renamed directory */
            else dirchanged = 0;

            newpath = av_strconcat(v, path, ent->name, "/", NULL);
            if(newpath == NULL) return -1;

            res = write_tardir(v, file, ino, outfd, newpath, dirchanged,
                               cache);
            av_free(newpath);
      
            if(res == -1) return -1;
        }
    }

    return 0;
}

static void clear_filenames(arch_inode *dir)
{
    arch_entry *ent;

    for(ent = dir->subdir; ent != NULL; ent = ent->next) {
        av_free(ent->ino->udata);
        ent->ino->udata = NULL;
        if(AV_ISDIR(ent->ino->st.mode)) clear_filenames(ent->ino);
    }
}

static int need_origarch(arch_inode *dir)
{
    arch_entry *ent;

    for(ent = dir->subdir; ent != NULL; ent = ent->next) {
        if(ent->ino->tmpfile == NULL && AV_ISREG(ent->ino->st.mode) &&
           ent->ino->st.size != 0) 
            return 1;

        if(AV_ISDIR(ent->ino->st.mode) && need_origarch(ent->ino)) return 1;
    }
    return 0;
}

static int zero_block(ave *v, int outfd)
{
    union block blk;
  
    av_memset(blk.buffer, 0, BLOCKSIZE);
    if(av_write(v, outfd, blk.buffer, BLOCKSIZE) == -1) return -1;
  
    return 0;
}

static int flush_tarfile(ave *v, vpath *path, archive *arch)
{
    arch_file file;
    rep_file *rf;
    int res;
    struct ugidcache cache;

    av_init_ugidcache(&cache);

    rf = av_get_replacement(v, BASE(path), need_origarch(arch->root->ino));
    if(rf == NULL) return -1;

    file.fh = av_open(v, BASE(path), AVO_RDONLY, 0);
    if(file.fh == -1) {
        av_del_replacement(rf);
        return -1;
    }
    file.ptr = 0;

    res = write_tardir(v, &file, arch->root->ino, rf->outfd, "", 0, &cache);
    clear_filenames(arch->root->ino);

    if(res != -1) {
        avoff_t currsize, esize;

        /* This pads the size to 10 blocks */
        /* FIXME: Do it nicer. Maybe with buffering all the writes */

        currsize = av_lseek(v, rf->outfd, 0, AVSEEK_CUR);
        if(currsize == -1) res = -1;
        else {
            esize = AV_DIV(currsize + BLOCKSIZE, BIGBLOCKSIZE) * BIGBLOCKSIZE;
            while(currsize < esize) {
                res = zero_block(v, rf->outfd);
                if(res == -1) break;
                currsize += BLOCKSIZE;
            }
        }
    }

    av_close(DUMMYV, file.fh);

    if(res == -1) {
        av_log(AVLOG_ERROR, "utar: Flush failed, errno: %i", v->errn);
        av_del_replacement(rf);
        return -1;
    }
  
    res = av_replace_file(v, rf);
    if(res == -1) {
        av_log(AVLOG_ERROR, "utar: Replace file failed, errno: %i", v->errn);
    }
  
    return res;
}

static int copy_file(ave *v, arch_fdi *di)
{
    arch_inode *ino = di->ino;
    avoff_t currpos;
    char buf[COPYBUFSIZE];
    avssize_t rres, wres;
    int fd;

    ino->tmpfile = av_get_tmpfile(v);
    if(ino->tmpfile == NULL) return -1;

    fd = av_localopen(v, ino->tmpfile, AVO_RDWR | AVO_CREAT | AVO_EXCL,
                        0600);
    if(fd == -1) goto error;
  
    currpos = di->ptr;
    di->ptr = 0;
  
    while(di->ptr < di->size) {
        rres = tar_read(v, (void *) di, buf, COPYBUFSIZE);
        if(rres == -1) goto error;
        if(rres == 0) {
            v->errn = EIO;
            goto error;
        }

        wres = av_localwrite(v, fd, buf, rres);

        if(wres == -1) goto error;
    }

    av_close(DUMMYV, di->file.fh);
    di->file.fh = fd;
    di->file.ptr = di->size;
    di->offset = 0;
    ino->offset = 0;
  
    di->ptr = currpos;

    ino->flags |= INOF_DIRTY;

    return 0;

  error:
    if(fd != -1) av_localclose(DUMMYV, fd);
    av_del_tmpfile(ino->tmpfile);
    ino->tmpfile = NULL;
    return -1;
}

static avssize_t tar_write(ave *v, void *devinfo, const char *buf, 
			   avsize_t nbyte)
{
    arch_fdi *di = (arch_fdi *) devinfo;

    if(nbyte == 0) return 0;
  
    if(di->ino->tmpfile == NULL && copy_file(v, di) == -1) return -1;

    return (*di->vdev->write) (v, devinfo, buf, nbyte);
}

#endif

static void tar_release(struct archive *arch, struct archnode *nod)
{
    struct tarnode *tn = (struct tarnode *) nod->data;

    if(tn != NULL) {
	av_free(tn->sparsearray);
	tn->sparsearray = NULL;
    }
}


static int read_sparsearray(struct archfile *fil)
{
    int res;
    union block header;
    int counter;
    struct sp_array *sparses;
    struct tarnode *tn = (struct tarnode *) fil->nod->data;
    int size, len;
  
    av_lseek(fil->basefile, tn->headeroff, AVSEEK_SET);
    res = get_next_block(fil->basefile, &header);
    if(res < 0)
        return res;

    size = 10;
    len = 0;
    sparses = (struct sp_array *) av_malloc(size * sizeof(struct sp_array));

    for (counter = 0; counter < SPARSES_IN_OLDGNU_HEADER; counter++) {
        sparses[len].offset = 
            from_oct (1 + 12, header.oldgnu_header.sp[counter].offset);
        sparses[len].numbytes =
            from_oct (1 + 12, header.oldgnu_header.sp[counter].numbytes);

        if (!sparses[counter].numbytes) break;

        len++;
    }

    if (header.oldgnu_header.isextended)	{
        /* Read in the list of extended headers and translate them into
           the sparsearray as before.  */

        while (1) {
            res = get_next_block(fil->basefile, &header);
            if(res < 0) {
                av_free(sparses);
                return res;
            }
      
            for (counter = 0; counter < SPARSES_IN_SPARSE_HEADER; counter++) {
                if (counter + len > size - 1) {
                    /* Realloc the scratch area since we've run out of
                       room.  */

                    size *= 2;
                    sparses = (struct sp_array *)
                        av_realloc (sparses, size * sizeof (struct sp_array));
                }

                if (header.sparse_header.sp[counter].numbytes[0] == 0)
                    break;
	
                sparses[len].offset =
                    from_oct (1 + 12, header.sparse_header.sp[counter].offset);
                sparses[len].numbytes =
                    from_oct (1 + 12, header.sparse_header.sp[counter].numbytes);

                len++;
            }
            if (!header.sparse_header.isextended)
                break;
        }
    }
  
    tn->sparsearray = sparses;
    tn->sp_array_len = len;
    fil->nod->offset = fil->basefile->ptr; /* the correct offset */

    return 0;
}

static avssize_t read_sparse(vfile *vf, char *buf, avsize_t nbyte)
{
    struct archfile *fil = arch_vfile_file(vf);
    struct tarnode *tn = (struct tarnode *) fil->nod->data;
    avoff_t offset;
    avoff_t size     = fil->nod->st.size;
    avoff_t realsize = fil->nod->realsize;
    struct sp_array *sparses;
    avoff_t realoff;
    int ctr;
    avsize_t nact;
    avoff_t start, end;
    avoff_t spstart, spend;
    avoff_t cmstart, cmend;
    int res;

    if(AV_ISDIR(fil->nod->st.mode))
        return -EISDIR;

    if(vf->ptr >= size) 
        return 0;
  
    if(tn->sparsearray == NULL) {
        res = read_sparsearray(fil);
        if(res < 0)
            return res;
    }
    sparses = tn->sparsearray;
    offset = fil->nod->offset;

    nact = AV_MIN(nbyte, (avsize_t) (size - vf->ptr));
    start = vf->ptr;
    end = start + nact;

    memset(buf, 0, nact);
  
    realoff = 0;
    ctr = 0; 
    while(ctr < tn->sp_array_len && realoff < realsize) {
        spstart = sparses[ctr].offset;
        spend = spstart + sparses[ctr].numbytes;

        if(spstart < end && spend > start) {
            avoff_t rdoffset;
            cmstart = AV_MAX(spstart, start);
            cmend   = AV_MIN(spend,   end);

            rdoffset = realoff + offset + (cmstart - spstart);
            res = av_pread(fil->basefile, buf + (cmstart - start), 
                           cmend - cmstart, rdoffset);
            if(res < 0)
                return res;
            if(res != (cmend - cmstart)) {
                av_log(AVLOG_WARNING, "TAR: Broken archive");
                return -EIO;
            }
        }
    
        realoff += ((spend - spstart - 1) / BLOCKSIZE + 1) * BLOCKSIZE;
        ctr++;
    }
  
    vf->ptr += nact;
    return nact;
}


static avssize_t tar_read(vfile *vf, char *buf, avsize_t nbyte)
{
    struct archfile *fil = arch_vfile_file(vf);
    struct tarnode *tn = (struct tarnode *) fil->nod->data;

    if(tn->type == GNUTYPE_SPARSE) 
        return read_sparse(vf, buf, nbyte);
    else
        return av_arch_read(vf, buf, nbyte);
}

int av_init_module_utar(struct vmodule *module);

int av_init_module_utar(struct vmodule *module)
{
    int res;
    struct avfs *avfs;
    struct ext_info tarexts[2];
    struct archparams *ap;
    
    tarexts[0].from = ".tar",   tarexts[0].to = NULL;
    tarexts[1].from = NULL;

    res = av_archive_init("utar", tarexts, AV_VER, module, &avfs);
    if(res < 0)
        return res;

    ap = (struct archparams *) avfs->data;
    ap->parse = parse_tarfile;
    ap->read = tar_read;
    ap->release = tar_release;

    av_add_avfs(avfs);

    return 0;
}
