#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h>
#include <linux/rculist.h>

#include "blk_fs.h"
#include "../lib/include/scth.h"

unsigned long the_ni_syscall;
unsigned long new_syscall_array[] = {0x0, 0x0, 0x0};
#define HACKED_ENTRIES (int)(sizeof(new_syscall_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)]-1};

/**********************************************************
 * System Call Implementation
 **********************************************************/

/**
 * int invalidate_data(int offset)
 * Used to invalidate data in a block at a given offset; 
 * Invalidation means that data should logically disappear from the device; 
 * this service should return the ENODATA error if no data is currently valid and associated with the offset parameter.
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int , offset)
#else
asmlinkage int sys_invalidate_data(int offset)
#endif
{
    struct super_block *sb = superblock;
    struct fs_metadata *metadata = (struct fs_metadata *) sb->s_fs_info;

    struct buffer_head *bh;

    rcu_item *curr;
    unsigned int target_valid = 0;
    
    if(!session.mounted){
        printk(KERN_ERR "%s: [INV] Device not mounted\n", MOD_NAME);
        return -ENODEV;
    }

    if (!sb){
        printk(KERN_ERR "%s: [INV] error retrieving superblock\n", MOD_NAME);
        return -EIO;
    }

    // WRITE_LOCK;
    mutex_lock(&session.mutex_w);

    // Look for the target block to invalidate. Iterate over the RCU list of valid blocks
    rcu_read_lock();
    list_for_each_entry_rcu(curr, &(metadata->rcu_list), node){
        if (curr->id == offset){
            target_valid = 1;
            break;
        } 
    }

    if (!target_valid){
        AUDIT printk(KERN_INFO "%s: [INV] Target block [%d] is not valid\n", MOD_NAME, offset);
        rcu_read_unlock();
        mutex_unlock(&session.mutex_w);
        return -ENODATA;
    }

    rcu_read_unlock();

    markInvalid(&metadata->invalid_blocks, curr->id);
    list_del_rcu(&(curr->node)); 
    
    bh = sb_bread(sb, curr->id + 2);
    if (!bh){
        mutex_unlock(&session.mutex_w);
        return -EIO;
    }

    // Write back on device
    ((blk_metadata*)(bh->b_data))->valid = INVALID_BIT;
    mark_buffer_dirty(bh);

    // WRITE_UNLOCK;
    mutex_unlock(&session.mutex_w);

    if(session.wb_sync) sync_dirty_buffer(bh);
    
    // Wait for grace period ends
    synchronize_rcu();
    // Free reference of target RCU item
    kfree(curr);
    brelse(bh);

    AUDIT printk(KERN_INFO "%s: [INV] Block %d succesfully invalidate\n", MOD_NAME, offset);

    return 0;
}

/**
 * int put_data(char * source, size_t size) 
 * Used to put into one free block of the block-device size bytes of the user-space data identified by the source pointer, 
 * this operation must be executed all or nothing; 
 * the system call returns an integer representing the offset of the device (the block index) where data have been put; 
 * if there is currently no room available on the device, the service should simply return the ENOMEM error;
*/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{   
    struct super_block *sb = superblock;
    struct fs_metadata *metadata = (struct fs_metadata *) sb->s_fs_info;
    struct buffer_head *bh;
    rcu_item *rcu_i;

    char *temp_buf;
    int ret, free_block = -1;
    bool block_available = false;

    if(!session.mounted){
        printk(KERN_ERR "%s: [PUT] Device not mounted\n", MOD_NAME);
        return -ENODEV;
    }

    if (!sb){
        printk(KERN_ERR "%s: [PUT] Error retrieving superblock\n", MOD_NAME);
        return -EIO;
    }

    // Cannot copy on device messages bigger than (4096 - sizeof(blk_metadata)) bytes. We reserve one byte for '\n' char. 
    if (size >= MAX_MSG_SIZE) return -EFBIG;

    temp_buf = (char*) kzalloc(size+1, GFP_KERNEL);
    rcu_i = kzalloc(sizeof(rcu_item), GFP_KERNEL); 

    if (!temp_buf || !rcu_i){
        return -EIO;
    }

    // User message
    ret = copy_from_user(temp_buf, source, size);

    // Double check on consistency of message length and input size value
    if (strlen(temp_buf) != size || ret < 0){
        kfree(temp_buf);
        return -EIO;
    }

    // AUDIT
    //     printk(KERN_INFO "%s: [PUT] Called (text: %s, size: %ld)\n", MOD_NAME, temp_buf, size);

    // Implementation choice: this system call will add '\n' to the end of the user message
    temp_buf[size] = '\n';


    /**
     * Acquire the write lock and we identify the first available block.
     * It is not possible to optimize the size of the CS: for further details refer to the documentation.
     */
    mutex_lock(&session.mutex_w);
    for (free_block = 0; free_block < NUM_BLOCKS; free_block++){
        if (isInvalid(&metadata->invalid_blocks, free_block)){
            clearInvalid(&metadata->invalid_blocks, free_block);
            block_available = true;
            break;
        }
    }

    if (!block_available){
        printk(KERN_INFO "%s: [PUT] No space for the new user message\n", MOD_NAME);
        goto NO_MEM;
    }
    AUDIT
        printk(KERN_INFO "%s: [PUT] Target invalid block chosen: [%d]\n", MOD_NAME, free_block);

        
    bh = sb_bread(sb, free_block + 2);
    if (!bh){
        goto REVERT;
    }

    ((blk_metadata*)(bh->b_data))->valid = VALID_BIT;
    ((blk_metadata*)(bh->b_data))->data_len = strlen(temp_buf);
    ((blk_metadata*)(bh->b_data))->order = session.last_put_order++;
    rcu_i->id = free_block;
    rcu_i->data_len = strlen(temp_buf);
    
    rcu_i->dev_order = session.last_put_order;      // // Keeping this metadata in session is faster than retrieving it from the RCU tail
    
    // Write on device
    memcpy(bh->b_data + sizeof(blk_metadata), temp_buf, strlen(temp_buf));
    mark_buffer_dirty(bh);

    // Last step: add to RCU list. Now the new data are visible by others
    list_add_tail_rcu(&rcu_i->node, &metadata->rcu_list);

    mutex_unlock(&session.mutex_w);

    if (session.wb_sync)
        sync_dirty_buffer(bh);

    kfree(temp_buf);
    return free_block;

REVERT:
    kfree(rcu_i);
    kfree(temp_buf);
    clearInvalid(&metadata->invalid_blocks, free_block);
    mutex_unlock(&session.mutex_w);
    return -EIO;

NO_MEM:
    mutex_unlock(&session.mutex_w);
    kfree(rcu_i);
    kfree(temp_buf);
    return -ENOMEM;
}

