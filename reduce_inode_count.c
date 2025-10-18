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
 *      2.  Move the inodes in use above that number to lower numbers
 *          2.a. For those inodes, update the references in folders entries
 *      3.  Reset stats for groups in new fs
 *      4.  Migrate all the inodes to the new reduced inodes tables
 *      5.  Free the remaining space. For flex_bg filesystems, try to place the new inode tables contiguously
 *
 */

#include "config.h"
#include "resize2fs.h"


static errcode_t inode_relocation_to_smaller_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group);

errcode_t reduce_inode_count(ext2_filsys fs, int flags, errcode_t(*progress) (ext2_resize_t rfs, int pass, unsigned long cur, unsigned long max_val), unsigned int new_inodes_per_group)
{
	ext2_resize_t rfs;
	errcode_t retval;
	struct resource_track rtrack, overall_track;

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
	rfs->itable_buf = 0;
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

	retval = ext2fs_dup_handle(fs, &rfs->new_fs);
	if (retval)
		goto errout;

	init_resource_track(&rtrack, "inode_relocation_to_smaller_tables", fs->io);
	retval = inode_relocation_to_smaller_tables(rfs, new_inodes_per_group);
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

/*auxiliar function to update inode reference when the inode number changes*/
static int fix_ea_entries(ext2_extent imap, struct ext2_ext_attr_entry *entry, struct ext2_ext_attr_entry *end, ext2_ino_t last_ino)
{
	int modified = 0;
	ext2_ino_t new_ino;

	while (entry < end && !EXT2_EXT_IS_LAST_ENTRY(entry)) {
		if (entry->e_value_inum > last_ino) {
			new_ino = ext2fs_extent_translate(imap, entry->e_value_inum);
			entry->e_value_inum = new_ino;
			modified = 1;
		}
		entry = EXT2_EXT_ATTR_NEXT(entry);
	}
	return modified;
}

/*auxiliar function to update inode reference when the inode number changes*/
static int fix_ea_ibody_entries(ext2_extent imap, struct ext2_inode_large *inode, int inode_size, ext2_ino_t last_ino)
{
	struct ext2_ext_attr_entry *start, *end;
	__u32 *ea_magic;

	if (inode->i_extra_isize == 0)
		return 0;

	ea_magic = (__u32 *) ((char *)inode + EXT2_GOOD_OLD_INODE_SIZE + inode->i_extra_isize);
	if (*ea_magic != EXT2_EXT_ATTR_MAGIC)
		return 0;

	start = (struct ext2_ext_attr_entry *)(ea_magic + 1);
	end = (struct ext2_ext_attr_entry *)((char *)inode + inode_size);

	return fix_ea_entries(imap, start, end, last_ino);
}

/*auxiliar function to update inode reference when the inode number changes*/
static int fix_ea_block_entries(ext2_extent imap, char *block_buf, unsigned int blocksize, ext2_ino_t last_ino)
{
	struct ext2_ext_attr_header *header;
	struct ext2_ext_attr_entry *start, *end;

	header = (struct ext2_ext_attr_header *)block_buf;
	start = (struct ext2_ext_attr_entry *)(header + 1);
	end = (struct ext2_ext_attr_entry *)(block_buf + blocksize);

	return fix_ea_entries(imap, start, end, last_ino);
}

/* A simple LRU cache to check recently processed blocks. */
struct blk_cache {
	int cursor;
	blk64_t blks[4];
};

#define BLK_IN_CACHE(b,c) ((b) == (c).blks[0] || (b) == (c).blks[1] || \
			   (b) == (c).blks[2] || (b) == (c).blks[3])
#define BLK_ADD_CACHE(b,c) { 			\
	(c).blks[(c).cursor] = (b);		\
	(c).cursor = ((c).cursor + 1) % 4;	\
}

