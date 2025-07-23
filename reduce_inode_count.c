/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000 by Theodore Ts'o
 *
 * reduce_inode_count.c --- reduce the inode count of an existing ext4 filesystem
 * 
 * Copyright (C) 2025 by danim7 (https://github.com/danim7)
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * Reducing the inode count consists of the following phases:
 *
 *      1.  Calculate the new maximum inode number
 *	2.  Move the inodes in use above that number to lower numbers
 *          2.a. For those inodes, update the references in folders entries
 * 	3.  Move all the inodes to the new reduced inodes tables and free the remaining space
 *      4.  For flex_bg filesystems, try to place the new inode tables contiguously
 *      5.  Update group stats
 *
 */

#include "config.h"
#include "resize2fs.h"
#include <time.h>


//#ifdef __linux__			/* Kludge for debugging */
#define RESIZE2FS_DEBUG
//#endif


static errcode_t inode_ref_fix(ext2_resize_t rfs);
static errcode_t inode_relocation_to_smaller_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group);

errcode_t reduce_inode_count(ext2_filsys fs, int flags,
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

	/*init_resource_track(&rtrack, "fix_uninit_block_bitmaps 1", fs->io);
	fix_uninit_block_bitmaps(fs);
	print_resource_track(rfs, &rtrack, fs->io);*/
	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval)
		goto errout;

	/*
	init_resource_track(&rtrack, "resize_group_descriptors", fs->io);
	retval = resize_group_descriptors(rfs, *new_size);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);*/


/*	init_resource_track(&rtrack, "move_bg_metadata", fs->io);
	retval = move_bg_metadata(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);
*/

	/*init_resource_track(&rtrack, "zero_high_bits_in_metadata", fs->io);
	retval = zero_high_bits_in_inodes(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);*/

/*	init_resource_track(&rtrack, "adjust_superblock", fs->io);
	retval = adjust_superblock(rfs, *new_size);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);
*/

	init_resource_track(&rtrack, "inode_relocation_to_smaller_tables", fs->io);
	retval = inode_relocation_to_smaller_tables(rfs, new_inodes_per_group);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);
	
	
//	init_resource_track(&rtrack, "fix_uninit_block_bitmaps 2", fs->io);
//	fix_uninit_block_bitmaps(rfs->new_fs);
//	print_resource_track(rfs, &rtrack, fs->io);
//	/* Clear the block bitmap uninit flag for the last block group */
//	ext2fs_bg_flags_clear(rfs->new_fs, rfs->new_fs->group_desc_count - 1,
//			     EXT2_BG_BLOCK_UNINIT);
			     


	//*new_size = ext2fs_blocks_count(rfs->new_fs->super);
	
	


	/*init_resource_track(&rtrack, "blocks_to_move", fs->io);
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
	print_resource_track(rfs, &rtrack, fs->io);*/

//	init_resource_track(&rtrack, "inode_ref_fix", fs->io);
	//retval = inode_ref_fix(rfs);
	//if (retval)
	//	goto errout;
	//print_resource_track(rfs, &rtrack, fs->io);

	/*init_resource_track(&rtrack, "move_itables", fs->io);
	retval = move_itables(rfs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);*/

	/*retval = clear_sparse_super2_last_group(rfs);
	if (retval)
		goto errout;*/

	/*init_resource_track(&rtrack, "calculate_summary_stats", fs->io);
	retval = resize2fs_calculate_summary_stats(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);*/
/*
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

	init_resource_track(&rtrack, "fix_sb_journal_backup", fs->io);
	retval = fix_sb_journal_backup(rfs->new_fs);
	if (retval)
		goto errout;
	print_resource_track(rfs, &rtrack, fs->io);
*/
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


struct istruct {
	ext2_resize_t rfs;
	errcode_t	err;
	unsigned int	max_dirs;
	unsigned int	num;
};

