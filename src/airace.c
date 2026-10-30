/* airace.c — who races and where they start (see airace.h).
 * Addresses in comments are SPEED2.EXE v1.2. */
#include "airace.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* ---- car types ---------------------------------------------------------------- */
static unsigned char *g_types; static int g_ntypes;
static char g_tname[64][32];

int airace_types_init(const unsigned char *g, long len)
{
    free(g_types); g_types = NULL; g_ntypes = 0;
    for (long i = 0; i + 8 <= len; i += 4) {
        if (rd32(g + i) != 0x34600) continue;
        long sz = (long)rd32(g + i + 4), body = i + 8;
        if (body + sz > len) break;
        /* the records start where the first CARS\ path sits 0x40 bytes in
           (the chunk body opens with alignment filler) */
        long r0 = -1;
        for (long k = body; k < body + 64 && k + 0x45 < body + sz; k++)
            if (!memcmp(g + k + 0x40, "CARS", 4) && g[k + 0x44] == 92) { r0 = k; break; }
        if (r0 < 0) break;
        int n = (int)((body + sz - r0) / N2_REC_SIZE);
        if (n > 64) n = 64;
        g_types = (unsigned char *)malloc((size_t)n * N2_REC_SIZE);
        if (!g_types) return 0;
        memcpy(g_types, g + r0, (size_t)n * N2_REC_SIZE);
        for (int t = 0; t < n; t++) {
            const char *p = (const char *)g_types + t * N2_REC_SIZE + 0x45;
            int k = 0;
            while (k < 31 && p[k] && p[k] != 92) { g_tname[t][k] = p[k]; k++; }
            g_tname[t][k] = 0;
            if (!g_tname[t][0]) { n = t; break; }
        }
        g_ntypes = n;
        break;
    }
    return g_ntypes;
}
int airace_ntypes(void) { return g_ntypes; }
int airace_type_of(const char *car)
{
    for (int t = 0; t < g_ntypes; t++) if (!strcmp(g_tname[t], car)) return t;
    return -1;
}
const unsigned char *airace_type_rec(int t) { return t >= 0 && t < g_ntypes ? g_types + t * N2_REC_SIZE : NULL; }
const char *airace_type_name(int t) { return t >= 0 && t < g_ntypes ? g_tname[t] : ""; }
int airace_type_suv(int t) { return t >= 0 && t < g_ntypes ? g_types[t * N2_REC_SIZE + 0x88a] != 0 : 0; }

/* 0x41aaf0 + 0x412be0: the streaming manager's traffic models -- every car
   type of class 2 (+0x844) in table order, each with a quota per region
   (+0x854 + region, s8). A traffic racer takes a random one of those with
   quota left while fewer than 7 models are in use; then one in use with
   quota left, else one in use, else the first. */
int airace_traffic_types(int region, int n, AiRng *rng, int *out)
{
    int list[64], used[64], nl = 0, avail = 0, distinct = 0;
    for (int t = 0; t < g_ntypes && nl < 64; t++)
        if ((int)rd32(g_types + t * N2_REC_SIZE + 0x844) == 2) { used[nl] = 0; list[nl++] = t; }
    if (nl < 1) return 0;
    for (int i = 0; i < nl; i++) if (0 < (signed char)g_types[list[i] * N2_REC_SIZE + 0x854 + region]) avail++;
    if (avail < 1) return 0;
    for (int k = 0; k < n; k++) {
        int pick = -1;
        if (distinct < 7) {
            int r = airng_int(rng, avail), j = 0;
            for (int i = 0; i < nl; i++) {
                int q = (signed char)g_types[list[i] * N2_REC_SIZE + 0x854 + region];
                if (!(used[i] < q)) continue;
                if (r == j) {
                    pick = i;
                    if (used[i] == 0) distinct++;
                    if (++used[i] == q) avail--;
                    break;
                }
                j++;
            }
            out[k] = pick >= 0 ? list[pick] : -1;   /* (none left: the racer keeps its model) */
            continue;
        }
        for (int i = 0; i < nl && pick < 0; i++)
            if (0 < used[i] && used[i] < (signed char)g_types[list[i] * N2_REC_SIZE + 0x854 + region]) pick = i;
        for (int i = 0; i < nl && pick < 0; i++) if (0 < used[i]) pick = i;
        if (pick < 0) pick = 0;
        used[pick]++;
        out[k] = list[pick];
    }
    return n;
}

