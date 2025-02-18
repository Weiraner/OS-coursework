#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "wfs.h"
#include <stdlib.h>

// global variables
char *diskimgs[MAX_DISKS];
struct wfs_sb sb;
uint8_t *ibitmap = NULL;
uint8_t *dbitmap = NULL;
struct wfs_inode *inodes = NULL;
size_t diskTurn = 0;
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int wfs_getattr(const char *path, struct stat *stbuf)
{
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...
    int inode_index = parsePath(path, 0, NULL);
    if (inode_index < 0)
    {
        // printf("The path %s doesn't exist\ninode_index:%d\n", path, inode_index);
        return -ENOENT;
    }

    stbuf->st_uid = inodes[inode_index].uid;
    stbuf->st_gid = inodes[inode_index].gid;
    stbuf->st_atime = inodes[inode_index].atim;
    stbuf->st_mtime = inodes[inode_index].mtim;
    stbuf->st_mode = inodes[inode_index].mode;
    stbuf->st_size = inodes[inode_index].size;
    // printf("find path %s\n", path);
    return 0; // Return 0 on success
}

static int wfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    printf("Enter wfs_mknod: %s\n", path);
    if (getIbit() == sb.num_inodes)
    {
        return -ENOSPC;
    }
    // if (!S_ISREG(mode))
    // {
    //     perror("Error: not use mknod to create a common file\n");
    //     return -EINVAL;
    // }
    // get the inode index of dir being write to
    char name[MAX_NAME + 2];
    int inode_index = parsePath(path, 1, name);
    if (inode_index == -2)
    {
        perror("Error: same name already exist\n");
        return -EEXIST;
    }
    else if (inode_index == -1)
    {
        perror("Error: path cannot be accessed\n");
        return -ENOENT;
    }

    if (writeToDir(inode_index, name, 0, mode) < 0)
    {
        perror("Error: not enough space\n");
        return -ENOSPC;
    }
    updateMetadata();
    print_non_empty_entries(0);
    return 0;
}
static int wfs_mkdir(const char *path, mode_t mode)
{
    printf("Enter test wfs_mkdir:%s\n", path);
    if (getIbit() == sb.num_inodes)
    {
        return -ENOSPC;
    }
    char name[MAX_NAME + 2];
    int inode_index = parsePath(path, 1, name);
    if (inode_index == -2)
    {
        perror("Error: same name already exist\n");
        return -EEXIST;
    }
    else if (inode_index == -1)
    {
        perror("Error: path cannot be accessed\n");
        return -ENOENT;
    }

    // printf("mkdir: inode_index: %d, name: %s\n", inode_index, name);
    if (writeToDir(inode_index, name, 1, mode) < 0)
    {
        perror("Error: not enough space\n");
        return -ENOSPC;
    }
    updateMetadata();
    // printf("Successful create dir %s\n", name);
    printf("disk1:");
    print_non_empty_entries(0);
    printf("disk2:");
    print_non_empty_entries(1);
    printf("disk3:");
    print_non_empty_entries(2);
    return 0;
}
static int wfs_unlink(const char *path)
{
    int inode_index = parsePath(path, 0, NULL);
    if (inode_index < 0)
    {
        perror("The path doesn't exist");
        return -ENOENT;
    }
    if (S_ISDIR(inodes[inode_index].mode))
    {
        perror("Error: Try to unlink a dir");
        return -1;
    }

    // free db it takes up
    for (int i = 0; i < N_BLOCKS; i++)
    {
        int db_idx = inodes[inode_index].blocks[i] - 1;
        if (db_idx == -1)
            continue;
        // unlink indirect blocks
        if (i == N_BLOCKS - 1)
        {
            off_t indirect_db_idx[BLOCK_SIZE / sizeof(off_t)];
            read_from_indirect_db(db_idx, indirect_db_idx);
            for (int j = 0; j < BLOCK_SIZE / sizeof(off_t); j++)
            {
                off_t db_indirect_block = indirect_db_idx[j] - 1;
                printf("Unlink: unmap block_idx:%d,j:%d,data_block_ids:%d\n", i, j, db_idx);
                if (db_indirect_block == -1)
                    continue;
                dbitmap[db_indirect_block / 8] &= ~(1 << db_indirect_block % 8);
            }
        }
        // unlink direct blocks
        dbitmap[db_idx / 8] &= ~(1 << db_idx % 8);
        // printf("Unlink: unmap dbitmap: block_idx: %d, data_block_idx: %d\n", i, db_idx);
    }

    free_inode_from_parent(path, inode_index);
    print_non_empty_entries(0);
    return 0;
}
static int wfs_rmdir(const char *path)
{
    int inode_index = parsePath(path, 0, NULL);
    if (inode_index < 0)
    {
        perror("The path doesn't exist");
        return -ENOENT;
    }
    if (!S_ISDIR(inodes[inode_index].mode))
    {
        perror("Error: Try to rmdir a file");
        return -1;
    }

    // 新增：检查目录是否为空
    if (inodes[inode_index].size > 0) // 假设 size 表示目录中的条目数
    {
        perror("Error: Directory is not empty");
        return -ENOTEMPTY; // 返回目录不为空的错误
    }

    free_inode_from_parent(path, inode_index);
    print_non_empty_entries(0);
    return 0;
}
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("Enter wfs_read\n");
    int inode_index = parsePath(path, 0, NULL);
    if (inode_index < 0)
    {
        perror("The path doesn't exist");
        return -ENOENT;
    }
    if (S_ISDIR(inodes[inode_index].mode))
    {
        perror("Error: Try to read a dir");
        return -1;
    }
    if (offset > inodes[inode_index].size)
    {
        perror("Error: Try to read out of a file");
        return -1;
    }

    struct wfs_inode inode = inodes[inode_index];
    if (offset + size > inode.size)
    {
        size = inode.size - offset;
    }

    // read the file
    size_t read = 0;
    while (size > 0)
    {
        int block_idx = offset / BLOCK_SIZE;
        int db_offset = offset % BLOCK_SIZE;
        off_t db_idx;

        // direct blocks
        if (block_idx <= D_BLOCK)
        {
            db_idx = inode.blocks[block_idx] - 1;
        }
        // indirect blocks
        else
        {
            // TODO: implement indirect block
            off_t indirect_db_idx[BLOCK_SIZE / sizeof(off_t)];
            read_from_indirect_db(inode.blocks[IND_BLOCK] - 1, indirect_db_idx);
            db_idx = indirect_db_idx[block_idx - IND_BLOCK] - 1;
        }

        char block[BLOCK_SIZE];
        getDataBlockByDbindex((void *)block, db_idx);

        size_t read_bytes = MIN(BLOCK_SIZE - db_offset, size);
        memcpy(buf + read, block + db_offset, read_bytes);

        // // Debug output to check read data
        // printf("Read from block %zu: ", db_idx);
        // for (size_t i = 0; i < read_bytes; i++)
        // {
        //     printf("%02x ", (unsigned char)block[db_offset + i]);
        // }
        // printf("\n");

        read += read_bytes;
        offset += read_bytes;
        size -= read_bytes;
    }
    return read;
}
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("Enter wfs_write: %s\n", path);
    int inode_index = parsePath(path, 0, NULL);
    if (inode_index < 0)
    {
        return -ENOENT;
    }
    // Check if the inode is a directory
    if (S_ISDIR(inodes[inode_index].mode))
    {
        return -EISDIR; // Return error code for writing to a directory
    }
    // Check if the write exceeds the maximum file size
    if (size + offset > (N_BLOCKS - 1 + (BLOCK_SIZE / sizeof(off_t))) * BLOCK_SIZE)
    {
        return -ENOSPC;
    }
    size_t written = 0;

    // save a copy for old dbitmap
    uint8_t *dbitmap_old = malloc((sb.num_data_blocks / 8));
    if (dbitmap_old == NULL)
    {
        perror("Failed to allocate memory for dbitmap_old\n");
        return -1; // 处理内存分配失败
    }
    memcpy(dbitmap_old, dbitmap, (sb.num_data_blocks / 8));
    // save a copy for inode
    struct wfs_inode inode = inodes[inode_index];

    // write the file
    while (size > 0)
    {
        int block_idx = offset / BLOCK_SIZE;
        int db_offset = offset % BLOCK_SIZE;
        off_t db_idx;

        // direct blocks
        if (block_idx <= D_BLOCK)
        {
            if (inode.blocks[block_idx] == 0)
            {
                // Allocate a data block
                int new_block = getDbit();
                if (new_block < 0)
                {
                    memcpy(dbitmap, dbitmap_old, (sb.num_data_blocks / 8));
                    return -ENOSPC;
                }
                inode.blocks[block_idx] = new_block + 1;
                set_dbit(new_block);
            }
            db_idx = inode.blocks[block_idx] - 1;
        }
        // indirect blocks
        else
        {
            // TODO: implement indirect block
            int indirect_block_db_idx = inode.blocks[IND_BLOCK] - 1;
            // initialize indirect dir db
            if (indirect_block_db_idx == -1)
            {
                // Allocate a data block
                int new_block = getDbit();
                if (new_block < 0)
                {
                    memcpy(dbitmap, dbitmap_old, (sb.num_data_blocks / 8));
                    return -ENOSPC;
                }
                inode.blocks[IND_BLOCK] = new_block + 1;
                indirect_block_db_idx = new_block;
                set_dbit(new_block);
            }

            // read indirect dir datablock
            off_t indirect_db_idx[BLOCK_SIZE / sizeof(off_t)];
            read_from_indirect_db(inode.blocks[IND_BLOCK] - 1, indirect_db_idx);

            // initialize indirect db if needed
            if (indirect_db_idx[block_idx - IND_BLOCK] == 0)
            {
                // Allocate a data block
                int new_block = getDbit();
                if (new_block < 0)
                {
                    memcpy(dbitmap, dbitmap_old, (sb.num_data_blocks / 8));
                    return -ENOSPC;
                }
                indirect_db_idx[block_idx - IND_BLOCK] = new_block + 1;
                write_datablock_toIdx(indirect_block_db_idx, indirect_db_idx);
                set_dbit(new_block);
            }
            db_idx = indirect_db_idx[block_idx - IND_BLOCK] - 1;
        }

        char block[BLOCK_SIZE];
        getDataBlockByDbindex((void *)block, db_idx);

        size_t copy_bytes = MIN(BLOCK_SIZE - db_offset, size);
        memcpy(block + db_offset, buf + written, copy_bytes);

        write_datablock_toIdx(db_idx, block);

        // // Debug output to check written data
        // printf("Written to block %zu: ", db_idx);
        // for (size_t i = 0; i < copy_bytes; i++)
        // {
        //     printf("%02x ", (unsigned char)block[db_offset + i]);
        // }
        // printf("\n");

        written += copy_bytes;
        offset += copy_bytes;
        size -= copy_bytes;
    }

    // Update inode size if needed
    if (offset > inode.size)
    {
        inode.size = offset;
    }
    inode.mtim = time(NULL);
    inodes[inode_index] = inode;
    updateMetadata();
    print_non_empty_entries(0);
    return written;
}
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    printf("Enter wfs_readdir for path: %s\n", path);

    // 获取目录的 inode 索引
    int inode_index = parsePath(path, 0, NULL);
    if (inode_index < 0)
    {
        perror("The path doesn't exist");
        return -ENOENT;
    }

    // 检查是否是目录
    if (!S_ISDIR(inodes[inode_index].mode))
    {
        perror("Error: Not a directory");
        return -ENOTDIR;
    }

    // 填充目录项
    filler(buf, ".", NULL, 0);  // 添加当前目录项
    filler(buf, "..", NULL, 0); // 添加父目录项

    // 遍历数据块，读取目录项
    for (int i = 0; i < N_BLOCKS; i++)
    {
        int db_index = inodes[inode_index].blocks[i] - 1; // 获取数据块索引
        if (db_index == -1)
            break; // 如果没有更多数据块，退出

        struct wfs_dentry entries[DENTRY_NUM];
        getDataBlockByDbindex(entries, db_index); // 读取数据块

        // 遍历目录项
        for (int j = 0; j < DENTRY_NUM; j++)
        {
            if (entries[j].name[0] != '\0') // 如果目录项有效
            {
                filler(buf, entries[j].name, NULL, 0); // 加目录项到缓冲区
            }
        }
    }

    return 0; // 成功返回
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .unlink = wfs_unlink,
    .rmdir = wfs_rmdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
};

