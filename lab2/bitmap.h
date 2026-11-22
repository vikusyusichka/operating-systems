#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>
#include <stdint.h>

#define MLB_MAX_LEVELS 6

struct mlbitmap {
    unsigned levels;
    size_t nbits;
    size_t words[MLB_MAX_LEVELS];
    uint64_t *map[MLB_MAX_LEVELS];
};

int mlb_init(struct mlbitmap *bm, size_t nbits);
void mlb_set(struct mlbitmap *bm, size_t i);
void mlb_clear(struct mlbitmap *bm, size_t i);
int mlb_test(const struct mlbitmap *bm, size_t i);
long mlb_find_from(const struct mlbitmap *bm, size_t i);

unsigned bit_ctz64(uint64_t x);
unsigned bit_popcount64(uint64_t x);

#endif
