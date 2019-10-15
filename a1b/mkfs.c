/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs formatting tool.
 */ 

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "a1fs.h"
#include "map.h"

/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Sync memory-mapped image file contents to disk. */
	bool sync;
	/** Verbose output. If false, the program must only print errors. */
	bool verbose;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

int ceil_divide(int x, int y) {	
	int result = x / y;
	if(x % y != 0){
		result += 1;
	}
	return result;	
}

// Set the i-th index of the bitmap to 1
void setBitOn(uint32_t *A, uint32_t i) {
	int int_bits = sizeof(uint32_t) * 8;
	A[i/int_bits] |= 1 << (i%int_bits);
}

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -s      sync image file contents to disk\n\
    -v      verbose output\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfsvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help    = true; return true;// skip other arguments
			case 'f': opts->force   = true; break;
			case 's': opts->sync    = true; break;
			case 'v': opts->verbose = true; break;
			case 'z': opts->zero    = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO
	//If super block is set up, the image has already been formatted into a1fs.
	struct a1fs_superblock *sb = (struct a1fs_superblock *)(image);
	if (sb->magic == A1FS_MAGIC){return true;}
	return false;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//TODO
	int num_block = size / A1FS_BLOCK_SIZE;
	int num_inode_bm = ceil_divide(opts->n_inodes, BITS_PER_BLOCK);
	int num_data_bm = ceil_divide(num_block, BITS_PER_BLOCK);
	int num_inode_t = ceil_divide(opts->n_inodes * sizeof(a1fs_inode), A1FS_BLOCK_SIZE);
	int used_blocks = num_inode_t + num_data_bm + num_inode_bm + 1;
	if (used_blocks > num_block || opts->n_inodes < 1) {
		return false;}
	a1fs_superblock * sb = (struct a1fs_superblock *)(image);
	sb->magic = A1FS_MAGIC;
	sb->size = size;
	sb->s_inodes_count = opts->n_inodes;
	sb->s_blocks_count = num_block;
	sb->s_free_blocks_count = num_block - 1 - num_inode_bm - num_data_bm - num_inode_t;
	sb->s_free_inodes_count = opts->n_inodes - 1;
	sb->bg_block_bitmap = (a1fs_blk_t) (1);
	sb->block_bitmap_count = num_data_bm;
	sb->bg_inode_bitmap = (a1fs_blk_t) (1 + num_data_bm);
	sb->inode_bitmap_count = num_inode_bm;
	sb->bg_inode_table = (a1fs_blk_t) (1 + num_data_bm + num_inode_bm);
	sb->inode_table_count = num_inode_t;
	sb->bg_data_block = (a1fs_blk_t) (1 + num_data_bm + num_inode_bm + num_inode_t);
	sb->data_block_count = 0;
	int j,i;
	int num_int_bits = sizeof(int) * 8;
	// data block bitmap
	for (j = 0; j < num_data_bm; j++) {
		a1fs_blk_t *data_bits = (a1fs_blk_t *) (image + A1FS_BLOCK_SIZE * (j + 1));
		// Just fill the entire table with 0 bits, who cares overkill lol
		for (i = 0; i < BITS_PER_BLOCK; i+=num_int_bits)
			*(data_bits + i) = 0; // int 0 = 32 zero bits
	}
	// inode bitmap
	for (j = 0; j < num_inode_bm; j++) {
		a1fs_blk_t *inode_bits = (a1fs_blk_t *) (image + A1FS_BLOCK_SIZE * (j + 1 + num_data_bm));
		for (i = 0; i < BITS_PER_BLOCK; i += num_int_bits)
			*(inode_bits + i) = 0;
	}

	// change root inode to '1'
	a1fs_blk_t *inode_bits = (a1fs_blk_t *) (image + A1FS_BLOCK_SIZE * (sb->bg_inode_bitmap));
	setBitOn(inode_bits, 0);
	sb->s_free_inodes_count--;
	a1fs_inode * root_inode = (a1fs_inode *) (image + A1FS_BLOCK_SIZE * (sb->bg_inode_table));
	root_inode->mode = 'd';
	root_inode->links = 0;
	root_inode->size = 0;
	clock_gettime(CLOCK_REALTIME, &(root_inode->mtime));
	root_inode->dentry_count = 0;
	// Allocate root inode's extent block
	a1fs_blk_t *data_bits = (a1fs_blk_t *) (image + A1FS_BLOCK_SIZE * (sb->bg_block_bitmap));
	setBitOn(data_bits, 0);
	sb->s_free_blocks_count--;
	root_inode->extentblock = sb->bg_data_block;
	a1fs_extent *extentblock = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*root_inode->extentblock);
	a1fs_extent *curr_extent = extentblock;
	
	// Allocate directory entries for testing purposes
	// TODO: Root dir should be empty first
	setBitOn(data_bits, 1);
	sb->s_free_blocks_count--;
	curr_extent->start = sb->bg_data_block + 1;
	curr_extent->count = 1;
	a1fs_dentry *curr_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE*curr_extent->start);
	root_inode->dentry_count += 2;
	curr_dir->ino = 0;
	strncat(curr_dir->name, "/", sizeof(curr_dir->name) - strlen("/") - 1);
	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	// Sync to disk if requested
	if (opts.sync && (msync(image, size, MS_SYNC) < 0)) {
		perror("msync");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
