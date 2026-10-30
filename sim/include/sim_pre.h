/* sim_pre.h -- force-included (-include) ahead of every game source in the
 * Windows simulator build. Pulls in the C library and SDL first, then turns
 * on the Vita code paths (__vita__) and routes the Vita's ux0:/app0: paths
 * to the simulator's folder (sim_vita.c). */
#ifndef OPENUG2_SIM_PRE_H
#define OPENUG2_SIM_PRE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <direct.h>
#include <malloc.h>
#include "SDL.h"

#define __vita__ 1

#ifdef __cplusplus
extern "C" {
#endif
extern int g_sim_post;   /* a post-race screen drawn (the script's waitpost) */
const char *sim_path(const char *p, char *buf, size_t cap);
FILE *sim_fopen(const char *p, const char *m);
int   sim_open(const char *p, int flags, ...);
DIR  *sim_opendir(const char *p);
int   sim_mkdir(const char *p, int mode);
int   sim_remove(const char *p);
int   sim_rmdir(const char *p);
int   sim_rename(const char *a, const char *b);
int   sim_stat(const char *p, struct stat *st);
int   sim_scandir(const char *p, struct dirent ***out, int (*filt)(const struct dirent *),
                  int (*cmp)(const struct dirent **, const struct dirent **));
int   alphasort(const struct dirent **a, const struct dirent **b);
#ifdef __cplusplus
}
#endif

#define fopen(p, m)      sim_fopen(p, m)
#define open(...)        sim_open(__VA_ARGS__)
#define opendir(p)       sim_opendir(p)
#define mkdir(p, m)      sim_mkdir(p, m)
#define remove(p)        sim_remove(p)
#define rmdir(p)         sim_rmdir(p)
#define rename(a, b)     sim_rename(a, b)
#define stat(p, b)       sim_stat(p, b)
#define scandir(p, o, f, c) sim_scandir(p, o, f, c)

/* the Vita's heap + kernel block layout, modelled (sim/sim_heap.c) */
#ifdef __cplusplus
extern "C" {
#endif
void *sim_malloc(size_t n);
void *sim_calloc(size_t c, size_t n);
void *sim_realloc(void *q, size_t n);
void  sim_free(void *p);
char *sim_strdup(const char *s);
#ifdef __cplusplus
}
#endif
#define malloc(n)     sim_malloc(n)
#define calloc(c, n)  sim_calloc(c, n)
#define realloc(q, n) sim_realloc(q, n)
#define free(p)       sim_free(p)
#define strdup(s)     sim_strdup(s)

#endif
