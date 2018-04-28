#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


#define assert(cond, msg)  if (!(cond)) { fprintf(stderr, msg); exit(1); }

#define BSIZE    (512)   // block size
#define NDIRECT  (12)
#define DIRSIZE  (14)
#define ROOTINO  (1)     // root i-number
#define T_UNUSED (0)
#define T_DIR    (1)     // Directory
#define T_FILE   (2)     // File
#define T_DEV    (3)     // Special device

typedef unsigned int uint;
typedef unsigned short ushort;

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
} Direct;

// Inodes per block.
#define IPB                 (BSIZE / sizeof(Inode))
// Block containing inode i
#define IBLOCK(i)           ((i) / IPB + 2)
// Bitmap bits per block
#define BPB                 (BSIZE * 8)
// Block containing bit for block b
#define BBLOCK(b, ninodes)  (b/BPB + (ninodes)/IPB + 3)


void *imgptr;
Superblock sb;

void *get_addr(void *imgptr, uint i) {
    return imgptr + i * BSIZE;
}

void check_bad_inode() {
    const int INODE_START = 2;
    const int INODE_END = 2 + sb.ninodes / IPB;
    for (int i = INODE_START; i < INODE_END; i++) {
        void *block = get_addr(imgptr, i);
        for (int j = 0; j < IPB; j++) {
            Inode inode = ((Inode *) block)[j];
            assert(!(inode.type == T_UNUSED || inode.type == T_DIR
                     || inode.type == T_DEV || inode.type == T_FILE),
                   "ERROR: bad inode.\n");
        }
    }
}

void check_root_dir() {
    void *block = get_addr(imgptr, 3);
    Inode root = ((Inode *)block)[0];
}

int main(int argc, char *argv[]) {
    assert(argc == 2, "Usage: xcheck <file_system_image>\n");

    int ret;
    int fd;
    struct stat filestat;

    fd = open(argv[1], O_RDONLY);
    assert(fd != -1, "image not found.\n");

    ret = fstat(fd, &filestat);
    assert(ret == 0, "error: fstat()\n");

    uint filesize = filestat.st_size;
    printf("filesize = %u\n", filesize);

    imgptr = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(imgptr != MAP_FAILED, "error: mmap()\n");

    sb = *(Superblock *) get_addr(imgptr, 1);
    printf("size=%u, nblocks=%u, ninodes=%u\n",
           sb.size, sb.nblocks, sb.ninodes);

    check_bad_inode();
    // TODO: task 2
    check_root_dir();
}
