#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "vm.h"

struct rng {
    uint64_t s;
};

static unsigned rnd(struct rng *g)
{
    g->s ^= g->s << 13;
    g->s ^= g->s >> 7;
    g->s ^= g->s << 17;
    return (unsigned)(g->s >> 33);
}

static unsigned rnd_range(struct rng *g, unsigned lo, unsigned hi)
{
    return lo + rnd(g) % (hi - lo + 1);
}

static struct {
    const struct sim_params *prm;
    struct sim_result *res;
    struct rng work, algo;

    struct phys_page phys[PHYS_PAGES];
    struct phys_page free_list, busy_list;
    unsigned nfree;
    unsigned daemon_pos;

    struct process *procs[MAX_PROCS];
    unsigned nprocs;
    int next_pid;
    unsigned long now;
    int events_left;
} S;

const char *algo_name(enum algo a)
{
    return a == ALGO_AGING ? "aging" : "random";
}

static void list_init(struct phys_page *l)
{
    l->next = l;
    l->prev = l;
}

static void list_del(struct phys_page *pg)
{
    pg->prev->next = pg->next;
    pg->next->prev = pg->prev;
}

static void list_add_tail(struct phys_page *l, struct phys_page *pg)
{
    pg->prev = l->prev;
    pg->next = l;
    l->prev->next = pg;
    l->prev = pg;
}

static int log_event(void)
{
    if (S.prm->log == NULL || S.events_left <= 0)
        return 0;
    S.events_left--;
    fprintf(S.prm->log, "[%7lu] ", S.now);
    return 1;
}

static void new_working_set(struct process *p)
{
    char *used = calloc(p->npages, 1);
    unsigned i, v;

    for (i = 0; i < p->ws_size; i++) {
        do {
            v = rnd(&S.work) % p->npages;
        } while (used[v]);
        used[v] = 1;
        p->ws[i] = v;
    }
    free(used);
    p->ws_left = rnd_range(&S.work, WS_CHANGE_MIN, WS_CHANGE_MAX);
}

static void proc_create(void)
{
    struct process *p = calloc(1, sizeof(*p));

    p->pid = S.next_pid++;
    p->npages = rnd_range(&S.work, VSPACE_MIN, VSPACE_MAX);
    p->page_table = calloc(p->npages, sizeof(struct pte));
    p->ws_size = rnd_range(&S.work, S.prm->ws_min, S.prm->ws_max);
    if (p->ws_size > p->npages)
        p->ws_size = p->npages;
    p->ws = calloc(p->ws_size, sizeof(unsigned));
    p->life_left = rnd_range(&S.work, LIFETIME_MIN, LIFETIME_MAX);
    p->started = S.now;
    new_working_set(p);

    S.procs[S.nprocs++] = p;
    S.res->procs_created++;

    if (S.prm->log != NULL)
        fprintf(S.prm->log, "[%7lu] create pid %d: vspace %u pages, ws %u pages, life %lu\n",
                S.now, p->pid, p->npages, p->ws_size, p->life_left);
}

static void proc_exit(unsigned idx, int finished)
{
    struct process *p = S.procs[idx];
    unsigned v, mapped = 0;
    struct phys_page *pg;

    for (v = 0; v < p->npages; v++) {
        if (!p->page_table[v].p)
            continue;
        pg = &S.phys[p->page_table[v].ppn];
        list_del(pg);
        pg->owner = NULL;
        list_add_tail(&S.free_list, pg);
        S.nfree++;
        mapped++;
    }

    if (S.prm->log != NULL)
        fprintf(S.prm->log,
                "[%7lu] %s pid %d: vspace %u, ws %u, accesses %lu, faults %lu (%.2f%%), "
                "ws changes %lu, resident %u\n",
                S.now, finished ? "exit" : "stop", p->pid, p->npages, p->ws_size,
                p->accesses, p->faults,
                p->accesses ? 100.0 * p->faults / p->accesses : 0.0,
                p->ws_changes, mapped);

    if (finished)
        S.res->procs_finished++;
    free(p->page_table);
    free(p->ws);
    free(p);

    memmove(&S.procs[idx], &S.procs[idx + 1], (S.nprocs - idx - 1) * sizeof(S.procs[0]));
    S.nprocs--;
}

static struct pte *page_pte(struct phys_page *pg)
{
    return &pg->owner->page_table[pg->vpn];
}

