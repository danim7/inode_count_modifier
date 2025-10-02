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
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` -i 196608 -O stable_inodes $image_file
sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}

mkdir 0 1 2 3 4 5 6 7 8 9
longstring=$( head -c 196608 < /dev/zero | tr '\0' 'r' )
count=1
dir=1
max=32000
while [ $count -le $max ]; do
#  echo "Count: $count"

  echo $count > $dir/file_$count
  echo $longstring >> $dir/file_$count
  echo $count >> $dir/file_$count
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi

  count=$((count + 1))
  dir=$(((dir + 1) % 10))
done

cd ..
HASH_A=`ls -ai ${mount_dir}/* | sha1sum | cut -f1 -d" "`
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}
e2fsck -f $image_file


$path_to_bin -r 123456 $image_file > ${script_name}_output_test_1
if [ $? -ne 1 ]
then
	echo "shall have gotten a non zero exit code"
  	exit 1
fi
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }

$path_to_bin -f -r 123456 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

$path_to_bin -c $new_count $image_file > ${script_name}_output_test_3
if [ $? -ne 1 ]
then
	echo "shall have gotten a non zero exit code"
  	exit 1
fi
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }


$path_to_bin -f -r 87654 $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

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