/**
 * int get_data(int offset, char * destination, size_t size)
 * 
 * Used to read up to size bytes from the block at a given offset, if it currently keeps data; 
 * this system call should return the amount of bytes actually loaded into the destination area 
 * or zero if no data is currently kept by the device block;
 * this service should return the ENODATA error if no data is currently valid and associated with the offset parameter.
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char*, destination, size_t, size)
#else
asmlinkage int sys_get_data(int offset, char* destination, size_t size)
#endif
{
    struct super_block *sb = superblock;
    struct fs_metadata *metadata = (struct fs_metadata *) sb->s_fs_info;
    rcu_item *rcu_i;
    struct buffer_head *bh;
    size_t to_read, not_readed;

    if(!session.mounted){
        printk(KERN_ERR "%s: [GET] Device not mounted\n", MOD_NAME);
        return -ENODEV;
    }

    if (!sb){
        printk(KERN_ERR "%s: [GET] Error occured while retrieving superblock\n", MOD_NAME);
        return -EIO;
    }

    if (offset > NUM_BLOCKS){
        printk(KERN_ERR "%s: [GET] A block was requested whose id [%d] is outside the manageable block limit.\n", MOD_NAME, offset);
        return -EINVAL;
    }

    AUDIT printk(KERN_INFO "%s: [GET] Requested block %d\n", MOD_NAME, offset);

    rcu_read_lock();

    list_for_each_entry_rcu(rcu_i, &(metadata->rcu_list), node){
        
        if (rcu_i->id == offset){
            bh = sb_bread(sb, offset +2);                           // +2: blocks of metadata

            if(!bh){
                rcu_read_unlock();
                return -EIO;
            }

            if (size > rcu_i->data_len){
                to_read = rcu_i->data_len;
            }else to_read = size;

            not_readed = copy_to_user(destination, bh->b_data + sizeof(blk_metadata), to_read);
            brelse(bh);
            rcu_read_unlock();            
            return (to_read - not_readed);
        }    
     }   
    
    rcu_read_unlock();
    AUDIT printk(KERN_INFO "%s: [GET] Block %d is not valid\n", MOD_NAME, offset);
    return -ENODATA;

}

/**********************************************************
 * Hacked System Call Installation
 **********************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;       
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;  
#else
#endif


int install_syscalls( void* the_syscall_table){

    int ret,i;

    new_syscall_array[0] = (unsigned long)sys_put_data;
    new_syscall_array[1] = (unsigned long)sys_get_data;
    new_syscall_array[2] = (unsigned long)sys_invalidate_data;

    ret = get_entries(restore,HACKED_ENTRIES,(unsigned long*)the_syscall_table,&the_ni_syscall);
    if (ret != HACKED_ENTRIES){
        printk(KERN_ERR "%s: Could not hack %d entries (just %d)\n",MOD_NAME,HACKED_ENTRIES,ret); 
        return -1;      
    }

	unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_syscall_array[i];
    }

	protect_memory();
    printk("%s: [%d] Hacked Syscall Entries successfully installed in Sys-call Table at address %p", MOD_NAME, HACKED_ENTRIES, the_syscall_table);

    return 0;
}

int uninstall_syscalls(void *the_syscall_table){
    int i;
    unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
	protect_memory();
    printk("%s: Sys-Call Table restored to its original content\n",MOD_NAME);

    return 0;
}

/**********************************************************
 * File Operations Implementation
 **********************************************************/