static void daemon_tick(void)
{
    unsigned i;
    struct phys_page *pg;
    struct pte *e;

    for (i = 0; i < DAEMON_BATCH; i++) {
        pg = &S.phys[S.daemon_pos];
        S.daemon_pos = (S.daemon_pos + 1) % PHYS_PAGES;
        if (pg->owner == NULL)
            continue;
        e = page_pte(pg);
        pg->age = (uint8_t)((pg->age >> 1) | (e->r << 7));
        e->r = 0;
    }
}

static struct phys_page *victim_random(void)
{
    return &S.phys[rnd(&S.algo) % PHYS_PAGES];
}

static struct phys_page *victim_aging(void)
{
    struct phys_page *pg, *best = NULL;
    unsigned key, best_key = ~0u;
    struct pte *e;

    for (pg = S.busy_list.next; pg != &S.busy_list; pg = pg->next) {
        e = page_pte(pg);
        key = ((unsigned)e->r << 9) | ((unsigned)pg->age << 1) | e->m;
        if (key < best_key) {
            best_key = key;
            best = pg;
        }
    }
    return best;
}

static void evict(struct phys_page *pg)
{
    struct pte *e = page_pte(pg);

    if (log_event())
        fprintf(S.prm->log, "  replace ppn %u: pid %d vpn %u age 0x%02x R=%u M=%u%s\n",
                pg->ppn, pg->owner->pid, pg->vpn, pg->age, e->r, e->m,
                e->m ? " -> write back" : "");

    if (e->m)
        S.res->writebacks++;
    e->p = 0;
    e->r = 0;
    e->m = 0;
    list_del(pg);
    pg->owner = NULL;
    S.res->replacements++;
}

static void page_fault(struct process *p, unsigned vpn)
{
    struct phys_page *pg;
    struct pte *e = &p->page_table[vpn];

    p->faults++;
    S.res->faults++;

    if (S.nfree > 0) {
        pg = S.free_list.next;
        list_del(pg);
        S.nfree--;
    } else {
        pg = S.prm->algo == ALGO_AGING ? victim_aging() : victim_random();
        if (log_event())
            fprintf(S.prm->log, "fault pid %d vpn %u -> no free pages\n", p->pid, vpn);
        evict(pg);
    }

    pg->owner = p;
    pg->vpn = vpn;
    pg->age = 0;
    list_add_tail(&S.busy_list, pg);

    e->ppn = pg->ppn;
    e->p = 1;
}

static void memory_access(struct process *p)
{
    unsigned vpn;
    int write;
    struct pte *e;

    if (rnd(&S.work) % 100 < WS_HIT_PERCENT)
        vpn = p->ws[rnd(&S.work) % p->ws_size];
    else
        vpn = rnd(&S.work) % p->npages;
    write = rnd(&S.work) % 100 < WRITE_PERCENT;

    e = &p->page_table[vpn];
    if (!e->p)
        page_fault(p, vpn);
    e->r = 1;
    if (write)
        e->m = 1;

    p->accesses++;
    S.res->accesses++;
}

void sim_run(const struct sim_params *prm, struct sim_result *res)
{
    unsigned long next_spawn;
    unsigned cur = 0, q, i;
    struct process *p;

    memset(&S, 0, sizeof(S));
    memset(res, 0, sizeof(*res));
    S.prm = prm;
    S.res = res;
    S.work.s = prm->seed * 2654435761u + 1;
    S.algo.s = prm->seed * 40503u + 7;
    S.next_pid = 1;
    S.events_left = prm->verbose;

    list_init(&S.free_list);
    list_init(&S.busy_list);
    for (i = 0; i < PHYS_PAGES; i++) {
        S.phys[i].ppn = i;
        list_add_tail(&S.free_list, &S.phys[i]);
    }
    S.nfree = PHYS_PAGES;

    next_spawn = 0;
    while (res->accesses < prm->accesses) {
        if (S.now >= next_spawn) {
            if (S.nprocs < MAX_PROCS)
                proc_create();
            next_spawn = S.now + rnd_range(&S.work, SPAWN_MIN, SPAWN_MAX);
        }
        if (S.nprocs == 0) {
            S.now = next_spawn;
            continue;
        }

        if (cur >= S.nprocs)
            cur = 0;
        p = S.procs[cur];

        for (q = 0; q < QUANTUM && res->accesses < prm->accesses; q++) {
            memory_access(p);
            S.now++;
            if (S.now % DAEMON_PERIOD == 0)
                daemon_tick();

            if (--p->ws_left == 0) {
                new_working_set(p);
                p->ws_changes++;
            }
            if (--p->life_left == 0)
                break;
        }

        if (p->life_left == 0)
            proc_exit(cur, 1);
        else
            cur++;
    }

    while (S.nprocs > 0)
        proc_exit(0, 0);
}
