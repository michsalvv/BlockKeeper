#ifndef MAIN_H
#define MAIN_H

#include <linux/mutex.h>
#include "common.h"
	
typedef struct{
    unsigned int mounted;
    struct mutex mutex_w; 
}session_info;

extern struct super_block *superblock;  		// Global device superblock
extern session_info session;					// Global session informations reference



struct fs_metadata{
	struct list_head rcu_list;							// Valid Blocks
	uint8_t invalid_blocks[NUM_BLOCKS / BITS_PER_BYTE];		// One bit per block. 1 = INVALID | 0 = VALID
};

typedef struct {
	char valid :1;	// TODO non serve, togliere
	unsigned int id; // Can handle only 256 blocks, maybe we need more
	size_t data_len;	//TODO forse basta mantenerla solamente in memoria, ragionare se è conveniente tenerla qui per velocità
	uint64_t timestamp;
	struct list_head node;
}rcu_item;

#endif