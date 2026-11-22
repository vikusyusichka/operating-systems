#include "bitmap.h"
#include "kernel.h"

unsigned bit_ctz64(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(x);
#else
    unsigned n = 0;

    while ((x & 1) == 0) {
        x >>= 1;
        n++;
    }
    return n;
#endif
}

unsigned bit_popcount64(uint64_t x)
{
    unsigned n = 0;

    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
}

int mlb_init(struct mlbitmap *bm, size_t nbits)
{
    size_t n = nbits, w;
    unsigned l = 0;

    bm->nbits = nbits;
    do {
        if (l == MLB_MAX_LEVELS)
            return -1;
        w = (n + 63) / 64;
        bm->words[l] = w;
        bm->map[l] = kernel_alloc(w * sizeof(uint64_t));
        if (bm->map[l] == NULL)
            return -1;
        n = w;
        l++;
    } while (w > 1);

    bm->levels = l;
    return 0;
}

void mlb_set(struct mlbitmap *bm, size_t i)
{
    unsigned l;
    size_t w;
    uint64_t was;

    for (l = 0; l < bm->levels; l++) {
        w = i / 64;
        was = bm->map[l][w];
        bm->map[l][w] = was | ((uint64_t)1 << (i % 64));
        if (was != 0)
            break;
        i = w;
    }
}

void mlb_clear(struct mlbitmap *bm, size_t i)
{
    unsigned l;
    size_t w;

    for (l = 0; l < bm->levels; l++) {
        w = i / 64;
        bm->map[l][w] &= ~((uint64_t)1 << (i % 64));
        if (bm->map[l][w] != 0)
            break;
        i = w;
    }
}

int mlb_test(const struct mlbitmap *bm, size_t i)
{
    return (bm->map[0][i / 64] >> (i % 64)) & 1;
}

static long find_level(const struct mlbitmap *bm, unsigned l, size_t i)
{
    size_t w = i / 64;
    uint64_t bits;
    long up;

    if (w >= bm->words[l])
        return -1;

    bits = bm->map[l][w] & (~(uint64_t)0 << (i % 64));
    if (bits != 0)
        return (long)(w * 64 + bit_ctz64(bits));

    if (l + 1 == bm->levels)
        return -1;

    up = find_level(bm, l + 1, w + 1);
    if (up < 0)
        return -1;
    return (long)((size_t)up * 64 + bit_ctz64(bm->map[l][up]));
}

long mlb_find_from(const struct mlbitmap *bm, size_t i)
{
    if (i >= bm->nbits)
        return -1;
    return find_level(bm, 0, i);
}