/* ---- the race table (GLOBALB 0x34a11, the first one, 136-byte records) ---------- */
static unsigned char *g_races; static int g_nraces;
int airace_races_init(const unsigned char *g, long len)
{
    free(g_races); g_races = NULL; g_nraces = 0;
    for (long i = 0; i + 8 <= len; i += 4) {
        if (rd32(g + i) != 0x34a11) continue;
        long sz = (long)rd32(g + i + 4);
        if (i + 8 + sz > len || sz < 136 || sz % 136) continue;
        g_races = (unsigned char *)malloc((size_t)sz);
        if (!g_races) return 0;
        memcpy(g_races, g + i + 8, (size_t)sz);
        g_nraces = (int)(sz / 136);
        break;
    }
    return g_nraces;
}
/* a record's race type (0x5002e0 tags +0x34 == 2 as URL): +0xf, every
   record of one event agrees. A record lists its races at +0x18 ({u16
   event, u8, u8} x +0x7e: a career URL tournament has up to 3); an event
   that is never a record's first race takes the type of a record that
   lists it later. */
int airace_event_type(int event)
{
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < g_nraces; i++) {
            const unsigned char *r = g_races + i * 136;
            int n = pass ? r[0x7e] : 1, hit = 0;
            if (n > 7) n = 7;                       /* +0x34 follows */
            for (int j = 0; j < n && !hit; j++)
                hit = (r[0x18 + 4 * j] | r[0x19 + 4 * j] << 8) == event;
            if (!hit) continue;
            if (r[0x34] == 2) return AIRACE_URL;
            return r[0xf];
        }
    return -1;
}

/* ---- random ------------------------------------------------------------------- */
static void airng_next(AiRng *r)            /* the state step shared by 0x43c1c0 / 0x43c1f0 */
{
    unsigned t = r->s ^ 0x1d872b41u, u = t >> 5 ^ t;
    r->s = u << 27 ^ u ^ t;
}
int airng_int(AiRng *r, int n)                                          /* 0x43c1c0 */
{
    unsigned v = n > 0 ? r->s % (unsigned)n : 0;   /* the old state, before the step */
    airng_next(r);
    return (int)v;
}
float airng_float(AiRng *r, float x)                                    /* 0x43c1f0 */
{
    unsigned v = r->s % 0x7fffffffu;
    airng_next(r);
    return (float)(int)v * 4.656612873077393e-10f * x;
}

/* ---- the quick race (0x53ee90) ---------------------------------------------- */
static const char *const NAMES[53] = {                                  /* 0x7f6cd0 */
    "Phil", "Scott", "Kristian", "Carl", "Steve", "Stefan", "Jason", "Greg", "Marc", "Audrey",
    "Ben", "Mike", "Vlad", "Russ", "Neil", "Mario", "Al", "Carla", "Frank", "Carol",
    "Serena", "Bill", "Antonio", "Michelle", "Vince", "Amy", "Matt", "Bryce", "Clarence", "Ryan",
    "Eric", "Daryl", "Stan", "Brent", "Dana", "Hector", "Stacey", "Alex", "Simon", "Timmy",
    "Terry", "Jackie", "Alden", "Jared", "Paul", "Arnel", "Jamey", "Brian", "Gary", "Lola",
    "Kirby", "Don", "Marcel"
};
/* the racer car lists (car type indices): 0x7f6da4 (region 0 = US, the PC
   game), 0x7f6dc0 (other regions), 0x7f6ddc (the SUVs) */
