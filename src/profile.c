/* profile.c -- the player profile and its save files (see profile.h). */
#include "profile.h"
#include "options.h"
#include "career.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __vita__
#define PROFILE_DIR "ux0:data/NFSU2/profiles"
#define SCORES_FILE "ux0:data/NFSU2/scores.bin"
#else
#define PROFILE_DIR "profiles"
#define SCORES_FILE "scores.bin"
#endif

/* the realmc container (0x6fc226): "20CM", file size, the sizes of block A
   and the payload, their CRCs, the header's CRC; block A {version, payload
   size}. The retail payload is version 0x10d, 0xd692 bytes; this port's
   own (the working files) carries its own version so neither loads the
   other's. */
#define RMC_MAGIC 0x4d433032u
#define RMC_HDR 0x24
#define PROFILE_VERSION 0x4f55010du

static char g_name[32];
static int g_named;

/* HighScoresDatabase (0x844adc): 0x20 x 10 entries of 8 bytes, then the
   race statistics (0x14 dwords, not kept by this port yet) */
#define HS_N 320
typedef struct { int16_t track; uint8_t kind, dir; int32_t value; } HsEntry;
static HsEntry g_hs[HS_N];

static unsigned char *g_snap; static long g_snaplen;

/* ------------------------------------------------------------------ CRC */

/* 0x6fe173: MSB first, poly 0x04c11db7 (table 0x8145e0), seeded with the
   complement of the first four bytes big-endian, the result complemented */
static uint32_t rmc_crc(const unsigned char *p, long n) {
    static uint32_t t[256]; static int ok;
    if (!ok) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; k++) c = c & 0x80000000u ? (c << 1) ^ 0x04c11db7u : c << 1;
            t[i] = c;
        }
        ok = 1;
    }
    if (n < 4) return 0;
    uint32_t crc = ~((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]);
    for (long i = 4; i < n; i++) crc = ((crc << 8) | p[i]) ^ t[crc >> 24];
    return ~crc;
}

/* ------------------------------------------------------------- files */

