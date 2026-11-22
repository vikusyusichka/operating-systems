#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "fs.h"
#include "kernel.h"

#define DEFAULT_DEV_SIZE (1024L * 1024)
#define MAX_ARGS 4

static const char *type_name(int type)
{
    switch (type) {
    case FT_REG:     return "regular";
    case FT_DIR:     return "directory";
    case FT_SYMLINK: return "symlink";
    }
    return "?";
}

static int need_fs(void)
{
    if (fs_super() == NULL) {
        printf("error: no file system, run mkfs first\n");
        return 0;
    }
    return 1;
}

static void print_entry(const char *name, uint32_t ino)
{
    struct desc d;
    char target[BLOCK_SIZE + 1];

    fs_desc_get(ino, &d);
    printf("  %-27s %4u  %-9s", name, ino, type_name(d.type));
    if (d.type == FT_SYMLINK && k_readlink(ino, target) == 0)
        printf(" -> %s", target);
    putchar('\n');
}

static void cmd_ls(const char *path)
{
    int ino = k_lookup(path, 1);
    struct desc d;

    if (ino < 0) {
        printf("error: %s\n", k_error());
        return;
    }
    fs_desc_get((uint32_t)ino, &d);
    if (d.type != FT_DIR) {
        printf("error: not a directory\n");
        return;
    }
    printf("  %-27s %4s  %s\n", "name", "desc", "type");
    fs_dir_list((uint32_t)ino, print_entry);
}

static void cmd_stat(const char *path)
{
    struct desc d;
    uint32_t i, bno, nblk, ind;
    int ino = k_lookup(path, 0), any = 0;
    char target[BLOCK_SIZE + 1];

    if (ino < 0) {
        printf("error: %s\n", k_error());
        return;
    }
    fs_desc_get((uint32_t)ino, &d);
    nblk = fs_count_blocks(&d, &ind);
    printf("  path:   %s\n", path);
    printf("  desc:   %d\n", ino);
    printf("  type:   %s\n", type_name(d.type));
    if (d.type == FT_SYMLINK && k_readlink((uint32_t)ino, target) == 0)
        printf("  target: %s\n", target);
    printf("  nlink:  %u\n", d.nlink);
    printf("  size:   %llu\n", (unsigned long long)d.size);
    printf("  blocks: %u data + %u map\n", nblk, ind);
    printf("  map:   ");
    for (i = 0; i < MAX_FILE_BLOCKS; i++) {
        bno = fs_bmap(&d, i);
        if (bno != 0) {
            printf(" [%u]=%u", i, bno);
            any = 1;
        }
    }
    printf("%s\n", any ? "" : " (no blocks)");
    if (d.indirect != 0)
        printf("  indirect map block: %u\n", d.indirect);
}

static void cmd_df(void)
{
    const struct superblock *s = fs_super();

    printf("  blocks %u x %u bytes, descriptors %u\n", s->nblocks, BLOCK_SIZE, s->ndesc);
    printf("  bitmap %u..%u, descriptors %u..%u, data %u..%u\n",
           s->bitmap_start, s->bitmap_start + s->bitmap_blocks - 1,
           s->desc_start, s->desc_start + s->desc_blocks - 1,
           s->data_start, s->nblocks - 1);
    printf("  free blocks %u\n", fs_free_blocks());
}

static void cmd_pwd(void)
{
    char buf[MAX_PATH];

    if (k_getcwd(buf) == 0)
        printf("  %s\n", buf);
    else
        printf("error: %s\n", k_error());
}

static void cmd_read(int fd, uint64_t size)
{
    unsigned char *buf = malloc(size ? size : 1);
    long r, i;

    r = k_read(fd, buf, size);
    if (r < 0) {
        printf("error: %s\n", k_error());
    } else {
        printf("read %ld bytes: ", r);
        for (i = 0; i < r; i++) {
            if (buf[i] == 0)
                putchar('.');
            else if (buf[i] >= ' ' && buf[i] <= '~')
                putchar(buf[i]);
            else
                putchar('?');
        }
        putchar('\n');
    }
    free(buf);
}

static void cmd_write(int fd, uint64_t size, const char *text)
{
    unsigned char *buf = malloc(size ? size : 1);
    size_t len = strlen(text);
    uint64_t i;
    long r;

    for (i = 0; i < size; i++)
        buf[i] = strcmp(text, "\\0") == 0 ? 0 : (unsigned char)text[i % len];
    r = k_write(fd, buf, size);
    if (r < 0)
        printf("error: %s\n", k_error());
    else if ((uint64_t)r < size)
        printf("wrote %ld bytes (%s)\n", r, k_error());
    else
        printf("wrote %ld bytes\n", r);
    free(buf);
}

