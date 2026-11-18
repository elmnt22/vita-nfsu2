/* xpevents.inc -- included at the end of xproads.c (it uses the lane graph there).

   The city's race events. Port of ExpansionDev/events/mkevents.py + mwbar.py and
   BeyondDev/tools/events/writers.py: MW gameplay.bin races (start grid, ordered
   checkpoints, finish line, barrier groups) -> the cheapest chain of lanes through the
   road graph passing every checkpoint in order (a lane the race's own barriers cross
   costs +5 km: MW fences off the roads a race does not take) -> one polyline, unkinked,
   recut into sections -> <out_dir>/ Paths<id>.bin, Routes<id>F.bin, events.txt,
   starts.txt, barriers.txt (main.c beyond_events_select, xpcity.c barriers). Every event
   goes through the game's own route builder (racepath.c) and is dropped if it refuses
   it. UG2 types: circuit 0, sprint 1, drag 4 (lap KO -> circuit, tollbooth /
   speedtrap -> sprint). Geometry in doubles, as the Python.
   Carbon's gameplay.bin is the same VPAK of vaults with three layout changes (export
   entries 16 bytes, entry tables 8-aligned, the inline flag 0x40); its races sit in the
   race_bin_<tier> vaults (race_bin_muscle/2_1_1), their pieces in a vault of their own, and
   the type is the race's parent (circuit, p2p, speedtraprace, ...), not its name. Canyon
   duels -> sprint (port guess); drift / canyon drift races are left out, as MW's are. */
#include "racepath.h"

typedef struct { double x, y, z; } D3;
typedef struct { D3 *p; int n, cap; } DPoly;
typedef struct { DPoly *v; int n, cap; } DPolys;
static int dp_add(DPoly *q, D3 v)
{
    if (q->n == q->cap) {
        int c = q->cap ? q->cap * 2 : 64; D3 *p = realloc(q->p, (size_t)c * sizeof *p);
        if (!p) return 0;
        q->p = p; q->cap = c;
    }
    q->p[q->n++] = v; return 1;
}
static int dps_new(DPolys *s)                         /* the index of a new empty polyline, -1 */
{
    if (s->n == s->cap) {
        int c = s->cap ? s->cap * 2 : 64; DPoly *v = realloc(s->v, (size_t)c * sizeof *v);
        if (!v) return -1;
        s->v = v; s->cap = c;
    }
    memset(&s->v[s->n], 0, sizeof s->v[s->n]); return s->n++;
}
static void dps_free(DPolys *s) { for (int i = 0; i < s->n; i++) free(s->v[i].p); free(s->v); memset(s, 0, sizeof *s); }
static double dd2(D3 a, D3 b) { return hypot(b.x - a.x, b.y - a.y); }
static double pyround(double x) { return nearbyint(x); }   /* Python round(): halves to even (the default mode) */
static void dunit(D3 p, D3 q, double u[2]) { double dx = q.x - p.x, dy = q.y - p.y, l = hypot(dx, dy); if (l == 0) l = 1; u[0] = dx / l; u[1] = dy / l; }

static void pu16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void pu32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void pf32(uint8_t *p, double v) { float f = (float)v; uint32_t u; memcpy(&u, &f, 4); pu32(p, u); }

/* a whole top-level chunk of a BUN (id), or NULL */
static uint8_t *read_top(const char *path, uint32_t want, size_t *len)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    uint8_t *out = NULL; *len = 0;
    for (;;) {
        uint8_t h[8];
        if (fread(h, 1, 8, f) != 8) break;
        uint32_t id = u32le(h), sz = u32le(h + 4);
        if (id == want) {
            out = malloc(sz ? sz : 1);
            if (out && fread(out, 1, sz, f) != sz) { free(out); out = NULL; }
            if (out) *len = sz;
            break;
        }
        if (fseek(f, (long)sz, SEEK_CUR)) break;
    }
    fclose(f); return out;
}
static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); *len = b ? (size_t)n : 0; return b;
}

/* ---- VLT hashes, MW gameplay.bin (a VPAK of vaults) ---- */
#define MIXH(a, b, c) do { \
    a -= b; a -= c; a ^= c >> 13; b -= c; b -= a; b ^= a << 8;  c -= a; c -= b; c ^= b >> 13; \
    a -= b; a -= c; a ^= c >> 12; b -= c; b -= a; b ^= a << 16; c -= a; c -= b; c ^= b >> 5;  \
    a -= b; a -= c; a ^= c >> 3;  b -= c; b -= a; b ^= a << 10; c -= a; c -= b; c ^= b >> 15; } while (0)
static uint32_t vlt_hash(const uint8_t *k, size_t n)                    /* vlthash.py h32 */
{
    uint32_t a = 0x9e3779b9u, b = 0x9e3779b9u, c = 0xABCDEF00u; size_t i = 0;
    while (n - i >= 12) { a += u32le(k + i); b += u32le(k + i + 4); c += u32le(k + i + 8); MIXH(a, b, c); i += 12; }
    c += (uint32_t)n;
    for (size_t j = 0; i + j < n; j++) {
        uint32_t ch = k[i + j];
        if (j < 4) a += ch << (8 * j); else if (j < 8) b += ch << (8 * (j - 4)); else c += ch << (8 * (j - 7));
    }
    MIXH(a, b, c); return c;
}
static uint32_t bstr_hash(const char *s) { uint32_t h = 0xffffffffu; while (*s) h = h * 33u + (uint8_t)*s++; return h; }

/* hash -> string, open addressing (a Python dict of the printable runs of a bin) */
typedef struct { uint32_t h; const uint8_t *s; int n; } NmE;
typedef struct { NmE *e; size_t cap, cnt; } Nm;
static int nm_put(Nm *m, uint32_t h, const uint8_t *s, int n, int keep)
{
    if ((m->cnt + 1) * 2 > m->cap) {
        size_t c = m->cap ? m->cap * 2 : 1 << 12; NmE *e = calloc(c, sizeof *e);
        if (!e) return 0;
        for (size_t i = 0; i < m->cap; i++) if (m->e[i].s) {
            size_t j = m->e[i].h & (c - 1); while (e[j].s) j = (j + 1) & (c - 1); e[j] = m->e[i];
        }
        free(m->e); m->e = e; m->cap = c;
    }
    size_t j = h & (m->cap - 1);
    while (m->e[j].s && m->e[j].h != h) j = (j + 1) & (m->cap - 1);
    if (!m->e[j].s) m->cnt++;
    else if (keep) return 1;
    m->e[j].h = h; m->e[j].s = s; m->e[j].n = n; return 1;
}
static const NmE *nm_get(const Nm *m, uint32_t h)
{
    if (!m->cap) return NULL;
    size_t j = h & (m->cap - 1);
    while (m->e[j].s) { if (m->e[j].h == h) return &m->e[j]; j = (j + 1) & (m->cap - 1); }
    return NULL;
}
/* re.findall(rb'[ -~]{2,}', bin); with_last: also the last '/' part unless already known (mwvlt.vault) */
static int nm_bin(Nm *m, const uint8_t *b, size_t n, int with_last)
{
    for (size_t i = 0; i < n; ) {
        size_t j = i; while (j < n && b[j] >= 0x20 && b[j] <= 0x7e) j++;
        if (j - i >= 2) {
            if (!nm_put(m, vlt_hash(b + i, j - i), b + i, (int)(j - i), 0)) return 0;
            if (with_last) {
                size_t k = j; while (k > i && b[k - 1] != '/') k--;
                if (!nm_put(m, vlt_hash(b + k, j - k), b + k, (int)(j - k), 1)) return 0;
            }
        }
        i = j + 1;
    }
    return 1;
}

#define F_POS 0x9f743a0eu
#define F_ROT 0x5a6a57c6u
#define F_CPS 0x34aae3fcu
#define F_CHILD 0x916e0e78u
#define F_BARRIERS 0xe244f26bu      /* the race's own BARRIER_SPLINE_<n> scenery groups ('*' = the other way round) */
#define VCOL 0x8e112eb7u
#define F_TLIMIT 0x7585f041u        /* TimeLimit (s, float): tollbooth / checkpoint races */
#define F_TBONUS 0xf52cc30eu        /* TimeBonus (s, int): a time_bonus_checkpoint */
#define F_TRAPS 0x822179d1u         /* SpeedTrapList: the speed trap markers */
enum { RT_CIRCUIT = 0, RT_SPRINT = 1, RT_DRAG = 4, RT_DRIFT = 5 };   /* UG2's race types (AIRACE_*) */
/* the event's own rules where MW / Carbon differ from UG2's type (modes.txt "id mode"; xpmode.h) */
enum { XM_NONE = 0, XM_CANYON_DUEL = 1, XM_CANYON_SPRINT = 2, XM_CANYON_DRIFT = 3, XM_CHECKPOINT = 4,
       XM_TOLLBOOTH = 5, XM_SPEEDTRAP = 6, XM_LAPKO = 7, XM_NMODES = 8 };
static const char *const k_mode_label[XM_NMODES] = { NULL, "Canyon Duel", "Canyon Sprint", "Canyon Drift",
                                                      "Checkpoint", "Tollbooth", "Speedtrap", "Lap Knockout" };
