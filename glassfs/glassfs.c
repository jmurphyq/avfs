#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/security.h>

#include <asm/uaccess.h>

/* some random number */
#define GLASSFS_MAGIC	0x28476c61


static struct super_operations glassfs_ops;
static struct inode_operations glassfs_file_inode_operations;
static struct file_operations glassfs_file_operations;
static struct inode_operations glassfs_dir_inode_operations;
static struct inode_operations glassfs_symlink_inode_operations;
static struct dentry_operations glassfs_dentry_operations;

static struct inode *glassfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_fop = &glassfs_file_operations;

		switch (mode & S_IFMT) {
		default:
			inode->i_op = &glassfs_file_inode_operations;
			break;
		case S_IFDIR:
			inode->i_op = &glassfs_dir_inode_operations;
			break;
		case S_IFLNK:
			inode->i_op = &glassfs_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

static void change_list(struct list_head *oldlist, struct list_head *newlist)
{
	struct list_head *prev = oldlist->prev;
	struct list_head *next = oldlist->next;
	prev->next = newlist;
	next->prev = newlist;
}

static void exchange_lists(struct list_head *list1, struct list_head *list2)
{
	change_list(list1, list2);
	change_list(list2, list1);
}

static void exchange_files(struct file *file1, struct file *file2)
{
	struct file tmp;
	
	exchange_lists(&file1->f_list, &file2->f_list);
	exchange_lists(&file1->f_ep_links, &file2->f_ep_links);

	tmp = *file1;
	*file1 = *file2;
	*file2 = tmp;
}

static int glassfs_open(struct inode *inode, struct file *file)
{
	struct vfsmount *nmnt = mntget(file->f_vfsmnt->mnt_sb->s_fs_info);
	struct dentry *ndentry = dget(file->f_dentry->d_fsdata);
	struct file *nfile;
//	printk("glassfs_open\n");
	nfile = dentry_open(ndentry, nmnt, file->f_flags);
	if(IS_ERR(nfile))
		return PTR_ERR(nfile);
	exchange_files(file, nfile);
	fput(nfile);
	return 0;
}

static int glassfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			   struct kstat *stat)
{
	struct vfsmount *nmnt = mnt->mnt_sb->s_fs_info;
	struct dentry *ndentry = dentry->d_fsdata;
//	printk("glassfs_getattr\n");
	return vfs_getattr(nmnt, ndentry, stat);
}

static int is_avfs(const unsigned char *name, unsigned int len)
{
	for (; len--; name++)
		if (*name == (unsigned char) '@')
			return 1;
	return 0;
}

static void print_path(struct dentry *dentry, struct nameidata *nd)
{
	char *page;
	char *path;

	page = (char *) __get_free_page(GFP_KERNEL);
	if (page) {
		path = d_path(nd->dentry,nd->mnt, page, PAGE_SIZE);
		printk("glassfs: '%s/%.*s'\n", path,
		       dentry->d_name.len, dentry->d_name.name);
		free_page((unsigned long) page);
	}
}


static struct dentry *glassfs_lookup(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd)
{
	struct dentry *ndir = dir->u.generic_ip;
	struct dentry *ndentry;
	struct inode *inode = NULL;

//	printk("glassfs_lookup\n");
	down(&ndir->d_inode->i_sem);
	ndentry = lookup_hash(&dentry->d_name, ndir);
	up(&ndir->d_inode->i_sem);
	if(nd && !ndentry->d_inode &&
	   is_avfs(dentry->d_name.name, dentry->d_name.len)) {
		struct dentry *avfsdentry;

		avfsdentry = lookup_avfs(dentry, nd);
		if(avfsdentry->d_inode) {
			
		}
	}

	if(ndentry->d_inode) {
		struct inode *ninode = ndentry->d_inode;
		inode = glassfs_get_inode(dir->i_sb, ninode->i_mode,
					  ninode->i_rdev);
		if(!inode) {
			dput(ndentry);
			return ERR_PTR(-ENOMEM);
		}
		inode->u.generic_ip = dget(ndentry);
	}
	dentry->d_fsdata = ndentry;
	dentry->d_op = &glassfs_dentry_operations;
	
	return d_splice_alias(inode, dentry);
}

static int glassfs_permission(struct inode *inode, int mask,
			   struct nameidata *nd)
{
	struct dentry *ndentry = inode->u.generic_ip;
	struct inode *ninode = ndentry->d_inode;
//	printk("glassfs_permission\n");
	return permission(ninode, mask, NULL);
}


static int glassfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct vfsmount *nmnt = sb->s_fs_info;
	struct super_block *nsb = nmnt->mnt_sb;
