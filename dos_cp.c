/* 3005 coursework 2,   mjh, Nov 2005 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

/* get_name retrieves the filename from a directory entry */

void get_name(char *fullname, struct direntry *dirent) 
{
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);

    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }
    fullname[0]='\0';
    strcat(fullname, name);

    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) {
	strcat(fullname, ".");
	strcat(fullname, extension);
    }
}

/* find_file seeks through the directories in the memory disk image,
   until it finds the named file */

/* flags, depending on whether we're searching for a file or a
   directory */
#define FIND_FILE 0
#define FIND_DIR 1

struct direntry* find_file(char *infilename, uint16_t cluster,
			   int find_mode,
			   uint8_t *image_buf, struct bpb33* bpb)
{
    char buf[MAXPATHLEN];
    char *seek_name, *next_name;
    int d;
    struct direntry *dirent;
    uint16_t dir_cluster;
    char fullname[13];

    /* find the first dirent in this directory */
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    /* first we need to split the file name we're looking for into the
       first part of the path, and the remainder.  We hunt through the
       current directory for the first part.  If there's a remainder,
       and what we find is a directory, then we recurse, and search
       that directory for the remainder */

    strncpy(buf, infilename, MAXPATHLEN);
    seek_name = buf;

    /* trim leading slashes */
    while (*seek_name == '/' || *seek_name == '\\') {
	seek_name++;
    }

    /* search for any more slashes - if so, it's a dirname */
    next_name = seek_name;
    while (1) {
	if (*next_name == '/' || *next_name == '\\') {
	    *next_name = '\0';
	    next_name ++;
	    break;
	}
	if (*next_name == '\0') {
	    /* end of name - no slashes found */
	    next_name = NULL;
	    if (find_mode == FIND_DIR) {
		return dirent;
	    }
	    break;
	}
	next_name++;
    }

    while (1) {
	/* hunt a cluster for the relevant dirent.  If we reach the
	   end of the cluster, we'll need to go to the next cluster
	   for this directory */
	for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; 
	     d += sizeof(struct direntry)) {
	    if (dirent->deName[0] == SLOT_EMPTY) {
		/* we failed to find the file */
		return NULL;
	    }
	    if (dirent->deName[0] == SLOT_DELETED) {
		/* skip over a deleted file */
		dirent++;
		continue;
	    }
	    get_name(fullname, dirent);
	    if (strcmp(fullname, seek_name)==0) {
		/* found it! */
		if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		    /* it's a directory */
		    if (next_name == NULL) {
			fprintf(stderr, "Cannot copy out a directory\n");
			exit(1);
		    }
		    dir_cluster = getushort(dirent->deStartCluster);
		    return find_file(next_name, dir_cluster, 
				     find_mode, image_buf, bpb);
		} else if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
		    /* it's a volume */
		    fprintf(stderr, "Cannot copy out a volume\n");
		    exit(1);
		} else {
		    /* assume it's a file */
		    return dirent;
		}
	    }
	    dirent++;
	}
	/* we've reached the end of the cluster for this directory.
	   Where's the next cluster? */
	if (cluster == 0) {
	    // root dir is special
	    dirent++;
	} else {
	    cluster = get_fat_entry(cluster, image_buf, bpb);
	    dirent = (struct direntry*)cluster_to_addr(cluster, 
						       image_buf, bpb);
	}
    }
}

/* copy_out_file actually does the work of copying, recursing through
   the clusters of the memory disk image, and copying out a cluster at
   a time */

void copy_out_file(FILE *fd, uint16_t cluster, uint32_t bytes_remaining,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    int total_clusters, clust_size;
    uint8_t *p;

    clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    if (cluster == 0) {
	fprintf(stderr, "Bad file termination\n");
	return;
    } else if (is_end_of_file(cluster)) {
	return;	
    } else if (cluster > total_clusters) {
	abort(); /* this shouldn't be able to happen */
    }

    /* map the cluster number to the data location */
    p = cluster_to_addr(cluster, image_buf, bpb);

    if (bytes_remaining <= clust_size) {
	/* this is the last cluster */
	fwrite(p, bytes_remaining, 1, fd);
    } else {
	/* more clusters after this one */
	fwrite(p, clust_size, 1, fd);

	/* recurse, continuing to copy */
	copy_out_file(fd, get_fat_entry(cluster, image_buf, bpb), 
		      bytes_remaining - clust_size, image_buf, bpb);
    }
    return;
}

/* copyout copies a file from the FAT-12 memory disk image to a
   regular file in the file system */

void copyout(char *infilename, char* outfilename,
	     uint8_t *image_buf, struct bpb33* bpb)
{
    struct direntry *dirent = (void*)1;
    FILE *fd;
    uint16_t start_cluster;
    uint32_t size;

    /* skip the volume name */
    assert(strncmp("a:", infilename, 2)==0);
    infilename+=2;

    /* find the dirent of the file in the memory disk image */
    dirent = find_file(infilename, 0, FIND_FILE, image_buf, bpb);
    if (dirent == NULL) {
	fprintf(stderr, "No file called %s exists in the disk image\n",
		infilename);
	exit(1);
    }

    /* open the real file for writing */
    fd = fopen(outfilename, "w");
    if (fd == NULL) {
	fprintf(stderr, "Can't open file %s to copy data out\n",
		outfilename);
	exit(1);
    }

    /* do the actual copy out*/
    start_cluster = getushort(dirent->deStartCluster);
    size = getulong(dirent->deFileSize);
    copy_out_file(fd, start_cluster, size, image_buf, bpb);
    
    fclose(fd);
}

