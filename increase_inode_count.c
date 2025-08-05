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
 *	1.  Allocate new larger inode tables
 *	2.  Migrate inodes to the new itables
 *	3.  Free the space of the old itables
 *	4.  If there were no contiguous free blocks to allocate some of the new itables:
 *	    4.a  Move some data blocks to make room for it
 *	    4.b  Update references to those data blocks in the affected files/folders
 *	    4.c  Go back to step 1 for the new itables pending allocation
 *	5.  Update group stats
 *
 */

#include "config.h"
#include "resize2fs.h"
#include <time.h>

#ifdef __linux__			/* Kludge for debugging */
#define RESIZE2FS_DEBUG
#endif

typedef enum {
    itable_status_not_allocated = 0, /*must be zero for calloc()*/
    itable_status_allocated = 1,
    itable_status_filled = 2
} itable_status;

static errcode_t block_mover(ext2_resize_t rfs, itable_status *new_itable_status);
static errcode_t inode_scan_and_fix(ext2_resize_t rfs, itable_status *new_itable_status);
static errcode_t migrate_inodes(ext2_resize_t rfs, unsigned int *evacuated_inodes, unsigned int *dir_count, unsigned int *free_inode_count, itable_status *new_itable_status, unsigned int *total_inodes_free);
static errcode_t inode_relocation_to_bigger_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group);
static errcode_t make_room_for_new_itables(ext2_resize_t rfs, itable_status *new_itable_status);
static errcode_t allocate_new_itables(ext2_resize_t rfs, itable_status *new_itable_status, unsigned int *allocated_new_itables);
static errcode_t migrate_ea_block(ext2_extent bmap, ext2_filsys fs, ext2_ino_t ino, struct ext2_inode *inode, int *changed);

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

//#define AVOID_OLD	1
//#define DESPERATION	2

