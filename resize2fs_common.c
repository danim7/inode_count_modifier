/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */
 
#include "config.h"
#include "resize2fs.h"
#include <time.h>


//#ifdef __linux__			/* Kludge for debugging */
#define RESIZE2FS_DEBUG
//#endif


/*
 * Clean up the bitmaps for uninitialized bitmaps
 */
void fix_uninit_block_bitmaps(ext2_filsys fs)
{
	blk64_t		blk, lblk;
	dgrp_t		g;
	unsigned int	i;

	if (!ext2fs_has_group_desc_csum(fs))
		return;

	for (g=0; g < fs->group_desc_count; g++) {
		if (!(ext2fs_bg_flags_test(fs, g, EXT2_BG_BLOCK_UNINIT))) {
			continue;
		}
		

		blk = ext2fs_group_first_block2(fs, g);
		lblk = ext2fs_group_last_block2(fs, g);
		ext2fs_unmark_block_bitmap_range2(fs->block_map, blk,
						  lblk - blk + 1);

		ext2fs_reserve_super_and_bgd(fs, g, fs->block_map);
		ext2fs_mark_block_bitmap2(fs->block_map,
					  ext2fs_block_bitmap_loc(fs, g));
		ext2fs_mark_block_bitmap2(fs->block_map,
					  ext2fs_inode_bitmap_loc(fs, g));
		for (i = 0, blk = ext2fs_inode_table_loc(fs, g);
		     i < fs->inode_blocks_per_group;
		     i++, blk++)
			ext2fs_mark_block_bitmap2(fs->block_map, blk);
	}
}



/*
 * The extent translation table is stored in clusters so we need to
 * take special care when mapping a source block number to its
 * destination block number.
 */
__u64 extent_translate(ext2_filsys fs, ext2_extent extent, __u64 old_loc)
{
	__u64 new_block = C2B(ext2fs_extent_translate(extent, B2C(old_loc)));

	if (new_block != 0)
		new_block += old_loc & (EXT2FS_CLUSTER_RATIO(fs) - 1);
	return new_block;
}




/*
 * Progress callback
 */
errcode_t progress_callback(ext2_filsys fs,
				   ext2_inode_scan scan EXT2FS_ATTR((unused)),
				   dgrp_t group, void * priv_data)
{
	ext2_resize_t rfs = (ext2_resize_t) priv_data;
	errcode_t		retval;

	/*
	 * This check is to protect against old ext2 libraries.  It
	 * shouldn't be needed against new libraries.
	 */
	if ((group+1) == 0)
		return 0;

	if (rfs->progress) {
		io_channel_flush(fs->io);
		retval = (rfs->progress)(rfs, E2_RSZ_INODE_SCAN_PASS,
					 group+1, fs->group_desc_count);
		if (retval)
			return retval;
	}

	return 0;
}

errcode_t migrate_ea_block(ext2_resize_t rfs, ext2_ino_t ino,
				  struct ext2_inode *inode, int *changed)
{
	char *buf = NULL;
	blk64_t new_block;
	errcode_t err = 0;

	/* No EA block or no remapping?  Quit early. */
	if (ext2fs_file_acl_block(rfs->old_fs, inode) == 0 || !rfs->bmap)
		return 0;
	new_block = extent_translate(rfs->old_fs, rfs->bmap,
		ext2fs_file_acl_block(rfs->old_fs, inode));
	if (new_block == 0)
		return 0;

	/* Set the new ACL block */
	printf("migrate_ea_block, inode %u, old_block %llu, new_block %llu\n", ino,ext2fs_file_acl_block(rfs->old_fs, inode), new_block);
	ext2fs_file_acl_block_set(rfs->old_fs, inode, new_block);

	/* Update checksum */
	if (ext2fs_has_feature_metadata_csum(rfs->new_fs->super)) {
		err = ext2fs_get_mem(rfs->old_fs->blocksize, &buf);
		if (err)
			return err;
		rfs->old_fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		err = ext2fs_read_ext_attr3(rfs->old_fs, new_block, buf, ino);
		rfs->old_fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
		if (err)
			goto out;
		err = ext2fs_write_ext_attr3(rfs->old_fs, new_block, buf, ino);
		if (err)
			goto out;
	}
	*changed = 1;

out:
	ext2fs_free_mem(&buf);
	return err;
}

