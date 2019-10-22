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
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".



/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help or version
	if (opts->help || opts->version) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size, opts);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		if (fs->opts->sync && (msync(fs->image, fs->size, MS_SYNC) < 0)) {
			perror("msync");
		}
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}

uint32_t ceil_divide(uint32_t x, uint32_t y) {	
	uint32_t result = x / y;
	if(x % y != 0){
		result += 1;
	}
	return result;	
}


// Turn on the i-th bit in bitmap
void setBitOn(uint32_t *bm, uint32_t i) {
	uint32_t int_bits = sizeof(uint32_t) * 8;
	bm[i/int_bits] |= 1 << (i%int_bits);
}

// Turn off the i-th bit in bitmap
void setBitOff(uint32_t *bm, uint32_t i) {
	uint32_t int_bits = sizeof(uint32_t) * 8;
	bm[i/int_bits] &= ~(1 << (i%int_bits));
}

// Check whether the  i-th bit in bitmap is off
bool is_bit_off(uint32_t *bm, uint32_t i) {
	uint32_t int_bits = sizeof(uint32_t) * 8;
	return ( (bm[i/int_bits] & (1 << (i%int_bits))) == 0 );
}


// Helper function to seek a byte in the file represented by inode with offset,
// return the pointer to the byte, or NULL if offset is beyond EOF
void *seekbyte(a1fs_inode *inode, off_t offset) {
	uint64_t implicit_file_size = inode->size;
	if (S_ISDIR(inode->mode)) {
		implicit_file_size = sizeof(a1fs_dentry) * inode->dentry_count;
	}
	if ((uint64_t)offset > implicit_file_size) {return NULL;}

	fs_ctx *fs = get_fs();
	void *image = fs->image;

	a1fs_blk_t blocks_to_skip = offset / A1FS_BLOCK_SIZE;
	a1fs_extent *curr_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * inode->extentblock);
	// block number of the remaining bytes
	a1fs_blk_t last_block;
	a1fs_blk_t blockcount;
	// Traverse through the extents
	for (int i = 0; i < inode->extentcount; i++) {
		curr_extent += (sizeof(a1fs_extent) * i);
		blockcount = curr_extent->count;
		last_block = curr_extent->start;
		while (blockcount > 0) {
			if (blocks_to_skip == 0) {break;}
			last_block++;
			blockcount--;
		}
		if (blocks_to_skip == 0) {break;}
	}
	// We have strictly less than 4096 bytes to traverse, so just visit the block using pointer arithmetic
	int remaining_bytes = offset % A1FS_BLOCK_SIZE;
	void *target_byte = (void *)(image + A1FS_BLOCK_SIZE * last_block + remaining_bytes);
	return target_byte;
}


/**
 * Get inode number by absolute path.
 * 
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 * 
 * @param path  path to any file in the file syste.
 * @return      inode number represented by the path on success; -errno on error;
 */
long get_ino_num_by_path(const char *path) {
	
	if (strlen(path) >= A1FS_PATH_MAX) {
		printf("ENAMETOOLONG\n");
		return -ENAMETOOLONG;
	}

	printf("The path length is %ld\n", strlen(path));
	printf("The path is:%s\n", path);

	// path is just "/"
	if (strcmp(path, "/") == 0) {
		// Root inode number is 1
		return 1;
	}

	// Continuing from here, path = "/..."

	// get the address to the beginning of file system
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *)image;

	// Start with the Root inode
	a1fs_ino_t  curr_ino_t = 1;
	a1fs_inode  *curr_inode;
	uint64_t    dentry_count;
	int        foundPathCompo;
	a1fs_dentry *curr_dentry;

	// Make of a copy to the path, since strtok is destructive
	char cpy_path[strlen(path) + 1];
	strcpy(cpy_path, path);
	char delim[] = "/";
	char *pathComponent = pathComponent = strtok(cpy_path, delim);
	// Using do-while loop since curr_inode would be root inode initially, thus
	// iterating at least once.
	do {
		curr_inode = (a1fs_inode *) (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_t - 1));
		// If the path prefix is not a dir
		if ((curr_inode->mode & __S_IFDIR) <= 0) {
				printf("ENOTDIR\n");
				return -ENOTDIR;
		}
		// The inode is a directory inode, but did not allocate any directory entry
		dentry_count = curr_inode->dentry_count;
		if (dentry_count == 0) {
			return -ENOENT;
		}

		// Search in the current inode 's directory entries to find the next path component
		foundPathCompo = 0;
		for (uint64_t i = 0; i < dentry_count; i++) {
			curr_dentry = (a1fs_dentry *)(seekbyte(curr_inode, sizeof(a1fs_dentry) * i));
			if (strcmp(curr_dentry->name, pathComponent) == 0) {
				foundPathCompo = 1;
				curr_ino_t = curr_dentry->ino;
				break;
			}
		}
		if (!foundPathCompo) {
			printf("Path is --%s--\n", path);
			printf("Path %d \"/\"\n", strcmp(path, "/"));
			printf("PathCompo is %s\n", pathComponent);
			printf("dentry_count is %ld\n", dentry_count);
			printf("ENOENT\n");
			return -ENOENT;
		}

		pathComponent = strtok(NULL, delim);
		
	} while (pathComponent != NULL);

	return (long) curr_ino_t;
}