/**
 * VFS Read implementation: 
 * All valid blocks are read in delivery order (RCU Order), but always within the limits of the length requested by the reader.
 * 
 * KNOWN ISSUE: When making consecutive calls to the `read()` syscall and reaching the end of the file, 0 bytes will always be read.
 * To resume reading, it is necessary to open a new file descriptor.
 * Therefore, it is necessary to handle this event at the application level.
*/
ssize_t dev_read (struct file * filp, char __user * buf, size_t len, loff_t * off){
    struct buffer_head *bh = NULL;
    struct inode * the_inode = filp->f_inode;

    loff_t file_size = the_inode->i_size ;
    loff_t block_offset;
    uint64_t *delivered_order;
    ssize_t ret, readed_bytes = 0;
    char *temp_buf;
    
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    struct list_head *rcu_head = &(((struct fs_metadata *) sb->s_fs_info)->rcu_list);
    rcu_item *rcu_i, *next;
    
    temp_buf = (char*) kzalloc(len, GFP_KERNEL);
    if (!temp_buf){
        return -ENOMEM;
    }
    
    // Keep a reference to the last order number read in the previous invocation (in case the file descriptor hasn't been restored)
    delivered_order = (uint64_t*) filp->private_data;

    AUDIT printk(KERN_DEBUG "%s: [READ] len = %ld, off = %lld , delivery_order %lld\n",MOD_NAME, len, *off, *delivered_order);

    rcu_read_lock();

    list_for_each_entry_rcu(rcu_i, rcu_head, node){
        int readable_bytes = 0;

        if (len <= 0)
            break;

        // Check if the block is in the correct delivery order
        if (rcu_i->dev_order < *delivered_order){
            AUDIT printk(KERN_INFO "%s: [READ] Block skipped, already readed.\n", MOD_NAME);
            continue;
        }

        if ((*delivered_order != 0) && (rcu_i->dev_order != *delivered_order)){
            rcu_read_unlock();
            return -ESPIPE;
        }

        block_offset = *off;

        // The block can be readed completely
        if ((rcu_i->data_len - block_offset) <= len){
            readable_bytes = (rcu_i->data_len - block_offset);
            AUDIT printk(KERN_INFO "%s: [READ] Block can be readed completely [%d bytes]\n", MOD_NAME, readable_bytes);

            *off = 0;
            next = (rcu_item*) list_next_or_null_rcu(rcu_head, &(rcu_i->node), rcu_item, node);

            if (next != NULL)
                *delivered_order = next->dev_order;
            else *delivered_order = (*delivered_order)+1;
        }
        // The block can be readed partially
        else{
            readable_bytes = len;
            AUDIT printk(KERN_INFO "%s: [READ] Block can be readed partially [%d bytes]\n", MOD_NAME, readable_bytes);
            *off += readable_bytes;

            // We still have to read this block
            *delivered_order = rcu_i->dev_order;          
        }

        len -= readable_bytes;



        // Read current block
        bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, rcu_i->id + 2);
        if(!bh){
            rcu_read_unlock();
            return -EIO;
        }

        // Skip metadata (+ sizeof(blk_metadata))
        strncpy(temp_buf + readed_bytes, bh->b_data + sizeof(blk_metadata) + block_offset, readable_bytes);
        brelse(bh);

        readed_bytes += readable_bytes;

    }