// helper method
int initialMetadata(char *name)
{
    int fd = -1;
    fd = open(name, O_RDWR);
    if (fd == -1)
    {
        perror("Error opening first disk image\n");
        return -1;
    }

    // read the superblock
    if (read(fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb))
    {
        perror("Failed to read superblock\n");
        return -1;
    }

    // allocate and read the ibitmap
    size_t ibitmap_size = sb.num_inodes / 8;
    ibitmap = (uint8_t *)malloc(ibitmap_size);
    if (ibitmap == NULL)
    {
        perror("Failed to allocate memory for ibitmap\n");
        close(fd);
        return 1;
    }
    if (lseek(fd, sb.i_bitmap_ptr, SEEK_SET) == -1)
    {
        perror("Error: ibitmap offset\n");
        free(ibitmap);
        close(fd);
        return -1;
    }
    if (read(fd, ibitmap, ibitmap_size) != ibitmap_size)
    {
        perror("Failed to read ibitmap\n");
        free(ibitmap);
        close(fd);
        return -1;
    }

    // allocate and read the dbitmap
    if (sb.raid != 0)
    {
        size_t dbitmap_size = sb.num_data_blocks / 8;
        dbitmap = (uint8_t *)malloc(dbitmap_size);
        memset(dbitmap, 0, dbitmap_size);
    }
    // raid ==0
    else
    {
        size_t dbitmap_size = (sb.num_data_blocks / 8) * sb.diskNum;
        dbitmap = (uint8_t *)malloc(dbitmap_size);
        memset(dbitmap, 0, dbitmap_size);
    }

    // allocate and read the inodes
    size_t inodes_size = sb.num_inodes * INODE_SIZE;
    inodes = (struct wfs_inode *)malloc(inodes_size);
    if (inodes == NULL)
    {
        perror("Failed to allocate memory for inodes\n");
        free(ibitmap);
        free(dbitmap);
        close(fd);
        return 1;
    }
    if (lseek(fd, sb.i_blocks_ptr, SEEK_SET) == -1)
    {
        perror("Error: inodes offset\n");
        free(ibitmap);
        free(dbitmap);
        free(inodes);
        close(fd);
        return -1;
    }
    if (read(fd, inodes, inodes_size) != inodes_size)
    {
        perror("Failed to read inodes\n");
        free(ibitmap);
        free(dbitmap);
        free(inodes);
        close(fd);
        return -1;
    }

    // return 0 on success
    return 0;
}

