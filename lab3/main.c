#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "vm.h"

#define SEEDS 5

static void usage(void)
{
    printf("usage:\n"
           "  lab3                                     experiment: fault rate vs working set size\n"
           "  lab3 run <random|aging> <ws_min> <ws_max> [seed] [events]\n");
}

static void print_result(enum algo a, const struct sim_result *r)
{
    printf("\nalgorithm %s\n", algo_name(a));
    printf("  processes     created %lu, finished %lu\n", r->procs_created, r->procs_finished);
    printf("  accesses      %lu\n", r->accesses);
    printf("  page faults   %lu (%.2f%%)\n", r->faults, 100.0 * r->faults / r->accesses);
    printf("  replacements  %lu\n", r->replacements);
    printf("  write backs   %lu\n", r->writebacks);
}

static int run_one(int argc, char **argv)
{
    struct sim_params prm;
    struct sim_result res;

    if (argc < 5) {
        usage();
        return 1;
    }
    memset(&prm, 0, sizeof(prm));
    prm.algo = strcmp(argv[2], "aging") == 0 ? ALGO_AGING : ALGO_RANDOM;
    prm.ws_min = (unsigned)atoi(argv[3]);
    prm.ws_max = (unsigned)atoi(argv[4]);
    prm.seed = argc > 5 ? strtoul(argv[5], NULL, 10) : 1;
    prm.verbose = argc > 6 ? atoi(argv[6]) : 40;
    prm.accesses = SIM_ACCESSES;
    prm.log = stdout;

    if (prm.ws_min == 0 || prm.ws_max < prm.ws_min) {
        printf("bad working set range\n");
        return 1;
    }

    printf("physical pages %d, max processes %d, %lu accesses, ws %u..%u, seed %lu\n\n",
           PHYS_PAGES, MAX_PROCS, prm.accesses, prm.ws_min, prm.ws_max, prm.seed);
    sim_run(&prm, &res);
    print_result(prm.algo, &res);
    return 0;
}

static double avg_rate(enum algo a, unsigned lo, unsigned hi, double *wb)
{
    struct sim_params prm;
    struct sim_result res;
    double sum = 0, wsum = 0;
    int s;

    memset(&prm, 0, sizeof(prm));
    prm.algo = a;
    prm.ws_min = lo;
    prm.ws_max = hi;
    prm.accesses = SIM_ACCESSES;
    for (s = 1; s <= SEEDS; s++) {
        prm.seed = (unsigned long)s;
        sim_run(&prm, &res);
        sum += 100.0 * res.faults / res.accesses;
        wsum += 100.0 * res.writebacks / res.accesses;
    }
    *wb = wsum / SEEDS;
    return sum / SEEDS;
}

static void experiment(void)
{
    static const unsigned ws[] = {2, 4, 6, 8, 10, 12, 16, 24, 32};
    unsigned i, lo, hi;
    double r, a, wr, wa;

    printf("physical pages %d, max processes %d, %d accesses per run, %d seeds\n\n",
           PHYS_PAGES, MAX_PROCS, SIM_ACCESSES, SEEDS);
    printf("  ws range | random faults | aging faults | random wb | aging wb\n");
    printf("  ---------+---------------+--------------+-----------+---------\n");
    for (i = 0; i < sizeof(ws) / sizeof(ws[0]); i++) {
        lo = ws[i] - ws[i] / 4;
        hi = ws[i] + ws[i] / 4;
        r = avg_rate(ALGO_RANDOM, lo, hi, &wr);
        a = avg_rate(ALGO_AGING, lo, hi, &wa);
        printf("  %3u..%-3u | %12.2f%% | %11.2f%% | %8.2f%% | %7.2f%%\n", lo, hi, r, a, wr, wa);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "run") == 0)
        return run_one(argc, argv);
    if (argc > 1) {
        usage();
        return 1;
    }
    experiment();
    return 0;
}