static int check_and_change_inodes(ext2_ino_t dir,
				   int entry EXT2FS_ATTR((unused)),
				   struct ext2_dir_entry *dirent, int offset,
				   int	blocksize EXT2FS_ATTR((unused)),
				   char *buf EXT2FS_ATTR((unused)),
				   void *priv_data)
{
	struct istruct *is = (struct istruct *) priv_data;
	struct ext2_inode 	inode;
	ext2_ino_t		new_inode;
	errcode_t		retval;
	int			ret = 0;

	if (is->rfs->progress && offset == 0) {
		io_channel_flush(is->rfs->new_fs->io);
		is->err = (is->rfs->progress)(is->rfs,
					      E2_RSZ_INODE_REF_UPD_PASS,
					      ++is->num, is->max_dirs);
		if (is->err)
			return DIRENT_ABORT;
	}

	/*
	 * If we have checksums enabled and the inode wasn't present in the
	 * old fs, then we must rewrite all dir blocks with new checksums.
	 */
	if (ext2fs_has_feature_metadata_csum(is->rfs->new_fs->super) /*&&
	    !ext2fs_test_inode_bitmap2(is->rfs->new_fs->inode_map, dir)*/)
		ret |= DIRENT_CHANGED;

	if (!dirent->inode)
		return ret;

	new_inode = ext2fs_extent_translate(is->rfs->imap, dirent->inode);

	if (!new_inode)
		return ret;
//#ifdef RESIZE2FS_DEBUG
//	if (is->rfs->flags & RESIZE_DEBUG_INODEMAP)
		printf("Inode translate (dir=%u, name=%.*s, %u->%u)\n",
		       dir, ext2fs_dirent_name_len(dirent), dirent->name,
		       dirent->inode, new_inode);
//#endif

	dirent->inode = new_inode;

	/* Update the directory mtime and ctime */
	retval = ext2fs_read_inode(is->rfs->new_fs, dir, &inode);
	//printf(" ext2fs_read_inode %u, i_size_lo: %u, i_blocks: %u, retval %li\n", dir, inode.i_size, inode.i_blocks, retval);

	if (retval == 0) {
		inode.i_mtime = inode.i_ctime = is->rfs->new_fs->now ?
			is->rfs->new_fs->now : time(0);
		is->err = ext2fs_write_inode(is->rfs->new_fs, dir, &inode);
		//printf(" ext2fs_write_inode retval %li\n", is->err);
		if (is->err)
			return ret | DIRENT_ABORT;
	}

	return ret | DIRENT_CHANGED;
}

static errcode_t inode_ref_fix(ext2_resize_t rfs)
{
	errcode_t		retval;
	struct istruct 		is;

	if (!rfs->imap)
		return 0;

	/*
	 * Now, we iterate over all of the directories to update the
	 * inode references
	 */
	is.num = 0;
	is.max_dirs = ext2fs_dblist_count2(rfs->old_fs->dblist);
	is.rfs = rfs;
	is.err = 0;

	if (rfs->progress) {
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_REF_UPD_PASS,
					 0, is.max_dirs);
		if (retval)
			goto errout;
	}
	printf("rfs->old_fs->dblist: %p\n", rfs->old_fs->dblist);
	//printf("rfs->old_fs->dblist->fs: %p\n", rfs->old_fs->dblist->fs);
	printf("rfs->old_fs: %p\n", rfs->old_fs);
	printf("rfs->new_fs: %p\n", rfs->new_fs);
        //rfs->old_fs->dblist->fs = rfs->new_fs; 
        //printf("rfs->old_fs->dblist->fs: %p\n", rfs->old_fs->dblist->fs);
	rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	retval = ext2fs_dblist_dir_iterate(rfs->old_fs->dblist,
					   DIRENT_FLAG_INCLUDE_EMPTY, 0,
					   check_and_change_inodes, &is);
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	printf("ext2fs_dblist_dir_iterate: retval: %li\n", retval);
	if (retval)
		goto errout;
	if (is.err) {
		retval = is.err;
		goto errout;
	}

	if (rfs->progress && (is.num < is.max_dirs))
		(rfs->progress)(rfs, E2_RSZ_INODE_REF_UPD_PASS,
				is.max_dirs, is.max_dirs);
				
						//io_channel_flush(rfs->new_fs->io);

errout:
	ext2fs_free_extent_table(rfs->imap);
	rfs->imap = 0;
	return retval;
}