static void init_block_alloc(ext2_resize_t rfs)
{
//	rfs->alloc_state = AVOID_OLD;
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
	ext2_filsys	fs = rfs->old_fs;
        blk64_t initial = rfs->new_blk;
        int visited_initial = 0;

	while (1) {
		if (rfs->new_blk >= ext2fs_blocks_count(fs->super)) {
			rfs->new_blk = fs->super->s_first_data_block;
			printf("Moving search back to first block for allocations\n");
			//rfs->alloc_state = DESPERATION;
			continue;
		}
		//printf("get_new_block %llu %i %i\n", aux_test, ext2fs_test_block_bitmap2(fs->block_map, aux_test), ext2fs_test_block_bitmap2(rfs->reserve_blocks, aux_test));
		if (initial == rfs->new_blk) {
		      //printf("intial %llu visited %i\n", rfs->new_blk, visited_initial);
		      if (visited_initial)
		          return 0;
		      visited_initial = 1;
		}
		if (ext2fs_test_block_bitmap2(fs->block_map, rfs->new_blk) ||
		    ext2fs_test_block_bitmap2(rfs->reserve_blocks, rfs->new_blk)) {
			rfs->new_blk++;
			continue;
		}
		//printf("get_new_block returning block %llu, frees %llu\n", rfs->new_blk, ext2fs_free_blocks_count(rfs->old_fs->super));
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
	int is_new_fs = (fs == rfs->new_fs);

	printf("get_alloc_block allocating %s...\n", is_new_fs ? "in new fs" : "in old fs");
	blk = get_new_block(rfs);
	if (!blk)
		return ENOSPC;
	printf("get_alloc_block got %llu\n", (unsigned long long) blk);


        /*move_blocks now contains allocated blocks not to be remapped*/
        ext2fs_mark_block_bitmap2(rfs->move_blocks, blk);


        /*We need to update stats (which will also mark block and clear uninit) in the other fs
          to avoid bitmap inconsistencies in fsck for unoptimized/expanded? extent trees whose
          new blocks get allocated by this function*/
        if (is_new_fs) {
            ext2fs_block_alloc_stats2(rfs->old_fs, blk, +1);

            ext2fs_mark_block_bitmap2(rfs->new_fs->block_map, blk);
	    group = ext2fs_group_of_blk2(rfs->new_fs, blk);
	    ext2fs_clear_block_uninit(rfs->new_fs, group);
	} else {
	    ext2fs_block_alloc_stats2(rfs->new_fs, blk, +1);

	    ext2fs_mark_block_bitmap2(rfs->old_fs->block_map, blk);
	    group = ext2fs_group_of_blk2(rfs->old_fs, blk);
	    ext2fs_clear_block_uninit(rfs->old_fs, group);
	}

	*ret = (blk64_t) blk;
	return 0;
}

static errcode_t block_mover(ext2_resize_t rfs, itable_status *new_itable_status)
{
	blk64_t			blk, old_blk, new_blk;
	ext2_filsys		fs = rfs->new_fs;
	ext2_filsys		old_fs = rfs->old_fs;
	errcode_t		retval;
	__u64			c, size;
	int			to_move, moved;
	ext2_badblocks_list	badblock_list = 0;
	int			bb_modified = 0;
	ext2_filsys             fs_bb_inode
	                            = (new_itable_status[(EXT2_BAD_INO-1)/rfs->new_fs->super->s_inodes_per_group] == itable_status_filled) ?
	                            rfs->new_fs : rfs->old_fs;

        rfs->old_fs->get_alloc_block = resize2fs_get_alloc_block;
        rfs->new_fs->get_alloc_block = resize2fs_get_alloc_block;

	retval = ext2fs_read_bb_inode(fs_bb_inode, &badblock_list);
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
	//init_block_alloc(rfs);
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
		        printf("block_mover ENOSPC old_block %llu\n", blk);
		        break;
			//retval = ENOSPC;
			//goto errout;
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
			retval = ext2fs_update_bb_inode(fs_bb_inode,
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
			if (ext2fs_test_block_bitmap2(pb->rfs->move_blocks, block)) {
      			        printf("ino=%u, blockcnt=%lld, %llu->%llu, frees (%s): %llu. Already moved and re-allocated - nothing to do\n",
		                         pb->old_ino, (long long) blockcnt, (unsigned long long) block, (unsigned long long) new_block,
		       	                 fs == pb->rfs->new_fs ? "new_fs" : "old_fs", ext2fs_free_blocks_count(fs->super));
			        return 0;
	                }
			*block_nr = new_block;
			ret |= BLOCK_CHANGED;
			pb->changed = 1;
//#ifdef RESIZE2FS_DEBUG
//			if (pb->rfs->flags & RESIZE_DEBUG_BMOVE)
				printf("ino=%u, blockcnt=%lld, %llu->%llu, frees (%s): %llu\n",
				       pb->old_ino, (long long) blockcnt,
				       (unsigned long long) block, (unsigned long long) new_block,
				       fs == pb->rfs->new_fs ? "new_fs" : "old_fs", ext2fs_free_blocks_count(fs->super));
//#endif

			/*If not a metadata block, unmark it now, so it can be reallocated for other stuff (like extent tree growth).
			The problem with this, it is that it may break the contiguous areas we reserved in make_room for new itables,
			thus making the realloc itables function to fail.
  			Maybe we could condition this behaviour depending on the Desperation mode of the block allocator, hoping for at least
  			one itable to be allocated so the prev_allocated loop continues and tries again later...
			Also, is it safe to also unmark metadata blocks? or not until all the block for the inode have been treated?
			In this case, the unmark shall be placed after ext2fs_block_iterate3 call()*/
			if (blockcnt >= 0) {
			     // ext2fs_unmark_block_bitmap2(pb->rfs->reserve_blocks, block);
			      /*printf("test block %llu: %i %i\n", block,
			      ext2fs_test_block_bitmap2(pb->rfs->old_fs->block_map, block), ext2fs_test_block_bitmap2(pb->rfs->new_fs->block_map, block));*/
			}

			block = new_block;

		}
	}
	return ret;
}


