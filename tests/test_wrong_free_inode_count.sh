if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi

path_to_bin=$1
image_file=/tmp/test.free_inode_count.ext4.img 

cd /tmp
mkdir mounted
sudo umount /tmp/mounted
rm $image_file
fallocate -l 1GB $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` $image_file 
sudo mount -o loop $image_file /tmp/mounted
cd mounted/
fallocate -l 850M f850
cd /tmp
rhash -rH mounted > SHA1SUM
sudo umount /tmp/mounted 
dumpe2fs /tmp/test.resize.ext4.img  > dumpe_before

$path_to_bin  -r 4096 $image_file > output_test_1
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }
$path_to_bin  -r 24096 $image_file > output_test_2
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 2; }

sudo mount -o loop $image_file mounted/
rhash -c SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount $image_file

