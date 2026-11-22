#include <string.h>

#include "fs.h"
#include "kernel.h"

struct open_file {
    int used;
    uint32_t ino;
    uint64_t offset;
};

struct lookup {
    int ino;
    uint32_t parent;
    char name[NAME_MAX_LEN + 1];
};

static struct open_file fds[MAX_FD];
static uint32_t cwd = ROOT_INO;
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

void k_init(void)
{
    memset(fds, 0, sizeof(fds));
    cwd = ROOT_INO;
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

static int file_type(uint32_t ino)
{
    struct desc d;

    fs_desc_get(ino, &d);
    return d.type;
}

static int namei(const char *path, int follow_last, struct lookup *res)
{
    char buf[MAX_PATH], tmp[MAX_PATH], target[BLOCK_SIZE + 1];
    char comp[NAME_MAX_LEN + 1];
    const char *p, *rest, *q;
    uint32_t cur;
    int ino, links = 0, last;
    size_t len;
    long n;

    if (path[0] == 0)
        return fail("empty path");
    if (strlen(path) >= MAX_PATH)
        return fail("path too long");
    strcpy(buf, path);
    cur = buf[0] == '/' ? ROOT_INO : cwd;
    p = buf;

    for (;;) {
        while (*p == '/')
            p++;
        if (*p == 0) {
            res->ino = (int)cur;
            res->parent = cur;
            res->name[0] = 0;
            return 0;
        }

        rest = strchr(p, '/');
        if (rest == NULL)
            rest = p + strlen(p);
        len = (size_t)(rest - p);
        if (len > NAME_MAX_LEN)
            return fail("file name too long");
        memcpy(comp, p, len);
        comp[len] = 0;

        for (q = rest; *q == '/'; q++)
            ;
        last = *q == 0;

        ino = fs_dir_lookup(cur, comp);
        if (ino < 0) {
            if (!last)
                return fail("no such file or directory");
            res->ino = -1;
            res->parent = cur;
            strcpy(res->name, comp);
            return 0;
        }

        if (file_type((uint32_t)ino) == FT_SYMLINK && (!last || follow_last)) {
            if (++links > MAX_SYMLINK_FOLLOW)
                return fail("too many levels of symbolic links");
            n = fs_read((uint32_t)ino, 0, target, BLOCK_SIZE);
            target[n] = 0;
            if (strlen(target) + strlen(rest) >= MAX_PATH)
                return fail("path too long");
            strcpy(tmp, target);
            strcat(tmp, rest);
            strcpy(buf, tmp);
            p = buf;
            if (buf[0] == '/')
                cur = ROOT_INO;
            continue;
        }

        if (last) {
            res->ino = ino;
            res->parent = cur;
            strcpy(res->name, comp);
            return 0;
        }
        if (file_type((uint32_t)ino) != FT_DIR)
            return fail("not a directory");
        cur = (uint32_t)ino;
        p = rest;
    }
}

static int new_name(const char *path, struct lookup *r)
{
    if (namei(path, 0, r) != 0)
        return -1;
    if (r->ino >= 0)
        return fail("file exists");
    if (!fs_name_valid(r->name))
        return fail("invalid file name");
    return 0;
}

int k_lookup(const char *path, int follow_last)
{
    struct lookup r;

    if (namei(path, follow_last, &r) != 0)
        return -1;
    if (r.ino < 0)
        return fail("no such file or directory");
    return r.ino;
}

static int check_fd(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !fds[fd].used)
        return fail("bad file descriptor");
    return 0;
}

int k_mkfs(uint32_t ndesc)
{
    k_init();
    if (fs_mkfs(ndesc) != 0)
        return fail("cannot create file system with this number of descriptors");
    return 0;
}

int k_create(const char *path)
{
    struct lookup r;
    struct desc d;
    int ino;

    if (new_name(path, &r) != 0)
        return -1;
    ino = fs_desc_alloc(FT_REG);
    if (ino < 0)
        return fail("no free file descriptors in file system");
    if (fs_dir_add(r.parent, r.name, (uint32_t)ino) != 0) {
        fs_free_file((uint32_t)ino);
        return fail("no space for directory entry");
    }
    fs_desc_get((uint32_t)ino, &d);
    d.nlink = 1;
    fs_desc_put((uint32_t)ino, &d);
    return 0;
}