static const signed char CARS_US[26] = { 27, 19, 15, 22, 13, 11, 16, 21, 2, 14, 30, 28, 26, 4, 29, 3, 1, 0, 24, 23, 20, 17, 12, 25, 5, 18 };
static const signed char CARS_SUV[3] = { 8, 9, 10 };

/* 0x4fe970: n distinct names */
static void pick_names(AiRng *rng, const char **out, int n)
{
    char used[53] = { 0 };
    for (int i = 0; i < n; i++) {
        out[i] = NAMES[0];
        for (int tries = 0; tries < 100; tries++) {
            int k = airng_int(rng, 53);
            if (!used[k]) { out[i] = NAMES[k]; used[k] = 1; break; }
        }
    }
}
static float stock_level(int type)
{
    static float cache[64]; static int have[64];
    if (type < 0 || type >= 64) return 99;
    if (!have[type]) {
        unsigned char built[N2_REC_SIZE];
        PerfParts pp; perf_clear(&pp);
        perf_build(airace_type_rec(type), &pp, built);
        cache[type] = perf_level(perf_rating(built));
        have[type] = 1;
    }
    return cache[type];
}
/* 0x4feaf0 (no override slots, no race record, any type allowed): the
   list shuffled, then the first car whose stock level is within tol of
   the player's, tol 0.06 then doubling every pass */
static int pick_car(AiRng *rng, int suv, float player_level)
{
    const signed char *list = CARS_US; int n = 26;
    if (suv) { list = CARS_SUV; n = 3; }
    int order[26];
    for (int i = 0; i < n; i++) order[i] = list[i];
    for (int i = 0; i < n; i++) {
        int j = airng_int(rng, n), t = order[i];
        order[i] = order[j]; order[j] = t;
    }
    float tol = 0.03f;
    for (int pass = 0; pass < 64; pass++) {
        tol += tol;
        for (int i = 0; i < n; i++)
            if (order[i] < g_ntypes && stock_level(order[i]) <= player_level + tol) return order[i];
    }
    return order[0];
}
void airace_names(AiRng *rng, const char **out, int n) { pick_names(rng, out, n); }
int airace_quick(int n, int player_type, const unsigned char *player_built,
                 const PerfParts *player_parts, int difficulty,
                 const unsigned char *blob, AiRng *rng, AiOpponent *out)
{
    if (n <= 0 || !perf_ready() || g_ntypes <= 0) return 0;
    if (n > AIRACE_MAX) n = AIRACE_MAX;
    const char *names[8];
    pick_names(rng, names, 8);
    float prating = perf_rating(player_built), phand = perf_handling(player_built);
    float plevel = perf_level(prating);
    int pnos = (int)(player_parts->level[2] + 0.5f);
    if (difficulty < 0) difficulty = 0;
    if (difficulty > 2) difficulty = 2;
    for (int i = 0; i < n; i++) {
        AiOpponent *o = &out[i];
        memset(o, 0, sizeof *o);
        o->id = 100 + i;
        snprintf(o->name, sizeof o->name, "%s", names[i]);
        o->type = pick_car(rng, airace_type_suv(player_type), plevel);
        snprintf(o->car, sizeof o->car, "%s", airace_type_name(o->type));
        o->skin = o->id - 0x62;
        /* 0x4fed00: the first skill whose cumulative chance covers r */
        {
            float r = airng_float(rng, 1.0f);
            const unsigned char *cdf = blob + 0x9f4 + difficulty * 0x28;
            o->skill = 0;
            for (int k = 0; k < 10; k++) if (r <= rdf(cdf + 4 * k)) { o->skill = k; break; }
        }
        o->block = 1;
        /* 0x532010 -> 0x5b7570(player, 1, !drag): the player's rating and
           handling, then the player's nitrous level */
        perf_clear(&o->parts);
        perf_fit_rating(o->type, airace_type_rec(o->type), prating, phand, &o->parts, o->rec);
        perf_fit_level(&o->parts, 2, pnos);
        perf_build(airace_type_rec(o->type), &o->parts, o->rec);
        o->rating = perf_rating(o->rec);
        o->handling = perf_handling(o->rec);
        o->klass = perf_class(o->rating);
    }
    return n;
}

