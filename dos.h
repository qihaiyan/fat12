/* 3005 Coursework 2, mjh, Nov 2005 */

#define MAXPATHLEN 255

#ifndef TRUE
#define TRUE (1)
#define FALSE (0)
#endif


/* prototypes for functions in dos.c */

#include <stdint.h>

uint8_t *mmap_file(char *filename, int *fd);
struct bpb33* check_bootsector(uint8_t *image_buf);
uint16_t get_fat_entry(uint16_t clusternum, uint8_t *image_buf, 
		       struct bpb33* bpb);
void set_fat_entry(uint16_t clusternum, uint16_t value, 
		   uint8_t *image_buf, struct bpb33* bpb);
int is_end_of_file(uint16_t cluster) ;
uint8_t *root_dir_addr(uint8_t *image_buf, struct bpb33* bpb);
uint8_t *cluster_to_addr(uint16_t cluster, uint8_t *image_buf, 
			 struct bpb33* bpb);
