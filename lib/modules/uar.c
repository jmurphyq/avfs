/*
    AVFS: A Virtual File System Library
    Copyright (C) 1998  Miklos Szeredi (mszeredi@inf.bme.hu)
    
    This file can be distributed either under the GNU LGPL, or under
    the GNU GPL. See the file COPYING.LIB and COPYING. 

    AR module
*/
#if 0
#include "archives.h"

#define ARMAGIC     "!<arch>\n"
#define ARMAGICLEN  8
#define ENDMAGIC    "`\n"

struct ar_header {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char endmagic[2];
};

struct ar_values {
    avtime_t mtime;
    avuid_t  uid;
    avgid_t  gid;
    avmode_t mode;
    avsize_t size;
    avoff_t offset;
};

static avssize_t read_file(ave *v, arch_file *file, char *buf, avsize_t nbyte)
{
    avssize_t res;
  
    res = __av_read(v, file->fh, buf, nbyte);
    if(res == -1) return -1;
    file->ptr += res;

    return res;
}

static int fill_arentry(ave *v, arch_entry *ent, struct ar_values *arv)
{
    int res;
    struct avstat filestat;

    if(ent->ino != AVNULL)
        return 0;

    __av_default_stat(&filestat);

    filestat.mode    = arv->mode;
    filestat.uid     = arv->uid;
    filestat.gid     = arv->gid;
    filestat.blocks  = AV_DIV(arv->size, 512);
    filestat.blksize = VBLOCKSIZE;
    filestat.dev     = ent->arch->dev;
    filestat.ino     = ent->arch->inoctr++;
    filestat.mtime   = arv->mtime;
    filestat.atime   = filestat.mtime;
    filestat.ctime   = filestat.mtime;
    filestat.size    = arv->size;

    res = __av_new_inode(v, ent, &filestat);
    if(res == -1)
        return -1;
  
    ent->ino->offset = arv->offset;
    ent->ino->realsize = arv->size;

    return 0;
}

static int insert_arentry(ave *v, archive *arch, struct ar_values *arv, 
			  const char *name)
{
    int res;
    arch_entry *ent;

    if(!name[0]) 
        return 0; /* Broken header: empty name */
    if((arv->mode & AV_IFMT) == 0)
        return 0; /* Broken header: illegal type */

    ent = __av_find_entry(v, arch->root, name, FIND_CREATE, 0);
    if(ent == AVNULL)
        return -1;
  
    res = fill_arentry(v, ent, arv);
    __av_unref_entry(ent);

    return res;
}

static avulong getnum(const char *s, int len, int base)
{
    avulong num;
    int i;
  
    num = 0;
    for(i = 0; i < len; i++) {
        if(s[i] >= '0' && s[i] < '0' + base) num = (num * base) + (s[i] - '0');
        else break;
    }
  
    return num;
}

static int interpret_header(struct ar_header *hbuf, struct ar_values *arv)
{
    if(__av_strncmp(hbuf->endmagic, ENDMAGIC, 2) != 0) return -1;

    arv->mtime = getnum(hbuf->date, 12, 10);
    arv->uid   = getnum(hbuf->uid,  6,  10);
    arv->gid   = getnum(hbuf->gid,  6,  10);
    arv->mode  = getnum(hbuf->mode, 8,  8);
    arv->size  = getnum(hbuf->size, 10, 10);

    return 0;
}