/*this function will update inode references when the inum changes.
to do so, it calls fix_ea_block_entries(), fix_ea_ibody_entries() and fix_ea_entries()*/
static errcode_t fix_ea_inode_refs(ext2_resize_t rfs, struct ext2_inode *inode, char *block_buf, ext2_ino_t last_ino)
{
	ext2_filsys fs = rfs->old_fs;
	ext2_inode_scan scan = NULL;
	ext2_ino_t ino;
	int inode_size = EXT2_INODE_SIZE(fs->super);
	blk64_t blk;
	int modified;
	struct blk_cache blk_cache;
	struct ext2_ext_attr_header *header;
	errcode_t retval;

	memset(&blk_cache, 0, sizeof(blk_cache));

	header = (struct ext2_ext_attr_header *)block_buf;

	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval)
		goto out;

	while (1) {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode, inode_size);
		if (retval)
			goto out;
		if (!ino)
			break;

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue;	/* inode not in use */

		if (inode_size != EXT2_GOOD_OLD_INODE_SIZE) {
			modified = fix_ea_ibody_entries(rfs->imap, (struct ext2_inode_large *)inode, inode_size, last_ino);
			if (modified) {
				retval = ext2fs_write_inode_full(fs, ino, inode, inode_size);
				if (retval)
					goto out;
			}
		}

		blk = ext2fs_file_acl_block(fs, inode);
		if (blk && !BLK_IN_CACHE(blk, blk_cache)) {
			printf("fix_ea_inode_refs, inode %u, block %llu\n", ino, blk);
			retval = ext2fs_read_ext_attr3(fs, blk, block_buf, ino);
			if (retval)
				goto out;

			modified = fix_ea_block_entries(rfs->imap, block_buf, fs->blocksize, last_ino);
			if (modified) {
				retval = ext2fs_write_ext_attr3(fs, blk, block_buf, ino);
				if (retval)
					goto out;
				/*
				 * If refcount is greater than 1, we might see
				 * the same block referenced by other inodes
				 * later.
				 */
				if (header->h_refcount > 1)
					BLK_ADD_CACHE(blk, blk_cache);
			}
		}
	}
	retval = 0;
 out:
	if (scan)
		ext2fs_close_inode_scan(scan);
	return retval;

}

struct istruct {
	ext2_resize_t rfs;
	errcode_t err;
	unsigned int max_dirs;
	unsigned int num;
};

static int check_and_change_inodes(ext2_ino_t dir, int entry EXT2FS_ATTR((unused)), struct ext2_dir_entry *dirent, int offset, int blocksize EXT2FS_ATTR((unused)), char *buf EXT2FS_ATTR((unused)), void *priv_data)
{
	struct istruct *is = (struct istruct *)priv_data;
	struct ext2_inode inode;
	ext2_ino_t new_inode;
	errcode_t retval;
	int ret = 0;

	/*
	 * If we have checksums enabled and the inode isn't present in the
	 * new fs, then we must rewrite all dir blocks with new checksums.
	 */
	if (ext2fs_has_feature_metadata_csum(is->rfs->new_fs->super) && !ext2fs_test_inode_bitmap2(is->rfs->new_fs->inode_map, dir))
		ret |= DIRENT_CHANGED;

	if (!dirent->inode)
		return ret;

	new_inode = ext2fs_extent_translate(is->rfs->imap, dirent->inode);

	if (!new_inode)
		return ret;

	printf("Inode translate (dir=%u, name=%.*s, %u->%u)\n", dir, ext2fs_dirent_name_len(dirent), dirent->name, dirent->inode, new_inode);

	dirent->inode = new_inode;

	/* Update the directory mtime and ctime */
	retval = ext2fs_read_inode(is->rfs->old_fs, dir, &inode);

	if (retval == 0) {
		inode.i_mtime = inode.i_ctime = is->rfs->new_fs->now ? is->rfs->new_fs->now : time(0);
		is->err = ext2fs_write_inode(is->rfs->old_fs, dir, &inode);
		if (is->err) {
			return ret | DIRENT_ABORT;
		}
	}

	return ret | DIRENT_CHANGED;
}

