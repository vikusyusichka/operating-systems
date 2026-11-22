#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdio.h>

struct pte {
    unsigned p : 1;
    unsigned r : 1;
    unsigned m : 1;
    unsigned ppn : 29;
};

struct process;

struct phys_page {
    unsigned ppn;
    struct process *owner;
    unsigned vpn;
    uint8_t age;
    struct phys_page *next, *prev;
};

struct process {
    int pid;
    unsigned npages;
    struct pte *page_table;
    unsigned *ws;
    unsigned ws_size;
    unsigned long ws_left;
    unsigned long life_left;
    unsigned long started;
    unsigned long accesses, faults, ws_changes;
};

enum algo { ALGO_RANDOM, ALGO_AGING };

struct sim_params {
    enum algo algo;
    unsigned ws_min, ws_max;
    unsigned long seed;
    unsigned long accesses;
    int verbose;
    FILE *log;
};

struct sim_result {
    unsigned long accesses, faults, replacements, writebacks;
    unsigned long procs_created, procs_finished;
};

void sim_run(const struct sim_params *prm, struct sim_result *res);
const char *algo_name(enum algo a);

#endif
