#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "allocator.h"
#include "block.h"
#include "kernel.h"

#define DEFAULT_ARENA_PAGES 16

static struct arena *arena_list = NULL;
static struct free_node free_list = { &free_list, &free_list };

static size_t default_arena_size(void)
{
    return kernel_page_size() * DEFAULT_ARENA_PAGES;
}

static size_t max_default_payload(void)
{
    return default_arena_size() - ARENA_HDR - BLOCK_HDR;
}

static void free_list_add(struct block *b)
{
    struct free_node *n = block_to_node(b);

    n->next = free_list.next;
    n->prev = &free_list;
    free_list.next->prev = n;
    free_list.next = n;
}

static void free_list_remove(struct block *b)
{
    struct free_node *n = block_to_node(b);

    n->prev->next = n->next;
    n->next->prev = n->prev;
}

static struct arena *block_arena(struct block *b)
{
    return (struct arena *)((char *)b - ARENA_HDR);
}

static struct block *arena_first_block(struct arena *a)
{
    return (struct block *)((char *)a + ARENA_HDR);
}

static struct block *arena_create(size_t size, int large)
{
    struct arena *a;
    struct block *b;

    a = kernel_alloc(size);
    if (a == NULL)
        return NULL;

    a->size = size;
    a->prev = NULL;
    a->next = arena_list;
    if (arena_list != NULL)
        arena_list->prev = a;
    arena_list = a;

    b = arena_first_block(a);
    b->size_curr = (size - ARENA_HDR - BLOCK_HDR) | BLOCK_FIRST | BLOCK_LAST;
    if (large)
        block_set(b, BLOCK_LARGE);
    b->size_prev = 0;
    return b;
}

static void arena_destroy(struct block *b)
{
    struct arena *a = block_arena(b);

    if (a->prev != NULL)
        a->prev->next = a->next;
    else
        arena_list = a->next;
    if (a->next != NULL)
        a->next->prev = a->prev;

    kernel_free(a, a->size);
}

static void block_merge(struct block *left, struct block *right)
{
    block_set_size(left, block_size(left) + BLOCK_HDR + block_size(right));
    if (block_has(right, BLOCK_LAST))
        block_set(left, BLOCK_LAST);
    else
        block_next(left)->size_prev = block_size(left);
}

static struct block *block_split(struct block *b, size_t size)
{
    struct block *rest;
    size_t old = block_size(b);

    if (old < size + BLOCK_HDR + MIN_PAYLOAD)
        return NULL;

    block_set_size(b, size);
    rest = block_next(b);
    rest->size_curr = old - size - BLOCK_HDR;
    rest->size_prev = size;

    if (block_has(b, BLOCK_LAST)) {
        block_clear(b, BLOCK_LAST);
        block_set(rest, BLOCK_LAST);
    } else {
        block_next(rest)->size_prev = block_size(rest);
    }
    return rest;
}

static void release_tail(struct block *rest)
{
    struct block *next;

    if (!block_has(rest, BLOCK_LAST)) {
        next = block_next(rest);
        if (!block_has(next, BLOCK_BUSY)) {
            free_list_remove(next);
            block_merge(rest, next);
        }
    }
    free_list_add(rest);
}

static struct block *find_free(size_t size)
{
    struct free_node *n;

    for (n = free_list.next; n != &free_list; n = n->next) {
        if (block_size(node_to_block(n)) >= size)
            return node_to_block(n);
    }
    return NULL;
}

static size_t normalize_size(size_t size)
{
    if (size > SIZE_MAX / 2)
        return 0;
    if (size < MIN_PAYLOAD)
        size = MIN_PAYLOAD;
    return ROUND_UP(size, ALIGNMENT);
}

static void *alloc_large(size_t size)
{
    struct block *b;
    size_t page = kernel_page_size();
    size_t total;

    if (size > SIZE_MAX - ARENA_HDR - BLOCK_HDR - page)
        return NULL;
    total = ROUND_UP(size + ARENA_HDR + BLOCK_HDR, page);

    b = arena_create(total, 1);
    if (b == NULL)
        return NULL;
    block_set(b, BLOCK_BUSY);
    return block_to_payload(b);
}

void *mem_alloc(size_t size)
{
    struct block *b, *rest;

    size = normalize_size(size);
    if (size == 0)
        return NULL;

    if (size > max_default_payload())
        return alloc_large(size);

    b = find_free(size);
    if (b != NULL) {
        free_list_remove(b);
    } else {
        b = arena_create(default_arena_size(), 0);
        if (b == NULL)
            return NULL;
    }

    rest = block_split(b, size);
    if (rest != NULL)
        free_list_add(rest);

    block_set(b, BLOCK_BUSY);
    return block_to_payload(b);
}

