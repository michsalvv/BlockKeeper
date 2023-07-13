#ifndef COMMON_H
#define COMMON_H

#include <linux/types.h>
// #include <linux/ktime.h>

#define MOD_NAME "BlockKeeper"
#define FS_NAME "blockkeeper_fs"
#define FILE_NAME "the-file"

#ifdef DEBUG
#include <printk.h>
#define dprint(...) (printk(__VA_ARGS__))
#else
#define dprint(...)
#endif

#define AUDIT if(1)

#define MAGIC 0x6D696368
#define DEFAULT_BLOCK_SIZE 4096

#ifndef NUM_BLOCKS
	#define NUM_BLOCKS 100
#endif

#define SB_BLOCK_NUMBER 0
#define FILENAME_MAXLEN 255

#define FS_ROOT_INODE_NUMBER 10
#define FS_UNIQFILE_INODE_NUMBER 1

#define INVALID_BIT 1
#define VALID_BIT 0

/**
 * Definition of necessary structures both for the kernel module and for the formatting of the device
 * - Inode definition
 * - Superblock definition
 * - Dir definition
 * - Block Metadata definition
*/

struct fs_inode {
	mode_t mode;
	uint64_t inode_no;
	uint64_t data_block_number; //not exploited is equal to file_size / default_block_size

	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

struct sb_info{
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
};

struct fs_dir_record {
	char filename[FILENAME_MAXLEN];
	uint64_t inode_no;
};

typedef struct __attribute__((packed)) blk_metadata{
	char valid :1;
	uint64_t order;
	uint16_t data_len;	// Aggiusta il tipo di dato a seconda di quanti byte di data è possibile mettere
}blk_metadata;

// Operations structs
extern const struct inode_operations fs_inode_ops;
extern const struct file_operations fs_file_ops; 

extern const struct file_operations fs_dir_ops;

#endif