static unsigned char *read_file(const char *path, long *len) {
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (b && n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = n;
    return b;
}
static int write_file(const char *path, const void *p, long n) {
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = n <= 0 || fwrite(p, 1, (size_t)n, f) == (size_t)n;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    return rename(tmp, path) == 0;
}

static void scores_save(void) { write_file(SCORES_FILE, g_hs, sizeof g_hs); }
static void scores_load(void) {
    long n; unsigned char *b = read_file(SCORES_FILE, &n);
    memset(g_hs, 0, sizeof g_hs);
    if (b && n == (long)sizeof g_hs) memcpy(g_hs, b, sizeof g_hs);
    free(b);
}

/* ------------------------------------------------------ the working set */

/* tagged blocks: NAME, OPTN (N2Options, its size first), GRGE (garage.txt),
   HSCR (the score table), CCAR (the career cars: FEPlayerCarDB, which
   UserProfile 0x52a280 saves just before the career), CARR (the career) */
typedef struct { unsigned char *p; long n, cap; } Buf;
static void put(Buf *b, const void *p, long n) {
    if (b->n + n > b->cap) {
        long c = b->cap ? b->cap : 4096;
        while (c < b->n + n) c *= 2;
        unsigned char *q = (unsigned char *)realloc(b->p, (size_t)c);
        if (!q) return;
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, p, (size_t)n); b->n += n;
}
static void put_block(Buf *b, const char tag[4], const void *p, long n) {
    uint32_t l = (uint32_t)n;
    put(b, tag, 4); put(b, &l, 4);
    if (n > 0) put(b, p, n);
}
static unsigned char *serialize(long *len) {
    Buf b = {0};
    char nm[32]; memset(nm, 0, sizeof nm); memcpy(nm, g_name, sizeof nm - 1);
    put_block(&b, "NAME", nm, sizeof nm);
    {   uint32_t sz = sizeof g_opt;
        unsigned char o[sizeof sz + sizeof g_opt];
        memcpy(o, &sz, sizeof sz); memcpy(o + sizeof sz, &g_opt, sizeof g_opt);
        put_block(&b, "OPTN", o, sizeof o); }
    long gl; unsigned char *g = read_file(PROFILE_GARAGE_FILE, &gl);
    put_block(&b, "GRGE", g, g ? gl : 0);
    free(g);
    put_block(&b, "HSCR", g_hs, sizeof g_hs);
    {   long cl = career_cars_save(NULL, 0);        /* FEPlayerCarDB 0x5166b0 (the career slots) */
        unsigned char *cb = (unsigned char *)malloc((size_t)cl);
        if (cb) { career_cars_save(cb, cl); put_block(&b, "CCAR", cb, cl); free(cb); } }
    {   long cl = career_save(NULL, 0);             /* PlayerCareerState 0x514530 */
        unsigned char *cb = (unsigned char *)malloc((size_t)cl);
        if (cb) { career_save(cb, cl); put_block(&b, "CARR", cb, cl); free(cb); } }
    *len = b.n;
    return b.p;
}
/* the blocks back into the working files; 1 ok */
static int apply(const unsigned char *p, long n) {
    const unsigned char *opt = NULL, *gar = NULL, *hs = NULL, *car = NULL, *cc = NULL;
    long optn = 0, garn = 0, hsn = 0, carn = 0, ccn = 0;
    for (long i = 0; i + 8 <= n; ) {
        uint32_t l; memcpy(&l, p + i + 4, 4);
        if (i + 8 + (long)l > n) return 0;
        const unsigned char *d = p + i + 8;
        if (!memcmp(p + i, "OPTN", 4)) { opt = d; optn = l; }
        else if (!memcmp(p + i, "GRGE", 4)) { gar = d; garn = l; }
        else if (!memcmp(p + i, "HSCR", 4)) { hs = d; hsn = l; }
        else if (!memcmp(p + i, "CARR", 4)) { car = d; carn = l; }
        else if (!memcmp(p + i, "CCAR", 4)) { cc = d; ccn = l; }
        i += 8 + l;
    }
    uint32_t sz = 0;
    if (opt && optn >= 4) memcpy(&sz, opt, 4);
    if (opt && sz == sizeof g_opt && optn == (long)(4 + sz)) memcpy(&g_opt, opt + 4, sizeof g_opt);
    else printf("profile: options from another build, kept\n");
    options_save();
    if (gar && garn > 0) write_file(PROFILE_GARAGE_FILE, gar, garn);
    else remove(PROFILE_GARAGE_FILE);
    memset(g_hs, 0, sizeof g_hs);
    if (hs && hsn == (long)sizeof g_hs) memcpy(g_hs, hs, sizeof g_hs);
    scores_save();
    /* LoadFromBuffer 80131f28: Default(true) (the cars defaulted, slot 0
       unlocked), the car DB, the career; here the career load defaults
       first, then the cars go over it */
    if (!car || !career_load(car, carn)) career_default();
    if (cc && !career_cars_load(cc, ccn)) printf("profile: career cars from another build, defaulted\n");
    return 1;
}

/* ------------------------------------------------------------- profile */

void profile_init(void) {
    mkdir(PROFILE_DIR, 0777);
    scores_load();
    if (!g_named) g_opt.autosave = 0;   /* unnamed at boot: autosave needs a name (0x49b170) */
}
int profile_named(void) { return g_named; }
const char *profile_name(void) { return g_named ? g_name : ""; }
void profile_set_name(const char *name) {
    snprintf(g_name, sizeof g_name, "%s", name ? name : "");
    g_named = g_name[0] != 0;
    if (!g_named) g_opt.autosave = 0;     /* 0x49b170: autosave needs a name */
}

static int name_ok(const char *s) {     /* MC_List's filter: < 8 chars, no space */
    size_t l = strlen(s);
    if (!l || l > PROFILE_NAME_MAX || strchr(s, ' ') || s[0] == '.') return 0;
    return 1;
}
static void profile_path(const char *name, char *out, int cap) {
    snprintf(out, (size_t)cap, "%s/%s/%s", PROFILE_DIR, name, name);
}
int profile_file(const char *file, char *out, int cap) {
    if (!g_named || !file) return 0;
    char dir[300];
    snprintf(dir, sizeof dir, "%s/%s", PROFILE_DIR, g_name);
    mkdir(PROFILE_DIR, 0777); mkdir(dir, 0777);
    snprintf(out, (size_t)cap, "%s/%s", dir, file);
    return 1;
}
int profile_exists(const char *name) {
    char p[300]; profile_path(name, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}
int profile_list(char names[][PROFILE_NAME_MAX + 1], int cap) {
    int n = 0;
    DIR *d = opendir(PROFILE_DIR);
    struct dirent *e;
    while (d && n < cap && (e = readdir(d))) {
        if (!name_ok(e->d_name) || !profile_exists(e->d_name)) continue;
        snprintf(names[n++], PROFILE_NAME_MAX + 1, "%s", e->d_name);
    }
    if (d) closedir(d);
    for (int i = 1; i < n; i++)            /* FindFirst order: by name */
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[PROFILE_NAME_MAX + 1];
            memcpy(t, names[j], sizeof t); memcpy(names[j], names[j - 1], sizeof t); memcpy(names[j - 1], t, sizeof t);
        }
    return n;
}

static void put32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }

