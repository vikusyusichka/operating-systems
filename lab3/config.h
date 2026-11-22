#ifndef CONFIG_H
#define CONFIG_H

#ifndef PHYS_PAGES
#define PHYS_PAGES 256
#endif

#ifndef MAX_PROCS
#define MAX_PROCS 8
#endif

#ifndef VSPACE_MIN
#define VSPACE_MIN 32
#endif
#ifndef VSPACE_MAX
#define VSPACE_MAX 256
#endif

#ifndef LIFETIME_MIN
#define LIFETIME_MIN 5000
#endif
#ifndef LIFETIME_MAX
#define LIFETIME_MAX 20000
#endif

#ifndef WS_CHANGE_MIN
#define WS_CHANGE_MIN 200
#endif
#ifndef WS_CHANGE_MAX
#define WS_CHANGE_MAX 1000
#endif

#ifndef SPAWN_MIN
#define SPAWN_MIN 500
#endif
#ifndef SPAWN_MAX
#define SPAWN_MAX 3000
#endif

#ifndef QUANTUM
#define QUANTUM 50
#endif

#ifndef WS_HIT_PERCENT
#define WS_HIT_PERCENT 90
#endif
#ifndef WRITE_PERCENT
#define WRITE_PERCENT 30
#endif

#ifndef DAEMON_PERIOD
#define DAEMON_PERIOD 16
#endif
#ifndef DAEMON_BATCH
#define DAEMON_BATCH 32
#endif

#ifndef SIM_ACCESSES
#define SIM_ACCESSES 200000
#endif

#endif
