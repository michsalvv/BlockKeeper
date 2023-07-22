#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/version.h>

#include "blk_fs.h"

/**********************************************************
 * Standard File Operations
 **********************************************************/


struct dentry *fs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags){

    struct fs_inode *FS_specific_inode;
    struct inode *unique_file_inode = NULL;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh = NULL;
    
    // printk("%s: running the lookup inode-function for name %s",MOD_NAME,child_dentry->d_name.name);
    
    if(!strcmp(child_dentry->d_name.name, FILE_NAME)){
        unique_file_inode = iget_locked(sb, 1);     // iget_locked use sbread: read from cache
        // printk("%s: running the lookup inode-function for name %s",MOD_NAME,child_dentry->d_name.name);
        
        if (!unique_file_inode)
            return ERR_PTR(-ENOMEM);

        //already cached inode - simply return successfully
        if(!(unique_file_inode->i_state & I_NEW)){
            // printk("%s: unique_file_inode already cached", MOD_NAME);
            return child_dentry;
        }
        

	//this work is done if the inode was not already cached
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
        inode_init_owner(sb->s_user_ns, unique_file_inode, NULL, S_IFREG); //set the root user as owned of the FS root
#else
        inode_init_owner(the_inode, NULL, S_IFREG); 
#endif
        unique_file_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;
        unique_file_inode->i_fop = &fs_file_ops;
        unique_file_inode->i_op = &fs_inode_ops;

        set_nlink(unique_file_inode,1); //directly set an inode's link count

        // retrieve the file size via the FS specific inode, putting it into the generic inode
        bh = (struct buffer_head *)sb_bread(sb, FS_UNIQFILE_INODE_NUMBER);
        if(!bh){
            return ERR_PTR(-EIO);
        }

        FS_specific_inode = (struct fs_inode*) bh->b_data;
        unique_file_inode->i_size = FS_specific_inode->file_size;
        brelse(bh);

        d_add(child_dentry, unique_file_inode);

        /**
         * struct dentry * dget (struct dentry * dentry);
         * 
         * increment the reference count and return the dentry
         * A dentry will not be destroyed when it has reference
        */
        dget(child_dentry);

        /**
         * Called when the inode is fully initialised to clear the new state (I_NEW) of the inode 
         * and wake up anyone waiting for the inode to finish initialization.
         * 
         * unlock the inode to make it usable
        */
        unlock_new_inode(unique_file_inode);

        return child_dentry;
    }

    return NULL;
}

const struct inode_operations fs_inode_ops = {
    .lookup = fs_lookup
};