//	printk("glassfs_statfs\n");
	return vfs_statfs(nsb, buf);
}

static void glassfs_dentry_release(struct dentry *dentry)
{
	struct dentry *ndentry = dentry->d_fsdata;
//	printk("glassfs_dentry_release\n");
	dput(ndentry);
}

static int glassfs_dentry_revalidate(struct dentry *dentry,
				     struct nameidata *nd)
{
	struct dentry *ndentry = dentry->d_fsdata;
//	printk("glassfs_dentry_revalidate\n");
	if(ndentry->d_op && ndentry->d_op->d_revalidate)
		return ndentry->d_op->d_revalidate(ndentry, NULL);
	return 1;
}

static	int glassfs_readlink (struct dentry *dentry, char __user *buf,
			      int buflen)
{
	struct dentry *ndentry = dentry->d_fsdata;
	struct inode *ninode = ndentry->d_inode;
	int error;
	
	error = -EINVAL;
	if (ninode->i_op && ninode->i_op->readlink) {
		error = security_inode_readlink(ndentry);
		if (!error) {
			update_atime(ninode);
			error = ninode->i_op->readlink(ndentry, buf, buflen);
		}
	}
	return error;
}

static int glassfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct dentry *ndentry = dentry->d_fsdata;
	struct inode *ninode = ndentry->d_inode;
	int error = 0;
	
	if (ninode->i_op && ninode->i_op->follow_link) {
		error = security_inode_follow_link(ndentry, nd);
		if(!error) {
			update_atime(ninode);
			error = ninode->i_op->follow_link(ndentry, nd);
		}
	}
	return error;
}

static void glassfs_clear_inode(struct inode *inode)
{
	struct dentry *ndentry = inode->u.generic_ip;
	dput(ndentry);
}

static struct dentry_operations glassfs_dentry_operations = {
	.d_revalidate	= glassfs_dentry_revalidate,
	.d_release      = glassfs_dentry_release,
};

static struct file_operations glassfs_file_operations = {
	.open		= glassfs_open,
};


static struct inode_operations glassfs_dir_inode_operations = {
	.lookup		= glassfs_lookup,
	.permission	= glassfs_permission,
	.getattr	= glassfs_getattr,
};

static struct inode_operations glassfs_file_inode_operations = {
	.permission	= glassfs_permission,
	.getattr	= glassfs_getattr,
};

static struct inode_operations glassfs_symlink_inode_operations = {
	.readlink	= glassfs_readlink,
	.follow_link	= glassfs_follow_link,
	.permission	= glassfs_permission,
	.getattr	= glassfs_getattr,
};


static struct super_operations glassfs_ops = {
	.statfs		= glassfs_statfs,
	.clear_inode	= glassfs_clear_inode,
};


static int glassfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	struct vfsmount *nmnt = current->fs->rootmnt;
	struct super_block *nsb = nmnt->mnt_sb;

	sb->s_blocksize = nsb->s_blocksize;
	sb->s_blocksize_bits = nsb->s_blocksize_bits;
	sb->s_magic = GLASSFS_MAGIC;
	sb->s_op = &glassfs_ops;
	inode = glassfs_get_inode(sb, S_IFDIR | 0755, 0);
	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_fs_info = mntget(nmnt);
	root->d_fsdata = dget(current->fs->root);
	inode->u.generic_ip = current->fs->root;

	sb->s_root = root;
	return 0;
}

static struct super_block *glassfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
//	printk("glassfs_get_sb\n");
	return get_sb_nodev(fs_type, flags, data, glassfs_fill_super);
}

static void glassfs_kill_sb(struct super_block *sb)
{
	struct vfsmount *nmnt = sb->s_fs_info;
	struct dentry *ndentry = sb->s_root->d_fsdata;
	
//	printk("glassfs_kill_sb\n");
	dput(ndentry);
	mntput(nmnt);

	kill_anon_super(sb);
}

static struct file_system_type glassfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "glassfs",
	.get_sb		= glassfs_get_sb,
	.kill_sb	= glassfs_kill_sb,
};

static int __init init_glassfs_fs(void)
{
	return register_filesystem(&glassfs_fs_type);
}

static void __exit exit_glassfs_fs(void)
{
	unregister_filesystem(&glassfs_fs_type);
}


module_init(init_glassfs_fs)
module_exit(exit_glassfs_fs)


MODULE_LICENSE("GPL");

/* 
 * Local Variables:
 * indent-tabs-mode: t
 * c-basic-offset: 8
 * End:
 */
