#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h>
#include <linux/rculist.h>
#include <linux/delay.h>    // For test

#include "blk_fs.h"
#include "../lib/include/scth.h"

unsigned long the_ni_syscall;
unsigned long new_syscall_array[] = {0x0, 0x0, 0x0};
#define HACKED_ENTRIES (int)(sizeof(new_syscall_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)]-1};



/**
 * int invalidate_data(int offset)
 * Used to invalidate data in a block at a given offset; 
 * invalidation means that data should logically disappear from the device; 
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
        return -ENODEV;
    }
    AUDIT printk("%s: [INV] on block %d\n", MOD_NAME, offset);

    if (!sb){
        printk("%s: sys_invalidate_data error retrieving superblock\n", MOD_NAME);
        return -EINVAL;
    }

    // Cerchiamo un blocco valido da invalidare, quindi scorriamo la lista RCU che contiene solo blocchi validi
    // WRITE_LOCK;
    printk("%s: [INV] Waiting for lock acquire\n", MOD_NAME);
    mutex_lock(&session.mutex_w);
    printk("%s: [INV] Lock acquired\n", MOD_NAME);

    rcu_read_lock();
    list_for_each_entry_rcu(curr, &(metadata->rcu_list), node){
        printk("%s: blk %d\n", MOD_NAME, curr->id);
        if (curr->id == offset){
            target_valid = 1;
            break;
        } 
    }

    if (!target_valid){
        AUDIT printk("%s: [INV] target not valid\n", MOD_NAME);
        rcu_read_unlock();
        mutex_unlock(&session.mutex_w);
        return -ENODATA;
    }

    rcu_read_unlock();

    metadata->invalid_blocks[curr->id] = INVALID_BIT;
    list_del_rcu(&(curr->node));    //TODO It delete while maintain timestmap order ??? Non penso proprio :(
    
    // Write back on device
    
    bh = sb_bread(sb, curr->id + 2);
    if (!bh){
        mutex_unlock(&session.mutex_w);
        return -1;
    }

    ((blk_metadata*)(bh->b_data))->valid = INVALID_BIT;
    mark_buffer_dirty(bh);
    printk("%s: [INV] Buffer marked as dirty\n", MOD_NAME);

    // WRITE_UNLOCK;
    mutex_unlock(&session.mutex_w);

    if(session.wb_synch) sync_dirty_buffer(bh);
    
    // Grace Period
    synchronize_rcu();
    // Free reference
    kfree(curr);
    brelse(bh);

    AUDIT printk("%s: [INV] deleted from RCU \n", MOD_NAME);
    // ragionare se la copia dei metadati nel device deve essere fatta nella CS
    // secondo me no, perchè tanto anche la dev_read è costruita leggendo dalla RCU, quindi se non è valido non lo legge il blocco

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size)
#else
asmlinkage int sys_put_data(char *source, size_t size)
#endif
{

    printk("%s: sys_put_data called with params %s and %ld\n", MOD_NAME, source, size);
    return 1;

}

/**
 * int get_data(int offset, char * destination, size_t size)
 * 
 * Used to read up to size bytes from the block at a given offset, if it currently keeps data; 
 * this system call should return the amount of bytes actually loaded into the destination area 
 * or zero if no data is currently kept by the device block;
 * this service should return the ENODATA error if no data is currently valid and associated with the offset parameter.
*/
//TODO Aggiungere qualche controllo in più
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
        return -ENODEV;
    }

    if (!sb){
        printk("%s: [GET] Error occured while retrieving superblock\n", MOD_NAME);
        return -EINVAL;
    }

    if (offset > NUM_BLOCKS){
        printk("%s: [GET] A block was requested whose id [%d] is outside the manageable block limit.\n", MOD_NAME, offset);
        return -EINVAL;
    }

    AUDIT printk("%s: [GET] on block %d\n", MOD_NAME, offset);

    rcu_read_lock();
    // AUDIT printk("%s: [GET] get lock. sleep started\n", MOD_NAME);
    // AUDIT printk("%s: [GET] Sleep finished\n", MOD_NAME);

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
            // printk("%s: [GET] rcu_i.id %d \n", MOD_NAME, rcu_i->id);
            // msleep(2*1000);
            rcu_read_unlock();            
            return (to_read - not_readed);
        }    
     }   
    // msleep(10*1000);
    // printk("%s: [GET] rcu_i.id %d \n", MOD_NAME, rcu_i->id);
    
    rcu_read_unlock();
    AUDIT printk("%s: [GET] block %d not valid\n", MOD_NAME, offset);
    return -ENODATA;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;       
long sys_get_data = (unsigned long) __x64_sys_get_data;
long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;  
#else
#endif


int install_syscalls( void* the_syscall_table){

    int ret,i;
    printk("%s: Installing %d Hacked Syscall Entries", MOD_NAME, HACKED_ENTRIES);
    printk("%s: [driver.c] Received sys_call_table address %px\n",MOD_NAME,the_syscall_table);

    new_syscall_array[0] = (unsigned long)sys_put_data;
    new_syscall_array[1] = (unsigned long)sys_get_data;
    new_syscall_array[2] = (unsigned long)sys_invalidate_data;

    ret = get_entries(restore,HACKED_ENTRIES,(unsigned long*)the_syscall_table,&the_ni_syscall);
    if (ret != HACKED_ENTRIES){
        printk("%s: could not hack %d entries (just %d)\n",MOD_NAME,HACKED_ENTRIES,ret); 
        return -1;      
    }

	unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_syscall_array[i];
    }

	protect_memory();

    printk("%s: all new system-calls correctly installed on sys-call table\n",MOD_NAME);
    return 0;
}

int uninstall_syscalls(void *the_syscall_table){
    int i;
    unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
	protect_memory();
    printk("%s: sys-call table restored to its original content\n",MOD_NAME);

    return 0;
}

