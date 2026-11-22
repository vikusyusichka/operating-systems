#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "allocator.h"
#include "tester.h"

#define SLOTS 512
#define CHECK_EVERY 16

struct slot {
    unsigned char *ptr;
    size_t size;
    uint32_t sum;
};

static struct slot slots[SLOTS];
static uint64_t rng_state;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}

static size_t random_size(void)
{
    uint32_t r = rnd() % 1000;

    if (r < 10)
        return 0;
    if (r < 650)
        return rnd() % 256 + 1;
    if (r < 900)
        return rnd() % (16 * 1024) + 1;
    if (r < 995)
        return rnd() % (512 * 1024) + 1;
    return rnd() % (6 * 1024 * 1024) + 1;
}

static uint32_t checksum(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void fill_random(unsigned char *p, size_t from, size_t to)
{
    size_t i;

    for (i = from; i < to; i++)
        p[i] = (unsigned char)rnd();
}

static int verify(int i, unsigned long iter, const char *where)
{
    if (slots[i].ptr == NULL)
        return 0;
    if (checksum(slots[i].ptr, slots[i].size) != slots[i].sum) {
        printf("iter %lu: checksum mismatch in slot %d (%s)\n", iter, i, where);
        return -1;
    }
    return 0;
}

static int check_struct(unsigned long iter, const char *op)
{
    int err = mem_check();

    if (err != 0) {
        printf("iter %lu: mem_check() = %d after %s\n", iter, err, op);
        return -1;
    }
    return 0;
}

int run_tester(unsigned long iterations, unsigned long seed)
{
    unsigned long it;
    unsigned long n_alloc = 0, n_free = 0, n_realloc = 0, n_fail = 0;
    int i, op;
    size_t size, keep;
    unsigned char *p;
    uint32_t prefix;
    const char *name;

    rng_state = seed ? seed : 1;

    for (it = 0; it < iterations; it++) {
        i = (int)(rnd() % SLOTS);
        if (slots[i].ptr != NULL)
            op = (rnd() % 2 == 0) ? 1 : 2;
        else
            op = (rnd() % 4 == 0) ? 2 : 0;

        if (verify(i, it, "before op") != 0)
            return -1;

        if (op == 0) {
            name = "mem_alloc";
            size = random_size();
            p = mem_alloc(size);
            n_alloc++;
            if (p == NULL) {
                n_fail++;
            } else {
                fill_random(p, 0, size);
                slots[i].ptr = p;
                slots[i].size = size;
                slots[i].sum = checksum(p, size);
            }
        } else if (op == 1) {
            name = "mem_free";
            mem_free(slots[i].ptr);
            slots[i].ptr = NULL;
            n_free++;
        } else {
            name = "mem_realloc";
            size = random_size();
            keep = slots[i].size < size ? slots[i].size : size;
            if (slots[i].ptr == NULL)
                keep = 0;
            prefix = checksum(slots[i].ptr, keep);

            p = mem_realloc(slots[i].ptr, size);
            n_realloc++;
            if (p == NULL) {
                n_fail++;
                if (verify(i, it, "after failed realloc") != 0)
                    return -1;
            } else {
                if (checksum(p, keep) != prefix) {
                    printf("iter %lu: realloc lost data in slot %d\n", it, i);
                    return -1;
                }
                fill_random(p, keep, size);
                slots[i].ptr = p;
                slots[i].size = size;
                slots[i].sum = checksum(p, size);
            }
        }

        if (it % CHECK_EVERY == 0 && check_struct(it, name) != 0)
            return -1;

        if (it % 1000 == 0) {
            for (i = 0; i < SLOTS; i++)
                if (verify(i, it, "periodic") != 0)
                    return -1;
        }
    }

    for (i = 0; i < SLOTS; i++) {
        if (verify(i, it, "final") != 0)
            return -1;
        mem_free(slots[i].ptr);
        slots[i].ptr = NULL;
    }
    if (check_struct(it, "final free") != 0)
        return -1;

    printf("tester: %lu iterations, alloc=%lu free=%lu realloc=%lu failed=%lu\n",
           iterations, n_alloc, n_free, n_realloc, n_fail);
    return 0;
}
