#!/bin/bash
LANG=C

if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi

script_name=$(basename "$0")
path_to_bin=$1
image_file=/tmp/test.${script_name}.ext4.img
mount_dir=/tmp/${script_name}_mounted


mkdir ${mount_dir}
sudo umount ${mount_dir}

rm -f $image_file
fallocate -l 12288m $image_file
mkfs.ext4 -m 0 -O bigalloc -E root_owner=`id -u`:`id -g` $image_file
sudo mount -o loop $image_file ${mount_dir}

cd ${mount_dir}
longstring=$( head -c 167001 < /dev/zero | tr '\0' 'b' )
echo $count > single_file
echo $longstring >> single_file
echo $count >> single_file
cd /tmp
sha1sum ${mount_dir}/single_file
HASH_A=`sha1sum ${mount_dir}/single_file | cut -f1 -d" "`
sudo umount  ${mount_dir}
e2fsck -vf $image_file 


echo "launch test 1"
$path_to_bin -f -c 12 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
echo "end test 1"
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }

echo "launch test 2"
$path_to_bin -f -r 8192 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
echo "end test 2"
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}
HASH_B=`sha1sum ${mount_dir}/single_file | cut -f1 -d" "`

if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "hash comparison ok"
else
 echo "hash comparison NOT ok"
 exit -2
fi

sudo umount  ${mount_dir}


#########################
# second part
#########################

rm -f $image_file
fallocate -l 12288m $image_file
mkfs.ext4 -m 0 -O bigalloc -E root_owner=`id -u`:`id -g` $image_file
sudo mount -o loop $image_file ${mount_dir}

cd ${mount_dir}
longstring=$( head -c 567001 < /dev/zero | tr '\0' 'b' )
echo $count > single_file
echo $longstring >> single_file
echo $count >> single_file
cd /tmp
sha1sum ${mount_dir}/single_file
HASH_A=`sha1sum ${mount_dir}/single_file | cut -f1 -d" "`
sudo umount  ${mount_dir}
e2fsck -vf $image_file 

echo "launch test 3"
$path_to_bin -f -c 576 $image_file > ${script_name}_output_test_3 || { echo 'modification 3 failed' ; exit 1; }
echo "end test 3"
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

echo "launch test 4"
$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
echo "end test 4"
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}
HASH_B=`sha1sum ${mount_dir}/single_file | cut -f1 -d" "`

if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "hash comparison ok"
else
 echo "hash comparison NOT ok"
 exit -2
fi

sudo umount  ${mount_dir}