/**
 * File Operations
*/

/**
 *  This operation is not synchronized, *off can be changed concurrently.
 *  Add synchronization if you need it for any reason
 * 
 * All'invocazione della read, vengono letti tutti i blocchi validi in ordine di timestamp (RCU Order), ma sempre nei limiti della len richiesta dal lettore.
 * In questo modo anche se avviene un'invalidazione di un blocco durante la lettura, proprio per il meccanismo RCU (synchronize_rcu()), l'elemento della RCU del
 * blocco invalidato non verrà distrutto prima della scadenza del grace period. 
*/
ssize_t dev_read (struct file * filp, char __user * buf, size_t len, loff_t * off){
    struct buffer_head *bh = NULL;
    struct inode * the_inode = filp->f_inode;

    loff_t file_size = the_inode->i_size ;

    uint64_t *delivered_order;
    ssize_t ret, used_len = 0;
    char *temp_buf;
    
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    struct list_head *rcu_head = &(((struct fs_metadata *) sb->s_fs_info)->rcu_list);
    rcu_item *rcu_i;
 

    temp_buf = (char*) kzalloc(len, GFP_KERNEL);
    if (!temp_buf){
        return -ENOMEM;
    }
    
    /**
     * Finchè ho spazio nel buffer leggo tutti i possibili blocchi validi. In questo modo allungo il grace period e riduco la gestione dell'invalidazione
     * dei blocchi in concorrenza.
     * Tuttavia, se l'operazione del VFS viene invocata tramite read() syscall manuali, è possibile che tra una read e l'altra avvenga una validazione. 
     * In questo caso quindi è stato ritenuto più consistente andare a consegnare all'utente solamente blocchi la cui intera dimensione entrasse nel buffer.
     * Mettere esempio:
     *      Testo del Blocco: 1
     *      Testo del Blocc
    */
    delivered_order = (uint64_t*) filp->private_data;

    AUDIT printk(KERN_DEBUG "%s: [READ] Called with (len = %ld, off = %lld) | Last Order %lld\n",MOD_NAME, len, *off, *delivered_order);

    rcu_read_lock();

    list_for_each_entry_rcu(rcu_i, rcu_head, node){
        int readable_b;

        if (used_len == len){
            break;
        }
        // Check if the block is in the correct delivery order
        if (rcu_i->dev_order <= *delivered_order){
            continue;
        }

        // Check if there is enough space in the buffer
        if (rcu_i->data_len > (len-used_len)){
            AUDIT printk(KERN_INFO "%s: [READ] No space in buffer for this block\n", MOD_NAME);
            goto err;
        }
        readable_b = rcu_i->data_len;

        // Read current block
        bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, rcu_i->id + 2);
        if(!bh){
            rcu_read_unlock();
            return -EIO;
        }

        // Skip metadata blocks
        strncpy(temp_buf + used_len, bh->b_data + sizeof(blk_metadata), readable_b);
        used_len += readable_b;
        brelse(bh);
        *delivered_order = rcu_i->dev_order;
    }

    /**
     * Questo controllo è necessario per permettere il corretto funzionamento del comando `cat`, il quale invoca successive chiamate a `dev_read()`
     * finchè l'offset non ha raggiunto la fine del file. 
     * Controlliamo se l'iterazione è tornata a puntare alla testa: In quel caso non ci saranno più blocchi validi da poter leggere.
     * Di conseguenza, l'output per il comando `cat` è pronto, dobbiamo quindi spostare l'offset fino alla fine del file,
     * in questo modo non ci sarà una successiva invocazione della dev_read(). 
     * Possiamo farlo perchè siamo sicuri di aver letto tutti i possibili blocchi validi durante la critical section RCU. 
     * 
     * KNOW ISSUE: effettuando invocazione della syscall read() successive, al giungimento esse non verranno più invocate poichè l'offset
     * corrisponderà alla dimensione del file. Per riniziare la lettura è necessario aprire un nuovo file descriptor. 
     * E' necessario dunque gestire tale evento a livello applicativo. 
    */

    // Check if we have reached the end of the file
    if (&rcu_i->node == rcu_head){
        AUDIT printk(KERN_INFO "%s: [READ] No more block to read\n", MOD_NAME);
        *off = file_size;
    }
    rcu_read_unlock();            

    // Copy the data to the user space buffer
    ret = copy_to_user(buf,temp_buf, used_len);
    kfree(temp_buf);

    // Return the number of bytes read
    return used_len;

err:
    rcu_read_unlock();
    kfree(temp_buf);
    return -ENODATA;
}


//TODO Se viene aperto per scrivere, chiudi. Non credo servano controlli sul mounted, non posso aprire un file se non è montato il suo FS, nemmeno lo vedo penso
int dev_open (struct inode * inode, struct file * filp){
    uint64_t *delivered_order;

    // Check for permission
    if (filp->f_mode & FMODE_WRITE) {
      printk(KERN_ERR "%s: [OPEN] Cannot open file for write mode\n", MOD_NAME);
      goto fail;
    }

    delivered_order = (uint64_t *)kzalloc(sizeof(uint64_t), GFP_KERNEL);
    filp->private_data = delivered_order;

    AUDIT printk(KERN_INFO "%s: [OPEN] open operation called\n", MOD_NAME);
    return 0;

fail:
    kfree(delivered_order);
    return -EPERM;
}

//Non credo servano controlli
int dev_release (struct inode * inode, struct file *filp){
    kfree(filp->private_data);
    AUDIT printk(KERN_INFO "%s: [RELEASE] release operation called\n", MOD_NAME);
    return 0;
}


const struct file_operations fs_file_ops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .open = dev_open,
    .release = dev_release
};