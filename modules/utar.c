/*  
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)

    Based on the GNU tar sources (C) Free Software Foundation
    
    This file can be distributed under the GNU GPL. 
    See the file COPYING. 

    TAR module
*/
#if 0
#include "gtar.h"
#include "archives.h"

#define COPYBUFSIZE 16384
#define BIGBLOCKSIZE (20 * BLOCKSIZE)

/* Some constants from POSIX are given names.  */
#define NAME_FIELD_SIZE   100
#define PREFIX_FIELD_SIZE 155
#define UNAME_FIELD_SIZE   32

/* inode udata is used for saving filenames temporarily at archive creation */

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

struct tar_fdidat {
    struct sp_array *sparsearray;
    int sp_array_len;
    avoff_t headeroff;
};

struct tar_entdat {
    avuid_t uid;
    avgid_t gid;
    char uname[UNAME_FIELD_SIZE];
    char gname[UNAME_FIELD_SIZE];
};

#define ISSPACE(x) av_isspace(x)
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
/* This should be equivalent to: sprintf (WHERE, "%*lo ", DIGS - 2, VALUE);
   except that sprintf fills in the trailing NUL and we don't.  */

static void to_oct (long value, int digs, char *where)
{
    --digs;			/* Trailing null slot is left alone */
    where[--digs] = ' ';	/* put in the space, though */

    /* Produce the digits -- at least one.  */

    do
    {
        where[--digs] = '0' + (char) (value & 7);	/* one octal digit */
        value >>= 3;
    }
    while (digs > 0 && value != 0);

    /* Leading spaces, if necessary.  */
    while (digs > 0)
        where[--digs] = ' ';
}
#endif

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

static int find_next_block(ave *v, struct arch_file *file, union block *blk)
{
    int res;

    res = av_read(v, file->fh, blk->buffer, BLOCKSIZE);
    if(res == 0) return 0;
    if(res == -1) return -2;
    if(res < BLOCKSIZE) {
        v->errn = EIO;
        return -2;
    }
  
    file->ptr += BLOCKSIZE;
    return 1;
}

static int get_next_block(ave *v, struct arch_file *file, union block *blk)
{
    int res;
  
    res = find_next_block(v, file, blk);
    if(res == -1) return -2;
    if(res == 0) {
        v->errn = EIO;
        return -2;
    }
  
    return 0;
}

/* return values: -2 fatal, -1 bad header, 0 eof, 1 good */

