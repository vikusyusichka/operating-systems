#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

#define BLOCK_SIZE 512

int dev_open(const char *path, long create_size);
void dev_close(void);
uint32_t dev_nblocks(void);
int dev_read(uint32_t bno, void *buf);
int dev_write(uint32_t bno, const void *buf);

#endif
