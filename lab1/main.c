#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocator.h"
#include "tester.h"

static void demo(void)
{
    char *a, *b, *c, *d, *big, *nptr;

    mem_show("initial");

    a = mem_alloc(100);
    b = mem_alloc(200);
    c = mem_alloc(300);
    d = mem_alloc(1);
    strcpy(a, "hello");
    mem_show("alloc 100, 200, 300, 1");

    mem_free(b);
    mem_show("free 200");

    nptr = mem_realloc(a, 250);
    if (nptr != NULL)
        a = nptr;
    printf("a = \"%s\" after realloc\n", a);
    mem_show("realloc 100 -> 250");

    nptr = mem_realloc(c, 64);
    if (nptr != NULL)
        c = nptr;
    mem_show("realloc 300 -> 64");

    big = mem_alloc(1024 * 1024);
    mem_show("alloc 1 MiB (large arena)");

    mem_free(big);
    mem_free(a);
    mem_free(c);
    mem_show("free large, a, c");

    mem_free(d);
    mem_show("free all");
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
        printf("TEST FAILED\n");
        return 1;
    }
    mem_show("after tester");
    printf("TEST OK\n");
    return 0;
}
