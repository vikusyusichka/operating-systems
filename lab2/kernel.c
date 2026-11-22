#include <stdint.h>

#include "kernel.h"

#ifdef _WIN32
#include <windows.h>

size_t kernel_page_size(void)
{
    static size_t page = 0;
    SYSTEM_INFO si;

    if (page == 0) {
        GetSystemInfo(&si);
        page = si.dwPageSize;
    }
    return page;
}

void *kernel_alloc(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void *kernel_alloc_aligned(size_t size, size_t align)
{
    void *p, *q;
    uintptr_t a;
    int i;

    if (size > SIZE_MAX - align)
        return NULL;

    for (i = 0; i < 16; i++) {
        p = VirtualAlloc(NULL, size + align, MEM_RESERVE, PAGE_NOACCESS);
        if (p == NULL)
            return NULL;
        a = ((uintptr_t)p + align - 1) & ~(uintptr_t)(align - 1);
        VirtualFree(p, 0, MEM_RELEASE);
        q = VirtualAlloc((void *)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (q != NULL)
            return q;
    }
    return NULL;
}

void kernel_free(void *ptr, size_t size)
{
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
}

#else
#include <sys/mman.h>
#include <unistd.h>

size_t kernel_page_size(void)
{
    static size_t page = 0;

    if (page == 0)
        page = (size_t)sysconf(_SC_PAGESIZE);
    return page;
}

void *kernel_alloc(size_t size)
{
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
        return NULL;
    return ptr;
}

void *kernel_alloc_aligned(size_t size, size_t align)
{
    char *p;
    uintptr_t a;
    size_t head, tail;

    if (size > SIZE_MAX - align)
        return NULL;
    p = kernel_alloc(size + align);
    if (p == NULL)
        return NULL;

    a = ((uintptr_t)p + align - 1) & ~(uintptr_t)(align - 1);
    head = a - (uintptr_t)p;
    tail = align - head;
    if (head != 0)
        munmap(p, head);
    if (tail != 0)
        munmap((char *)a + size, tail);
    return (void *)a;
}

void kernel_free(void *ptr, size_t size)
{
    munmap(ptr, size);
}

#endif