int k_open(const char *path)
{
    int ino, fd;

    ino = k_lookup(path, 1);
    if (ino < 0)
        return -1;
    if (file_type((uint32_t)ino) != FT_REG)
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

int k_link(const char *path1, const char *path2)
{
    struct lookup r;
    struct desc d;
    int ino;

    ino = k_lookup(path1, 0);
    if (ino < 0)
        return -1;
    if (file_type((uint32_t)ino) == FT_DIR)
        return fail("hard links to directories are not allowed");
    if (new_name(path2, &r) != 0)
        return -1;
    if (fs_dir_add(r.parent, r.name, (uint32_t)ino) != 0)
        return fail("no space for directory entry");
    fs_desc_get((uint32_t)ino, &d);
    d.nlink++;
    fs_desc_put((uint32_t)ino, &d);
    return 0;
}

int k_unlink(const char *path)
{
    struct lookup r;
    struct desc d;

    if (namei(path, 0, &r) != 0)
        return -1;
    if (r.ino < 0)
        return fail("no such file or directory");
    if (file_type((uint32_t)r.ino) == FT_DIR)
        return fail("is a directory, use rmdir");
    fs_dir_remove(r.parent, r.name);
    fs_desc_get((uint32_t)r.ino, &d);
    d.nlink--;
    fs_desc_put((uint32_t)r.ino, &d);
    maybe_release((uint32_t)r.ino);
    return 0;
}

int k_truncate(const char *path, uint64_t size)
{
    int ino = k_lookup(path, 1);

    if (ino < 0)
        return -1;
    if (file_type((uint32_t)ino) != FT_REG)
        return fail("not a regular file");
    if (fs_truncate((uint32_t)ino, size) != 0)
        return fail("size is larger than maximum file size");
    return 0;
}

int k_mkdir(const char *path)
{
    struct lookup r;
    struct desc d;
    int ino;

    if (new_name(path, &r) != 0)
        return -1;
    ino = fs_desc_alloc(FT_DIR);
    if (ino < 0)
        return fail("no free file descriptors in file system");

    if (fs_dir_add((uint32_t)ino, ".", (uint32_t)ino) != 0 ||
        fs_dir_add((uint32_t)ino, "..", r.parent) != 0 ||
        fs_dir_add(r.parent, r.name, (uint32_t)ino) != 0) {
        fs_free_file((uint32_t)ino);
        return fail("no space on device");
    }

    fs_desc_get((uint32_t)ino, &d);
    d.nlink = 2;
    fs_desc_put((uint32_t)ino, &d);
    fs_desc_get(r.parent, &d);
    d.nlink++;
    fs_desc_put(r.parent, &d);
    return 0;
}

int k_rmdir(const char *path)
{
    struct lookup r;
    struct desc d;

    if (namei(path, 0, &r) != 0)
        return -1;
    if (r.ino < 0)
        return fail("no such file or directory");
    if (file_type((uint32_t)r.ino) != FT_DIR)
        return fail("not a directory");
    if (r.ino == ROOT_INO)
        return fail("cannot remove root directory");
    if (strcmp(r.name, ".") == 0 || strcmp(r.name, "..") == 0 || r.name[0] == 0)
        return fail("invalid argument");
    if ((uint32_t)r.ino == cwd)
        return fail("directory is the current working directory");
    if (!fs_dir_is_empty((uint32_t)r.ino))
        return fail("directory not empty");

    fs_dir_remove(r.parent, r.name);
    fs_desc_get(r.parent, &d);
    d.nlink--;
    fs_desc_put(r.parent, &d);
    fs_free_file((uint32_t)r.ino);
    return 0;
}

int k_cd(const char *path)
{
    int ino = k_lookup(path, 1);

    if (ino < 0)
        return -1;
    if (file_type((uint32_t)ino) != FT_DIR)
        return fail("not a directory");
    cwd = (uint32_t)ino;
    return 0;
}

int k_symlink(const char *str, const char *path)
{
    struct lookup r;
    struct desc d;
    size_t len = strlen(str);
    int ino;

    if (len == 0 || len > BLOCK_SIZE)
        return fail("symlink content must be 1..512 bytes");
    if (new_name(path, &r) != 0)
        return -1;
    ino = fs_desc_alloc(FT_SYMLINK);
    if (ino < 0)
        return fail("no free file descriptors in file system");
    if (fs_write((uint32_t)ino, 0, str, len) != (long)len ||
        fs_dir_add(r.parent, r.name, (uint32_t)ino) != 0) {
        fs_free_file((uint32_t)ino);
        return fail("no space on device");
    }
    fs_desc_get((uint32_t)ino, &d);
    d.nlink = 1;
    fs_desc_put((uint32_t)ino, &d);
    return 0;
}

int k_readlink(uint32_t ino, char *buf)
{
    long n;

    if (file_type(ino) != FT_SYMLINK)
        return fail("not a symbolic link");
    n = fs_read(ino, 0, buf, BLOCK_SIZE);
    buf[n] = 0;
    return 0;
}

int k_getcwd(char *buf)
{
    char path[MAX_PATH], tmp[MAX_PATH], name[NAME_MAX_LEN + 1];
    uint32_t cur = cwd;
    int parent;

    path[0] = 0;
    while (cur != ROOT_INO) {
        parent = fs_dir_lookup(cur, "..");
        if (parent < 0 || fs_dir_name_of((uint32_t)parent, cur, name) != 0)
            return fail("broken directory tree");
        strcpy(tmp, "/");
        strcat(tmp, name);
        strcat(tmp, path);
        strcpy(path, tmp);
        cur = (uint32_t)parent;
    }
    strcpy(buf, path[0] ? path : "/");
    return 0;
}