// Return the parent directory's inode number
long get_parent_dir_ino_num_by_path(const char *path) {

	// cut the last component which is the directory name we want to create
	char parent_path[strlen(path) + 1];
	strcpy(parent_path, path);
	char *ptr = strrchr(parent_path, '/');
	if (ptr != parent_path){
		ptr[0] = '\0';
	} else {
		ptr[1] = '\0';
	}

	return (long) get_ino_num_by_path(parent_path);
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The f_fsid and f_flag fields are ignored.
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */

static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	st->f_bsize   = A1FS_BLOCK_SIZE;
	st->f_frsize  = A1FS_BLOCK_SIZE;
	a1fs_superblock *sb = fs->image;
	st->f_blocks = sb->size / A1FS_BLOCK_SIZE;
	st->f_bfree = sb->s_free_blocks_count;
	st->f_files = sb->s_inodes_count;
	st->f_ffree = sb->s_free_inodes_count;
	st->f_namemax = A1FS_NAME_MAX;

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the stat() system call. See "man 2 stat" for details.
 * The st_dev, st_blksize, and st_ino fields are ignored.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors).
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	fs_ctx *fs = get_fs();

	void *image = fs->image;
	a1fs_superblock *sb = image;
	printf("GETATTR Path passed is: %s\n", path);

	long curr_ino_num = get_ino_num_by_path(path);
	if (curr_ino_num < 0) {
		fprintf(stderr, "get_attr ERRROR\n");
		return curr_ino_num;
	}
	a1fs_ino_t curr_ino_t = (a1fs_ino_t) curr_ino_num;

	a1fs_inode *curr_inode = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_t - 1));
	st->st_mode = curr_inode->mode;
	st->st_nlink = (nlink_t)(curr_inode->links);
	blkcnt_t sectors_used = (blkcnt_t)(curr_inode->size / 512);
	if (curr_inode->size % 512 != 0)
		sectors_used++;
	st->st_blocks = sectors_used;
	st->st_mtime = curr_inode->mtime.tv_sec;
	st->st_size = curr_inode->size;
	return 0;

}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler() for each directory
 * entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	printf("\nEntered into readdir\n");
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	void *image = fs->image;
	a1fs_superblock *sb = image;
	
	long curr_ino_num = get_ino_num_by_path(path);
	if (curr_ino_num < 0) {
		fprintf(stderr, "read_dirERORORROORORORORR\n");
		return curr_ino_num;
	}
	a1fs_inode *curr_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_num - 1));
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	a1fs_dentry *curr_dir;
	for (uint64_t i = 0; i < curr_inode->dentry_count; i++) {
		curr_dir = (a1fs_dentry *) seekbyte(curr_inode, sizeof(a1fs_dentry) * i);
		if (curr_dir->ino != 0) {
			filler(buf, curr_dir->name, NULL, 0);
		}
	}
	return 0;
}

/**
 * Return the index of the first bit of a bit sequence such that
 * - all bits in the sequence have value of 0
 * - the sequence is of length len
 *
 * NOTE: If len == 1, then it is equivalently searching for a bit of value 0
 * in the bitmap.
 *
 * Errors:
 *   ENOSPC  no such bit sequence of length len exists
 *
 * @param bitmap the bitmap.
 * @param limit  how many bits to iterate through at total.
 * @param len    how many consecutive bits 
 * @return       the first bit of the bit sequence on success; -ENOSPC on error.
 */