static errcode_t inode_scan_and_fix(ext2_resize_t rfs, itable_status *new_itable_status)
{
	struct process_block_struct	pb;
	ext2_ino_t		ino;
	struct ext2_inode 	*inode = NULL;
	errcode_t		retval;
	char			*block_buf = 0;
	int			inode_size;
	ext2_filsys             fs;


	set_com_err_hook(quiet_com_err_proc);

	retval = ext2fs_get_array(rfs->old_fs->blocksize, 3, &block_buf);
	if (retval) goto errout;


	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_SCAN_PASS,
					 0, rfs->old_fs->group_desc_count);
		if (retval)
			goto errout;
	}

	pb.rfs = rfs;
	pb.inode = inode;
	pb.error = 0;
	inode_size = EXT2_INODE_SIZE(rfs->new_fs->super);
	inode = malloc(inode_size);
	if (!inode) {
		retval = ENOMEM;
		goto errout;
	}

	for (ino = 1; ino <= rfs->old_fs->super->s_inodes_count; ino++) {
	    if (!ino)
	        break;

	    if (new_itable_status[(ino-1) / rfs->new_fs->super->s_inodes_per_group] == itable_status_filled)
	        fs = rfs->new_fs;
            else
                fs = rfs->old_fs;


	    retval = ext2fs_read_inode_full(fs, ino, inode, inode_size);
	    if (retval)
	        goto errout;

	    if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
		continue; /* inode not in use */

	    pb.changed = 0;

	    /* Remap EA block */
	    retval = migrate_ea_block(rfs->bmap, fs, ino, inode, &pb.changed);
	    if (retval)
		    goto errout;

	    if (pb.changed)
		    retval = ext2fs_write_inode_full(fs,
						     ino,
						     inode, inode_size);
	    if (retval)
		    goto errout;

	    /*
	     * Update inodes to point to new blocks
	     */
	    fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	    if (ext2fs_inode_has_valid_blocks2(fs, inode) &&
	        (rfs->bmap)) {
		    pb.ino = ino;
		    pb.old_ino = ino;
		    pb.has_extents = inode->i_flags & EXT4_EXTENTS_FL;
		    retval = ext2fs_block_iterate3(fs,
					           ino, 0, block_buf,
					           update_block_reference, &pb);
		    if (retval || pb.error)
		            printf("ext2fs_block_iterate3: retval %lu, pb.error %lu, ino %u\n", retval, pb.error, ino);
		    if (retval)
			    goto errout;
		    if (pb.error) {
			    retval = pb.error;
			    goto errout;
		    }
	    }
	}


	io_channel_flush(rfs->old_fs->io);
	io_channel_flush(rfs->new_fs->io);

errout:
	reset_com_err_hook();
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	rfs->new_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (rfs->bmap) {
		ext2fs_free_extent_table(rfs->bmap);
		rfs->bmap = 0;
	}
	if (block_buf)
		ext2fs_free_mem(&block_buf);
	free(inode);
	return retval;
}

/*this function will update block numbers when the blocks are moved*/
static errcode_t migrate_ea_block(ext2_extent bmap, ext2_filsys fs, ext2_ino_t ino,
				  struct ext2_inode *inode, int *changed)
{
	char *buf = NULL;
	blk64_t new_block;
	errcode_t err = 0;

	/* No EA block or no remapping?  Quit early. */
	if (ext2fs_file_acl_block(fs, inode) == 0 || !bmap)
		return 0;
	new_block = extent_translate(fs, bmap,
		ext2fs_file_acl_block(fs, inode));
	if (new_block == 0)
		return 0;

	/* Set the new ACL block */
	printf("migrate_ea_block, inode %u, old_block %llu, new_block %llu\n", ino,ext2fs_file_acl_block(fs, inode), new_block);
	ext2fs_file_acl_block_set(fs, inode, new_block);

	/* Update checksum */
	if (ext2fs_has_feature_metadata_csum(fs->super)) {
		err = ext2fs_get_mem(fs->blocksize, &buf);
		if (err)
			return err;
		fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		err = ext2fs_read_ext_attr3(fs, new_block, buf, ino);
		fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
		if (err)
			goto out;
		err = ext2fs_write_ext_attr3(fs, new_block, buf, ino);
		if (err)
			goto out;
	}
	*changed = 1;

out:
	ext2fs_free_mem(&buf);
	return err;
}