int checkValidDiskfile(char *name, char **diskimgs)
{
    int fd = -1;
    fd = open(name, O_RDWR);
    if (fd == -1)
    {
        perror("Error opening disk image\n");
        return 1;
    }

    // read the superblock
    if (read(fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb))
    {
        perror("Failed to read superblock\n");
        return 1;
    }
    close(fd);

    // store name in correct diskimg
    diskimgs[sb.diskIndex] = name;
    return 0;
}

void filter_argv(int *argc, char *argv[], int i)
{
    // move argv pointer, from 1 to i
    for (int j = 1; j + i < *argc; j++)
    {
        argv[j] = argv[j + i];
    }

    // 更新 argc
    *argc -= i;
}
// find the datablock (512b) corresponding to the db_index
int getDataBlockByDbindex(void *buffer, int db_index)
{
    // striping mode
    if (sb.raid == 0)
    {
        int fd = open(diskimgs[db_index % sb.diskNum], O_RDWR);
        off_t db_start_ptr = sb.d_blocks_ptr + (db_index / sb.diskNum) * BLOCK_SIZE;
        lseek(fd, db_start_ptr, SEEK_SET);
        if (read(fd, buffer, BLOCK_SIZE) < BLOCK_SIZE)
        {
            perror("Error: read from datablock\n");
            return -1;
        }
        close(fd);
    }
    // mirroring mode
    else if (sb.raid == 1)
    {
        int fd = open(diskimgs[0], O_RDWR);
        off_t db_start_ptr = sb.d_blocks_ptr + db_index * BLOCK_SIZE;
        // printf("mainmethodIndex: %jd\n", (intmax_t)db_start_ptr);
        lseek(fd, db_start_ptr, SEEK_SET);
        if (read(fd, buffer, BLOCK_SIZE) < BLOCK_SIZE)
        {
            perror("Error: read from datablock\n");
            return -1;
        }
        close(fd);
    }
    else if (sb.raid == 2)
    {
        char temp_buffers[sb.diskNum][BLOCK_SIZE]; // 用于存储从每个磁盘读取的数据块
        int votes[sb.diskNum];                     // 用于记录每个数据块的得票数
        memset(votes, 0, sizeof(votes));           // 初始化得票数为0

        for (int i = 0; i < sb.diskNum; i++)
        {
            int fd = open(diskimgs[i], O_RDWR);
            if (fd < 0)
            {
                perror("Error: open disk image\n");
                return -1;
            }

            off_t db_start_ptr = sb.d_blocks_ptr + db_index * BLOCK_SIZE;
            lseek(fd, db_start_ptr, SEEK_SET);

            if (read(fd, temp_buffers[i], BLOCK_SIZE) < BLOCK_SIZE)
            {
                perror("Error: read from datablock\n");
                close(fd);
                return -1;
            }
            close(fd);
        }

        for (int i = 0; i < sb.diskNum; i++)
        {
            for (int j = 0; j < sb.diskNum; j++)
            {
                if (memcmp(temp_buffers[i], temp_buffers[j], BLOCK_SIZE) == 0)
                {
                    votes[i]++;
                }
            }
        }

        int max_votes = -1;
        int selected_index = 0;
        for (int i = 0; i < sb.diskNum; i++)
        {
            if (votes[i] > max_votes || (votes[i] == max_votes && i < selected_index))
            {
                max_votes = votes[i];
                selected_index = i;
            }
        }

        memcpy(buffer, temp_buffers[selected_index], BLOCK_SIZE);
    }

    // verified mirroring
    // TODO
    // 问题：1v太tm麻烦了等会做
    // else
    // {
    // }
    return 0;
}