/* copy_in_file actually does the copying of the file into the memory
   image, updates the FAT, and returns the starting cluster of the
   file */

uint16_t copy_in_file(FILE* fd, uint8_t *image_buf, struct bpb33* bpb, 
		      uint32_t *size)
{
    uint32_t clust_size, total_clusters, i;
    uint8_t *buf;
    size_t bytes;
    uint16_t start_cluster = 0;
    uint16_t prev_cluster = 0;
    
    clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    buf = malloc(clust_size);
    while(1) {
	/* read a block of data, and store it */
	bytes = fread(buf, 1, clust_size, fd);
	if (bytes > 0) {
	    *size += bytes;

	    /* find a free cluster */
	    for (i = 2; i < total_clusters; i++) {
		if (get_fat_entry(i, image_buf, bpb) == CLUST_FREE) {
		    break;
		}
	    }

	    if (i == total_clusters) {
		/* oops - we ran out of disk space */
		fprintf(stderr, "No more space in filesystem\n");
		/* we should clean up here, rather than just exit */ 
		exit(1);
	    }

	    /* remember the first cluster, as we need to store this in
	       the dirent */
	    if (start_cluster == 0) {
		start_cluster = i;
	    } else {
		/* link the previous cluster to this one in the FAT */
		assert(prev_cluster != 0);
		set_fat_entry(prev_cluster, i, image_buf, bpb);
	    }
	    /* make sure we've recorded this cluster as used */
	    set_fat_entry(i, FAT12_MASK&CLUST_EOFS, image_buf, bpb);

	    /* copy the data into the cluster */
	    memcpy(cluster_to_addr(i, image_buf, bpb), buf, clust_size);
	}
	if (bytes < clust_size) {
	    /* We didn't real a full cluster, so we either got a read
	       error, or reached end of file.  We exit anyway */
	    break;
	}
	prev_cluster = i;
    }

    free(buf);
    return start_cluster;
}

/* write the values into a directory entry */
void write_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
	if (p2[i] == '/' || p2[i] == '\\') {
	    uppername = p2+i+1;
	}
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
	uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
	fprintf(stderr, "No filename extension given - defaulting to .___\n");
    } else {
	*p = '\0';
	p++;
	len = strlen(p);
	if (len > 3) len = 3;
	memcpy(dirent->deExtension, p, len);
    }
    if (strlen(uppername)>8) {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* a real filesystem would set the time and date here, but it's
       not necessary for this coursework */
}


/* create_dirent finds a free slot in the directory, and write the
   directory entry */

void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    while(1) {
	if (dirent->deName[0] == SLOT_EMPTY) {
	    /* we found an empty slot at the end of the directory */
	    write_dirent(dirent, filename, start_cluster, size);
	    dirent++;

	    /* make sure the next dirent is set to be empty, just in
	       case it wasn't before */
	    memset((uint8_t*)dirent, 0, sizeof(struct direntry));
	    dirent->deName[0] = SLOT_EMPTY;
	    return;
	}
	if (dirent->deName[0] == SLOT_DELETED) {
	    /* we found a deleted entry - we can just overwrite it */
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}

/* copyin copies a file from a regular file on the filesystem into a
   file in the FAT-12 memory disk image  */

void copyin(char *infilename, char* outfilename,
	     uint8_t *image_buf, struct bpb33* bpb)
{
    struct direntry *dirent = (void*)1;
    FILE *fd;
    uint16_t start_cluster;
    uint32_t size = 0;

    assert(strncmp("a:", outfilename, 2)==0);
    outfilename+=2;

    /* check that the file doesn't already exist */
    dirent = find_file(outfilename, 0, FIND_FILE, image_buf, bpb);
    if (dirent != NULL) {
	fprintf(stderr, "File %s already exists\n", outfilename);
	exit(1);
    }

    /* find the dirent of the directory to put the file in */
    dirent = find_file(outfilename, 0, FIND_DIR, image_buf, bpb);
    if (dirent == NULL) {
	fprintf(stderr, "Directory does not exists in the disk image\n");
	exit(1);
    }

    /* open the real file for reading */
    fd = fopen(infilename, "r");
    if (fd == NULL) {
	fprintf(stderr, "Can't open file %s to copy data in\n",
		infilename);
	exit(1);
    }

    /* do the actual copy in*/
    start_cluster = copy_in_file(fd, image_buf, bpb, &size);

    /* create the directory entry */
    create_dirent(dirent, outfilename, start_cluster, size, image_buf, bpb);
    
    fclose(fd);
}

void usage()
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  dos_cp <imagename> a:<filename1> <filename2>\n");
    fprintf(stderr, "    copies file called filename1 from disk image to a normal file\n");
    fprintf(stderr, "  dos_cp <imagename> <filename3> a:<filename4>\n");
    fprintf(stderr, "    copies normal file called filename3 into disk image as filename4\n");
    exit(1);
}

int main(int argc, char** argv)
{
    int fd;
    uint8_t *image_buf;
    struct bpb33* bpb;
    if (argc < 4 || argc > 4) {
	usage();
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);

    /* use the "a:" bit to determine whether we're copying in or out */
    if (strncmp("a:", argv[2], 2)==0) {
	/* copy from FAT-12 disk image to external filesystem */
	copyout(argv[2], argv[3], image_buf, bpb);
    } else if (strncmp("a:", argv[3], 2)==0) {
	/* copy from external filesystem to FAT-12 disk image */
	copyin(argv[2], argv[3], image_buf, bpb);
    } else {
	usage();
    }
    close(fd);
    exit(0);
}
