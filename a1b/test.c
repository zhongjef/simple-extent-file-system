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

int main()
{
	printf("%ld", sizeof(a1fs_blk_t));
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