static errcode_t inode_ref_fix(ext2_resize_t rfs)
{
	errcode_t retval;
	struct istruct is;

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

	rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	retval = ext2fs_dblist_dir_iterate(rfs->old_fs->dblist, DIRENT_FLAG_INCLUDE_EMPTY, 0, check_and_change_inodes, &is);
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (retval)
		goto errout;
	if (is.err) {
		retval = is.err;
		goto errout;
	}

 errout:
	ext2fs_free_extent_table(rfs->imap);
	rfs->imap = 0;
	return retval;
}

static int feed_dblist(ext2_filsys fs, blk64_t *block_nr, e2_blkcnt_t blockcnt, blk64_t ref_block EXT2FS_ATTR((unused)), int ref_offset EXT2FS_ATTR((unused)), void *priv_data)
{
	struct process_block_struct *pb;
	errcode_t retval;
	blk64_t block, new_block;
	int ret = 0;

	pb = (struct process_block_struct *)priv_data;
	block = *block_nr;

	if (pb->is_dir) {
		retval = ext2fs_add_dir_block2(fs->dblist, pb->ino, block, (int)blockcnt);
		if (retval) {
			pb->error = retval;
			ret |= BLOCK_ABORT;
		}
	}
	return ret;
}

static errcode_t inode_scan_and_fix(ext2_resize_t rfs)
{
	struct process_block_struct pb;
	ext2_ino_t ino, new_inode;
	struct ext2_inode *inode = NULL;
	ext2_inode_scan scan = NULL;
	errcode_t retval;
	char *block_buf = 0;
	ext2_ino_t start_to_move;
	int inode_size;
	int update_ea_inode_refs = 0;

	rfs->bmap = 0;

	set_com_err_hook(quiet_com_err_proc);

	retval = ext2fs_open_inode_scan(rfs->old_fs, 0, &scan);
	if (retval)
		goto errout;

	retval = ext2fs_init_dblist(rfs->old_fs, 0);
	if (retval)
		goto errout;
	retval = ext2fs_get_array(rfs->old_fs->blocksize, 3, &block_buf);
	if (retval)
		goto errout;

	start_to_move = (rfs->new_fs->group_desc_count * rfs->new_fs->super->s_inodes_per_group);
	printf("start_to_move: %u\n", start_to_move);

	/*
	 * Check to make sure there are enough inodes
	 */
	if ((rfs->old_fs->super->s_inodes_count - rfs->old_fs->super->s_free_inodes_count) > start_to_move) {
		retval = ENOSPC;
		printf("not enough inodes\n");
		exit(1);
		goto errout;
	}

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
		if (retval)
			goto errout;
		if (!ino)
			break;

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue;	/* inode not in use */

		pb.is_dir = LINUX_S_ISDIR(inode->i_mode);
		pb.changed = 0;

		new_inode = ino;
		if (ino <= start_to_move)
			goto remap_inodes;	/* Don't need to move inode. */

		/*
		 * Find a new inode.  Now that extents and directory blocks
		 * are tied to the inode number through the checksum, we must
		 * set up the new inode before we start rewriting blocks.
		 */
		retval = ext2fs_new_inode(rfs->old_fs, 0, 0, 0, &new_inode);
		if (retval)
			goto errout;

		ext2fs_inode_alloc_stats2(rfs->old_fs, new_inode, +1, pb.is_dir);
		/*
		 * i_ctime field in xattr inodes contain a portion of the ref
		 * count, do not overwrite.
		 */
		if (inode->i_flags & EXT4_EA_INODE_FL) {
			update_ea_inode_refs = 1;
			printf("EA inode: old %u, new %u\n", ino, new_inode);
		} else
			inode->i_ctime = rfs->old_fs->now ? rfs->old_fs->now : time(0);

		retval = ext2fs_write_inode_full(rfs->old_fs, new_inode, inode, inode_size);
		if (retval)
			goto errout;
		pb.changed = 0;

		printf("Inode moved %u->%u\n", ino, new_inode);

		if (!rfs->imap) {
			retval = ext2fs_create_extent_table(&rfs->imap, 0);
			if (retval)
				goto errout;
		}
		ext2fs_add_extent_entry(rfs->imap, ino, new_inode);

 remap_inodes:

		/*
		 * Schedule directory blocks for inode remapping.  Need to write out dir blocks
		 * with new inode numbers if we have metadata_csum enabled.
		 */
		rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		if (ext2fs_inode_has_valid_blocks2(rfs->old_fs, inode) && (pb.is_dir)) {
			pb.ino = new_inode;
			pb.old_ino = ino;
			pb.has_extents = inode->i_flags & EXT4_EXTENTS_FL;
			retval = ext2fs_block_iterate3(rfs->old_fs, new_inode, 0, block_buf, feed_dblist, &pb);
			if (retval)
				goto errout;
			if (pb.error) {
				retval = pb.error;
				goto errout;
			}
		} else if ((inode->i_flags & EXT4_INLINE_DATA_FL) && (pb.is_dir)) {
			/* inline data dir; update it too */
			retval = ext2fs_add_dir_block2(rfs->old_fs->dblist, new_inode, 0, 0);
			if (retval)
				goto errout;
		}

		/* Fix up extent block checksums with the new inode number */
		if (ext2fs_has_feature_metadata_csum(rfs->old_fs->super) && (inode->i_flags & EXT4_EXTENTS_FL)) {
			retval = ext2fs_fix_extents_checksums(rfs->old_fs, new_inode, NULL);
			if (retval)
				goto errout;
		}
	}

	if (update_ea_inode_refs && ext2fs_has_feature_ea_inode(rfs->old_fs->super)) {
		retval = fix_ea_inode_refs(rfs, inode, block_buf, start_to_move);
		if (retval)
			goto errout;
	}
	io_channel_flush(rfs->old_fs->io);

 errout:
	reset_com_err_hook();
	rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (scan)
		ext2fs_close_inode_scan(scan);
	if (block_buf)
		ext2fs_free_mem(&block_buf);
	free(inode);
	return retval;
}

