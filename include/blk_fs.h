#ifndef MAIN_H
#define MAIN_H

#include <linux/mutex.h>
#include "common.h"
	
typedef struct{
    unsigned int mounted;
    struct mutex mutex_w; 
	unsigned int wb_synch;
}session_info;

extern struct super_block *superblock;  		// Global device superblock
extern session_info session;					// Global session informations reference


struct fs_metadata{
	struct list_head rcu_list;								// Valid blocks RCU List
	uint8_t invalid_blocks[NUM_BLOCKS / BITS_PER_BYTE];		// One bit per block ( 1 = INVALID | 0 = VALID )
};

typedef struct {
	unsigned int id;
	size_t data_len;	// Usefull have this also in RCU 
	uint64_t dev_order;
	struct list_head node;
}rcu_item;

#endif