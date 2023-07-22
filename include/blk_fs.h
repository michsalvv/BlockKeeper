#ifndef MAIN_H
#define MAIN_H

#include <linux/mutex.h>
#include "common.h"
	
typedef struct{
    unsigned int mounted;
    struct mutex mutex_w; 
	unsigned int wb_sync;
	uint64_t last_put_order;
}session_info;

extern struct super_block *superblock;  					// Global device superblock
extern session_info session;								// Global session informations reference

typedef struct InvalidBlockSet {
    uint8_t blocks[NUM_BLOCKS / BITS_PER_BYTE];	
} InvalidBlockSet;

struct fs_metadata{
	struct list_head rcu_list;								// Valid blocks RCU List
	InvalidBlockSet invalid_blocks;					
};

typedef struct {
	unsigned int id;
	size_t data_len;										// Usefull have this also in RCU 
	uint64_t dev_order;
	struct list_head node;
}rcu_item;


extern void markInvalid(InvalidBlockSet* set, int blockId);
extern void clearInvalid(InvalidBlockSet* set, int blockId);
extern void initializeInvalidBlockSet(InvalidBlockSet* set);
extern bool isInvalid(InvalidBlockSet* set, int blockId);

extern void list_sort(struct list_head *, struct list_head *);
extern void dump_list(struct list_head *head);
extern void free_rcu_list(struct list_head *);

#endif