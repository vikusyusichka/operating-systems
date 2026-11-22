#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

#define MAX_FD 16
#define MAX_PATH 1024
#define MAX_SYMLINK_FOLLOW 16

void k_init(void);
int k_mkfs(uint32_t ndesc);
int k_lookup(const char *path, int follow_last);
int k_create(const char *path);
int k_open(const char *path);
int k_close(int fd);
int k_seek(int fd, uint64_t off);
long k_read(int fd, void *buf, uint64_t n);
long k_write(int fd, const void *buf, uint64_t n);
int k_link(const char *path1, const char *path2);
int k_unlink(const char *path);
int k_truncate(const char *path, uint64_t size);
int k_mkdir(const char *path);
int k_rmdir(const char *path);
int k_cd(const char *path);
int k_symlink(const char *str, const char *path);
int k_readlink(uint32_t ino, char *buf);
int k_getcwd(char *buf);

const char *k_error(void);

#endif