static errcode_t inode_relocation_to_bigger_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group) {

	errcode_t	retval;
	ext2_ino_t ino_num = 0, total_inodes_free = 0;
	dgrp_t group = 0, allocated_new_itables = 0, prev_allocated_new_itables = 0xFFFFFFFF; /*0xFFFFFFFF to identify the first iteration*/
	unsigned int *dir_count = NULL, *free_inode_count = NULL;
	unsigned int *evacuated_inodes = NULL;
	itable_status         *new_itable_status = NULL;
	blk64_t		itable_start;
	
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
        evacuated_inodes = (unsigned int*) calloc(rfs->new_fs->group_desc_count, sizeof(unsigned int));
        if (evacuated_inodes == NULL) {
              printf("alloc_error evacuated_inodes\n");
              goto errout;
        }
        /* using calloc as itable_status_not_allocated = 0 */
        new_itable_status = (itable_status*) calloc(rfs->new_fs->group_desc_count, sizeof(itable_status));
        if (new_itable_status == NULL) {
              printf("alloc_error new_itable_status\n");
              goto errout;
        }


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


        do {
	      retval = allocate_new_itables(rfs, new_itable_status, &allocated_new_itables);
	      if (retval) {
	          printf("allocate_new_itables returned with status %li\n", retval);
	          goto errout;
	      }
	      if (prev_allocated_new_itables != 0xFFFFFFFF) {
	          printf("prev_allocated_new_itables %u, allocated_new_tables %u\n", prev_allocated_new_itables, allocated_new_itables);
	          if (prev_allocated_new_itables == allocated_new_itables) {
	              printf("FATAL, breaking loop because no free space available to allocate new itables\n");
	              retval = -1;
	              goto errout;
	          }
	      }

	      retval = migrate_inodes(rfs, evacuated_inodes, dir_count, free_inode_count, new_itable_status, &total_inodes_free);
	      if (retval) {
	          printf("migrate_inodes returned with status %li\n", retval);
	          goto errout;
	      }

	      for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
	          itable_start = ext2fs_inode_table_loc(rfs->old_fs, group);
	          if (itable_start != 0 && evacuated_inodes[group] == rfs->old_fs->super->s_inodes_per_group) {
	                  printf("Freeing old itable of group %u, blocks %llu - %llu\n", group, itable_start, itable_start + rfs->old_fs->inode_blocks_per_group - 1);
	        	  ext2fs_block_alloc_stats_range(rfs->old_fs, itable_start, rfs->old_fs->inode_blocks_per_group, -1);
	        	  ext2fs_block_alloc_stats_range(rfs->new_fs, itable_start, rfs->old_fs->inode_blocks_per_group, -1);
	        	  ext2fs_inode_table_loc_set(rfs->old_fs, group, 0);
	      	  }
	      }

	      if (allocated_new_itables < rfs->new_fs->group_desc_count) {
                  retval = make_room_for_new_itables(rfs, new_itable_status);
                  if (retval) {
                      goto errout;
                  }
              }
              prev_allocated_new_itables = allocated_new_itables;
	} while (allocated_new_itables < rfs->new_fs->group_desc_count);

	io_channel_flush(rfs->old_fs->io);
	
	/*complete ino_num and group with the last values treated by migrate_inodes*/
	ino_num = rfs->old_fs->super->s_inodes_count + 1;
	group = (rfs->old_fs->super->s_inodes_count - 1) / rfs->new_fs->super->s_inodes_per_group;
	
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


	for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
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
        if (dir_count)
            free(dir_count);
        if (free_inode_count)
            free(free_inode_count);
        if (evacuated_inodes)
            free(evacuated_inodes);
        if (new_itable_status)
            free(new_itable_status);
	return retval;
}

