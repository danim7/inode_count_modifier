/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000 by Theodore Ts'o
 *
 * increase_inode_count.c --- increase the inode count of an existing ext4 filesystem
 * 
 * Copyright (C) 2025 by danim7 (https://github.com/danim7)
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * Increasing the inode count consists of the following phases:
 *
 *	1.  Re-allocate inode tables 
 *          1.a. If there is no contiguous areas for the new itables, move some data blocks to make room for it
 *          1.b. Update references to those data blocks in the affected files/folders
 *      2.  Move the inodes to the new itables
 *      3.  Free the space of the old itables
 *      4.  Update group stats
 */

#include "config.h"
#include "resize2fs.h"
#include <time.h>

#ifdef __linux__			/* Kludge for debugging */
#define RESIZE2FS_DEBUG
#endif

static errcode_t block_mover(ext2_resize_t rfs);
static errcode_t inode_scan_and_fix(ext2_resize_t rfs);
static errcode_t move_itables(ext2_resize_t rfs);
static errcode_t inode_relocation_to_bigger_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group);
static errcode_t make_room_for_new_itables(ext2_resize_t rfs, unsigned int *need_block_mover);

errcode_t increase_inode_count(ext2_filsys fs, int flags,
	    errcode_t (*progress)(ext2_resize_t rfs, int pass,
					  unsigned long cur,
					  unsigned long max_val), unsigned int new_inodes_per_group)
{
	ext2_resize_t	rfs;
	errcode_t	retval;
	struct resource_track	rtrack, overall_track;

	/*
	 * Create the data structure
	 */
	retval = ext2fs_get_mem(sizeof(struct ext2_resize_struct), &rfs);
	if (retval)
		return retval;

	memset(rfs, 0, sizeof(struct ext2_resize_struct));
	fs->priv_data = rfs;
	rfs->old_fs = fs;
	rfs->flags = flags;
	rfs->itable_buf	 = 0;
	rfs->progress = progress;

	init_resource_track(&overall_track, "overall resize2fs", fs->io);
	init_resource_track(&rtrack, "read_bitmaps", fs->io);
	retval = ext2fs_read_bitmaps(fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	fs->super->s_state |= EXT2_ERROR_FS;
	ext2fs_mark_super_dirty(fs);
	ext2fs_flush(fs);

	init_resource_track(&rtrack, "fix_uninit_block_bitmaps 1", fs->io);
	fix_uninit_block_bitmaps(fs);
	print_resource_track(rfs, &rtrack, fs->io);
	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval)
		goto errout;

	/*init_resource_track(&rtrack, "resize_group_descriptors", fs->io);
	retval = resize_group_descriptors(rfs, *new_size);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);
	*/
	init_resource_track(&rtrack, "inode_relocation_to_bigger_tables", fs->io);
	retval = inode_relocation_to_bigger_tables(rfs, new_inodes_per_group);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	/*init_resource_track(&rtrack, "move_bg_metadata", fs->io);
	retval = move_bg_metadata(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "zero_high_bits_in_metadata", fs->io);
	retval = zero_high_bits_in_inodes(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "adjust_superblock", fs->io);
	retval = adjust_superblock(rfs, *new_size);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_uninit_block_bitmaps 2", fs->io);
	fix_uninit_block_bitmaps(rfs->new_fs);
	print_resource_track(rfs, &rtrack, fs->io);
	// Clear the block bitmap uninit flag for the last block group 
	ext2fs_bg_flags_clear(rfs->new_fs, rfs->new_fs->group_desc_count - 1,
			     EXT2_BG_BLOCK_UNINIT);

	*new_size = ext2fs_blocks_count(rfs->new_fs->super);

	init_resource_track(&rtrack, "blocks_to_move", fs->io);
	retval = blocks_to_move(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

#ifdef RESIZE2FS_DEBUG
	if (rfs->flags & RESIZE_DEBUG_BMOVE)
		printf("Number of free blocks: %llu/%llu, Needed: %llu\n",
		       (unsigned long long) ext2fs_free_blocks_count(rfs->old_fs->super),
		       (unsigned long long) ext2fs_free_blocks_count(rfs->new_fs->super),
		       (unsigned long long) rfs->needed_blocks);
#endif

	init_resource_track(&rtrack, "block_mover", fs->io);
	retval = block_mover(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "inode_scan_and_fix", fs->io);
	retval = inode_scan_and_fix(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "inode_ref_fix", fs->io);
	retval = inode_ref_fix(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "move_itables", fs->io);
	retval = move_itables(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	retval = clear_sparse_super2_last_group(rfs);
	if (retval)
		goto errout;

	init_resource_track(&rtrack, "calculate_summary_stats", fs->io);
	retval = resize2fs_calculate_summary_stats(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_resize_inode", fs->io);
	retval = fix_resize_inode(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);

	init_resource_track(&rtrack, "fix_orphan_file_inode", fs->io);
	retval = fix_orphan_file_inode(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);
*/
	init_resource_track(&rtrack, "fix_sb_journal_backup", fs->io);
	retval = fix_sb_journal_backup(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);


	retval = ext2fs_set_gdt_csum(rfs->new_fs);

	if (retval)
		goto errout;

	rfs->new_fs->super->s_state &= ~EXT2_ERROR_FS;
	rfs->new_fs->flags &= ~EXT2_FLAG_MASTER_SB_ONLY;

	print_resource_track(rfs, &overall_track, fs->io);

	retval = ext2fs_close_free(&rfs->new_fs);
	if (retval)
		goto errout;

	rfs->flags = flags;

	ext2fs_free(rfs->old_fs);
	rfs->old_fs = NULL;
	if (rfs->itable_buf)
		ext2fs_free_mem(&rfs->itable_buf);
	if (rfs->reserve_blocks)
		ext2fs_free_block_bitmap(rfs->reserve_blocks);
	if (rfs->move_blocks)
		ext2fs_free_block_bitmap(rfs->move_blocks);
	ext2fs_free_mem(&rfs);

	return 0;

errout:
	if (rfs->new_fs) {
		ext2fs_free(rfs->new_fs);
		rfs->new_fs = NULL;
	}
	if (rfs->itable_buf)
		ext2fs_free_mem(&rfs->itable_buf);
	ext2fs_free_mem(&rfs);
	return retval;
}




