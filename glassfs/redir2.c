#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/string.h>
#include <linux/init.h>

#define REDIR2_VERSION "0.0"

#define TRIGFS_MAGIC	0x28476c62

static struct dentry *(*orig_lookup)(struct inode *, struct dentry *,
				     struct nameidata *);


static struct file_system_type trigfs_fs_type;

static int is_avfs(const unsigned char *name, unsigned int len)
{
	for (; len--; name++)
		if (*name == (unsigned char) '@')
			return 1;
	return 0;
}

#if 0
/*
	char *page;
	char *path;

	page = (char *) __get_free_page(GFP_KERNEL);
	if(!page)
		return result;

	path = d_path(nd->dentry,nd->mnt, page, PAGE_SIZE);
	
	printk("redir2_lookup: '%s/%.*s'\n",
			       path,
1			       dentry->d_name.len,
			       dentry->d_name.name);
			free_page((unsigned long) page);
		}
	}
	return orig_lookup(inode, dentry, nd);
*/
#endif


static int trigfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	printk("trigfs_follow_link\n");
	return 0;
}

static struct inode_operations trigfs_inode_operations = {
	.follow_link	= trigfs_follow_link,
};

static struct dentry *redir2_lookup(struct inode *inode, struct dentry *dentry,
				    struct nameidata *nd)
{
	struct dentry *result;

	result = orig_lookup(inode, dentry, nd);
	if(nd && !result && !dentry->d_inode &&
	   is_avfs(dentry->d_name.name, dentry->d_name.len)) {
		printk("redir2_lookup: %.*s\n", 
		       dentry->d_name.len,
		       dentry->d_name.name);

		
	}
	return result;	
}

static struct super_operations trigfs_ops = {
};

static int trigfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = 12;
	sb->s_magic = TRIGFS_MAGIC;
	sb->s_op = &trigfs_ops;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;

	inode->i_mode = S_IFDIR | 0777;
	inode->i_op = &trigfs_inode_operations;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static struct super_block *trigfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, trigfs_fill_super);
}


static struct file_system_type trigfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "trigfs",
	.get_sb		= trigfs_get_sb,
	.kill_sb	= kill_anon_super,
};


static int __init init_redir2(void)
{
	printk(KERN_INFO "redir2 init (version %s)\n", REDIR2_VERSION);
	
	orig_lookup = current->fs->root->d_inode->i_op->lookup;
	current->fs->root->d_inode->i_op->lookup = redir2_lookup;

	return register_filesystem(&trigfs_fs_type);
}

static void __exit exit_redir2(void)
{
	printk(KERN_INFO "redir2 cleanup\n");

	unregister_filesystem(&trigfs_fs_type);
	if(orig_lookup)
		current->fs->root->d_inode->i_op->lookup = orig_lookup;
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
