/* 3005 Coursework 2, mjh, Nov 2005 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


/* memory map the FAT-12  disk image file */
uint8_t *mmap_file(char *filename, int *fd)
{
    struct stat statbuf;
    int size;
    uint8_t *image_buf;
    char pathname[MAXPATHLEN+1];

    /* If filename isn't an absolute pathname, then we'd better prepend
       the current working directory to it */
    if (filename[0] == '/') {
	strncpy(pathname, filename, MAXPATHLEN);
    } else {
	getcwd(pathname, MAXPATHLEN);
	if (strlen(pathname) + strlen(filename) + 1 > MAXPATHLEN) {
	    fprintf(stderr, "Filename too long\n");
	    exit(1);
	}
	strcat(pathname, "/");
	strcat(pathname, filename);
    }

    /* Step 2: find out how big the disk image file is */
    /* we can use "stat" to do this, by checking the file status */
    if (stat(pathname, &statbuf) < 0) {
	fprintf(stderr, "Cannot read disk image file %s:\n%s\n", 
		pathname, strerror(errno));
	exit(1);
    }

    size = statbuf.st_size;

    /* Step 3: open the file for read/write */
    *fd = open(pathname, O_RDWR);
    if (*fd < 0) {
	fprintf(stderr, "Cannot read disk image file %s:\n%s\n", 
		pathname, strerror(errno));
	exit(1);
    }

    /* Step 3: we memory map the file */

    image_buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (image_buf == MAP_FAILED) {
	fprintf(stderr, "Failed to memory map: \n%s\n", strerror(errno));
	exit(1);
    }
    return image_buf;
}

/* read the bootsector from the disk, and check that it is sane */
/* define DEBUG to see what the disk parameters actually are */

struct bpb33* check_bootsector(uint8_t *image_buf)
{
    struct bootsector33* bootsect;
    struct byte_bpb33* bpb;  /* BIOS parameter block */
    struct bpb33* bpb2;

    bootsect = (struct bootsector33*)image_buf;
    if (bootsect->bsJump[0] == 0xe9 ||
	(bootsect->bsJump[0] == 0xeb && bootsect->bsJump[2] == 0x90)) {
#ifdef DEBUG
	printf("Good jump inst\n");
#endif
    } else {
	fprintf(stderr, "illegal boot sector jump inst: %x%x%x\n", 
		bootsect->bsJump[0], bootsect->bsJump[1], 
		bootsect->bsJump[2]); 
    } 

#ifdef DEBUG
    printf("OemName: %s\n", bootsect->bsOemName);
#endif

    if (bootsect->bsBootSectSig0 == BOOTSIG0
	&& bootsect->bsBootSectSig0 == BOOTSIG0) {
	//Good boot sector sig;
#ifdef DEBUG
	printf("Good boot sector signature\n");
#endif
    } else {
	fprintf(stderr, "Boot boot sector signature %x%x\n", 
		bootsect->bsBootSectSig0, 
		bootsect->bsBootSectSig1);
    }

    bpb = (struct byte_bpb33*)&(bootsect->bsBPB[0]);

    /* bpb is a byte-based struct, because this data is unaligned.
       This makes it hard to access the multi-byte fields, so we copy
       it to a slightly larger struct that is word-aligned */
    bpb2 = malloc(sizeof(struct bpb33));

    bpb2->bpbBytesPerSec = getushort(bpb->bpbBytesPerSec);
    bpb2->bpbSecPerClust = bpb->bpbSecPerClust;
    bpb2->bpbResSectors = getushort(bpb->bpbResSectors);
    bpb2->bpbFATs = bpb->bpbFATs;
    bpb2->bpbRootDirEnts = getushort(bpb->bpbRootDirEnts);
    bpb2->bpbSectors = getushort(bpb->bpbSectors);
    bpb2->bpbFATsecs = getushort(bpb->bpbFATsecs);
    bpb2->bpbHiddenSecs = getushort(bpb->bpbHiddenSecs);
    

#ifdef DEBUG
    printf("Bytes per sector: %d\n", bpb2->bpbBytesPerSec);
    printf("Sectors per cluster: %d\n", bpb2->bpbSecPerClust);
    printf("Reserved sectors: %d\n", bpb2->bpbResSectors);
    printf("Number of FATs: %d\n", bpb->bpbFATs);
    printf("Number of root dir entries: %d\n", bpb2->bpbRootDirEnts);
    printf("Total number of sectors: %d\n", bpb2->bpbSectors);
    printf("Number of sectors per FAT: %d\n", bpb2->bpbFATsecs);
    printf("Number of hidden sectors: %d\n", bpb2->bpbHiddenSecs);
#endif

