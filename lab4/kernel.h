#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

#define MAX_FD 16

int k_mkfs(uint32_t ndesc);
int k_create(const char *name);
int k_open(const char *name);
int k_close(int fd);
int k_seek(int fd, uint64_t off);
long k_read(int fd, void *buf, uint64_t n);
long k_write(int fd, const void *buf, uint64_t n);
int k_link(const char *name1, const char *name2);
int k_unlink(const char *name);
int k_truncate(const char *name, uint64_t size);

const char *k_error(void);

#endif