long find_free_entry_of_length_in_bitmap(uint32_t *bitmap, uint32_t limit, uint32_t len) {
	for (uint32_t bit = 0; bit < limit; bit++) {
		// found a bit of value 0
		if (is_bit_off(bitmap, bit)) {
			int all_bits_zero = 1;
			for (uint32_t i = 0; i < len; i++) {
				if (!is_bit_off(bitmap, bit + i)) {
					all_bits_zero = 0;
					break;
				}
			}
			if (all_bits_zero) {
				return bit;
			}
		}

		// The number of unchecked bits are less than len
		if (limit - bit < len) {
			return -ENOSPC;
		}
	}
	// Actually hopefully would not ever each here
	return -ENOSPC;
}

/**
 * Return the longest length of continuous empty bits
 *
 * @param bitmap the bitmap.
 * @param limit  how many bits to iterate through at total.
 * @return       the longest length of continuous empty bits
 */
uint32_t find_largest_chunk(uint32_t *bitmap, uint32_t limit){
	uint32_t longest = 0;
	uint32_t sec_longest = 0;
	for (uint32_t bit = 0; bit < limit; bit++) {
		if (is_bit_off(bitmap, bit)) {
			if (longest == 0) {
				longest += 1;
			}
			sec_longest += 1;
			if (sec_longest >= longest) {
				longest = sec_longest;
			}
		} else {
			sec_longest = 0;
		}
	}
	return longest;
}

/**
 * Allocate a extent block for the empty inode and modify corresponding metadata
 */
int alloc_extent_block(a1fs_inode *ino) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	// no more free data block, return error
	if (sb->s_free_blocks_count < 1) { return -ENOSPC; }
	uint32_t *data_bitmap = (uint32_t *) (image + sb->bg_block_bitmap * A1FS_BLOCK_SIZE);
	long some_bit_off = find_free_entry_of_length_in_bitmap(data_bitmap, sb->data_block_count, 1);
	if (some_bit_off < 0) { return -ENOSPC; }
	ino->extentblock = (a1fs_blk_t) sb->bg_data_block + some_bit_off;
	setBitOn(data_bitmap, some_bit_off);
	(sb->s_free_blocks_count)--;
	return 0;
}

// Allocate an extent according to the size
int alloc_an_extent_for_size(a1fs_extent *extent, uint64_t size) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	// no more free data block, return error
	if (sb->s_free_blocks_count < 1) { return -ENOSPC; }
	uint32_t *data_bitmap = (uint32_t *) (image + sb->bg_block_bitmap * A1FS_BLOCK_SIZE);
	uint32_t blocks_needed = ceil_divide(size, A1FS_BLOCK_SIZE);
	long some_bit_off = find_free_entry_of_length_in_bitmap(data_bitmap, sb->data_block_count, blocks_needed);
	if (some_bit_off < 0) { return -ENOSPC; }
	for (uint32_t i = 0; i < blocks_needed; i++) {
		setBitOn(data_bitmap, some_bit_off + i);
		(sb->s_free_blocks_count)--;
	}

	extent->start = (a1fs_blk_t)(sb->bg_data_block + some_bit_off);
	extent->count = blocks_needed;
	return 0;
}

// Fill a block with free directories
void fill_with_dentry(a1fs_blk_t blk_num) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_dentry *curr_dentry;
	for (uint32_t i = 0; i < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); i++) {
		curr_dentry = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE * blk_num + sizeof(a1fs_dentry) * i);
		curr_dentry->ino = 0;
	}
}

// First allocate an extent block for the directory inode, then allocate an extent of length 1
// then fill the first block pointed to by the extent with 16 directories
int init_dir_inode_extent(a1fs_inode *inode) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;

	int ret0 = alloc_extent_block(inode);
	if (ret0 != 0) { return ret0; };
	a1fs_extent *curr_extent;
	// Initialize 512 free extents
	for (uint32_t i = 0; i < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); i++) {
		curr_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * inode->extentblock + sizeof(a1fs_extent) * i);
		curr_extent->count = 0;
	}
	a1fs_extent *new_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE *(inode->extentblock));
	int ret1 = alloc_an_extent_for_size(new_extent, sizeof(a1fs_dentry));
	if (ret1 != 0) {return ret1;}
	(inode->extentcount)++;
	// allocate a new directory entry
	fill_with_dentry(new_extent->start);
	inode->dentry_count += A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
	(inode->links)++;
	return 0;
}