static int read_arfile(ave *v, arch_file *file, archive *arch)
{
    char magic[ARMAGICLEN];
    struct ar_header hbuf;
    struct ar_values arv;
    avssize_t rres;
    avoff_t sres, noff;
    char *longnames = AVNULL;
    int longnameslen = 0;
    int ret = -1;
    int i;
  
    rres = read_file(v, file, magic, ARMAGICLEN);
    if(rres == -1) return -1;
    if(rres != ARMAGICLEN || __av_strncmp(magic, ARMAGIC, ARMAGICLEN) != 0) {
        v->errn = EIO;
        return -1;
    }
  
    while(1) {
        rres = read_file(v, file, (char *) &hbuf, sizeof(hbuf));
        if(rres == -1) goto end;                       /* Error */
        if(rres == 0) break;
        if(rres != sizeof(hbuf)) break;                /* Broken archive */
        if(interpret_header(&hbuf, &arv) == -1) break; /* Broken archive */
        arv.offset = file->ptr;

        if((__av_strncmp(hbuf.name, "//              ", 16) == 0 ||
            __av_strncmp(hbuf.name, "ARFILENAMES/    ", 16) == 0)) {
            /* Long name table */

            if(longnames == AVNULL && arv.size > 0 && arv.size < (1 << 22)) {
                longnameslen = arv.size;
                longnames = __av_malloc(v, longnameslen);
                if(longnames != AVNULL) {
                    rres = read_file(v, file, longnames, longnameslen);
                    if(rres == -1) goto end;               /* Error */
                    if(rres != longnameslen) break;        /* Broken archive */
	  
                    for(i = 0; i < longnameslen; i++) {
                        if(longnames[i] == '/' || longnames[i] == '\\' ||
                           longnames[i] == '\n') longnames[i] = '\0';
                    }
                    longnames[longnameslen-1] = '\0';
                }
            }
        }
        else if((hbuf.name[0] == '/' || hbuf.name[0] == ' ') &&
                hbuf.name[1] >= '0' && hbuf.name[1] <= '9') {
            /* Long name */

            if(longnames != AVNULL) {
                int nameoffs;
	
                nameoffs = getnum(hbuf.name + 1, 15, 10);
                if(nameoffs >= 0 && nameoffs  < longnameslen) {
                    if(insert_arentry(v, arch, &arv, longnames+nameoffs) == -1) 
                        goto end;                            /* Error */
                }
            }
        }
        else if(hbuf.name[0] == '#' && hbuf.name[1] == '1' &&
                hbuf.name[2] == '/' && hbuf.name[3] >= '0' &&
                hbuf.name[3] <= '9') {
            /* BSD4.4-style long filename */

            int namelen;
      
            namelen = getnum(hbuf.name + 3, 13, 10);
            arv.size -= namelen;
            arv.offset += namelen;

            if(longnames != AVNULL) __av_free(longnames);
            longnames = __av_malloc(v, namelen+1);
            if(longnames != AVNULL) {
                rres = read_file(v, file, longnames, namelen);
                if(rres == -1) goto end;                      /* Error */
                if(rres != namelen) break;                    /* Broken archive */
	
                longnames[namelen] = '\0';
                if(insert_arentry(v, arch, &arv, longnames) == -1) 
                    goto end;                                   /* Error */

                __av_free(longnames);
                longnames = AVNULL;
            }
        }
        else if(hbuf.name[0] != '/' && 
                __av_strncmp(hbuf.name, "__.SYMDEF       ", 16) != 0) {
            /* Short name */

            for(i = 0; i < 16; i++) 
                if(hbuf.name[i] == '/') {
                    hbuf.name[i] = '\0';
                    break;
                }

            /* If no slash was found, strip spaces from end */
            if(i == 16) 
                for(i = 15; i >= 0 && hbuf.name[i] == ' '; i--) hbuf.name[i] = '\0';
      
            if(insert_arentry(v, arch, &arv, hbuf.name) == -1) 
                goto end;                                  /* Error */
        }
    
        noff = arv.offset + arv.size;
        if((noff & 1) != 0) noff++;

        sres = __av_lseek(v, file->fh, noff, AVSEEK_SET);

        if(sres == -1) goto end;                       /* Error */
        file->ptr = sres;
    }

    ret = 0;

  end:
    __av_free(longnames);
    return ret;
}

static int parse_arfile(ave *v, vpath *path, archive *arch)
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

    res = read_arfile(v, &file, arch);
  
    __av_close(DUMMYV, file.fh);
    
    return res;  
}


extern int __av_init_module_uar(ave *v);

int __av_init_module_uar(ave *v)
{
    struct ext_info arexts[3];
    struct vdev_info *vdev;
    arch_devd *dd;

    INIT_EXT(arexts[0], ".a",   AVNULL);
    INIT_EXT(arexts[1], ".deb", AVNULL);
    INIT_EXT(arexts[2], AVNULL, AVNULL);

    vdev = __av_init_arch(v, "uar", arexts, AV_VER);
    if(vdev == AVNULL) return -1;
  
    dd = (arch_devd *) vdev->devdata;
    dd->parsefunc = parse_arfile;

    return __av_add_vdev(v, vdev);
}
#endif