static errcode_t allocate_new_itables(ext2_resize_t rfs, itable_status *new_itable_status, unsigned int *allocated_new_itables) {

	blk64_t		itable_start;
	errcode_t	retval;
	dgrp_t group = 0;
	int len = 0;

	for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
		if (new_itable_status[group] == itable_status_not_allocated) {
			ext2fs_inode_table_loc_set(rfs->new_fs, group, 0);
			retval = ext2fs_allocate_group_table(rfs->new_fs, group, 0);
			if (retval) {
			        printf("unsuccessful ext2fs_allocate_group_table for group %u with retval %li - will retry later\n", group, retval);
			} else {
			      itable_start = ext2fs_inode_table_loc(rfs->new_fs, group);
			      len = rfs->new_fs->inode_blocks_per_group;
			      /*ext2fs_allocate_group_table() doesn't update stats in group if flex_bg is not set, we have to do it ourselves*/
			      if (!ext2fs_has_feature_flex_bg(rfs->new_fs->super))
			              ext2fs_block_alloc_stats_range(rfs->new_fs, itable_start, len, +1);
			      ext2fs_block_alloc_stats_range(rfs->old_fs, itable_start, len, +1);
			      retval = ext2fs_zero_blocks2(rfs->new_fs, itable_start, len, &itable_start, &len);
                              if (retval) {
                                      fprintf(stderr, _("\nCould not write %d "
                                                "blocks in inode table starting at %llu: %s\n"),
                                              len, (unsigned long long) itable_start, error_message(retval));
                                      exit(1);
                              }
			      printf("successful ext2fs_allocate_group_table for group %u with retval %li in block %llu\n", group, retval, itable_start);
			      new_itable_status[group] = itable_status_allocated;
			      (*allocated_new_itables)++;
			}
		}
	}

        io_channel_flush(rfs->new_fs->io);

	retval = 0;
errout:
	return retval;
}


static errcode_t migrate_inodes(ext2_resize_t rfs,
                            unsigned int *evacuated_inodes,
                            unsigned int *dir_count,
                            unsigned int *free_inode_count,
                            itable_status *new_itable_status,
                            unsigned int *total_inodes_free) {
        ext2_ino_t               ino_num = 0;
	struct ext2_inode       *inode = NULL;
	int                      inode_size = 0;
	dgrp_t                   new_group = 0, old_group = 0;
	errcode_t	         retval;

	inode_size = EXT2_INODE_SIZE(rfs->new_fs->super);
	inode = malloc(inode_size);
	if (!inode) {
		retval = ENOMEM;
		goto errout;
	}

        for (ino_num = 1; ino_num<=rfs->old_fs->super->s_inodes_count; ino_num++) {
                new_group = (ino_num - 1) / rfs->new_fs->super->s_inodes_per_group;
                old_group = (ino_num - 1) / rfs->old_fs->super->s_inodes_per_group;
                if (new_itable_status[new_group] != itable_status_allocated) {
                    if (new_group == rfs->new_fs->group_desc_count-1)
                          break;
                    ino_num = (new_group+1) * rfs->new_fs->super->s_inodes_per_group;
                    continue;
                }

	        retval = ext2fs_read_inode_full(rfs->old_fs, ino_num, inode, inode_size);
	        printf("Migrating inode %u to new itable: links_count: %u, i_size_lo: %u, i_blocks: %u, old group: %u, new group: %u, read_retval: %li",
	                      ino_num, inode->i_links_count, inode->i_size,inode->i_blocks, old_group, new_group, retval);
	        if (retval)
	            goto errout;

	        evacuated_inodes[old_group]++;

	        if (inode->i_links_count == 0 && ino_num >= EXT2_FIRST_INODE(rfs->new_fs->super)) {
	                free_inode_count[new_group]++;
	                (*total_inodes_free)++;

		} else {
			if (LINUX_S_ISDIR(inode->i_mode))
	                    dir_count[new_group]++;
		        ext2fs_inode_alloc_stats2(rfs->new_fs, ino_num, +1, LINUX_S_ISDIR(inode->i_mode)!=0?1:0);
	        }
	        retval = ext2fs_write_inode_full(rfs->new_fs, ino_num, inode, inode_size);
	        printf(" - write_retval: %li\n", retval);
	        if (retval)
	                goto errout;

	        /*are we about to completely migrate the current new itable?*/
	        if (ino_num+1 > rfs->old_fs->super->s_inodes_count || (((ino_num+1)-1)/rfs->new_fs->super->s_inodes_per_group) != new_group) {
	                new_itable_status[new_group] = itable_status_filled;
	        }
	}


errout:
	if (inode)
	        free(inode);
        return retval;
}