// Create a new inode for the given mode, returns the new inode number
long init_new_inode(mode_t mode) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	if (sb->s_free_inodes_count < 1) {
		return -ENOSPC;
	}

	uint32_t *inode_bitmap = (uint32_t *) (image + sb->bg_inode_bitmap * A1FS_BLOCK_SIZE);
	long free_bit = find_free_entry_of_length_in_bitmap(inode_bitmap, sb->s_inodes_count, 1);
	// out of inodes to allocate, return ENOSPC
	if (free_bit < 0) { return free_bit; }
	a1fs_inode *new_inode = (a1fs_inode *)(image + sb->bg_inode_table * A1FS_BLOCK_SIZE + (free_bit) * sizeof(a1fs_inode));
	setBitOn(inode_bitmap, free_bit);
	a1fs_ino_t new_inode_num = free_bit + 1;
	(sb->s_free_inodes_count)--;
	
	new_inode->mode = (mode | 0777);
	if (S_ISDIR(mode)) {
		new_inode->links = 2;
	} else if (S_ISREG(mode)) {
		new_inode->links = 1;
	}
	new_inode->size = 0;
	clock_gettime(CLOCK_REALTIME, &(new_inode->mtime));
	new_inode->extentcount = 0;
	new_inode->dentry_count = 0;
	return new_inode_num;
}

// Insert a new inode num to the parent directory's entries and update metadata accordingly
int add_new_inode_to_parent_dir(a1fs_inode *parent_inode, a1fs_ino_t new_ino_num, const char *entryname) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	clock_gettime(CLOCK_REALTIME, &(parent_inode->mtime));
	// allocate extent block for the parent_inode if it hasn't allocate any yet
	if (parent_inode->extentcount == 0) {
		int ret = init_dir_inode_extent(parent_inode);
		if (ret != 0) { return ret; }
	}

	parent_inode->size += sizeof(a1fs_dentry);
	a1fs_dentry *new_dir = NULL;
	// search for a free directory by checking if the ino == 0
	uint64_t i = 0;
	a1fs_dentry *cur_dir;
	off_t offset = 0;
	while (i < parent_inode->dentry_count){
		cur_dir = (a1fs_dentry *) seekbyte(parent_inode, offset);
		if (cur_dir->ino == 0){
			new_dir = cur_dir;
			break;
		}
		offset += sizeof(a1fs_dentry);
		i++;
	}

	// Allocate more extent for the free directory
	if (new_dir == NULL) {
		a1fs_extent *curr_extent = NULL;
		a1fs_extent *free_extent = NULL;
		// Find a free extent to allocate more directories
		for (int i = 0; i < parent_inode->extentcount; i++) {
			curr_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE*(parent_inode->extentblock) + sizeof(a1fs_extent) * i);
			if (curr_extent->count == 0) {
				free_extent = curr_extent;
				break;
			}
		}
		// Cannot find any free extent, no space
		if (free_extent == NULL) {
			return -ENOSPC;
		}
		int ret1 = alloc_an_extent_for_size(free_extent, sizeof(a1fs_dentry));
		// Not enough free data blocks left to new directories
		if (ret1 < 0) {return ret1;}
		new_dir = (a1fs_dentry *)(image + A1FS_BLOCK_SIZE * free_extent->start);
	}

	new_dir->ino = new_ino_num;
	// get the entry name we want to create
	strcpy(new_dir->name, entryname);
	return 0;
}

/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * NOTE: the mode argument may not have the type specification bits set, i.e.
 * S_ISDIR(mode) can be false. To obtain the correct directory type bits use
 * "mode | S_IFDIR".
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).ini
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.  /local/kkk/mydir
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	// mode unused
	(void)mode;
	printf("\nEntered into mkdir\n");
	fs_ctx *fs = get_fs();

	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;

	// get parent directory inode number
	long parent_directory_ino_num = get_parent_dir_ino_num_by_path(path);
	if (parent_directory_ino_num < 0) {
		fprintf(stderr, "Mkdir ERRORRRRRRRRR\n");
		return parent_directory_ino_num;
	}
	parent_directory_ino_num = (a1fs_ino_t) parent_directory_ino_num;

	// set up a new inode
	long new_inode_num = init_new_inode(__S_IFDIR);
	if (new_inode_num < 0) {
		return new_inode_num;
	}
	new_inode_num = (a1fs_blk_t) new_inode_num;
	
	a1fs_inode *parent_directory_ino = (a1fs_inode *)(image + sb->bg_inode_table * A1FS_BLOCK_SIZE + (parent_directory_ino_num - 1) * sizeof(a1fs_inode));

	char *entryname = strrchr(path, '/');
	entryname += 1;
	// helper function to add a new inode under parent directory's inode
	int ret = add_new_inode_to_parent_dir(parent_directory_ino, new_inode_num, entryname);
	if (ret != 0) { return ret; }
	return 0;
}

