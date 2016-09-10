# FAT 12, as used on Floppies
A floppy disk consists of 1.44MBytes of data, arranged as 512 byte sectors. 
A FAT-12 filesystem groups these sectors into clusters and then maintains a File Allocation Table, with one entry per cluster. This allows a larger block size than 512 bytes. However, on a floppy disk, the block size is normally 512 bytes (i.e, one sector per cluster).

The implementation are slightly trimmed down versions of the C header files from FreeBSD’s implementation of the FAT-12 filesystem.

There're three programs:
* dos_l s: This will do a recursive listing of all files and directories in a FAT12 disk image file.
For example “. / dos_l s f l oppy. i mg” would list all the files and directories in the disk image file called floppy.img.

* dos_cp: This can be used to copy files into and out of the disk image file.
For example, “. / dos_cp f l oppy. i mg a: f oo. t xt bar . t xt ” will copy the file foo.txt from the disk image called floppy.img and save it as bar.txt in the regular filesystem.

* dos_scandisk that takes a disk image file, checks it for the two types of inconsistency listed above, and fixes the inconsistencies.
  1. Print out a list of clusters that are not referenced from any file. Print these out separated by spaces in a single line, preceeded by “Unreferenced: ”. For example: Unreferenced: 5 6 7 8

  2. From the unreferenced blocks your code found in part 1., print out the files that make up these blocks. Print out the starting block of the file and the number of blocks in the file in the following format: Lost File: 58 3
  This indicates that a missing file starts at block 58 and is three blocks long.

  3. Create a directory entry in the root directory for any unreferenced files. These should be named “found1.dat”, “found2.dat”, etc.

  4. Print out a list of files whose length in the directory entry is inconsistent with their length in the FAT. Print out the filename, its length in the dirent and its length in the FAT. For example:f oo. t xt 23567 8192 bar.txt 4721 4096

  5. Free any clusters that are beyond the end of a file (as indicated by the directory entry for that file). Make sure you terminate the file correctly in the FAT.
