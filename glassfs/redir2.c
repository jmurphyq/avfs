#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/string.h>
#include <linux/init.h>

#define REDIR2_VERSION "0.0"

#define AVFS_MAGIC_CHAR '#'
#define OVERLAY_DIR "/overlay"
#define OVERLAY_DIR_LEN 8

#define FUSE_MAGIC 0x65735546

static struct dentry *(*orig_lookup)(struct inode *, struct dentry *,
				     struct nameidata *);


static struct vfsmount *orig_mount;
static struct semaphore redir2_sem;

static struct super_operations dummy_super_operations;
static struct super_block dummy_sb = {
	.s_op = &dummy_super_operations,
};

static int is_avfs(const unsigned char *name, unsigned int len)
{
	for (; len--; name++)
		if (*name == AVFS_MAGIC_CHAR)
			return 1;
	return 0;
}

static char * my_d_path( struct dentry *dentry, struct vfsmount *vfsmnt,
			struct dentry *root, struct vfsmount *rootmnt,
			char *buffer, int buflen)
{
	char * end = buffer+buflen;
	char * retval;
	int namelen;

	*--end = '\0';
	buflen--;
	if (!IS_ROOT(dentry) && d_unhashed(dentry)) {
		buflen -= 10;
		end -= 10;
		if (buflen < 0)
			goto Elong;
		memcpy(end, " (deleted)", 10);
	}

	if (buflen < 1)
		goto Elong;
	/* Get '/' right */
	retval = end-1;
	*retval = '/';

	for (;;) {
		struct dentry * parent;

		if (dentry == root && vfsmnt == rootmnt)
			break;
		if (dentry == vfsmnt->mnt_root || IS_ROOT(dentry)) {
			/* Global root? */
			spin_lock(&vfsmount_lock);
			if (vfsmnt->mnt_parent == vfsmnt) {
				spin_unlock(&vfsmount_lock);
				goto global_root;
			}
			dentry = vfsmnt->mnt_mountpoint;
			vfsmnt = vfsmnt->mnt_parent;
			spin_unlock(&vfsmount_lock);
			continue;
		}
		parent = dentry->d_parent;
		prefetch(parent);
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			goto Elong;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		retval = end;
		dentry = parent;
	}

	return retval;

global_root:
	namelen = dentry->d_name.len;
	buflen -= namelen;
	if (buflen < 0)
		goto Elong;
	retval -= namelen-1;	/* hit the slash */
	memcpy(retval, dentry->d_name.name, namelen);
	return retval;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

static int mount_avfs(struct dentry *dentry, char *path, int mode)
{
	struct inode *inode;

	inode = new_inode(&dummy_sb);
	if (!inode)
		return -ENOMEM;

	inode->i_mode = mode;
	d_instantiate(dentry, inode);
	
	char *argv[] = { "/bin/mount", "--bind", path, path + OVERLAY_DIR_LEN,
			 NULL };
	char *envp[] = { NULL };
	int ret;
	ret = call_usermodehelper("/bin/mount", argv, envp, 1);
	printk("mount ret: %i\n", ret);
	if (ret) {
		d_delete(dentry);
		return -EINVAL;
	}

	return 0;
}

static int exists_avfs(char *path, int *modep)
{
	int err;
	struct nameidata avfsnd;

	printk("lookup_avfs: '%s'\n", path);

	avfsnd.last_type = LAST_ROOT;
	avfsnd.flags = 0;
	avfsnd.mnt = mntget(orig_mount);
	avfsnd.dentry = dget(orig_mount->mnt_sb->s_root);
	err = path_walk(path, &avfsnd);
	if (err)
		return 0;

	if(!avfsnd.dentry->d_inode) {
		path_release(&avfsnd);
		return 0;
	}
	*modep = avfsnd.dentry->d_inode->i_mode;
	path_release(&avfsnd);
	return 1;
}

static int lookup_avfs(struct dentry *dentry, struct nameidata *nd)
{
	char *page;
	char *path;
	int err;
	
	err = -ENOMEM;
	page = (char *) __get_free_page(GFP_KERNEL);
	if (page) {
		unsigned int offset = PAGE_SIZE - dentry->d_name.len - 2;
		spin_lock(&dcache_lock);
		path = my_d_path(nd->dentry, nd->mnt, nd->mnt->mnt_sb->s_root, nd->mnt, page, offset);
		spin_unlock(&dcache_lock);
		err = -ENAMETOOLONG;
		if (!IS_ERR(path) && page + OVERLAY_DIR_LEN < path) {
			unsigned int pathlen = strlen(path);
			int mode;
			path[pathlen] = '/';
			memcpy(path + pathlen + 1, dentry->d_name.name,
			       dentry->d_name.len);
			path[1 + pathlen + dentry->d_name.len] = '\0';
			path -= OVERLAY_DIR_LEN;
			memcpy(path, OVERLAY_DIR, OVERLAY_DIR_LEN);
			
			if (exists_avfs(path, &mode))
				err = mount_avfs(dentry, path, mode);
			else
				err = -ENOENT;
		}
		free_page((unsigned long) page);
	}
	return err;
}

static int redir2_dentry_revalidate(struct dentry *dentry,
				    struct nameidata *nd)
{
	//printk("redir2_dentry_revalidate\n");
	return 0;
}

static struct dentry_operations redir2_dentry_operations = {
	.d_revalidate	= redir2_dentry_revalidate,
};

static inline int is_create(struct nameidata *nd)
{
	if (!nd)
		return 1;
	if ((nd->flags & LOOKUP_CREATE) && !(nd->flags & LOOKUP_CONTINUE))
		return 1;
	return 0;
}

static struct dentry *lookup_maybeavfs(struct inode *dir,
				       struct dentry *dentry,
				       struct nameidata *nd)
{
	int err;

	dentry->d_op = &redir2_dentry_operations;
	dentry->d_flags |= DCACHE_AUTOFS_PENDING;
		
	d_add(dentry, NULL);
	up(&dir->i_sem);
	err = lookup_avfs(dentry, nd);
	down(&dir->i_sem);
	if (err)
		return ERR_PTR(err);
	return NULL;

}

static struct dentry *redir2_lookup(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd)
{
	//printk("lookup %.*s\n", dentry->d_name.len, dentry->d_name.name);

	if (is_create(nd) || !is_avfs(dentry->d_name.name, dentry->d_name.len))
		return orig_lookup(dir, dentry, nd);

	return lookup_maybeavfs(dir, dentry, nd);
}

static int __init init_redir2(void)
{
	printk(KERN_INFO "redir2 init (version %s)\n", REDIR2_VERSION);

	sema_init(&redir2_sem, 1);
	read_lock(&current->fs->lock);
	orig_mount = mntget(current->fs->rootmnt);
	orig_lookup = current->fs->root->d_inode->i_op->lookup;
	current->fs->root->d_inode->i_op->lookup = redir2_lookup;
	read_unlock(&current->fs->lock);

	return 0;
}

static void __exit exit_redir2(void)
{
	printk(KERN_INFO "redir2 cleanup\n");

	if(orig_lookup)
		current->fs->root->d_inode->i_op->lookup = orig_lookup;
	mntput(orig_mount);
}


module_init(init_redir2)
module_exit(exit_redir2)


MODULE_LICENSE("GPL");

/* 
 * Local Variables:
 * indent-tabs-mode: t
 * c-basic-offset: 8
 * End:
 */