// return the num corresponding to name in this inode with inode_index file
// return -1 if file/dir with name do not exist
int getInodeFromName(char *name, int inode_index)
{
    // find file/dir name
    for (int i = 0; i < N_BLOCKS; i++)
    {
        int db_index = inodes[inode_index].blocks[i] - 1;
        // printf("db_index: %d, inode_index: %d, i: %d\n", db_index, inode_index, i);
        // if db_index =-1, then this block have been allocated to a datablock yet
        if (db_index == -1)
            break;
        struct wfs_dentry db[DENTRY_NUM];
        // printf("name: %s,db_index: %d\n", name, db_index);
        getDataBlockByDbindex(db, db_index);
        for (int j = 0; j < DENTRY_NUM; j++)
        {
            if (db[j].name[0] != '\0')
                // printf("i:%d, j:%d name: %s\n", i, j, db[j].name);
                if (strcmp(db[j].name, name) == 0)
                {
                    return db[j].num;
                }
        }
    }
    // if fail to find it after searching, then return -1;
    return -1;
}

// parsePath, return the inode number of the corresponding file/dir
// mode0: read the file/dir of the path
// mode1: try to create the file of the path
// mode2: read parent path
int parsePath(const char *input, int mode, char *output)
{
    int inode_index = 0;
    char *path = strdup(input);
    char *token = strtok((char *)path, "/");
    // return 0 when root dir
    if (token == NULL)
        return inode_index;

    while (token != NULL)
    {
        char *name = token;
        token = strtok(NULL, "/");
        // if next token is NULL, then name should be the last one in the path
        if (token == NULL)
        {
            // read mode = 0
            if (mode == 0)
            {
                if (getInodeFromName(name, inode_index) < 0)
                {
                    return -1;
                }
                // return the inode of the file/dir of the path
                return getInodeFromName(name, inode_index);
            }
            // create mode =1
            else if (mode == 1)
            {
                int res = getInodeFromName(name, inode_index);
                if (res >= 0)
                {
                    perror("Error: such name exist\n");
                    return -2;
                }
                if (res == -2)
                {
                    perror("Shoudn't enter here\n");
                    return -2;
                }
                strcpy(output, name);
                // return the inode_index of the dir of file being created
                return inode_index;
            }
            else if (mode == 2)
            {
                strcpy(output, name);
                return inode_index;
            }
        }
        // else name should be a dir
        else
        {
            // find the inode_index corresponding to the name
            inode_index = getInodeFromName(name, inode_index);
            if (inode_index < 0)
            {
                perror("Error: 2. fail to find such dir/file\n");
                return -1;
            }
            // if it is not a dir, error
            if (!S_ISDIR(inodes[inode_index].mode))
            {
                perror("Error: Not a directory\n");
                return -1;
            }
        }
    }
    return -1;
}

