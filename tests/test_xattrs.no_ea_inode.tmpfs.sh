#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Need one parameter with the full path of the binary to be tested"
	echo "Example:"
	echo $0 " /usr/bin/inode_count_modifier"
	exit -1
fi

script_name=$(basename "$0")
path_to_bin=$1
image_file=/tmp/${script_name}_tmpfs/test.256M.ext4.img
mount_dir=/tmp/${script_name}_mounted

cd /tmp
mkdir ${mount_dir}
mkdir ${script_name}_tmpfs
sudo umount ${mount_dir}
sudo umount /tmp/${script_name}_tmpfs
rm $image_file
sudo mount -t tmpfs -o size=256m none /tmp/${script_name}_tmpfs/
fallocate -l 255m $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` -i 8192 $image_file
sudo mount -o loop $image_file ${mount_dir}
cd ${mount_dir}
mkdir folder
cd folder/

count=1
max=24
valueattr="abcdef"
while [ $count -le $max ]; do
	echo "Count: $count";
	echo $count > file_$count;
	if [ $? -ne 0 ];   then
		break;
	fi;
	for i in {0..200};do
		setfattr -n user.t$i -v ${i}_${valueattr}_${count} file_$count;
		if [ $? -ne 0 ];then
			break;
		fi;
	done;
	count=$((count + 1));
done

cd ..
count=1
max=8192
while [ $count -le $max ]; do
	cp -r --preserve=mode,ownership,xattr folder folder_${count} 2>/dev/null
	if [ $? -ne 0 ];   then
		break;
	fi;
	mv folder_${count} folder
	if [ $? -ne 0 ];   then
		break;
	fi;
	count=$((count + 1));
done

cd /tmp
df -i  ${mount_dir}
df -Th ${mount_dir}
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
getfattr -dR ${mount_dir} | zstd > ${script_name}_GETFATTR-D-MOUNTED.zst
sudo umount ${mount_dir}
e2fsck -vf $image_file

$path_to_bin -r 4096 $image_file > ${script_name}_output_test_1 || { echo 'modification 1 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}

HASH_A=`getfattr -dR ${mount_dir} | sha1sum | cut -f1 -d" "`
HASH_B=`unzstd -c ${script_name}_GETFATTR-D-MOUNTED.zst | sha1sum | cut -f1 -d" "`
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

cd ${mount_dir}
while [ $count -le $max ]; do
	cp -r --preserve=mode,ownership,xattr folder folder_${count} 2>/dev/null
	if [ $? -ne 0 ];   then
		break;
	fi;
	mv folder_${count} folder
	if [ $? -ne 0 ];   then
		break;
	fi;
	count=$((count + 1));
done

cd /tmp
df -i  ${mount_dir}
df -Th ${mount_dir}

find ${mount_dir} -name "file_18" -exec rm {} \;
rm -f ${script_name}_SHA1SUM ${script_name}_GETFATTR-D-MOUNTED.zst
rhash -Hr ${mount_dir} > ${script_name}_SHA1SUM
getfattr -dR ${mount_dir} | zstd > ${script_name}_GETFATTR-D-MOUNTED.zst
new_count=`df -i ${mount_dir} | tail -n +2  | tr -s " "  | cut -d" " -f3`
echo $new_count

sudo umount ${mount_dir}
e2fsck -vf $image_file 

$path_to_bin -c $new_count  $image_file > ${script_name}_output_test_2 || { echo 'modification 2 failed' ; exit 1; }
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }
sudo mount -o loop $image_file ${mount_dir}

HASH_A=`getfattr -dR ${mount_dir} | sha1sum | cut -f1 -d" "`
HASH_B=`unzstd -c ${script_name}_GETFATTR-D-MOUNTED.zst | sha1sum | cut -f1 -d" "`
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
sudo umount /tmp/${script_name}_tmpfs

