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
 * CSC369 Assignment 1 - a1fs types, constants, and data structures header file.
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>


/**
 * a1fs block size in bytes. You are not allowed to change this value.
 *
 * The block size is the unit of space allocation. Each file (and directory)
 * must occupy an integral number of blocks. Each of the file systems metadata
 * partitions, e.g. superblock, inode/block bitmaps, inode table (but not an
 * individual inode) must also occupy an integral number of blocks.
 */
#define A1FS_BLOCK_SIZE 4096
#define BITS_PER_BLOCK (A1FS_BLOCK_SIZE * 8)


/** Block number (block pointer) type. */
typedef uint32_t a1fs_blk_t;

/** Inode number type. */
typedef uint32_t a1fs_ino_t;


/** Magic value that can be used to identify an a1fs image. */
#define A1FS_MAGIC 0xC5C369A1C5C369A1ul


/** Extent - a contiguous range of blocks. */
typedef struct a1fs_extent {
	/** Starting block of the extent. */
	a1fs_blk_t start;
	/** Number of blocks in the extent. */
	a1fs_blk_t count;

} a1fs_extent;


/** a1fs inode. */
typedef struct a1fs_inode {
	/** File mode. */
	mode_t mode;
	/** Reference count (number of hard links). */
	uint32_t links;
	/** File size in bytes. */
	uint64_t size;
	/**
	 * Last modification timestamp.
	 *
	 * Use the CLOCK_REALTIME clock; see "man 3 clock_gettime". Must be updated
	 * when the file (or directory) is created, written to, or its size changes.
	 */
	struct timespec mtime;
	//number of extents
	unsigned short extentcount;
	//extent block
	a1fs_blk_t extentblock;
	//directory entry count
	uint64_t dentry_count;
	char padding[10];
} a1fs_inode;

// A single block must fit an integral number of inodes
static_assert(A1FS_BLOCK_SIZE % sizeof(a1fs_inode) == 0, "invalid inode size");


/** a1fs superblock. */
typedef struct a1fs_superblock {
	/** Must match A1FS_MAGIC. */
	uint64_t magic;
	/** File system size in bytes. */
	uint64_t size;

	unsigned int   s_inodes_count;      /* Inodes count */
	unsigned int   s_blocks_count;      /* Blocks count */
	unsigned int   s_free_blocks_count; /* Free data blocks count */
	unsigned int   s_free_inodes_count; /* Free inodes count */
	a1fs_blk_t     bg_block_bitmap;     /* Data block bitmap block number*/
	unsigned int   block_bitmap_count;	/* Data block bitmap block count */
	a1fs_blk_t     bg_inode_bitmap;     /* Inodes bitmap block number */
	unsigned int   inode_bitmap_count;  /* Inodes bitmap block count */
	a1fs_blk_t     bg_inode_table;      /* Inodes table block number*/
	unsigned int   inode_table_count;   /* Inodes table count */
	a1fs_blk_t     bg_data_block;       /* First data block number */
	unsigned int   data_block_count;    /* Data block count */
} a1fs_superblock;

// Superblock must fit into a single block
static_assert(sizeof(a1fs_superblock) <= A1FS_BLOCK_SIZE,
              "superblock is too large");


/** Maximum file name (path component) length. Includes the null terminator. */
#define A1FS_NAME_MAX 252

/** Maximum file path length. Includes the null terminator. */
#define A1FS_PATH_MAX PATH_MAX

/** Fixed size directory entry structure. */
typedef struct a1fs_dentry {
	/** Inode number. */
	a1fs_ino_t ino;
	/** File name. A null-terminated string. */
	char name[A1FS_NAME_MAX];

} a1fs_dentry;

static_assert(sizeof(a1fs_dentry) == 256, "invalid dentry size");