void mem_free(void *ptr)
{
    struct block *b, *next, *prev;

    if (ptr == NULL)
        return;

    b = payload_to_block(ptr);
    if (block_has(b, BLOCK_LARGE)) {
        arena_destroy(b);
        return;
    }

    block_clear(b, BLOCK_BUSY);

    if (!block_has(b, BLOCK_LAST)) {
        next = block_next(b);
        if (!block_has(next, BLOCK_BUSY)) {
            free_list_remove(next);
            block_merge(b, next);
        }
    }
    if (!block_has(b, BLOCK_FIRST)) {
        prev = block_prev(b);
        if (!block_has(prev, BLOCK_BUSY)) {
            free_list_remove(prev);
            block_merge(prev, b);
            b = prev;
        }
    }

    if (block_has(b, BLOCK_FIRST) && block_has(b, BLOCK_LAST)) {
        arena_destroy(b);
        return;
    }
    free_list_add(b);
}

void *mem_realloc(void *ptr, size_t size)
{
    struct block *b, *next, *rest;
    size_t new_size, cur;
    void *new_ptr;

    if (ptr == NULL)
        return mem_alloc(size);

    new_size = normalize_size(size);
    if (new_size == 0)
        return NULL;

    b = payload_to_block(ptr);
    cur = block_size(b);

    if (block_has(b, BLOCK_LARGE)) {
        if (new_size <= cur && new_size > max_default_payload())
            return ptr;
        goto move;
    }

    if (new_size <= cur) {
        rest = block_split(b, new_size);
        if (rest != NULL)
            release_tail(rest);
        return ptr;
    }

    if (!block_has(b, BLOCK_LAST)) {
        next = block_next(b);
        if (!block_has(next, BLOCK_BUSY) &&
            cur + BLOCK_HDR + block_size(next) >= new_size) {
            free_list_remove(next);
            block_merge(b, next);
            rest = block_split(b, new_size);
            if (rest != NULL)
                free_list_add(rest);
            return ptr;
        }
    }

move:
    new_ptr = mem_alloc(size);
    if (new_ptr == NULL)
        return NULL;
    memcpy(new_ptr, ptr, cur < new_size ? cur : new_size);
    mem_free(ptr);
    return new_ptr;
}

void mem_show(const char *title)
{
    struct arena *a;
    struct block *b;
    int i = 0;

    printf("==== %s ====\n", title);
    if (arena_list == NULL)
        printf("  (no arenas)\n");

    for (a = arena_list; a != NULL; a = a->next, i++) {
        printf("arena #%d %p size=%zu\n", i, (void *)a, a->size);
        b = arena_first_block(a);
        for (;;) {
            printf("  %p  %-4s size=%-8zu prev=%-8zu%s%s%s\n",
                   block_to_payload(b),
                   block_has(b, BLOCK_BUSY) ? "busy" : "free",
                   block_size(b), block_size_prev(b),
                   block_has(b, BLOCK_FIRST) ? " first" : "",
                   block_has(b, BLOCK_LAST) ? " last" : "",
                   block_has(b, BLOCK_LARGE) ? " large" : "");
            if (block_has(b, BLOCK_LAST))
                break;
            b = block_next(b);
        }
    }
    printf("\n");
}

int mem_check(void)
{
    struct arena *a;
    struct block *b, *prev;
    struct free_node *n;
    size_t total, free_in_arenas = 0, free_in_list = 0;

    for (a = arena_list; a != NULL; a = a->next) {
        if (a->next != NULL && a->next->prev != a)
            return -1;

        b = arena_first_block(a);
        if (!block_has(b, BLOCK_FIRST) || b->size_prev != 0)
            return -2;

        prev = NULL;
        total = ARENA_HDR;
        for (;;) {
            if (block_size(b) % ALIGNMENT != 0 ||
                ((uintptr_t)block_to_payload(b) % ALIGNMENT) != 0)
                return -3;
            if (prev != NULL) {
                if (block_has(b, BLOCK_FIRST))
                    return -4;
                if (b->size_prev != block_size(prev))
                    return -5;
                if (!block_has(prev, BLOCK_BUSY) && !block_has(b, BLOCK_BUSY))
                    return -6;
            }
            if (!block_has(b, BLOCK_BUSY))
                free_in_arenas++;

            total += BLOCK_HDR + block_size(b);
            if (total > a->size)
                return -7;
            if (block_has(b, BLOCK_LAST))
                break;
            prev = b;
            b = block_next(b);
        }
        if (total != a->size)
            return -8;
    }

    for (n = free_list.next; n != &free_list; n = n->next) {
        if (n->next->prev != n)
            return -9;
        if (block_has(node_to_block(n), BLOCK_BUSY))
            return -10;
        free_in_list++;
    }
    if (free_in_list != free_in_arenas)
        return -11;

    return 0;
}
