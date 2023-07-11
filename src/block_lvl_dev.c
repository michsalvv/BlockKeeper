#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/slab.h>

#include "blk_fs.h"
#include "driver.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michele Salvatori");
MODULE_DESCRIPTION("BlockKeeper: Block-Level Data Management Service");


unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);

LIST_HEAD(rcu_list);  
struct super_block *superblock;

session_info session = {
            .mounted = 0,
#ifdef WB_DAEMON    
            .wb_synch = 1
#endif
        };

static struct super_operations fs_super_ops = {};
static struct dentry_operations fs_dentry_ops = {};

//TODO Liberare risorse se occorre un errore?
int bkeeper_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct sb_info *sb_disk;
    struct timespec64 curr_time;
    struct fs_metadata *fs_md;
    struct blk_metadata *temp_md;
    uint64_t magic;

    size_t ii, init_blks;   
    struct fs_inode* unique_inode;
    rcu_item *rcu_i;
    


    sb->s_magic = MAGIC;
    bh = sb_bread(sb, SB_BLOCK_NUMBER);

    if(!sb){
	    return -EIO;
    }

    sb_disk = (struct sb_info *)(bh->b_data);
    magic = sb_disk->magic;
    brelse(bh);

    if(magic != sb->s_magic){
        printk("%s: Error! Magic number differs: %lld != %ld", MOD_NAME, magic, sb->s_magic);
	    return -EBADF;
    }


    /* Check over manageable block size */
    if (sb_disk->block_size != DEFAULT_BLOCK_SIZE){
        printk("%s: [FAILED] The driver can handle block of size %d but the device has a default block size of %lld\n", MOD_NAME, DEFAULT_BLOCK_SIZE, sb_disk->block_size);
        return -EIO;
    } 

    /* Manteniamo nelle info del superblocco un riferimento alla RCU list e l'array di free nodes  */
    fs_md = (struct fs_metadata*) kzalloc(sizeof(struct fs_metadata), GFP_ATOMIC);
    sb->s_fs_info = (void*) fs_md; 
    sb->s_op = &fs_super_ops;

    root_inode = iget_locked(sb, 0);
    if (!root_inode){
        return -ENOMEM;
    }

    root_inode->i_ino = FS_ROOT_INODE_NUMBER;

    //set the root user as owned of the FS root
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    inode_init_owner(sb->s_user_ns, root_inode, NULL, S_IFDIR); 
#else
    inode_init_owner(root_inode, NULL, S_IFDIR); //set the root user as owned of the FS root
#endif
    root_inode->i_sb = sb;

    root_inode->i_op = &fs_inode_ops;//set our inode operations
    root_inode->i_fop = &fs_dir_ops; //set our file operations

    //update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;
    
    sb->s_root->d_op = &fs_dentry_ops;
    
    unlock_new_inode(root_inode);   //unlock the inode to make it usable

    bh = sb_bread(sb, FS_UNIQFILE_INODE_NUMBER);
    if (!bh){
        return -EIO;
    }
    
    unique_inode = (struct fs_inode*) bh->b_data;
    brelse(bh);

    /* Reference to global superblock */
    superblock = sb; 


    init_blks = unique_inode->file_size / DEFAULT_BLOCK_SIZE;

    /* Check over the actual number of blocks that has been allocated and the number of blocks that the driver can handle */
    if (init_blks > NUM_BLOCKS ){
        printk("%s: [FAILED] Device has [%ld] blocks. The driver can handle max [%d] blocks\n", MOD_NAME, init_blks, NUM_BLOCKS);
        return -EINVAL;
    }


    /* Initialize block state array and RCU list for valid blocks */
    memset(fs_md->invalid_blocks, 0, sizeof(fs_md->invalid_blocks));
    INIT_LIST_HEAD_RCU(&(fs_md->rcu_list));

    /* Initialize writers mutexes */ 
    mutex_init(&session.mutex_w);

    // Iteriamo da due perchè non teniamo nella RCU il superblocco e l'inode
    for (ii=2; ii<init_blks; ii++){
        // Leggi i metadati dei blocchi e se è valido lo metti nella RCU
        bh = sb_bread(sb, ii);
        if (!bh){
            return -EINVAL;
        }

        temp_md = (struct blk_metadata*)bh->b_data;
        brelse(bh);

        if (temp_md->valid == VALID_BIT){
            printk("%s: block %ld is valid\n", MOD_NAME, ii);
            fs_md->invalid_blocks[ii-2] = VALID_BIT;
            rcu_i = kzalloc(sizeof(rcu_item), GFP_KERNEL);   //TODO KERNEL o GFP_ATOMIC? Guarda appunti
            if (!rcu_i){
                return -ENOMEM;
            }

            rcu_i->id = ii-2;                               // Block ID starting from 0
            rcu_i->valid = VALID_BIT;
            rcu_i->data_len = temp_md->data_len;
            rcu_i->dev_order = temp_md->order; 
            
            // No need of writing synch. The FS cannot be mounted twice.
            list_add_tail_rcu(&(rcu_i->node), &(fs_md->rcu_list));
            continue;
        }

            printk("%s: block %ld is not valid\n", MOD_NAME, ii);
        fs_md->invalid_blocks[ii-2] = INVALID_BIT;
    }
    return 0;
}

static void bkeeper_kill_superblock(struct super_block *s) {
    kill_block_super(s);

    if (!session.mounted) { 
        printk("%s: umount procedure fail\n", MOD_NAME);
        return;
    }
    session.mounted = 0;
    //TODO Free resources
    printk("%s: %s unmount successful.\n",MOD_NAME, FS_NAME);
    return;
}

struct dentry *bkeeper_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;

    if (session.mounted){
        printk("%s: %s already mounted. Can be mounted only once\n", MOD_NAME, FS_NAME);
        return ERR_PTR(-EEXIST);    
    }
    session.mounted = 1;

    ret = mount_bdev(fs_type, flags, dev_name, data, bkeeper_fill_super);
    if (unlikely(IS_ERR(ret))){
        printk("%s: [FAILED] error mounting %s",MOD_NAME, FS_NAME);
        session.mounted = 0;
    }
    else
        printk("%s: %s is succesfully mounted on from device %s\n",MOD_NAME, FS_NAME, dev_name);

    return ret;
}


//file system structure
static struct file_system_type bkeeper_fs = {
	    .owner = THIS_MODULE,
        .name           = FS_NAME,
        .mount          = bkeeper_mount,
        .kill_sb        = bkeeper_kill_superblock,
};


//TODO check on the_usctm params
int init_module(void) {

    int ret;
	AUDIT printk("%s: Received sys_call_table address %px\n",MOD_NAME,(void*)the_syscall_table);

    ret = register_filesystem(&bkeeper_fs);

    if (likely(ret == 0)){
        printk("%s: sucessfully registered file system driver\n",MOD_NAME);
        install_syscalls((void*)the_syscall_table);
    }
    else
        printk("%s: failed to unregister singlefilefs driver - error %d", MOD_NAME, ret);

    return ret;
}

void cleanup_module(void) {

    int ret;
    printk("%s: shutting down\n",MOD_NAME);

    //unregister filesystem
    ret = unregister_filesystem(&bkeeper_fs);

    uninstall_syscalls((void*)the_syscall_table);

    if (likely(ret == 0))
        printk("%s: sucessfully unregistered file system driver\n",MOD_NAME);
    else
        printk("%s: failed to unregister %s driver - error %d", MOD_NAME, FS_NAME, ret);
        
}