/*
 * This helper function creates a block bitmap with all of the
 * filesystem meta-data blocks.
 */
static errcode_t mark_table_blocks(ext2_filsys fs,
				   ext2fs_block_bitmap bmap)
{
	dgrp_t			i;
	blk64_t			blk;

	for (i = 0; i < fs->group_desc_count; i++) {
		ext2fs_reserve_super_and_bgd(fs, i, bmap);

		/*
		 * Mark the blocks used for the inode table
		 */
		blk = ext2fs_inode_table_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap_range2(bmap, blk,
						fs->inode_blocks_per_group);

		/*
		 * Mark block used for the block bitmap
		 */
		blk = ext2fs_block_bitmap_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap2(bmap, blk);

		/*
		 * Mark block used for the inode bitmap
		 */
		blk = ext2fs_inode_bitmap_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap2(bmap, blk);
	}
	/* Reserve the MMP block */
	if (ext2fs_has_feature_mmp(fs->super) &&
	    fs->super->s_mmp_block > fs->super->s_first_data_block &&
	    fs->super->s_mmp_block < ext2fs_blocks_count(fs->super))
		ext2fs_mark_block_bitmap2(bmap, fs->super->s_mmp_block);
	return 0;
}


/*
 * This helper function tries to allocate a new block.  We try to
 * avoid hitting the original group descriptor blocks at least at
 * first, since we want to make it possible to recover from a badly
 * aborted resize operation as much as possible.
 *
 * In the future, I may further modify this routine to balance out
 * where we get the new blocks across the various block groups.
 * Ideally we would allocate blocks that corresponded with the block
 * group of the containing inode, and keep contiguous blocks
 * together.  However, this very difficult to do efficiently, since we
 * don't have the necessary information up front.
 */

#define AVOID_OLD	1
#define DESPERATION	2

static void init_block_alloc(ext2_resize_t rfs)
{
	rfs->alloc_state = AVOID_OLD;
	rfs->new_blk = rfs->new_fs->super->s_first_data_block;
#if 0
	/* HACK for testing */
	if (ext2fs_blocks_count(rfs->new_fs->super) >
	    ext2fs_blocks_count(rfs->old_fs->super))
		rfs->new_blk = ext2fs_blocks_count(rfs->old_fs->super);
#endif
}

static blk64_t get_new_block(ext2_resize_t rfs)
{
	ext2_filsys	fs = rfs->new_fs;
fs = rfs->old_fs;
	while (1) {
		if (rfs->new_blk >= ext2fs_blocks_count(fs->super)) {
			if (rfs->alloc_state == DESPERATION)
				return 0;

#ifdef RESIZE2FS_DEBUG
			if (rfs->flags & RESIZE_DEBUG_BMOVE)
				printf("Going into desperation mode "
				       "for block allocations\n");
#endif
			rfs->alloc_state = DESPERATION;
			rfs->new_blk = fs->super->s_first_data_block;
			continue;
		}
		if (ext2fs_test_block_bitmap2(fs->block_map, rfs->new_blk) ||
		    ext2fs_test_block_bitmap2(rfs->reserve_blocks,
					     rfs->new_blk) ||
		    ((rfs->alloc_state == AVOID_OLD) &&
		     (rfs->new_blk < ext2fs_blocks_count(rfs->old_fs->super)) &&
		     ext2fs_test_block_bitmap2(rfs->old_fs->block_map,
					      rfs->new_blk))) {
			rfs->new_blk++;
			continue;
		}
		return rfs->new_blk;
	}
}

