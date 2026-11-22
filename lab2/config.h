#ifndef CONFIG_H
#define CONFIG_H

#define ALIGN 16

#define N_FIRST  8
#define M_GROUP  4
#define K_GROUPS 7
#define NCLASSES (N_FIRST + M_GROUP * K_GROUPS)
#define SIZE_N   (N_FIRST * ALIGN)

#define SMALL_OBJ_MAX 512

#define SLAB_MIN_OBJS  8
#define SLAB_MAX_PAGES 32

#define ARENA_PAGES 4096
#define MAX_ARENAS  256

#define EXT_MAX_PAGES (ARENA_PAGES / 4)

#define EXT_ARRAY_MAX    256
#define EXT_HASH_BUCKETS 64

#define HDR_RESERVE 8

#endif
