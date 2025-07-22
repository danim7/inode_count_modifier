if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi


path_to_bin=$1
image_file=/tmp/test_small_last_group.ext4.img 

cd /tmp
mkdir mounted
sudo umount /tmp/mounted
rm $image_file 
fallocate -l 2154823680 $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` $image_file
dumpe2fs $image_file
sudo mount -o loop $image_file mounted/
cd mounted/
fallocate -l 1886879744 big
cd ..
rhash -Hr mounted > SHA1SUM
sudo umount /tmp/mounted
e2fsck -vf $image_file

$path_to_bin  -r 4096 $image_file > output_test_1
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }

$path_to_bin  -r 65536 $image_file > output_test_2
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

$path_to_bin  -r 8192 $image_file > output_test_3
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

$path_to_bin  -r 16384 $image_file > output_test_4
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

$path_to_bin  -r 55555 $image_file > output_test_5
e2fsck -vf $image_file  || { echo 'test 5 failed' ; exit 1; }

$path_to_bin  -r 6776 $image_file > output_test_6
e2fsck -vf $image_file  || { echo 'test 6 failed' ; exit 1; }

sudo mount -o loop $image_file mounted/
rhash -c SHA1SUM
sudo umount $image_file