static errcode_t resize2fs_get_alloc_block(ext2_filsys fs,
					   blk64_t goal EXT2FS_ATTR((unused)),
					   blk64_t *ret)
{
	ext2_resize_t rfs = (ext2_resize_t) fs->priv_data;
	blk64_t blk;
	int group;

	blk = get_new_block(rfs);
	if (!blk)
		return ENOSPC;

/*#ifdef RESIZE2FS_DEBUG
	if (rfs->flags & 0xF)*/
		printf("get_alloc_block allocating %llu\n",
		       (unsigned long long) blk);
/*#endif*/

	ext2fs_mark_block_bitmap2(rfs->old_fs->block_map, blk);
	ext2fs_mark_block_bitmap2(rfs->new_fs->block_map, blk);

	group = ext2fs_group_of_blk2(rfs->old_fs, blk);
	ext2fs_clear_block_uninit(rfs->old_fs, group);
	group = ext2fs_group_of_blk2(rfs->new_fs, blk);
	ext2fs_clear_block_uninit(rfs->new_fs, group);

	*ret = (blk64_t) blk;
	return 0;
}


static errcode_t resize2fs_get_alloc_block_stats_update(ext2_filsys fs,
					   blk64_t goal EXT2FS_ATTR((unused)),
					   blk64_t *ret)
{
	ext2_resize_t rfs = (ext2_resize_t) fs->priv_data;
	blk64_t blk;
	int group;

	blk = get_new_block(rfs);
	if (!blk)
		return ENOSPC;

/*#ifdef RESIZE2FS_DEBUG
	if (rfs->flags & 0xF)*/
		printf("get_alloc_block_stats_update allocating %llu\n",
		       (unsigned long long) blk);
/*#endif*/

	ext2fs_mark_block_bitmap2(rfs->old_fs->block_map, blk);
	ext2fs_mark_block_bitmap2(rfs->new_fs->block_map, blk);
	


	group = ext2fs_group_of_blk2(rfs->old_fs, blk);
	ext2fs_clear_block_uninit(rfs->old_fs, group);
	group = ext2fs_group_of_blk2(rfs->new_fs, blk);
	ext2fs_clear_block_uninit(rfs->new_fs, group);
	
	ext2fs_block_alloc_stats2(rfs->old_fs, blk, +1);
	ext2fs_block_alloc_stats2(rfs->new_fs, blk, +1);

	*ret = (blk64_t) blk;
	return 0;
}

