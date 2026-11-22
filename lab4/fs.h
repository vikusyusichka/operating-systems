#ifndef FS_H
#define FS_H

#include <stdint.h>

#include "device.h"

#define FS_MAGIC     0x34534C46u
#define NDIRECT      10
#define NINDIRECT    (BLOCK_SIZE / 4)
#define MAX_FILE_BLOCKS (NDIRECT + NINDIRECT)
#define NAME_MAX_LEN 27
#define ROOT_INO     0

enum { FT_NONE = 0, FT_REG = 1, FT_DIR = 2 };

struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t nblocks;
    uint32_t ndesc;
    uint32_t bitmap_start, bitmap_blocks;
    uint32_t desc_start, desc_blocks;
    uint32_t data_start;
};

struct desc {
    uint16_t type;
    uint16_t nlink;
    uint32_t reserved;
    uint64_t size;
    uint32_t direct[NDIRECT];
    uint32_t indirect;
    uint32_t pad;
};

struct dirent {
    char name[NAME_MAX_LEN + 1];
    uint32_t ino;
};

int fs_mkfs(uint32_t ndesc);
int fs_mount(void);
const struct superblock *fs_super(void);

int fs_desc_alloc(int type);
int fs_desc_get(uint32_t ino, struct desc *d);
int fs_desc_put(uint32_t ino, const struct desc *d);
void fs_free_file(uint32_t ino);

long fs_read(uint32_t ino, uint64_t off, void *buf, uint64_t n);
long fs_write(uint32_t ino, uint64_t off, const void *buf, uint64_t n);
int fs_truncate(uint32_t ino, uint64_t size);
uint32_t fs_count_blocks(const struct desc *d, uint32_t *indirect_used);
uint32_t fs_bmap(const struct desc *d, uint32_t idx);

int fs_name_valid(const char *name);
int fs_dir_lookup(const char *name);
int fs_dir_add(const char *name, uint32_t ino);
int fs_dir_remove(const char *name);
void fs_dir_list(void (*cb)(const char *name, uint32_t ino));

uint32_t fs_free_blocks(void);

#endif
