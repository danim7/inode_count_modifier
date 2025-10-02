/*
 * main.c --- ext2 resizer main program
 *
 * Copyright (C) 1997, 1998 by Theodore Ts'o and
 * 	PowerQuest, Inc.
 *
 * Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 by Theodore Ts'o
 *
 * inode_count_modifier --- reduce the inode count of an existing ext4 filesystem
 * 
 * Copyright (C) 2025 by danim7 (https://github.com/danim7)
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <unistd.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include "resize2fs.h"


char *program_name;
static char *device_name, *io_options;

static void usage (char *prog)
{
	/*fprintf (stderr, _("Usage: %s [-d debug_flags] [-f] [-F] [-M] [-P] "
			   "[-p] device [-b|-s|new_size] [-S RAID-stride] "
			   "[-z undo_file]\n\n"),
		 prog ? prog : "resize2fs");*/
	fprintf (stderr, _("Usage: %s [-f] -c|-r new_value device \n\n"),
		 prog ? prog : "inode_count_modifier");

	exit (1);
}

static errcode_t resize_progress_func(ext2_resize_t rfs, int pass,
				      unsigned long cur, unsigned long max)
{
	ext2_sim_progmeter progress;
	const char	*label;
	errcode_t	retval;

	progress = (ext2_sim_progmeter) rfs->prog_data;
	if (max == 0)
		return 0;
	if (cur == 0) {
		if (progress)
			ext2fs_progress_close(progress);
		progress = 0;
		switch (pass) {
		case E2_RSZ_EXTEND_ITABLE_PASS:
			label = _("Extending the inode table");
			break;
		case E2_RSZ_BLOCK_RELOC_PASS:
			label = _("Relocating blocks");
			break;
		case E2_RSZ_INODE_SCAN_PASS:
			label = _("Scanning inode table");
			break;
		case E2_RSZ_INODE_REF_UPD_PASS:
			label = _("Updating inode references");
			break;
		case E2_RSZ_MOVE_ITABLE_PASS:
			label = _("Moving inode table");
			break;
		default:
			label = _("Unknown pass?!?");
			break;
		}
		printf(_("Begin pass %d (max = %lu)\n"), pass, max);
		retval = ext2fs_progress_init(&progress, label, 30,
					      40, max, 0);
		if (retval)
			progress = 0;
		rfs->prog_data = (void *) progress;
	}
	if (progress)
		ext2fs_progress_update(progress, cur);
	if (cur >= max) {
		if (progress)
			ext2fs_progress_close(progress);
		progress = 0;
		rfs->prog_data = 0;
	}
	return 0;
}

static void bigalloc_check(ext2_filsys fs, int force)
{
	if (!force && ext2fs_has_feature_bigalloc(fs->super)) {
		fprintf(stderr, "%s", _("\nResizing bigalloc file systems has "
					"not been fully tested.  Proceed at\n"
					"your own risk!  Use the force option "
					"if you want to go ahead anyway.\n\n"));
		exit(1);
	}
}

