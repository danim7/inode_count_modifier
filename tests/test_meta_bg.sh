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
image_file=/tmp/test_${script_name}.ext4.img

cd /tmp

mkdir ${mount_dir}
sudo umount ${mount_dir}
rm $image_file
fallocate -l 7G $image_file
#meta_bg is not compatible with resize_inode, we need to disable it
mkfs.ext4 -E root_owner=`id -u`:`id -g` -O meta_bg,^resize_inode $image_file
dumpe2fs $image_file
sudo mount -o loop $image_file ${mount_dir}


cd ${mount_dir}
mkdir 0 1 2 3 4 5 6 7 8 9
longstring=$( head -c 1234567 < /dev/zero | tr '\0' 'y' )
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
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}
e2fsck -f $image_file

$path_to_bin -f -r 8096 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }

$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

$path_to_bin  -c $new_count $image_file > ${script_name}_output_test_3 || { echo 'modification 3 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

$path_to_bin -f -r 8192 $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

$path_to_bin  -r 16384 $image_file > ${script_name}_output_test_5 || { echo 'modification 5 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 5 failed' ; exit 1; }

$path_to_bin  -r 55555 $image_file > ${script_name}_output_test_6 || { echo 'modification 6 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 6 failed' ; exit 1; }

$path_to_bin -f -r 6776 $image_file > ${script_name}_output_test_7 || { echo 'modification 7 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 7 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}
rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount $image_file

e2fsck -f $image_file
resize2fs $image_file 21G
e2fsck -f $image_file

sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}

longstring=$( head -c 534950 < /dev/zero | tr '\0' 'y' )
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
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}
e2fsck -f $image_file
dumpe2fs $image_file


$path_to_bin -f -r 8096 $image_file > ${script_name}_output_test_8 || { echo 'modification 8 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 8 failed' ; exit 1; }

$path_to_bin -f -r 4096 $image_file > ${script_name}_output_test_9 || { echo 'modification 9 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 9 failed' ; exit 1; }

$path_to_bin  -c $new_count $image_file > ${script_name}_output_test_10 || { echo 'modification 10 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 10 failed' ; exit 1; }

$path_to_bin -f -r 8192 $image_file > ${script_name}_output_test_11 || { echo 'modification 11 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 11 failed' ; exit 1; }

$path_to_bin  -r 16384 $image_file > ${script_name}_output_test_12 || { echo 'modification 12 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 12 failed' ; exit 1; }

$path_to_bin  -r 55555 $image_file > ${script_name}_output_test_13 || { echo 'modification 13 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 13 failed' ; exit 1; }

$path_to_bin -f -r 6776 $image_file > ${script_name}_output_test_14 || { echo 'modification 14 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 14 failed' ; exit 1; }


sudo mount -o loop $image_file ${mount_dir}
rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount $image_file