static errcode_t block_mover(ext2_resize_t rfs)
{
	blk64_t			blk, old_blk, new_blk;
	ext2_filsys		fs = rfs->new_fs;
	ext2_filsys		old_fs = rfs->old_fs;
	errcode_t		retval;
	__u64			c, size;
	int			to_move, moved;
	ext2_badblocks_list	badblock_list = 0;
	int			bb_modified = 0;
	
	fs = rfs->old_fs;

	fs->get_alloc_block = resize2fs_get_alloc_block;
	old_fs->get_alloc_block = resize2fs_get_alloc_block;

	retval = ext2fs_read_bb_inode(old_fs, &badblock_list);
	if (retval)
		return retval;

	new_blk = fs->super->s_first_data_block;
	if (!rfs->itable_buf) {
		retval = ext2fs_get_array(fs->blocksize,
					fs->inode_blocks_per_group,
					&rfs->itable_buf);
		if (retval)
			goto errout;
	}
	retval = ext2fs_create_extent_table(&rfs->bmap, 0);
	if (retval)
		goto errout;

	/*
	 * The first step is to figure out where all of the blocks
	 * will go.
	 */
	to_move = moved = 0;
	init_block_alloc(rfs);
	for (blk = B2C(old_fs->super->s_first_data_block);
	     blk < ext2fs_blocks_count(old_fs->super);
	     blk += EXT2FS_CLUSTER_RATIO(fs)) {
		if (!ext2fs_test_block_bitmap2(old_fs->block_map, blk))
			continue;
		if (!ext2fs_test_block_bitmap2(rfs->move_blocks, blk))
			continue;
		if (ext2fs_badblocks_list_test(badblock_list, blk)) {
			ext2fs_badblocks_list_del(badblock_list, blk);
			bb_modified++;
			continue;
		}

		new_blk = get_new_block(rfs);
		if (!new_blk) {
		        printf("block_mover ENOSPC\n");
			retval = ENOSPC;
			goto errout;
		}
		ext2fs_block_alloc_stats2(rfs->new_fs, new_blk, +1);
		ext2fs_block_alloc_stats2(rfs->old_fs, new_blk, +1);
		ext2fs_add_extent_entry(rfs->bmap, B2C(blk), B2C(new_blk));
		to_move++;
	}

	if (to_move == 0) {
		if (rfs->bmap) {
			ext2fs_free_extent_table(rfs->bmap);
			rfs->bmap = 0;
		}
		retval = 0;
		goto errout;
	}

	/*
	 * Step two is to actually move the blocks
	 */
	retval =  ext2fs_iterate_extent(rfs->bmap, 0, 0, 0);
	if (retval) goto errout;

	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_BLOCK_RELOC_PASS,
					 0, to_move);
		if (retval)
			goto errout;
	}
	while (1) {
		retval = ext2fs_iterate_extent(rfs->bmap, &old_blk, &new_blk, &size);
		if (retval) goto errout;
		if (!size)
			break;
		old_blk = C2B(old_blk);
		new_blk = C2B(new_blk);
		size = C2B(size);
//#ifdef RESIZE2FS_DEBUG
//		if (rfs->flags & RESIZE_DEBUG_BMOVE)
			printf("Moving %llu blocks %llu->%llu\n",
			       (unsigned long long) size,
			       (unsigned long long) old_blk,
			       (unsigned long long) new_blk);
//#endif
		do {
			c = size;
			if (c > fs->inode_blocks_per_group)
				c = fs->inode_blocks_per_group;
			retval = io_channel_read_blk64(fs->io, old_blk, c,
						       rfs->itable_buf);
			if (retval) goto errout;
			retval = io_channel_write_blk64(fs->io, new_blk, c,
							rfs->itable_buf);
			if (retval) goto errout;
			//ext2fs_block_alloc_stats2(fs, new_blk, +1);
			ext2fs_block_alloc_stats_range(rfs->new_fs, old_blk, c, -1);
			ext2fs_block_alloc_stats_range(rfs->old_fs, old_blk, c, -1);
			size -= c;
			new_blk += c;
			old_blk += c;
			moved += c;
			if (rfs->progress) {
				retval = (rfs->progress)(rfs,
						E2_RSZ_BLOCK_RELOC_PASS,
						moved, to_move);
				if (retval)
					goto errout;
			}
		} while (size > 0);
	}

	io_channel_flush(fs->io);

errout:
	if (badblock_list) {
		if (!retval && bb_modified)
			retval = ext2fs_update_bb_inode(old_fs,
							badblock_list);
		ext2fs_badblocks_list_free(badblock_list);
	}
	return retval;
}


int update_block_reference(ext2_filsys fs, blk64_t	*block_nr,
			 e2_blkcnt_t blockcnt,
			 blk64_t ref_block EXT2FS_ATTR((unused)),
			 int ref_offset EXT2FS_ATTR((unused)), void *priv_data)
{
	struct process_block_struct *pb;
	errcode_t	retval;
	blk64_t		block, new_block;
	int		ret = 0;

	pb = (struct process_block_struct *) priv_data;
	block = *block_nr;
	if (pb->rfs->bmap) {
		new_block = extent_translate(fs, pb->rfs->bmap, block);
		if (new_block) {
			*block_nr = new_block;
			ret |= BLOCK_CHANGED;
			pb->changed = 1;
//#ifdef RESIZE2FS_DEBUG
//			if (pb->rfs->flags & RESIZE_DEBUG_BMOVE)
				printf("ino=%u, blockcnt=%lld, %llu->%llu\n",
				       pb->old_ino, (long long) blockcnt,
				       (unsigned long long) block,
				       (unsigned long long) new_block);
//#endif
			block = new_block;
		}
	}
	return ret;
}