static errcode_t reubicate_and_free_itables(ext2_resize_t rfs)
{
	errcode_t retval = 0;
	dgrp_t group;
	int flexbg_size = 0, flexbg_i;
	unsigned int itables_blocks_to_be_freed;
	blk64_t after_prev_itable;

	if (ext2fs_has_feature_flex_bg(rfs->new_fs->super)) {
		flexbg_size = 1U << rfs->new_fs->super->s_log_groups_per_flex;
		if (!rfs->itable_buf) {
			retval = ext2fs_get_array(rfs->new_fs->blocksize, rfs->new_fs->inode_blocks_per_group, &rfs->itable_buf);
			if (retval)
				goto errout;

			memset(rfs->itable_buf, 0, rfs->new_fs->blocksize * rfs->new_fs->inode_blocks_per_group);
		}
		for (flexbg_i = 0; flexbg_i < rfs->new_fs->group_desc_count; flexbg_i += flexbg_size) {

			after_prev_itable = ext2fs_inode_table_loc(rfs->old_fs, flexbg_i) + rfs->new_fs->inode_blocks_per_group;
			for (group = flexbg_i + 1; group < flexbg_i + flexbg_size && group < rfs->new_fs->group_desc_count; group++) {
				printf("group %u, after_prev_itable: %llu\n", group, after_prev_itable);
				if (ext2fs_inode_table_loc(rfs->old_fs, group - 1) + rfs->old_fs->inode_blocks_per_group == ext2fs_inode_table_loc(rfs->old_fs, group)) {
					printf("moving itables, contiguous groups %u, %u, itables in %llu, %llu\n",
						group - 1, group, ext2fs_inode_table_loc(rfs->old_fs, group - 1), ext2fs_inode_table_loc(rfs->old_fs, group));

					ext2fs_inode_table_loc_set(rfs->new_fs, group, after_prev_itable);
					retval = io_channel_read_blk64(rfs->old_fs->io, ext2fs_inode_table_loc(rfs->old_fs, group), rfs->new_fs->inode_blocks_per_group, rfs->itable_buf);
					if (retval)
						goto errout;
					retval = io_channel_write_blk64(rfs->old_fs->io, after_prev_itable, rfs->new_fs->inode_blocks_per_group, rfs->itable_buf);
					if (retval)
						goto errout;

					ext2fs_group_desc_csum_set(rfs->new_fs, group);
					after_prev_itable += rfs->new_fs->inode_blocks_per_group;

				} else {
					itables_blocks_to_be_freed = ext2fs_inode_table_loc(rfs->old_fs, group - 1) + rfs->old_fs->inode_blocks_per_group - after_prev_itable;
					if (ext2fs_has_feature_bigalloc(rfs->new_fs->super))
						tweak_values_for_bigalloc(rfs, &after_prev_itable, &itables_blocks_to_be_freed);
					ext2fs_block_alloc_stats_range(rfs->new_fs, after_prev_itable, itables_blocks_to_be_freed, -1);
					printf("partial continuity, unmarking %u blocks starting on itable %llu\n", itables_blocks_to_be_freed, after_prev_itable);

					after_prev_itable = ext2fs_inode_table_loc(rfs->old_fs, group) + rfs->new_fs->inode_blocks_per_group;
				}
			}

			itables_blocks_to_be_freed = ext2fs_inode_table_loc(rfs->old_fs, group - 1) + rfs->old_fs->inode_blocks_per_group - after_prev_itable;
			if (ext2fs_has_feature_bigalloc(rfs->new_fs->super))
				tweak_values_for_bigalloc(rfs, &after_prev_itable, &itables_blocks_to_be_freed);
			ext2fs_block_alloc_stats_range(rfs->new_fs, after_prev_itable, itables_blocks_to_be_freed, -1);
			printf("full continuity, unmarking %u blocks starting on itable %llu\n", itables_blocks_to_be_freed, after_prev_itable);

		}
	} else { /*no flex_bg */
		for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
			itables_blocks_to_be_freed = rfs->old_fs->inode_blocks_per_group - rfs->new_fs->inode_blocks_per_group;
			after_prev_itable = ext2fs_inode_table_loc(rfs->new_fs, group) + rfs->new_fs->inode_blocks_per_group;
			if (ext2fs_has_feature_bigalloc(rfs->new_fs->super))
				tweak_values_for_bigalloc(rfs, &after_prev_itable, &itables_blocks_to_be_freed);
			ext2fs_block_alloc_stats_range(rfs->new_fs, after_prev_itable, itables_blocks_to_be_freed, -1);
		}
	}

 errout:
	if (rfs->itable_buf)
		ext2fs_free_mem(&rfs->itable_buf);
	return retval;

}