/**
 * Check if a directory is empty or not.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * @param path  path to the directory to remove.
 * @return      0 if the directory is empty; 1 if the directory is not empty.
 */
int check_dir_empty(const char *path) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	a1fs_ino_t curr_ino_num = (a1fs_ino_t) get_ino_num_by_path(path);
	a1fs_inode *curr_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_num - 1));
	return curr_inode->size > 0;
}

void rm_inode(a1fs_ino_t ino_num){
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	a1fs_inode *curr_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(ino_num - 1));
	uint32_t *block_bitmap = (uint32_t *) (image + sb->bg_block_bitmap * A1FS_BLOCK_SIZE);
	uint32_t *inode_bitmap = (uint32_t *) (image + sb->bg_inode_bitmap * A1FS_BLOCK_SIZE);
	// set bit off for extent block and dentry block on data bitmap
	if (curr_inode->extentcount > 0){

		a1fs_extent *curr_extent;
		// Free each extent's block
		for (uint32_t i = 0; i < curr_inode->extentblock; i++) {
			curr_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * curr_inode->extentblock + sizeof(a1fs_extent) * i);
			for (uint32_t i = 0; i < curr_extent->count; i++) {
				setBitOff(block_bitmap, curr_extent->start + i - sb->bg_data_block);
				sb->s_free_blocks_count++;
			}
		}
		// Free the inode's extent block
		a1fs_blk_t extent_block_on_bitmap = curr_inode->extentblock - sb->bg_data_block;
		setBitOff(block_bitmap, extent_block_on_bitmap);
		sb->s_free_blocks_count++;
	}
	// set bit off for inode on inode bitmap
	a1fs_blk_t inode_on_bitmap = ino_num - 1;
	setBitOff(inode_bitmap, inode_on_bitmap);
	sb->s_free_inodes_count ++;
}

void rm_inode_from_parent_directory(a1fs_ino_t parent_ino_num, a1fs_ino_t child_ino_num){
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	a1fs_inode *parent_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(parent_ino_num - 1));
	parent_inode->links --;
	parent_inode->size -= (sizeof(a1fs_dentry));
	clock_gettime(CLOCK_REALTIME, &(parent_inode->mtime));

	uint64_t i = 0;
	a1fs_dentry *cur_dir;
	off_t offset = 0;
	// change dentry ino to 0
	while (i < parent_inode->dentry_count){
		cur_dir = (a1fs_dentry *) seekbyte(parent_inode, offset);
		if (cur_dir->ino == child_ino_num){
			cur_dir->ino = 0;
			cur_dir->name[0] = '\0';
			break;
		}
		offset += sizeof(a1fs_dentry);
		i++;
	}
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	if (check_dir_empty(path) == 1) {return -ENOTEMPTY;}
	long curr_ino_num = get_ino_num_by_path(path);
	rm_inode((a1fs_ino_t)curr_ino_num);
	long parent_ino_num = get_parent_dir_ino_num_by_path(path);
	rm_inode_from_parent_directory((a1fs_ino_t)parent_ino_num, (a1fs_ino_t)curr_ino_num);
	return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	// Insufficent amount of free inode for the new file
	if (sb->s_free_inodes_count < 1) {
		return -ENOSPC;
	}

	long parent_dir_ino_num = get_parent_dir_ino_num_by_path(path);
	// Error checking
	if (parent_dir_ino_num < 0) { return (int) parent_dir_ino_num; };
	a1fs_inode *parent_inode = (a1fs_inode *) (image + A1FS_BLOCK_SIZE * (sb->bg_inode_table) + sizeof(a1fs_inode) * (parent_dir_ino_num - 1));
	
	// Init a new inode for the empty file
	long new_inode_num = init_new_inode(mode);
	if (new_inode_num < 0) { return new_inode_num; }
	new_inode_num = (a1fs_ino_t) new_inode_num;

	char *entryname = strrchr(path, '/');
	entryname+=1;
	int ret = add_new_inode_to_parent_dir(parent_inode, new_inode_num, entryname);
	if (ret != 0) { return ret; }	
	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	long curr_ino_num = get_ino_num_by_path(path);
	rm_inode((a1fs_ino_t)curr_ino_num);
	long parent_ino_num = get_parent_dir_ino_num_by_path(path);
	rm_inode_from_parent_directory((a1fs_ino_t)parent_ino_num, (a1fs_ino_t)curr_ino_num);
	return 0;
}

