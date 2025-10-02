#!/bin/bash

LANG=C

if [ "$#" -ne 2 ]; then
    echo "Need two parameters: [full path of the binary to be tested] [full path to folder with test scripts]"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier /src/tests/"
    exit -1
fi

cd $2

time ./test_xattrs.no_ea_inode.tmpfs.sh $1 || { echo 'test_xattrs.no_ea_inode.tmpfs failed' ; exit 1; }
time ./test_low_free_space.tmpfs.sh $1 || { echo 'test_low_free_space.tmpfs failed' ; exit 1; }
time ./test_no_flex_bg.32bits.inode_128bits.badblocks.tiny_last_group.tmpfs.sh $1 || { echo 'test_no_flex_bg.32bits.inode_128bits.badblocks.tiny_last_group.tmpfs failed' ; exit 1; }
time ./test_blocksize_not_4096.tmpfs.sh $1 || { echo 'test_blocksize_not_4096.tmpfs failed' ; exit 1; }
time ./test_stable_inodes.tmpfs.sh $1 || { echo 'test_stable_inodes.tmpfs failed' ; exit 1; }
time ./test_bigalloc.tmpfs.sh $1 || { echo 'test_bigalloc.tmpfs failed' ; exit 1; }
time ./test_bigalloc_single_file.sh $1 || { echo 'test_bigalloc_single_file failed' ; exit 1; }
time ./test_tiny_last_group.sh $1 || { echo 'test_tiny_last_group failed' ; exit 1; }
time ./test_wrong_free_inode_count.sh $1 || { echo 'test_wrong_free_inode_count failed' ; exit 1; }
time ./test_xattrs_ea_inode.sh $1 || { echo 'test_xattrs_ea_inode failed' ; exit 1; }
time ./test_meta_bg.sh $1 || { echo 'test_meta_bg failed' ; exit 1; }
time ./test_fs_70gb.sh $1 || { echo 'test_fs_70gb failed' ; exit 1; }