static int read_entry(ave *v, arch_file *file, struct tar_entinfo *tinf)
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
    char *next_long_name = AVNULL, *next_long_link = AVNULL;
    union block *header = &tinf->header;

    while (1)
    {
        res = find_next_block(v, file, header);
        if (res <= 0) break;  /* HEADER_END_OF_FILE */

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
            res = -1;
            v->errn = EIO;
            av_log(AVLOG_WARNING, "utar: Bad header");
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
            bp = *longp = (char *) av_malloc (v, (avsize_t) tinf->size);
            if (bp == AVNULL) {
                res = -2;
                break;
            }

            for (size = tinf->size; size > 0; size -= written)
	    {
                res = get_next_block (v, file, &data_block);
                if (res < 0) break;
                written = BLOCKSIZE;
                if (written > size)
                    written = size;

                av_memcpy (bp, data_block.buffer, (avsize_t) written);
                bp += written;
	    }
            if(res < 0) break;

            /* Loop!  */

	}
        else
	{
            tinf->datastart = file->ptr;

            if (header->oldgnu_header.isextended) {
                do {
                    res = get_next_block (v, file, &data_block);
                    if(res < 0) break;
                }
                while(data_block.sparse_header.isextended);
            }
            if(res < 0) break;

            sres = av_lseek(v, file->fh, 
                              AV_DIV(tinf->size, BLOCKSIZE) * BLOCKSIZE, 
                              AVSEEK_CUR);
            if(sres == -1) {
                res = -2;
                break;
            }
            file->ptr = sres;

            tinf->name = av_strdup (v, next_long_name ? next_long_name
                                      : header->header.name);
            tinf->linkname = av_strdup (v, next_long_link ? next_long_link
                                          : header->header.linkname);

            if(tinf->name == AVNULL || tinf->linkname == AVNULL) {
                av_free(tinf->name);
                av_free(tinf->linkname);
                res = -2;
            }
            else res = 1;
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

    if (av_strcmp (header->header.magic, TMAGIC) == 0)
        format = POSIX_FORMAT;
    else if (av_strcmp (header->header.magic, OLDGNU_MAGIC) == 0)
        format = OLDGNU_FORMAT;
    else
        format = V7_FORMAT;
    *format_pointer = format;

    stat_info->mode = from_oct (8, header->header.mode);
    stat_info->mode &= 07777;
    stat_info->mtime = from_oct (1 + 12, header->header.mtime);

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

        av_strncpy(ugname, header->header.uname, UNAME_FIELD_SIZE);
        stat_info->uid = av_finduid(ugname, from_oct (8, header->header.uid),
                                      cache);

        av_strncpy(ugname, header->header.gname, UNAME_FIELD_SIZE);
        stat_info->gid = av_findgid(ugname, from_oct (8, header->header.gid),
                                      cache);

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


static int check_existing(ave *v, arch_entry *ent, struct avstat *tarstat)
{
    
     if(ent == ent->arch->root) {
         av_log(AVLOG_WARNING, "utar: Empty filename");
         return -1;
     }
     if(AV_ISDIR(ent->ino->st.mode)) {
         if(AV_ISDIR(tarstat->mode)) {
             ent->ino->st.mode = tarstat->mode;
             ent->ino->st.uid = tarstat->uid;
             ent->ino->st.gid = tarstat->gid;
             ent->ino->st.mtime = tarstat->mtime;
             ent->ino->origst = ent->ino->st;
             return 0;
         }
         else {
             av_log(AVLOG_WARNING,
                      "utar: Overwriting directory with file");
             return -1;
         }
     }

     av_unlink_inode(ent);
     return 1;
 }

 static int fill_link(ave *v, arch_entry *ent, const char *linkname)
 {
     archive *arch = ent->arch;
     arch_entry *entlink;

     entlink = av_find_entry(DUMMYV, arch->root, linkname, FIND_POSITIVE, 0);
     if(entlink == AVNULL) {
         av_log(AVLOG_WARNING, "utar: Illegal hard link");
         return -1; 
     }
     ent->ino = entlink->ino;
    av_link_inode(ent);
    av_unref_entry(entlink);
    
    return 0;
}

static int fill_node(ave *v, arch_entry *ent, struct tar_entinfo *tinf,
                     struct avstat *tarstat)
{
    int res;

    tarstat->blocks = AV_DIV(tinf->size, 512);
    tarstat->blksize = BLOCKSIZE;
    tarstat->dev = ent->arch->dev;
    tarstat->ino = ent->arch->inoctr++;
    tarstat->atime = tarstat->mtime;  /* FIXME */
    tarstat->ctime = tarstat->mtime;

    res = av_new_inode(v, ent, tarstat);
    if(res == -1)
        return -2;

    ent->ino->offset = tinf->datastart;
    ent->ino->realsize = tinf->size;
    ent->ino->typeflag = tinf->header.header.typeflag;

    if(tinf->header.header.typeflag == SYMTYPE) {
        ent->ino->syml = tinf->linkname;
        tinf->linkname = AVNULL;
    }

    return 0;
}

static int fill_entry(ave *v, arch_entry *ent, struct tar_entinfo *tinf,
                      struct avstat *tarstat)
{
    int res;
    union block *header = &tinf->header;
    struct tar_entdat *ed;

    if(ent->ino != AVNULL) {
        res = check_existing(v, ent, tarstat);
        if(res != 1)
            return res;
    }

    AV_NEW(v, ed);
    if(ed == AVNULL)
        return -2;

    ed->uid = from_oct (8, header->header.uid);
    ed->gid = from_oct (8, header->header.gid);
    av_strncpy(ed->uname, header->header.uname, UNAME_FIELD_SIZE);
    av_strncpy(ed->gname, header->header.gname, UNAME_FIELD_SIZE);

    if(header->header.typeflag == LNKTYPE) 
        res = fill_link(v, ent, tinf->linkname);
    else 
        res = fill_node(v, ent, tinf, tarstat);

    if(res == 0)
        ent->udata = (void *) ed;
    else
        av_free(ed);
    
    return res;
}

/* insert_tarentry() returns -2 on fatal, -1 on minor, 0 on OK */

static int insert_tarentry(ave *v, archive *arch, struct tar_entinfo *tinf,
                           struct avstat *tarstat)
{
    int res;
    arch_entry *ent = AVNULL;

    if(tinf->header.header.typeflag == GNUTYPE_SPARSE) {
        arch->flags |= ARCHF_RDONLY;
        if(arch->readonly_reason == AVNULL)
            arch->readonly_reason =
                av_strdup(DUMMYV, "utar: Cannot modify archive containing sparsefiles");
    }

    /* Appears to be a file.  But BSD tar uses the convention that a
       slash suffix means a directory.  */
    if(AV_ISREG(tarstat->mode) && 
       tinf->name[av_strlen(tinf->name)-1] == DIR_SEP_CHAR) 
        tarstat->mode = (tarstat->mode & 07777) | AV_IFDIR;
    
    ent = av_find_entry(v, arch->root, tinf->name, FIND_CREATE, 0);
    if(ent == AVNULL)
        return -2;

    res = fill_entry(v, ent, tinf, tarstat);
    av_unref_entry(ent);

    return res;
}

static int read_tarfile(ave *v, arch_file *file, archive *arch)
{
    struct tar_entinfo tinf;
    enum archive_format format;
    struct ugidcache cache;
    struct avstat tarstat;
    int res;

    av_init_ugidcache(&cache);

    while(1) {
        res = read_entry(v, file, &tinf);
        if(res == -2) return -1;
        else if(res == -1) {
            arch->flags |= ARCHF_RDONLY; /* Broken archive */

            if(arch->readonly_reason == AVNULL) 
                arch->readonly_reason = 
                    av_strdup(DUMMYV, "utar: Cannot modify archive with errors");

            continue;
        }
        else if(res == 0)
            return 0;

        av_default_stat(&tarstat);
        decode_header(&tinf.header, &tarstat, &format, &cache);

        res = insert_tarentry(v, arch, &tinf, &tarstat);
        av_free(tinf.name);
        av_free(tinf.linkname);

        if(res == -2)
            return -1;
    }
}


static int parse_tarfile(ave *v, vpath *path, archive *arch)
{
    int res;
    arch_file file;
    const char *params = PARAM(path);

    if(params[0] && (params[0] != PARAM_WRITABLE || params[1])) {
        v->errn = ENOENT;
        return -1;
    }

    file.fh = av_open(v, BASE(path), AVO_RDONLY, 0);
    if(file.fh == -1)
        return -1;
    file.ptr = 0;

    res = read_tarfile(v, &file, arch);
  
    av_close(DUMMYV, file.fh);
    
    return res;  
}


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

    if(!(ino->flags & INOF_CREATED) &&  ted != AVNULL && 
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

    if(ino->udata != AVNULL)
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
        name = av_strconcat(v, path, ent->name, AVNULL);
    else
        name = av_strconcat(v, path, ent->name, "/", AVNULL);

    if(name == AVNULL) return -1;

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
        if(ino->tmpfile != AVNULL) {
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

    for(ent = dir->subdir; ent != AVNULL; ent = ent->next) {
        ted = (struct tar_entdat *) ent->udata;
        ino = ent->ino;

        if(!(ino->flags & INOF_AUTODIR)) {
            if(create_entry(v, ent, path, file, outfd, cache) == -1) return -1;
      
            if(!AV_ISDIR(ino->st.mode) && ino->st.nlink > 1 && 
               ino->udata == AVNULL) {
                ino->udata = av_strconcat(v, path, ent->name, AVNULL);
                if(ino->udata == AVNULL) return -1;
            }
        }

        if(AV_ISDIR(ino->st.mode)) {
            int dirchanged;
            char *newpath;

            if(ted == AVNULL) dirchanged = 1; /* Renamed directory */
            else dirchanged = 0;

            newpath = av_strconcat(v, path, ent->name, "/", AVNULL);
            if(newpath == AVNULL) return -1;

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

    for(ent = dir->subdir; ent != AVNULL; ent = ent->next) {
        av_free(ent->ino->udata);
        ent->ino->udata = AVNULL;
        if(AV_ISDIR(ent->ino->st.mode)) clear_filenames(ent->ino);
    }
}

static int need_origarch(arch_inode *dir)
{
    arch_entry *ent;

    for(ent = dir->subdir; ent != AVNULL; ent = ent->next) {
        if(ent->ino->tmpfile == AVNULL && AV_ISREG(ent->ino->st.mode) &&
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
    if(rf == AVNULL) return -1;

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


static int tar_close(ave *v, void *devinfo)
{
    arch_fdi *di = (arch_fdi *) devinfo;
    struct tar_fdidat *tfd = (struct tar_fdidat *) di->udata;
  
    av_free(tfd->sparsearray);
  
    return (*di->vdev->close)(v, devinfo);
}


static void *tar_open(ave *v, vpath *path, int flags, int mode)
{
    arch_fdi *di;
    arch_devd *dd = (arch_devd *) av_get_vdev(path)->devdata;
    struct tar_fdidat *tfd;

    AV_NEW(v, tfd);
    if(tfd == AVNULL) return AVNULL;

    di = (arch_fdi *) (*dd->vdev->open)(v, path, flags, mode);
    if(di == AVNULL) {
        av_free(tfd);
        return AVNULL;
    }
  
    tfd->sparsearray = AVNULL;
    tfd->headeroff = di->ino->offset - BLOCKSIZE;

    di->udata = (void *) tfd;

    return (void *) di;
}

static int read_sparsearray(ave *v, arch_fdi *di)
{
    int res;
    union block header;
    int counter;
    struct sp_array *newarray, *sparses;
    struct tar_fdidat *tfd = (struct tar_fdidat *) di->udata;
    int size, len;
  
    di->file.ptr = av_lseek(v, di->file.fh, tfd->headeroff, AVSEEK_SET);
    if(di->file.ptr == -1) return -1;
  
    res = find_next_block(v, &di->file, &header);
    if(res <= 0) return -1;

    size = 10;
    len = 0;
    sparses = 
        (struct sp_array *) av_malloc(v, size * sizeof(struct sp_array));
    if(sparses == AVNULL) return -1;

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
            res = find_next_block(v, &di->file, &header);
            if(res <= 0) goto error;
      
            for (counter = 0; counter < SPARSES_IN_SPARSE_HEADER; counter++) {
                if (counter + len > size - 1) {
                    /* Realloc the scratch area since we've run out of
                       room.  */

                    size *= 2;
                    newarray = (struct sp_array *)
                        av_realloc (v, sparses, size * sizeof (struct sp_array));
                    if(newarray == AVNULL) goto error;
                    sparses = newarray;
                }

                if (header.sparse_header.sp[counter].numbytes[0] == 0) break;
	
                sparses[len].offset =
                    from_oct (1 + 12, header.sparse_header.sp[counter].offset);
                sparses[len].numbytes =
                    from_oct (1 + 12, header.sparse_header.sp[counter].numbytes);

                len++;
            }
            if (!header.sparse_header.isextended) break;
        }
    }
  
    tfd->sparsearray = sparses;
    tfd->sp_array_len = len;
    di->ino->offset = di->file.ptr; /* the correct offset */
    return 0;

  error:
    av_free(sparses);
    return -1;
}

static avssize_t read_sparse(ave *v, arch_fdi *di, char *buf, avsize_t nbyte)
{
    struct tar_fdidat *tfd = (struct tar_fdidat *) di->udata;
    avoff_t offset   = di->ino->offset;
    avoff_t size     = di->ino->st.size;
    avoff_t realsize = di->ino->realsize;
    struct sp_array *sparses;
    avoff_t realoff;
    int ctr;
    avsize_t nact;
    avoff_t start, end;
    avoff_t spstart, spend;
    avoff_t cmstart, cmend;
    int res;

    if(AV_ISDIR(di->ino->st.mode)) {
        v->errn = EISDIR;
        return -1;
    }
  
    if(tfd->sparsearray == AVNULL) {
        res = read_sparsearray(v, di);
        if(res == -1) return -1;
    }
    sparses = tfd->sparsearray;

    if(di->ptr >= size) return 0;

    nact = AV_MIN(nbyte, (avsize_t) (size - di->ptr));
    start = di->ptr;
    end = start + nact;

    av_memset(buf, 0, nact);
  
    realoff = 0;
    ctr = 0; 
    while(ctr < tfd->sp_array_len && realoff < realsize) {
        spstart = sparses[ctr].offset;
        spend = spstart + sparses[ctr].numbytes;

        if(spstart < end && spend > start) {
            cmstart = AV_MAX(spstart, start);
            cmend   = AV_MIN(spend,   end);

            di->file.ptr = 
                av_lseek(v, di->file.fh,
                           realoff + offset + (cmstart - spstart), 
                           AVSEEK_SET);
            if(di->file.ptr == -1) return -1;
            res = av_read(v, di->file.fh, buf + (cmstart - start), 
                            cmend - cmstart);
            if(res == -1) {
                di->file.ptr = -1;
                return -1;
            }
            di->file.ptr += res;
            if(res != (cmend - cmstart)) {
                v->errn = EIO;
                return -1;
            }
        }
    
        realoff += ((spend - spstart - 1) / BLOCKSIZE + 1) * BLOCKSIZE;
        ctr++;
    }
  
    di->ptr += nact;
    return nact;
}


static avssize_t tar_read(ave *v, void *devinfo, char *buf, avsize_t nbyte)
{
    arch_fdi *di = (arch_fdi *) devinfo;

    if(di->ino->typeflag == GNUTYPE_SPARSE) 
        return read_sparse(v, di, buf, nbyte);

    return (*di->vdev->read)(v, devinfo, buf, nbyte);
}

static int copy_file(ave *v, arch_fdi *di)
{
    arch_inode *ino = di->ino;
    avoff_t currpos;
    char buf[COPYBUFSIZE];
    avssize_t rres, wres;
    int fd;

    ino->tmpfile = av_get_tmpfile(v);
    if(ino->tmpfile == AVNULL) return -1;

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
    ino->tmpfile = AVNULL;
    return -1;
}

static avssize_t tar_write(ave *v, void *devinfo, const char *buf, 
			   avsize_t nbyte)
{
    arch_fdi *di = (arch_fdi *) devinfo;

    if(nbyte == 0) return 0;
  
    if(di->ino->tmpfile == AVNULL && copy_file(v, di) == -1) return -1;

    return (*di->vdev->write) (v, devinfo, buf, nbyte);
}

extern int av_init_module_utar(ave *v);

int av_init_module_utar(ave *v)
{
    struct ext_info tarexts[2];
    struct vdev_info *vdev;
    struct arch_devd *dd;

    INIT_EXT(tarexts[0], ".tar", AVNULL);
    INIT_EXT(tarexts[1], AVNULL, AVNULL);

    vdev = av_init_arch(v, "utar", tarexts, AV_VER);
    if(vdev == AVNULL) return -1;

    dd = (arch_devd *) vdev->devdata;

    dd->parsefunc   = parse_tarfile;
    dd->flushfunc   = flush_tarfile;

    dd->flags |= DEVF_WRITABLE | DEVF_NEEDWRITEPARAM;
    dd->blksize = BLOCKSIZE;

    vdev->open      = tar_open;
    vdev->close     = tar_close;
    vdev->read      = tar_read;
    vdev->write     = tar_write;
  
    return av_add_vdev(v, vdev);
}
#endif
