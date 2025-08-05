#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi

script_name=$(basename "$0")
mount_dir=/tmp/${script_name}_mounted
path_to_bin=$1
image_file=/tmp/test.${script_name}_free_inode_count.ext4.img

cd /tmp
mkdir ${mount_dir}
sudo umount ${mount_dir}
rm $image_file
fallocate -l 1GB $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` $image_file 
sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}
fallocate -l 850M f850
cd /tmp
rhash -rH ${mount_dir} > ${script_name}_SHA1SUM
sudo umount ${mount_dir}
dumpe2fs $image_file > ${script_name}_dumpe2fs_before

$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }
$path_to_bin -r 24096 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }
$path_to_bin -c 12 $image_file > ${script_name}_output_test_3 || { echo 'modification 3 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }
$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}
rhash -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount $image_file

