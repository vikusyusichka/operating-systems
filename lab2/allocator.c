#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "allocator.h"
#include "bitmap.h"
#include "config.h"
#include "kernel.h"
#include "radix.h"

#define ROUND_UP(x, a) (((x) + ((size_t)(a) - 1)) & ~((size_t)(a) - 1))

struct link {
    struct link *next;
    struct link *prev;
};

enum {
    H_FREE = 1,
    H_BUSY,
    H_SLAB,
    H_HUGE
};

struct hdr {
    struct link link;
    unsigned char kind;
    unsigned char in_slab;
    unsigned short cls;
    unsigned nobj;
    unsigned nfree;
    char *addr;
    size_t pages;
    char *objs;
    uint64_t bitmap[];
};

struct class_info {
    size_t size;
    size_t pages;
    size_t objs_off;
    unsigned nobj;
    int small;
};

struct cache {
    struct link list;
    size_t nslabs;
    size_t nempty;
    size_t nfree;
};

struct hnode {
    size_t pages;
    struct link list;
    struct hnode *next;
};

static int initialized;
static size_t page_size;
static unsigned page_shift;
static size_t arena_size;

static struct class_info cls_info[NCLASSES];
static struct cache caches[NCLASSES];
static unsigned hdr_cls;

static struct link *ext_array;
static struct hnode **ext_hash;
static struct hnode *hnode_free;
static struct mlbitmap ext_bm;

static char *arenas[MAX_ARENAS];
static unsigned narenas;
static struct link huge_list;

static void list_init(struct link *l)
{
    l->next = l;
    l->prev = l;
}

static int list_empty(const struct link *l)
{
    return l->next == l;
}

static void list_add_head(struct link *l, struct link *n)
{
    n->next = l->next;
    n->prev = l;
    l->next->prev = n;
    l->next = n;
}

static void list_add_tail(struct link *l, struct link *n)
{
    n->prev = l->prev;
    n->next = l;
    l->prev->next = n;
    l->prev = n;
}

