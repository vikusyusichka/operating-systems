#include <string.h>

#include "fs.h"
#include "kernel.h"

struct open_file {
    int used;
    uint32_t ino;
    uint64_t offset;
};

static struct open_file fds[MAX_FD];
static const char *err = "";

const char *k_error(void)
{
    return err;
}

static int fail(const char *msg)
{
    err = msg;
    return -1;
}

static int is_open(uint32_t ino)
{
    int i;

    for (i = 0; i < MAX_FD; i++)
        if (fds[i].used && fds[i].ino == ino)
            return 1;
    return 0;
}

static void maybe_release(uint32_t ino)
{
    struct desc d;

    fs_desc_get(ino, &d);
    if (d.nlink == 0 && !is_open(ino))
        fs_free_file(ino);
}

static int check_fd(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !fds[fd].used)
        return fail("bad file descriptor");
    return 0;
}

int k_mkfs(uint32_t ndesc)
{
    memset(fds, 0, sizeof(fds));
    if (fs_mkfs(ndesc) != 0)
        return fail("cannot create file system with this number of descriptors");
    return 0;
}

int k_create(const char *name)
{
    struct desc d;
    int ino;

    if (!fs_name_valid(name))
        return fail("invalid file name");
    if (fs_dir_lookup(name) >= 0)
        return fail("file exists");
    ino = fs_desc_alloc(FT_REG);
    if (ino < 0)
        return fail("no free file descriptors in file system");
    if (fs_dir_add(name, (uint32_t)ino) != 0) {
        fs_free_file((uint32_t)ino);
        return fail("no space for directory entry");
    }
    fs_desc_get((uint32_t)ino, &d);
    d.nlink = 1;
    fs_desc_put((uint32_t)ino, &d);
    return 0;
}

int k_open(const char *name)
{
    struct desc d;
    int ino, fd;

    ino = fs_dir_lookup(name);
    if (ino < 0)
        return fail("no such file");
    fs_desc_get((uint32_t)ino, &d);
    if (d.type != FT_REG)
        return fail("not a regular file");

    for (fd = 0; fd < MAX_FD; fd++) {
        if (!fds[fd].used) {
            fds[fd].used = 1;
            fds[fd].ino = (uint32_t)ino;
            fds[fd].offset = 0;
            return fd;
        }
    }
    return fail("too many open files");
}

int k_close(int fd)
{
    uint32_t ino;

    if (check_fd(fd) != 0)
        return -1;
    ino = fds[fd].ino;
    fds[fd].used = 0;
    maybe_release(ino);
    return 0;
}

int k_seek(int fd, uint64_t off)
{
    if (check_fd(fd) != 0)
        return -1;
    fds[fd].offset = off;
    return 0;
}

long k_read(int fd, void *buf, uint64_t n)
{
    long r;

    if (check_fd(fd) != 0)
        return -1;
    r = fs_read(fds[fd].ino, fds[fd].offset, buf, n);
    fds[fd].offset += (uint64_t)r;
    return r;
}

long k_write(int fd, const void *buf, uint64_t n)
{
    long r;

    if (check_fd(fd) != 0)
        return -1;
    r = fs_write(fds[fd].ino, fds[fd].offset, buf, n);
    if (r < 0)
        return fail("write error");
    fds[fd].offset += (uint64_t)r;
    if ((uint64_t)r < n)
        fail("file system full or maximum file size reached");
    return r;
}

int k_link(const char *name1, const char *name2)
{
    struct desc d;
    int ino;

    ino = fs_dir_lookup(name1);
    if (ino < 0)
        return fail("no such file");
    if (!fs_name_valid(name2))
        return fail("invalid file name");
    if (fs_dir_lookup(name2) >= 0)
        return fail("file exists");
    if (fs_dir_add(name2, (uint32_t)ino) != 0)
        return fail("no space for directory entry");
    fs_desc_get((uint32_t)ino, &d);
    d.nlink++;
    fs_desc_put((uint32_t)ino, &d);
    return 0;
}

int k_unlink(const char *name)
{
    struct desc d;
    int ino;

    ino = fs_dir_lookup(name);
    if (ino < 0)
        return fail("no such file");
    fs_dir_remove(name);
    fs_desc_get((uint32_t)ino, &d);
    d.nlink--;
    fs_desc_put((uint32_t)ino, &d);
    maybe_release((uint32_t)ino);
    return 0;
}

int k_truncate(const char *name, uint64_t size)
{
    int ino = fs_dir_lookup(name);

    if (ino < 0)
        return fail("no such file");
    if (fs_truncate((uint32_t)ino, size) != 0)
        return fail("size is larger than maximum file size");
    return 0;
}
