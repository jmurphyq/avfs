#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/string.h>
#include <linux/init.h>

#define REDIR2_VERSION "0.0"

#define AVFS_MAGIC_CHAR '#'

static struct dentry *(*orig_lookup)(struct inode *, struct dentry *,
				     struct nameidata *);


static int is_avfs(const unsigned char *name, unsigned int len)
{
	for (; len--; name++)
		if (*name == AVFS_MAGIC_CHAR)
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
#if 0
		result = d_alloc(dentry->d_parent, &dentry->d_name);
		if (!result)
			result = ERR_PTR(-ENOMEM);
		else {
			/* Take over the dentry */
			d_drop(dentry);
			dentry = result;
			dentry->d_op = &redir2_dentry_operations;
			d_add(dentry, NULL);
		}
#endif

static inline int is_create(struct nameidata *nd)
{
	if (!nd)
		return 1;
	if ((nd->flags & LOOKUP_CREATE) && !(nd->flags & LOOKUP_CONTINUE))
		return 1;
	return 0;
}


static struct dentry *redir2_lookup(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd)
{
	struct dentry *result;

	result = orig_lookup(dir, dentry, nd);
	if (!is_create(nd) && !result && !dentry->d_inode &&
	    is_avfs(dentry->d_name.name, dentry->d_name.len)) {
		printk("redir2_lookup: %.*s\n", dentry->d_name.len,
		       dentry->d_name.name);
		
		d_add(dentry, (struct inode *) 1);
		up(&dir->i_sem);
		do_mount("none", "none", "none", 0, NULL);
		down(&dir->i_sem);
			
	}
	return result;	
}

static int __init init_redir2(void)
{
	printk(KERN_INFO "redir2 init (version %s)\n", REDIR2_VERSION);
	
	orig_lookup = current->fs->root->d_inode->i_op->lookup;
	current->fs->root->d_inode->i_op->lookup = redir2_lookup;

	return 0;
}

static void __exit exit_redir2(void)
{
	printk(KERN_INFO "redir2 cleanup\n");

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
