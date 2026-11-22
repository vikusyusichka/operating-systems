#ifndef RADIX_H
#define RADIX_H

#include <stddef.h>

int radix_init(unsigned page_shift);
int radix_prepare(size_t key, size_t count);
void radix_set(size_t key, void *val);
void *radix_get(size_t key);

#endif
