# Introduction

This tool provides a way to change the bytes/inode ratio of an existing ext4 filesystem, thus increasing or decreasing the inode count. Previously, this parameter was chosen at filesystem creation time and could not be modified afterwards. Now it is possible to trade inodes for free space, and vice versa.  

Originally, it was largely based on the source code of "resize2fs" from e2fsprogs v1.47.2.  


# Disclaimer

As a general principle when modifying filesystems: **Make sure you have backups of your data before using this tool.**

This has been developed as a personal project to address a specific need. This program is published in the hope that it will be useful to someone else, but without any warranty. Please note this has not been developed by the official ext4 developers. The section "Tests" provides further details about how this tool has been tested, and what filesystem features shall be supported.  

It is advised to run `e2fsck -vf /dev/partition` after the process, to check eveything is ok.  


# Tests

The tests performed to validate this tool can be found in the "tests" directory.  
They cover different features and options that can be set in a ext4 filesystem.  
The general testing process consists of these steps:  

1. Create a filesystem and feed data into it
2. Hash all data
3. fsck the filesystem
4. Change inode count
5. fsck the filesytem, no errors should be found
6. Verify hash of all data, it shall match hash from step 2

The steps 3-5 can be repeated multiple times with a different parameter for inode count. At the end, no errors shall be found by fsck, and all data shall hash to the right values.  

The data used to feed a filesystem is created by each test script, except for "test_fs_70gb.sh" which uses a sample of real-world files (totalling 117589 inodes and 64GiB).  

If you use this tool, please don't hesitate to provide any feedback (whether it's positive or negative).  

# Use cases

There are two options:  

- Reducing the bytes/inode ratio will increase the inode count of a filesystem:  
  This could be useful when getting a "no space" error because of running out of free inodes. If a wrong ratio/count was chosen during filesystem creation, the user will hit this error, even if there is still plenty of free space on the partition.  
  Please note that some free space (in blocks) is needed to increase the inode count, as it is necessary to grow the inode tables to allocate them.  

- Increasing the bytes/inode ratio will reduce the inode count of a filesystem:  
  This could be useful to get some extra free space for data, by reducing the space used for inode tables.  
  The default inode ratio of 16KiB could waste space by creating much more inodes than necessary for the user (specially on secondary drives used to store big files instead of the operating system root).  
  
  
By default mkfs.ext4 creates a filesystem using a inode ratio value of 16384 bytes-per-inode, and a inode size of 256 bytes. The actual parameters can be found in /etc/mke2fs.conf.  
This will create filesystems with the following characteristics:  

  
| Filesystem size | Number of inodes | Space used by inode tables |
| --------------: | ----------------:| -------------------------: |
| 10 GiB | 655 360 | 160 MiB |
| 100 GiB | 6 553 600 | 1.56 GiB |
| 1 TiB | 67 108 864 | 16 GiB |


# Compilation

In Ubuntu, the standard development packages (gcc, make, autotools, etc..) are required.  
Also, the `libext2fs-dev` package is required to compile the program.   

<pre>
sudo apt install libext2fs-dev
</pre>

Then, the program can be compiled as follows:  
<pre>
autoreconf -fi
./configure
make
</pre>

For a static build, use the following flag in ./configure command:  
<pre>
./configure LDFLAGS=-static
</pre>


# Usage

The partition must be **unmounted**, the change can only be performed **offline**.

There are two mode of operation: choose a new inode count (-c) or ratio (-r).  
` >> inode_count_modifier [-c|-r] [new_value] [filesystem]`  

Where:  
- c|r: select mode of operation: -c for inode count, -r for inode ratio.  
- new value: total inodes for (-c), new bytes-per-inode ratio for (-r).  
- filesystem: device of the ext4 partition to be modified.  



* Change the inode ratio to 131072 bytes-per-inode in /dev/sda1 partition:  
`Example: inode_count_modifier -r 131072 /dev/sda1 `  

* Change the total inode count to 1000000 inodes in /dev/sda1 partition:  
`Example: inode_count_modifier -c 1000000 /dev/sda1 `  

Please note that the actual count could be rounded up in order to completely fill the inode tables, otherwise, that space would be wasted.  

## Some other useful commands:
### Get the number of free and used inodes:  

<pre>
 >> df -i
 Filesystem       Inodes  IUsed    IFree IUse% Mounted on
 /dev/nvme1n1p5 18911088 660776 18250312    4% /
</pre>

### Calculate some inode metrics for a given ext4 filesystem:  

<pre>
 >> tune2fs -l /dev/sda1 | grep -E "Block|Inode"
Inode count:              655360
Block count:              2621440
Block size:               4096
Blocks per group:         32768
Inodes per group:         8192
Inode blocks per group:   512
Inode size:	          256
</pre>

   - The current inode ratio is: [Block count] * [Block size] / [Inode count] = 16384 bytes-per-inode  
   - The inode tables take [Inode count] * [Inode size] = 167772160 bytes = 160 MiB  


# TODO

- test huge fs
- calculate minimum necessary size to perform a safe increase of inode tables
- build a fancy progress display with rfs->progress