/* RaceStarter::AddCareerEventAIOpponents 0x5260b0 (GC 8011a22c) */
int airace_career(int n, const AiCareerSlot *slot, int suv_event, float career_level,
                  const unsigned char *player_built, const PerfParts *player_parts, int drag,
                  AiRng *rng, AiOpponent *out)
{
    if (n <= 0 || !perf_ready() || g_ntypes <= 0) return 0;
    if (n > AIRACE_MAX) n = AIRACE_MAX;
    float prating = perf_rating(player_built), phand = perf_handling(player_built);
    float plevel = perf_level(prating);
    int pclass = perf_class(prating);
    int pnos = (int)(player_parts->level[2] + 0.5f);
    for (int i = 0; i < n; i++) {
        const AiCareerSlot *c = &slot[i];
        AiOpponent *o = &out[i];
        memset(o, 0, sizeof *o);
        o->id = 100 + i;
        snprintf(o->name, sizeof o->name, "%s", c->name ? c->name : "");
        /* the level the car is picked by: 0 the entry's, 3 the stage's
           GetAIPerformanceMatchingParameter, else the player's */
        float t = c->mode == 0 ? (float)c->level : c->mode == 3 ? career_level : plevel;
        /* "RANDOM": PickRandomCarsOfSomeSort 0x4feaf0 (the player's type allowed,
           the SUV list for an SUV event +0xd), stock parts; else FillWithPreset */
        o->type = c->type >= 0 && c->type < g_ntypes ? c->type : pick_car(rng, suv_event, t);
        snprintf(o->car, sizeof o->car, "%s", airace_type_name(o->type));
        o->skin = o->id - 0x62;                  /* SetCompositeNameHash(i + 2) */
        o->skill = c->skill;
        o->block = c->block;
        perf_clear(&o->parts);
        if (c->mode == 1) {
            /* MatchPerformance(player, 1, !drag) as the quick race (0x5b7570); the
               class is GetCarClass of the player's car */
            perf_fit_rating(o->type, airace_type_rec(o->type), prating, drag ? -1.0f : phand, &o->parts, o->rec);
            perf_fit_level(&o->parts, 2, pnos);
            o->klass = pclass;
        } else if (c->mode != 2) {
            /* level L (0..10) along the type's rating range and the handling
               range (none in drag), MatchPerformance(rating, 0, handling);
               CalculateCarClass(rating, 1) reads the rating as a level (both
               EXEs), so the class is the top one's; then the player's nitrous
               level (MatchUpgradeLevel, path 2) */
            float L = c->mode == 0 ? (float)c->level : career_level;   /* + 0x7992e4 (0) */
            if (!(L > 0)) L = 0;
            if (L > 10.0f) L = 10.0f;
            float lo = 0, hi = 0;
            perf_type_rating_range(o->type, &lo, &hi);
            float target = (hi - lo) * L * 0.1f + lo;
            float hand = drag ? -1.0f : (perf_handling_max() - perf_handling_min()) * L * 0.1f + perf_handling_min();
            perf_fit_rating(o->type, airace_type_rec(o->type), target, hand, &o->parts, o->rec);
            {
                float r = target;
                if (!(r > 0)) r = 0;
                if (r > 10.0f) r = 10.0f;
                o->klass = perf_class(r * 0.1f * (perf_rating_max() - perf_rating_min()) + perf_rating_min());
            }
            perf_fit_level(&o->parts, 2, pnos);
        }
        perf_build(airace_type_rec(o->type), &o->parts, o->rec);
        o->rating = perf_rating(o->rec);
        o->handling = perf_handling(o->rec);
        if (c->mode == 2) o->klass = perf_class(o->rating);
    }
    return n;
}

