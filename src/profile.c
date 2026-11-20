/* profile.c -- the player profile and its save files (see profile.h). */
#include "profile.h"
#include "savenote.h"
#include "options.h"
#include "career.h"
#include "trophy.h"
#include "beyond.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
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

/* a save cut off between its two renames (write_file) left the old file as
   <path>.bak: put it back (audit 11-17) */
static void file_recover(const char *path) {
    char bak[300]; struct stat st;
    if (stat(path, &st) == 0) return;
    snprintf(bak, sizeof bak, "%s.savebak", path);
    if (stat(bak, &st) == 0 && rename(bak, path) == 0) printf("profile: %s restored from its .savebak\n", path);
}
static unsigned char *read_file(const char *path, long *len) {
    *len = 0;
    file_recover(path);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }   /* not a length of -1 */
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
    /* the old file stays whole as .bak until the new one is in place: a power
       cut between the renames leaves .bak, which file_recover puts back */
    char bak[300]; struct stat st;
    snprintf(bak, sizeof bak, "%s.savebak", path);
    remove(bak);
    int had = stat(path, &st) == 0;
    if (had && rename(path, bak) != 0) { remove(tmp); return 0; }
    if (rename(tmp, path) != 0) {
        if (had) rename(bak, path);
        remove(tmp);
        return 0;
    }
    if (had) remove(bak);
    return 1;
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
typedef struct { unsigned char *p; long n, cap; int fail; } Buf;
static void put(Buf *b, const void *p, long n) {
    if (b->n + n > b->cap) {
        long c = b->cap ? b->cap : 4096;
        while (c < b->n + n) c *= 2;
        unsigned char *q = (unsigned char *)realloc(b->p, (size_t)c);
        if (!q) { b->fail = 1; return; }
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, p, (size_t)n); b->n += n;
}
static void put_block(Buf *b, const char tag[4], const void *p, long n) {
    uint32_t l = (uint32_t)n;
    put(b, tag, 4); put(b, &l, 4);
    if (n > 0) put(b, p, n);
}
/* World Expansion "Start fresh" (port): while a city's own career is in, the Bayview
   career (its CARR / CCAR blocks) waits here and is what the profile saves */
