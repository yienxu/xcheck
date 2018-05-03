#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


#define assert(cond, msg)  if (!(cond)) { fprintf(stderr, msg); exit(1); }

#define BSIZE    (512)   // block size
#define NDIRECT  (12)
#define DIRSIZE  (14)
#define T_UNUSED (0)
#define T_DIR    (1)     // Directory
#define T_FILE   (2)     // File
#define T_DEV    (3)     // Special device

typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

typedef struct {
    uint size;     // Size of file system image (blocks)
    uint nblocks;  // Number of data blocks
    uint ninodes;  // Number of inodes.
} Superblock;

typedef struct {
    short type;   // File type
    short major;  // Major device number (T_DEV only)
    short minor;  // Minor device number (T_DEV only)
    short nlink;  // Number of links to inode in file system
    uint size;  // Size of file (bytes)
    uint addrs[NDIRECT + 1];  // Data block addresses
} Inode;

typedef struct {
    ushort inum;
    char name[DIRSIZE];
} Dirent;

// Inodes per block.
#define IPB                 (BSIZE / sizeof(Inode))
// Block containing inode i
#define IBLOCK(i)           ((i) / IPB + 2)
// Bitmap bits per block
#define BPB                 (BSIZE * 8)
// Block containing bit for block b
#define BBLOCK(b, ninodes)  ((b)/BPB + (ninodes)/IPB + 3)

void *imgptr;
Superblock sb;
uint INODE_START;
uint INODE_END;
uint DATA_START;
uint DATA_END;

void *get_addr(uint blknum) {
    return imgptr + blknum * BSIZE;
}

Inode get_inode(ushort inum) {
    void *block = get_addr(INODE_START + inum / IPB);
    Inode inode = ((Inode *) block)[inum % IPB];
    return inode;
}

int is_block_used(uint blknum) {
    uchar *bitmap = get_addr(BBLOCK(blknum, sb.ninodes));
    blknum %= BSIZE * 8;
    uint loc = blknum / 8;
    uint offset = blknum % 8;
    return (bitmap[loc] & (1 << offset)) != 0;
}

int main(int argc, char *argv[]) {
    assert(argc == 2, "Usage: gimg <file_system_image>\n");

    int ret;
    int fd;
    struct stat filestat;

    fd = open(argv[1], O_RDONLY);
    assert(fd != -1, "image not found.\n");

    ret = fstat(fd, &filestat);
    assert(ret == 0, "error: fstat()\n");

    uint filesize = filestat.st_size;
    imgptr = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(imgptr != MAP_FAILED, "error: mmap()\n");

    sb = *(Superblock *) get_addr(1);

    INODE_START = 2;
    INODE_END = 2 + sb.ninodes / IPB;
    DATA_START = BBLOCK(sb.size - 1, sb.ninodes) + 1;
    DATA_END = sb.size;

    ret = munmap(imgptr, filesize);
    assert(ret == 0, "error: munmap()\n");

    return 0;
}