// write entry to file
// mode=0, file entry; mode = 1, dir entry
// return -2 if the disk is not enough
int writeToDir(int inode_index, char *name, int m, int mode)
{

    struct wfs_inode inode = inodes[inode_index];
    if (sb.raid == 0)
    {
        // get first dentry block with empty dentry
        int data_block_idx;
        off_t newDataPtr = dataToWrite_ptr(&inode, &data_block_idx);
        struct wfs_dentry db[DENTRY_NUM];

        getDataBlockByDbindex(db, data_block_idx);

        // update parent inode
        inode.size += sizeof(struct wfs_dentry);
        if (m == 1)
            inode.nlinks++;
        inodes[inode_index] = inode;

        // update parent data block
        struct wfs_dentry new_dentry;
        strncpy(new_dentry.name, name, MAX_NAME - 1);
        new_dentry.name[MAX_NAME - 1] = '\0';
        new_dentry.num = getIbit();
        db[newDataPtr] = new_dentry;

        // update new child Inode
        if (createNewInode(name, m, mode) < 0)
        {
            return -2;
        }

        // write to the file
        write_datablock_toIdx(data_block_idx, db);

        return 0;
    }
    // 问题 在写dir的时候要区别1/1v吗
    else
    {
        off_t newDataPtr = dataToWrite_ptr(&inode, NULL);
        if (newDataPtr <= 0)
        {
            perror("Error: Not enough data blocks\n");
            return -2;
        }
        // update the parent inode
        inode.size += sizeof(struct wfs_dentry);
        if (m == 1)
            inode.nlinks++;
        inodes[inode_index] = inode;

        // update the parent data block
        struct wfs_dentry new_dentry;
        strncpy(new_dentry.name, name, MAX_NAME - 1);
        new_dentry.name[MAX_NAME - 1] = '\0';
        new_dentry.num = getIbit();

        // update new child Inode
        if (createNewInode(name, m, mode) < 0)
        {
            return -2;
        }
        // 写入每个文件
        for (int i = 0; i < sb.diskNum; i++)
        {
            int fd = open(diskimgs[i], O_RDWR);
            if (fd == -1)
            {
                perror("Error opening disk image\n");
                return -1;
            }

            // Check if newDataPtr is a multiple of 512
            if (newDataPtr % 512 == 0)
            {
                // Clear the 512 bytes starting from newDataPtr
                char clear_buffer[512] = {0};
                if (pwrite(fd, clear_buffer, sizeof(clear_buffer), newDataPtr) != sizeof(clear_buffer))
                {
                    perror("Error clearing block before writing dentry\n");
                    close(fd);
                    return -1;
                }
            }
            // write the new dentry
            if (pwrite(fd, &new_dentry, sizeof(struct wfs_dentry), newDataPtr) != sizeof(struct wfs_dentry))
            {
                perror("Error writing dentry\n");
                close(fd);
                return -1;
            }

            close(fd);
            fd = -1;
        }
        // return 0 on success
        return 0;
    }
}
// get ptr to write from inode
// return -1 if there is not enough space
off_t dataToWrite_ptr(struct wfs_inode *inode, int *datablock_block_idx)
{
    // check if enough data block
    size_t dbit = getDbit();
    off_t write_ptr;

    // find the first empty dentry
    for (int i = 0; i < N_BLOCKS; i++)
    {
        off_t dentry_block_idx = inode->blocks[i] - 1;
        printf("inodeNum:%d, block_idx: %d, db_block_idx: %zu\n", inode->num, i, dentry_block_idx);
        // create a new dentry block
        if (inode->blocks[i] == 0)
        {
            inode->blocks[i] = dbit + 1;
            write_ptr = sb.d_blocks_ptr + dbit * BLOCK_SIZE;
            set_dbit(dbit);
            // printf("new block, write_ptr: %zu\n", write_ptr);
            printf("inodeNum:%d, block_idx: %d, db_block_idx+1: %zu\n", inode->num, i, inode->blocks[i]);
            if (sb.raid == 0)
            {
                *datablock_block_idx = dbit;
                return 0;
            }
            else
            {
                return write_ptr;
            }
        }
        // search for empty dentry
        struct wfs_dentry db[DENTRY_NUM];
        getDataBlockByDbindex(db, dentry_block_idx);
        for (int j = 0; j < DENTRY_NUM; j++)
        {
            // find a empty dentry
            if (db[j].name[0] == '\0')
            {
                if (sb.raid == 0)
                {
                    *datablock_block_idx = dentry_block_idx;
                    return j;
                }
                else
                {
                    write_ptr = sb.d_blocks_ptr + dentry_block_idx * BLOCK_SIZE + j * sizeof(struct wfs_dentry);
                    // printf("dentry: %d, write_ptr: %zu\n", j, write_ptr);
                    return write_ptr;
                }
            }
        }
    }
    return -1;
    // if (inode->size % BLOCK_SIZE == 0)
    // {
    //     inode->blocks[inode->size / BLOCK_SIZE] = dbit + 1;
    //     write_ptr = sb.d_blocks_ptr + dbit * BLOCK_SIZE;
    //     set_dbit(dbit);
    // }
    // else
    // {
    //     size_t db_index = inode->blocks[inode->size / BLOCK_SIZE] - 1;
    //     write_ptr = sb.d_blocks_ptr + db_index * BLOCK_SIZE + inode->size % BLOCK_SIZE;
    // }
    // printf("write_ptr: %zu\n", write_ptr);
    // return write_ptr;
}

