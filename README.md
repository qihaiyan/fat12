# FAT 12, as used on Floppies
A floppy disk consists of 1.44MBytes of data, arranged as 512 byte sectors. 
A FAT-12 filesystem groups these sectors into clusters and then maintains a File Allocation Table, with one entry per cluster. This allows a larger block size than 512 bytes. However, on a floppy disk, the block size is normally 512 bytes (i.e, one sector per cluster).

The implementation are slightly trimmed down versions of the C header files from FreeBSD’s implementation of the FAT-12 filesystem.

There're two programs:
* dos_l s: This will do a recursive listing of all files and directories in a FAT12 disk image file.
For example “. / dos_l s f l oppy. i mg” would list all the files and directories in the disk image file called floppy.img.

* dos_cp: This can be used to copy files into and out of the disk image file.
For example, “. / dos_cp f l oppy. i mg a: f oo. t xt bar . t xt ” will copy the file foo.txt from the disk image called floppy.img and save it as bar.txt in the regular filesystem.
