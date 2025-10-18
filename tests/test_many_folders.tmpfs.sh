#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi

script_name=$(basename "$0")
path_to_bin=$1
image_file=/tmp/${script_name}_tmpfs/test.${script_name}.ext4.img
mount_dir=/tmp/${script_name}_mounted


mkdir ${mount_dir} /tmp/${script_name}_tmpfs
sudo umount ${mount_dir} 
sudo umount /tmp/${script_name}_tmpfs
sudo mount -t tmpfs -o size=750m none /tmp/${script_name}_tmpfs/
fallocate -l 750m $image_file
mkfs.ext4 -E root_owner=`id -u`:`id -g` $image_file
sudo mount -o loop $image_file ${mount_dir}

cd ${mount_dir}

mkdir A B
export count=1
max=1000000
while [ $count -le $max ]; do
	find A -type d -not -name "lost+found" -not -newer A -exec sh -c 'mkdir $1/$count || kill "$PPID"' sh {} \;
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi
  	find B -type d -not -name "lost+found" -not -newer B -exec sh -c 'mkdir $1/$count || kill "$PPID"' sh {} \;
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi

  count=$((count + 1))
done

rm -rf A

new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
HASH_A=`find ${mount_dir} | sort | sha1sum | cut -f1 -d" "`
cd /tmp
sudo umount ${mount_dir}

e2fsck -vf $image_file || { echo 'pre-test 1 failed' ; exit 1; }
$path_to_bin -c $new_count $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }


$path_to_bin -r 4096 $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}
HASH_B=`find ${mount_dir} | sort | sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "hash comparison ok"
else
 echo "hash comparison NOT ok"
 exit -2
fi



cd ${mount_dir}
mkdir C D E F
while [ $count -le $max ]; do
  	find C -type d -not -name "lost+found" -not -newer C -exec sh -c 'mkdir $1/$count || kill "$PPID"' sh {} \;
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi
  	find D -type d -not -name "lost+found" -not -newer D -exec sh -c 'mkdir $1/$count || kill "$PPID"' sh {} \;
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi
  	find E -type d -not -name "lost+found" -not -newer E -exec sh -c 'mkdir $1/$count || kill "$PPID"' sh {} \;
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi
  	find F -type d -not -name "lost+found" -not -newer F -exec sh -c 'mkdir $1/$count || kill "$PPID"' sh {} \;
  	if [ $? -ne 0 ]
  	then
  	   break
  	fi

  count=$((count + 1))
done

rm -rf F

new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
HASH_A=`find ${mount_dir} | sort | sha1sum | cut -f1 -d" "`
cd /tmp
sudo umount ${mount_dir}

e2fsck -vf $image_file  || { echo 'pre-test 3 failed' ; exit 1; }
$path_to_bin -c $new_count $image_file > ${script_name}_output_test_3 || { echo 'modification 3 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

$path_to_bin -r 4567 $image_file > ${script_name}_output_test_4 || { echo 'modification 4 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

sudo mount -o loop $image_file ${mount_dir}
HASH_B=`find ${mount_dir} | sort | sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "hash comparison ok"
else
 echo "hash comparison NOT ok"
 exit -2
fi
sudo umount ${mount_dir}
sudo umount /tmp/${script_name}_tmpfs