void airace_start_order(int nh, int na, int mode, AiRng *rng, int *pos)  /* 0x5264a0 */
{
    int p = 1;
    if (mode == 2) {
        for (int i = 0; i < na; i++) pos[nh + i] = p++;
        for (int i = 0; i < nh; i++) pos[i] = p++;
    } else {
        for (int i = 0; i < nh; i++) pos[i] = p++;
        for (int i = 0; i < na; i++) pos[nh + i] = p++;
    }
    if (mode == 3 && nh == 1 && na > 0) {                               /* 0x4fede0 */
        int k = airng_int(rng, na + nh);
        int t = pos[0]; pos[0] = pos[k]; pos[k] = t;
    }
}

/* ---- the grid (0x5eb5d0) ------------------------------------------------------ */
unsigned airace_hash(const char *s)                                     /* 0x43db50 */
{
    unsigned h = 0xffffffffu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}
int airace_grid(const char *path, int event, int backward, float (*pos)[3], float *heading, int max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return 0; }
    fclose(f);
    unsigned want = airace_hash(backward ? "BackwardStartingPosition" : "ForwardStartingPosition");
    int got = 0;
    unsigned char have[64] = { 0 };
    long stack[8]; int depth = 0;
    long o = 0, end = len;
    while (o + 8 <= end) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        long body = o + 8, next = body + (long)sz;
        if (next > end || next < body) break;
        if ((id & 0x80000000u) && sz > 8 && depth < 7) { stack[depth++] = end; end = next; o = body; continue; }
        if (id == 0x34146) {                                            /* 0x5d8470 */
            long s = (body + 15) & ~15L;
            for (; s + 48 <= next; s += 48) {
                const unsigned char *m = d + s;
                if (rd32(m + 8) != want || (int)rd32(m + 0x24) != event) continue;
                int k = (int)rd32(m + 0xc);
                if (k < 0 || k >= max || k >= 64 || have[k]) continue;
                pos[k][0] = rdf(m + 0x10); pos[k][1] = rdf(m + 0x14); pos[k][2] = rdf(m + 0x18);
                heading[k] = (float)(m[0x20] | m[0x21] << 8) * (6.28318531f / 65536.0f);
                have[k] = 1;
            }
        }
        o = next;
        while (o >= end && depth > 0) { o = end; end = stack[--depth]; }
    }
    free(d);
    while (got < max && got < 64 && have[got]) got++;                   /* slots 0..got-1 present */
    return got;
}
int airace_marker(const char *path, int event, const char *type, float pos[3], unsigned *heading16)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return 0; }
    fclose(f);
    unsigned want = airace_hash(type);
    int found = 0;
    long stack[8]; int depth = 0;
    long o = 0, end = len;
    while (o + 8 <= end && !found) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        long body = o + 8, next = body + (long)sz;
        if (next > end || next < body) break;
        if ((id & 0x80000000u) && sz > 8 && depth < 7) { stack[depth++] = end; end = next; o = body; continue; }
        if (id == 0x34146) {
            for (long s = (body + 15) & ~15L; s + 48 <= next; s += 48) {
                const unsigned char *m = d + s;
                if (rd32(m + 8) != want || (int)rd32(m + 0x24) != event) continue;
                pos[0] = rdf(m + 0x10); pos[1] = rdf(m + 0x14); pos[2] = rdf(m + 0x18);
                *heading16 = (unsigned)(m[0x20] | m[0x21] << 8);
                found = 1;
                break;
            }
        }
        o = next;
        while (o >= end && depth > 0) { o = end; end = stack[--depth]; }
    }
    free(d);
    return found;
}
