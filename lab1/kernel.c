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

void kernel_free(void *ptr, size_t size)
{
    munmap(ptr, size);
}

#endif