void quiet_com_err_proc(const char *whoami EXT2FS_ATTR((unused)),
			       errcode_t code EXT2FS_ATTR((unused)),
			       const char *fmt EXT2FS_ATTR((unused)),
			       va_list args EXT2FS_ATTR((unused)))
{
}

int fix_ea_entries(ext2_extent imap, struct ext2_ext_attr_entry *entry,
			  struct ext2_ext_attr_entry *end, ext2_ino_t last_ino)
{
	int modified = 0;
	ext2_ino_t new_ino;

	while (entry < end && !EXT2_EXT_IS_LAST_ENTRY(entry)) {
		if (entry->e_value_inum > last_ino) {
			new_ino = ext2fs_extent_translate(imap,
							  entry->e_value_inum);
			entry->e_value_inum = new_ino;
			modified = 1;
		}
		entry = EXT2_EXT_ATTR_NEXT(entry);
	}
	return modified;
}

int fix_ea_ibody_entries(ext2_extent imap,
				struct ext2_inode_large *inode, int inode_size,
				ext2_ino_t last_ino)
{
	struct ext2_ext_attr_entry *start, *end;
	__u32 *ea_magic;

	if (inode->i_extra_isize == 0)
		return 0;

	ea_magic = (__u32 *)((char *)inode + EXT2_GOOD_OLD_INODE_SIZE +
				inode->i_extra_isize);
	if (*ea_magic != EXT2_EXT_ATTR_MAGIC)
		return 0;

	start = (struct ext2_ext_attr_entry *)(ea_magic + 1);
	end = (struct ext2_ext_attr_entry *)((char *)inode + inode_size);

	return fix_ea_entries(imap, start, end, last_ino);
}

