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
image_file=/tmp/${script_name}_tmpfs/test_${script_name}.ext4.img

cd /tmp

mkdir ${mount_dir}
mkdir ${script_name}_tmpfs
sudo umount ${mount_dir}
sudo umount /tmp/${script_name}_tmpfs
rm $image_file
sudo mount -t tmpfs -o size=2G none /tmp/${script_name}_tmpfs/
fallocate -l 2G $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` -i 13348 $image_file
sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}

longstring=$( head -c 13348 < /dev/zero | tr '\0' 'J' )
count=1
max=32000
while [ $count -le $max ]; do

  echo $count > file_$count
  echo $longstring >> file_$count
  echo $count >> file_$count
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi

  count=$((count + 1))
done

cd ..
HASH_A=`ls -ai ${mount_dir}/* | sha1sum | cut -f1 -d" "`
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
total_inode_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f2`
sudo umount ${mount_dir}
e2fsck -f $image_file


$path_to_bin -i +144288 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }
if tail -2 ${script_name}_output_test_1 | head -1 | grep "now has 305408 inodes"; then
    echo "number of inodes is correct for test 1"
else
    echo "wrong number of inodes after test 1"
    exit 1
fi

$path_to_bin -i -270111 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }
if tail -2 ${script_name}_output_test_2 | head -1 | grep "now has 35328 inodes"; then
    echo "number of inodes is correct for test 2"
else
    echo "wrong number of inodes after test 2"
    exit 1
fi

sudo mount -o loop $image_file ${mount_dir}

HASH_B=`ls -ai ${mount_dir}/* | sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then
 echo "hash comparison for ls -ai ok"
else
 echo "hash comparison for ls -ai NOT ok"
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

sudo umount $image_file
sudo umount /tmp/${script_name}_tmpfs