static int resize2fs_setup_tdb(const char *device, char *undo_file,
			       io_manager *io_ptr)
{
	errcode_t retval = ENOMEM;
	const char *tdb_dir = NULL;
	char *tdb_file = NULL;
	char *dev_name, *tmp_name;

	/* (re)open a specific undo file */
	if (undo_file && undo_file[0] != 0) {
		retval = set_undo_io_backing_manager(*io_ptr);
		if (retval)
			goto err;
		*io_ptr = undo_io_manager;
		retval = set_undo_io_backup_file(undo_file);
		if (retval)
			goto err;
		printf(_("Overwriting existing filesystem; this can be undone "
			 "using the command:\n"
			 "    e2undo %s %s\n\n"),
			undo_file, device);
		return retval;
	}

	/*
	 * Configuration via a conf file would be
	 * nice
	 */
	tdb_dir = getenv("E2FSPROGS_UNDO_DIR");
	if (!tdb_dir)
		tdb_dir = "/var/lib/e2fsprogs";

	if (!strcmp(tdb_dir, "none") || (tdb_dir[0] == 0) ||
	    access(tdb_dir, W_OK))
		return 0;

	tmp_name = strdup(device);
	if (!tmp_name)
		goto errout;
	dev_name = basename(tmp_name);
	tdb_file = malloc(strlen(tdb_dir) + 11 + strlen(dev_name) + 7 + 1);
	if (!tdb_file) {
		free(tmp_name);
		goto errout;
	}
	sprintf(tdb_file, "%s/resize2fs-%s.e2undo", tdb_dir, dev_name);
	free(tmp_name);

	if ((unlink(tdb_file) < 0) && (errno != ENOENT)) {
		retval = errno;
		com_err(program_name, retval,
			_("while trying to delete %s"), tdb_file);
		goto errout;
	}

	retval = set_undo_io_backing_manager(*io_ptr);
	if (retval)
		goto errout;
	*io_ptr = undo_io_manager;
	retval = set_undo_io_backup_file(tdb_file);
	if (retval)
		goto errout;
	printf(_("Overwriting existing filesystem; this can be undone "
		 "using the command:\n"
		 "    e2undo %s %s\n\n"), tdb_file, device);

	free(tdb_file);
	return 0;
errout:
	free(tdb_file);
err:
	com_err(program_name, retval, "%s",
		_("while trying to setup undo file\n"));
	return retval;
}


static int check_space_last_group(ext2_filsys fs, unsigned int inode_blocks_per_group) {

	ext2fs_block_bitmap	meta_bmap;
	blk64_t                 b;
	errcode_t	        retval;
	unsigned int            movable_blocks = 0;
	ext2_badblocks_list	badblock_list = 0;


	retval = ext2fs_allocate_block_bitmap(fs, _("meta-data blocks"), &meta_bmap);
	if (retval)
		return retval;

	retval = mark_table_blocks(fs, meta_bmap); //mark as used in meta_bmap the SB, BGD, reserved GDT, bitmaps, itables and MMP
	if (retval)
		return retval;

	retval = ext2fs_read_bb_inode(fs, &badblock_list);
	if (retval) {
		printf("Error while reading badblock list in check_space_last_group()\n");
		exit(1);
	}

	for (b = ext2fs_group_first_block2(fs, fs->group_desc_count-1); b < ext2fs_blocks_count(fs->super); b++) {
	        if (!ext2fs_test_block_bitmap2(meta_bmap, b) && !ext2fs_badblocks_list_test(badblock_list, b))
	              movable_blocks++;
	}

/*TODO: This could be further optimized. For the given filesystem and new inode count, we may calculate if
  the last inode table could be evacuated and its space freed before we allocate the new itable. For those cases,
  the if condition could be updated to movable_blocks < (inode_blocks_per_group-fs->inode_blocks_per_group)*/
	if (movable_blocks < inode_blocks_per_group) {
	      printf("************ STOP ************\n");
	      if (!ext2fs_has_feature_flex_bg(fs->super))
	          printf("The filesystem flex_bg feature is not set\n");
	      if (!fs->super->s_log_groups_per_flex)
	          printf("The value of s_log_groups_per_flex is %u\n", fs->super->s_log_groups_per_flex);
	      printf("The last group only has %u movable blocks\n", movable_blocks);
	      printf("This is not enough to allocate a new inode table of %u blocks\n", inode_blocks_per_group);
	      printf("Under these conditions, it is not possible to continue.\n\n");
	      printf("You may try first one of these options:\n");
	      printf(" - Use debugfs to %s%s%s\n",  !ext2fs_has_feature_flex_bg(fs->super) ? "set the flex_bg feature " : "",
	                                            !ext2fs_has_feature_flex_bg(fs->super) && !fs->super->s_log_groups_per_flex ? "and " : "",
	                                            !fs->super->s_log_groups_per_flex ? "set a value of log_groups_per_flex to 4 in the superblock (default mkfs value)" : "");
	      printf(" - Use resize2fs to grow the filesystem by at least %u blocks\n", inode_blocks_per_group-movable_blocks);
	      if (fs->group_desc_count > 1)
	        printf(" - Use resize2fs to shrink the filesystem to %llu blocks, in order to get rid of the last group\n",
	                                                  (blk64_t)EXT2_BLOCKS_PER_GROUP(fs->super)*(fs->group_desc_count-1));
	      printf("After that, you can try again to change the inode count\n");
	      exit(1);
	}

errout:
	if (meta_bmap)
		ext2fs_free_block_bitmap(meta_bmap);
	if (badblock_list)
		ext2fs_badblocks_list_free(badblock_list);

	return 0;
}