static void list_del(struct link *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

static size_t page_key(const void *p)
{
    return (uintptr_t)p >> page_shift;
}

static char *arena_base(const void *p)
{
    return (char *)((uintptr_t)p & ~(uintptr_t)(arena_size - 1));
}

static void reg_bounds(struct hdr *h)
{
    size_t key = page_key(h->addr);

    radix_set(key, h);
    radix_set(key + h->pages - 1, h);
}

static size_t gcd(size_t a, size_t b)
{
    size_t t;

    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static unsigned fls_size(size_t v)
{
    unsigned r = 0;

    while (v != 0) {
        r++;
        v >>= 1;
    }
    return r;
}

static size_t hdr_bytes(size_t words)
{
    return sizeof(struct hdr) + words * sizeof(uint64_t);
}

static unsigned size_to_class(size_t size)
{
    size_t v, p, j;

    if (size <= SIZE_N)
        return (unsigned)((size + ALIGN - 1) / ALIGN) - 1;

    v = (size - SIZE_N - 1) / (M_GROUP * ALIGN * 2) + 1;
    p = fls_size(v);
    if (p > K_GROUPS)
        return NCLASSES;

    j = M_GROUP - (SIZE_N + (size_t)M_GROUP * ALIGN * 2 * (((size_t)1 << p) - 1) - size)
                  / (((size_t)1 << p) * ALIGN);
    return (unsigned)(N_FIRST + (p - 1) * M_GROUP + j - 1);
}

static size_t class_size(unsigned c)
{
    size_t p, j;

    if (c < N_FIRST)
        return (size_t)(c + 1) * ALIGN;
    p = (c - N_FIRST) / M_GROUP + 1;
    j = (c - N_FIRST) % M_GROUP + 1;
    return SIZE_N + (size_t)M_GROUP * ALIGN * 2 * (((size_t)1 << (p - 1)) - 1)
           + j * ((size_t)1 << p) * ALIGN;
}

static size_t slab_pages(size_t obj)
{
    size_t ymin = obj / gcd(obj, page_size);
    size_t y = ymin;

    if (y > SLAB_MAX_PAGES) {
        y = (obj * SLAB_MIN_OBJS + page_size - 1) / page_size;
        if (y > SLAB_MAX_PAGES)
            y = SLAB_MAX_PAGES;
        if (y * page_size < obj)
            y = (obj + page_size - 1) / page_size;
        return y;
    }
    while (y * page_size / obj < SLAB_MIN_OBJS && y + ymin <= SLAB_MAX_PAGES)
        y += ymin;
    return y;
}

static struct link *ext_list(size_t pages, int create)
{
    struct hnode *n;
    size_t b;

    if (pages <= EXT_ARRAY_MAX)
        return &ext_array[pages - 1];

    b = pages % EXT_HASH_BUCKETS;
    for (n = ext_hash[b]; n != NULL; n = n->next)
        if (n->pages == pages)
            return &n->list;

    if (!create || hnode_free == NULL)
        return NULL;
    n = hnode_free;
    hnode_free = n->next;
    n->pages = pages;
    list_init(&n->list);
    n->next = ext_hash[b];
    ext_hash[b] = n;
    return &n->list;
}

static void ext_list_release(size_t pages)
{
    struct hnode **pn, *n;

    if (pages <= EXT_ARRAY_MAX)
        return;
    for (pn = &ext_hash[pages % EXT_HASH_BUCKETS]; *pn != NULL; pn = &(*pn)->next) {
        if ((*pn)->pages == pages) {
            n = *pn;
            *pn = n->next;
            n->next = hnode_free;
            hnode_free = n;
            return;
        }
    }
}

static void ext_insert(struct hdr *h)
{
    struct link *l = ext_list(h->pages, 1);

    if (list_empty(l))
        mlb_set(&ext_bm, h->pages - 1);
    list_add_head(l, &h->link);
}

static void ext_remove(struct hdr *h)
{
    struct link *l;

    list_del(&h->link);
    l = ext_list(h->pages, 0);
    if (list_empty(l)) {
        mlb_clear(&ext_bm, h->pages - 1);
        ext_list_release(h->pages);
    }
}

static struct hdr *hdr_alloc(void);
static void hdr_free(struct hdr *h);
static void extent_put(struct hdr *h);
static char *extent_get(size_t pages, struct hdr **out);

static struct hdr *slab_init(char *addr, unsigned c, struct hdr *ext)
{
    struct class_info *ci = &cls_info[c];
    struct cache *ca = &caches[c];
    struct hdr *s = ci->small ? (struct hdr *)addr : ext;
    size_t words = (ci->nobj + 63) / 64, i, key;

    s->kind = H_SLAB;
    s->in_slab = (unsigned char)ci->small;
    s->cls = (unsigned short)c;
    s->nobj = ci->nobj;
    s->nfree = ci->nobj;
    s->addr = addr;
    s->pages = ci->pages;
    s->objs = addr + ci->objs_off;
    for (i = 0; i < words; i++)
        s->bitmap[i] = ~(uint64_t)0;
    if (ci->nobj % 64 != 0)
        s->bitmap[words - 1] = ((uint64_t)1 << (ci->nobj % 64)) - 1;

    key = page_key(addr);
    for (i = 0; i < s->pages; i++)
        radix_set(key + i, s);

    list_add_tail(&ca->list, &s->link);
    ca->nslabs++;
    ca->nempty++;
    ca->nfree += s->nobj;
    return s;
}

static struct hdr *slab_new(unsigned c)
{
    struct class_info *ci = &cls_info[c];
    struct hdr *h = NULL;
    char *addr;

    if (ci->small) {
        addr = extent_get(ci->pages, NULL);
        if (addr == NULL)
            return NULL;
        return slab_init(addr, c, NULL);
    }
    addr = extent_get(ci->pages, &h);
    if (addr == NULL)
        return NULL;
    return slab_init(addr, c, h);
}

static void slab_release(struct hdr *s)
{
    struct cache *ca = &caches[s->cls];
    struct hdr *h;
    size_t i, key = page_key(s->addr);

    list_del(&s->link);
    ca->nempty--;
    ca->nfree -= s->nobj;
    ca->nslabs--;

    if (s->in_slab) {
        h = hdr_alloc();
        if (h == NULL) {
            list_add_tail(&ca->list, &s->link);
            ca->nempty++;
            ca->nfree += s->nobj;
            ca->nslabs++;
            return;
        }
        h->addr = s->addr;
        h->pages = s->pages;
    } else {
        h = s;
    }

    for (i = 0; i < h->pages; i++)
        radix_set(key + i, NULL);
    extent_put(h);
}

static void *slab_take(struct hdr *s)
{
    size_t words = (s->nobj + 63) / 64, w;
    unsigned bit;

    for (w = 0; w < words; w++) {
        if (s->bitmap[w] != 0) {
            bit = bit_ctz64(s->bitmap[w]);
            s->bitmap[w] &= ~((uint64_t)1 << bit);
            s->nfree--;
            return s->objs + (w * 64 + bit) * cls_info[s->cls].size;
        }
    }
    return NULL;
}

static void *cache_take(unsigned c)
{
    struct cache *ca = &caches[c];
    struct hdr *s = (struct hdr *)ca->list.next;
    void *p;

    if (s->nfree == s->nobj)
        ca->nempty--;
    p = slab_take(s);
    ca->nfree--;
    if (s->nfree == 0)
        list_del(&s->link);
    return p;
}

static void *cache_alloc(unsigned c)
{
    if (list_empty(&caches[c].list) && slab_new(c) == NULL)
        return NULL;
    return cache_take(c);
}

static void cache_free(struct hdr *s, void *p)
{
    struct cache *ca = &caches[s->cls];
    size_t i = (size_t)((char *)p - s->objs) / cls_info[s->cls].size;

    s->bitmap[i / 64] |= (uint64_t)1 << (i % 64);
    s->nfree++;
    ca->nfree++;

    if (s->nfree == 1)
        list_add_head(&ca->list, &s->link);

    if (s->nfree == s->nobj) {
        list_del(&s->link);
        list_add_tail(&ca->list, &s->link);
        ca->nempty++;
        if (s->cls != hdr_cls && ca->nempty > 1)
            slab_release(s);
    }
}

static struct hdr *hdr_alloc(void)
{
    if (list_empty(&caches[hdr_cls].list))
        return NULL;
    return cache_take(hdr_cls);
}

static void hdr_free(struct hdr *h)
{
    cache_free(radix_get(page_key(h)), h);
}

static char *arena_raw(void)
{
    char *base;

    if (narenas == MAX_ARENAS)
        return NULL;
    base = kernel_alloc_aligned(arena_size, arena_size);
    if (base == NULL)
        return NULL;
    if (radix_prepare(page_key(base), ARENA_PAGES) != 0) {
        kernel_free(base, arena_size);
        return NULL;
    }
    arenas[narenas++] = base;
    return base;
}

static void arena_drop(char *base)
{
    unsigned i;

    for (i = 0; i < narenas; i++) {
        if (arenas[i] == base) {
            arenas[i] = arenas[--narenas];
            break;
        }
    }
    kernel_free(base, arena_size);
}

static struct hdr *arena_new(void)
{
    char *base = arena_raw();
    struct hdr *h;

    if (base == NULL)
        return NULL;
    h = hdr_alloc();
    if (h == NULL) {
        arena_drop(base);
        return NULL;
    }
    h->kind = H_FREE;
    h->addr = base;
    h->pages = ARENA_PAGES;
    reg_bounds(h);
    ext_insert(h);
    return h;
}

static char *extent_get(size_t pages, struct hdr **out)
{
    struct hdr *f, *h;
    long idx;
    size_t total;
    char *addr;

    idx = mlb_find_from(&ext_bm, pages - 1);
    if (idx < 0) {
        f = arena_new();
        if (f == NULL)
            return NULL;
    } else {
        f = (struct hdr *)ext_list((size_t)idx + 1, 0)->next;
    }

    ext_remove(f);
    total = f->pages;
    addr = f->addr;

    if (total == pages) {
        if (out != NULL) {
            f->kind = H_BUSY;
            *out = f;
        } else {
            hdr_free(f);
        }
        return addr;
    }

    if (out != NULL) {
        h = hdr_alloc();
        if (h == NULL) {
            ext_insert(f);
            return NULL;
        }
        h->kind = H_BUSY;
        h->addr = addr;
        h->pages = pages;
        reg_bounds(h);
        *out = h;
    }

    f->addr = addr + pages * page_size;
    f->pages = total - pages;
    radix_set(page_key(f->addr), f);
    ext_insert(f);
    return addr;
}

static void extent_put(struct hdr *h)
{
    struct hdr *l, *r, *dead[3];
    int nd = 0;
    char *base = arena_base(h->addr), *end;

    h->kind = H_FREE;

    if (h->addr > base) {
        l = radix_get(page_key(h->addr) - 1);
        if (l != NULL && l->kind == H_FREE) {
            ext_remove(l);
            l->pages += h->pages;
            dead[nd++] = h;
            h = l;
        }
    }

    end = h->addr + h->pages * page_size;
    if (end < base + arena_size) {
        r = radix_get(page_key(end));
        if (r != NULL && r->kind == H_FREE) {
            ext_remove(r);
            h->pages += r->pages;
            dead[nd++] = r;
        }
    }

    if (h->pages == ARENA_PAGES) {
        radix_set(page_key(base), NULL);
        radix_set(page_key(base) + ARENA_PAGES - 1, NULL);
        dead[nd++] = h;
        arena_drop(base);
    } else {
        reg_bounds(h);
        ext_insert(h);
    }

    while (nd > 0)
        hdr_free(dead[--nd]);
}

static int extent_resize(struct hdr *h, size_t pages)
{
    struct hdr *t, *r;
    char *base, *end;
    size_t take;

    if (pages == h->pages)
        return 0;

    if (pages < h->pages) {
        t = hdr_alloc();
        if (t == NULL)
            return -1;
        t->addr = h->addr + pages * page_size;
        t->pages = h->pages - pages;
        h->pages = pages;
        radix_set(page_key(h->addr) + pages - 1, h);
        extent_put(t);
        return 0;
    }

    base = arena_base(h->addr);
    end = h->addr + h->pages * page_size;
    if (end >= base + arena_size)
        return -1;
    r = radix_get(page_key(end));
    if (r == NULL || r->kind != H_FREE || h->pages + r->pages < pages)
        return -1;

    take = pages - h->pages;
    ext_remove(r);
    h->pages = pages;
    radix_set(page_key(h->addr) + pages - 1, h);
    if (r->pages == take) {
        hdr_free(r);
    } else {
        r->addr += take * page_size;
        r->pages -= take;
        radix_set(page_key(r->addr), r);
        ext_insert(r);
    }
    return 0;
}

static void *huge_alloc(size_t pages)
{
    struct hdr *h;
    char *base;
    size_t bytes;

    if (pages > SIZE_MAX / page_size)
        return NULL;
    bytes = pages * page_size;
    base = kernel_alloc_aligned(bytes, arena_size);
    if (base == NULL)
        return NULL;
    if (radix_prepare(page_key(base), 1) != 0) {
        kernel_free(base, bytes);
        return NULL;
    }
    h = hdr_alloc();
    if (h == NULL) {
        kernel_free(base, bytes);
        return NULL;
    }
    h->kind = H_HUGE;
    h->addr = base;
    h->pages = pages;
    radix_set(page_key(base), h);
    list_add_tail(&huge_list, &h->link);
    return base;
}

static void huge_free(struct hdr *h)
{
    list_del(&h->link);
    radix_set(page_key(h->addr), NULL);
    kernel_free(h->addr, h->pages * page_size);
    hdr_free(h);
}

static int lazy_init(void)
{
    size_t maxw = 0, nobj0, words, i;
    struct hnode *pool;
    size_t npool = ARENA_PAGES - EXT_ARRAY_MAX;
    unsigned c;

    if (initialized)
        return 0;

    page_size = kernel_page_size();
    page_shift = fls_size(page_size) - 1;
    arena_size = page_size * ARENA_PAGES;

    for (c = 0; c < NCLASSES; c++) {
        struct class_info *ci = &cls_info[c];

        ci->size = class_size(c);
        ci->pages = slab_pages(ci->size);
        ci->small = ci->size <= SMALL_OBJ_MAX;
        nobj0 = ci->pages * page_size / ci->size;
        words = (nobj0 + 63) / 64;
        ci->objs_off = ci->small ? ROUND_UP(hdr_bytes(words), ALIGN) : 0;
        ci->nobj = (unsigned)((ci->pages * page_size - ci->objs_off) / ci->size);
        if (!ci->small && words > maxw)
            maxw = words;
        list_init(&caches[c].list);
    }
    hdr_cls = size_to_class(hdr_bytes(maxw));
    if (hdr_cls >= NCLASSES || !cls_info[hdr_cls].small)
        return -1;

    ext_array = kernel_alloc(EXT_ARRAY_MAX * sizeof(struct link));
    ext_hash = kernel_alloc(EXT_HASH_BUCKETS * sizeof(struct hnode *));
    pool = kernel_alloc(npool * sizeof(struct hnode));
    if (ext_array == NULL || ext_hash == NULL || pool == NULL)
        return -1;
    for (i = 0; i < EXT_ARRAY_MAX; i++)
        list_init(&ext_array[i]);
    for (i = 0; i < npool; i++) {
        pool[i].next = hnode_free;
        hnode_free = &pool[i];
    }

    if (mlb_init(&ext_bm, ARENA_PAGES) != 0)
        return -1;
    if (radix_init(page_shift) != 0)
        return -1;
    list_init(&huge_list);

    initialized = 1;
    return 0;
}

static int hdr_reserve(void)
{
    struct cache *ca = &caches[hdr_cls];
    struct class_info *ci = &cls_info[hdr_cls];
    struct hdr *h;
    char *base;

    while (ca->nfree < HDR_RESERVE) {
        if (mlb_find_from(&ext_bm, ci->pages - 1) >= 0) {
            if (slab_new(hdr_cls) == NULL)
                return -1;
            continue;
        }
        base = arena_raw();
        if (base == NULL)
            return -1;
        slab_init(base, hdr_cls, NULL);
        h = hdr_alloc();
        h->kind = H_FREE;
        h->addr = base + ci->pages * page_size;
        h->pages = ARENA_PAGES - ci->pages;
        reg_bounds(h);
        ext_insert(h);
    }
    return 0;
}

static void hdr_trim(void)
{
    struct cache *ca = &caches[hdr_cls];

    while (ca->nempty > 1)
        slab_release((struct hdr *)ca->list.prev);
}

static void *do_alloc(size_t size)
{
    unsigned c;
    size_t pages;
    struct hdr *h = NULL;

    if (size == 0)
        size = 1;

    c = size_to_class(size);
    if (c < NCLASSES)
        return cache_alloc(c);

    if (size > SIZE_MAX - page_size)
        return NULL;
    pages = (size + page_size - 1) / page_size;
    if (pages <= EXT_MAX_PAGES)
        return extent_get(pages, &h);
    return huge_alloc(pages);
}

static void do_free(void *ptr)
{
    struct hdr *h = radix_get(page_key(ptr));

    if (h == NULL)
        return;

    switch (h->kind) {
    case H_SLAB:
        cache_free(h, ptr);
        break;
    case H_BUSY:
        extent_put(h);
        break;
    case H_HUGE:
        huge_free(h);
        break;
    }
}

static size_t usable_size(const struct hdr *h)
{
    if (h->kind == H_SLAB)
        return cls_info[h->cls].size;
    return h->pages * page_size;
}

static void *do_realloc(void *ptr, size_t size)
{
    struct hdr *h = radix_get(page_key(ptr));
    unsigned c;
    size_t pages, old;
    void *np;

    if (h == NULL)
        return NULL;
    if (size == 0)
        size = 1;

    c = size_to_class(size);
    if (h->kind == H_SLAB) {
        if (c == h->cls)
            return ptr;
        goto move;
    }
    if (c < NCLASSES || size > SIZE_MAX - page_size)
        goto move;

    pages = (size + page_size - 1) / page_size;
    if (h->kind == H_HUGE) {
        if (pages > EXT_MAX_PAGES && pages <= h->pages)
            return ptr;
        goto move;
    }
    if (pages <= EXT_MAX_PAGES && extent_resize(h, pages) == 0)
        return ptr;

move:
    old = usable_size(h);
    np = do_alloc(size);
    if (np == NULL)
        return NULL;
    memcpy(np, ptr, old < size ? old : size);
    do_free(ptr);
    return np;
}

void *mem_alloc(size_t size)
{
    void *p;

    if (lazy_init() != 0 || hdr_reserve() != 0)
        return NULL;
    p = do_alloc(size);
    hdr_trim();
    return p;
}

void mem_free(void *ptr)
{
    if (ptr == NULL || !initialized)
        return;
    if (hdr_reserve() != 0)
        return;
    do_free(ptr);
    hdr_trim();
}

void *mem_realloc(void *ptr, size_t size)
{
    void *p;

    if (ptr == NULL)
        return mem_alloc(size);
    if (!initialized || hdr_reserve() != 0)
        return NULL;
    p = do_realloc(ptr, size);
    hdr_trim();
    return p;
}

void mem_show_classes(void)
{
    unsigned c;

    if (lazy_init() != 0)
        return;
    printf("page size %zu, arena %zu pages (%zu KiB)\n",
           page_size, (size_t)ARENA_PAGES, arena_size / 1024);
    printf("class  size  pages  objects  header\n");
    for (c = 0; c < NCLASSES; c++)
        printf("%5u %5zu %6zu %8u  %s%s\n", c, cls_info[c].size, cls_info[c].pages,
               cls_info[c].nobj, cls_info[c].small ? "in slab" : "external",
               c == hdr_cls ? "  <- headers" : "");
    printf("\n");
}

static const char *kind_name(int kind)
{
    switch (kind) {
    case H_FREE: return "free";
    case H_BUSY: return "busy";
    case H_SLAB: return "slab";
    case H_HUGE: return "huge";
    }
    return "?";
}

void mem_show(const char *title)
{
    unsigned a, c;
    size_t page;
    struct hdr *h;
    struct link *l;

    printf("==== %s ====\n", title);
    if (!initialized) {
        printf("  (not initialized)\n\n");
        return;
    }

    for (a = 0; a < narenas; a++) {
        printf("arena %p\n", (void *)arenas[a]);
        for (page = 0; page < ARENA_PAGES; page += h->pages) {
            h = radix_get(page_key(arenas[a]) + page);
            if (h == NULL) {
                printf("  [%4zu] ???\n", page);
                break;
            }
            printf("  [%4zu] %s pages=%-4zu", page, kind_name(h->kind), h->pages);
            if (h->kind == H_SLAB)
                printf(" obj=%-5zu used=%u/%u", cls_info[h->cls].size,
                       h->nobj - h->nfree, h->nobj);
            printf("\n");
        }
    }
    for (l = huge_list.next; l != &huge_list; l = l->next) {
        h = (struct hdr *)l;
        printf("huge %p pages=%zu\n", (void *)h->addr, h->pages);
    }

    printf("caches:");
    for (c = 0; c < NCLASSES; c++)
        if (caches[c].nslabs != 0)
            printf(" %zu:%zu/%zu", cls_info[c].size, caches[c].nslabs, caches[c].nfree);
    printf("  (size:slabs/free objects)\n\n");
}

int mem_check(void)
{
    size_t nslabs[NCLASSES] = {0}, nempty[NCLASSES] = {0};
    size_t free_ext = 0, listed, page, i, words, bits, key0, fr, emp, s;
    unsigned a, c;
    int prev_free;
    struct hdr *h;
    struct link *l, *n;

    if (!initialized)
        return 0;

    for (a = 0; a < narenas; a++) {
        key0 = page_key(arenas[a]);
        prev_free = 0;
        for (page = 0; page < ARENA_PAGES; page += h->pages) {
            h = radix_get(key0 + page);
            if (h == NULL)
                return -1;
            if (h->addr != arenas[a] + page * page_size)
                return -2;
            if (h->pages == 0 || page + h->pages > ARENA_PAGES)
                return -3;
            if (radix_get(key0 + page + h->pages - 1) != h)
                return -4;

            switch (h->kind) {
            case H_FREE:
                if (prev_free)
                    return -5;
                if (!mlb_test(&ext_bm, h->pages - 1))
                    return -6;
                free_ext++;
                prev_free = 1;
                break;
            case H_BUSY:
                prev_free = 0;
                break;
            case H_SLAB:
                prev_free = 0;
                if (h->cls >= NCLASSES || h->pages != cls_info[h->cls].pages)
                    return -7;
                for (i = 0; i < h->pages; i++)
                    if (radix_get(key0 + page + i) != h)
                        return -8;
                words = (h->nobj + 63) / 64;
                bits = 0;
                for (i = 0; i < words; i++)
                    bits += bit_popcount64(h->bitmap[i]);
                if (bits != h->nfree || h->nfree > h->nobj)
                    return -9;
                nslabs[h->cls]++;
                if (h->nfree == h->nobj)
                    nempty[h->cls]++;
                break;
            default:
                return -10;
            }
        }
    }

    listed = 0;
    for (s = 1; s <= ARENA_PAGES; s++) {
        l = ext_list(s, 0);
        if ((l != NULL && !list_empty(l)) != mlb_test(&ext_bm, s - 1))
            return -11;
        if (l == NULL)
            continue;
        for (n = l->next; n != l; n = n->next) {
            h = (struct hdr *)n;
            if (h->kind != H_FREE || h->pages != s)
                return -12;
            listed++;
        }
    }
    if (listed != free_ext)
        return -13;

    for (c = 0; c < NCLASSES; c++) {
        fr = 0;
        emp = 0;
        for (n = caches[c].list.next; n != &caches[c].list; n = n->next) {
            h = (struct hdr *)n;
            if (h->kind != H_SLAB || h->cls != c || h->nfree == 0)
                return -14;
            fr += h->nfree;
            if (h->nfree == h->nobj)
                emp++;
        }
        if (fr != caches[c].nfree || emp != caches[c].nempty)
            return -15;
        if (nslabs[c] != caches[c].nslabs || nempty[c] != emp)
            return -16;
    }
    return 0;
}