/**
 * Rename a file or directory.
 *
 * Implements the rename() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "from" exists.
 *   The parent directory of "to" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param from  original file path.
 * @param to    new file path.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rename(const char *from, const char *to)
{
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *)image;

	a1fs_ino_t from_ino_num = (a1fs_ino_t) get_ino_num_by_path(from);
	a1fs_ino_t from_parent_ino_num = (a1fs_ino_t) get_parent_dir_ino_num_by_path(from);

	long to_ino_num = get_ino_num_by_path(to);
	char *entryname = strrchr(from, '/') + 1;
	// last component does not exists, move "from" to parent dir of "to", then rename "from " to last component of dir
	if (to_ino_num <= 0) {
		to_ino_num = get_parent_dir_ino_num_by_path(to);
		entryname = strrchr(to, '/') + 1;
	}
	// Move "from" inode under "to"
	a1fs_inode *to_ino = (a1fs_inode *)(image + A1FS_BLOCK_SIZE * sb->bg_inode_table + sizeof(a1fs_inode) *((a1fs_ino_t)to_ino_num-1));
	rm_inode_from_parent_directory(from_parent_ino_num, from_ino_num);
	int ret = add_new_inode_to_parent_dir(to_ino, from_ino_num, entryname);
	if (ret != 0) { return ret; }
	return 0;
}


/**
 * Change the access and modification times of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only have to implement the setting of modification time (mtime).
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * @param path  path to the file or directory.
 * @param tv    timestamps array. See "man 2 utimensat" for details.
 * @return      0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec tv[2])
{
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *)image;
	
	a1fs_ino_t ino_num = get_ino_num_by_path(path);
	a1fs_inode *inode = (a1fs_inode *) (image + A1FS_BLOCK_SIZE * sb->bg_inode_table + sizeof(a1fs_inode) * (ino_num - 1));
	inode->mtime.tv_sec = tv[1].tv_sec;
	inode->mtime.tv_nsec = tv[1].tv_nsec;
	return 0;
}

// return 0 if there are enough free extents, otherwise return 1
int if_extent_can_hold_length(uint32_t *bitmap, uint32_t limit, uint32_t len, a1fs_inode *inode){
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	uint32_t remaining = len;
	int extent_need = 0;
	while (remaining > 0){
		uint32_t longest = find_largest_chunk(bitmap, limit);
		if (longest > remaining){
			extent_need ++;
			remaining = 0;
		} else{
			extent_need ++;
			remaining -= longest;
		}
	}
	unsigned short extent_count = inode->extentcount;
	a1fs_extent *new_extent_block;
	unsigned short i = 0;
	while (i < extent_count){
		if (extent_need == 0){break;}
		new_extent_block = (a1fs_extent *) (image + A1FS_BLOCK_SIZE * inode->extentblock + (a1fs_blk_t)i * A1FS_BLOCK_SIZE);
		if (new_extent_block->count == 0){
			extent_need --;
		}
		i++;
	}
	if (extent_need == 0){
		return 0;
	}
	return 1;
}

// pad the buf with size many zeroes
void pad_zeroes(char *buf, size_t size) {
	memset(buf, 0, size);
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, future reads from the new uninitialized range must
 * return zero data.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();
	printf("\nEntered into truncate\n");
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	a1fs_ino_t ino_num = get_ino_num_by_path(path);
	a1fs_inode *curr_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(ino_num - 1));
	uint32_t *data_bitmap = (uint32_t *) (image + sb->bg_block_bitmap * A1FS_BLOCK_SIZE);
	clock_gettime(CLOCK_REALTIME, &(curr_inode->mtime));
	if(curr_inode->size == (uint64_t)size) {return 0;}
	// shrinking
	else if (curr_inode->size > (uint64_t)size){
		curr_inode->size = (uint64_t)size;
		uint32_t num_block_need = ceil_divide((uint32_t)size, A1FS_BLOCK_SIZE);
		uint32_t num_block_old = ceil_divide((uint32_t)curr_inode->size, A1FS_BLOCK_SIZE);
		uint32_t num_block_delete = num_block_old - num_block_need;
		a1fs_extent *extent_block;
		unsigned short extent_count = curr_inode->extentcount;
		sb->s_free_blocks_count += num_block_delete;
		while (num_block_delete > 0){
			extent_block = (a1fs_extent *) (image + A1FS_BLOCK_SIZE * curr_inode->extentblock + (a1fs_blk_t)extent_count * A1FS_BLOCK_SIZE);
			if (extent_block->count >= num_block_delete){
				a1fs_blk_t old_count = extent_block->count;
				extent_block->count -= num_block_delete;
				for (uint32_t i = 0; i < num_block_delete; i++){
					setBitOff(data_bitmap, extent_block->start + old_count - i - sb->s_free_blocks_count);
				}
				num_block_delete = 0;
			} 
			else if (extent_block->count < num_block_delete && extent_block->count > 0){ 
				a1fs_blk_t old_count = extent_block->count;
				extent_block->count = 0;
				for (uint32_t i = 0; i < old_count; i++){
					setBitOff(data_bitmap, extent_block->start + old_count - i - sb->s_free_blocks_count);
				}
				num_block_delete -= old_count;
			}
			extent_count --;
		}
	}
	// extending
	else if (curr_inode->size < (uint64_t)size){
		uint32_t num_block_need = ceil_divide((uint32_t)size, A1FS_BLOCK_SIZE) - ceil_divide(curr_inode->size, A1FS_BLOCK_SIZE);
		printf("%d\n", num_block_need);
		if (num_block_need > sb->s_free_blocks_count){ return -ENOSPC;}
		if (curr_inode->extentcount == 0){ alloc_extent_block(curr_inode);}
		long result = find_free_entry_of_length_in_bitmap(data_bitmap, sb->data_block_count, num_block_need);
		// find a continuous block 
		if (result > 0){
			// inode has no extent initialized
			if (curr_inode->extentcount == 0){
				a1fs_extent *new_extent_block = (a1fs_extent *) (image + A1FS_BLOCK_SIZE * curr_inode->extentblock);
				new_extent_block->start = (a1fs_blk_t)result + sb->bg_data_block;
				new_extent_block->count = num_block_need;
				for (uint32_t j = 0; j < num_block_need; j++){
					setBitOn(data_bitmap, (uint32_t)result + j);
				}
				curr_inode->extentcount = 512;
				for (uint32_t i = 1; i < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); i++) {
					new_extent_block = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * curr_inode->extentblock + sizeof(a1fs_extent) * i);
					new_extent_block->count = 0;
				}
				char * new_data_block = (char *) (image + A1FS_BLOCK_SIZE * new_extent_block->start);
				pad_zeroes(new_data_block, new_extent_block->count * A1FS_BLOCK_SIZE);
				curr_inode->size = (uint64_t)size;
				sb->s_free_blocks_count -= num_block_need;
			}
			else if (curr_inode->extentcount > 0){
				unsigned short i = 0;
				while (i < curr_inode->extentcount){
					a1fs_extent * new_extent_block = (a1fs_extent *) (image + A1FS_BLOCK_SIZE * curr_inode->extentblock + (a1fs_blk_t)i * A1FS_BLOCK_SIZE);
					if (new_extent_block->count == 0){
						new_extent_block->start = (a1fs_blk_t) result + sb->bg_data_block;
						new_extent_block->count = num_block_need;
						for (uint32_t j = 0; j < num_block_need; j++){
							setBitOn(data_bitmap, (uint32_t)result + j);
						}
						curr_inode->size = (uint64_t)size;
						sb->s_free_blocks_count -= num_block_need;
						char * new_data_block = (char *) (image + A1FS_BLOCK_SIZE * new_extent_block->start);
						pad_zeroes(new_data_block, new_extent_block->count * A1FS_BLOCK_SIZE);
						break;
					}
					i ++;
				}
			}
		}else if (result < 0){
			if (curr_inode->extentcount == 0){ alloc_extent_block(curr_inode);}
			if (curr_inode->extentcount == 0){
				curr_inode->extentcount = 512;
				a1fs_extent * new_extent_block;
				for (uint32_t i = 0; i < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); i++) {
					new_extent_block = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * curr_inode->extentblock + sizeof(a1fs_extent) * i);
					new_extent_block->count = 0;
				}
			}
			if (if_extent_can_hold_length(data_bitmap, sb->data_block_count, num_block_need, curr_inode) == 1){return -ENOSPC;}
			curr_inode->size = (uint64_t)size;
			sb->s_free_blocks_count -= num_block_need;
			while (num_block_need > 0){
				uint32_t longest = find_largest_chunk(data_bitmap, sb->data_block_count);
				if (longest >= num_block_need){ 
					result = find_free_entry_of_length_in_bitmap(data_bitmap, sb->data_block_count, longest);
					unsigned short extent_count = curr_inode->extentcount;
					a1fs_extent *new_extent_block; 
					unsigned short i = 0;
					while (i < extent_count){
						new_extent_block = (a1fs_extent *) (image + A1FS_BLOCK_SIZE * curr_inode->extentblock + (a1fs_blk_t)i * A1FS_BLOCK_SIZE);
						if (new_extent_block->count == 0){
							new_extent_block->start = (a1fs_blk_t)result + sb->bg_data_block;
							new_extent_block->count = num_block_need;
								for (uint32_t j = 0; j < num_block_need; j++){
									setBitOn(data_bitmap, (uint32_t)result + j);
							}
							char * new_data_block = (char *) (image + A1FS_BLOCK_SIZE * new_extent_block->start);
							pad_zeroes(new_data_block, new_extent_block->count * A1FS_BLOCK_SIZE);
							break;
						}
						i ++;
					}
					num_block_need = 0;
				}
				else if (longest < num_block_need){
					result = find_free_entry_of_length_in_bitmap(data_bitmap, sb->data_block_count, longest);
					unsigned short extent_count = curr_inode->extentcount;
					unsigned short i = 0;
					a1fs_extent * new_extent_block;
					while (i < extent_count){
						new_extent_block = (a1fs_extent *) (image + A1FS_BLOCK_SIZE * curr_inode->extentblock + (a1fs_blk_t)i * A1FS_BLOCK_SIZE);
						if (new_extent_block->count == 0){
							new_extent_block->start = (a1fs_blk_t)result + sb->bg_data_block;
							new_extent_block->count = longest;
							for (uint32_t j = 0; j < longest; j++){
								setBitOn(data_bitmap, (uint32_t)result + j);
							}
							char * new_data_block = (char *) (image + A1FS_BLOCK_SIZE * new_extent_block->start);
							pad_zeroes(new_data_block, new_extent_block->count * A1FS_BLOCK_SIZE);
							num_block_need -= longest;
							break;
						}
						i ++;
					}
				}
			}
		}
	}

	return 0;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Should return exactly the number of bytes
 * requested except on EOF (end of file) or error, otherwise the rest of the
 * data will be substituted with zeros. Reads from file ranges that have not
 * been written to must return zero data.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size - number of bytes requested.
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	// unused
	(void)fi;
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *)image;
	a1fs_ino_t file_ino_num = (a1fs_ino_t) get_ino_num_by_path(path);
	a1fs_inode *file_ino = (a1fs_inode *)(image + A1FS_BLOCK_SIZE * sb->bg_inode_table + sizeof(a1fs_inode) * (file_ino_num-1));
	// If file is empty or the offset is beyond EOF, substitude the rest of the data with 0
	char *currbyte;
	if (file_ino->size == 0 || (currbyte = (char *)seekbyte(file_ino, offset)) == NULL) {
		pad_zeroes(buf, size);
		return 0;
	}
	uint64_t bytes_read = 0;
	while (size > 0) {
		if (bytes_read == file_ino->size - offset) {
			pad_zeroes(buf, size);
			break;
		}
		memcpy(buf, currbyte, 1);
		buf++;
		bytes_read++;
		currbyte = (char *)seekbyte(file_ino, offset+bytes_read);
		size--;
	}
	return bytes_read;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Should return exactly the number of
 * bytes requested except on error. If the offset is beyond EOF (end of file),
 * the file must be extended. If the write creates a "hole" of uninitialized
 * data, future reads from the "hole" must return zero data.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size - number of bytes requested.
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();
	void *image = fs->image; 
	a1fs_superblock *sb = (a1fs_superblock *) image;
	a1fs_ino_t file_ino_num = get_ino_num_by_path(path);
	a1fs_inode *file_ino = (a1fs_inode *) (image + A1FS_BLOCK_SIZE * sb->bg_inode_table + sizeof(a1fs_inode) * (file_ino_num-1));

	// Nothing to write
	if (size == 0) {return 0;}

	// Now size > 0 we have something to write
	// Check whether file size is enough, if not, allocate more as needed
	if ((file_ino->size == 0) || (offset+size > file_ino->size)) {
		int ret = a1fs_truncate(path, offset+size);
		if (ret < 0) {return ret;}
	}
	char *currbyte = (char *)seekbyte(file_ino, offset);
	uint64_t bytes_wrote = 0;
	while (size > 0) {
		memcpy(currbyte, buf, 1);
		buf++;
		bytes_wrote++;
		currbyte = (char *)seekbyte(file_ino, offset+bytes_wrote);
		size--;
	}
	return bytes_wrote;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.rename   = a1fs_rename,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