/*
it shall replicate what is done in ext2fs_initialize(), with some extra checks
on having at least enough inodes for what the fs already has
*/
static int calculate_new_inodes_per_group(ext2_filsys fs, int type_of_value, long long unsigned int value, unsigned int *ipg, int force) {

  int inode_ratio, blocksize = EXT2_BLOCK_SIZE(fs->super);
  unsigned int new_inode_count, new_inodes_per_group, inode_blocks_per_group_rounded,
              required_inodes = fs->super->s_inodes_count - fs->super->s_free_inodes_count,
              max_inode_blocks_per_group = blocksize*8*EXT2_INODE_SIZE(fs->super)/blocksize;
  double inode_blocks_per_group_d;
  blk64_t free_space, current_inode_blocks_space, new_inode_blocks_space, safe_margin;

  printf("Current inode blocks per group: %u\n", fs->inode_blocks_per_group);
  printf("Current inode count: %u\n", fs->super->s_inodes_count);
  printf("Current inode ratio: %llu bytes-per-inode\n", ext2fs_blocks_count(fs->super)*blocksize/fs->super->s_inodes_count);
  printf("Current inodes per group: %u\n", fs->super->s_inodes_per_group);
  printf("Current space used by inode tables: ");
  current_inode_blocks_space = ((blk64_t)fs->inode_blocks_per_group)*fs->group_desc_count*(blocksize/1024); //in KiB
  if (current_inode_blocks_space > 1048576) {
    printf("%.2f GiB\n", (double)current_inode_blocks_space/1048576);
  } else if (current_inode_blocks_space > 1024) {
    printf("%.2f MiB\n", (double)current_inode_blocks_space/1024);
  } else {
    printf("%.2f KiB\n", (double)current_inode_blocks_space);
  }

  printf("\nInodes currently used by the filesystem: %u\n", required_inodes);
  printf("Current free space: ");
  free_space = ext2fs_free_blocks_count(fs->super)*(blocksize/1024);
  if (free_space > 1048576) {
        printf("%.2f GiB\n", (double)free_space/1048576);
  } else if (free_space > 1024) {
        printf("%.2f MiB\n", (double)free_space/1024);
  } else {
        printf("%.2f KiB\n", (double)free_space);
  }
  printf("\n");


  if (type_of_value == 0) {
    if (value < EXT2_FIRST_INODE(fs->super)+1) {
        printf("The requested inode count is too low. Minimum is %u\n\n", EXT2_FIRST_INODE(fs->super)+1);
        exit(1);
    }
    if (value > 0xffffffff) {
        printf("The requested inode count is too high. Maximum is %u\n\n", 0xffffffff);
        exit(1);
    }
    /*inode_ratio = ext2fs_blocks_count(fs->super)*blocksize/value;*/
    printf("Inode count requested by the user: %llu\n\n", value);
    new_inodes_per_group = ext2fs_div64_ceil(value, fs->group_desc_count);
  } else {
    inode_ratio = value;
    printf("Inode ratio requested by the user: %i bytes-per-inode\n\n", inode_ratio);
    new_inodes_per_group = ext2fs_div64_ceil(
                                        ext2fs_div64_ceil(ext2fs_blocks_count(fs->super)*blocksize, inode_ratio),
                                        fs->group_desc_count);

    inode_blocks_per_group_d = //((double)EXT2_INODE_SIZE(fs->super)*new_inodes_per_group)/blocksize;
    //ext2fs_blocks_count(fs->super)*blocksize/inode_ratio/fs->group_desc_count*EXT2_INODE_SIZE(fs->super)/blocksize;
    (double)ext2fs_blocks_count(fs->super)/inode_ratio/fs->group_desc_count*EXT2_INODE_SIZE(fs->super);
    printf("New inode blocks per group (based on inode ratio, before rounding): %f\n", inode_blocks_per_group_d);
  }

  /*
   * Finally, make sure the number of inodes per group is a
   * multiple of 8.  This is needed to simplify the bitmap
   * splicing code.
   */
  if (new_inodes_per_group < 8)
	  new_inodes_per_group = 8;
  else {
      if (new_inodes_per_group % 8) {
          new_inodes_per_group &= ~7;
          new_inodes_per_group += 8;
      }
  }
  inode_blocks_per_group_rounded = (((new_inodes_per_group *
				  EXT2_INODE_SIZE(fs->super)) +
			         blocksize - 1) /
			        blocksize);

  if (ext2fs_has_feature_bigalloc(fs->super)
      && inode_blocks_per_group_rounded > fs->inode_blocks_per_group
      && inode_blocks_per_group_rounded % EXT2FS_CLUSTER_RATIO(fs)) {

    /*The increaser will allocate different clusters to each inode table, they cannot be shared among different itables.
    Therefore, make sure the whole cluster is used, otherwise, the remaining blocks would be wasted.
    Ideally, we could optimize by trying to allocate contiguous blocks and compact itables so they share the same cluster...*/

          inode_blocks_per_group_rounded += EXT2FS_CLUSTER_RATIO(fs) - (inode_blocks_per_group_rounded % EXT2FS_CLUSTER_RATIO(fs));
          printf("New inode blocks per group (after rounding to fill last itable cluster): %u\n", inode_blocks_per_group_rounded);

  } else {
    printf("New inode blocks per group (after rounding to fill last itable block): %u\n", inode_blocks_per_group_rounded);
  }

  if (inode_blocks_per_group_rounded < EXT2FS_CLUSTER_RATIO(fs)) {
      if (ext2fs_has_feature_bigalloc(fs->super) && inode_blocks_per_group_rounded > fs->inode_blocks_per_group) {
          printf("  inode_blocks_per_group was %u, forced to %i\n", inode_blocks_per_group_rounded, EXT2FS_CLUSTER_RATIO(fs));
          inode_blocks_per_group_rounded = EXT2FS_CLUSTER_RATIO(fs);
      } else if (inode_blocks_per_group_rounded < 1) {
          printf("  inode_blocks_per_group was %u, forced to 1\n", inode_blocks_per_group_rounded);
          inode_blocks_per_group_rounded = 1;
      }
  } else {
    if (inode_blocks_per_group_rounded > max_inode_blocks_per_group) {
       printf("  inode_blocks_per_group was %u, forced to %u as the remaining inodes would not be addressable in the inode bitmap\n",
                    inode_blocks_per_group_rounded, max_inode_blocks_per_group);
       inode_blocks_per_group_rounded = max_inode_blocks_per_group;
    }
  }


  if (fs->group_desc_count*((blk64_t)inode_blocks_per_group_rounded*blocksize/EXT2_INODE_SIZE(fs->super)) > 0xffffffff) {
      printf("ERROR: the new inode count (%llu) is above the max allowed value (%u)\n",
              fs->group_desc_count*((blk64_t)inode_blocks_per_group_rounded*blocksize/EXT2_INODE_SIZE(fs->super)),
              0xffffffff);
      exit(1);
  }

  new_inode_count = fs->group_desc_count*(inode_blocks_per_group_rounded*blocksize/EXT2_INODE_SIZE(fs->super));
  printf("New inode count: %u\n", new_inode_count);
  if (new_inode_count < EXT2_FIRST_INODE(fs->super)+1) {
          printf("The inode count is too low!\n");
          exit(1);
  }

  new_inodes_per_group = inode_blocks_per_group_rounded*blocksize/EXT2_INODE_SIZE(fs->super);
  printf("New inode ratio: %llu bytes-per-inode\n", ext2fs_blocks_count(fs->super)*blocksize/new_inode_count);
  printf("New inodes per group: %u\n", new_inodes_per_group);

  if (new_inodes_per_group > EXT2_MAX_INODES_PER_GROUP(fs->super)) {
    printf("ERROR: the new inodes per group is above the max allowed value (%u)\n", EXT2_MAX_INODES_PER_GROUP(fs->super));
    exit(1);
  }

  printf("New space used by inode tables: ");
  new_inode_blocks_space = ((blk64_t) inode_blocks_per_group_rounded)*fs->group_desc_count*(blocksize/1024);
  if (new_inode_blocks_space > 1048576) {
    printf("%.2f GiB\n", (double)new_inode_blocks_space/1048576);
  } else if (new_inode_blocks_space > 1024) {
    printf("%.2f MiB\n", (double)new_inode_blocks_space/1024);
  } else {
    printf("%.2f KiB\n", (double)new_inode_blocks_space);
  }
  printf("\n");


  if (required_inodes > new_inode_count) {
        printf("The chosen %s will not provide enough inodes for the existing filesystem, please choose a %s\n",
              type_of_value==1?"inode-ratio":"inode count",
              type_of_value==1?"lower bytes-per-inode ratio!":"higher inode count!");
        exit(1);
  }
        
  if (new_inode_count == fs->super->s_inodes_count) {
        printf("The existing filesystem already has %u inodes. No change needed.\n", new_inode_count);
        exit(0);
  }


  if (new_inode_count > fs->super->s_inodes_count) {
              safe_margin = new_inode_blocks_space/2; /*TODO: think about how to calculate the safe_margin*/
              if (new_inode_blocks_space+safe_margin > free_space) {
                      if (new_inode_blocks_space-current_inode_blocks_space > free_space) {
                               printf("The free space in the filesystem is too low to perform the change:\n"
                                      "It will not be possible to allocate large enough inode tables for the chosen inode %s\n", type_of_value ? "ratio" : "count");
                               exit(1);
                      }
                      printf("The filesystem doesn't have enough free space to perform the change in a safe way.\n");
                      if (force) {
                               printf("As the force flag has been provided, we will proceed with the change\n");
                      } else {
                               printf("Re-run with the force flag if you want to try anyway.\n");
                               exit(1);
                      }
              }


              if (!ext2fs_has_feature_flex_bg(fs->super) || !fs->super->s_log_groups_per_flex) {
                    check_space_last_group(fs, inode_blocks_per_group_rounded);
              }
  }
   
   
  *ipg = new_inodes_per_group;
  return 0;
	        
}

