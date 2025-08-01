#!/bin/bash

# TEST STRATEGY for manual checks:
# 1. Create test filesystem with this script
# 2. Increase inode count (aka. lower ratio)
#    a. Check for moved blocks concerning EA inodes, check specific file:
#       ino=476, blockcnt=1, 2433->21447
#       In debugfs:
#       icheck 21447 or directly stat <476>
#	In the output, check 0x00200000 flag in Flags: 0x280000
#       Also, check EXTENTS:
#       (0):21446, (1):21447
#       bd 21446 and bd 21447 will show the corresponding file
#       1600  6161 6161 6161 6161 6161 615f 3339 0000  aaaaaaaaaaa_39..
#       Check ACL move:
#       migrate_ea_block, inode 5569, old_block 2337, new_block 18171
#       stat <5569>
#       Inode: 5569   Type: regular    Mode:  0664   Flags: 0x80000
#       Generation: 2489313143    Version: 0x00000000:0000000d
#       User:  1001   Group:  1001   Project:     0   Size: 4
#       File ACL: 18171
#    b. General check, cd /tmp
#       rhash -c SHA1SUM
#       sha1sum GETFATTR-D-MOUNTED
#       getfattr -d mounted/files_* | sha1sum
#
# 3. Reduce inode count (aka. higher ratio). Reduce inode count to the minimum possible value
#    a. Check for moved EA inodes:
#      Relocating inodes beyond the new inode limit: read inode: 11628, links_count: 1
#      EA inode: 11628
#      ext2fs_new_inode inode: 8500 
#      Then, same as before, check flag and extents to retrive the corresponding file.
#
#      Also, check ACL flag. For example:
#      fix_ea_inode_refs, inode 361, block 15379
#      stat <361>
#      Inode: 361   Type: regular    Mode:  0664   Flags: 0x80000
#      Generation: 1368209171    Version: 0x00000000:0000000d
#      User:  1001   Group:  1001   Project:     0   Size: 3
#      File ACL: 15379
#    b. General check, cd /tmp
#       rhash -c SHA1SUM
#       sha1sum GETFATTR-D-MOUNTED
#       getfattr -d mounted/files_* | sha1sum

if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi

script_name=$(basename "$0")
mount_dir=/tmp/${script_name}_mounted
path_to_bin=$1
image_file=/tmp/test.${script_name}_100M.ext4.img

cd /tmp
mkdir ${mount_dir}
sudo umount ${mount_dir}
rm $image_file
fallocate -l 100M $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` -O ea_inode -i 8192 $image_file
sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}

count=1
max=8192
longstring=$( head -c 5001 < /dev/zero | tr '\0' 'a' )
while [ $count -le $max ]; do
  echo "Count: $count"
  echo $count > file_$count
  setfattr -n "user.large" -v $longstring file_$count
  for i in {0..10};
  	do
  	setfattr -n user.t$i -v ${i}_${longstring}_${count} file_$count
  	if [ $? -ne 0 ]
  	then
  	   count=$max
  	   break
  	fi
  done
  count=$((count + 1))
done


rm file_40?
rm file_50?
rm file_60?
rm file_70?
rm file_80?
rm file_90?

cd /tmp
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
getfattr -d ${mount_dir}/file_* | zstd > ${script_name}_GETFATTR-D-MOUNTED
sudo umount ${mount_dir}
e2fsck -vf $image_file

$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_1
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}
HASH_A=`getfattr -d ${mount_dir}/file_* | sha1sum | cut -f1 -d" "`
HASH_B=`unzstd -c ${script_name}_GETFATTR-D-MOUNTED | sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "xattr comparison ok"
else
 echo "xattr comparison NOT ok"
 exit -2
fi
rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}
e2fsck -f $image_file  || { echo 'test 2 failed' ; exit 1; }

$path_to_bin -c $new_count $image_file > ${script_name}_output_test_2
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}
HASH_A=`getfattr -d ${mount_dir}/file_* | sha1sum | cut -f1 -d" "`
HASH_B=`unzstd -c ${script_name}_GETFATTR-D-MOUNTED| sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "xattr comparison ok"
else
 echo "xattr comparison NOT ok"
 exit -2
fi
rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount ${mount_dir}
e2fsck -f $image_file  || { echo 'test 4 failed' ; exit 1; }

