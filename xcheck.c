#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


#define assert(cond, msg)  if (!(cond)) { fprintf(stderr, msg); exit(1); }
#define min(a, b) ((a) < (b) ? (a) : (b))

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
// Number of blocks the file is using
#define BUSED(i)            ((i).size / BSIZE + 1)
// Iterate size: minimum of the blocks and NDIRECT nodes
#define ITERSIZE(i)         (min(BUSED(i), NDIRECT))

void *imgptr;
Superblock sb;
uint INODE_START;
uint INODE_END;
uint DATA_START;
uint DATA_END;

void *get_addr(uint blknum) {
    return imgptr + blknum * BSIZE;
}

uint is_block_used(uint blknum) {
    uchar *bitmap = get_addr(BBLOCK(blknum, sb.ninodes));
    uint loc = blknum / 8;
    uint offset = blknum % 8;
    return (bitmap[loc] & (1 << offset)) != 0;
}

void check_bad_inode() {
    for (int i = INODE_START; i < INODE_END; i++) {
        void *block = get_addr(i);
        for (int j = 0; j < IPB; j++) {
            Inode inode = ((Inode *) block)[j];
//            uint selfinum = j + IPB * (i - INODE_START);
            // No.1
            assert(inode.type == T_UNUSED || inode.type == T_DIR
                   || inode.type == T_DEV || inode.type == T_FILE,
                   "ERROR: bad inode.\n");
            // No.2 - Direct
            if (inode.type == T_UNUSED) {
                continue;
            }
            for (int b = 0; b < ITERSIZE(inode); b++) {
                uint blknum = inode.addrs[b];
                if (blknum == 0) {
                    continue;
                }
                assert(blknum < sb.size && blknum >= DATA_START,
                       "ERROR: bad direct address in inode.\n");
                // No.5 Direct
                assert(is_block_used(blknum) == 1,
                       "ERROR: address used by inode but marked free in bitmap.\n");
            }
            // No.2 - Indirect
            uint indblknum = inode.addrs[NDIRECT];
            if (indblknum == 0) {
                continue;
            }
            uint *indblkptr = (uint *) get_addr(indblknum);
            for (int b = 0; b < BSIZE / sizeof(uint) && b < BUSED(inode); b++) {
                uint blknum = indblkptr[b];
                if (blknum == 0) {
                    continue;
                }
                assert(blknum < sb.size && blknum >= DATA_START,
                       "ERROR: bad indirect address in inode.\n");
                // No.5 Indirect
                assert(is_block_used(blknum) == 1,
                       "ERROR: address used by inode but marked free in bitmap.\n");
            }
        }
    }
}

void check_inode_dir_ref() {
    uint ref_counts[sb.ninodes];
    uint use_counts[sb.ninodes];
  //  uint link_counts[sb.ninodes];
    for (uint i = 0; i < sb.ninodes; i++) {
        ref_counts[i] = 0;
        use_counts[i] = 0;
        //link_counts[i] = 0;
    }

    // No.9
    for (int i = INODE_START; i < INODE_END; i++) {
        void *block = get_addr(i);
        for (int j = 0; j < IPB; j++) {
            Inode inode = ((Inode *) block)[j];
            uint selfinum = j + IPB * (i - INODE_START);
            if (inode.type == T_UNUSED) {

                continue;
            }
            use_counts[selfinum] = 1;
          //  printf("inum %d, nlink %d", selfinum, inode.nlink);
            if (inode.type != T_DIR) {
                continue;
            }
            // now inode is a dir
            for (int b = 0; b < ITERSIZE(inode); b++) {
                uint blknum = inode.addrs[b];
                if (blknum == 0) {
                    continue;
                }
                Dirent *dirents = get_addr(blknum);
                for (int ndir = 0; ndir < BSIZE / sizeof(Dirent); ndir++) {
                    Dirent r = dirents[ndir];
                    ref_counts[r.inum]++;
                }
            }
            uint indblknum = inode.addrs[NDIRECT];
            if (indblknum == 0) {
                continue;
            }
            uint *indblkptr = (uint *) get_addr(indblknum);
            for (int b = 0; b < BSIZE / sizeof(uint) && b < BUSED(inode); b++) {
                uint blknum = indblkptr[b];
                if (blknum == 0) {
                    continue;
                }
                Dirent *dirents = get_addr(blknum);
                for (int ndir = 0; ndir < BSIZE / sizeof(Dirent); ndir++) {
                    Dirent r = dirents[ndir];
                    ref_counts[r.inum]++;
                }
            }
        }
    }

    for (uint i = 1; i < sb.ninodes; i++) {
        if (use_counts[i] != 0) {
            assert(ref_counts[i] >= 1,
                   "ERROR: inode marked use but not found in a directory.\n");
        }
        if (ref_counts[i] >= 1) {
            assert(use_counts[i] != 0,
                   "ERROR: inode referred to in directory but marked free.\n");
        }

    }
}