int main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		c;
	int		flags = 0;
	int		flush = 0;
	int		force = 0;
	int		io_flags = 0;
	int		fd, ret;
	int		open_flags = O_RDWR;
	io_manager	io_ptr;
	ext2fs_struct_stat st_buf;
	int		len, mount_flags;
	char		*mtpt, *undo_file = NULL;
	
	int                     ratio_type = 0, count_type = 0;
	unsigned int            new_inodes_per_group;
	unsigned long long int  new_inode_value;
	const char             *ext2fs_version, *ext2fs_date;
        int                     version_int;


#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
	set_com_err_gettext(gettext);
#endif

	add_error_table(&et_ext2_error_table);


        version_int = ext2fs_get_library_version(&ext2fs_version, &ext2fs_date);
        printf("Using ext2fs library version %i, %s, %s\n", version_int, ext2fs_version, ext2fs_date);

	if (argc && *argv)
		program_name = *argv;
	else
		usage(NULL);

	while ((c = getopt(argc, argv, "d:fFhpz:r:c:")) != EOF) {
		switch (c) {
		case 'h':
			usage(program_name);
			break;
		case 'f':
			force = 1;
			break;
		case 'F':
			flush = 1;
			break;
		case 'd':
			flags |= atoi(optarg);
			break;
		case 'p':
			flags |= RESIZE_PERCENT_COMPLETE;
			break;
		case 'z':
			undo_file = optarg;
			break;
		case 'r':
		        ratio_type = 1;
		        new_inode_value = strtoull(optarg, NULL, 0);
		        break;
	        case 'c':
	                count_type = 1;
	                new_inode_value = strtoull(optarg, NULL, 0);
	                break;
		default:
			usage(program_name);
		}
	}
	if (optind == argc)
		usage(program_name);

	device_name = argv[optind++];
	if (optind < argc)
		usage(program_name);

	io_options = strchr(device_name, '?');
	if (io_options)
		*io_options++ = 0;

	/*
	 * Figure out whether or not the device is mounted, and if it is
	 * where it is mounted.
	 */
	len=80;
	while (1) {
		mtpt = malloc(len);
		if (!mtpt)
			return ENOMEM;
		mtpt[len-1] = 0;
		retval = ext2fs_check_mount_point(device_name, &mount_flags,
						  mtpt, len);
		if (retval) {
			com_err("ext2fs_check_mount_point", retval,
				_("while determining whether %s is mounted."),
				device_name);
			exit(1);
		}
		if (!(mount_flags & EXT2_MF_MOUNTED) || (mtpt[len-1] == 0))
			break;
		free(mtpt);
		len = 2 * len;
	}


	fd = ext2fs_open_file(device_name, open_flags, 0);
	if (fd < 0) {
		com_err("open", errno, _("while opening %s"),
			device_name);
		exit(1);
	}

	ret = ext2fs_fstat(fd, &st_buf);
	if (ret < 0) {
		com_err("open", errno,
			_("while getting stat information for %s"),
			device_name);
		exit(1);
	}

	if (flush) {
		retval = ext2fs_sync_device(fd, 1);
		if (retval) {
			com_err(argv[0], retval,
				_("while trying to flush %s"),
				device_name);
			exit(1);
		}
	}

	if (!S_ISREG(st_buf.st_mode )) {
		close(fd);
		fd = -1;
	}

