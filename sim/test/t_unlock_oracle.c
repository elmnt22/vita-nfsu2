/* C11 unlocks vs SPEED2.EXE (re-tools/out/career/unlock): writes career
   states (career_save blobs, byte-compatible with PlayerCareerState 0x529230)
   and the port's unlock answers for each; unlock_oracle.py loads the same
   blobs into the original under unicorn and asks SPEED2.EXE's functions;
   compare.py diffs the two.
     gen <dir> [extra .bin career blobs...]   (run_unlock.sh gen ...) */
#ifdef SHOP_COPY
#include SHOP_COPY
#else
#include "../../src/shop.c"
#endif
#include "career.h"
#include "airace.h"
void garage_tt_request(int what, int dir) { (void)what; (void)dir; }   /* main.c, not linked */

static unsigned char *rdf(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}
static uint32_t u32le(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned g_seed = 12345;
static int rnd(int n) { g_seed = g_seed * 1103515245u + 12345u; return n > 0 ? (int)((g_seed >> 8) % (unsigned)n) : 0; }

static const unsigned char *g_shops; static int g_nshops;     /* GLOBALB 0x34a12 of the first container */
static const unsigned char *career_shop(int i) { return i < g_nshops ? career_shop_by_handle(u32le(g_shops + 0xa0 * i + 0x38)) : NULL; }
static Shop *g_s;
static int g_maxrace, g_races[1024], g_nraces;

static void answers(FILE *o)
{
    for (int e = 0; e < 96; e++)
        for (int l = 0; l <= 4; l++) fprintf(o, "calc %d %d %d\n", e, l, calc_unlocked(g_s, e, l));
    for (int c = 0; c < PERF_NCAT; c++)
        for (int l = 0; l <= 3; l++) fprintf(o, "perf %d %d %d\n", c, l, perf_unlocked(g_s, c, l));
    for (int i = 0; i < 24; i++) {
        const unsigned char *sh = career_shop(i);
        if (!sh) break;
        fprintf(o, "shopU %d %d\nshopE %d %d\n", i, career_shop_unlocked(sh), i, career_shop_enterable(sh));
    }
    for (int i = 0; i < g_nraces; i++) fprintf(o, "track %d %d\n", g_races[i], career_qr_track_unlocked(g_races[i]));
    for (int t = 0; t < airace_ntypes() && t < 0x2e; t++)
        fprintf(o, "car %d %d %d\n", t, career_car_unlocked(t), career_qr_car_unlocked(t));
    fprintf(o, "career_done %d\n", career_completed_career());
    g_s->career = 0;                       /* outside the career: QuickRaceUnlocker (filter 1) */
    for (int e = 0; e < 96; e++)
        for (int l = 0; l <= 4; l++) fprintf(o, "qrpart %d %d %d\n", e, l, part_open(g_s, e, l, 0));
    for (int c = 0; c < PERF_NCAT; c++)
        for (int l = 0; l <= 3; l++) fprintf(o, "qrperf %d %d %d\n", c, l, perf_unlocked(g_s, c, l));
    g_s->career = 4;
}

static void emit(const char *dir, int k)
{
    char p[600];
    long n = career_save(NULL, 0);
    unsigned char *b = malloc(n);
    career_save(b, n);
    snprintf(p, sizeof p, "%s/s%03d.bin", dir, k);
    FILE *f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f);
    career_load(b, n);                    /* evaluate on the state as it loads */
    free(b);
    snprintf(p, sizeof p, "%s/s%03d.port", dir, k);
    f = fopen(p, "w"); answers(f); fclose(f);
}