static errcode_t inode_scan_and_fix(ext2_resize_t rfs)
{
	struct process_block_struct	pb;
	ext2_ino_t		ino, new_inode;
	struct ext2_inode 	*inode = NULL;
	ext2_inode_scan 	scan = NULL;
	errcode_t		retval;
	char			*block_buf = 0;
	int			inode_size;


	set_com_err_hook(quiet_com_err_proc);

	retval = ext2fs_open_inode_scan(rfs->old_fs, 0, &scan);
	if (retval) goto errout;

	retval = ext2fs_get_array(rfs->old_fs->blocksize, 3, &block_buf);
	if (retval) goto errout;


	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_SCAN_PASS,
					 0, rfs->old_fs->group_desc_count);
		if (retval)
			goto errout;
	}
	ext2fs_set_inode_callback(scan, progress_callback, (void *) rfs);
	pb.rfs = rfs;
	pb.inode = inode;
	pb.error = 0;
	new_inode = EXT2_FIRST_INODE(rfs->new_fs->super);
	inode_size = EXT2_INODE_SIZE(rfs->new_fs->super);
	inode = malloc(inode_size);
	if (!inode) {
		retval = ENOMEM;
		goto errout;
	}

	while (1) {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode, inode_size);
		if (retval) goto errout;
		if (!ino)
			break;

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue; /* inode not in use */

		pb.changed = 0;

		/* Remap EA block */
		retval = migrate_ea_block(rfs, ino, inode, &pb.changed);
		if (retval)
			goto errout;

		new_inode = ino;

		if (pb.changed)
			retval = ext2fs_write_inode_full(rfs->old_fs,
							 new_inode,
							 inode, inode_size);
		if (retval)
			goto errout;

		/*
		 * Update inodes to point to new blocks
		 */
		rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		if (ext2fs_inode_has_valid_blocks2(rfs->old_fs, inode) &&
		    (rfs->bmap)) {
			pb.ino = new_inode;
			pb.old_ino = ino;
			pb.has_extents = inode->i_flags & EXT4_EXTENTS_FL;
			retval = ext2fs_block_iterate3(rfs->old_fs,
						       new_inode, 0, block_buf,
						       update_block_reference, &pb);
			if (retval)
				goto errout;
			if (pb.error) {
				retval = pb.error;
				goto errout;
			}
		}
	}

	io_channel_flush(rfs->old_fs->io);

errout:
	reset_com_err_hook();
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (rfs->bmap) {
		ext2fs_free_extent_table(rfs->bmap);
		rfs->bmap = 0;
	}
	if (scan)
		ext2fs_close_inode_scan(scan);
	if (block_buf)
		ext2fs_free_mem(&block_buf);
	free(inode);
	return retval;
}