typedef struct { uint32_t key; int ord; D3 p; float rot; int tb; } PosE;   /* tb: TimeBonus, -1 none */   /* rot: F_ROT, degrees = the world heading */
typedef struct {
    char name[64]; int type, order; uint32_t par;   /* par: the parent collection (Carbon: the type) */
    int closed;                             /* laps (circuits, Carbon's drift races); else start -> finish */
    int mode;                               /* XM_*: the game's own event (canyon duel, speedtrap, ...) */
    uint32_t *cps, *child; int ncps, nchild;
    char (*bar)[24]; int nbar;              /* as listed ('*' kept) */
    D3 *wp; int nwp;
    float tlim;                             /* TimeLimit (s), 0 none */
    uint32_t *traps; int ntraps;            /* SpeedTrapList */
    char gates[1024];                       /* gates.txt lines: "id T x y z bonus" / "id S x y z 0" (id filled later) */
} Race;
typedef struct { uint32_t fo, de; int ix, ord; } FixE;
static int fix_cmp(const void *x, const void *y)
{
    const FixE *a = x, *b = y;
    if (a->fo != b->fo) return a->fo < b->fo ? -1 : 1;
    return (a->ord > b->ord) - (a->ord < b->ord);
}
/* the last PtrN entry for that field (a dict); ix1: only those into the bin (strings) */
static int fix_get(const FixE *f, size_t n, uint32_t fo, int ix1, uint32_t *de)
{
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (f[m].fo <= fo) lo = m + 1; else hi = m; }
    for (size_t i = lo; i-- > 0 && f[i].fo == fo; ) if (!ix1 || f[i].ix == 1) { *de = f[i].de; return 1; }
    return 0;
}
static int pos_cmp(const void *x, const void *y)
{
    const PosE *a = x, *b = y;
    if (a->key != b->key) return a->key < b->key ? -1 : 1;
    return (a->ord > b->ord) - (a->ord < b->ord);
}
static const PosE *pos_find(const PosE *p, int n, uint32_t k)   /* the last one of that key (a dict) */
{
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) / 2; if (p[m].key <= k) lo = m + 1; else hi = m; }
    return lo > 0 && p[lo - 1].key == k ? &p[lo - 1] : NULL;
}
static int race_type(const char *name)
{
    char n[64]; int i = 0;
    for (; name[i] && i < 63; i++) n[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    n[i] = 0;
    if (strstr(n, "drag")) return RT_DRAG;
    if (strstr(n, "circuit") || strstr(n, "lap_ko") || strstr(n, "lapknockout") || strstr(n, "knockout")) return RT_CIRCUIT;
    if (strstr(n, "sprint") || strstr(n, "p2p") || strstr(n, "tollbooth") || strstr(n, "speedtrap")) return RT_SPRINT;
    return -1;
}
static int carbon_type(uint32_t par)
{
    static const struct { const char *n; int t; } k[] = {
        { "circuit", RT_CIRCUIT }, { "knockout", RT_CIRCUIT }, { "lapknockout", RT_CIRCUIT },
        { "p2p", RT_SPRINT }, { "sprint", RT_SPRINT }, { "speedtraprace", RT_SPRINT }, { "tollboothrace", RT_SPRINT },
        { "canyon", RT_SPRINT }, { "canyonsprint", RT_SPRINT }, { "drag", RT_DRAG },
        { "driftrace", RT_DRIFT }, { "canyondrift", RT_DRIFT },   /* UG2's drift; a canyon drift = its downhill drift */
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (vlt_hash((const uint8_t *)k[i].n, strlen(k[i].n)) == par) return k[i].t;
    return -1;
}
static int carbon_mode(uint32_t par)
{
    static const struct { const char *n; int m; } k[] = {
        { "canyon", XM_CANYON_DUEL }, { "canyonsprint", XM_CANYON_SPRINT }, { "canyondrift", XM_CANYON_DRIFT },
        { "tollboothrace", XM_CHECKPOINT }, { "speedtraprace", XM_SPEEDTRAP },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (vlt_hash((const uint8_t *)k[i].n, strlen(k[i].n)) == par) return k[i].m;
    return XM_NONE;
}
static int mw_mode(const char *name)
{
    char n[64]; int i = 0;
    for (; name[i] && i < 63; i++) n[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    n[i] = 0;
    if (strstr(n, "tollbooth")) return XM_TOLLBOOTH;
    if (strstr(n, "speedtrap")) return XM_SPEEDTRAP;
    if (strstr(n, "lap_ko") || strstr(n, "lapknockout") || strstr(n, "knockout")) return XM_LAPKO;
    return XM_NONE;
}
static int is_barrier_name(const uint8_t *s, size_t n)
{
    size_t i = 0; if (i < n && s[i] == '*') i++;
    if (n < i + 16 || memcmp(s + i, "BARRIER_SPLINE_", 15)) return 0;
    for (size_t k = i + 15; k < n; k++) if (s[k] < '0' || s[k] > '9') return 0;
    return 1;
}
static void free_races(Race *r, int n)
{
    for (int i = 0; i < n; i++) { free(r[i].cps); free(r[i].child); free(r[i].bar); free(r[i].wp); free(r[i].traps); }
    free(r);
}
static int race_cmp(const void *x, const void *y)
{
    const Race *a = x, *b = y; int c = strcmp(a->name, b->name);
    return c ? c : (a->order > b->order) - (a->order < b->order);
}
static uint32_t *u32_array(const uint8_t *b, size_t bn, uint32_t d, int *n)
{
    *n = 0;
    if ((size_t)d + 8 > bn) return NULL;
    int c = u16le(b + d);
    if ((size_t)d + 8 + 4 * (size_t)c > bn) return NULL;
    uint32_t *a = malloc((size_t)(c ? c : 1) * 4);
    if (!a) return NULL;
    for (int i = 0; i < c; i++) a[i] = u32le(b + d + 8 + 4 * i);
    *n = c; return a;
}
static int ends_with(const uint8_t *s, int n, const char *t) { int m = (int)strlen(t); return n >= m && !memcmp(s + n - m, t, (size_t)m); }
/* the last '/' part of a key's full name ("" when unknown), as names.get(k, '').split('/')[-1] */
static void last_part(const Nm *names, uint32_t k, const uint8_t **s, int *n)
{
    const NmE *e = nm_get(names, k);
    *s = (const uint8_t *)""; *n = 0;
    if (!e) return;
    int i = e->n; while (i > 0 && e->s[i - 1] != '/') i--;
    *s = e->s + i; *n = e->n - i;
}
/* races(): the race_bin_<n> collections with checkpoints -> name, type, waypoints (start
   grid, checkpoints in order, the finish line unless a circuit), barrier names */
static void write_shops(const PosE *pos, int npos, const char *path, long *nshops);
static int read_races(const uint8_t *gp, size_t gn, Race **out, int *nout, const char *shops_path, long *nshops, int carbon)
{
    const size_t xst = carbon ? 16 : 20, xoff = carbon ? 12 : 16;   /* export entry stride, its data offset */
    const unsigned inl = carbon ? 0x40 : 0x20;
    *out = NULL; *nout = 0;
    if (gn < 0x10 || memcmp(gp, "VPAK", 4)) return 0;
    uint32_t n = u32le(gp + 4), soff = u32le(gp + 8);
    if ((size_t)0x10 + (size_t)n * 20 > gn) return 0;
    Nm names = {0};
    PosE *pos = NULL; int npos = 0, cpos = 0;
    Race *R = NULL; int nr = 0, cr = 0, ok = 1;
    for (uint32_t i = 0; i < n && ok; i++) {
        const uint8_t *e = gp + 0x10 + 20 * i;
        uint32_t bsz = u32le(e + 4), bo = u32le(e + 12);
        if ((size_t)bo + bsz <= gn && !nm_bin(&names, gp + bo, bsz, 0)) ok = 0;
    }
    for (uint32_t i = 0; i < n && ok; i++) {
        const uint8_t *e = gp + 0x10 + 20 * i;
        uint32_t no = u32le(e), bsz = u32le(e + 4), vsz = u32le(e + 8), bo = u32le(e + 12), vo = u32le(e + 16);
        if ((size_t)vo + vsz > gn || (size_t)bo + bsz > gn || (size_t)soff + no >= gn) continue;
        const char *vname = (const char *)gp + soff + no;
        const uint8_t *b = gp + bo, *vl = gp + vo;
        int racebin = !strncmp(vname, "race_bin_", 9) && vname[9];
        for (const char *q = vname + 9; racebin && !carbon && *q; q++) if (*q < '0' || *q > '9') racebin = 0;
        const uint8_t *exp = NULL, *ptr = NULL; size_t expn = 0, ptrn = 0;
        for (size_t o = 0; o + 8 <= vsz; ) {                             /* chunks: tag reversed, size with the header */
            uint32_t sz = u32le(vl + o + 4);
            if (sz >= 8 && o + sz <= vsz) {                              /* a dict of tags: the last one */
                if (!memcmp(vl + o, "NpxE", 4)) { exp = vl + o + 8; expn = sz - 8; }
                if (!memcmp(vl + o, "NrtP", 4)) { ptr = vl + o + 8; ptrn = sz - 8; }
            }
            if (sz < 8) break;
            o += sz;
        }
        if (!exp || !ptr || expn < 4) continue;
        size_t nf = ptrn / 12, nfix = 0;
        FixE *fx = malloc((nf ? nf : 1) * sizeof *fx);
        if (!fx) { ok = 0; break; }
        for (size_t k = 0; k < nf; k++) {
            const uint8_t *f = ptr + 12 * k;
            if (u16le(f + 4) == 3) { fx[nfix].fo = u32le(f); fx[nfix].ix = u16le(f + 6); fx[nfix].de = u32le(f + 8); fx[nfix].ord = (int)k; nfix++; }
        }
        qsort(fx, nfix, sizeof *fx, fix_cmp);
        Nm vn = {0};                                                    /* this vault's own names (mwvlt) */
        if (racebin && !nm_bin(&vn, b, bsz, 1)) { free(fx); ok = 0; break; }
        uint32_t ncol = u32le(exp);
        for (uint32_t c = 0; c < ncol && ok && 4 + xst * (c + 1) <= expn; c++) {
            const uint8_t *x = exp + 4 + xst * c;
            if (u32le(x + 4) != VCOL) continue;
            uint32_t off = u32le(x + xoff);
            if ((size_t)off + 32 > vsz) continue;
            const uint8_t *col = vl + off;
            uint32_t key = u32le(col), ne = u32le(col + 20); int nt = u16le(col + 24);
            size_t eo = (size_t)off + 32 + 4 * (size_t)nt;
            if (carbon) eo = (size_t)off + ((32 + 4 * (size_t)nt + 7) & ~(size_t)7);
            if (eo + (size_t)12 * ne > vsz) continue;
            /* the last entry of each field (a dict): 0 none, 1 inline, 2 pointer (de), 3 other */
            int kpos = 0, kcps = 0, kchild = 0, kbar = 0, ktr = 0; uint32_t dpos = 0, dcps = 0, dchild = 0, dbar = 0, dtr = 0;
            float rot = 0, tlim = 0; int tb = -1;
            for (uint32_t j = 0; j < ne; j++) {
                const uint8_t *en = vl + eo + 12 * j;
                uint32_t k = u32le(en), de = 0; int kind;
                if (u16le(en + 10) & inl) kind = 1;
                else kind = fix_get(fx, nfix, (uint32_t)(eo + 12 * j + 4), 0, &de) ? 2 : 3;
                if (k == F_POS) { kpos = kind; dpos = de; }
                else if (k == F_CPS) { kcps = kind; dcps = de; }
                else if (k == F_CHILD) { kchild = kind; dchild = de; }
                else if (k == F_BARRIERS) { kbar = kind; dbar = de; }
                else if (k == F_ROT && kind == 1) rot = f32le(en + 4);
                else if (k == F_TLIMIT && kind == 1) tlim = f32le(en + 4);
                else if (k == F_TBONUS && kind == 1) tb = (int)u32le(en + 4);
                else if (k == F_TRAPS) { ktr = kind; dtr = de; }
            }
            if (kpos == 2 && (size_t)dpos + 12 <= bsz) {
                if (npos == cpos) { int cc = cpos ? cpos * 2 : 1024; PosE *p2 = realloc(pos, (size_t)cc * sizeof *p2); if (!p2) { ok = 0; break; } pos = p2; cpos = cc; }
                pos[npos].key = key; pos[npos].ord = npos; pos[npos].rot = rot; pos[npos].tb = tb;
                pos[npos].p.x = f32le(b + dpos); pos[npos].p.y = f32le(b + dpos + 4); pos[npos].p.z = f32le(b + dpos + 8); npos++;
            }
            if (!racebin || !kcps) continue;
            const NmE *nm = nm_get(&vn, key);
            if (!nm && carbon) nm = nm_get(&names, key);
            if (!nm) continue;
            int slash = -1, nsl = 0; for (int q = 0; q < nm->n; q++) if (nm->s[q] == '/') { nsl++; slash = q; }
            if (nsl != 1) continue;
            int tl = nm->n - slash - 1; if (tl > 63) tl = 63;
            if (nr == cr) { int cc = cr ? cr * 2 : 256; Race *r2 = realloc(R, (size_t)cc * sizeof *r2); if (!r2) { ok = 0; break; } R = r2; cr = cc; }
            Race *r = &R[nr++]; memset(r, 0, sizeof *r);
            memcpy(r->name, nm->s + slash + 1, (size_t)tl); r->order = nr - 1; r->par = u32le(col + 8);
            if (kcps == 2) r->cps = u32_array(b, bsz, dcps, &r->ncps);
            if (kchild == 2) r->child = u32_array(b, bsz, dchild, &r->nchild);
            if (ktr == 2) r->traps = u32_array(b, bsz, dtr, &r->ntraps);
            r->tlim = tlim;
            if (carbon && kbar == 2 && (size_t)dbar + 8 <= bsz) {   /* Carbon: s16 numbers, negative = '*' (port guess) */
                int cnt = u16le(b + dbar);
                r->bar = calloc((size_t)(cnt ? cnt : 1), 24);
                if (!r->bar) { ok = 0; break; }
                for (int q = 0; q < cnt && (size_t)dbar + 8 + 2 * (size_t)q + 2 <= bsz; q++) {
                    int v = (int16_t)u16le(b + dbar + 8 + 2 * q);
                    if (v) snprintf(r->bar[r->nbar++], 24, "%sBARRIER_SPLINE_%d", v < 0 ? "*" : "", v < 0 ? -v : v);
                }
            } else if (kbar == 2 && (size_t)dbar + 8 <= bsz) {
                int cnt = u16le(b + dbar);
                r->bar = calloc((size_t)(cnt ? cnt : 1), 24);
                if (!r->bar) { ok = 0; break; }
                for (int q = 0; q < cnt; q++) {
                    uint32_t sde;
                    if (!fix_get(fx, nfix, dbar + 8 + 4 * (uint32_t)q, 1, &sde) || sde >= bsz) continue;
                    size_t sl = 0; while (sde + sl < bsz && b[sde + sl]) sl++;
                    if (sl >= 24 || !is_barrier_name(b + sde, sl)) continue;   /* mkevents.py: "odd barrier entry" */
                    memcpy(r->bar[r->nbar++], b + sde, sl);
                }
            }
        }
        free(vn.e); free(fx);
    }
    if (ok) qsort(pos, (size_t)npos, sizeof *pos, pos_cmp);
    if (ok && shops_path) write_shops(pos, npos, shops_path, nshops);
    /* the waypoints; races without a type, a start grid or checkpoints go */
    int w = 0;
    for (int i = 0; i < nr && ok; i++) {
        Race *r = &R[i];
        r->type = carbon ? carbon_type(r->par) : race_type(r->name);
        r->mode = carbon ? carbon_mode(r->par) : mw_mode(r->name);
        r->closed = r->type == RT_CIRCUIT ||
                    (r->type == RT_DRIFT && r->par == vlt_hash((const uint8_t *)"driftrace", 9));
        const PosE *sg = NULL, *fl = NULL;
        if (r->type >= 0) {
            int sgi = -1;
            for (int j = 0; j < r->nchild && sgi < 0; j++) { const uint8_t *s; int sn; last_part(&names, r->child[j], &s, &sn); if (ends_with(s, sn, "startgrid")) sgi = j; }
            if (sgi >= 0) {
                const uint8_t *s0; int n0; last_part(&names, r->child[sgi], &s0, &n0);
                uint32_t k = r->child[sgi];
                for (int j = 0; j < r->nchild; j++) { const uint8_t *s; int sn; last_part(&names, r->child[j], &s, &sn); if (sn == n0 && !memcmp(s, s0, (size_t)sn)) k = r->child[j]; }
                sg = pos_find(pos, npos, k);
            }
            int fli = -1;
            for (int j = 0; j < r->nchild; j++) { const uint8_t *s; int sn; last_part(&names, r->child[j], &s, &sn); if (sn == 10 && !memcmp(s, "finishline", 10)) fli = j; }
            if (fli >= 0) fl = pos_find(pos, npos, r->child[fli]);
        }
        int ncp = 0;
        for (int j = 0; j < r->ncps; j++) if (pos_find(pos, npos, r->cps[j])) ncp++;
        if (getenv("XP_DEBUG_TYPES") && r->type < 0) {
            const NmE *pn = nm_get(&names, r->par);
            fprintf(stderr, "xp type: %s par %.*s\n", r->name, pn ? (int)pn->n : 8, pn ? (const char *)pn->s : "?");
        }
        if (r->type < 0 || !sg || !ncp) { free(r->cps); free(r->child); free(r->bar); free(r->wp); free(r->traps); continue; }
        /* the event's own rules' markers: time bonus gates (children time_bonus_checkpoint<n>,
           their TimeBonus) and the speed traps (SpeedTrapList) */
        if (r->mode == XM_CHECKPOINT && r->tlim <= 0) r->tlim = 90.0f;   /* gpcore tollboothrace TimeLimit */
        {
            size_t gl = 0;
            for (int j = 0; j < r->nchild; j++) {
                const PosE *p = pos_find(pos, npos, r->child[j]);   /* time_bonus_checkpoint<n>, checkpoint<n>_timebonus */
                if (!p || p->tb <= 0 || gl + 64 > sizeof r->gates) continue;
                gl += (size_t)snprintf(r->gates + gl, sizeof r->gates - gl, "T %.1f %.1f %.1f %d|", p->p.x, p->p.y, p->p.z, p->tb);
            }
            for (int j = 0; j < r->ntraps; j++) {
                const PosE *p = pos_find(pos, npos, r->traps[j]);
                if (!p || gl + 64 > sizeof r->gates) continue;
                gl += (size_t)snprintf(r->gates + gl, sizeof r->gates - gl, "S %.1f %.1f %.1f 0|", p->p.x, p->p.y, p->p.z);
            }
        }
        free(r->traps); r->traps = NULL;
        r->wp = malloc((size_t)(ncp + 2) * sizeof *r->wp);
        if (!r->wp) { ok = 0; break; }
        r->wp[r->nwp++] = sg->p;
        for (int j = 0; j < r->ncps; j++) { const PosE *p = pos_find(pos, npos, r->cps[j]); if (p) r->wp[r->nwp++] = p->p; }
        if (fl && !r->closed) r->wp[r->nwp++] = fl->p;
        free(r->cps); free(r->child); r->cps = NULL; r->child = NULL;
        R[w++] = *r;
    }
    if (!ok) { for (int i = 0; i < w; i++) { free(R[i].bar); free(R[i].wp); } free(R); free(names.e); free(pos); return 0; }
    free(names.e); free(pos);
    *out = R; *nout = w;
    return 1;
}

/* the city's garages and shops -> shops.txt ("type x y z heading_deg", beyond.c): each
   menu gate's respawn marker (where MW puts the car down outside it, facing out) --
   safehouses + car lots as garages (0), the customization shops cycled through
   performance / paint / body / specialties (3 1 2 5, as the World Expansion's) */
static void write_shops(const PosE *pos, int npos, const char *path, long *nshops)
{
    static const struct { const char *fmt; int type; } gate[] = {
        { "menu_gate_safehouse/zone_safehouse_%d/respawn", 0 },
        { "menu_gate_carlot/zone_carlot_%d/respawn", 0 },
        { "menu_gate_customshop/zone_customshop_%d/respawn", -1 },
    };
    static const int cycle[4] = { 3, 1, 2, 5 };
    FILE *f = fopen(path, "w");
    if (!f) return;
    int nshop = 0, ok = 1;
    for (int g = 0; g < 3; g++)
        for (int i = 1; i <= 16; i++) {
            char nm[80]; snprintf(nm, sizeof nm, gate[g].fmt, i);
            const PosE *p = pos_find(pos, npos, vlt_hash((const uint8_t *)nm, strlen(nm)));
            if (!p) continue;
            int t = gate[g].type >= 0 ? gate[g].type : cycle[(i - 1) % 4];
            double h = fmod(p->rot, 360.0); if (h > 180) h -= 360; if (h <= -180) h += 360;
            if (fprintf(f, "%d %.1f %.1f %.2f %.1f\n", t, p->p.x, p->p.y, p->p.z, h) < 0) ok = 0;
            nshop++;
        }
    /* Carbon: named gates, no customization shops (it customizes in the safehouse): the
       safehouses are garages, its four car lots the four UG2 shops (port choice) */
    static const char *cgate[] = {
        "menu_gate_safehouse/safehouse_tuner/respawn", "menu_gate_safehouse/safehouse_muscle/respawn",
        "menu_gate_safehouse/safehouse_exotic/respawn", "menu_gate_safehouse/safehouse_highway/respawn",
        "menu_gate_carlot/carlot_tuner01/respawn", "menu_gate_carlot/carlot_muscle/respawn",
        "menu_gate_carlot/carlot_exotic/respawn", "menu_gate_carlot/carlot_tuner02/respawn",
    };
    for (int i = 0; i < 8; i++) {
        const PosE *p = pos_find(pos, npos, vlt_hash((const uint8_t *)cgate[i], strlen(cgate[i])));
        if (!p) continue;
        int t = i < 4 ? 0 : cycle[i - 4];
        double h = fmod(p->rot, 360.0); if (h > 180) h -= 360; if (h <= -180) h += 360;
        if (fprintf(f, "%d %.1f %.1f %.2f %.1f\n", t, p->p.x, p->p.y, p->p.z, h) < 0) ok = 0;
        nshop++;
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) remove(path);
    if (nshops) *nshops = ok ? nshop : 0;
}

/* ---- race barriers: each BARRIER_SPLINE group's wall segments (mwbar.py) ---- */
typedef struct { int gid, ord; double x0, y0, x1, y1; } BSeg;
typedef struct { char (*name)[24]; int ngid; BSeg *s; int ns, cs; } Bars;
static int bseg_cmp(const void *x, const void *y)
{
    const BSeg *a = x, *b = y;
    if (a->gid != b->gid) return a->gid < b->gid ? -1 : 1;
    return (a->ord > b->ord) - (a->ord < b->ord);
}
/* L2RA.BUN 0x34109: {u32, u32, u32 name hash, u16 id, u16 count, u32, u16 members[count]} padded to 4 */
static int bar_names(const char *companion, Bars *B)
{
    size_t n; uint8_t *d = read_top(companion, 0x34109, &n);
    if (!d) return 0;
    B->ngid = 65536;
    B->name = calloc(65536, 24);
    if (!B->name) { free(d); return 0; }
    enum { NK = 10000 + 10 + 100 };   /* Carbon numbers up to 80xx */
    static uint32_t kh[NK]; static char kn[NK][24]; static int kdone;
    if (!kdone) {
        int m = 0;
        for (int k = 0; k < 10000; k++) { snprintf(kn[m], 24, "BARRIER_SPLINE_%d", k); kh[m] = bstr_hash(kn[m]); m++; }
        for (int k = 0; k < 10; k++) { snprintf(kn[m], 24, "BARRIER_SPLINE_%02d", k); kh[m] = bstr_hash(kn[m]); m++; }
        for (int k = 0; k < 100; k++) { snprintf(kn[m], 24, "BARRIER_SPLINE_%03d", k); kh[m] = bstr_hash(kn[m]); m++; }
        kdone = 1;
    }
    for (size_t p = 0; p + 20 <= n; ) {
        if (!u32le(d + p)) { p += 4; continue; }   /* Carbon pads between groups */
        uint32_t h = u32le(d + p + 8); int gid = u16le(d + p + 12), cnt = u16le(d + p + 14);
        for (int k = 0; k < NK; k++) if (kh[k] == h) { memcpy(B->name[gid], kn[k], 24); break; }   /* the first format (setdefault) */
        p = (p + 20 + 2 * (size_t)cnt + 3) & ~(size_t)3;
    }
    free(d); return 1;
}
static double round2(double v) { return pyround(v * 100.0) / 100.0; }
static int carp_walls(const uint8_t *c, size_t s, Bars *B, uint32_t *art, uint32_t *gen, uint32_t g)
{
    size_t f0 = 0; while (f0 < s && c[f0] == 0x11) f0++;
    const uint8_t *h = c + f0; size_t hn = s - f0, ioff = 0; uint32_t icnt = 0; int have = 0;
    for (size_t q = 0x40; q + 16 <= hn; q += 16) {
        const uint8_t *e = h + q;
        int ac = e[2] == 'a' && e[3] == 'c', ic = e[2] == 'i' && e[3] == 'c';
        if (!ac && !ic) break;
        uint32_t cnt = u32le(e + 8), off = u32le(e + 12);
        if (ac) { int ai = e[0] | e[1] << 8; art[ai] = (uint32_t)(q + off); gen[ai] = g; }
        else { ioff = q + off; icnt = cnt; have = 1; }
    }
    for (uint32_t k = 0; have && k < icnt && ioff + 64 * ((size_t)k + 1) <= hn; k++) {
        const uint8_t *r = h + ioff + 64 * (size_t)k;
        int gid = u16le(r + 0x18), ai = u16le(r + 0x1a);
        if (!B->name[gid][0] || gen[ai] != g) continue;
        double ip[3] = { f32le(r + 0x30), f32le(r + 0x34), f32le(r + 0x38) };
        size_t a = art[ai];
        if (a + 16 > hn) continue;
        int ssz = u16le(h + a + 2), nb = u16le(h + a + 4), bsz = u16le(h + a + 6);
        size_t wb = a + 16 + (size_t)ssz;
        for (int i = 0; i < nb; i++) {
            if (32 * (i + 1) > bsz || wb + 32 * (size_t)i + 28 > hn) break;
            const uint8_t *w = h + wb + 32 * (size_t)i;
            double p0[3] = { f32le(w) - ip[0], f32le(w + 4) - ip[1], f32le(w + 8) - ip[2] };
            double p1[3] = { f32le(w + 16) - ip[0], f32le(w + 20) - ip[1], f32le(w + 24) - ip[2] };
            if (B->ns == B->cs) { int cc = B->cs ? B->cs * 2 : 4096; BSeg *n2 = realloc(B->s, (size_t)cc * sizeof *n2); if (!n2) return 0; B->s = n2; B->cs = cc; }
            BSeg *sg = &B->s[B->ns]; sg->gid = gid; sg->ord = B->ns;   /* MW physics (x, y up, z) -> world (z, -x, y) */
            sg->x0 = round2(p0[2]); sg->y0 = round2(-p0[0]); sg->x1 = round2(p1[2]); sg->y1 = round2(-p1[0]);
            B->ns++;
        }
    }
    return 1;
}
/* every CARP 0x3b801 chunk of the stream, read one at a time (the stream is 500 MB) */
static int bar_walls(const char *stream, Bars *B)
{
    FILE *f = fopen(stream, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f);
    uint32_t *art = calloc(65536, 4), *gen = calloc(65536, 4), g = 0;
    struct { long o, e; } st[32]; int sp = 0, ok = art && gen && len > 0;
    long o = 0, e = len;
    uint8_t *buf = NULL; size_t bcap = 0;
    while (ok) {
        if (o + 8 > e) { if (!sp) break; sp--; o = st[sp].o; e = st[sp].e; continue; }
        uint8_t hd[8];
        if (fseek(f, o, SEEK_SET) || fread(hd, 1, 8, f) != 8) break;
        uint32_t id = u32le(hd), s = u32le(hd + 4);
        if ((long long)o + 8 + s > (long long)e) { if (!sp) break; sp--; o = st[sp].o; e = st[sp].e; continue; }
        if (id == 0x3b801) {
            if (s > bcap) { uint8_t *nb = realloc(buf, s); if (!nb) { ok = 0; break; } buf = nb; bcap = s; }
            if (fread(buf, 1, s, f) != s) { ok = 0; break; }
            if (!carp_walls(buf, s, B, art, gen, ++g)) ok = 0;
            o += 8 + (long)s;
        } else if (id & 0x80000000u) {
            if (sp == 32) { ok = 0; break; }
            st[sp].o = o + 8 + (long)s; st[sp].e = e; sp++;
            e = o + 8 + (long)s; o += 8;
        } else o += 8 + (long)s;
    }
    free(buf); free(art); free(gen); fclose(f);
    if (ok) qsort(B->s, (size_t)B->ns, sizeof *B->s, bseg_cmp);
    return ok;
}
/* the wall segments of a barrier name ('*' stripped): every group of that name */
static int bar_segs(const Bars *B, const char *name, const BSeg **out, int *nout, int which)
{
    if (*name == '*') name++;
    int seen = 0;
    for (int gid = 0; gid < B->ngid; gid++) {
        if (!B->name[gid][0] || strcmp(B->name[gid], name)) continue;
        int lo = 0, hi = B->ns;
        while (lo < hi) { int m = (lo + hi) / 2; if (B->s[m].gid < gid) lo = m + 1; else hi = m; }
        int end = lo; while (end < B->ns && B->s[end].gid == gid) end++;
        if (end == lo) continue;
        if (seen++ == which) { *out = B->s + lo; *nout = end - lo; return 1; }
    }
    return 0;
}

/* ---- lane points on a 50 m grid (mkevents idx) ---- */
typedef struct { int gx, gy, li, k; } GEnt;
typedef struct { GEnt *e; int n; } Grid;
static int gent_cmp(const void *x, const void *y)
{
    const GEnt *a = x, *b = y;
    if (a->gx != b->gx) return a->gx < b->gx ? -1 : 1;
    if (a->gy != b->gy) return a->gy < b->gy ? -1 : 1;
    if (a->li != b->li) return a->li < b->li ? -1 : 1;
    return (a->k > b->k) - (a->k < b->k);
}
static int cell(double v, double c) { return (int)floor(v / c); }
static const GEnt *grid_cell(const Grid *G, int gx, int gy, int *n)
{
    int lo = 0, hi = G->n;
    while (lo < hi) { int m = (lo + hi) / 2; const GEnt *q = &G->e[m]; if (q->gx < gx || (q->gx == gx && q->gy < gy)) lo = m + 1; else hi = m; }
    int e = lo; while (e < G->n && G->e[e].gx == gx && G->e[e].gy == gy) e++;
    *n = e - lo; return G->e + lo;
}
static D3 lpt(const G *g, int li, int k) { V3 v = g->l[li].pts.p[k]; D3 d = { v.x, v.y, v.z }; return d; }
typedef struct { int li; double d; } NL;
typedef struct { NL *v; int n, cap; } NLs;
/* lanes passing within r of p; with a travel direction d, only those running its way */
static int near_lanes(const G *g, const Grid *G2, D3 p, double r, const double *d, NLs *o)
{
    o->n = 0;
    int cx = cell(p.x, 50), cy = cell(p.y, 50);
    for (int gx = cx - 1; gx <= cx + 1; gx++)
        for (int gy = cy - 1; gy <= cy + 1; gy++) {
            int n; const GEnt *c = grid_cell(G2, gx, gy, &n);
            for (int i = 0; i < n; i++) {
                int li = c[i].li, k = c[i].k;
                D3 q = lpt(g, li, k);
                double dd = hypot(q.x - p.x, q.y - p.y);
                int at = -1; for (int j = 0; j < o->n; j++) if (o->v[j].li == li) { at = j; break; }
                if (dd >= r || dd >= (at >= 0 ? o->v[at].d : 1e9)) continue;
                if (d) {
                    int np = g->l[li].pts.n;
                    D3 a = lpt(g, li, k > 0 ? k - 1 : 0), b = lpt(g, li, k + 1 < np ? k + 1 : np - 1);
                    if ((b.x - a.x) * d[0] + (b.y - a.y) * d[1] <= 0) continue;
                }
                if (at >= 0) { o->v[at].d = dd; continue; }
                if (o->n == o->cap) { int cc = o->cap ? o->cap * 2 : 32; NL *v = realloc(o->v, (size_t)cc * sizeof *v); if (!v) return 0; o->v = v; o->cap = cc; }
                o->v[o->n].li = li; o->v[o->n].d = dd; o->n++;
            }
        }
    if (!o->n && r < 60) return near_lanes(g, G2, p, r * 2, d, o);
    return 1;
}
static double orient(double px, double py, double qx, double qy, double rx, double ry) { return (qx - px) * (ry - py) - (qy - py) * (rx - px); }
static int seg_cross(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy)
{
    return orient(ax, ay, bx, by, cx, cy) * orient(ax, ay, bx, by, dx, dy) < 0 &&
           orient(cx, cy, dx, dy, ax, ay) * orient(cx, cy, dx, dy, bx, by) < 0;
}
/* lanes crossing one of the wall segments: stamp[li] = tag (seen: scratch) */
static void blocked_lanes(const G *g, const Grid *G2, const BSeg *s, int ns, int *stamp, int tag, int *seen, int *seentag)
{
    for (int i = 0; i < ns; i++) {
        int st = ++*seentag;
        double x0 = s[i].x0, y0 = s[i].y0, x1 = s[i].x1, y1 = s[i].y1;
        for (int gx = cell(fmin(x0, x1), 50) - 1; gx <= cell(fmax(x0, x1), 50) + 1; gx++)
            for (int gy = cell(fmin(y0, y1), 50) - 1; gy <= cell(fmax(y0, y1), 50) + 1; gy++) {
                int n; const GEnt *c = grid_cell(G2, gx, gy, &n);
                for (int j = 0; j < n; j++) {
                    int li = c[j].li;
                    if (stamp[li] == tag || seen[li] == st) continue;
                    seen[li] = st;
                    const Poly *P = &g->l[li].pts;
                    for (int k = 0; k + 1 < P->n; k++)
                        if (seg_cross(P->p[k].x, P->p[k].y, P->p[k + 1].x, P->p[k + 1].y, x0, y0, x1, y1)) { stamp[li] = tag; break; }
                }
            }
    }
}

/* ---- the lane chain (one Dijkstra over (lane, waypoints passed)) ---- */
#define BLOCK 5000.0
typedef struct { double d; int a, k; } HE;
typedef struct { HE *v; int n, cap; } Heap;
static int he_less(const HE *x, const HE *y) { return x->d != y->d ? x->d < y->d : x->a != y->a ? x->a < y->a : x->k < y->k; }
static int heap_push(Heap *h, HE e)
{
    if (h->n == h->cap) { int c = h->cap ? h->cap * 2 : 1024; HE *v = realloc(h->v, (size_t)c * sizeof *v); if (!v) return 0; h->v = v; h->cap = c; }
    int i = h->n++;
    while (i > 0) { int p = (i - 1) / 2; if (!he_less(&e, &h->v[p])) break; h->v[i] = h->v[p]; i = p; }
    h->v[i] = e; return 1;
}
static HE heap_pop(Heap *h)
{
    HE top = h->v[0], e = h->v[--h->n]; int i = 0;
    for (;;) {
        int c = 2 * i + 1; if (c >= h->n) break;
        if (c + 1 < h->n && he_less(&h->v[c + 1], &h->v[c])) c++;
        if (!he_less(&h->v[c], &e)) break;
        h->v[i] = h->v[c]; i = c;
    }
    if (h->n) h->v[i] = e;
    return top;
}
typedef struct { const G *g; const Grid *grid; double *dist; int *prev; int *touched; int ntouched; unsigned char *W; Heap heap; NLs nl; } ChainCtx;
static int nl_cmp(const void *x, const void *y) { const NL *a = x, *b = y; return (a->d > b->d) - (a->d < b->d); }
/* the chain (lanes) into *out / *nout; 0 none, -1 out of memory. blk: stamp/tag, or NULL */
static int chain(ChainCtx *C, const Race *r, const int *stamp, int tag, int **out, int *nout)
{
    const G *g = C->g; int nl = g->n, n = r->nwp, circ = r->closed;
    *out = NULL; *nout = 0;
    if (n < 1) return 0;
    memset(C->W, 0, (size_t)n * (size_t)nl);
    for (int k = 0; k < n; k++) {
        if (!near_lanes(g, C->grid, r->wp[k], 25.0, NULL, &C->nl)) return -1;
        if (k && !C->nl.n) return 0;
        for (int j = 0; j < C->nl.n; j++) C->W[(size_t)k * nl + C->nl.v[j].li] = 1;
    }
    double wd[2];
    { D3 a = r->wp[0], b = n > 1 ? r->wp[1] : r->wp[0]; wd[0] = b.x - a.x; wd[1] = b.y - a.y; }
    if (!near_lanes(g, C->grid, r->wp[0], 15.0, wd, &C->nl)) return -1;
    if (!C->nl.n && !near_lanes(g, C->grid, r->wp[0], 15.0, NULL, &C->nl)) return -1;
    if (!C->nl.n) return 0;
    /* stable sort by distance (insertion order on ties), the nearest 4 */
    for (int i = 1; i < C->nl.n; i++) { NL t = C->nl.v[i]; int j = i; while (j > 0 && nl_cmp(&C->nl.v[j - 1], &t) > 0) { C->nl.v[j] = C->nl.v[j - 1]; j--; } C->nl.v[j] = t; }
    int nst = C->nl.n < 4 ? C->nl.n : 4, starts[4];
    for (int i = 0; i < nst; i++) starts[i] = C->nl.v[i].li;
    double best_d = -1; int *best = NULL, nbest = 0;
    for (int si = 0; si < nst; si++) {
        int s = starts[si], k0 = 0;
        while (k0 + 1 < n && C->W[(size_t)(k0 + 1) * nl + s]) k0++;
        for (int i = 0; i < C->ntouched; i++) { C->dist[C->touched[i]] = 1e18; C->prev[C->touched[i]] = -1; }
        C->ntouched = 0; C->heap.n = 0;
        int st0 = s * n + k0, goal = -1;
        C->dist[st0] = 0; C->touched[C->ntouched++] = st0;
        HE h0 = { 0.0, s, k0 };
        if (!heap_push(&C->heap, h0)) { free(best); return -1; }
        while (C->heap.n) {
            HE e = heap_pop(&C->heap);
            int sa = e.a * n + e.k;
            if (e.d > C->dist[sa]) continue;
            if (e.k == n - 1 && (!circ || e.a == s) && sa != st0) { goal = sa; break; }
            if (best && e.d >= best_d) break;
            for (int j = 0; j < g->nsucc[e.a]; j++) {
                int bl = g->succ[e.a][j], kk = e.k;
                while (kk + 1 < n && C->W[(size_t)(kk + 1) * nl + bl]) kk++;
                double nd = e.d + g->l[bl].len + (stamp && stamp[bl] == tag ? BLOCK : 0.0);
                int sb = bl * n + kk;
                if (nd < C->dist[sb]) {
                    if (C->dist[sb] >= 1e18) C->touched[C->ntouched++] = sb;
                    C->dist[sb] = nd; C->prev[sb] = sa;
                    HE he = { nd, bl, kk };
                    if (!heap_push(&C->heap, he)) { free(best); return -1; }
                }
            }
        }
        if (goal < 0) continue;
        int len = 0; for (int x = goal; x >= 0; x = C->prev[x]) len++;
        if (best && C->dist[goal] >= best_d) continue;
        int *ch = malloc((size_t)len * sizeof *ch);
        if (!ch) { free(best); return -1; }
        int i = len; for (int x = goal; x >= 0; x = C->prev[x]) ch[--i] = x / n;
        if (circ) len--;
        free(best); best = ch; nbest = len; best_d = C->dist[goal];
    }
    if (!best) return 0;
    *out = best; *nout = nbest; return 1;
}
static int has_repeat(const int *c, int n, unsigned char *mark)
{
    int rep = 0;
    for (int i = 0; i < n; i++) { if (mark[c[i]]) rep = 1; mark[c[i]] = 1; }
    for (int i = 0; i < n; i++) mark[c[i]] = 0;
    return rep;
}

/* ---- the chain as polylines (writers.section_polylines), unkink, recut ---- */
static int hermite(D3 a, const double ta[2], D3 b, const double tb[2], DPoly *o)
{
    double d = dd2(a, b); int n = (int)(d / 4.0) + 1; if (n < 2) n = 2;
    for (int k = 0; k < n; k++) {
        double t = (double)k / (n - 1), t2 = t * t, t3 = t2 * t;
        double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
        D3 p = { h00 * a.x + h10 * d * ta[0] + h01 * b.x + h11 * d * tb[0],
                 h00 * a.y + h10 * d * ta[1] + h01 * b.y + h11 * d * tb[1], a.z + (b.z - a.z) * t };
        if (!dp_add(o, p)) return 0;
    }
    return 1;
}
static int section_polylines(const DPoly *const *pc, int n, int closed, DPolys *out)
{
    out->n = 0;
    for (int k = 0; k < n; k++) {
        int pi = dps_new(out); if (pi < 0) return 0;
        for (int i = 0; i < pc[k]->n; i++) if (!dp_add(&out->v[pi], pc[k]->p[i])) return 0;
        if (pi > 0) { DPoly *L = &out->v[pi - 1]; if (dd2(L->p[L->n - 1], out->v[pi].p[0]) < 0.5) out->v[pi].p[0] = L->p[L->n - 1]; }
        if (k + 1 < n || closed) {
            const DPoly *Q = pc[(k + 1) % n]; const DPoly *P = &out->v[pi];
            D3 a = P->p[P->n - 1], b = Q->p[0];
            if (dd2(a, b) >= 0.5) {
                double ta[2], tb[2]; dunit(P->p[P->n - 2], P->p[P->n - 1], ta); dunit(Q->p[0], Q->p[1], tb);
                int ci = dps_new(out); if (ci < 0) return 0;
                if (!hermite(a, ta, b, tb, &out->v[ci])) return 0;
            }
        }
    }
    if (closed && out->n) { DPoly *L = &out->v[out->n - 1]; L->p[L->n - 1] = out->v[0].p[0]; }
    return 1;
}
static double poly_len(const DPoly *P) { double s = 0; for (int q = 0; q + 1 < P->n; q++) s += dd2(P->p[q], P->p[q + 1]); return s; }
static int unkink(DPoly *P)
{
    const double lim = cos(75.0 * 3.14159265358979323846 / 180.0);
    D3 *o = malloc((size_t)(P->n ? P->n : 1) * sizeof *o);
    if (!o) return 0;
    int changed = 1;
    while (changed && P->n > 3) {
        changed = 0;
        int m = 0; o[m++] = P->p[0];
        for (int i = 1; i < P->n - 1; i++) {
            D3 a = o[m - 1], b = P->p[i], c = P->p[i + 1];
            double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
            double lu = hypot(ux, uy), lv = hypot(vx, vy);
            if (lu < 1.0 || lv < 1e-6 || (ux * vx + uy * vy) / (lu * lv) < lim) { changed = 1; continue; }
            o[m++] = b;
        }
        o[m++] = P->p[P->n - 1];
        memcpy(P->p, o, (size_t)m * sizeof *o); P->n = m;
    }
    free(o); return 1;
}
/* the chain as one polyline cut into sections of max(200 m, length / 100) */
static int recut(const G *g, const int *ch, int n, int closed, DPolys *lanes)
{
    DPoly *tmp = calloc((size_t)n, sizeof *tmp); const DPoly **pc = malloc((size_t)n * sizeof *pc);
    DPolys sp = {0}; DPoly P = {0}; int ok = tmp && pc;
    for (int k = 0; k < n && ok; k++) {
        for (int i = 0; i < g->l[ch[k]].pts.n && ok; i++) ok = dp_add(&tmp[k], lpt(g, ch[k], i));
        pc[k] = &tmp[k];
    }
    if (ok) ok = section_polylines(pc, n, closed, &sp);
    for (int s = 0; s < sp.n && ok; s++)
        for (int i = 0; i < sp.v[s].n && ok; i++) {
            D3 p = sp.v[s].p[i];
            if (!P.n || dd2(P.p[P.n - 1], p) > 0.01) ok = dp_add(&P, p);
        }
    if (ok) ok = unkink(&P);
    lanes->n = 0;
    if (ok && P.n >= 2) {
        double L = poly_len(&P), want = L / 100.0 > 200.0 ? L / 100.0 : 200.0, run = 0;
        int cur = dps_new(lanes); ok = cur >= 0 && dp_add(&lanes->v[cur], P.p[0]);
        for (int i = 1; i < P.n && ok; i++) {
            run += dd2(P.p[i - 1], P.p[i]);
            ok = dp_add(&lanes->v[cur], P.p[i]);
            if (ok && run >= want && P.n - 1 - i > 3) {
                cur = dps_new(lanes); ok = cur >= 0 && dp_add(&lanes->v[cur], P.p[i]); run = 0;
            }
        }
        if (ok && lanes->v[cur].n <= 1) { free(lanes->v[cur].p); lanes->n--; }
    }
    if (tmp) for (int k = 0; k < n; k++) free(tmp[k].p);
    free(tmp); free(pc); dps_free(&sp); free(P.p);
    return ok;
}

/* ---- writers: Paths<id>.bin (its nodes + sections; the shared 0x3414C records are appended
   once every event is known) and Routes<id>F.bin ---- */
typedef struct { uint8_t *b; size_t n, cap; } Buf;
static uint8_t *buf_grow(Buf *b, size_t add)
{
    if (b->n + add > b->cap) {
        size_t c = b->cap ? b->cap : 4096; while (c < b->n + add) c *= 2;
        uint8_t *nb = realloc(b->b, c); if (!nb) return NULL;
        b->b = nb; b->cap = c;
    }
    uint8_t *p = b->b + b->n; memset(p, 0, add); b->n += add; return p;
}
typedef struct { double x, y; int s; } Node;
typedef struct { int gx, gy, i; } NEnt;
static int nent_cmp(const void *x, const void *y)
{
    const NEnt *a = x, *b = y;
    if (a->gx != b->gx) return a->gx < b->gx ? -1 : 1;
    if (a->gy != b->gy) return a->gy < b->gy ? -1 : 1;
    return (a->i > b->i) - (a->i < b->i);
}
typedef struct { double d; int j; } Cand;
static int cand_cmp(const void *x, const void *y)
{
    const Cand *a = x, *b = y;
    if (a->d != b->d) return a->d < b->d ? -1 : 1;
    return (a->j > b->j) - (a->j < b->j);
}
/* 1 ok, 0 out of memory, -1 the format can't hold it (too many nodes / waypoints) */
static int paths_bin(const DPolys *S, int ev_id, int closed, Buf *part, uint8_t rec[0x110])
{
    int nn = 0; for (int s = 0; s < S->n; s++) nn += S->v[s].n;
    if (nn >= 32768) return -1;
    Node *nd = malloc((size_t)(nn ? nn : 1) * sizeof *nd); NEnt *ge = malloc((size_t)(nn ? nn : 1) * sizeof *ge);
    Cand *cd = NULL; int ccap = 0, ok = nd && ge, m = 0;
    int *first = malloc((size_t)(S->n ? S->n : 1) * sizeof *first);
    ok = ok && first;
    for (int s = 0; s < S->n && ok; s++) { first[s] = m; for (int i = 0; i < S->v[s].n; i++) { nd[m].x = S->v[s].p[i].x; nd[m].y = S->v[s].p[i].y; nd[m].s = s; m++; } }
    for (int i = 0; i < nn && ok; i++) { ge[i].gx = cell(nd[i].x, 30); ge[i].gy = cell(nd[i].y, 30); ge[i].i = i; }
    if (ok) qsort(ge, (size_t)nn, sizeof *ge, nent_cmp);
    uint8_t *nb = ok ? buf_grow(part, 8 + 0x18 * (size_t)nn) : NULL;
    if (!nb) ok = 0;
    else { pu32(nb, 0x34148); pu32(nb + 4, 0x18 * (uint32_t)nn); nb += 8; }
    for (int i = 0; i < nn && ok; i++) {
        int nc = 0, cx = cell(nd[i].x, 30), cy = cell(nd[i].y, 30);
        for (int a = -2; a <= 2; a++)
            for (int b = -2; b <= 2; b++) {
                int gx = cx + a, gy = cy + b, lo = 0, hi = nn;
                while (lo < hi) { int q = (lo + hi) / 2; if (ge[q].gx < gx || (ge[q].gx == gx && ge[q].gy < gy)) lo = q + 1; else hi = q; }
                for (int q = lo; q < nn && ge[q].gx == gx && ge[q].gy == gy; q++) {
                    int j = ge[q].i;
                    if (nd[j].s == nd[i].s) continue;
                    double d = hypot(nd[j].x - nd[i].x, nd[j].y - nd[i].y);
                    if (d > 60) continue;
                    if (nc == ccap) { int cc = ccap ? ccap * 2 : 256; Cand *c2 = realloc(cd, (size_t)cc * sizeof *c2); if (!c2) { ok = 0; break; } cd = c2; ccap = cc; }
                    cd[nc].d = d; cd[nc].j = j; nc++;
                }
            }
        if (!ok) break;
        qsort(cd, (size_t)nc, sizeof *cd, cand_cmp);
        int pick[3], np = 0, ps[3]; double pd0 = 0;
        for (int c = 0; c < nc && np < 3; c++) {                  /* one node per other section, nearest first */
            int s = nd[cd[c].j].s, dup = 0;
            for (int q = 0; q < np; q++) if (ps[q] == s) dup = 1;
            if (dup) continue;
            if (!np) pd0 = cd[c].d;
            ps[np] = s; pick[np++] = cd[c].j;
        }
        uint8_t *r = nb + 0x18 * (size_t)i;
        pf32(r, nd[i].x); pf32(r + 4, nd[i].y); pu16(r + 8, (unsigned)nd[i].s);
        r[0xa] = np ? (uint8_t)(pd0 < 254 ? (int)pd0 : 254) : 0xff; r[0xb] = 0xff;
        for (int q = 0; q < 3; q++) pu16(r + 0xc + 2 * q, q < np ? (unsigned)pick[q] : 0xffffu);
    }
    int n = S->n;
    uint8_t *sb = ok ? buf_grow(part, 8 + 0xdc * (size_t)n) : NULL;
    if (!sb) ok = 0;
    else { pu32(sb, 0x34149); pu32(sb + 4, 0xdc * (uint32_t)n); sb += 8; }
    for (int si = 0; si < n && ok; si++) {
        uint8_t *r = sb + 0xdc * (size_t)si;
        const DPoly *P = &S->v[si];
        int f = first[si], e = f + P->n;
        pu16(r + 0x1c, (unsigned)f); pu16(r + 0x1e, (unsigned)e); pu16(r + 0x20, (unsigned)(e - f));
        int nxt = si + 1 < n ? si + 1 : (closed ? 0 : -1), prv = si > 0 ? si - 1 : (closed ? n - 1 : -1);
        if (prv >= 0) { r[0x22] = 1; pu16(r + 0x24, (unsigned)prv); r[0x26] = 1; }
        if (nxt >= 0) { r[0x23] = 1; pu16(r + 0x24 + 32, (unsigned)nxt); r[0x26 + 32] = 0; }
        pf32(r + 0x64, poly_len(P));
        double x0 = P->p[0].x, y0 = P->p[0].y, x1 = x0, y1 = y0;
        for (int q = 0; q < P->n; q++) { x0 = fmin(x0, P->p[q].x); y0 = fmin(y0, P->p[q].y); x1 = fmax(x1, P->p[q].x); y1 = fmax(y1, P->p[q].y); }
        pf32(r + 0x88, P->p[0].x); pf32(r + 0x8c, P->p[0].y); pf32(r + 0x90, P->p[P->n - 1].x); pf32(r + 0x94, P->p[P->n - 1].y);
        pf32(r + 0x98, x0); pf32(r + 0x9c, y0); pf32(r + 0xa0, x1); pf32(r + 0xa4, y1);
    }
    /* waypoints on the sections: the start (+ a point ~20 m on, for the race direction), the
       middle of every 3rd section (the game pathfinds <= 10 sections between two), the end */
    double wp[40][2]; int nw = 0, every = (n + 29) / 30 > 3 ? (n + 29) / 30 : 3, rc = 1;
    for (int si = 0; si < n && ok && rc > 0; si++) {
        const DPoly *P = &S->v[si];
        if (si == 0) {
            int k1 = P->n - 1 < 1 ? P->n - 1 : 1;
            if (nw >= 40) { rc = -1; break; }
            wp[nw][0] = P->p[k1].x; wp[nw][1] = P->p[k1].y; nw++;
            double acc = 0; int k = k1;
            while (k + 1 < P->n - 1 && acc < 20) { acc += dd2(P->p[k], P->p[k + 1]); k++; }
            if (k > k1) { if (nw >= 40) { rc = -1; break; } wp[nw][0] = P->p[k].x; wp[nw][1] = P->p[k].y; nw++; }
            continue;
        }
        if (si % every) continue;
        if (nw >= 40) { rc = -1; break; }
        wp[nw][0] = P->p[P->n / 2].x; wp[nw][1] = P->p[P->n / 2].y; nw++;
    }
    if (ok && rc > 0 && n) {
        if (nw >= 40) rc = -1;
        else if (!closed) { const DPoly *P = &S->v[n - 1]; int q = P->n - 2 > 0 ? P->n - 2 : 0; wp[nw][0] = P->p[q].x; wp[nw][1] = P->p[q].y; nw++; }
        else { wp[nw][0] = wp[0][0]; wp[nw][1] = wp[0][1]; nw++; }
    }
    if (ok && rc > 0 && nw > 33) rc = -1;
    if (ok && rc > 0) {
        memset(rec, 0, 0x110);
        pu16(rec, (unsigned)ev_id); rec[2] = (uint8_t)nw; rec[3] = closed ? 1 : 0;
        for (int k = 0; k < nw; k++) { pf32(rec + 8 + 8 * k, wp[k][0]); pf32(rec + 12 + 8 * k, wp[k][1]); }
    }
    free(nd); free(ge); free(cd); free(first);
    return !ok ? 0 : rc;
}
#define PT_ROUTE (0x800u | 0x1000u | 0x40000u | (0x3Fu << 19))
#define EV_SEC_FLAGS 0x7FF141u
#define EV_HALF_W 5.0
static int routes_bin(const DPolys *S, int closed, FILE *f)
{
    int n = S->n, nlk = 0;
    double lap = 0;
    double **pr = calloc((size_t)(n ? n : 1), sizeof *pr);
    if (!pr) return 0;
    int ok = 1;
    for (int s = 0; s < n && ok; s++) {
        pr[s] = malloc((size_t)S->v[s].n * sizeof **pr);
        if (!pr[s]) { ok = 0; break; }
        for (int k = 0; k < S->v[s].n; k++) { if (k) lap += dd2(S->v[s].p[k - 1], S->v[s].p[k]); pr[s][k] = lap; }
    }
    /* links: per section with a next one, forward from its end + backward from the next start */
    typedef struct { double prog; int tp, sp, ts, ss; uint32_t fl; } Lk;
    Lk *lk = ok ? malloc((size_t)(2 * n + 1) * sizeof *lk) : NULL;
    if (!lk) ok = 0;
    for (int si = 0; si < n && ok; si++) {
        int nx = si + 1 < n ? si + 1 : (closed ? 0 : -1);
        if (nx < 0) continue;
        double p = lap ? fmod(pr[si][S->v[si].n - 1], lap) : 0;
        int as = si + 1, ap = S->v[si].n - 1, bs = nx + 1, bp = 0;
        Lk f1 = { p, bp, ap, bs, as, 0x2 }, f2 = { p, ap, bp, as, bs, 0x2 | (1u << 18) };
        lk[nlk++] = f1; lk[nlk++] = f2;
    }
    Buf b = {0};
    size_t secsz = 0; for (int s = 0; s < n; s++) secsz += 0x8c + 0x38 * (size_t)S->v[s].n;
    uint8_t *h = ok ? buf_grow(&b, 8) : NULL;
    if (!h) ok = 0; else { pu32(h, 0x34121); pu32(h + 4, (uint32_t)secsz); }
    for (int si = 0; si < n && ok; si++) {
        const DPoly *P = &S->v[si]; int sid = si + 1;
        uint8_t *r = buf_grow(&b, 0x8c + 0x38 * (size_t)P->n);
        if (!r) { ok = 0; break; }
        pu16(r + 0x0a, (unsigned)sid); pu16(r + 0x34, (unsigned)P->n);
        pf32(r + 0x38, lap ? fmod(pr[si][0], lap) : 0);
        pf32(r + 0x3c, si + 1 < n || !closed ? pr[si][P->n - 1] : lap);
        double x0 = P->p[0].x, y0 = P->p[0].y, x1 = x0, y1 = y0;
        for (int q = 0; q < P->n; q++) { x0 = fmin(x0, P->p[q].x); y0 = fmin(y0, P->p[q].y); x1 = fmax(x1, P->p[q].x); y1 = fmax(y1, P->p[q].y); }
        pf32(r + 0x60, x0 - EV_HALF_W); pf32(r + 0x64, y0 - EV_HALF_W); pf32(r + 0x68, x1 + EV_HALF_W); pf32(r + 0x6c, y1 + EV_HALF_W);
        pu32(r + 0x70, EV_SEC_FLAGS);
        for (int k = 0; k < P->n; k++) {
            uint8_t *q = r + 0x8c + 0x38 * (size_t)k;
            D3 p = P->p[k]; double dv[2], seg = 0;
            if (k + 1 < P->n) { dunit(p, P->p[k + 1], dv); seg = dd2(p, P->p[k + 1]); }
            else dunit(P->p[k - 1], p, dv);
            double pg = pr[si][k];
            if (closed && pg >= lap - 0.01) pg = lap;
            pf32(q, p.x); pf32(q + 4, p.y); pf32(q + 8, pg); pu32(q + 12, PT_ROUTE | (uint32_t)sid);
            double s256 = seg * 256; pu16(q + 0x10, s256 >= 65535 ? 65535u : (unsigned)(int)s256);
            double z8 = pyround(p.z * 8); if (z8 < -32768) z8 = -32768; if (z8 > 32767) z8 = 32767;
            pu16(q + 0x12, (unsigned)(int)z8 & 0xffff);
            pu16(q + 0x14, (unsigned)(int)(EV_HALF_W * 256)); pu16(q + 0x16, (unsigned)(int)(EV_HALF_W * 256));
            pu16(q + 0x18, (unsigned)(int)pyround(dv[0] * 32767) & 0xffff); pu16(q + 0x1a, (unsigned)(int)pyround(dv[1] * 32767) & 0xffff);
            int l0 = -1, l1 = -1;
            for (int j = 0; j < nlk; j++) if (lk[j].ss == sid && lk[j].sp == k) { if (l0 < 0) l0 = j; else if (l1 < 0) l1 = j; }
            pu16(q + 0x1c, l0 >= 0 ? (unsigned)l0 : 0xffffu); pu16(q + 0x1e, l1 >= 0 ? (unsigned)l1 : 0xffffu);
            q[0x2c] = q[0x2d] = 60;
        }
    }
    h = ok ? buf_grow(&b, 8 + 4 + 32 * (size_t)nlk) : NULL;
    if (!h) ok = 0;
    else {
        pu32(h, 0x34122); pu32(h + 4, 4 + 32 * (uint32_t)nlk);
        for (int j = 0; j < nlk; j++) {
            uint8_t *r = h + 12 + 32 * (size_t)j;
            pf32(r, lk[j].prog); pu16(r + 4, (unsigned)lk[j].tp); pu16(r + 6, (unsigned)lk[j].sp);
            pu16(r + 8, (unsigned)lk[j].ts); pu16(r + 10, (unsigned)lk[j].ss); pu32(r + 12, lk[j].fl);
            for (int q = 0; q < 4; q++) pu16(r + 0x18 + 2 * q, 0xffffu);
        }
    }
    if (ok && fwrite(b.b, 1, b.n, f) != b.n) ok = 0;
    free(b.b); free(lk);
    for (int s = 0; s < n; s++) free(pr[s]);
    free(pr);
    return ok;
}

/* ---- the whole set ---- */
typedef struct { int id, type, closed, mode, len; size_t partlen; char start[96]; char *bars; float *pl; int npl; float tlim; char gates[1024]; } EvMeta;   /* pl: the route, x y every ~15 m */
static int write_allrec(const char *dir, const EvMeta *m, int nm, const uint8_t *recs, const unsigned char *keep, int reread)
{
    int nk = 0; for (int i = 0; i < nm; i++) nk += keep[i];
    uint8_t hd[8]; pu32(hd, 0x3414C); pu32(hd + 4, 0x110 * (uint32_t)nk);
    for (int i = 0; i < nm; i++) {
        if (!keep[i]) continue;
        char p[600]; snprintf(p, sizeof p, "%s/Paths%d.bin", dir, m[i].id);
        uint8_t *part = NULL;
        if (reread) {
            FILE *f = fopen(p, "rb"); if (!f) return 0;
            part = malloc(m[i].partlen ? m[i].partlen : 1);
            int got = part && fread(part, 1, m[i].partlen, f) == m[i].partlen;
            fclose(f);
            if (!got) { free(part); return 0; }
        }
        FILE *f = fopen(p, reread ? "wb" : "ab"); if (!f) { free(part); return 0; }
        int ok = (!reread || fwrite(part, 1, m[i].partlen, f) == m[i].partlen) && fwrite(hd, 1, 8, f) == 8;
        for (int j = 0; j < nm && ok; j++) if (keep[j]) ok = fwrite(recs + 0x110 * (size_t)j, 1, 0x110, f) == 0x110;
        if (fclose(f)) ok = 0;
        free(part);
        if (!ok) return 0;
    }
    return 1;
}
int xp_events(const char *gameplay_bin, const char *raw_companion, const char *raw_stream,
              const char *out_dir, int game, const char *city, int first_id, XpEventStats *st)
{
    XpEventStats dummy; if (!st) st = &dummy;
    memset(st, 0, sizeof *st);
    if (game != XP_GAME_MW && game != XP_GAME_CARBON) return XP_ERR_FORMAT;
    size_t gn = 0, clen = 0; int rc = XP_ERR_FORMAT;
    uint8_t *gp = read_all(gameplay_bin, &gn);
    if (!gp) return XP_ERR_OPEN_IN;
    Race *R = NULL; int nr = 0;
    char shops[600]; snprintf(shops, sizeof shops, "%s/shops.txt", out_dir);
    if (!read_races(gp, gn, &R, &nr, shops, &st->shops, game == XP_GAME_CARBON)) { free(gp); return XP_ERR_FORMAT; }
    free(gp);
    st->races = nr;
    uint8_t *chunk = read_road_chunk(raw_companion, &clen, &rc);
    if (!chunk) { free_races(R, nr); return rc; }
    Rn rn; G g; memset(&g, 0, sizeof g);
    Bars B; memset(&B, 0, sizeof B);
    Grid grid = {0};
    ChainCtx C; memset(&C, 0, sizeof C);
    int *stamp = NULL, *seen = NULL, tag = 0, seentag = 0, nm = 0, nrecs = 0;
    EvMeta *meta = NULL; uint8_t *recs = NULL; unsigned char *mark = NULL, *keep = NULL;
    DPolys g2 = {0}, S = {0}; Buf part = {0};
    rc = XP_ERR_FORMAT;
    if (!rn_find(chunk, clen, &rn)) goto done;
    rc = XP_ERR_MEMORY;
    if (!build_graph(&rn, &g)) goto done;
    rc = XP_ERR_OPEN_IN;
    if (!bar_names(raw_companion, &B) || !bar_walls(raw_stream, &B)) goto done;
    st->barrier_segs = B.ns;
    rc = XP_ERR_MEMORY;
    {
        int np = 0; for (int i = 0; i < g.n; i++) np += g.l[i].pts.n;
        grid.e = malloc((size_t)(np ? np : 1) * sizeof *grid.e);
        if (!grid.e) goto done;
        for (int i = 0; i < g.n; i++) for (int k = 0; k < g.l[i].pts.n; k++) {
            GEnt *e = &grid.e[grid.n++]; e->gx = cell(g.l[i].pts.p[k].x, 50); e->gy = cell(g.l[i].pts.p[k].y, 50); e->li = i; e->k = k;
        }
        qsort(grid.e, (size_t)grid.n, sizeof *grid.e, gent_cmp);
    }
    int maxwp = 1; for (int i = 0; i < nr; i++) if (R[i].nwp > maxwp) maxwp = R[i].nwp;
    C.g = &g; C.grid = &grid;
    C.dist = malloc((size_t)g.n * maxwp * sizeof *C.dist); C.prev = malloc((size_t)g.n * maxwp * sizeof *C.prev);
    C.touched = malloc((size_t)g.n * maxwp * sizeof *C.touched); C.W = malloc((size_t)g.n * maxwp);
    stamp = calloc((size_t)g.n, sizeof *stamp); seen = calloc((size_t)g.n, sizeof *seen); mark = calloc((size_t)g.n, 1);
    meta = calloc((size_t)(nr ? nr : 1), sizeof *meta); recs = malloc((size_t)(nr ? nr : 1) * 0x110);
    if (!C.dist || !C.prev || !C.touched || !C.W || !stamp || !seen || !mark || !meta || !recs) goto done;
    for (size_t i = 0; i < (size_t)g.n * maxwp; i++) { C.dist[i] = 1e18; C.prev[i] = -1; }
    qsort(R, (size_t)nr, sizeof *R, race_cmp);
    for (int ri = 0; ri < nr; ri++) {
        Race *r = &R[ri];
        int rt = ++tag;
        for (int b = 0; b < r->nbar; b++) {
            const BSeg *s; int ns;
            for (int w = 0; bar_segs(&B, r->bar[b], &s, &ns, w); w++) blocked_lanes(&g, &grid, s, ns, stamp, rt, seen, &seentag);
        }
        int *ch = NULL, nch = 0, x = chain(&C, r, stamp, rt, &ch, &nch);
        if (x < 0) goto done;
        if (!x || has_repeat(ch, nch, mark)) {     /* no way round its barriers the format holds: the plain shortest one */
            free(ch); ch = NULL;
            x = chain(&C, r, NULL, 0, &ch, &nch); st->fallback++;
            if (x < 0) goto done;
        }
        int thru = 0; for (int i = 0; ch && i < nch; i++) thru += stamp[ch[i]] == rt;
        if (thru) st->through++;
        if (!ch || !nch || has_repeat(ch, nch, mark)) { free(ch); st->skipped++; continue; }
        int closed = r->closed;
        if (!recut(&g, ch, nch, closed, &g2) || !g2.n) { free(ch); if (!g2.n) { st->skipped++; continue; } goto done; }
        const DPoly **pc = malloc((size_t)g2.n * sizeof *pc);
        if (!pc) { free(ch); goto done; }
        for (int i = 0; i < g2.n; i++) pc[i] = &g2.v[i];
        int okp = section_polylines(pc, g2.n, closed, &S); free(pc);
        if (!okp) { free(ch); goto done; }
        int ev = first_id + nm;
        part.n = 0;
        uint8_t *rec = recs + 0x110 * (size_t)nm;
        x = paths_bin(&S, ev, closed, &part, rec);
        if (x <= 0) { free(ch); if (!x) goto done; st->skipped++; continue; }
        double L = 0; for (int s = 0; s < S.n; s++) L += poly_len(&S.v[s]);
        double l100 = pyround(L / 100); rec[5] = (uint8_t)(l100 > 255 ? 255 : l100);
        char p[600]; snprintf(p, sizeof p, "%s/Paths%d.bin", out_dir, ev);
        FILE *f = fopen(p, "wb");
        if (!f) { free(ch); rc = XP_ERR_OPEN_OUT; goto done; }
        int okw = fwrite(part.b, 1, part.n, f) == part.n; if (fclose(f)) okw = 0;
        snprintf(p, sizeof p, "%s/Routes%dF.bin", out_dir, ev);
        f = okw ? fopen(p, "wb") : NULL;
        if (f) { okw = routes_bin(&S, closed, f); if (fclose(f)) okw = 0; } else okw = 0;
        if (!okw) { free(ch); rc = XP_ERR_WRITE; goto done; }
        EvMeta *m = &meta[nm]; m->id = ev; m->type = r->type; m->closed = r->closed; m->mode = r->mode; m->tlim = r->tlim; memcpy(m->gates, r->gates, sizeof m->gates); m->len = (int)pyround(L); m->partlen = part.n;
        /* a barrier the route still crosses (no way round it the format holds) stays off */
        size_t bl = 0; for (int b = 0; b < r->nbar; b++) bl += strlen(r->bar[b]) + 1;
        m->bars = malloc(bl + 1); if (!m->bars) { free(ch); goto done; }
        m->bars[0] = 0;
        for (int b = 0; b < r->nbar; b++) {
            int bt = ++tag, hit = 0;
            const BSeg *s; int ns;
            for (int w = 0; bar_segs(&B, r->bar[b], &s, &ns, w); w++) blocked_lanes(&g, &grid, s, ns, stamp, bt, seen, &seentag);
            for (int i = 0; i < nch && !hit; i++) hit = stamp[ch[i]] == bt;
            if (hit) { st->barriers_off++; continue; }
            st->barriers_on++;
            if (m->bars[0]) strcat(m->bars, " ");
            strcat(m->bars, r->bar[b]);
        }
        /* the start: the first point, heading to the first one 8 m on */
        D3 a = S.v[0].p[0], bq = S.v[0].n > 1 ? S.v[0].p[1] : S.v[1 < S.n ? 1 : 0].p[0]; int fnd = 0;
        for (int s = 0; s < S.n && !fnd; s++) for (int i = 0; i < S.v[s].n; i++) if (dd2(S.v[s].p[i], a) > 8) { bq = S.v[s].p[i]; fnd = 1; break; }
        snprintf(m->start, sizeof m->start, "%d %.1f %.1f %.2f %.1f\n", ev, a.x, a.y, a.z, atan2(bq.y - a.y, bq.x - a.x) * 180.0 / 3.14159265358979323846);
        {   /* the Quick Race / engage track map's line (beyond.c draws it on the city map) */
            size_t tot = 0; for (int q = 0; q < S.n; q++) tot += (size_t)S.v[q].n;
            m->pl = malloc((tot + 1) * 2 * sizeof(float)); m->npl = 0;
            if (m->pl) {
                D3 last = a; int have = 0;
                for (int q = 0; q < S.n; q++) for (int i = 0; i < S.v[q].n; i++) {
                    D3 v = S.v[q].p[i];
                    if (have && dd2(v, last) < 15.0 && !(q == S.n - 1 && i == S.v[q].n - 1)) continue;
                    m->pl[2 * m->npl] = (float)v.x; m->pl[2 * m->npl + 1] = (float)v.y; m->npl++;
                    last = v; have = 1;
                }
            }
        }
        nm++;
        free(ch);
    }
    nrecs = nm;
    keep = malloc((size_t)(nm ? nm : 1));
    if (!keep) goto done;
    memset(keep, 1, (size_t)(nm ? nm : 1));
    rc = XP_ERR_WRITE;
    if (!write_allrec(out_dir, meta, nm, recs, keep, 0)) goto done;
    /* the game's own route builder must take each one; drop those it refuses */
    int bad = 0;
    for (int i = 0; i < nm; i++) {
        char p[600]; snprintf(p, sizeof p, "%s/Paths%d.bin", out_dir, meta[i].id);
        RacePath *rp = racepath_load(p, meta[i].id, 0);
        if (rp) { racepath_free(rp); continue; }
        keep[i] = 0; bad++;
        remove(p); snprintf(p, sizeof p, "%s/Routes%dF.bin", out_dir, meta[i].id); remove(p);
    }
    st->dropped = bad;
    if (bad) {
        /* the records shrink to the events kept */
        int w = 0;
        for (int i = 0; i < nm; i++) if (keep[i]) { if (w != i) { memmove(recs + 0x110 * (size_t)w, recs + 0x110 * (size_t)i, 0x110); free(meta[w].bars); free(meta[w].pl); meta[w] = meta[i]; meta[i].bars = NULL; meta[i].pl = NULL; } w++; }
        nm = w; memset(keep, 1, (size_t)(nm ? nm : 1));
        if (!write_allrec(out_dir, meta, nm, recs, keep, 1)) goto done;
    }
    {
        char p[600]; snprintf(p, sizeof p, "%s/events.txt", out_dir);
        FILE *fe = fopen(p, "w"); snprintf(p, sizeof p, "%s/starts.txt", out_dir);
        FILE *fs = fopen(p, "w"); snprintf(p, sizeof p, "%s/barriers.txt", out_dir);
        FILE *fb = fopen(p, "w"); snprintf(p, sizeof p, "%s/modes.txt", out_dir);
        FILE *fmo = fopen(p, "w"); snprintf(p, sizeof p, "%s/gates.txt", out_dir);
        FILE *fg = fopen(p, "w");
        int okw = fe && fs && fb && fmo && fg, cnt[6 * XM_NMODES] = {0};
        for (int i = 0; i < nm && okw; i++) {
            const char *label = meta[i].mode ? k_mode_label[meta[i].mode] :
                                meta[i].type == RT_CIRCUIT ? "Circuit" : meta[i].type == RT_SPRINT ? "Sprint" :
                                meta[i].type == RT_DRIFT ? "Drift" : "Drag";
            fprintf(fe, "%d %d %d %s %s %d\n", meta[i].id, meta[i].type, meta[i].len, city, label,
                    ++cnt[meta[i].mode * 6 + meta[i].type]);
            if (meta[i].mode) fprintf(fmo, "%d %d %.0f\n", meta[i].id, meta[i].mode, meta[i].tlim);
            for (const char *g = meta[i].gates; meta[i].mode && *g; ) {   /* "id T|S x y z bonus" */
                const char *e = strchr(g, '|'); if (!e) break;
                fprintf(fg, "%d %.*s\n", meta[i].id, (int)(e - g), g);
                g = e + 1;
            }
            fputs(meta[i].start, fs);
            if (meta[i].bars && meta[i].bars[0]) fprintf(fb, "%d %s\n", meta[i].id, meta[i].bars);
            st->events++;
            if (meta[i].type == RT_CIRCUIT) st->circuits++; else if (meta[i].type == RT_SPRINT) st->sprints++;
            else if (meta[i].type == RT_DRIFT) st->drifts++; else st->drags++;
        }
        snprintf(p, sizeof p, "%s/trackpoly.bin", out_dir);   /* "XTPL" n, then per event: id, closed, count, x y ... */
        FILE *ft = fopen(p, "wb");
        if (!ft) okw = 0;
        else {
            uint32_t hd[2] = { 0x4c505458u, (uint32_t)nm };
            if (fwrite(hd, 4, 2, ft) != 2) okw = 0;
            for (int i = 0; i < nm && okw; i++) {
                int32_t eh[3] = { meta[i].id, meta[i].closed, meta[i].pl ? meta[i].npl : 0 };
                if (fwrite(eh, 4, 3, ft) != 3 || (eh[2] && fwrite(meta[i].pl, 8, (size_t)eh[2], ft) != (size_t)eh[2])) okw = 0;
            }
            if (fclose(ft)) okw = 0;
        }
        if (fe && fclose(fe)) okw = 0;
        if (fs && fclose(fs)) okw = 0;
        if (fb && fclose(fb)) okw = 0;
        if (fmo && fclose(fmo)) okw = 0;
        if (fg && fclose(fg)) okw = 0;
        rc = okw ? XP_OK : XP_ERR_WRITE;
    }
done:
    (void)nrecs;
    if (meta) for (int i = 0; i < nr; i++) { free(meta[i].bars); free(meta[i].pl); }
    free(meta); free(recs); free(keep); free(mark); free(stamp); free(seen);
    free(C.dist); free(C.prev); free(C.touched); free(C.W); free(C.heap.v); free(C.nl.v);
    dps_free(&g2); dps_free(&S); free(part.b);
    free(grid.e); free(B.name); free(B.s);
    free_graph(&g); free(chunk); free_races(R, nr);
    return rc;
}