// create a inode in inodes
// m 0-file 1-dir
// mode:权限
int createNewInode(char *name, int m, int mode)
{
    struct wfs_inode newInode;
    size_t ibit = getIbit();
    if (ibit < 0)
    {
        return -1;
    }
    memset(&newInode, 0, sizeof(struct wfs_inode));
    newInode.num = ibit;
    newInode.uid = getuid();
    newInode.gid = getgid();
    newInode.size = 0;
    newInode.atim = newInode.mtim = newInode.ctim = time(NULL);
    // file inode
    if (m == 0)
    {
        newInode.mode = mode;
        newInode.nlinks = 1;
    }
    // dir inode
    else
    {
        newInode.mode = S_IFDIR | mode;
        newInode.nlinks = 2;
    }
    inodes[ibit] = newInode;
    set_ibit(ibit);
    // printf("Inode number: %d, atim: %ld\n", inodes[ibit - 1].num, inodes[ibit - 1].atim);
    return 0;
}

// update metadata
int updateMetadata()
{
    // write metadata to all disk images
    for (int i = 0; i < sb.diskNum; i++)
    {
        int disk_fd = open(diskimgs[i], O_WRONLY);
        if (disk_fd < 0)
        {
            perror("Error opening disk image");
            return -1;
        }

        // write inode bitmap
        if (pwrite(disk_fd, ibitmap, sb.num_inodes / 8, sb.i_bitmap_ptr) != sb.num_inodes / 8)
        {
            perror("Error writing inode bitmap\n");
            close(disk_fd);
            return -1;
        }
        // write data bitmap
        uint8_t *dbitmap_to_fill = malloc(sb.num_data_blocks / 8);
        if (!dbitmap_to_fill)
        {
            perror("Error allocating memory for dbitmap_to_fill\n");
            close(disk_fd);
            return -1;
        }

        if (sb.raid == 0)
        {
            memset(dbitmap_to_fill, 0, sb.num_data_blocks / 8); // Initialize dbitmap_to_fill with 0

            for (int j = 0; j < sb.num_data_blocks; j++)
            {
                if ((j % sb.diskNum) == i)
                {
                    int byte_index = j / 8;
                    int bit_index = j % 8;
                    if (dbitmap[byte_index] & (1 << bit_index))
                    {
                        dbitmap_to_fill[byte_index] |= (1 << bit_index);
                    }
                }
            }
        }
        else
        {
            memcpy(dbitmap_to_fill, dbitmap, sb.num_data_blocks / 8);
        }

        if (pwrite(disk_fd, dbitmap_to_fill, sb.num_data_blocks / 8, sb.d_bitmap_ptr) != sb.num_data_blocks / 8)
        {
            perror("Error writing data bitmap\n");
            free(dbitmap_to_fill);
            close(disk_fd);
            return -1;
        }

        free(dbitmap_to_fill);

        // // write data bitmap
        // uint8_t *dbitmap_to_fill = malloc(sb.num_data_blocks / 8);
        // if (sb.raid == 0)
        // {
        // }
        // else
        // {
        //     memcpy(dbitmap_to_fill, dbitmap, sb.num_data_blocks / 8);
        // }
        // if (pwrite(disk_fd, dbitmap_to_fill, sb.num_data_blocks / 8, sb.d_bitmap_ptr) != sb.num_data_blocks / 8)
        // {
        //     perror("Error writing data bitmap\n");
        //     close(disk_fd);
        //     return -1;
        // }

        // write inodes
        for (int j = 0; j < sb.num_inodes; j++)
        {
            off_t offset = sb.i_blocks_ptr + j * 512; // 每个 inode 写入第 j 个 512B 块
            if (pwrite(disk_fd, &inodes[j], INODE_SIZE, offset) != INODE_SIZE)
            {
                perror("Error writing inode");
                close(disk_fd);
                return -1;
            }
        }

        close(disk_fd); // 关闭当前磁盘映像文件
    }

    return 0;
}

