# Introduction

This tool provides a way to change the bytes/inode ratio of an existing ext4 filesystem, thus increasing or decreasing the inode count. Previously, this parameter was chosen at filesystem creation time and could not be modified afterwards.  

It is largely based on the source code of "resize2fs" from e2fsprogs v1.47.2.  


# Disclaimer

As of August 2025, this is **EXPERIMENTAL** software. Make sure you have backups of your data before using it. Currently, an ext4 partition created with the default options *seems to work properly*. Support for non-default features/options needs further testing/development.  

This has been developed as a personal project to address a specific need. This program is published in the hope that it will be useful to someone else, but without any warranty. Please note this has not been developed by the official ext4 developers.  

It is advised to run `e2fsck -vf /dev/partition` after the process, to check eveything is ok.  


# Use cases

There are two options:  

- Reducing the bytes/inode ratio will increase the inode count of a filesystem:  
  This could be useful when getting a "no space" error because of running out of free inodes. If a wrong ratio/count was chosen during filesystem creation, the user will hit this error, even if there is still plenty of free space on the partition.  
  Please note that some free space (in blocks) is needed to increase the inode count, as it is necessary to grow the inode tables to allocate them.  

- Increasing the bytes/inode ratio will reduce the inode count of a filesystem:  
  This could be useful to get some extra free space for data, by reducing the space used for inode tables.  
  The default inode ratio of 16KiB could waste space by creating much more inodes than necessary for the user (specially on secondary drives to store files much larger than 16KiB instead of the operating system root).  
  
  
By default (/etc/mke2fs.conf), mkfs.ext4 creates a filesystem using a inode ratio value of 16384 bytes-per-inode, and a inode size of 256 bytes.  
This will create filesystem with the following characteristics:  

  
| Filesystem size | Number of inodes | Space used by inode tables |
| --------------: | ----------------:| -------------------------: |
| 10 GiB | 655 360 | 160 MiB |
| 100 GiB | 6 553 600 | 1.56 GiB |
| 1 TiB | 67 108 864 | 16 GiB |


# Compilation

Prerequisites: other than standard development packages (gcc, make, etc..), the `libext2fs-dev` package is required to compile it.   

<pre>
./configure
make
</pre>

# Usage

There are two mode of operation: choose a new inode count (-c) or ratio (-r).  
` >> inode_count_modifier [-c|-r] [new_value] [filesystem]`  

Where:  
- c|r: select mode of operation: -c for inode count, -r for inode ratio.  
- new value: total inodes for (-c), new bytes-per-inode ratio for (-r).  
- filesystem: device of the ext4 partition to be modified.  



* Specify a new bytes/inode ratio:  
`Example: inode_count_modifier -r 131072 /dev/sda1 `  

* Specify a new inodes count:  
`Example: inode_count_modifier -c 1000000 /dev/sda1 `  

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

- support bigalloc, blocksizes different than 4096, other non-standard mkfs features...
- test huge fs
- calculate minimum necessary size to perform a safe increase of inode tables
- performance: move itable blocks instead of inodes one by one - first check buffering done by io_ functions
- is there any impact in the "resize_inode"? it shall not, because ext2fs_reserve_super_and_bgd() also marks the reserved GDT blocks (except for meta_bg)
- improve the build system: "configure, make, etc..." + static build
- build a fancy progress display with rfs->progress
- clean, factorize and improve code to make it more readable

