#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#include "common.h"


/*
	This makefs will write the following information onto the disk
	- BLOCK 0, superblock;
	- BLOCK 1, inode of the unique file (the inode for root is volatile);
	- BLOCK 2, ..., datablocks of the unique file 
*/
int fd;
int put_padding(int, int);
void write_datablock(int fd, int block_number, char *body);

int main(int argc, char *argv[])
{
	int ii, realIndex;
	struct stat stats;
	ssize_t ret, total_blocks;
	struct sb_info sb;
	struct fs_inode root_inode;
	struct fs_inode file_inode;
	struct fs_dir_record dir;
	
	char *file_body = "Testo del blocco ";//this is the default content of the unique file 
	char body [50];

	if (argc != 2) {
		printf("Usage: mkfs-singlefilefs <device>\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

	//pack the superblock
	sb.version = 1;		//file system version
	sb.magic = MAGIC;
	sb.block_size = DEFAULT_BLOCK_SIZE;

	ret = write(fd, (char *)&sb, sizeof(sb));

	if (ret != sizeof(sb)) {
		printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
		close(fd);
		return ret;
	}

	printf("Super block written succesfully\n");

	/**
	 * sb_bread di default legge blocchi da 4K, quindi se vogliamo leggere il primo blocco di dati ad esempio dobbiamo aggiungere del padding al sb
	*/
	put_padding(sizeof(sb), SB_BLOCK_NUMBER);

	// write file inode
	file_inode.mode = S_IFREG;			// Regular File Mode
	file_inode.inode_no = FS_UNIQFILE_INODE_NUMBER;

	fstat(fd, &stats);
	file_inode.file_size = stats.st_size;		

	printf("File size is %ld\n",file_inode.file_size);
	fflush(stdout);

	ret = write(fd, (char *)&file_inode, sizeof(file_inode));
	printf("Writed file node %ld bytes\n", ret);
	if (ret != sizeof(root_inode)) {
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}

	printf("File inode written succesfully.\n");
	
	//padding for block 1
	put_padding(sizeof(file_inode), FS_UNIQFILE_INODE_NUMBER);

	/**
	 * Dynamically obtain the maximum number of blocks that the file can contain (defined during compilation using the dd command). 
	 * This includes the superblock and inode.
	*/
	
	total_blocks = stats.st_size / DEFAULT_BLOCK_SIZE;	

	/**
	 * Initialization of  blocks's metadata
	 * La size del file è già stabilita a tempo di compilazione possiamo quindi già quindi assegnare un ID e il bit INVALID ad ogni blocco
	 * 
	 * Iterate from 2 because superblok and inode are the first blocks
	*/
	blk_metadata md;

	for (ii=0; ii< total_blocks - 2; ii++){

		if (ii==1 || ii == 4 || ii >= 7){

			sprintf(body, "%s %d\n", file_body, ii);
			write_datablock(fd, ii, body);
			put_padding(sizeof(struct blk_metadata) + strlen(body), ii);

		}else{
			// Write metadata
			md.valid = INVALID_BIT;
			md.order = 0;
			ret = write(fd, &md, sizeof(md));
			if (ret != sizeof(md)){
				printf("Metadata has not been written: [%ld] instead of [%ld]\n", ret, sizeof(md));
				close(fd);
				return -1;
			}

			// write empty datablock
			put_padding(sizeof(struct blk_metadata), ii);
			printf("Datablock n%d has been written succesfully.\n",ii);
		}
		
	}

	close(fd);
	return 0;
}

int put_padding (int written_bytes, int index_block){
	int nbytes, ret;
	nbytes = DEFAULT_BLOCK_SIZE - written_bytes;
	char *block_padding = malloc(nbytes);

	printf("Necessary padding for block %d: %d (4096 - %d)\n", index_block, nbytes, written_bytes);

	ret = write(fd, block_padding, nbytes);
	if (ret != nbytes) {
		printf("Writing padding for datablock n%d has failed. Written [%d] bytes instead of [%d]\n", index_block, ret, nbytes);
		close(fd);
		return -1;
	}
	printf("%d bytes of padding in the block n%d has been written sucessfully.\n", ret, index_block);
	return 0;
}

void write_datablock(int fd, int block_number, char *body) {
    blk_metadata md;
	int ret; 

    md.valid = VALID_BIT;
    md.data_len = strlen(body);
    md.order = block_number;

    ret = write(fd, &md, sizeof(md));
    if (ret != sizeof(md)) {
        printf("Metadata has not been written: [%d] instead of [%ld]\n", ret, sizeof(md));
        close(fd);
        return;
    }

    ret = write(fd, body, strlen(body));
    if (ret != strlen(body)) {
        printf("Writing file datablock n%d has failed. Written [%d] bytes instead of [%ld]\n", block_number, ret, strlen(body));
        close(fd);
        return;
    }
}