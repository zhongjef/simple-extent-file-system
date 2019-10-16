#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

#include "a1fs.h"
#include "map.h"

// Pointer to the 0th byte of the disk
unsigned char *disk;
int ceil_divide(int x, int y) {	
	int result = x / y;
	if(x % y != 0){
		result += 1;
	}
	return result;	
}
void print_bitmap(unsigned char *bm, int size) {
    int i, j;
    for(i = 0; i < size; i++) {
        for (j = 0; j < 8; j++) {
            unsigned char mask = 1 << j;
            if (bm[i] & mask) {
                printf("1");
            }
            else {
                printf("0");
            }
        }
        printf(" ");
    }
    printf("\n");
}
int main(int argc, char **argv)
{

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
    if (fd == -1)
    {
        perror("open");
        exit(1);
    }

    // Map the disk image into memory so that we don't have to do any reads and writes
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    a1fs_superblock *sb = (a1fs_superblock *)(disk);

    printf("Super Block:\n");

    // in byte
    printf("Size: %d\n", (int)sb->size);
    	printf("    Inode count: %d\n", sb->s_inodes_count);
	printf("    Blocks count: %d\n", sb->s_blocks_count);
	printf("    Free blocks count: %d\n", sb->s_free_blocks_count);
	printf("    Free inodes count: %d\n", sb->s_free_inodes_count);
	printf("    block bitmap: %d\n", sb->bg_block_bitmap);
	printf("    block bitmap count: %d\n", sb->block_bitmap_count);
	printf("    inode bitmap: %d\n", sb->bg_inode_bitmap);
	printf("    inode bitmap count: %d\n", sb->inode_bitmap_count);
	printf("    inode table: %d\n", sb->bg_inode_table);
	printf("    inode table count: %d\n", sb->inode_table_count);
	printf("    data block: %d\n", sb->bg_data_block);
	printf("    data block count: %d\n", sb->data_block_count);
    // Print Inode Bitmap
    printf("Inode bitmap: ");
    unsigned char *inode_bitmap = (unsigned char *)(disk + sb->bg_inode_bitmap * A1FS_BLOCK_SIZE);
    print_bitmap(inode_bitmap, sb->s_inodes_count);
    printf("\n");

    // Print Block Bitmap
    printf("Block bitmap: ");
    unsigned char *block_bitmap = (unsigned char *)(disk + A1FS_BLOCK_SIZE);
    print_bitmap(block_bitmap, sb->data_block_count);
    
    printf("\n");

    // Print Inode
    void *inode_block = (void *)(disk + (sb->bg_inode_table)* A1FS_BLOCK_SIZE);
    a1fs_inode *inode;
    for (int bit = 0; bit < sb->s_inodes_count; bit++)
    {
        if((inode_bitmap[bit] & (1 << bit)) == 1){  // bit map is 1
            inode = (void *)(inode_block + bit* sizeof(a1fs_inode));
            // bitmap count starts form 0
            printf("Inode: Inode#: %d\n Number of Link: %d\n Extend Block: %d\n Mode: %c\n Dentry: %ld\n", bit, inode->links, inode->extentcount, inode->mode, inode->dentry_count);
        }
    }
    printf("\n");
	printf("d entry: ");
	a1fs_dentry *dentry = (a1fs_dentry *)(disk + (sb->bg_data_block + 1)* A1FS_BLOCK_SIZE);
	printf("d entry name: %s", dentry->name);
	printf("d endasdas");
    return 0;
}
