/* 
   Redirection kernel module for avfs/podfuk.

   Copyright (C) 1998-1999  Miklos Szeredi (mszeredi@inf.bme.hu)

   This file can be distributed either under the GNU LGPL, or under
   the GNU GPL. See the file COPYING.LIB and COPYING. 

   Needs kernel 2.4.X and the virtual file kernel patch.
   
   Compile with:
      gcc -O2 -Wall -Wstrict-prototypes -fno-strict-aliasing -pipe -D__KERNEL__ -DMODULE -D_LOOSE_KERNEL_NAMES -c nredir.c

   For podfuk, add -DPODFUK
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/coda_psdev.h>

#define NREDIR_VERSION "0.2"

#define MAGIC_CHAR '#'

#define OVERLAY_DIR "/overlay"
#define OVERLAY_DIR_LEN 8

#define path_ok(pwd) (pwd->d_parent == pwd || !list_empty(&pwd->d_hash))

static void print_mount(struct vfsmount *mnt)
{
        char *path;
        unsigned long page;
        
        page = __get_free_page(GFP_USER);
        if(page) {
                path = d_path(mnt->mnt_mountpoint, mnt->mnt_parent,
                              (char *) page, PAGE_SIZE);
                printk("   %s\n", path);
                free_page(page);
        }
}

static void nredir_umount(struct vfsmount *mnt) 
{
        int error;
        
        printk("trying to umount child:\n");
        error = do_umount(mntget(mnt), 0, 0);
        if(!error) 
                printk("SUCCESS\n");
        else
                printk("FAILED: %i\n", error);
}

static void nredir_release(struct vfsmount *mnt)
{
        struct dentry *mountpoint;
        printk("umounted overlay:\n");
        print_mount(mnt);

        MOD_DEC_USE_COUNT;
        mountpoint = mnt->mnt_mountpoint;
        if(list_empty(&mountpoint->d_vfsmnt)) {
                printk("dropped mountpoint\n");
                d_drop(mountpoint);
        }
        
}

static struct mount_operations nredir_mount_operations = 
{
	umount:			nredir_umount,
	release:		nredir_release,
};

static void mount_it(struct vfsmount *mnt, struct dentry *dentry,
                     struct nameidata *ovnd)
{
        struct vfsmount *newmnt;
        struct nameidata newnd;
        
        printk(KERN_INFO "Mounting overlay\n");
        printk(KERN_INFO "old (%s,%i), new (%s, %i)\n",
               ovnd->dentry->d_name.name, (int) ovnd->dentry->d_inode,
               dentry->d_name.name, (int) dentry->d_inode);

        down(&mount_sem);
        newnd.dentry = dentry;
        newnd.mnt = mnt;
        newmnt = add_vfsmnt(&newnd, ovnd->dentry, ovnd->mnt->mnt_devname,
                            &nredir_mount_operations);
        up(&mount_sem);
        if(newmnt) 
                MOD_INC_USE_COUNT;
        
        if(newmnt)
            printk(KERN_INFO "ov mount OK\n");
        else
            printk(KERN_INFO "ov mount FAILED\n");
}

static char *get_ov_path(struct vfsmount *mnt, struct dentry *dentry)
{
        char *ovpath;
        char *path;
        int pathlen;
        unsigned long page;

        if(!path_ok(dentry))
                return NULL;
        
        page = __get_free_page(GFP_USER);
        if(!page)
                return NULL;
        
        path = d_path(dentry, mnt, (char *) page, PAGE_SIZE);
        pathlen = page + PAGE_SIZE - (unsigned int) path - 1;

        ovpath = kmalloc(OVERLAY_DIR_LEN + pathlen + 1, GFP_USER);
        if(ovpath) {
                char *s = ovpath;

                strcpy(s, OVERLAY_DIR);
                s += OVERLAY_DIR_LEN;
                strcpy(s, path);
        }

        free_page(page);
        return ovpath;
}
                
static int mount_overlay(struct vfsmount *mnt, struct dentry *dentry)
{
        char *ovpath;
        struct nameidata ovnd;
        int result = 0;
        
        ovpath = get_ov_path(mnt, dentry);
        if(ovpath) {
                int error = 0;
                int old_link_count;
                
                printk(KERN_INFO "ovpath = '%s'\n", ovpath);
                
                old_link_count = current->link_count;
                current->link_count = 0;
                if(path_init(ovpath, LOOKUP_POSITIVE, &ovnd))
                        error = path_walk(ovpath, &ovnd);
                current->link_count = old_link_count;
                
                if(!error) {
                        mount_it(mnt, dentry, &ovnd);
                        path_release(&ovnd);
                        result = 1;
                }
                
                kfree(ovpath);
        }

        return result;
}

static int mnt_list_member(struct dentry *dentry, struct vfsmount *mnt)
{
        struct list_head *curr;
        
        curr = dentry->d_vfsmnt.next;
        while(curr != &dentry->d_vfsmnt) {
                struct vfsmount *mntentry;

                mntentry = list_entry(curr, struct vfsmount, mnt_clash);
                if(mntentry->mnt_parent == mnt)
                        return 1;
                curr = curr->next;
        }

        return 0;
}

static int nredir_revalidate(struct dentry *dentry, int flag)
{
        struct super_block *sb;
        struct list_head *curr;
        struct list_head *next;
        struct vfsmount *mnt;

        /* FIXME: this is not SMP safe */
        sb = dentry->d_sb;
        curr = sb->s_mounts.next;
        while(curr != &sb->s_mounts) {
                next = curr->next;
                mnt = list_entry(curr, struct vfsmount, mnt_instances);
                if(!mnt_list_member(dentry, mnt))
                        mount_overlay(mnt, dentry);
                curr = next;
        }
        
        return 1;
}

static void nredir_dentry_release(struct dentry *dentry)
{
        printk("nredir_dentry_release: %s\n", dentry->d_name.name);
        MOD_DEC_USE_COUNT;
}

static struct dentry_operations nredir_dentry_operations = 
{
        d_revalidate:		nredir_revalidate,
        d_release:              nredir_dentry_release,
};


static struct dentry *nredir_lookup_virtual(struct nameidata *nd, 
                                          struct dentry *dentry)
{
#if 0
        printk(KERN_INFO "nredir_lookup_virtual: %s\n", dentry->d_name.name);
#endif
        
        /* FIXME: test should not refer to coda */
        if(strchr(dentry->d_name.name, MAGIC_CHAR) != 0 &&
           dentry->d_sb->s_magic != CODA_SUPER_MAGIC) {
                if(mount_overlay(nd->mnt, dentry)) {
                        /* This dentry is MINE from now on... */
                        if(dentry->d_op && dentry->d_op->d_release)
                                dentry->d_op->d_release(dentry);

                        dentry->d_op = &nredir_dentry_operations;
                        MOD_INC_USE_COUNT;
                        nredir_revalidate(dentry, 0);
                }
        }

        return dentry;
}

static int __init init_nredir(void)
{
    printk(KERN_INFO "nredir init (version %s)\n", NREDIR_VERSION);

    printk("lookup_virtual: %x\n", (int) lookup_virtual);
    lookup_virtual = nredir_lookup_virtual;

    /* FIXME: This is a bit too brutal approach */
    printk("shrinking dcache...\n");
    shrink_dcache();
    printk("done\n");

    return 0;
}


static void __exit cleanup_nredir(void)
{
    printk(KERN_INFO "nredir cleanup\n");

    lookup_virtual = NULL;

    printk("lookup_virtual: %x\n", (int) lookup_virtual);
}

module_init(init_nredir);
module_exit(cleanup_nredir);
