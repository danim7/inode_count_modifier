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
image_file=/tmp/${script_name}_tmpfs/test.bigalloc.ext4.img
mount_dir=/tmp/${script_name}_mounted


mkdir ${mount_dir} /tmp/${script_name}_tmpfs
sudo umount ${mount_dir} 
sudo umount /tmp/${script_name}_tmpfs 
sudo mount -t tmpfs -o size=3136m none /tmp/${script_name}_tmpfs/


fallocate -l 3136m $image_file
mkfs.ext4 -m 0 -O bigalloc -E root_owner=`id -u`:`id -g` $image_file
sudo mount -o loop $image_file ${mount_dir}

cd ${mount_dir}

mkdir 0 1 2 3 4 5 6 7 8 9
longstring=$( head -c 25001 < /dev/zero | tr '\0' 'a' )
count=1
dir=1
max=100000
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

cd 3/
rm file_5*

cd /tmp
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
sudo umount ${mount_dir}
e2fsck -f $image_file


$path_to_bin -f -r 8192 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file || { echo 'test 1 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}

rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi

cd ${mount_dir}
rm -rf 4/ 7/
mkdir new_dir_0 new_dir_1 new_dir_2 new_dir_3 new_dir_4 new_dir_5 new_dir_6 new_dir_7 new_dir_8 new_dir_9
longstring=$( head -c 12001 < /dev/zero | tr '\0' 'b' )
count=1
dir=1
max=100000
while [ $count -le $max ]; do

  echo $count > new_dir_$dir/file_$count
  echo $longstring >> new_dir_$dir/file_$count
  echo $count >> new_dir_$dir/file_$count
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi

  count=$((count + 1))
  dir=$(((dir + 1) % 10))
done

cd /tmp
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}
e2fsck -f $image_file



$path_to_bin -f -c $new_count $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}

rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount ${mount_dir}

e2fsck -f $image_file
count_param=$(( new_count + new_count / 4 ))
echo $count_param
$path_to_bin -f -c $count_param $image_file > ${script_name}_output_test_3 || { echo 'modification 3 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

$path_to_bin -f -c $new_count $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

count_param=$(( new_count + new_count / 3 ))
$path_to_bin -f -c $count_param $image_file > ${script_name}_output_test_5 || { echo 'modification 5 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 5 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}

rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount ${mount_dir}

################################################################################################################################
# second part
# now we begin with the reducer
################################################################################################################################
rm -f $image_file
fallocate -l 3136m $image_file
mkfs.ext4 -m 0 -O bigalloc -E root_owner=`id -u`:`id -g` $image_file
sudo mount -o loop $image_file ${mount_dir}

cd ${mount_dir}


mkdir 0 1 2 3 4 5 6 7 8 9
longstring=$( head -c 72384 < /dev/zero | tr '\0' 'x' )
count=1
dir=1
max=100000
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

cd 2/
rm file_7*
rm file_4*

cd /tmp
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}

e2fsck -f $image_file
$path_to_bin -f -c $new_count $image_file > ${script_name}_output_test_6 || { echo 'modification 6 failed' ; exit 1; }
e2fsck -vf $image_file || { echo 'test 6 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}

rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi

cd ${mount_dir}
rm -rf 1/ 6/
mkdir new_dir_0 new_dir_1 new_dir_2 new_dir_3 new_dir_4 new_dir_5 new_dir_6 new_dir_7 new_dir_8 new_dir_9
longstring=$( head -c 7001 < /dev/zero | tr '\0' 'b' )
count=1
dir=1
max=100000
while [ $count -le $max ]; do

  echo $count > new_dir_$dir/file_$count
  echo $longstring >> new_dir_$dir/file_$count
  echo $count >> new_dir_$dir/file_$count
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi

  count=$((count + 1))
  dir=$(((dir + 1) % 10))
done

cd /tmp
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
sudo umount ${mount_dir}
e2fsck -f $image_file


$path_to_bin -f -c $new_count $image_file > ${script_name}_output_test_7 || { echo 'modification 7 failed' ; exit 1; }
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
sudo umount ${mount_dir}

e2fsck -f $image_file
count_param=$(( new_count + new_count / 4 ))
echo $count_param
$path_to_bin -f -c $count_param $image_file > ${script_name}_output_test_8 || { echo 'modification 8 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 8 failed' ; exit 1; }

echo "launch test 9"
$path_to_bin -f -c $new_count $image_file > ${script_name}_output_test_9 || { echo 'modification 9 failed' ; exit 1; }
echo "end test 9"
e2fsck -vf $image_file  || { echo 'test 9 failed' ; exit 1; }

count_param=$(( new_count + new_count / 3 ))
$path_to_bin -f -c $count_param $image_file > ${script_name}_output_test_10 || { echo 'modification 10 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 10 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}

rhash --skip-ok -c ${script_name}_SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount ${mount_dir}

