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
image_file=/tmp/test_${script_name}_small_last_group.ext4.img

cd /tmp
mkdir ${mount_dir}
sudo umount ${mount_dir}
rm $image_file 
fallocate -l 2154823680 $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` $image_file
dumpe2fs $image_file
sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}
fallocate -l 1886879744 big
cd ..
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
sudo umount ${mount_dir}
e2fsck -vf $image_file

$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }

$path_to_bin  -r 65536 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

$path_to_bin  -r 8192 $image_file > ${script_name}_output_test_3 || { echo 'modification 3 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

$path_to_bin  -r 16384 $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

$path_to_bin  -r 55555 $image_file > ${script_name}_output_test_5 || { echo 'modification 5 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 5 failed' ; exit 1; }

$path_to_bin  -r 6776 $image_file > ${script_name}_output_test_6 || { echo 'modification 6 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 6 failed' ; exit 1; }

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

