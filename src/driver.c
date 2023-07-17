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
    // Free reference
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

    if (strlen(temp_buf) != size || ret < 0){
        kfree(temp_buf);
        return -EIO;
    }

    // AUDIT
    //     printk(KERN_INFO "%s: [PUT] Called (text: %s, size: %ld)\n", MOD_NAME, temp_buf, size);

    // Implementation choice: this system call will add '\n' to the end of the user message
    temp_buf[size] = '\n';


    /**
     * Prendo il lock e trovo il blocco disponibile. 
     * Bisogna farlo in Critical Section altrimenti potrei scegliere lo stesso blocco di un altro chiamante e sovrascriverlo (o essere sovrascritto).
     * Possiamo markarlo direttamente ora come invalido poichè finche non verrà aggiunto alla RCU List, i reader non saranno a conoscenza che tale blocco 
     * è diventato valido
     * Non possiamo ottimizzare ulteriormente la dimensione della CS poichè questo è l'unico modo per mantere la RCU in ordine.
     * Se infatti spezzassimo la CS in due unità, nella prima viene individuato il free_block, mentre nella seconda viene soltanto aggiunto l'elemento RCU: in questo
     * scenario, se un thread B (spawnato dopo A) arriva prima di A ad aggiungere l'elemento alla lista RCU, pur avendo un numero d'ordine maggiore del blocco di A, 
     * verrebbe aggiunto prima nella lista RCU, non mantenendo dunque l'ordine di arrivo. Questo perchè l'RCU è stata implementata in questo modo. 
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
    rcu_i->dev_order = session.last_put_order;      // Mantenerlo in sessione è più veloce rispetto a dover scorrere nuovamente la lista fino a raggiungere la tail
    
    // Write on device
    memcpy(bh->b_data + sizeof(blk_metadata), temp_buf, strlen(temp_buf));

    // Last step: add to RCU list. Now the new data are visible by others
    list_add_tail_rcu(&rcu_i->node, &metadata->rcu_list);

    mutex_unlock(&session.mutex_w);

    mark_buffer_dirty(bh);
    if (session.wb_sync)
        sync_dirty_buffer(bh);

    // Wait for grace period ends
    synchronize_rcu();

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
            AUDIT printk(KERN_INFO "%s: [READ] No space in buffer for block #%d of %ld bytes. Space left: %ld bytes \n", MOD_NAME, rcu_i->id, rcu_i->data_len, (len -used_len));
            break;
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
     * KNOW ISSUE: effettuando invocazione della syscall read() consecutive, al giungimento della fine del file, verranno sempre letti 0 bytes.
     * Per riniziare la lettura è necessario aprire un nuovo file descriptor. 
     * E' necessario dunque gestire tale evento a livello applicativo. 
    */

    // Check if we have reached the end of the file
    if (&rcu_i->node == rcu_head){
        AUDIT printk(KERN_INFO "%s: [READ] No more block to read\n", MOD_NAME);
        *off = file_size;
    }
    rcu_read_unlock();            

    if (used_len == 0){
        kfree(temp_buf);
        return 0;
    }


    // Copy the data to the user space buffer
    ret = copy_to_user(buf,temp_buf, used_len);
    kfree(temp_buf);

    // Return the number of bytes read
    return used_len;
}


int dev_open (struct inode * inode, struct file * filp){
    uint64_t *delivered_order;

    // Check for permission
    if (filp->f_mode & FMODE_WRITE) {
      printk(KERN_ERR "%s: [OPEN] Cannot open file for write mode\n", MOD_NAME);
      goto fail;
    }

    delivered_order = (uint64_t *)kzalloc(sizeof(uint64_t), GFP_KERNEL);
    filp->private_data = delivered_order;

    // AUDIT printk(KERN_INFO "%s: [OPEN] open operation called\n", MOD_NAME);
    return 0;

fail:
    kfree(delivered_order);
    return -EPERM;
}

int dev_release (struct inode * inode, struct file *filp){
    kfree(filp->private_data);
    // AUDIT printk(KERN_INFO "%s: [RELEASE] release operation called\n", MOD_NAME);
    return 0;
}


const struct file_operations fs_file_ops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .open = dev_open,
    .release = dev_release
};