/*****************************************************************************************************
We will migrate the inodes in-place to the existing tables. We do not allocate new itables when
reducing the inode count. The advantage of this approach is that we don't need any free blocks in the
filesystem for this operation. It could be done in a FS with zero free blocks, which might also be the
reason to run the reducer: free some space for data.

Let:
g: block group, 0-based
ipg: inodes per group
pos: inode position within the inode table of the block group, 1-based
inum: inode number

This gives the inode number formula:
inum = g * ipg + pos

When reducing the inode count, we start from the highest inode number and go down to 1.

This operation shall not overwrite a needed inode before it is migrated:
To overwrite a needed inode in a given g & pos before being migrated, its inum_old shall be
lesser than its inum_new, as the migration loop is moving backwards. So:

inum_old < inum_new

Substitute. As we talk about the same memory address, g & pos are the same, only ipg changes:

g * ipg_old + pos < g * ipg_new + pos

Thus, an overwrite will require ipg_olg < ipg_new.
However, we are reducing the inode count, so we are doing ipg_old > ipg_new.
Therefore, the migration loop will not overwrite needed inodes before migrating them.
****************************************************************************************************/
static errcode_t migrate_inodes_backwards_loop(ext2_resize_t rfs)
{
	ext2_ino_t ino;
	struct ext2_inode *inode = NULL;
	errcode_t retval;
	int inode_size = EXT2_INODE_SIZE(rfs->new_fs->super);

	inode = malloc(inode_size);
	if (!inode) {
		retval = ENOMEM;
		goto errout;
	}

	for (ino = rfs->new_fs->super->s_inodes_count; ino > 0; ino--) {

		retval = ext2fs_read_inode2(rfs->old_fs, ino, inode, inode_size, 0);	/*we might use READ_INODE_NOCSUM instead of checking retval */
		printf("Migrating inode %u to new itable: links_count: %u, i_size_lo: %u, i_blocks: %u, old group: %u, new group: %u, read_retval: %li",
			ino, inode->i_links_count, inode->i_size, inode->i_blocks, ext2fs_group_of_ino(rfs->old_fs, ino), ext2fs_group_of_ino(rfs->new_fs, ino), retval);
		/*we require to run fsck before changing the inode count, and that will fix inode checksums on used inodes. However, an unused inode with a wrong
		   checksum will not be detected by fsck. We don't want to stop the whole process now and leave a messy fs because of that, just log it and continue */
		if (retval && retval != EXT2_ET_INODE_CSUM_INVALID)
			goto errout;

		if (inode->i_links_count != 0 || ino < EXT2_FIRST_INODE(rfs->new_fs->super)) {
			ext2fs_inode_alloc_stats2(rfs->new_fs, ino, +1, LINUX_S_ISDIR(inode->i_mode));
		}

		/*if not in use, write the zeros from the inode to the itable anyway, as it may contain the previous inode */
		retval = ext2fs_write_inode2(rfs->new_fs, ino, inode, inode_size, 0);
		printf(" - write_retval: %li\n", retval);
		if (retval)
			goto errout;

	}

 errout:
	if (inode)
		free(inode);

	return retval;
}