int fix_ea_block_entries(ext2_extent imap, char *block_buf,
				unsigned int blocksize, ext2_ino_t last_ino)
{
	struct ext2_ext_attr_header *header;
	struct ext2_ext_attr_entry *start, *end;

	header = (struct ext2_ext_attr_header *)block_buf;
	start = (struct ext2_ext_attr_entry *)(header+1);
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

errcode_t fix_ea_inode_refs(ext2_resize_t rfs, struct ext2_inode *inode,
				   char *block_buf, ext2_ino_t last_ino)
{
	ext2_filsys	fs = rfs->new_fs;
	ext2_inode_scan	scan = NULL;
	ext2_ino_t	ino;
	int		inode_size = EXT2_INODE_SIZE(fs->super);
	blk64_t		blk;
	int		modified;
	struct blk_cache blk_cache;
	struct ext2_ext_attr_header *header;
	errcode_t		retval;

	memset(&blk_cache, 0, sizeof(blk_cache));

	header = (struct ext2_ext_attr_header *)block_buf;

	retval = ext2fs_open_inode_scan(fs, 0, &scan);
	if (retval)
		goto out;

	while (1) {
		retval = ext2fs_get_next_inode_full(scan, &ino, inode,
						    inode_size);
		if (retval)
			goto out;
		if (!ino)
			break;

		if (inode->i_links_count == 0 && ino != EXT2_RESIZE_INO)
			continue; /* inode not in use */

		if (inode_size != EXT2_GOOD_OLD_INODE_SIZE) {
			modified = fix_ea_ibody_entries(rfs->imap,
					(struct ext2_inode_large *)inode,
					inode_size, last_ino);
			if (modified) {
				retval = ext2fs_write_inode_full(fs, ino, inode,
								 inode_size);
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

			modified = fix_ea_block_entries(rfs->imap, block_buf,
							fs->blocksize,
							last_ino);
			if (modified) {
				retval = ext2fs_write_ext_attr3(fs, blk,
								block_buf, ino);
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


int process_block(ext2_filsys fs, blk64_t	*block_nr,
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

	if (pb->is_dir) {
		retval = ext2fs_add_dir_block2(fs->dblist, pb->ino,
					       block, (int) blockcnt);
		if (retval) {
			pb->error = retval;
			ret |= BLOCK_ABORT;
		}
	}
	return ret;
}


/*
 * Finally, recalculate the summary information
 */
errcode_t resize2fs_calculate_summary_stats(ext2_filsys fs)
{
	errcode_t	retval;
	blk64_t		b, blk = fs->super->s_first_data_block;
	ext2_ino_t	ino;
	unsigned int	n, max, group, count;
	blk64_t		total_clusters_free = 0;
	int		total_inodes_free = 0;
	int		group_free = 0;
	int		uninit = 0;
	char		*bitmap_buf;

	/*
	 * First calculate the block statistics
	 */
	bitmap_buf = malloc(fs->blocksize);
	if (!bitmap_buf)
		return ENOMEM;
	for (group = 0; group < fs->group_desc_count; group++) {
		retval = ext2fs_get_block_bitmap_range2(fs->block_map,
			B2C(blk), fs->super->s_clusters_per_group, bitmap_buf);
		if (retval) {
			free(bitmap_buf);
			return retval;
		}
		max = ext2fs_group_blocks_count(fs, group) >>
		       fs->cluster_ratio_bits;
		if ((group == fs->group_desc_count - 1) && (max & 7)) {
			n = 0;
			for (b = (fs->super->s_first_data_block +
				  ((blk64_t) fs->super->s_blocks_per_group *
				   group));
			     b < ext2fs_blocks_count(fs->super);
			     b += EXT2FS_CLUSTER_RATIO(fs)) {
				if (ext2fs_test_block_bitmap2(fs->block_map, b))
					n++;
			}
		} else {
			n = ext2fs_bitcount(bitmap_buf, (max + 7) / 8);
		}
		group_free = max - n;
		total_clusters_free += group_free;
		ext2fs_bg_free_blocks_count_set(fs, group, group_free);
		ext2fs_group_desc_csum_set(fs, group);
		blk += fs->super->s_blocks_per_group;
	}
	free(bitmap_buf);
	ext2fs_free_blocks_count_set(fs->super, C2B(total_clusters_free));

	/*
	 * Next, calculate the inode statistics
	 */
	group_free = 0;
	count = 0;
	group = 0;

	/* Protect loop from wrap-around if s_inodes_count maxed */
	uninit = ext2fs_bg_flags_test(fs, group, EXT2_BG_INODE_UNINIT);
	for (ino = 1; ino <= fs->super->s_inodes_count && ino > 0; ino++) {
		if (uninit ||
		    !ext2fs_fast_test_inode_bitmap2(fs->inode_map, ino)) {
			group_free++;
			total_inodes_free++;
		}
		count++;
		if ((count == fs->super->s_inodes_per_group) ||
		    (ino == fs->super->s_inodes_count)) {
			ext2fs_bg_free_inodes_count_set(fs, group, group_free);
			ext2fs_group_desc_csum_set(fs, group);
			group++;
			if (group >= fs->group_desc_count)
				break;
			count = 0;
			group_free = 0;
			uninit = ext2fs_bg_flags_test(fs, group, EXT2_BG_INODE_UNINIT);
		}
	}
	fs->super->s_free_inodes_count = total_inodes_free;
	ext2fs_mark_super_dirty(fs);
	return 0;
}


/*
 * Fix the resize inode
 */
errcode_t fix_resize_inode(ext2_filsys fs)
{
	struct ext2_inode	inode;
	errcode_t		retval;

	if (!ext2fs_has_feature_resize_inode(fs->super))
		return 0;

	retval = ext2fs_read_inode(fs, EXT2_RESIZE_INO, &inode);
	if (retval) goto errout;

	ext2fs_iblk_set(fs, &inode, 1);

	retval = ext2fs_write_inode(fs, EXT2_RESIZE_INO, &inode);
	if (retval) goto errout;

	if (!inode.i_block[EXT2_DIND_BLOCK]) {
		/*
		 * Avoid zeroing out block #0; that's rude.  This
		 * should never happen anyway since the filesystem
		 * should be fsck'ed and we assume it is consistent.
		 */
		fprintf(stderr, "%s",
			_("Should never happen: resize inode corrupt!\n"));
		exit(1);
	}

	retval = ext2fs_zero_blocks2(fs, inode.i_block[EXT2_DIND_BLOCK], 1,
				     NULL, NULL);
	if (retval)
		goto errout;

	retval = ext2fs_create_resize_inode(fs);
	if (retval)
		goto errout;

errout:
	return retval;
}


struct process_orphan_block_data {
	char 		*buf;
	errcode_t	errcode;
	ext2_ino_t	ino;
	__u32		generation;
};

static int process_orphan_block(ext2_filsys fs,
			       blk64_t	*block_nr,
			       e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
			       blk64_t	ref_blk EXT2FS_ATTR((unused)),
			       int	ref_offset EXT2FS_ATTR((unused)),
			       void *priv_data)
{
	struct process_orphan_block_data *pd = priv_data;
	struct ext4_orphan_block_tail *tail;
	blk64_t			blk = *block_nr;
	__le32			new_crc;

	pd->errcode = io_channel_read_blk64(fs->io, blk, 1, pd->buf);
	if (pd->errcode)
		return BLOCK_ABORT;
	tail = ext2fs_orphan_block_tail(fs, pd->buf);
	new_crc = ext2fs_cpu_to_le32(ext2fs_do_orphan_file_block_csum(fs,
			pd->ino, pd->generation, blk, pd->buf));
	if (new_crc == tail->ob_checksum)
		return 0;
	tail->ob_checksum = new_crc;
	pd->errcode = io_channel_write_blk64(fs->io, blk, 1, pd->buf);
	if (pd->errcode)
		return BLOCK_ABORT;
	return 0;
}

/*
 * Fix the checksums in orphan_file inode
 */
errcode_t fix_orphan_file_inode(ext2_filsys fs)
{
	struct process_orphan_block_data pd;
	struct ext2_inode	inode;
	errcode_t		retval;
	ext2_ino_t		orphan_inum;
	char			*orphan_buf;

	if (!ext2fs_has_feature_orphan_file(fs->super) ||
	    !ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	orphan_inum = fs->super->s_orphan_file_inum;
	retval = ext2fs_read_inode(fs, orphan_inum, &inode);
	if (retval)
		return retval;
	orphan_buf = malloc(fs->blocksize * 4);
	if (!orphan_buf)
		return ENOMEM;

	pd.errcode = 0;
	pd.buf = orphan_buf + 3 * fs->blocksize;
	pd.ino = orphan_inum;
	pd.generation = inode.i_generation;

	retval = ext2fs_block_iterate3(fs, fs->super->s_orphan_file_inum,
				       BLOCK_FLAG_DATA_ONLY,
				       orphan_buf, process_orphan_block, &pd);
	free(orphan_buf);
	return (retval ? retval : pd.errcode);
}


/*
 *  Journal may have been relocated; update the backup journal blocks
 *  in the superblock.
 */
errcode_t fix_sb_journal_backup(ext2_filsys fs)
{
	errcode_t	  retval;
	struct ext2_inode inode;

	if (!ext2fs_has_feature_journal(fs->super))
		return 0;

	/* External journal? Nothing to do. */
	if (fs->super->s_journal_dev && !fs->super->s_journal_inum)
		return 0;

	retval = ext2fs_read_inode(fs, fs->super->s_journal_inum, &inode);
	if (retval)
		return retval;
	memcpy(fs->super->s_jnl_blocks, inode.i_block, EXT2_N_BLOCKS*4);
	fs->super->s_jnl_blocks[15] = inode.i_size_high;
	fs->super->s_jnl_blocks[16] = inode.i_size;
	fs->super->s_jnl_backup_type = EXT3_JNL_BACKUP_BLOCKS;
	ext2fs_mark_super_dirty(fs);
	return 0;
}