#ifdef CONFIG_TESTIO_DEBUG
	if (getenv("TEST_IO_FLAGS") || getenv("TEST_IO_BLOCK")) {
		io_ptr = test_io_manager;
		test_io_backing_manager = unix_io_manager;
	} else
#endif
		io_ptr = unix_io_manager;

	if (!(mount_flags & EXT2_MF_MOUNTED))
		io_flags = EXT2_FLAG_RW | EXT2_FLAG_EXCLUSIVE;
	if (mount_flags & EXT2_MF_MOUNTED)
		io_flags |= EXT2_FLAG_DIRECT_IO;

	io_flags |= EXT2_FLAG_64BITS | EXT2_FLAG_THREADS;
	if (undo_file) {
		retval = resize2fs_setup_tdb(device_name, undo_file, &io_ptr);
		if (retval)
			exit(1);
	}
	retval = ext2fs_open2(device_name, io_options, io_flags,
			      0, 0, io_ptr, &fs);
	if (retval) {
		com_err(program_name, retval, _("while trying to open %s"),
			device_name);
		printf("%s", _("Couldn't find valid filesystem superblock.\n"));
		exit (1);
	}
	fs->default_bitmap_type = EXT2FS_BMAP64_RBTREE;

	/*
	 * Before acting on an unmounted filesystem, make sure it's ok,
	 * unless the user is forcing it.
	 *
	 * We do ERROR and VALID checks even if we're only printing the
	 * minimum size, because traversal of a badly damaged filesystem
	 * can cause issues as well.  We don't require it to be fscked after
	 * the last mount time in this case, though, as this is a bit less
	 * risky.
	 */
	if (!force && !(mount_flags & EXT2_MF_MOUNTED)) {
		int checkit = 0;

		if (fs->super->s_state & EXT2_ERROR_FS)
			checkit = 1;

		if ((fs->super->s_state & EXT2_VALID_FS) == 0)
			checkit = 1;

		if ((fs->super->s_lastcheck < fs->super->s_mtime))
			checkit = 1;

		if ((ext2fs_free_blocks_count(fs->super) >
		     ext2fs_blocks_count(fs->super)) ||
		    (fs->super->s_free_inodes_count > fs->super->s_inodes_count))
			checkit = 1;

		if ((fs->super->s_last_orphan != 0) ||
		    ext2fs_has_feature_journal_needs_recovery(fs->super))
			checkit = 1;

		if (checkit) {
			fprintf(stderr,
				_("Please run 'e2fsck -f %s' first.\n\n"),
				device_name);
			goto errout;
		}
	}

	/*
	 * Check for compatibility with the feature sets.  We need to
	 * be more stringent than ext2fs_open().
	 */
	if (fs->super->s_feature_compat & ~EXT2_LIB_FEATURE_COMPAT_SUPP) {
		com_err(program_name, EXT2_ET_UNSUPP_FEATURE,
			"(%s)", device_name);
		goto errout;
	}

	if (mount_flags & EXT2_MF_MOUNTED) {
		printf("Filesystem is mounted. Online change is not supported\n");
		exit(1);

	} else {
		bigalloc_check(fs, force);

		  
		  
		if (ratio_type == count_type) {
		  printf("You must specify either '-c' for inode count or '-r' for inode ratio\n");
		  exit(1);
		}


	        retval = calculate_new_inodes_per_group(fs, ratio_type==0?0:1, new_inode_value, &new_inodes_per_group, force);
	        
	        printf("new_inodes_per_group: %i\n", new_inodes_per_group);

	        if (retval) {
	          goto errout;
	        }
	        
	        if (ext2fs_has_feature_stable_inodes(fs->super)) {
	            if (new_inodes_per_group > fs->super->s_inodes_per_group) {
	                if (force) {
	                    printf("Increasing inode count in a filesystem with stable_inodes, because the force flag is set\n");
	                } else {
	                    printf("Asked to increase the inode count in a filesystem with stable_inodes feature flag.\n"
	                           "Please note it will not be possible to reduce the inode count later because of this flag.\n"
	                           "Restart with force parameter to proceed\n");
		            goto errout;
	                }
	            } else {
	                  /*TODO: check if the new inode_count is >= the max inode number in use in the old_fs:
	                  in that case, it should be possible to reduce the inode count*/
	            	  printf("Cannot reduce indode count in this filesystem because it has the stable_inodes feature flag.\n");
		          goto errout;
	            }
	        }

	        if (new_inodes_per_group > fs->super->s_inodes_per_group) {
			printf("Calling increase_inode_count\n");
		        retval = increase_inode_count(fs, flags,
				   ((flags & RESIZE_PERCENT_COMPLETE) ?
				    resize_progress_func : 0), new_inodes_per_group);
				    
		} else {
		        printf("Calling reduce_inode_count\n");
			retval = reduce_inode_count(fs, flags,
				   ((flags & RESIZE_PERCENT_COMPLETE) ?
				    resize_progress_func : 0), new_inodes_per_group);
		}
	}
	free(mtpt);
	if (retval) {
		com_err(program_name, retval, _("while trying to modify inode count on %s"),
			device_name);
		fprintf(stderr,
			_("Please run 'e2fsck -fy %s' to fix the filesystem\n"
			  "after the aborted operation.\n"),
			device_name);
		goto errout;
	}
	printf(_("The filesystem on %s now has %u inodes.\n\n"),  device_name, new_inodes_per_group*fs->group_desc_count);


	if (fd > 0)
		close(fd);
	remove_error_table(&et_ext2_error_table);
	return 0;
errout:
	(void) ext2fs_close_free(&fs);
	remove_error_table(&et_ext2_error_table);
	return 1;
}