static void randomize(int k)
{
    career_default();
    CareerState *c = &g_career;
    int stage = rnd(7);                    /* 0..6 */
    c->stage = stage > 5 ? 5 : stage;
    int dens = rnd(101);                   /* % events done */
    for (int s = 0; s < 6; s++) {
        CareerStage *st = &c->st[s];
        int full = s < c->stage, part = s == c->stage;
        int nw = full ? 20 + rnd(16) : part ? rnd(36) : rnd(4) == 0 ? rnd(5) : 0;
        int nu = full ? 3 + rnd(8) : part ? rnd(11) : 0;
        int ns = full ? 2 + rnd(3) : part ? rnd(5) : 0;
        for (int i = 0; i < 35; i++) st->world[i] = i < nw ? 0x1000u + i : 0;
        for (int i = 0; i < 10; i++) st->url[i] = i < nu ? 0x2000u + i : 0;
        for (int i = 0; i < 4; i++) st->sponsor[i] = i < ns ? 0x3000u + i : 0;
        st->cleared = full;
    }
    c->nrec = 0;
    for (int i = 0; i < career_nevents() && c->nrec < 0x170; i++) {
        const unsigned char *ev = career_event(i);
        int es = (signed char)ev[0x37];
        int p = es < c->stage ? 90 : es == c->stage ? dens : 3;
        if (k % 7 == 0) p = rnd(2) ? 100 : 0;              /* some all-or-nothing */
        if (rnd(100) < p || rnd(100) < 3) {
            CareerRecord *r = career_add_record(u32le(ev + 8));
            if (!r) break;
            r->flag[CREC_DONE] = rnd(8) ? CDONE_WON : CDONE_LOST;
            r->flag[CREC_ENTERED] = (uint8_t)rnd(3);
        }
    }
    for (int i = 0; i < 24 && c->nrec < 0x180; i++) {
        const unsigned char *sh = career_shop(i);
        if (!sh || rnd(2)) continue;
        CareerRecord *r = career_add_record(u32le(sh + 0x38));
        if (r) { r->flag[CREC_ENTERED] = (uint8_t)rnd(3); r->flag[CREC_DONE] = rnd(3) ? 5 : 0; }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "gen")) { printf("usage: gen <dir> [blobs]\n"); return 1; }
    long gl; unsigned char *g = rdf("E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN", &gl);
    if (!g || !career_init(g, gl) || !airace_types_init(g, gl)) { printf("no data\n"); return 1; }
    g_s = calloc(1, sizeof *g_s);
    snprintf(g_s->dataroot, sizeof g_s->dataroot, "%s", "E:/NFS Underground 2 Vita/data/nfsug2");
    load_db(g_s);
    g_s->career = 4;                       /* CalculateIfUnlocked side only (filter 4) */
    for (long i = 0; i + 8 < gl; i += 4)
        if (u32le(g + i) == 0x80034a10u) {
            for (long j = i + 8; j < i + 8 + (long)u32le(g + i + 4); j += 8 + u32le(g + j + 4))
                if (u32le(g + j) == 0x34a12u) { g_shops = g + j + 8; g_nshops = (int)(u32le(g + j + 4) / 0xa0); }
            break;
        }
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        for (int k = 0; k < (signed char)ev[0x7e]; k++) {
            int r = (int16_t)(ev[0x18 + 4 * k] | ev[0x19 + 4 * k] << 8);
            if (r > g_maxrace) g_maxrace = r;
            int seen = 0;
            for (int q = 0; q < g_nraces; q++) seen |= g_races[q] == r;
            if (!seen && g_nraces < 1020) g_races[g_nraces++] = r;
        }
    }
    g_races[g_nraces++] = -1; g_races[g_nraces++] = 9999;
    const char *dir = argv[2];
    int k = 0;
    career_default(); emit(dir, k++);
    for (int a = 3; a < argc; a++) {
        long n; unsigned char *b = rdf(argv[a], &n);
        if (!b || !career_load(b, n)) { printf("bad blob %s\n", argv[a]); continue; }
        emit(dir, k++); free(b);
    }
    for (int i = 0; i < 300; i++) { randomize(i); emit(dir, k++); }
    /* the car unlock records: hash, event (region 0), the type it maps to */
    char p[600]; snprintf(p, sizeof p, "%s/types.port", dir);
    FILE *f = fopen(p, "w");
    for (int t = 0; t < airace_ntypes(); t++) {
        const unsigned char *r = airace_type_rec(t);
        if (r) fprintf(f, "type %d %u %d %s\n", t, u32le(r + 0xd0), (int)u32le(r + 0x840), airace_type_name(t));
    }
    fclose(f);
    printf("%d states, max race %d, %d types\n", k, g_maxrace, airace_ntypes());
    return 0;
}