static errcode_t inode_relocation_to_smaller_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group)
{
	struct process_block_struct	pb;
	ext2_ino_t		ino, new_inode;
	struct ext2_inode 	*inode = NULL;
	ext2_inode_scan 	scan = NULL;
	errcode_t		retval;
	char			*block_buf = 0;
	ext2_ino_t		start_to_move;
	int			inode_size;
	int			update_ea_inode_refs = 0;
	ext2_ino_t i; 
	dgrp_t group; 
	unsigned int itables_a_borrar, total_inodes_free, moved_itables;
	blk64_t itable_start, after_prev_itable, itable;
	unsigned int *dir_count = NULL, *free_inode_count = NULL;
	int             flexbg_size = 0, flexbg_i;
	ext2fs_inode_bitmap inode_bitmap = NULL;

	/*if ((rfs->old_fs->group_desc_count <=
	     rfs->new_fs->group_desc_count) &&
	    !rfs->bmap)
		return 0;
*/
	set_com_err_hook(quiet_com_err_proc);

	retval = ext2fs_open_inode_scan(rfs->old_fs, 0, &scan);
	if (retval) goto errout;

	retval = ext2fs_init_dblist(rfs->old_fs, 0);
	if (retval) goto errout;
	retval = ext2fs_get_array(rfs->old_fs->blocksize, 3, &block_buf);
	if (retval) goto errout;


	start_to_move = (rfs->old_fs->group_desc_count * new_inodes_per_group);
	printf("start_to_move: %u\n", start_to_move);
	
	
	/*
	 * Check to make sure there are enough inodes
	 */
	if ((rfs->old_fs->super->s_inodes_count -
	     rfs->old_fs->super->s_free_inodes_count) >
	    start_to_move) {
		retval = ENOSPC;
		printf("not enough inodes\n");
		exit(1);
		goto errout;
	}
	

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
	
	
	/*
	 * First, copy all of the inodes that need to be moved
	 * elsewhere in the inode table
	 */
	while (1) {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode, inode_size);
		printf("Relocating inodes beyond the new inode limit: read inode: %u, links_count: %u\n", ino, inode->i_links_count);
		if (retval) goto errout;
		if (!ino)
			break;
			
		if (inode->i_flags & EXT4_EA_INODE_FL)
		    printf("EA inode: %u\n", ino);

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue; /* inode not in use */

		pb.is_dir = LINUX_S_ISDIR(inode->i_mode);
		pb.changed = 0;

		/* Remap EA block */
		retval = migrate_ea_block(rfs, ino, inode, &pb.changed);
		if (retval)
			goto errout;

		new_inode = ino;

		if (ino <= start_to_move){
			goto remap_blocks;
		}

		 /*
		 The ext2fs_new_inode() searches for the lowest inode that is available.
		 If that behavior changes, we may need to rewrite this part.
		 */
		retval = ext2fs_new_inode(rfs->old_fs, 0, 0, 0, &new_inode);
		if (retval)
			goto errout;
		if (new_inode > start_to_move) {
		    printf("ext2fs_new_inode returned a new_inode beyond the new end.\n"
		            "Fix this or selected a larger inode count (aka. lower ratio)\n");
		    retval = -1;
		    goto errout;
		}

		
		printf("ext2fs_new_inode inode: %u\n", new_inode);
		
		ext2fs_inode_alloc_stats2(rfs->old_fs, new_inode, +1,  pb.is_dir);
					  
		
		/*
		 * i_ctime field in xattr inodes contain a portion of the ref
		 * count, do not overwrite.
		 */
		if (inode->i_flags & EXT4_EA_INODE_FL)
			update_ea_inode_refs = 1;
		else
			inode->i_ctime = rfs->old_fs->now ?
				rfs->old_fs->now : time(0);

		retval = ext2fs_write_inode_full(rfs->old_fs, new_inode, inode, inode_size);
		if (retval)
			goto errout;
		pb.changed = 0;

#ifdef RESIZE2FS_DEBUG
		if (rfs->flags & RESIZE_DEBUG_INODEMAP)
			printf("Inode moved %u->%u\n", ino, new_inode);
#endif
		if (!rfs->imap) {
			retval = ext2fs_create_extent_table(&rfs->imap, 0);
			if (retval)
				goto errout;
		}
		ext2fs_add_extent_entry(rfs->imap, ino, new_inode);

remap_blocks:
		if (pb.changed)
			retval = ext2fs_write_inode_full(rfs->old_fs,
							 new_inode,
							 inode, inode_size);
		if (retval)
			goto errout;

		/*
		 * Update inodes to point to new blocks; schedule directory
		 * blocks for inode remapping.  Need to write out dir blocks
		 * with new inode numbers if we have metadata_csum enabled.
		 */
		rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		rfs->bmap = 0; 
		if (ext2fs_inode_has_valid_blocks2(rfs->old_fs, inode) &&
		    (rfs->bmap || pb.is_dir)) {
			pb.ino = new_inode;
			pb.old_ino = ino;
			pb.has_extents = inode->i_flags & EXT4_EXTENTS_FL;
			retval = ext2fs_block_iterate3(rfs->old_fs,
						       new_inode, 0, block_buf,
						       process_block, &pb);
	
			if (retval)
				goto errout;
			if (pb.error) {
				retval = pb.error;
				goto errout;
			}
		} else if ((inode->i_flags & EXT4_INLINE_DATA_FL) &&
			   (rfs->bmap || pb.is_dir)) {
			/* inline data dir; update it too */
			retval = ext2fs_add_dir_block2(rfs->old_fs->dblist,
						       new_inode, 0, 0);
			if (retval)
				goto errout;
		}

		/* Fix up extent block checksums with the new inode number */
		if (ext2fs_has_feature_metadata_csum(rfs->old_fs->super) &&
		    (inode->i_flags & EXT4_EXTENTS_FL)) {
			retval = ext2fs_fix_extents_checksums(rfs->old_fs,
							      new_inode, NULL);
			if (retval)
				goto errout;
		}
	}


	if (update_ea_inode_refs &&
	    ext2fs_has_feature_ea_inode(rfs->new_fs->super)) {
		retval = fix_ea_inode_refs(rfs, inode, block_buf,
					   start_to_move);
		if (retval)
			goto errout;
	}
	
	printf("calling inode_ref_fix()\n");
	retval = inode_ref_fix(rfs);
	if (retval)
		goto errout;
	
	
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
	//exit(0);
	
	
	

	printf("new_fs->inode_blocks_per_group: %u\n", rfs->new_fs->inode_blocks_per_group);
	printf("new_fs->super->s_inodes_per_group: %u\n", rfs->new_fs->super->s_inodes_per_group);
	printf("new_fs->super->s_inode_size: %u\n", rfs->new_fs->super->s_inode_size);
	printf("new_fs->inode_blocks_per_group: %u\n", rfs->new_fs->inode_blocks_per_group);
	printf("new_fs->blocksize: %u\n", rfs->new_fs->blocksize);
	printf("new_fs->super->s_log_block_size: %u\n", rfs->new_fs->super->s_log_block_size);
	printf("new_fs->super->s_inodes_count: %u\n", rfs->new_fs->super->s_inodes_count);
	printf("new_fs->group_desc_count: %u\n", rfs->new_fs->group_desc_count);

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
	for (i = rfs->new_fs->super->s_inodes_count; i>0; i--) {
	
	        retval = ext2fs_read_inode_full(rfs->old_fs, i, inode, inode_size);
	        group = (i - 1) / rfs->new_fs->super->s_inodes_per_group;
	        printf("Moving inodes to the new itable: read inode: %u, links_count: %u, group: %u, LINUX_S_ISDIR(inode->i_mode): %i, retval: %li\n",
	                  i, inode->i_links_count, group, LINUX_S_ISDIR(inode->i_mode), retval);
	        if (retval)
	            goto errout;
	        
        
	        if (inode->i_links_count == 0 && i >= EXT2_FIRST_INODE(rfs->new_fs->super)) {
	                free_inode_count[group]++;
	                total_inodes_free++;  
		} else {
		      if (LINUX_S_ISDIR(inode->i_mode))
	                  dir_count[group]++;

		      //ext2fs_inode_alloc_stats2(rfs->old_fs, i, -1, LINUX_S_ISDIR(inode->i_mode)!=0?1:0);
		      ext2fs_inode_alloc_stats2(rfs->new_fs, i, +1, LINUX_S_ISDIR(inode->i_mode)!=0?1:0);
	        }
	        //if inode not in use, write zeros to the itable anyway, as it may contain the previous inode
	        retval = ext2fs_write_inode_full(rfs->new_fs, i, inode, inode_size);
	        printf("Moving inodes to the new itable: written inode: %u, i_size_lo: %u, i_blocks: %u, retval: %li\n", i, inode->i_size,inode->i_blocks, retval);
	        if (retval) goto errout;
	        
	        if (ext2fs_has_feature_metadata_csum(rfs->new_fs->super) &&
		    (inode->i_flags & EXT4_EXTENTS_FL)) {
			retval = ext2fs_fix_extents_checksums(rfs->new_fs, i, NULL);
			if (retval)
				goto errout;
		}
	
	}

	
	io_channel_flush(rfs->old_fs->io);
	//??	
	io_channel_flush(rfs->new_fs->io);
	
	//TODO??: set the rest of inode bitmap: ext2fs_bg_itable_unused_set??
	
	
	if (ext2fs_has_feature_flex_bg(rfs->new_fs->super)) {
	    flexbg_size = 1U << rfs->new_fs->super->s_log_groups_per_flex;
	      if (!rfs->itable_buf) {
	          	retval = ext2fs_get_array(rfs->new_fs->blocksize, rfs->new_fs->inode_blocks_per_group,
				      &rfs->itable_buf);
	                if (retval)
		             goto errout;

	                memset(rfs->itable_buf, 0, rfs->new_fs->blocksize * rfs->new_fs->inode_blocks_per_group);
	      }
	    for (flexbg_i = 0; flexbg_i < rfs->new_fs->group_desc_count; flexbg_i += flexbg_size) {
	    
	        after_prev_itable = ext2fs_inode_table_loc(rfs->old_fs, flexbg_i)+rfs->new_fs->inode_blocks_per_group;
	        for (group = flexbg_i+1; group<flexbg_i+flexbg_size && group<rfs->new_fs->group_desc_count; group++) {
	            printf("group %u, after_prev_itable: %llu\n", group, after_prev_itable);
	            if (ext2fs_inode_table_loc(rfs->old_fs, group-1)+rfs->old_fs->inode_blocks_per_group == ext2fs_inode_table_loc(rfs->old_fs, group)) {
	                printf("moving itables, contiguous groups %u, %u, itables in %llu, %llu\n",
	                        group-1, group, ext2fs_inode_table_loc(rfs->old_fs, group-1), ext2fs_inode_table_loc(rfs->old_fs, group));
	                        
	                ext2fs_inode_table_loc_set(rfs->new_fs, group, after_prev_itable);
	                retval = io_channel_read_blk64(rfs->old_fs->io, ext2fs_inode_table_loc(rfs->old_fs, group), rfs->new_fs->inode_blocks_per_group, rfs->itable_buf);
			if (retval) goto errout;
			retval = io_channel_write_blk64(rfs->old_fs->io, after_prev_itable, rfs->new_fs->inode_blocks_per_group, rfs->itable_buf);
			if (retval) goto errout;
			
			ext2fs_group_desc_csum_set(rfs->new_fs, group);
	                after_prev_itable += rfs->new_fs->inode_blocks_per_group;
	                
	            } else {
	                ext2fs_block_alloc_stats_range(rfs->new_fs, after_prev_itable,
	                                                        ext2fs_inode_table_loc(rfs->old_fs, group-1)+rfs->old_fs->inode_blocks_per_group-after_prev_itable, -1);
	                printf("partial continuity, unmarking %llu blocks starting on itable %llu\n",
	                                      ext2fs_inode_table_loc(rfs->old_fs, group-1)+rfs->old_fs->inode_blocks_per_group-after_prev_itable, after_prev_itable);

	                after_prev_itable = ext2fs_inode_table_loc(rfs->old_fs, group)+rfs->new_fs->inode_blocks_per_group;
	            }
	        }

        	ext2fs_block_alloc_stats_range(rfs->new_fs, after_prev_itable,
	                                                ext2fs_inode_table_loc(rfs->old_fs, group-1)+rfs->old_fs->inode_blocks_per_group-after_prev_itable, -1);
	        printf("full continuity, unmarking %llu blocks starting on itable %llu\n",
	                                      ext2fs_inode_table_loc(rfs->old_fs, group-1)+rfs->old_fs->inode_blocks_per_group-after_prev_itable, after_prev_itable);
 
	    }
	} else { //no flex_bg
		for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
		    itables_a_borrar = rfs->old_fs->inode_blocks_per_group - rfs->new_fs->inode_blocks_per_group;
		    ext2fs_block_alloc_stats_range(rfs->new_fs,
		                                    ext2fs_inode_table_loc(rfs->new_fs, group) + rfs->new_fs->inode_blocks_per_group,
	                                            itables_a_borrar,
	                                            -1);
	         
	        }
	}
			

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
	if (inode_bitmap)
		ext2fs_free_inode_bitmap(inode_bitmap);
	free(dir_count);
	free(free_inode_count);
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