static unsigned char *g_xf_car, *g_xf_cc; static long g_xf_carn, g_xf_ccn; static int g_xf_city, g_xf_wins;
static unsigned char *serialize(long *len) {
    Buf b = {0};
    char nm[32]; memset(nm, 0, sizeof nm); memcpy(nm, g_name, sizeof nm - 1);
    put_block(&b, "NAME", nm, sizeof nm);
    {   uint32_t sz = sizeof g_opt;
        unsigned char o[sizeof sz + sizeof g_opt];
        memcpy(o, &sz, sizeof sz); memcpy(o + sizeof sz, &g_opt, sizeof g_opt);
        put_block(&b, "OPTN", o, sizeof o); }
    long gl; unsigned char *g = read_file(PROFILE_GARAGE_FILE, &gl);
    /* GitHub #18: a garage file there but unreadable (out of memory) is not
       an empty garage -- saved as one, the next load lost every tuned car */
    {   struct stat st;   /* stat, not fopen: the open itself needs memory (garage_read) */
        if (!g && stat(PROFILE_GARAGE_FILE, &st) == 0) b.fail = 1; }
    put_block(&b, "GRGE", g, g ? gl : 0);
    free(g);
    put_block(&b, "HSCR", g_hs, sizeof g_hs);
    if (g_xf_city) {   /* a city's own career is in: the Bayview one, as it waits */
        put_block(&b, "CCAR", g_xf_cc, g_xf_ccn);
        put_block(&b, "CARR", g_xf_car, g_xf_carn);
    } else {
    {   long cl = career_cars_save(NULL, 0);        /* FEPlayerCarDB 0x5166b0 (the career slots) */
        unsigned char *cb = (unsigned char *)malloc((size_t)cl);
        if (cb) { career_cars_save(cb, cl); put_block(&b, "CCAR", cb, cl); free(cb); } else b.fail = 1; }
    {   long cl = career_save(NULL, 0);             /* PlayerCareerState 0x514530 */
        unsigned char *cb = (unsigned char *)malloc((size_t)cl);
        if (cb) { career_save(cb, cl); put_block(&b, "CARR", cb, cl); free(cb); } else b.fail = 1; }
    }
    {   unsigned char tb[64];                        /* the port's trophies (trophy.c), not the original's */
        long tl = trophy_save(tb, sizeof tb);
        if (tl > 0) put_block(&b, "TRPH", tb, tl); }
    if (b.fail) {   /* a part missing: no save beats a save that loses it */
        printf("profile: out of memory while gathering the profile, not saved\n");
        free(b.p); *len = 0; return NULL;
    }
    *len = b.n;
    return b.p;
}
/* the blocks back into the working files; 1 ok */
static int apply(const unsigned char *p, long n) {
    const unsigned char *opt = NULL, *gar = NULL, *hs = NULL, *car = NULL, *cc = NULL, *tr = NULL;
    long optn = 0, garn = 0, hsn = 0, carn = 0, ccn = 0, trn = 0;
    for (long i = 0; i + 8 <= n; ) {
        uint32_t l; memcpy(&l, p + i + 4, 4);
        if (l > (uint32_t)(n - i - 8)) return 0;   /* unsigned: a huge l is no negative length */
        const unsigned char *d = p + i + 8;
        if (!memcmp(p + i, "OPTN", 4)) { opt = d; optn = l; }
        else if (!memcmp(p + i, "GRGE", 4)) { gar = d; garn = l; }
        else if (!memcmp(p + i, "HSCR", 4)) { hs = d; hsn = l; }
        else if (!memcmp(p + i, "CARR", 4)) { car = d; carn = l; }
        else if (!memcmp(p + i, "CCAR", 4)) { cc = d; ccn = l; }
        else if (!memcmp(p + i, "TRPH", 4)) { tr = d; trn = l; }
        i += 8 + l;
    }
    /* (GitHub #3 S7) a career block of another size cannot be read: refuse the load
       before anything is written ("Unable to load"), not a reset career under the
       profile's name that the next autosave writes over the real one */
    if (car && !g_xf_city && carn != career_save(NULL, 0)) {
        printf("profile: career block %ld bytes, want %ld -- not loaded\n", carn, career_save(NULL, 0));
        return 0;
    }
    uint32_t sz = 0;
    if (opt && optn >= 4) memcpy(&sz, opt, 4);
    if (opt && sz == sizeof g_opt && optn == (long)(4 + sz)) memcpy(&g_opt, opt + 4, sizeof g_opt);
    else if (opt && optn == (long)(4 + sz) && sz >= offsetof(N2Options, shuffle)) {
        /* (GitHub #3 S6) another build's block: its head (audio, gameplay, the save
           settings -- the same layout in every build) is still this profile's; only
           the rest stays as it is, not the last profile's autosave / save prompts */
        memcpy(&g_opt, opt + 4, offsetof(N2Options, shuffle));
        printf("profile: options from another build, audio / gameplay / save settings taken\n");
    } else printf("profile: options from another build, kept\n");
    options_save();
    if (gar && garn > 0) {
        /* the garage not written: the last profile's cars would be saved into this one */
        if (!write_file(PROFILE_GARAGE_FILE, gar, garn)) { printf("profile: garage not written, load refused\n"); return 0; }
    } else remove(PROFILE_GARAGE_FILE);
    memset(g_hs, 0, sizeof g_hs);
    if (hs && hsn == (long)sizeof g_hs) memcpy(g_hs, hs, sizeof g_hs);
    scores_save();
    /* LoadFromBuffer 80131f28: Default(true) (the cars defaulted, slot 0
       unlocked), the car DB, the career; here the career load defaults
       first, then the cars go over it */
    if (g_xf_city) {   /* a city's career is in (a cancel's restore): the Bayview one stays waiting */
        unsigned char *a = car && carn > 0 ? (unsigned char *)malloc((size_t)carn) : NULL;
        unsigned char *k = cc && ccn > 0 ? (unsigned char *)malloc((size_t)ccn) : NULL;
        if (a && k) {
            memcpy(a, car, (size_t)carn); memcpy(k, cc, (size_t)ccn);
            free(g_xf_car); free(g_xf_cc); g_xf_car = a; g_xf_cc = k; g_xf_carn = carn; g_xf_ccn = ccn;
        } else { free(a); free(k); }
    } else {
    if (!car || !career_load(car, carn)) career_default();
    if (cc && !career_cars_load(cc, ccn)) printf("profile: career cars from another build, defaulted\n");
    }
    trophy_load(tr, trn);                        /* none (an older save): none unlocked yet */
    trophy_report_all();                         /* switched on since: tell the system what is earned */
    trophy_check();                              /* catch up with the career as saved */
    return 1;
}