// 修改后的函数
void print_non_empty_entries(int disk_index)
{
    // printf("Debug: Entering print_non_empty_entries\n");

    // 打开文件
    int fd = open(diskimgs[disk_index], O_RDONLY);
    if (fd < 0)
    {
        perror("Error opening disk file");
        return;
    }

    // printf("Debug: Disk file '%s' opened successfully\n", diskimgs[disk_index]);

    for (size_t block = 0; block < getDbit(); ++block)
    {
        // 检查数据块是否已分配
        if (!(dbitmap[block / 8] & (1 << (block % 8))))
        {
            continue;
        }

        // printf("Debug: Accessing block %zu\n", block);

        // 计算数据块的偏移量
        off_t block_offset = sb.d_blocks_ptr + block * BLOCK_SIZE;
        if (lseek(fd, block_offset, SEEK_SET) < 0)
        {
            perror("Error seeking to block");
            close(fd);
            return;
        }
        // printf("block_offset: %jd\n", (intmax_t)block_offset);

        // 读取数据块内容
        struct wfs_dentry entries[BLOCK_SIZE / sizeof(struct wfs_dentry)];
        ssize_t bytes_read = read(fd, entries, sizeof(entries));
        if (bytes_read < 0)
        {
            perror("Error reading block data");
            close(fd);
            return;
        }

        // 遍历数据块中的目录项
        for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); ++i)
        {
            if (entries[i].name[0] != '\0')
            {
                printf("Name: %s, Num: %d, Index: %d, Block:%zu\n", entries[i].name, entries[i].num, i, block);
            }
        }
    }

    close(fd); // 关闭文件
    // printf("Debug: Exiting print_non_empty_entries\n");
}
// get the index of first empty ibit in ibitmap
size_t getIbit()
{
    for (size_t i = 0; i < sb.num_inodes; i++)
    {
        if (!(ibitmap[i / 8] & (1 << (i % 8))))
        {
            return i; // Return the index of the first empty bit
        }
    }
    return sb.num_inodes; // Return -1 if no empty bit is found
}
// check if n db can be allocated in dbitmap
int checkDbit(int n)
{
    int count = 0; // 计数器，用于跟踪可用的空位数量
    for (size_t i = 0; i < sb.num_data_blocks; i++)
    {
        if (!(dbitmap[i / 8] & (1 << (i % 8))))
        {
            count++;        // 找到一个空位，计数加一
            if (count >= n) // 如果找到了足够的空位
            {
                return 1; // 返回1表示可以分配n个位置
            }
        }
    }
    return 0; // 返回0表示没有足够的空位
}

void read_from_indirect_db(int db_idx, off_t indirect_db[])
{
    // TODO: raid0
    if (sb.raid == 0)
    {
        FILE *diskimg = fopen(diskimgs[db_idx % sb.diskNum], "rb"); // 打开对应的文件
        if (diskimg == NULL)
        {
            perror("Failed to open disk image");
            return;
        }

        // 计算数据块的起始位置
        off_t db_ptr = sb.d_blocks_ptr + (db_idx / sb.diskNum) * BLOCK_SIZE; // 每个块512字节
        fseek(diskimg, db_ptr, SEEK_SET);                                    // 移动到数据块的开始位置

        // 读取数据块到indirect_db数组
        fread(indirect_db, sizeof(off_t), BLOCK_SIZE / sizeof(off_t), diskimg);

        // 清空数据块
        fseek(diskimg, db_ptr, SEEK_SET);                // 再次移动到数据块的开始位置
        char zero_buffer[512] = {0};                     // 创建一个全为零的冲区
        fwrite(zero_buffer, sizeof(char), 512, diskimg); // 写入零以清空数据块

        fclose(diskimg); // 关闭文件
        return;
    }
    else if (sb.raid == 1)
    {
        FILE *diskimg = fopen(diskimgs[0], "rb"); // 打开对应的文件
        if (diskimg == NULL)
        {
            perror("Failed to open disk image");
            return;
        }

        // 计算数据块的起始位置
        off_t db_ptr = sb.d_blocks_ptr + db_idx * BLOCK_SIZE; // 每个块512字节
        fseek(diskimg, db_ptr, SEEK_SET);                     // 移动到数据块的开始位置

        // 读取数据块到indirect_db数组
        fread(indirect_db, sizeof(off_t), BLOCK_SIZE / sizeof(off_t), diskimg);

        // 清空数据块
        fseek(diskimg, db_ptr, SEEK_SET);                // 再次移动到数据块的开始位置
        char zero_buffer[512] = {0};                     // 创建一个全为零的冲区
        fwrite(zero_buffer, sizeof(char), 512, diskimg); // 写入零以清空数据块

        fclose(diskimg); // 关闭文件
    }
    return;
}

// get the index of first empty dbit in dbitmap
size_t getDbit()
{
    for (size_t i = 0; i < sb.num_data_blocks; i++)
    {
        if (!(dbitmap[i / 8] & (1 << (i % 8))))
        {
            return i; // Return the index of the first empty bit
        }
    }
    return -1; // Return -1 if no empty bit is found
}
void set_ibit(size_t n)
{
    size_t byte_index = n / 8;
    size_t bit_index = n % 8;
    ibitmap[byte_index] |= (1 << bit_index);
}
void set_dbit(size_t n)
{
    size_t byte_index = n / 8;
    size_t bit_index = n % 8;
    dbitmap[byte_index] |= (1 << bit_index);

    // 新增：当第 i 位 dbit 被设置为 1 ，打开对应的 diskimgs[i] 并清空相应区域
    if (sb.raid == 0)
    {
        int fd = open(diskimgs[n % sb.diskNum], O_RDWR);
        if (fd < 0)
        {
            perror("Error opening disk image\n");
            return;
        }
        // // // 清空从指针 sb.d_blocks_ptr + BLOCK_SIZE * i 开始的区域
        // off_t clear_ptr = sb.d_blocks_ptr + BLOCK_SIZE * (n % sb.diskNum + 1);
        // char clear_buffer[BLOCK_SIZE] = {0}; // Create a buffer filled with zeros
        // if (pwrite(fd, clear_buffer, sizeof(clear_buffer), clear_ptr) != sizeof(clear_buffer))
        // {
        //     perror("Error clearing block\n");
        // }

        close(fd); // Close the disk image
    }
    else
    {
        if (dbitmap[byte_index] & (1 << bit_index)) // Check if the bit was just set
        {
            int i = n;                           // Use n directly as the index
            for (int j = 0; j < sb.diskNum; j++) // Ensure i is within bounds
            {
                int fd = open(diskimgs[j], O_RDWR);
                if (fd < 0)
                {
                    perror("Error opening disk image\n");
                    return;
                }

                // 清空从指针 sb.d_blocks_ptr + BLOCK_SIZE * i 开始的区域
                off_t clear_ptr = sb.d_blocks_ptr + BLOCK_SIZE * i;
                char clear_buffer[BLOCK_SIZE] = {0}; // Create a buffer filled with zeros
                if (pwrite(fd, clear_buffer, sizeof(clear_buffer), clear_ptr) != sizeof(clear_buffer))
                {
                    perror("Error clearing block\n");
                }

                close(fd); // Close the disk image
            }
        }
    }
}