static errcode_t inode_relocation_to_smaller_tables(ext2_resize_t rfs, unsigned int new_inodes_per_group)
{
	errcode_t retval;
	dgrp_t group;

	rfs->new_fs->super->s_inodes_per_group = new_inodes_per_group;
	rfs->new_fs->inode_blocks_per_group = ext2fs_div_ceil(rfs->new_fs->super->s_inodes_per_group * rfs->new_fs->super->s_inode_size, rfs->new_fs->blocksize);
	rfs->new_fs->super->s_inodes_count = rfs->new_fs->group_desc_count * rfs->new_fs->super->s_inodes_per_group;

	display_info(rfs);

	printf("calling inode_scan_and_fix()\n");
	retval = inode_scan_and_fix(rfs);
	if (retval)
		goto errout;

	printf("calling inode_ref_fix()\n");
	retval = inode_ref_fix(rfs);
	if (retval)
		goto errout;

	io_channel_flush(rfs->old_fs->io);

	for (group = 0; group < rfs->new_fs->group_desc_count; group++) {
		ext2fs_bg_used_dirs_count_set(rfs->new_fs, group, 0);
		ext2fs_bg_free_inodes_count_set(rfs->new_fs, group, rfs->new_fs->super->s_inodes_per_group);
		ext2fs_bg_itable_unused_set(rfs->new_fs, group, rfs->new_fs->super->s_inodes_per_group);
	}
	rfs->new_fs->super->s_free_inodes_count = rfs->new_fs->super->s_inodes_count;

	printf("calling migrate_inodes_backwards_loop()\n");
	retval = migrate_inodes_backwards_loop(rfs);
	if (retval)
		goto errout;

	printf("calling reubicate_and_free_itables()\n");
	retval = reubicate_and_free_itables(rfs);
	if (retval)
		goto errout;

	ext2fs_mark_super_dirty(rfs->new_fs);
	io_channel_flush(rfs->new_fs->io);

 errout:

	return retval;
}