static errcode_t inode_relocation_to_bigger_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group) {


	blk64_t		itable_start;
	errcode_t	retval;
	ext2_filsys 	fs;
	ext2_ino_t ino_num = 0;
	dgrp_t group = 0; 
	unsigned int itable, total_inodes_free;
	unsigned int *dir_count, *free_inode_count, *need_block_mover;
	struct ext2_inode 	*inode = NULL;
	int inode_size = 0, len;
	int retry_alloc_itables = 0;
	char * inode_bitmap;
	
	inode_size = EXT2_INODE_SIZE(rfs->new_fs->super);
	inode = malloc(inode_size);
	if (!inode) {
		retval = ENOMEM;
		goto errout;
	}

	fs = rfs->new_fs;


	rfs->new_fs->super->s_inodes_per_group = new_inodes_per_group;
        rfs->new_fs->inode_blocks_per_group = ext2fs_div_ceil(rfs->new_fs->super->s_inodes_per_group * rfs->new_fs->super->s_inode_size, rfs->new_fs->blocksize);
        rfs->new_fs->group_desc_count = rfs->old_fs->group_desc_count;
        rfs->new_fs->super->s_inodes_count = rfs->new_fs->group_desc_count * rfs->new_fs->super->s_inodes_per_group;


        printf("old_fs->inode_blocks_per_group: %u\n", rfs->old_fs->inode_blocks_per_group);
        printf("old_fs->super->s_inodes_per_group: %u\n", rfs->old_fs->super->s_inodes_per_group);
        printf("old_fs->super->s_inode_size: %u\n", rfs->old_fs->super->s_inode_size);
        printf("old_fs->inode_blocks_per_group: %u\n", rfs->old_fs->inode_blocks_per_group);
        printf("old_fs->blocksize: %u\n", rfs->old_fs->blocksize);
        printf("old_fs->super->s_log_block_size: %u\n", rfs->old_fs->super->s_log_block_size);
        printf("old_fs->super->s_inodes_count: %u\n", rfs->old_fs->super->s_inodes_count);
        printf("\n\n");
        printf("new_fs->inode_blocks_per_group: %u\n", rfs->new_fs->inode_blocks_per_group);
        printf("new_fs->super->s_inodes_per_group: %u\n", rfs->new_fs->super->s_inodes_per_group);
        printf("new_fs->super->s_inode_size: %u\n", rfs->new_fs->super->s_inode_size);
        printf("new_fs->inode_blocks_per_group: %u\n", rfs->new_fs->inode_blocks_per_group);
        printf("new_fs->blocksize: %u\n", rfs->new_fs->blocksize);
        printf("new_fs->super->s_log_block_size: %u\n", rfs->new_fs->super->s_log_block_size);
        printf("new_fs->super->s_inodes_count: %u\n", rfs->new_fs->super->s_inodes_count);
        printf("EXT2_FIRST_INODE(new_fs->super): %u\n", EXT2_FIRST_INODE(rfs->new_fs->super));
        printf("new_fs->super->s_log_groups_per_flex: %u\n", rfs->new_fs->super->s_log_groups_per_flex);

	need_block_mover = (unsigned int*) calloc(rfs->new_fs->group_desc_count, sizeof(unsigned int));
        if (need_block_mover == NULL) {
              printf("alloc_error need_block_mover\n");
              goto errout;
        }
alloc_itables:

	for (group = 0; group < fs->group_desc_count; group++) {

		if (retry_alloc_itables == 0 ||
		    (retry_alloc_itables == 1 && need_block_mover[group])) {
		        need_block_mover[group] = 0;
			ext2fs_inode_table_loc_set(fs, group, 0);
			
			retval = ext2fs_allocate_group_table(fs, group, rfs->old_fs->block_map);
			if (retval) {
			        printf("unsuccessful ext2fs_allocate_group_table for group %u with retval %li%s\n", group, retval, retry_alloc_itables?"":" - will retry later");
			        need_block_mover[group]++;

			} else {
			      itable_start = ext2fs_inode_table_loc(fs,group);
			      len = fs->inode_blocks_per_group;
			      ext2fs_mark_block_bitmap_range2(fs->block_map, itable_start, len);
			      retval = ext2fs_zero_blocks2(fs, itable_start, len, &itable_start, &len);
                              if (retval) {
                                      fprintf(stderr, _("\nCould not write %d "
                                                "blocks in inode table starting at %llu: %s\n"),
                                              len, (unsigned long long) itable_start, error_message(retval));
                                      exit(1);
                              }
			      printf("successful ext2fs_allocate_group_table for group %u with retval %li in block %llu\n", group, retval, itable_start);
			}
		}
	}
	
	retval = 0;	
	for (group=0; group < fs->group_desc_count; group++) {
	    printf(" group %u need block_mover %u\n", group, need_block_mover[group]);
	    retval += need_block_mover[group];
	}
	if (retval) {
	      if (retry_alloc_itables) {
	          printf("already tried to move data blocks and realloc itables\n");
	          retval = -1;
	          goto errout;
	      }
	      retval = make_room_for_new_itables(rfs, need_block_mover);
	      if (retval) {
	          goto errout;
	      }
	      retry_alloc_itables = 1;
	      goto alloc_itables;
		
	}

	
	dir_count = (unsigned int*) calloc(rfs->new_fs->group_desc_count, sizeof(unsigned int));
        if (dir_count == NULL) {
              printf("alloc_error dir_count\n");
              goto errout;
        }
        free_inode_count = (unsigned int*) calloc(rfs->new_fs->group_desc_count, sizeof(unsigned int));
        if (free_inode_count == NULL) {
              printf("alloc_error free_inode_count\n");
              goto errout;
        }
	total_inodes_free = 0;
	for (ino_num = 1; ino_num<=rfs->old_fs->super->s_inodes_count; ino_num++) {
	
	        retval = ext2fs_read_inode_full(rfs->old_fs, ino_num, inode, inode_size);
	        group = (ino_num - 1) / rfs->new_fs->super->s_inodes_per_group;
	        printf("Moving inodes to the new itable: read inode: %u, links_count: %u, group: %u, retval: %li\n", ino_num, inode->i_links_count, group, retval);
	        if (retval)
	            goto errout;

	        if (inode->i_links_count == 0 && ino_num >= EXT2_FIRST_INODE(rfs->new_fs->super)) {
	                free_inode_count[group]++;
	                total_inodes_free++;

		} else {
			if (LINUX_S_ISDIR(inode->i_mode))
	                    dir_count[group]++;
		        //ext2fs_inode_alloc_stats2(rfs->old_fs, ino_num, -1, LINUX_S_ISDIR(inode->i_mode)!=0?1:0);  
		        ext2fs_inode_alloc_stats2(rfs->new_fs, ino_num, +1, LINUX_S_ISDIR(inode->i_mode)!=0?1:0);
	        }
	        retval = ext2fs_write_inode_full(rfs->new_fs, ino_num, inode, inode_size);
	        printf("Moving inodes to the new itable: written inode: %u, i_size_lo: %u, i_blocks: %u, retval: %li\n", ino_num, inode->i_size,inode->i_blocks, retval);
	        if (retval) goto errout;
	        
	        if (ext2fs_has_feature_metadata_csum(rfs->new_fs->super) &&
		    (inode->i_flags & EXT4_EXTENTS_FL)) {
			retval = ext2fs_fix_extents_checksums(rfs->new_fs, ino_num, NULL);
			if (retval)
				goto errout;
		}
	
	}
	
	/*the first inode after the new end...does it belong to the last group treated?
	if so, we need to finish fixing the inode and dir info for that group*/
        if (group == (ino_num - 1) / rfs->new_fs->super->s_inodes_per_group) {
            printf("completing for last group with used inodes. group: %u, ino_num: %u, adding: %u\n",
                    group, ino_num, rfs->new_fs->super->s_inodes_per_group - (ino_num - 1) % rfs->new_fs->super->s_inodes_per_group);
            free_inode_count[group]+=rfs->new_fs->super->s_inodes_per_group - (ino_num - 1) % rfs->new_fs->super->s_inodes_per_group;
            total_inodes_free+=rfs->new_fs->super->s_inodes_per_group - (ino_num - 1) % rfs->new_fs->super->s_inodes_per_group;
        }/*the inodes in the remaining groups shall all be free*/
        for (group++;group<rfs->new_fs->group_desc_count; group++)
            total_inodes_free+=free_inode_count[group]=rfs->new_fs->super->s_inodes_per_group;
        
	retval = ext2fs_resize_inode_bitmap2(rfs->new_fs->super->s_inodes_count, rfs->new_fs->super->s_inodes_count, rfs->new_fs->inode_map);
	//printf("ext2fs_resize_inode_bitmap bmap->magic: %li, retval: %li\n", rfs->new_fs->inode_map->magic, retval);
	
	io_channel_flush(rfs->old_fs->io);

	for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
	      if (ext2fs_inode_table_loc(rfs->new_fs, group) == ext2fs_inode_table_loc(rfs->old_fs, group)) {
	            printf("group %u, same itables loc old and new\n", group);
	      } else {
	            printf("group %u, different itables loc old and new\n", group);
	            itable_start = ext2fs_inode_table_loc(rfs->old_fs, group);
	            ext2fs_block_alloc_stats_range(rfs->new_fs, itable_start, rfs->old_fs->inode_blocks_per_group, -1);
	      }
        
	      if (ext2fs_bg_used_dirs_count(rfs->new_fs, group) == dir_count[group]) {
	            printf("Group %u, dir_count matches: %u\n", group, dir_count[group]);
	      } else {
	            printf("Group %u, dir_count does not match: %u vs in descriptor %u\n", group, dir_count[group], ext2fs_bg_used_dirs_count(rfs->new_fs, group));
	            ext2fs_bg_used_dirs_count_set(rfs->new_fs, group, dir_count[group]);
	            ext2fs_group_desc_csum_set(rfs->new_fs, group);
	      }
	      if (ext2fs_bg_free_inodes_count(rfs->new_fs, group) == free_inode_count[group]) {
	            printf("Group %u, free_inode_count matches: %u\n", group, free_inode_count[group]);
	      } else {
	            printf("Group %u, free_inode_count does not match: %u vs in descriptor %u\n", group, free_inode_count[group], ext2fs_bg_free_inodes_count(rfs->new_fs, group));
	            ext2fs_bg_free_inodes_count_set(rfs->new_fs, group, free_inode_count[group]);
	            ext2fs_group_desc_csum_set(rfs->new_fs, group);
	      }
	}
	rfs->new_fs->super->s_free_inodes_count = total_inodes_free;
	ext2fs_mark_super_dirty(rfs->new_fs);
	io_channel_flush(rfs->new_fs->io);