int profile_save(void) {
    if (!g_named) return 0;
    long pn; unsigned char *pay = serialize(&pn);
    if (!pay) return 0;
    long total = RMC_HDR + pn;
    unsigned char *f = (unsigned char *)malloc((size_t)total);
    if (!f) { free(pay); return 0; }
    memset(f, 0, RMC_HDR);
    put32(f + 0x1c, PROFILE_VERSION); put32(f + 0x20, (uint32_t)pn);
    memcpy(f + RMC_HDR, pay, (size_t)pn);
    put32(f + 0x00, RMC_MAGIC); put32(f + 0x04, (uint32_t)total);
    put32(f + 0x08, 8); put32(f + 0x0c, (uint32_t)pn);
    put32(f + 0x10, rmc_crc(f + 0x1c, 8));
    put32(f + 0x14, rmc_crc(f + RMC_HDR, pn));
    put32(f + 0x18, rmc_crc(f, 0x18));
    char dir[300], path[300];
    snprintf(dir, sizeof dir, "%s/%s", PROFILE_DIR, g_name);
    mkdir(PROFILE_DIR, 0777); mkdir(dir, 0777);
    profile_path(g_name, path, sizeof path);
    int ok = write_file(path, f, total);
    free(f); free(pay);
    printf("profile: %s %s (%ld bytes)\n", ok ? "saved" : "cannot save", g_name, total);
    return ok;
}

int profile_load(const char *name) {
    char path[300]; profile_path(name, path, sizeof path);
    long n; unsigned char *f = read_file(path, &n);
    int ok = f && n >= RMC_HDR && get32(f) == RMC_MAGIC && get32(f + 4) == (uint32_t)n &&
             get32(f + 8) == 8 && get32(f + 0xc) == (uint32_t)(n - RMC_HDR) &&
             get32(f + 0x18) == rmc_crc(f, 0x18) && get32(f + 0x10) == rmc_crc(f + 0x1c, 8) &&
             get32(f + 0x14) == rmc_crc(f + RMC_HDR, n - RMC_HDR) &&
             get32(f + 0x1c) == PROFILE_VERSION && get32(f + 0x20) == (uint32_t)(n - RMC_HDR);
    if (ok) ok = apply(f + RMC_HDR, n - RMC_HDR);
    free(f);
    if (ok) profile_set_name(name);       /* 0x5344d0: named = 1 */
    printf("profile: %s %s\n", ok ? "loaded" : "cannot load", name);
    return ok;
}

