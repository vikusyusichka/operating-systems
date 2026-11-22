#include "radix.h"
#include "kernel.h"

static unsigned lvl_bits[3];
static unsigned key_bits;
static void **root;

static size_t level_index(size_t key, int l)
{
    unsigned shift;

    if (l == 0)
        shift = lvl_bits[1] + lvl_bits[2];
    else if (l == 1)
        shift = lvl_bits[2];
    else
        shift = 0;
    return (key >> shift) & (((size_t)1 << lvl_bits[l]) - 1);
}

int radix_init(unsigned page_shift)
{
    unsigned addr_bits = (sizeof(void *) == 8) ? 48 : 32;

    key_bits = addr_bits - page_shift;
    lvl_bits[0] = key_bits / 3;
    lvl_bits[1] = key_bits / 3;
    lvl_bits[2] = key_bits - 2 * (key_bits / 3);

    root = kernel_alloc(sizeof(void *) << lvl_bits[0]);
    return root != NULL ? 0 : -1;
}

static int key_valid(size_t key)
{
    return key_bits >= sizeof(size_t) * 8 || (key >> key_bits) == 0;
}

static int ensure_path(size_t key)
{
    void **n1, **n2;

    if (!key_valid(key))
        return -1;

    n1 = root[level_index(key, 0)];
    if (n1 == NULL) {
        n1 = kernel_alloc(sizeof(void *) << lvl_bits[1]);
        if (n1 == NULL)
            return -1;
        root[level_index(key, 0)] = n1;
    }
    n2 = n1[level_index(key, 1)];
    if (n2 == NULL) {
        n2 = kernel_alloc(sizeof(void *) << lvl_bits[2]);
        if (n2 == NULL)
            return -1;
        n1[level_index(key, 1)] = n2;
    }
    return 0;
}

int radix_prepare(size_t key, size_t count)
{
    size_t k = key, end = key + count;
    size_t leaf_mask = ((size_t)1 << lvl_bits[2]) - 1;

    while (k < end) {
        if (ensure_path(k) != 0)
            return -1;
        k = (k | leaf_mask) + 1;
    }
    return 0;
}

void radix_set(size_t key, void *val)
{
    void **n1, **n2;

    if (!key_valid(key))
        return;
    n1 = root[level_index(key, 0)];
    if (n1 == NULL)
        return;
    n2 = n1[level_index(key, 1)];
    if (n2 == NULL)
        return;
    n2[level_index(key, 2)] = val;
}

void *radix_get(size_t key)
{
    void **n1, **n2;

    if (!key_valid(key))
        return NULL;
    n1 = root[level_index(key, 0)];
    if (n1 == NULL)
        return NULL;
    n2 = n1[level_index(key, 1)];
    if (n2 == NULL)
        return NULL;
    return n2[level_index(key, 2)];
}