// Write data back to file
int write_datablock_toIdx(int db_idx, void *buf)
{
    // TODO: raid0
    if (sb.raid == 0)
    {
        int fd = open(diskimgs[db_idx % sb.diskNum], O_RDWR);
        if (fd == -1)
        {
            perror("Error opening disk image\n");
            return -1;
        }
        off_t db_block_ptr = sb.d_blocks_ptr + (db_idx / sb.diskNum) * BLOCK_SIZE;
        printf("write_data_block: db_block_ptr: %zu, db_idx: %d\n", db_block_ptr, db_idx);
        if (pwrite(fd, buf, BLOCK_SIZE, db_block_ptr) != BLOCK_SIZE)
        {
            perror("Error writing dentry\n");
            close(fd);
            return -1;
        }
        close(fd);
        // printf("disk: %s have been written datablock, idx: %d\n content %s\n", diskimgs[db_idx % sb.diskNum], db_idx, (char *)buf);
        return 0;
    }
    else
    {
        for (int i = 0; i < sb.diskNum; i++)
        {
            int fd = open(diskimgs[i], O_RDWR);
            if (fd == -1)
            {
                perror("Error opening disk image\n");
                return -1;
            }
            off_t db_block_ptr = sb.d_blocks_ptr + db_idx * BLOCK_SIZE;
            // write the new datablock
            if (pwrite(fd, buf, BLOCK_SIZE, db_block_ptr) != BLOCK_SIZE)
            {
                perror("Error writing dentry\n");
                close(fd);
                return -1;
            }

            close(fd);
            fd = -1;
        }
        return 0;
    }
}

void free_inode_from_parent(const char *path, int inode_index)
{
    // free its parent thing
    char name[MAX_NAME + 2];
    int parent_inode_idx = parsePath(path, 2, name);
    inodes[parent_inode_idx].nlinks--;
    inodes[parent_inode_idx].size -= sizeof(struct wfs_dentry);

    // free its inode
    ibitmap[inode_index / 8] &= ~(1 << inode_index % 8);

    // find the dentry
    for (int i = 0; i < N_BLOCKS; i++)
    {
        off_t dentry_block_idx = inodes[parent_inode_idx].blocks[i] - 1;
        if (dentry_block_idx == -1)
            break;
        struct wfs_dentry db[DENTRY_NUM];
        getDataBlockByDbindex(db, dentry_block_idx);
        for (int j = 0; j < DENTRY_NUM; j++)
        {
            if (strcmp(db[j].name, name) == 0)
            {
                memset(&db[j], 0, sizeof(struct wfs_dentry));

                // // 输出 db[j] 从 0 到 DENTRY_NUM - 1 的所有 name 和 num
                // for (int k = 0; k < DENTRY_NUM; k++)
                // {
                //     printf("db[%d]: name = %s, num = %d\n", k, db[k].name, db[k].num);
                // }

                write_datablock_toIdx(dentry_block_idx, (void *)db);
                updateMetadata();
                break;
            }
        }
    }
}

// New function to print all data blocks for a given inode
void print_data_blocks(int inode_index)
{
    struct wfs_inode inode = inodes[inode_index];
    for (int i = 0; i < N_BLOCKS; i++)
    {
        int db_index = inode.blocks[i] - 1;
        if (db_index == -1)
            break; // No more data blocks

        char block[BLOCK_SIZE];
        getDataBlockByDbindex((void *)block, db_index);

        // printf("Block %d: ", db_index);
        // for (size_t j = 0; j < BLOCK_SIZE; j++)
        // {
        //     printf("%02x ", (unsigned char)block[j]);
        // }
        // printf("\n");
    }
}

int main(int argc, char *argv[])
{
    // parse arguments
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s disk1 disk2 [FUSE options] mount_point\n", argv[0]);
        return -1;
    }

    // parse disks
    int i = 1;

    // initialize metedata for easy access
    initialMetadata(argv[1]);

    while (i < argc - 1)
    {
        if (argv[i][0] == '-')
            break;
        // else it is a disk
        // check if it is a valid image
        if (checkValidDiskfile(argv[i], diskimgs) != 0)
        {
            perror("disks entered in wfs arg not valid\n");
            return -1;
        }
        i++;
    }

    if (sb.diskNum != i - 1)
    {
        perror("Error: Not enough disks\n");
        return -1;
    }

    filter_argv(&argc, argv, i - 1);

    // for (int i = 0; i < argc; i++)
    // {
    //     printf("%s\n", argv[i]);
    // }

    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    return fuse_main(argc, argv, &ops, NULL);
}
