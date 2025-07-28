#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Need one parameter with the full path of the binary to be tested"
	echo "Example:"
	echo $0 " /usr/bin/inode_count_modifier"
	exit -1
fi


path_to_bin=$1
image_file=/tmp/tmpfs/test.256M.ext4.img

cd /tmp
mkdir mounted
mkdir tmpfs
sudo umount /tmp/mounted
sudo umount /tmp/tmpfs
rm $image_file
sudo mount -t tmpfs -o size=256m none /tmp/tmpfs/
fallocate -l 255m $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` -i 8192 $image_file
sudo mount -o loop $image_file mounted/
cd mounted/
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
df -i  mounted
df -Th mounted
rhash -Hr mounted > SHA1SUM
getfattr -dR mounted/ | zstd > GETFATTR-D-MOUNTED.zst
sudo umount /tmp/mounted
e2fsck -vf $image_file

$path_to_bin -r 4096 $image_file > output_test_1
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }
sudo mount -o loop $image_file mounted/

HASH_A=`getfattr -dR mounted/ | sha1sum | cut -f1 -d" "`
HASH_B=`unzstd -c GETFATTR-D-MOUNTED.zst | sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "xattr comparison ok"
else
 echo "xattr comparison NOT ok"
 exit -2
fi
rhash --skip-ok -c SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi

cd mounted
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
df -i  mounted
df -Th mounted

find mounted/ -name "file_18" -exec rm {} \;
rm -f SHA1SUM GETFATTR-D-MOUNTED.zst
rhash -Hr mounted > SHA1SUM
getfattr -dR mounted/ | zstd > GETFATTR-D-MOUNTED.zst
new_count=`df -i mounted/ | tail -n +2  | tr -s " "  | cut -d" " -f3`
echo $new_count

sudo umount /tmp/mounted
e2fsck -vf $image_file 

$path_to_bin -c $new_count  $image_file > output_test_2
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }
sudo mount -o loop $image_file mounted/

HASH_A=`getfattr -dR mounted/ | sha1sum | cut -f1 -d" "`
HASH_B=`unzstd -c GETFATTR-D-MOUNTED.zst | sha1sum | cut -f1 -d" "`
if [[ "$HASH_A" == "$HASH_B" ]]
then 
 echo "xattr comparison ok"
else
 echo "xattr comparison NOT ok"
 exit -2
fi
rhash --skip-ok -c SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi

