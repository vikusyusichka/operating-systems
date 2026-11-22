#include <string.h>

#include "fs.h"

#define DESC_PER_BLOCK (BLOCK_SIZE / sizeof(struct desc))
#define BITS_PER_BLOCK (BLOCK_SIZE * 8)
#define DIRENT_SIZE    ((uint64_t)sizeof(struct dirent))

static struct superblock sb;
static int mounted;

const struct superblock *fs_super(void)
{
    return mounted ? &sb : NULL;
}

static int block_is_zero(const uint8_t *b)
{
    int i;

    for (i = 0; i < BLOCK_SIZE; i++)
        if (b[i] != 0)
            return 0;
    return 1;
}

static void bitmap_set(uint32_t bno, int val)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t blk = sb.bitmap_start + bno / BITS_PER_BLOCK;
    uint32_t bit = bno % BITS_PER_BLOCK;

    dev_read(blk, buf);
    if (val)
        buf[bit / 8] |= (uint8_t)(1u << (bit % 8));
    else
        buf[bit / 8] &= (uint8_t)~(1u << (bit % 8));
    dev_write(blk, buf);
}

static uint32_t block_alloc(void)
{
    uint8_t buf[BLOCK_SIZE];
    static const uint8_t zero[BLOCK_SIZE];
    uint32_t i, j, bit, bno;

    for (i = 0; i < sb.bitmap_blocks; i++) {
        dev_read(sb.bitmap_start + i, buf);
        for (j = 0; j < BLOCK_SIZE; j++) {
            if (buf[j] == 0xFF)
                continue;
            for (bit = 0; bit < 8; bit++) {
                if (buf[j] & (1u << bit))
                    continue;
                bno = i * BITS_PER_BLOCK + j * 8 + bit;
                if (bno >= sb.nblocks)
                    return 0;
                buf[j] |= (uint8_t)(1u << bit);
                dev_write(sb.bitmap_start + i, buf);
                dev_write(bno, zero);
                return bno;
            }
        }
    }
    return 0;
}

static void block_free(uint32_t bno)
{
    bitmap_set(bno, 0);
}

uint32_t fs_free_blocks(void)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t bno, n = 0;

    for (bno = 0; bno < sb.nblocks; bno++) {
        if (bno % BITS_PER_BLOCK == 0)
            dev_read(sb.bitmap_start + bno / BITS_PER_BLOCK, buf);
        if (!(buf[(bno % BITS_PER_BLOCK) / 8] & (1u << (bno % 8))))
            n++;
    }
    return n;
}

int fs_mkfs(uint32_t ndesc)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t nb = dev_nblocks(), b;
    struct desc root;

    if (ndesc < 2)
        return -1;

    memset(&sb, 0, sizeof(sb));
    sb.magic = FS_MAGIC;
    sb.block_size = BLOCK_SIZE;
    sb.nblocks = nb;
    sb.ndesc = ndesc;
    sb.bitmap_start = 1;
    sb.bitmap_blocks = (nb + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    sb.desc_start = sb.bitmap_start + sb.bitmap_blocks;
    sb.desc_blocks = (uint32_t)((ndesc + DESC_PER_BLOCK - 1) / DESC_PER_BLOCK);
    sb.data_start = sb.desc_start + sb.desc_blocks;
    if (sb.data_start >= nb)
        return -1;

    memset(buf, 0, sizeof(buf));
    for (b = sb.bitmap_start; b < sb.data_start; b++)
        dev_write(b, buf);
    memcpy(buf, &sb, sizeof(sb));
    dev_write(0, buf);
    mounted = 1;

    for (b = 0; b < sb.data_start; b++)
        bitmap_set(b, 1);

    memset(&root, 0, sizeof(root));
    root.type = FT_DIR;
    root.nlink = 1;
    fs_desc_put(ROOT_INO, &root);
    return 0;
}

int fs_mount(void)
{
    uint8_t buf[BLOCK_SIZE];

    mounted = 0;
    if (dev_read(0, buf) != 0)
        return -1;
    memcpy(&sb, buf, sizeof(sb));
    if (sb.magic != FS_MAGIC || sb.block_size != BLOCK_SIZE || sb.nblocks != dev_nblocks())
        return -1;
    mounted = 1;
    return 0;
}

int fs_desc_get(uint32_t ino, struct desc *d)
{
    uint8_t buf[BLOCK_SIZE];

    if (ino >= sb.ndesc)
        return -1;
    dev_read(sb.desc_start + ino / DESC_PER_BLOCK, buf);
    memcpy(d, buf + (ino % DESC_PER_BLOCK) * sizeof(struct desc), sizeof(*d));
    return 0;
}

int fs_desc_put(uint32_t ino, const struct desc *d)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t blk = sb.desc_start + ino / DESC_PER_BLOCK;

    if (ino >= sb.ndesc)
        return -1;
    dev_read(blk, buf);
    memcpy(buf + (ino % DESC_PER_BLOCK) * sizeof(struct desc), d, sizeof(*d));
    return dev_write(blk, buf);
}

int fs_desc_alloc(int type)
{
    struct desc d;
    uint32_t ino;

    for (ino = 1; ino < sb.ndesc; ino++) {
        fs_desc_get(ino, &d);
        if (d.type != FT_NONE)
            continue;
        memset(&d, 0, sizeof(d));
        d.type = (uint16_t)type;
        fs_desc_put(ino, &d);
        return (int)ino;
    }
    return -1;
}

static uint32_t bmap_get(const struct desc *d, uint32_t idx)
{
    uint32_t ind[NINDIRECT];

    if (idx < NDIRECT)
        return d->direct[idx];
    if (d->indirect == 0)
        return 0;
    dev_read(d->indirect, ind);
    return ind[idx - NDIRECT];
}

uint32_t fs_bmap(const struct desc *d, uint32_t idx)
{
    return bmap_get(d, idx);
}

static int bmap_set(struct desc *d, uint32_t idx, uint32_t bno)
{
    uint32_t ind[NINDIRECT];

    if (idx < NDIRECT) {
        d->direct[idx] = bno;
        return 0;
    }
    if (d->indirect == 0) {
        if (bno == 0)
            return 0;
        d->indirect = block_alloc();
        if (d->indirect == 0)
            return -1;
    }
    dev_read(d->indirect, ind);
    ind[idx - NDIRECT] = bno;
    if (block_is_zero((const uint8_t *)ind)) {
        block_free(d->indirect);
        d->indirect = 0;
    } else {
        dev_write(d->indirect, ind);
    }
    return 0;
}

uint32_t fs_count_blocks(const struct desc *d, uint32_t *indirect_used)
{
    uint32_t i, n = 0;

    for (i = 0; i < MAX_FILE_BLOCKS; i++)
        if (bmap_get(d, i) != 0)
            n++;
    *indirect_used = d->indirect != 0;
    return n;
}

long fs_read(uint32_t ino, uint64_t off, void *buf, uint64_t n)
{
    struct desc d;
    uint8_t blk[BLOCK_SIZE];
    uint64_t done = 0, chunk;
    uint32_t idx, boff, bno;

    if (fs_desc_get(ino, &d) != 0 || off >= d.size)
        return 0;
    if (n > d.size - off)
        n = d.size - off;

    while (done < n) {
        idx = (uint32_t)((off + done) / BLOCK_SIZE);
        boff = (uint32_t)((off + done) % BLOCK_SIZE);
        chunk = BLOCK_SIZE - boff;
        if (chunk > n - done)
            chunk = n - done;

        bno = bmap_get(&d, idx);
        if (bno == 0)
            memset(blk, 0, BLOCK_SIZE);
        else
            dev_read(bno, blk);
        memcpy((uint8_t *)buf + done, blk + boff, chunk);
        done += chunk;
    }
    return (long)done;
}

static int chunk_is_zero(const uint8_t *p, uint64_t n)
{
    uint64_t i;

    for (i = 0; i < n; i++)
        if (p[i] != 0)
            return 0;
    return 1;
}

long fs_write(uint32_t ino, uint64_t off, const void *buf, uint64_t n)
{
    struct desc d;
    uint8_t blk[BLOCK_SIZE];
    const uint8_t *src = buf;
    uint64_t done = 0, chunk, max = (uint64_t)MAX_FILE_BLOCKS * BLOCK_SIZE;
    uint32_t idx, boff, bno;

    if (fs_desc_get(ino, &d) != 0)
        return -1;
    if (off >= max)
        return 0;
    if (n > max - off)
        n = max - off;

    while (done < n) {
        idx = (uint32_t)((off + done) / BLOCK_SIZE);
        boff = (uint32_t)((off + done) % BLOCK_SIZE);
        chunk = BLOCK_SIZE - boff;
        if (chunk > n - done)
            chunk = n - done;

        bno = bmap_get(&d, idx);
        if (bno == 0) {
            if (!chunk_is_zero(src + done, chunk)) {
                bno = block_alloc();
                if (bno == 0)
                    break;
                if (bmap_set(&d, idx, bno) != 0) {
                    block_free(bno);
                    break;
                }
                memset(blk, 0, BLOCK_SIZE);
                memcpy(blk + boff, src + done, chunk);
                dev_write(bno, blk);
            }
        } else {
            dev_read(bno, blk);
            memcpy(blk + boff, src + done, chunk);
            if (block_is_zero(blk)) {
                block_free(bno);
                bmap_set(&d, idx, 0);
            } else {
                dev_write(bno, blk);
            }
        }
        done += chunk;
    }

    if (off + done > d.size)
        d.size = off + done;
    fs_desc_put(ino, &d);
    return (long)done;
}

int fs_truncate(uint32_t ino, uint64_t size)
{
    struct desc d;
    uint8_t blk[BLOCK_SIZE];
    uint32_t idx, first, last, bno, boff;

    if (fs_desc_get(ino, &d) != 0)
        return -1;
    if (size > (uint64_t)MAX_FILE_BLOCKS * BLOCK_SIZE)
        return -1;

    if (size < d.size) {
        first = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);
        last = (uint32_t)((d.size - 1) / BLOCK_SIZE);
        for (idx = first; idx <= last; idx++) {
            bno = bmap_get(&d, idx);
            if (bno != 0) {
                block_free(bno);
                bmap_set(&d, idx, 0);
            }
        }
        boff = (uint32_t)(size % BLOCK_SIZE);
        if (boff != 0) {
            idx = (uint32_t)(size / BLOCK_SIZE);
            bno = bmap_get(&d, idx);
            if (bno != 0) {
                dev_read(bno, blk);
                memset(blk + boff, 0, BLOCK_SIZE - boff);
                if (block_is_zero(blk)) {
                    block_free(bno);
                    bmap_set(&d, idx, 0);
                } else {
                    dev_write(bno, blk);
                }
            }
        }
    }
    d.size = size;
    return fs_desc_put(ino, &d);
}

void fs_free_file(uint32_t ino)
{
    struct desc d;

    fs_truncate(ino, 0);
    fs_desc_get(ino, &d);
    memset(&d, 0, sizeof(d));
    fs_desc_put(ino, &d);
}

int fs_name_valid(const char *name)
{
    size_t i, len = strlen(name);

    if (len == 0 || len > NAME_MAX_LEN)
        return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= ' ' || c > '~' || c == '/')
            return 0;
    }
    return 1;
}

static long dir_find(const char *name, struct dirent *out)
{
    struct desc root;
    struct dirent e;
    uint64_t off;

    fs_desc_get(ROOT_INO, &root);
    for (off = 0; off < root.size; off += DIRENT_SIZE) {
        fs_read(ROOT_INO, off, &e, DIRENT_SIZE);
        if (e.ino != 0 && strncmp(e.name, name, NAME_MAX_LEN + 1) == 0) {
            if (out != NULL)
                *out = e;
            return (long)off;
        }
    }
    return -1;
}

int fs_dir_lookup(const char *name)
{
    struct dirent e;

    if (dir_find(name, &e) < 0)
        return -1;
    return (int)e.ino;
}

int fs_dir_add(const char *name, uint32_t ino)
{
    struct desc root;
    struct dirent e;
    uint64_t off;

    fs_desc_get(ROOT_INO, &root);
    for (off = 0; off < root.size; off += DIRENT_SIZE) {
        fs_read(ROOT_INO, off, &e, DIRENT_SIZE);
        if (e.ino == 0)
            break;
    }

    memset(&e, 0, sizeof(e));
    strncpy(e.name, name, NAME_MAX_LEN);
    e.ino = ino;
    return fs_write(ROOT_INO, off, &e, DIRENT_SIZE) == (long)DIRENT_SIZE ? 0 : -1;
}

int fs_dir_remove(const char *name)
{
    struct desc root;
    struct dirent e;
    long off = dir_find(name, NULL);

    if (off < 0)
        return -1;

    memset(&e, 0, sizeof(e));
    fs_write(ROOT_INO, (uint64_t)off, &e, DIRENT_SIZE);

    fs_desc_get(ROOT_INO, &root);
    while (root.size >= DIRENT_SIZE) {
        fs_read(ROOT_INO, root.size - DIRENT_SIZE, &e, DIRENT_SIZE);
        if (e.ino != 0)
            break;
        root.size -= DIRENT_SIZE;
    }
    return fs_truncate(ROOT_INO, root.size);
}

void fs_dir_list(void (*cb)(const char *name, uint32_t ino))
{
    struct desc root;
    struct dirent e;
    uint64_t off;

    fs_desc_get(ROOT_INO, &root);
    for (off = 0; off < root.size; off += DIRENT_SIZE) {
        fs_read(ROOT_INO, off, &e, DIRENT_SIZE);
        if (e.ino != 0)
            cb(e.name, e.ino);
    }
}
