#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>

#define ALIGNMENT 16
#define ROUND_UP(x, a) (((x) + ((size_t)(a) - 1)) & ~((size_t)(a) - 1))

#define BLOCK_BUSY  ((size_t)1)
#define BLOCK_FIRST ((size_t)2)
#define BLOCK_LAST  ((size_t)4)
#define BLOCK_LARGE ((size_t)8)
#define BLOCK_FLAGS (BLOCK_BUSY | BLOCK_FIRST | BLOCK_LAST | BLOCK_LARGE)

struct block {
    size_t size_curr;
    size_t size_prev;
};

struct free_node {
    struct free_node *next;
    struct free_node *prev;
};

struct arena {
    struct arena *next;
    struct arena *prev;
    size_t size;
};

#define BLOCK_HDR   ROUND_UP(sizeof(struct block), ALIGNMENT)
#define ARENA_HDR   ROUND_UP(sizeof(struct arena), ALIGNMENT)
#define MIN_PAYLOAD ROUND_UP(sizeof(struct free_node), ALIGNMENT)

static inline size_t block_size(const struct block *b)
{
    return b->size_curr & ~BLOCK_FLAGS;
}

static inline size_t block_size_prev(const struct block *b)
{
    return b->size_prev;
}

static inline void block_set_size(struct block *b, size_t size)
{
    b->size_curr = size | (b->size_curr & BLOCK_FLAGS);
}

static inline int block_has(const struct block *b, size_t flag)
{
    return (b->size_curr & flag) != 0;
}

static inline void block_set(struct block *b, size_t flag)
{
    b->size_curr |= flag;
}

static inline void block_clear(struct block *b, size_t flag)
{
    b->size_curr &= ~flag;
}

static inline void *block_to_payload(struct block *b)
{
    return (char *)b + BLOCK_HDR;
}

static inline struct block *payload_to_block(void *ptr)
{
    return (struct block *)((char *)ptr - BLOCK_HDR);
}

static inline struct block *block_next(struct block *b)
{
    return (struct block *)((char *)b + BLOCK_HDR + block_size(b));
}

static inline struct block *block_prev(struct block *b)
{
    return (struct block *)((char *)b - BLOCK_HDR - block_size_prev(b));
}

static inline struct free_node *block_to_node(struct block *b)
{
    return (struct free_node *)block_to_payload(b);
}

static inline struct block *node_to_block(struct free_node *n)
{
    return payload_to_block(n);
}

#endif
