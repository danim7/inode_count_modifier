if [ "$#" -ne 1 ]; then
    echo "Need one parameter with the full path of the binary to be tested"
    echo "Example:"
    echo $0 " /usr/bin/inode_count_modifier"
    exit -1
fi

path_to_bin=$1
image_file=/tmp/test.ext4.img

sudo umount /tmp/mounted
mkdir -p /tmp/mounted
rm -f $image_file
fallocate -l 70G $image_file
mkfs.ext4 -m 0 -E root_owner=`id -u`:`id -g` $image_file
sudo mount -o loop -t ext4 $image_file /tmp/mounted/
cd /tmp/mounted
cp -ar ~/src .
cp -ar ~/snap/firefox .
cp -ar -L ~/link_to_VMs .
ls -li
cd /tmp
rhash -rH mounted > SHA1SUM
sudo umount /tmp/mounted
e2fsck -vf $image_file
cd /tmp
$path_to_bin -r 8192 $image_file > output_test_1
e2fsck -vf $image_file  || { echo 'test 1 failed' ; exit 1; }

$path_to_bin -r 4096 $image_file > output_test_2
e2fsck -vf $image_file  || { echo 'test 2 failed' ; exit 1; }

$path_to_bin -r 88192 $image_file > output_test_3
e2fsck -vf $image_file  || { echo 'test 3 failed' ; exit 1; }

$path_to_bin -r 108192 $image_file > output_test_4
e2fsck -vf $image_file  || { echo 'test 4 failed' ; exit 1; }

$path_to_bin -r 36192 $image_file > output_test_5
e2fsck -vf $image_file  || { echo 'test 5 failed' ; exit 1; }

$path_to_bin -r 228192 $image_file > output_test_6
e2fsck -vf $image_file  || { echo 'test 6 failed' ; exit 1; }

$path_to_bin -r 32768 $image_file > output_test_7
e2fsck -vf $image_file  || { echo 'test 7 failed' ; exit 1; }


sudo mount -o loop $image_file mounted/
rhash --skip-ok -c SHA1SUM
if [[ $? -eq 0 ]]
then 
 echo "rhash test ok"
else
 echo "rhash test NOT ok"
 exit -2
fi
sudo umount /tmp/mounted

