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
/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}

a1fs_ino_t get_ino_num_by_path(const char *path) {
	
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	// get the address to the beginning of file system
	fs_ctx *fs = get_fs();
	void *image = fs->image;
	a1fs_superblock *sb = image;
	a1fs_inode *inode_table = (a1fs_inode *)(image + A1FS_BLOCK_SIZE*sb->bg_inode_table);
	
	// TODO: For Step 2, we initially assume that dentry_count is small enough
	// 		 so that all dentry are stored in one block, but we actually would
	//       have to look into other blocks within the same extent, or even
	//       other extents.

	a1fs_ino_t curr_ino_t = 0;
	a1fs_inode *curr_inode;
	a1fs_extent *curr_extent;
	a1fs_dentry *curr_dir;
	uint32_t dentry_count;
	char foundPathCompo;
	a1fs_dentry *curr_dentry;

	// make of a copy to the path, since strtok is destructive
	char cpy_path[strlen(path) + 1];
	strcpy(cpy_path, path);
	char delim[] = "/";
	// Use strtok to get each path compoenent
	char *pathComponent = strtok(cpy_path, delim);
	// Using do-while loop since curr_inode would be root inode initially, thus
	// iterating at least once.
	do {  // Iterate to the inode given by absolute path
		curr_inode = (inode_table + sizeof(a1fs_inode)*curr_ino_t);
		if (curr_inode->mode == 'd')
			return -ENOTDIR;
		curr_extent = (a1fs_extent *) (image + A1FS_BLOCK_SIZE*curr_inode->extentblock);
		curr_dir = (a1fs_dentry *) (image + A1FS_BLOCK_SIZE*curr_extent->start);
		dentry_count = curr_inode->dentry_count;
		
		foundPathCompo = 0;
		for (uint32_t i = 0; i < dentry_count; i++)
		{
			curr_dentry = (a1fs_dentry *)(curr_dir + (sizeof(a1fs_dentry) * i));
			if (strcmp(curr_dentry->name, pathComponent) == 0)
			{
				foundPathCompo = 1;
				curr_ino_t = curr_dentry->ino;
				break;
			}
		}
		if (!foundPathCompo)
			return -ENOENT;
		pathComponent = strtok(NULL, delim);
	} while (pathComponent != NULL);

	return curr_ino_t;
}

int main()
{
	a1fs_ino_t curr_ino_t = get_ino_num_by_path("/");
	printf("root inode path is: %d\n", curr_ino_t);
	// printf("%ld", sizeof(a1fs_blk_t));
	// char str[] = "/strtok/needs/to";
	// char delim[] = "/";

	// char *ptr = strtok(str, delim);

	// while(ptr != NULL)
	// {
	// 	printf("'%s'\n", ptr);
	// 	ptr = strtok(NULL, delim);
	// }
	// printf("\n");
	// printf("%ld", sizeof(a1fs_inode));
	return 0;
}