static errcode_t make_room_for_new_itables(ext2_resize_t rfs, itable_status *new_itable_status) {
	unsigned int	j;
	int             flexbg_size = 0, retried_from_beginning = 0;
	dgrp_t		g;
	blk64_t		blk, blk2, first_blk, last_blk;
	blk64_t         pledged_blocks = 50; /* start at 50 as a safe margin for extent trees rebalancing.. TODO: what would be a good start number*/
	errcode_t	retval;
	ext2_filsys 	fs = rfs->old_fs;
	ext2fs_block_bitmap	meta_bmap;
	ext2_badblocks_list	badblock_list = 0;
	
	init_block_alloc(rfs);

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

	retval = ext2fs_read_bb_inode(new_itable_status[(EXT2_BAD_INO-1)/rfs->new_fs->super->s_inodes_per_group] == itable_status_filled ?
	                              rfs->new_fs : rfs->old_fs,
	                              &badblock_list);
	if (retval) {
		printf("Error while reading badblock list in make_room_for_new_itables()\n");
		return retval;
	}

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
	    if (ext2fs_free_blocks_count(rfs->new_fs->super) - pledged_blocks < rfs->new_fs->inode_blocks_per_group) {
	        printf("breaking loop before running out of space, free: %llu, pledged: %llu, ipg: %u\n",
	              ext2fs_free_blocks_count(rfs->new_fs->super), pledged_blocks, rfs->new_fs->inode_blocks_per_group);
	        break; /*don't search if we are not going to find*/
	    }
	
	    if (ext2fs_has_feature_flex_bg(fs->super)) {
	            if (!(g % (1U << fs->super->s_log_groups_per_flex))) {
	                first_blk = ext2fs_group_first_block2(fs, g & ~(flexbg_size - 1));
	                last_blk = (g|(flexbg_size - 1)>=fs->group_desc_count-1) ?
	                             ext2fs_blocks_count(rfs->old_fs->super)-1 :
	                             ext2fs_group_first_block2(fs, (g|(flexbg_size - 1))+1)-1;
	                retried_from_beginning = 0;
	            }
	    }
	    if (new_itable_status[g] != itable_status_not_allocated) {
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
		            if (ext2fs_test_block_bitmap2(meta_bmap, blk2)
		                || ext2fs_test_block_bitmap2(rfs->reserve_blocks, blk2)
		                || ext2fs_badblocks_list_test(badblock_list, blk2)) {
		                    blk = blk2;
		                    break;
		            }
		    }
		    if (j == rfs->new_fs->inode_blocks_per_group) {
			printf(" --->blocks to move in group %u are %llu - %llu\n", g, blk, blk2-1);
		        ext2fs_mark_block_bitmap_range2(rfs->move_blocks, blk, rfs->new_fs->inode_blocks_per_group);
		        ext2fs_mark_block_bitmap_range2(rfs->reserve_blocks, blk, rfs->new_fs->inode_blocks_per_group);
		        /* multiplied by 2, to account for possible extent tree rebalancing...TODO: check is it optimal?*/
		        pledged_blocks += 2*rfs->new_fs->inode_blocks_per_group;
		        /*move block allocater first try out of the contigous area we just created*/
		        /*rfs->new_blk = blk2;*/
			first_blk = blk2;
			break;
		    }
		    if (blk2 > last_blk) {
		      blk = blk2; //will break the inner loop and enter the next if about failed allocation
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
	            break;
	        }
	   }
	}

      printf("Free old %llu, Free new blocks %llu\n",  ext2fs_free_blocks_count(rfs->old_fs->super),  ext2fs_free_blocks_count(rfs->new_fs->super));

      retval = block_mover(rfs, new_itable_status);
      if (retval) {
        printf("block_mover returned with status %li\n", retval);
        goto errout;
      }

      /* At this point rfs->move_blocks is not needed anymore for its original purpose.
      So we will use it to mark blocks allocated by the resize2fs_get_alloc_block,
      and to avoid remapping those blocks in the update_block_reference.
      First of all, reset the whole bitmap*/
      ext2fs_unmark_block_bitmap_range2(rfs->move_blocks, rfs->old_fs->super->s_first_data_block, ext2fs_blocks_count(rfs->old_fs->super));

      retval = inode_scan_and_fix(rfs, new_itable_status);
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
	if (badblock_list) {
		ext2fs_badblocks_list_free(badblock_list);
	}

	return retval;

}