/**
 * Check if we have reached the end of the file (valid blocks only).
 * 
 * This check is necessary to ensure the correct functioning of the `cat` command, which makes
 * successive calls to `dev_read()` until the offset reaches the end of the file.
 * We check if the iteration has returned to the head of the list: in that case, there are no more valid blocks to read.
 * Therefore, the output for the `cat` command is ready, and we need to move the offset to the end of the file
 * to avoid further invocations of `dev_read()`.
 * We can do this because we are sure that we have read all the possible valid blocks during the RCU critical section.
 * 
 * NOTE: No longer necessary in this release.
 */
    // if (&rcu_i->node == rcu_head){
    //     AUDIT printk(KERN_INFO "%s: [READ] No more block to read\n", MOD_NAME);
    //     *off = file_size;
    // }
    rcu_read_unlock();            

    if (readed_bytes == 0){
        kfree(temp_buf);
        return readed_bytes;
    }


    // Copy the data to the user space buffer
    ret = copy_to_user(buf,temp_buf, readed_bytes);
    kfree(temp_buf);

    // Return the number of bytes totally read
    return readed_bytes;
}


int dev_open (struct inode * inode, struct file * filp){
    uint64_t *delivered_order;
    delivered_order = (uint64_t *)kzalloc(sizeof(uint64_t), GFP_KERNEL);
    *delivered_order = 0;

      printk(KERN_INFO "%s: [OPEN]\n", MOD_NAME);
    // Check for permission
    if (filp->f_mode & FMODE_WRITE) {
      printk(KERN_ERR "%s: [OPEN] Cannot open file for write mode\n", MOD_NAME);
      goto fail;
    }

    filp->private_data = delivered_order;

    return 0;

fail:
    kfree(delivered_order);
    return -EPERM;
}

int dev_release (struct inode * inode, struct file *filp){
    printk(KERN_INFO "%s: [RELEASE]\n", MOD_NAME);

    kfree(filp->private_data);
    return 0;
}


const struct file_operations fs_file_ops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .open = dev_open,
    .release = dev_release
};