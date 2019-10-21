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

/**
 * Get inode number by absolute path.
 * 
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 * 
 * @param path  path to any file in the file syste.
 * @return      0 on success; -errno on error;
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
	a1fs_superblock *sb          = image;
	
	// TODO: For Step 2, we initially assume that dentry_count is small enough
	// 		 so that all dentry are stored in one block, but we actually would
	//       have to look into other blocks within the same extent, or even
	//       other extents.

	// Start with the Root inode
	a1fs_ino_t  curr_ino_t = 1;
	a1fs_inode  *curr_inode;
	a1fs_extent *curr_extent;
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
		// If dentry_count > 0, then the inode must have allocated some block to store the extent
		curr_extent = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*(curr_inode->extentblock));
		
		// Search in the current inode 's directory entries to find the next path component
		foundPathCompo = 0;
		for (uint64_t i = 0; i < dentry_count; i++) {
			curr_dentry = (a1fs_dentry *)(image + A1FS_BLOCK_SIZE*(curr_extent->start) + (sizeof(a1fs_dentry) * i));
			if (strcmp(curr_dentry->name, pathComponent) == 0) { // At root inode what if path component is NULL
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
	//TODO
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
	// NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// get the address to the beginning of file system
	// if (strcmp(path, "/") == 0) {
	// 	st->st_mode = S_IFDIR | 0777;
	// 	return 0;
	// }
	// (void)fs;
	// return -ENOSYS;

	void *image = fs->image;
	a1fs_superblock *sb = image;
	// a1fs_inode *inode_table = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*(sb->bg_inode_table));
	printf("GETATTR Path passed is: %s\n", path);

	long curr_ino_num = get_ino_num_by_path(path);
	if (curr_ino_num < 0) {
		fprintf(stderr, "get_attr ERRROR\n");
		return curr_ino_num;
	}
	a1fs_ino_t curr_ino_t = (a1fs_ino_t) curr_ino_num;
	// a1fs_inode *inode_table = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*(sb->bg_inode_table));
	// a1fs_inode *curr_inode = (inode_table + sizeof(a1fs_inode)*(curr_ino_t - 1));

	a1fs_inode *curr_inode = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_t - 1));
	// TODO what should I put here for st_mode?
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

	//NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {
		// filler(buf, "." , NULL, 0);
		// filler(buf, "..", NULL, 0);
	// 	return 0;
	// }
	// (void)fs;
	// return -ENOSYS;
	void *image = fs->image;
	a1fs_superblock *sb = image;
	// a1fs_inode *inode_table = (a1fs_inode *)();
	
	long curr_ino_num = get_ino_num_by_path(path);
	if (curr_ino_num < 0) {
		fprintf(stderr, "read_dirERORORROORORORORR\n");
		return curr_ino_num;
	}
	a1fs_inode *curr_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_num - 1));
	a1fs_extent *curr_extent = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*(curr_inode->extentblock));
	// a1fs_dentry *curr_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE*(curr_extent->start));
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	a1fs_dentry *curr_dir;
	for (uint64_t i = 0; i < curr_inode->dentry_count; i++) {
		curr_dir = (a1fs_dentry *)  (image + A1FS_BLOCK_SIZE*(curr_extent->start) + sizeof(a1fs_dentry) * i);
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
 * Allocate a extent block for the inode and modify corresponding metadata
 */
