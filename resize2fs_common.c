/*
 * resize2fs.c --- ext2 main routine
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000 by Theodore Ts'o
 *
 * resize2fs_common.c - functions used by inode_count_modifier
 *
 * Copyright (C) 2025 by danim7 (https://github.com/danim7)
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

void quiet_com_err_proc(const char *whoami EXT2FS_ATTR((unused)),
			       errcode_t code EXT2FS_ATTR((unused)),
			       const char *fmt EXT2FS_ATTR((unused)),
			       va_list args EXT2FS_ATTR((unused)))
{
}

errcode_t mark_table_blocks(ext2_filsys fs,
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


/*The function ext2fs_block_alloc_stats_range() doesn't work very well on bigalloc filesystems if we pass to it
unaligned blocks or uneven lengths, which may be the case with itables.
This function will modify the input values to compensate for those issues before calling the other funcion.
We assume any other metadata (blockmaps, inodemaps, etc...) is always placed before inode tables if they share the same cluster
(which is how mkfs creates the filesystems), so we avoid to free the cluster if the itable doesn't own its first block*/
errcode_t tweak_values_for_bigalloc(ext2_resize_t rfs, blk64_t *first_block, unsigned int *num_blocks) {
        blk64_t start, end, diff, cluster_size = EXT2FS_CLUSTER_RATIO(rfs->new_fs);

        start = *first_block;
        end = *first_block + *num_blocks - 1;
	printf("tweak_values_for_bigalloc: before values: first_block: %llu, num_blocks %u, end %llu\n", *first_block, *num_blocks, end);

	/*If the first block to free is not aligned with the beginning of the cluster, we will move it to the beginning of the next cluster.*/
        if (start%cluster_size) {
              diff = cluster_size-(start%cluster_size);
              *first_block += diff;

              /*overflow case if we are freeing very few blocks*/
              if (*num_blocks <= diff)
                  *num_blocks = 0;
              else
                  *num_blocks -= diff;
        }

        /*If the end block is not aligned with the end of the cluster, we will move it to the end of the current cluster.*/
        if (!((end&EXT2FS_CLUSTER_MASK(rfs->new_fs)) == EXT2FS_CLUSTER_MASK(rfs->new_fs)) && *num_blocks != 0)
             *num_blocks += cluster_size-(end%cluster_size)-1;

        /*If we have many itables in the same cluster (aka.: very small num_blocks),
        the responsible to free the cluster will be the call having the first block of the cluster.
        The other calls shall not attempt to free it to avoid duplicated frees*/
        if (*first_block > (end&~EXT2FS_CLUSTER_MASK(rfs->new_fs))) {
              if (*num_blocks <= cluster_size)
                    *num_blocks = 0;
              else
                    *num_blocks -= cluster_size;
        }
        printf("tweak_values_for_bigalloc: after values: first_block: %llu, num_blocks %u, end %llu\n", *first_block, *num_blocks, *first_block + *num_blocks - 1);

}

