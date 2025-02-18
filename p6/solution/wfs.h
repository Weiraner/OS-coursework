#include <time.h>
#include <sys/stat.h>
#include <stdint.h>

#define BLOCK_SIZE (512)
#define MAX_NAME (28)

#define D_BLOCK (6)
#define IND_BLOCK (D_BLOCK + 1)
#define N_BLOCKS (IND_BLOCK + 1)

#define MAX_DISKS 10
#define INODE_SIZE (512)
#define DENTRY_NUM 16

/*
  The fields in the superblock should reflect the structure of the filesystem.
  `mkfs` writes the superblock to offset 0 of the disk image.
  The disk image will have this format:

          d_bitmap_ptr       d_blocks_ptr
               v                  v
+----+---------+---------+--------+--------------------------+
| SB | IBITMAP | DBITMAP | INODES |       DATA BLOCKS        |
+----+---------+---------+--------+--------------------------+
0    ^                   ^
i_bitmap_ptr        i_blocks_ptr

*/

// Superblock
struct wfs_sb
{
    size_t num_inodes;
    size_t num_data_blocks;
    off_t i_bitmap_ptr;
    off_t d_bitmap_ptr;
    off_t i_blocks_ptr;
    off_t d_blocks_ptr;
    // Extend after this line
    int raid;
    int diskNum;
    int diskIndex;
};

// Inode
struct wfs_inode
{
    int num;     /* Inode number */
    mode_t mode; /* File type and mode */
    uid_t uid;   /* User ID of owner */
    gid_t gid;   /* Group ID of owner */
    off_t size;  /* Total size, in bytes */
    int nlinks;  /* Number of links */

    time_t atim; /* Time of last access */
    time_t mtim; /* Time of last modification */
    time_t ctim; /* Time of last status change */

    off_t blocks[N_BLOCKS];
};

// Directory entry
struct wfs_dentry
{
    char name[MAX_NAME];
    int num;
};

// Global variables (declared `extern` for external linkage)
extern char *diskimgs[MAX_DISKS];
extern struct wfs_sb sb;
extern uint8_t *ibitmap;
extern uint8_t *dbitmap;
extern struct wfs_inode *inodes;
extern size_t diskTurn;

// Function declarations
// Initialize metadata for the first disk
int initialMetadata(char *name);
// Check if a given disk image is valid and store its reference
int checkValidDiskfile(char *name, char **diskimgs);
// Filter FUSE-specific arguments from command-line arguments
void filter_argv(int *argc, char *argv[], int i);
// Retrieve a data block by its index
int getDataBlockByDbindex(void *buffer, int db_index);
// Find an inode by name within a directory inode
int getInodeFromName(char *name, int inode_index);
// Parse a path and return the inode index of the file/directory
// mode 0: Read file/directory
// mode 1: Create file in directory
int parsePath(const char *path, int mode, char *output);
// Write a new file/directory entry into a directory
int writeToDir(int dir_inode_index, char *name, int m, int mode);
off_t dataToWrite_ptr(struct wfs_inode *inode, int *data_block_idx);
int createNewInode(char *name, int m, int mode);
int updateMetadata();
void print_non_empty_entries(int disk_index);
size_t getIbit();
size_t getDbit();
void set_ibit(size_t n);
void set_dbit(size_t n);
int write_datablock_toIdx(int db_idx, void *buf);
void read_from_indirect_db(int db_idx, off_t indirect_db[]);
void free_inode_from_parent(const char *path, int inode_index);
void print_data_blocks(int inode_index);