int alloc_extent_block(a1fs_inode *ino) {
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = (a1fs_superblock *) image;
	// no more free data block, return error
	if (sb->s_free_blocks_count < 1) { return -ENOSPC; }
	uint32_t *data_bitmap = (uint32_t *) (image + sb->bg_block_bitmap * A1FS_BLOCK_SIZE);
	long some_bit_off = find_free_entry_of_length_in_bitmap(data_bitmap, sb->s_blocks_count, 1);
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
	long some_bit_off = find_free_entry_of_length_in_bitmap(data_bitmap, sb->s_blocks_count, blocks_needed);
	if (some_bit_off < 0) { return -ENOSPC; }
	// Step 7 change here
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
	a1fs_dentry *curr_dentry = image + A1FS_BLOCK_SIZE * blk_num;
	for (uint32_t i = 0; i < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); i++) {
		curr_dentry[i].ino = 0;
	}
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
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
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
	if (sb->s_free_inodes_count < 1) {
		return -ENOSPC;
	}

	// get parent directory inode number
	long parent_directory_ino_num = get_parent_dir_ino_num_by_path(path);
	if (parent_directory_ino_num < 0) {
		fprintf(stderr, "Mkdir ERRORRRRRRRRR\n");
		return parent_directory_ino_num;
	}
	parent_directory_ino_num = (a1fs_ino_t) parent_directory_ino_num;

	//if there is no free inode available, return ENOSPC
	uint32_t *inode_bitmap = (uint32_t *) (image + sb->bg_inode_bitmap * A1FS_BLOCK_SIZE);
	long free_bit = find_free_entry_of_length_in_bitmap(inode_bitmap, sb->s_inodes_count, 1);
	// out of inodes to allocate
	if (free_bit < 0) { return free_bit; }
	// int j = 1;
	a1fs_inode *new_inode = (a1fs_inode *)(image + sb->bg_inode_table * A1FS_BLOCK_SIZE + (free_bit) * sizeof(a1fs_inode)) ; // == inode_table[1]
	setBitOn(inode_bitmap, free_bit);
	a1fs_ino_t new_inode_num = free_bit + 1;
	(sb->s_free_inodes_count)--;
	
	new_inode->mode = (__S_IFDIR | 0777);
	new_inode->links = 2;
	new_inode->size = 0;
	clock_gettime(CLOCK_REALTIME, &(new_inode->mtime));
	new_inode->extentcount = 0;
	new_inode->dentry_count = 0;
	
	a1fs_inode *parent_directory_ino = (a1fs_inode *)(image + sb->bg_inode_table * A1FS_BLOCK_SIZE + (parent_directory_ino_num - 1) * sizeof(a1fs_inode));
	clock_gettime(CLOCK_REALTIME, &(parent_directory_ino->mtime));
	// allocate extent block for the parent_directory_ino if it hasn't allocate any yet
	// Step 7 extends here
	if (parent_directory_ino->extentcount == 0) {
		int ret0 = alloc_extent_block(parent_directory_ino);
		if (ret0 != 0) { return ret0; };
		a1fs_extent *new_extent = image + A1FS_BLOCK_SIZE *(parent_directory_ino->extentblock);
		int ret1 = alloc_an_extent_for_size(new_extent, sizeof(a1fs_dentry));
		if (ret1 != 0) {return ret1;}
		(parent_directory_ino->extentcount)++;
		fill_with_dentry(new_extent->start);
		parent_directory_ino->dentry_count += A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
	}
	
	// allocate a new directory entry
	//(parent_directory_ino->dentry_count)++;
	(parent_directory_ino->links)++;
	parent_directory_ino->size += (sizeof(a1fs_dentry));
	a1fs_extent *extentblock = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*(parent_directory_ino->extentblock));
	// let new directory entry point to the last spot in block
	a1fs_dentry *new_dir = NULL;
	// search for a free directory by checking if the ino == 0
	uint64_t i = 0;
	a1fs_dentry *cur_dir;
	while (i < parent_directory_ino->dentry_count){
		cur_dir = (a1fs_dentry *) (image + (A1FS_BLOCK_SIZE * extentblock->start) + sizeof(a1fs_dentry) * i);
		if (cur_dir->ino == 0){
			new_dir = cur_dir;
			break;
		}
		i++;
	}

	// Step 7
	if (new_dir == NULL) {
		// Allocate more extent for direcotrys
		(void) mode;
	}

	new_dir->ino = new_inode_num;
	// get the directory name we want to create
	char *dir_name;
	char cpy_path1[strlen(path) + 1];
	strcpy(cpy_path1, path);
	int delim = '/';
	dir_name = strrchr(path, delim);
	dir_name+=1;
	strcpy(new_dir->name, dir_name);
	return 0;
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
	fs_ctx *fs = get_fs();
	
	//TODO
	//void *image = fs->image;
	//long curr_ino_num = get_ino_num_by_path(path);
	//if (curr_ino_num < 0) {
		//fprintf(stderr, "rmv_dirERORORROORORORORR\n");
		//return curr_ino_num;
	//}
	//a1fs_inode *curr_inode = (image + A1FS_BLOCK_SIZE*(sb->bg_inode_table) + sizeof(a1fs_inode)*(curr_ino_num - 1));
	
	(void)path;
	(void)fs;
	return -ENOSYS;
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

	//TODO
	(void)path;
	(void)mode;
	(void)fs;
	return -ENOSYS;
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
	fs_ctx *fs = get_fs();

	//TODO
	(void)path;
	(void)fs;
	return -ENOSYS;
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

	//TODO
	(void)from;
	(void)to;
	(void)fs;
	return -ENOSYS;
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

	//TODO
	(void)path;
	(void)tv;
	(void)fs;
	return -ENOSYS;
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

	//TODO
	(void)path;
	(void)size;
	(void)fs;
	return -ENOSYS;
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
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
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

	//TODO
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
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