static void help(void)
{
    printf("commands (pathname can be absolute or relative):\n"
           "  mkfs n                     create file system with n file descriptors\n"
           "  stat pathname              show file descriptor (symlink is not followed)\n"
           "  ls [pathname]              list hard links in directory\n"
           "  create pathname            create regular file\n"
           "  open pathname              open file, prints numeric fd\n"
           "  close fd                   close fd\n"
           "  seek fd offset             set offset for fd\n"
           "  read fd size               read bytes ('.' is zero byte)\n"
           "  write fd size [text]       write bytes, text is repeated (\\0 - zero bytes)\n"
           "  link pathname1 pathname2   create hard link pathname2 to pathname1\n"
           "  unlink pathname            remove hard link\n"
           "  truncate pathname size     change file size\n"
           "  mkdir pathname             create directory\n"
           "  rmdir pathname             remove empty directory\n"
           "  cd pathname                change current working directory\n"
           "  pwd                        print current working directory\n"
           "  symlink str pathname       create symbolic link with content str\n"
           "  df                         show layout and free blocks\n"
           "  quit\n");
}

static void ok_or_error(int r)
{
    if (r < 0)
        printf("error: %s\n", k_error());
}

static int execute(int argc, char **argv)
{
    const char *c = argv[0];
    int r;

    if (strcmp(c, "quit") == 0 || strcmp(c, "exit") == 0)
        return 1;
    if (strcmp(c, "help") == 0) {
        help();
        return 0;
    }
    if (strcmp(c, "mkfs") == 0 && argc == 2) {
        ok_or_error(k_mkfs((uint32_t)strtoul(argv[1], NULL, 10)));
        return 0;
    }
    if (!need_fs())
        return 0;

    if (strcmp(c, "ls") == 0 && argc <= 2)
        cmd_ls(argc == 2 ? argv[1] : ".");
    else if (strcmp(c, "df") == 0 && argc == 1)
        cmd_df();
    else if (strcmp(c, "pwd") == 0 && argc == 1)
        cmd_pwd();
    else if (strcmp(c, "stat") == 0 && argc == 2)
        cmd_stat(argv[1]);
    else if (strcmp(c, "create") == 0 && argc == 2)
        ok_or_error(k_create(argv[1]));
    else if (strcmp(c, "open") == 0 && argc == 2) {
        r = k_open(argv[1]);
        if (r < 0)
            printf("error: %s\n", k_error());
        else
            printf("fd = %d\n", r);
    } else if (strcmp(c, "close") == 0 && argc == 2)
        ok_or_error(k_close(atoi(argv[1])));
    else if (strcmp(c, "seek") == 0 && argc == 3)
        ok_or_error(k_seek(atoi(argv[1]), strtoull(argv[2], NULL, 10)));
    else if (strcmp(c, "read") == 0 && argc == 3)
        cmd_read(atoi(argv[1]), strtoull(argv[2], NULL, 10));
    else if (strcmp(c, "write") == 0 && (argc == 3 || argc == 4))
        cmd_write(atoi(argv[1]), strtoull(argv[2], NULL, 10), argc == 4 ? argv[3] : "abcdefghij");
    else if (strcmp(c, "link") == 0 && argc == 3)
        ok_or_error(k_link(argv[1], argv[2]));
    else if (strcmp(c, "unlink") == 0 && argc == 2)
        ok_or_error(k_unlink(argv[1]));
    else if (strcmp(c, "truncate") == 0 && argc == 3)
        ok_or_error(k_truncate(argv[1], strtoull(argv[2], NULL, 10)));
    else if (strcmp(c, "mkdir") == 0 && argc == 2)
        ok_or_error(k_mkdir(argv[1]));
    else if (strcmp(c, "rmdir") == 0 && argc == 2)
        ok_or_error(k_rmdir(argv[1]));
    else if (strcmp(c, "cd") == 0 && argc == 2)
        ok_or_error(k_cd(argv[1]));
    else if (strcmp(c, "symlink") == 0 && argc == 3)
        ok_or_error(k_symlink(argv[1], argv[2]));
    else
        printf("error: unknown command or wrong arguments (type help)\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "disk.img";
    long size = argc > 2 ? atol(argv[2]) : DEFAULT_DEV_SIZE;
    int interactive = isatty(fileno(stdin));
    char line[MAX_PATH + 64], cwdbuf[MAX_PATH], *args[MAX_ARGS + 1], *p;
    int n;

    if (dev_open(path, size) != 0) {
        printf("cannot open device %s\n", path);
        return 1;
    }
    printf("device %s: %u blocks of %d bytes\n", path, dev_nblocks(), BLOCK_SIZE);
    k_init();
    if (fs_mount() == 0)
        printf("file system mounted\n");
    else
        printf("no file system on device, use mkfs\n");

    for (;;) {
        if (fs_super() == NULL || k_getcwd(cwdbuf) != 0)
            strcpy(cwdbuf, "");
        if (interactive) {
            printf("%s> ", cwdbuf);
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 0 || line[0] == '#')
            continue;
        if (!interactive)
            printf("%s> %s\n", cwdbuf, line);

        n = 0;
        for (p = strtok(line, " \t"); p != NULL && n <= MAX_ARGS; p = strtok(NULL, " \t"))
            args[n++] = p;
        if (n > MAX_ARGS) {
            printf("error: too many arguments\n");
            continue;
        }
        if (execute(n, args))
            break;
    }

    dev_close();
    return 0;
}