void check_bad_data() {
    // No.6
    for (uint theb = DATA_START; theb < DATA_END; theb++) {
        uint theblknum = theb;
        if (!is_block_used(theblknum)) {
            continue;
        }
        int matched = 0;
        for (int i = INODE_START; i < INODE_END && !matched; i++) {
            void *block = get_addr(i);
            for (int j = 0; j < IPB; j++) {
                Inode inode = ((Inode *) block)[j];
                if (inode.type == T_UNUSED) {
                    continue;
                }
                for (int b = 0; b <= NDIRECT; b++) {
                    uint blknum = inode.addrs[b];
                    if (theblknum == blknum) {
                        matched = 1;
                        break;
                    }
                }
                uint indblknum = inode.addrs[NDIRECT];
                if (indblknum == 0) {
                    continue;
                }
                uint *indblkptr = (uint *) get_addr(indblknum);
                for (int b = 0; b < BSIZE / sizeof(uint); b++) {
                    uint blknum = indblkptr[b];
                    if (theblknum == blknum) {
                        matched = 1;
                        break;
                    }
                }
            }
        }
        assert(matched,
               "ERROR: bitmap marks block in use but it is not in use.\n");
    }
}

void check_root_dir() {
    // No.3
    void *block = get_addr(INODE_START);
    Inode inode = ((Inode *) block)[1];
    assert(inode.type == T_DIR, "ERROR: root directory does not exist.\n");

    block = get_addr(inode.addrs[0]);
    Dirent *dirents = (Dirent *) block;
    assert(dirents[0].inum == 1 && dirents[1].inum == 1 &&
           strcmp(dirents[0].name, ".") == 0 &&
           strcmp(dirents[1].name, "..") == 0,
           "ERROR: root directory does not exist.\n");
}

void check_dir() {
    for (int i = INODE_START; i < INODE_END; i++) {
        void *block = get_addr(i);
        for (int j = 0; j < IPB; j++) {
            Inode inode = ((Inode *) block)[j];
            uint selfinum = j + IPB * (i - INODE_START);
            if (inode.type != T_DIR) {
                continue;
            }
            Dirent *dirents = (Dirent *) get_addr(inode.addrs[0]);
            Dirent selfdir = dirents[0];
            assert(selfdir.inum == selfinum && strcmp(selfdir.name, ".") == 0,
                   "ERROR: directory not properly formatted.\n");
            Dirent prntdir = dirents[1];
            assert(prntdir.inum != 0 && strcmp(prntdir.name, "..") == 0,
                   "ERROR: directory not properly formatted.\n");
        }
    }
}

int compar(const void *e1, const void *e2) {
    uint a = *(uint *) e1;
    uint b = *(uint *) e2;
    return b - a;
}

void check_addr_usage() {
    // No.7 & 8
    uint dir_addrs[sb.size];
    uint dirptr = 0;
    uint ind_addrs[sb.size];
    uint indptr = 0;
    for (int i = 0; i < sb.size; i++) {
        dir_addrs[i] = 0;
        ind_addrs[i] = 0;
    }

    for (int i = INODE_START; i <= INODE_END; i++) {
        void *block = get_addr(i);
        for (int j = 0; j < IPB; j++) {
            Inode inode = ((Inode *) block)[j];
            if (inode.type == T_UNUSED) {
                continue;
            }
            for (int b = 0; b <= ITERSIZE(inode); b++) {
                uint blknum = inode.addrs[b];
                if (blknum != 0) {
                    dir_addrs[dirptr++] = blknum;
                }
            }
            uint indblknum = inode.addrs[NDIRECT];
            if (indblknum == 0) {
                continue;
            }
            uint *indblkptr = (uint *) get_addr(indblknum);
            for (int b = 0; b < BSIZE / sizeof(uint) && b < BUSED(inode); b++) {
                uint blknum = indblkptr[b];
                if (blknum != 0) {
                    ind_addrs[indptr++] = blknum;
                }
            }
        }
    }

    qsort(&dir_addrs[0], sb.size, sizeof(uint), compar);
    qsort(&ind_addrs[0], sb.size, sizeof(uint), compar);

    for (int i = 0; i < sb.size - 1; i++) {
        if (dir_addrs[i] == 0 || dir_addrs[i] != dir_addrs[i + 1]) {
            continue;
        }
        assert(0, "ERROR: direct address used more than once.\n");
    }
    for (int i = 0; i < sb.size - 1; i++) {
        if (ind_addrs[i] == 0 || ind_addrs[i] != ind_addrs[i + 1]) {
            continue;
        }
        assert(0, "ERROR: indirect address used more than once.\n");
    }
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
    imgptr = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(imgptr != MAP_FAILED, "error: mmap()\n");

    sb = *(Superblock *) get_addr(1);
//    printf("size=%u, nblocks=%u, ninodes=%u\n\n",
//           sb.size, sb.nblocks, sb.ninodes);

    INODE_START = 2;
    INODE_END = 2 + sb.ninodes / IPB;
    DATA_START = BBLOCK(sb.size - 1, sb.ninodes) + 1;
    DATA_END = sb.size;

    check_bad_inode();
    check_bad_data();
    check_dir();
    check_root_dir();
    check_addr_usage();
    check_inode_dir_ref();

    // freeing...
    ret = munmap(imgptr, filesize);
    assert(ret == 0, "error: munmap()\n");

    return 0;
}
