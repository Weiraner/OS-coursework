#include "wfs.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

// initialize superblock
int superblock_initial(struct wfs_sb *sb, int raid, int diskNum, int inodeNum, int blockNum, off_t disk_size)
{
    sb->num_inodes = inodeNum;
    sb->num_data_blocks = blockNum;
    sb->i_bitmap_ptr = sizeof(struct wfs_sb);
    sb->d_bitmap_ptr = sb->i_bitmap_ptr + inodeNum / 8;

    sb->i_blocks_ptr = ((sb->d_bitmap_ptr + blockNum / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
    sb->d_blocks_ptr = sb->i_blocks_ptr + inodeNum * INODE_SIZE;

    sb->raid = raid;
    sb->diskNum = diskNum;

    if (disk_size < sb->d_blocks_ptr + blockNum * BLOCK_SIZE)
    {
        return -1;
    }

    return 0;
}

int write_bitmap(int fd, off_t offset, size_t size, uint8_t initial_value)
{
    uint8_t *bitmap = calloc(1, size);
    if (bitmap == NULL)
    {
        perror("Error: bitmap initialization");
        return -1;
    }
    bitmap[0] = initial_value;

    if (lseek(fd, offset, SEEK_SET) == -1)
    {
        perror("Error: bitmap offset");
        free(bitmap);
        return -1;
    }
    if (write(fd, bitmap, size) != size)
    {
        perror("Error: bitmap modification");
        free(bitmap);
        return -1;
    }

    free(bitmap);
    return 0;
}

int mkfs_initial(int raid, char **diskimgs, int diskNum, int inodeNum, int blockNum)
{
    int fd = -1;
    struct stat file_stat;
    struct wfs_sb sb;
    struct wfs_inode root_inode;
    off_t disk_size;
    int i;

    // Align inodeNum and blockNum to multiples of 32
    inodeNum = ((inodeNum + 31) / 32) * 32;
    blockNum = ((blockNum + 31) / 32) * 32;

    // Open the first disk to calculate disk size and initialize superblock
    fd = open(diskimgs[0], O_RDWR);
    if (fd == -1)
    {
        perror("Error opening disk image");
        return -1;
    }
    if (fstat(fd, &file_stat) == -1)
    {
        perror("Error getting file stats");
        close(fd);
        return -1;
    }
    disk_size = file_stat.st_size;
    close(fd);

    if (superblock_initial(&sb, raid, diskNum, inodeNum, blockNum, disk_size) != 0)
    {
        fprintf(stderr, "Error setting up superblock; disk image may be too small\n");
        return -1;
    }

    // Initialize root inode
    memset(&root_inode, 0, sizeof(struct wfs_inode));
    root_inode.num = 0;
    root_inode.mode = S_IFDIR | 0755;
    root_inode.uid = getuid();
    root_inode.gid = getgid();
    root_inode.size = 0;
    root_inode.nlinks = 2;
    time_t current_time = time(NULL);
    root_inode.atim = root_inode.mtim = root_inode.ctim = current_time;
    root_inode.blocks[0] = 0;

    // Iterate over all disks to set up the file system
    for (i = 0; i < diskNum; i++)
    {
        fd = open(diskimgs[i], O_RDWR);
        if (fd == -1)
        {
            perror("Error: open disk");
            goto error;
        }
        if (fstat(fd, &file_stat) == -1)
        {
            perror("Error: get file stats");
            goto error;
        }
        if (file_stat.st_size < sb.d_blocks_ptr + (blockNum * BLOCK_SIZE))
        {
            fprintf(stderr, "Error: Disk image %s is too small\n", diskimgs[i]);
            goto error;
        }

        // change the diskIndex in sb
        sb.diskIndex = i;

        // Write superblock
        if (lseek(fd, 0, SEEK_SET) == -1 || write(fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb))
        {
            perror("Error: superblock initialize");
            goto error;
        }

        // Write inode bitmap
        size_t inodemapSize = sb.d_bitmap_ptr - sb.i_bitmap_ptr;
        if (write_bitmap(fd, sb.i_bitmap_ptr, inodemapSize, 0x01) != 0)
            goto error;

        // Write data bitmap
        size_t datamapSize = sb.i_blocks_ptr - sb.d_bitmap_ptr;
        if (write_bitmap(fd, sb.d_bitmap_ptr, datamapSize, 0x00) != 0)
            goto error;

        // Write root inode
        uint8_t buffer[BLOCK_SIZE] = {0};
        memcpy(buffer, &root_inode, sizeof(struct wfs_inode));
        off_t rootOffset = sb.i_blocks_ptr + (root_inode.num * BLOCK_SIZE);
        if (lseek(fd, rootOffset, SEEK_SET) == -1 || write(fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
        {
            perror("Error writing root inode");
            goto error;
        }

        close(fd);
        fd = -1; // Reset fd for next disk
    }

    return 0;

error:
    if (fd != -1)
        close(fd);
    return -1;
}

int main(int argc, char *argv[])
{
    int raid = -1;
    char *diskimgs[MAX_DISKS];
    int diskNum = 0;
    int inodeNum = -1;
    int blockNum = -1;
    int opt;

    while ((opt = getopt(argc, argv, "r:d:i:b:")) != -1)
    {
        switch (opt)
        {
        case 'r':
            if (strcmp(optarg, "0") == 0)
            {
                raid = 0;
            }
            else if (strcmp(optarg, "1") == 0)
            {
                raid = 1;
            }
            else if (strcmp(optarg, "1v") == 0)
            {
                raid = 2;
            }
            else
            {
                return 1;
            }
            break;
        case 'd':
            if (diskNum >= MAX_DISKS)
            {
                return 1;
            }
            diskimgs[diskNum++] = optarg;
            break;
        case 'i':
            inodeNum = atoi(optarg);
            break;
        case 'b':
            blockNum = atoi(optarg);
            break;
            return 1;
        }
    }

    if (raid == -1 || diskNum == 0 || ((raid == 0 || raid == 1 || raid == 2) && diskNum < 2) || (inodeNum == -1 || blockNum == -1))
    {
        return 1;
    }

    if (mkfs_initial(raid, diskimgs, diskNum, inodeNum, blockNum) < 0)
    {
        return -1;
    }

    return 0;
}
