#ifndef KERNEL_H
#define KERNEL_H

#include <stddef.h>

size_t kernel_page_size(void);
void *kernel_alloc(size_t size);
void *kernel_alloc_aligned(size_t size, size_t align);
void kernel_free(void *ptr, size_t size);

#endif
