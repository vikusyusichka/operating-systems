#include <stdio.h>
#include <string.h>

#include "device.h"

static FILE *dev;
static uint32_t nblocks;

int dev_open(const char *path, long create_size)
{
    static const char zero[BLOCK_SIZE];
    long size, i;

    dev = fopen(path, "r+b");
    if (dev == NULL) {
        dev = fopen(path, "w+b");
        if (dev == NULL)
            return -1;
        for (i = 0; i < create_size / BLOCK_SIZE; i++)
            fwrite(zero, BLOCK_SIZE, 1, dev);
        fflush(dev);
    }

    fseek(dev, 0, SEEK_END);
    size = ftell(dev);
    nblocks = (uint32_t)(size / BLOCK_SIZE);
    return nblocks > 0 ? 0 : -1;
}

void dev_close(void)
{
    if (dev != NULL)
        fclose(dev);
    dev = NULL;
}

uint32_t dev_nblocks(void)
{
    return nblocks;
}

int dev_read(uint32_t bno, void *buf)
{
    if (bno >= nblocks)
        return -1;
    fseek(dev, (long)bno * BLOCK_SIZE, SEEK_SET);
    return fread(buf, BLOCK_SIZE, 1, dev) == 1 ? 0 : -1;
}

int dev_write(uint32_t bno, const void *buf)
{
    if (bno >= nblocks)
        return -1;
    fseek(dev, (long)bno * BLOCK_SIZE, SEEK_SET);
    if (fwrite(buf, BLOCK_SIZE, 1, dev) != 1)
        return -1;
    fflush(dev);
    return 0;
}
