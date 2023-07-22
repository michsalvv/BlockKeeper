#include "blk_fs.h"
#include <linux/rculist.h>
#include <linux/slab.h>

/**********************************************************
 * InvalidBlockSet API and RCU sort function
 **********************************************************/

void markInvalid(InvalidBlockSet* set, int blockId) {
    set->blocks[blockId / BITS_PER_BYTE] |= (1 << (blockId % BITS_PER_BYTE));
}

void clearInvalid(InvalidBlockSet* set, int blockId) {
    set->blocks[blockId / BITS_PER_BYTE] &= ~(1 << (blockId % BITS_PER_BYTE));
}

void initializeInvalidBlockSet(InvalidBlockSet* set) {
    memset(set->blocks, 0, sizeof(set->blocks));
}

bool isInvalid(InvalidBlockSet* set, int blockId) {
    return (set->blocks[blockId / BITS_PER_BYTE] & (1 << (blockId % BITS_PER_BYTE))) != 0;
}

int compare_items(rcu_item *a, rcu_item *b) {
    return (a->dev_order > b->dev_order) - (a->dev_order < b->dev_order);
}

void list_sort(struct list_head *old_head, struct list_head *sorted_list){
    rcu_item *item, *temp;
    struct list_head *pos;
    INIT_LIST_HEAD(sorted_list);

    list_for_each_entry_safe(item, temp, old_head, node){
        list_del_rcu(&item->node);
        pos = NULL;
        list_for_each(pos, sorted_list){
            rcu_item *sorted_item = list_entry(pos, rcu_item, node);

            if (compare_items(item, sorted_item) < 0) {
                list_add_rcu(&item->node, pos->prev);
                break;
            }
        }
        if (pos == sorted_list){
            list_add_tail_rcu(&item->node, sorted_list);
        }
    }
}

void dump_list(struct list_head *head){
    rcu_item *rcu_i;
    printk("%s: ---- DUMP RCU LIST ----\n", MOD_NAME);

    list_for_each_entry_rcu(rcu_i, head, node){
        printk("%s: RCU Element [ID: %d| ORDER: %lld]\n", MOD_NAME, rcu_i->id, rcu_i->dev_order);
    }

}

void free_rcu_list(struct list_head *head) {
    rcu_item *item, *temp;
    list_for_each_entry_safe(item, temp, head, node) {
        list_del_rcu(&item->node);
        kfree(item);
    }
}
