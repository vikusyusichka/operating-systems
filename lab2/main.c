#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocator.h"
#include "tester.h"

static void demo(void)
{
    char *small[20], *big, *ext, *huge, *nptr;
    int i;

    mem_show_classes();

    for (i = 0; i < 20; i++)
        small[i] = mem_alloc(40);
    strcpy(small[0], "hello");
    mem_show("20 x alloc 40 (class 48)");

    big = mem_alloc(3000);
    mem_show("alloc 3000 (class 3072, header outside the slab)");

    ext = mem_alloc(100 * 1024);
    mem_show("alloc 100 KiB (extent of 25 pages)");

    nptr = mem_realloc(ext, 200 * 1024);
    if (nptr != NULL)
        ext = nptr;
    mem_show("realloc extent 100 KiB -> 200 KiB (in place)");

    nptr = mem_realloc(ext, 60 * 1024);
    if (nptr != NULL)
        ext = nptr;
    mem_show("realloc extent 200 KiB -> 60 KiB (in place)");

    huge = mem_alloc(8 * 1024 * 1024);
    mem_show("alloc 8 MiB (extent not from arena)");

    nptr = mem_realloc(small[0], 100);
    if (nptr != NULL)
        small[0] = nptr;
    printf("small[0] = \"%s\" after realloc 40 -> 100\n\n", small[0]);

    for (i = 0; i < 20; i++)
        mem_free(small[i]);
    mem_free(big);
    mem_free(ext);
    mem_free(huge);
    mem_show("free all");

    printf("mem_check() = %d\n", mem_check());
}

int main(int argc, char **argv)
{
    unsigned long iters = 20000, seed = 12345;

    if (argc > 1 && strcmp(argv[1], "demo") == 0) {
        demo();
        return 0;
    }
    if (argc > 1)
        iters = strtoul(argv[1], NULL, 10);
    if (argc > 2)
        seed = strtoul(argv[2], NULL, 10);

    if (run_tester(iters, seed) != 0) {
        mem_show("broken state");
        printf("TEST FAILED\n");
        return 1;
    }
    mem_show("after tester");
    printf("TEST OK\n");
    return 0;
}