/* ------------------------------------------------------------- profile */

void profile_init(void) {
    mkdir(PROFILE_DIR, 0777);
    scores_load();
    beyond_progress_path_fn(profile_xp_progress_path);   /* per-profile expansion progress (CR8) */
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
    file_recover(p);
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

/* (GitHub #3 S5) "Unable to save %1." then autosave off, as the memcard screens' answer to
   a failed save does (RM_SAVE_FAIL: g_opt.autosave = 0) -- the direct autosaves too, so a
   card that cannot take the save stops being written to behind the player's back */
static int save_failed(void) {
    savenote_failed();
    if (g_opt.autosave) {
        g_opt.autosave = 0;
        options_save();
        printf("profile: the save failed -- autosave off\n");
    }
    return 0;
}
static int xf_flush_unsaved(void);
int profile_save(void) {
    if (!g_named) return 0;
    int xok = 1;
    if (g_xf_city) xok = profile_xf_save();   /* the city's own career beside it */
    if (!xf_flush_unsaved()) xok = 0;          /* a city career kept after a failed write (CR9) */
    long pn; unsigned char *pay = serialize(&pn);
    if (!pay) return save_failed();   /* said, not silent (audit 11-17) */
    long total = RMC_HDR + pn;
    unsigned char *f = (unsigned char *)malloc((size_t)total);
    if (!f) { free(pay); printf("profile: out of memory for the save file\n"); return save_failed(); }
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
    if (ok && !xok) { ok = 0; printf("profile: the city career file was not written\n"); }   /* (CR9) not "saved" */
    printf("profile: %s %s (%ld bytes)\n", ok ? "saved" : "cannot save", g_name, total);
    if (ok) savenote_saved();   /* the corner note (port addition, savenote.c) */
    else save_failed();
    return ok;
}

/* ---- World Expansion "Start fresh" (port; EXPANSION_PLAN "Money, cars and progress"):
   a city's own career -- cash, car slots, stage -- like the start of UG2's career, in
   the profile's XPCITY<n> file ("XPF1", u32 wins, u32 career bytes, u32 car bytes, the
   two blocks as the profile has them). It never touches the Bayview career. ---- */
static int xf_blobs(unsigned char **car, long *carn, unsigned char **cc, long *ccn) {
    *carn = career_save(NULL, 0); *ccn = career_cars_save(NULL, 0);
    *car = (unsigned char *)malloc((size_t)*carn); *cc = (unsigned char *)malloc((size_t)*ccn);
    if (!*car || !*cc) { free(*car); free(*cc); *car = *cc = NULL; return 0; }
    career_save(*car, *carn); career_cars_save(*cc, *ccn);
    return 1;
}
int profile_xf_city(void) { return g_xf_city; }
int profile_xf_wins(void) { return g_xf_wins; }
void profile_xf_win(void) {
    if (!g_xf_city) return;
    g_xf_wins++;
    /* PORT GUESS: the city's stage (its parts / cars) goes up every 10 first wins, to stage 4 */
    int st = g_xf_wins / 10; if (st > 4) st = 4;
    if (st > g_career.stage) { g_career.stage = st; printf("xp fresh: city stage %d (%d wins)\n", st, g_xf_wins); }
}
/* (Codex CR9, 11-20) a city career whose file could not be written when the trip
   ended stays here for the session: the next trip there takes it up and the next
   profile save writes it, instead of it being lost (or a default saved over it) */
static unsigned char *g_xf_unsaved; static long g_xf_unsavedn; static int g_xf_unsaved_city;
static char g_xf_unsaved_name[sizeof g_name];   /* whose it is: never written into another profile */
static void xf_unsaved_drop(void) { free(g_xf_unsaved); g_xf_unsaved = NULL; g_xf_unsavedn = 0; g_xf_unsaved_city = 0; }
static unsigned char *xf_build(long *n) {   /* the XPCITY<n> file of the city career now in */
    unsigned char *car, *cc; long carn, ccn;
    if (!xf_blobs(&car, &carn, &cc, &ccn)) return NULL;
    *n = 16 + carn + ccn;
    unsigned char *b = (unsigned char *)malloc((size_t)*n);
    if (b) {
        memcpy(b, "XPF1", 4); put32(b + 4, (uint32_t)g_xf_wins); put32(b + 8, (uint32_t)carn); put32(b + 12, (uint32_t)ccn);
        memcpy(b + 16, car, (size_t)carn); memcpy(b + 16 + carn, cc, (size_t)ccn);
    }
    free(car); free(cc);
    return b;
}
static int xf_valid(const unsigned char *b, long n) {
    return b && n >= 16 && !memcmp(b, "XPF1", 4) && 16 + (long)get32(b + 8) + (long)get32(b + 12) == n;
}
static int xf_flush_unsaved(void) {
    if (!g_xf_unsaved) return 1;
    char p[320];
    snprintf(p, sizeof p, "%s/%s/XPCITY%d", PROFILE_DIR, g_xf_unsaved_name, g_xf_unsaved_city);
    if (!write_file(p, g_xf_unsaved, g_xf_unsavedn)) return strcmp(g_xf_unsaved_name, g_name) != 0;   /* another profile's: not this save's failure */
    printf("xp fresh: city %d career of %s (kept from a failed save) saved\n", g_xf_unsaved_city, g_xf_unsaved_name);
    xf_unsaved_drop();
    return 1;
}
int profile_xf_save(void) {
    if (!g_xf_city) return 0;
    char f[16], p[320];
    snprintf(f, sizeof f, "XPCITY%d", g_xf_city);
    if (!profile_file(f, p, sizeof p)) return 0;   /* unnamed: the city's career lasts the trip */
    long n = 0; unsigned char *b = xf_build(&n);
    int ok = b && write_file(p, b, n);
    free(b);
    if (ok && g_xf_unsaved_city == g_xf_city && !strcmp(g_xf_unsaved_name, g_name)) xf_unsaved_drop();
    printf("xp fresh: city %d career %s ($%d, %d wins)\n", g_xf_city, ok ? "saved" : "NOT saved", g_career.cash, g_xf_wins);
    return ok;
}
/* 0: not entered (the Bayview career stays in): an XPCITY file that exists but
   cannot be read or parsed is never treated as a first visit (CR9), and no memory
   for the Bayview snapshot refuses the trip (GAPS XF6) */
int profile_xf_enter(int city) {
    if (g_xf_city == city) return 1;
    profile_xf_leave();
    char f[16], p[320]; long n = 0; unsigned char *b = NULL;
    int exists = 0, from_mem = 0;
    snprintf(f, sizeof f, "XPCITY%d", city);
    if (g_xf_unsaved && g_xf_unsaved_city == city && g_named && !strcmp(g_xf_unsaved_name, g_name)) { b = g_xf_unsaved; n = g_xf_unsavedn; exists = from_mem = 1; }
    else if (profile_file(f, p, sizeof p)) {
        b = read_file(p, &n);   /* (puts a .savebak back first) */
        struct stat st;
        exists = b != NULL || stat(p, &st) == 0;
    }
    if (exists && !xf_valid(b, n)) {
        printf("xp fresh: city %d career file %s -- the trip is refused, the file kept\n", city, b ? "is damaged" : "cannot be read");
        if (!from_mem) free(b);
        return 0;
    }
    if (!xf_blobs(&g_xf_car, &g_xf_carn, &g_xf_cc, &g_xf_ccn)) {
        printf("xp fresh: no memory for the Bayview career's copy -- the trip is refused\n");
        if (!from_mem) free(b);
        return 0;
    }
    g_xf_city = city; g_xf_wins = 0;
    int ok = 0;
    if (exists) {
        ok = career_load(b + 16, (long)get32(b + 8)) && career_cars_load(b + 16 + get32(b + 8), (long)get32(b + 12));
        if (!ok) {   /* parsed but refused: the Bayview career back in, the file untouched */
            career_load(g_xf_car, g_xf_carn); career_cars_load(g_xf_cc, g_xf_ccn);
            free(g_xf_car); free(g_xf_cc); g_xf_car = g_xf_cc = NULL; g_xf_carn = g_xf_ccn = 0;
            g_xf_city = 0;
            printf("xp fresh: city %d career could not be loaded -- the trip is refused, the file kept\n", city);
            if (!from_mem) free(b);
            return 0;
        }
        g_xf_wins = (int)get32(b + 4);
    } else career_default();   /* a new city career: UG2's start (no cash, the first slot, the car lot) */
    if (from_mem) { g_xf_unsaved = NULL; g_xf_unsavedn = 0; g_xf_unsaved_city = 0; }   /* it is live again */
    free(b);
    printf("xp fresh: city %d career %s ($%d, %d wins)\n", city, exists ? (from_mem ? "taken up from memory" : "loaded") : "new",
           g_career.cash, g_xf_wins);
    return 1;
}
void profile_xf_leave(void) {
    if (!g_xf_city) return;
    if (!profile_xf_save() && g_named) {   /* (CR9) not written: keep it for a later save, not lost */
        long n = 0; unsigned char *b = xf_build(&n);
        if (b) {
            free(g_xf_unsaved); g_xf_unsaved = b; g_xf_unsavedn = n; g_xf_unsaved_city = g_xf_city;
            snprintf(g_xf_unsaved_name, sizeof g_xf_unsaved_name, "%s", g_name);
            printf("xp fresh: city %d career kept in memory until a save goes through\n", g_xf_city);
        }
        savenote_failed();
    }
    int ok = career_load(g_xf_car, g_xf_carn);
    if (!ok) career_default();
    career_cars_load(g_xf_cc, g_xf_ccn);
    printf("xp fresh: city %d left, the Bayview career back%s ($%d)\n", g_xf_city, ok ? "" : " (DEFAULTED)", g_career.cash);
    free(g_xf_car); free(g_xf_cc); g_xf_car = g_xf_cc = NULL; g_xf_carn = g_xf_ccn = 0;
    g_xf_city = 0; g_xf_wins = 0;
}

int profile_xf_exists(int city) {
    char f[16], p[320]; struct stat st;
    snprintf(f, sizeof f, "XPCITY%d", city);
    return profile_file(f, p, sizeof p) && stat(p, &st) == 0;
}
/* port: switching a city back to Start fresh starts it clean (EXPANSION_PLAN "Share Your Garage") */
void profile_xf_forget(int city) {
    if (g_xf_city == city) profile_xf_leave();
    char f[16], p[320];
    snprintf(f, sizeof f, "XPCITY%d", city);
    if (profile_file(f, p, sizeof p) && remove(p) == 0) printf("xp fresh: city %d career cleared\n", city);
    if (g_xf_unsaved && g_xf_unsaved_city == city && !strcmp(g_xf_unsaved_name, g_name)) xf_unsaved_drop();
    snprintf(f, sizeof f, "XPPROG_F%d", city);   /* its wins too: a fresh start pays full purses again (CR8) */
    if (profile_file(f, p, sizeof p) && remove(p) == 0) printf("xp fresh: city %d progress cleared\n", city);
}

int profile_xp_progress_path(char *out, int cap) {
    if (!g_named) return 0;
    char f[24];
    if (g_xf_city) snprintf(f, sizeof f, "XPPROG_F%d", g_xf_city);
    else snprintf(f, sizeof f, "XPPROG");
    if (!profile_file(f, out, cap)) return 0;
    if (!g_xf_city) {   /* the one-time move: the old shared file goes to the first profile that races there */
        struct stat st;
        const char *old = beyond_progress_legacy_path();
        if (stat(out, &st) != 0 && stat(old, &st) == 0) {
            if (rename(old, out) == 0) printf("xp progress: %s moved to profile %s\n", old, g_name);
            else printf("xp progress: could not move %s to profile %s\n", old, g_name);
        }
    }
    return 1;
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
    trophy_default();
    profile_set_name("");
    options_save();
}

int profile_snapshot(void) {
    free(g_snap);
    g_snap = serialize(&g_snaplen);
    return g_snap != NULL;
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
int profile_has_snapshot(void) { return g_snap != NULL; }

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
