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


//Helper functions
void setBitOn(unsigned char *A, uint32_t i) {
	int char_bits = sizeof(unsigned char) * 8;
	A[i/char_bits] |= 1 << (i%char_bits);
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
	if (strcmp(path, "/") == 0 || path[1] == '.') {
		// Root inode number is 1
		return 1;	
	}

	// Continuing from here, path = "/..."

	// get the address to the beginning of file system
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb          = image;
	a1fs_inode      *inode_table = (a1fs_inode *) (image + A1FS_BLOCK_SIZE*sb->bg_inode_table);
	
	// TODO: For Step 2, we initially assume that dentry_count is small enough
	// 		 so that all dentry are stored in one block, but we actually would
	//       have to look into other blocks within the same extent, or even
	//       other extents.

	// Start with the Root inode
	a1fs_ino_t  curr_ino_t = 1;
	a1fs_inode  *curr_inode;
	a1fs_extent *curr_extent;
	a1fs_dentry *curr_dir;
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
		curr_inode = (a1fs_inode *) (inode_table + sizeof(a1fs_inode)*(curr_ino_t - 1));
		// If the path component is not a dir, but we still have some more path components to parse
		if (curr_inode->mode != 'd') {
				printf("ENOTDIR\n");
				return -ENOTDIR;
		}
		curr_extent = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*(curr_inode->extentblock));
		curr_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE*(curr_extent->start));
		dentry_count = curr_inode->dentry_count;
		
		// Search in the current inode 's directory entries to find the next path component
		foundPathCompo = 0;
		for (uint64_t i = 0; i < dentry_count; i++) {
			curr_dentry = (a1fs_dentry *)(curr_dir + (sizeof(a1fs_dentry) * i));
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
	st->f_blocks = sb->size/A1FS_BLOCK_SIZE;
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
	a1fs_inode *inode_table = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*(sb->bg_inode_table));
	printf("GETATTR Path passed is: %s", path);
	if (get_ino_num_by_path(path) < 0) {
		fprintf(stderr, "get_attr ERRROR\n");
		return get_ino_num_by_path(path);
	}
	a1fs_ino_t curr_ino_t = (a1fs_ino_t) get_ino_num_by_path(path);

	a1fs_inode *curr_inode = (inode_table + sizeof(a1fs_inode)*(curr_ino_t - 1));
	// TODO what should I put here for st_mode?
	st->st_mode = curr_inode->mode;
	st->st_nlink = (nlink_t)(curr_inode->links);
	blkcnt_t sectors_used = (blkcnt_t)(curr_inode->size / 512);
	if (curr_inode->size % 512 != 0)
		sectors_used++;
	st->st_blocks = sectors_used;
	st->st_mtime = curr_inode->mtime.tv_sec;
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
	// 	filler(buf, "." , NULL, 0);
	// 	filler(buf, "..", NULL, 0);
	// 	return 0;
	// }
	// (void)fs;
	// return -ENOSYS;
	void *image = fs->image;
	a1fs_superblock *sb = image;
	a1fs_inode *inode_table = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*sb->bg_inode_table);
	
	if (get_ino_num_by_path(path) < 0) {
		fprintf(stderr, "read_dirERORORROORORORORR\n");
		return get_ino_num_by_path(path);
	}
	a1fs_ino_t curr_ino_t = (long) get_ino_num_by_path(path);
	a1fs_inode *curr_inode = (inode_table + sizeof(a1fs_inode)*(curr_ino_t - 1));
	a1fs_extent *curr_extent = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*curr_inode->extentblock);
	a1fs_dentry *curr_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE*curr_extent->start);
	for (uint64_t i = 0; i < curr_inode->dentry_count; i++) {
		filler(buf, curr_dir[i].name, NULL, 0);
	}
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
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.  /local/kkk/mydir
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	printf("\nEntered into mkdir\n");
	fs_ctx *fs = get_fs();

	//TODO
	void *image = fs->image;
	a1fs_superblock *sb = image;
	//if there is no free inode available, return ENOSPC
	if (sb->s_free_inodes_count < 1) {
		return -ENOSPC;
	}
	unsigned char *inode_bitmap = (unsigned char *)(image + sb->bg_inode_bitmap * A1FS_BLOCK_SIZE);
	void *inode_block = (void *)(image + sb->bg_inode_table * A1FS_BLOCK_SIZE);
	a1fs_inode *new_inode;
	a1fs_ino_t inode_num;
	// loop inode bitmap to find an empty spot
    	for (unsigned int bit = 0; bit < sb->s_inodes_count; bit++)
    	{
        	if((inode_bitmap[bit] & (1 << bit)) == 0){  // bit map is 0
				// create a new inode
				new_inode = (void *)(inode_block + bit * sizeof(a1fs_inode));
				setBitOn(inode_bitmap, bit);
				inode_num = bit + 1; // the inode number stored in d entry always + 1, so inode number 0 represents free d entry
			break;
        	}
    	}
	// file mode??
	new_inode->mode = 'd';
	new_inode->links = 0;
	new_inode->size = 0;
	new_inode->dentry_count = 0;
	clock_gettime(CLOCK_REALTIME, &(new_inode->mtime));
	// cut the last component which is the directory name we want to create
	char cpy_path[strlen(path) + 1];
	strcpy(cpy_path, path);
	char *ptr = strrchr(cpy_path, '/');
	if (ptr != NULL){
		*ptr = '\0';
	}
	// get parent directory inode, and modify its info
	if (get_ino_num_by_path(cpy_path) < 0) {
		fprintf(stderr, "Mkdir ERRORRRRRRRRR\n");
		return get_ino_num_by_path(cpy_path);
	}
	a1fs_ino_t parent_directory_ino_num = (long) get_ino_num_by_path(cpy_path);
	a1fs_inode *parent_directory_ino = (a1fs_inode *)(inode_block + parent_directory_ino_num * sizeof(a1fs_inode));
	clock_gettime(CLOCK_REALTIME, &(parent_directory_ino->mtime));
	uint64_t dir_count = parent_directory_ino->dentry_count;
	parent_directory_ino->dentry_count++;
	parent_directory_ino->size += (sizeof(a1fs_dentry));
	a1fs_extent *extentblock = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*new_inode->extentblock);
	// let new directory entry point to the last spot in block
	a1fs_dentry *new_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE * extentblock->start + sizeof(a1fs_dentry) * dir_count);
	uint64_t i = 0;
	// search for a free directory by checking if the ino == 0
	while (i < dir_count){
		a1fs_dentry *cur_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE * extentblock->start + sizeof(a1fs_dentry) * i);
		if (cur_dir->ino == 0){
			new_dir = cur_dir;
		}
		i++;
	}
	new_dir->ino = inode_num;
	// get the directory name we want to create
	char * dir_name;
	char cpy_path1[strlen(path) + 1];
	strcpy(cpy_path1, path);
	int delim = '/';
	dir_name = strrchr(path, delim);
	dir_name++;
	strcpy(new_dir->name, dir_name);
	(void)mode;
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