int profile_delete(const char *name) {
    char path[300], dir[300];
    profile_path(name, path, sizeof path);
    snprintf(dir, sizeof dir, "%s/%s", PROFILE_DIR, name);
    int ok = remove(path) == 0;
    rmdir(dir);
    if (ok && g_named && !strcmp(name, g_name)) profile_default();
    printf("profile: %s %s\n", ok ? "deleted" : "cannot delete", name);
    return ok;
}

void profile_default(void) {
    remove(PROFILE_GARAGE_FILE);
    memset(g_hs, 0, sizeof g_hs);
    scores_save();
    career_default();
    profile_set_name("");
    options_save();
}

void profile_snapshot(void) {
    free(g_snap);
    g_snap = serialize(&g_snaplen);
}
int profile_dirty(void) {
    if (!g_snap) return 0;
    long n; unsigned char *b = serialize(&n);
    int d = !b || n != g_snaplen || memcmp(b, g_snap, (size_t)n);
    free(b);
    return d;
}
void profile_restore(void) {   /* the name too: a cancelled "lose changes" is the old profile again */
    if (!g_snap) return;
    apply(g_snap, g_snaplen);
    if (g_snaplen >= 8 + 32 && !memcmp(g_snap, "NAME", 4)) {
        char nm[32]; memcpy(nm, g_snap + 8, sizeof nm); nm[sizeof nm - 1] = 0;
        profile_set_name(nm);
    }
}
void profile_drop_snapshot(void) { free(g_snap); g_snap = NULL; g_snaplen = 0; }

/* ---------------------------------------------------------- high scores */

int hs_best_time(int track, int reverse, const unsigned char *rec) {
    int best = 0x7fffffff, found = 0;
    for (int i = 0; i < HS_N; i++)
        if (g_hs[i].track == track && g_hs[i].dir == reverse && (g_hs[i].value < best || !found)) {
            found = 1; best = g_hs[i].value;
        }
    if (!found && rec) {   /* Timer::SetTime(ms * 0.001 * 4000 + 0.5), truncated (0x75bed4) */
        float ms; memcpy(&ms, rec + (reverse ? 0xa0 : 0x9c), 4);
        best = (int)(ms * 0.001f * 4000.0f + 0.5f);
    }
    return best;
}
int hs_best_drift(int track, int reverse) {
    int best = 0;
    for (int i = 0; i < HS_N; i++)
        if (g_hs[i].track == track && g_hs[i].dir == reverse && best < g_hs[i].value) best = g_hs[i].value;
    return best;
}
int hs_commit(int kind, int track, int reverse, int value) {
    /* FindHighScore: the entry for (track, direction, kind) */
    HsEntry *e = NULL;
    for (int i = 0; i < HS_N && !e; i++)
        if (g_hs[i].track == track && g_hs[i].dir == reverse && g_hs[i].kind == kind) e = &g_hs[i];
    int valid = kind == HS_DRIFT ? value > 0 : value != 0 && value != 0x7fffffff;
    int rec = 0;
    if (e) rec = valid && (kind == HS_DRIFT ? e->value < value : value < e->value);  /* DidPlayerBreakRecord */
    else if (valid) {
        for (int i = 0; i < HS_N && !e; i++) if (!g_hs[i].track) e = &g_hs[i];   /* GetEmptyHighScore */
        if (e) { e->track = (int16_t)track; e->dir = (uint8_t)reverse; e->kind = (uint8_t)kind; rec = 1; }
    }
    if (rec) {
        e->value = value;
        scores_save();
        printf("high score: track %d %s %s %d\n", track, reverse ? "reverse" : "forward",
               kind == HS_DRIFT ? "drift" : "time", value);
    }
    return rec;
}