    return bpb2;
}

/* get_fat_entry returns the value from the FAT entry for
   clusternum. */
uint16_t get_fat_entry(uint16_t clusternum, 
		       uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t offset;
    uint16_t value;
    uint8_t b1, b2;
    
    /* this involves some really ugly bit shifting.  This probably
       only works on a little-endian machine. */
    offset = bpb->bpbResSectors * bpb->bpbBytesPerSec * bpb->bpbSecPerClust 
	+ (3 * (clusternum/2));
    switch(clusternum % 2) {
    case 0:
	b1 = *(image_buf + offset);
	b2 = *(image_buf + offset + 1);
	/* mjh: little-endian CPUs are ugly! */
	value = ((0x0f & b2) << 8) | b1;
	break;
    case 1:
	b1 = *(image_buf + offset + 1);
	b2 = *(image_buf + offset + 2);
	value = b2 << 4 | ((0xf0 & b1) >> 4);
	break;
    }
    return value;
}

/* set_fat_entry sets the value of the FAT entry for clusternum to value. */
void set_fat_entry(uint16_t clusternum, uint16_t value,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t offset;
    uint8_t *p1, *p2;
    
    /* this involves some really ugly bit shifting.  This probably
       only works on a little-endian machine. */
    offset = bpb->bpbResSectors * bpb->bpbBytesPerSec * bpb->bpbSecPerClust 
	+ (3 * (clusternum/2));
    switch(clusternum % 2) {
    case 0:
	p1 = image_buf + offset;
	p2 = image_buf + offset + 1;
	/* mjh: little-endian CPUs are really ugly! */
	*p1 = (uint8_t)(0xff & value);
	*p2 = (uint8_t)((0xf0 & (*p2)) | (0x0f & (value >> 8)));
	break;
    case 1:
	p1 = image_buf + offset + 1;
	p2 = image_buf + offset + 2;
	*p1 = (uint8_t)((0x0f & (*p1)) | ((0x0f & value) << 4));
	*p2 = (uint8_t)(0xff & (value >> 4));
	break;
    }
}


/* is_end_of_file returns true if the FAT entry for cluster indicates
   this is the last cluster in a file */
int is_end_of_file(uint16_t cluster) {
    if (cluster >= (FAT12_MASK & CLUST_EOFS)
	&& cluster <= (FAT12_MASK & CLUST_EOFE)) {
	return TRUE;
    } else 
	return FALSE;
}


/* root_dir_addr returns the address in the mmapped disk image for the
   start of the root directory, as indicated in the boot sector */
uint8_t *root_dir_addr(uint8_t *image_buf, struct bpb33* bpb)
{
    uint32_t offset;
    offset = 
	(bpb->bpbBytesPerSec 
	 * (bpb->bpbResSectors + (bpb->bpbFATs * bpb->bpbFATsecs)));
    return image_buf + offset;
}

/* cluster_to_addr returns the memory location where the memory mapped
   cluster actually starts */

uint8_t *cluster_to_addr(uint16_t cluster, uint8_t *image_buf, 
			 struct bpb33* bpb)
{
    uint8_t *p;
    p = root_dir_addr(image_buf, bpb);
    if (cluster != MSDOSFSROOT) {
	/* move to the end of the root directory */
	p += bpb->bpbRootDirEnts * sizeof(struct direntry);
	/* move forward the right number of clusters */
	p += bpb->bpbBytesPerSec * bpb->bpbSecPerClust 
	    * (cluster - CLUST_FIRST);
    }
    return p;
}