errout:

	return retval;
}



static errcode_t make_room_for_new_itables(ext2_resize_t rfs, unsigned int *need_block_mover) {
	unsigned int	j;
	int             flexbg_size = 0, retried_from_beginning = 0;
	dgrp_t		g;
	blk64_t		blk, blk2, first_blk, last_blk;
	errcode_t	retval;
	ext2_filsys 	fs;
	ext2fs_block_bitmap	meta_bmap;
	
	fs = rfs->old_fs;
		
		
	retval = ext2fs_allocate_block_bitmap(fs, _("blocks to be moved"),
					      &rfs->move_blocks);
	if (retval)
		return retval;
		
	retval = ext2fs_allocate_block_bitmap(fs, _("reserved blocks"),
					      &rfs->reserve_blocks);
	if (retval)
		return retval;	

	retval = ext2fs_allocate_block_bitmap(fs, _("meta-data blocks"),
					      &meta_bmap);
	if (retval)
		return retval;

	retval = mark_table_blocks(rfs->old_fs, meta_bmap); //mark as used in meta_bmap the SB, BGD, reserved GDT, bitmaps, itables and MMP from the OLD FS
	if (retval)
		return retval;

	for (g = 0; g < fs->group_desc_count; g++) {
		blk = ext2fs_inode_table_loc(rfs->new_fs, g);
		if (blk) {
			ext2fs_mark_block_bitmap_range2(meta_bmap, blk,	rfs->new_fs->inode_blocks_per_group);
			printf("mark in meta_bmap for group %u the inode blocks %llu to %llu\n", g, blk, blk+rfs->new_fs->inode_blocks_per_group);
		}
	}
	
	flexbg_size = 1U << fs->super->s_log_groups_per_flex;

	for (g = 0; g < fs->group_desc_count; g++) {
	
	    if (ext2fs_has_feature_flex_bg(fs->super)) {
	            if (!(g % (1U << fs->super->s_log_groups_per_flex))) {
	                first_blk = ext2fs_group_first_block2(fs, g & ~(flexbg_size - 1));
	                last_blk = (g|(flexbg_size - 1)>=fs->group_desc_count-1) ?
	                             ext2fs_blocks_count(rfs->old_fs->super)-1 :
	                             ext2fs_group_first_block2(fs, (g|(flexbg_size - 1))+1)-1;
	                retried_from_beginning = 0;
	            }
	    }
	    if (need_block_mover[g] == 0) {
			printf(" --->no need to make room for a new itable for group %u\n", g);

	   } else {
	        if (!ext2fs_has_feature_flex_bg(fs->super)) {
	            first_blk = ext2fs_group_first_block2(fs, g);
	            last_blk = (g == fs->group_desc_count-1) ?
	                        ext2fs_blocks_count(rfs->old_fs->super)-1 :
	                        ext2fs_group_first_block2(fs, g+1)-1;
	        }
search_for_space:	        
	        printf("making room in group %u, searching in blocks %llu - %llu\n", g, first_blk, last_blk);
	        for (blk = first_blk; blk <= last_blk; blk++) {

	            if (ext2fs_has_group_desc_csum(fs) &&
				    ext2fs_bg_flags_test(rfs->old_fs, ext2fs_group_of_blk2(rfs->old_fs, blk), EXT2_BG_BLOCK_UNINIT)) {
			/* The block bitmap is uninitialized, so skip to the next block group.
			 * This shall not happen, as we called fix_uninit_block_bitmaps() at the beginning */
			printf("the EXT2_BG_BLOCK_UNINIT shall have been removed for group %u\n", g);
			blk=ext2fs_group_first_block2(fs, ext2fs_group_of_blk2(fs, blk)+1)-1;
			continue;
		    }
		    
		    for (blk2 = blk, j = 0;
		         j < rfs->new_fs->inode_blocks_per_group && blk2 <= last_blk;
		          blk2++, j++) {
		            if (ext2fs_test_block_bitmap2(meta_bmap, blk2) || ext2fs_test_block_bitmap2(rfs->reserve_blocks, blk2)) {
		                blk = blk2;
		                break;
		            }
		    }
		    if (j == rfs->new_fs->inode_blocks_per_group) {
			printf(" --->blocks to move in group %u are %llu - %llu\n", g, blk, blk2);
		        ext2fs_mark_block_bitmap_range2(rfs->move_blocks, blk, rfs->new_fs->inode_blocks_per_group);
		        ext2fs_mark_block_bitmap_range2(rfs->reserve_blocks, blk, rfs->new_fs->inode_blocks_per_group);
			first_blk = blk2;
			break;
		    }
		    if (blk2 > last_blk) {
		      blk = blk2; //will break the loop and enter the next if about failed allocation
		    }     
	        }
	        if (blk > last_blk) {
	        /*ext2fs_allocate_group_table() -> flexbg_offset() will ultimately search from 0 up to the last block of the flex_bg group, but not afterwards*/
	            if (!retried_from_beginning && ext2fs_has_feature_flex_bg(fs->super)) {
	                retried_from_beginning = 1;
	                first_blk = fs->super->s_first_data_block;
	                goto search_for_space;
	            }
	            printf("unable to locate a suitable area to make room while treating group %u\n", g);
	            retval = EXT2_ET_BLOCK_ALLOC_FAIL;
	            goto errout;
	        }
	   }
	}

      retval = block_mover(rfs);
      if (retval) {
        printf("block_mover returned with status %li\n", retval);
        goto errout;
      }

      /*needed to avoid bitmap inconsistencies in fsck for unoptimized/expanded? extent trees*/
      rfs->old_fs->get_alloc_block = resize2fs_get_alloc_block_stats_update;

      retval = inode_scan_and_fix(rfs);
      if (retval) {
        printf("inode_scan_and_fix returned with status %li\n", retval);
        goto errout;
      }
      
errout:
	if (meta_bmap)
		ext2fs_free_block_bitmap(meta_bmap);
	if (rfs->reserve_blocks) {
		ext2fs_free_block_bitmap(rfs->reserve_blocks);
		rfs->reserve_blocks = 0;
	}
	if (rfs->move_blocks) {
		ext2fs_free_block_bitmap(rfs->move_blocks);
		rfs->move_blocks = 0;
	}

	return retval;

}

