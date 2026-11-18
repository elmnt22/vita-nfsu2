/* smokeable.c — the retail NFSU2 smackable roadside props (see smokeable.h).
 * Facts and addresses: E:/re-tools/out/SMOKEABLE.md. */
#include "smokeable.h"
#include "resource.h"
#include "acid.h"
#include "n2sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMK_LIVE_MAX   32          /* SmokeableSlotPool (0x86b374) */
#define SMK_MAX_MESH   6           /* meshes of one prop kept for drawing */
#define SMK_STEP       (1.0f / 60.0f)   /* SmokeableMaximumTimeStep 0x7fc858 */

static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const unsigned char *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void cross3(float *o, const float *a, const float *b)
{
    float x = a[1]*b[2] - a[2]*b[1], y = a[2]*b[0] - a[0]*b[2], z = a[0]*b[1] - a[1]*b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static float len3(const float *v) { return sqrtf(dot3(v, v)); }
/* v (model) by the rows of R (EA row-vector convention) */
static void rowmul(float *o, const float *v, const float *R)
{
    float x = v[0]*R[0] + v[1]*R[4] + v[2]*R[8];
    float y = v[0]*R[1] + v[1]*R[5] + v[2]*R[9];
    float z = v[0]*R[2] + v[1]*R[6] + v[2]*R[10];
    o[0] = x; o[1] = y; o[2] = z;
}
static uint32_t bhash(const char *s)                    /* bStringHash */
{
    uint32_t h = 0xffffffffu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}

/* ---- data --------------------------------------------------------------- */
static const char *const CATEGORY[19] = {               /* SmokeableCategoryNameTable 0x7fc808 */
    "NONE", "GARBAGE_BAG", "SHOPPING_CART", "METAL_FENCE", "TRAFFIC_CONE", "WATER_DRUM",
    "OIL_DRUM", "PARKING_METER", "ROAD_SIGN_METAL", "ROAD_SIGN_WOOD", "MARKET_STALL",
    "SANDWICH_BOARD", "SAW_HORSE", "METAL_SMALL", "METAL_BIG", "WOODBOX", "TRASHCAN",
    "GLASS", "CHAIR" };

typedef struct {                   /* SmokeableInfo, 0x100 B (0x34026) */
    char     name[25];
    uint32_t key;                  /* +0x80 */
    float    mass, inertia[3];     /* +0x20, +0x30 / +0x44 / +0x58 */
    uint32_t anchor_fx, motion_fx; /* +0x84 / +0x88 emitter groups */
    float    cg[3];                /* +0xb0 */
    float    lifetime;             /* +0xc0 */
    float    anchor;               /* +0xc4 */
    float    bounce, friction;     /* +0xcc / +0xe0, Init's clamps applied */
    int      category;             /* +0xdc matched */
    int      terrain;              /* +0xd4 matched: MatchTerrainType (GC 8020e294), the contact surface */
    int      persistent;           /* +0xe4 */
    CarColl *hull;                 /* GJK polytope by name */
    AcidEmitter *t_motion, *t_anchor;   /* +0xf0 / +0xf8 the emitter templates (SpawnSmokeables) */
} SmkInfo;

typedef struct {                   /* SmokeableSpawner, 0x70 B (0x34027) */
    int      info;                 /* -1: no info (disabled at spawn) */
    uint16_t section, instance;    /* its scenery override (+4) */
    uint32_t model;                /* +8 */
    float    mn[3], mx[3];         /* +0x10 / +0x20 world box */
    float    M[16];                /* +0x30 world matrix, orthonormalised */
    /* runtime */
    unsigned char present, virgin, disabled;
    signed char live;              /* index into g_live, -1 none */
    int      mesh[SMK_MAX_MESH], nmesh;   /* in the active resident's scene */
    uint32_t sig;                  /* those meshes' signature */
} SmkSpawner;

typedef struct {
    int       n;
    GpuMesh   gm[SMK_MAX_MESH];
    GLuint    tex[SMK_MAX_MESH];
    unsigned char mode[SMK_MAX_MESH];
    uint32_t  sig;
} SmkGpu;

typedef struct {                   /* a live Smokeable */
    int       used, spawner, info;
    N2Prop   *b;
    CarColl  *cc;                  /* its hull with the step-start pose */
    CarCollBodyOps ops;
    WCollCtx  wc;
    int       awake, woken;        /* +0x506; ever woken (+0x15c set) */
    float     spawn_t, wake_t, lastcar_t;   /* +0x158 / +0x15c / +0x160 */
    float     zoff;                /* +0x168: the visual sits this much lower */
    float     anchor;              /* +0x150 */
    int       dstate;              /* +0x508 damping state */
    int       touches, touches_prev;  /* contacts this frame / the last */
    float     contact_since;       /* when the current contact run began */
    int       motion_fx, anchor_fx;   /* emitters started (+0x505 / +0x504) */
    int       serial;              /* this conversion's id (the sound's smokeable) */
    int       slosh;               /* +0x50c: its water-drum slosh has played */
#define SMK_EM 6
    AcidEmitter *em_motion[SMK_EM], *em_anchor[SMK_EM];   /* +0x4fc / +0x4f4 started copies */
    int       nem_motion, nem_anchor;
} SmkLive;

static SmkInfo    *g_info;   static int g_ninfo;
static int         g_fx_built;   /* the infos' emitter templates taken */
static SmkSpawner *g_sp;     static int g_nsp;
static uint32_t   *g_keys;   static int *g_keyidx;      /* sorted section<<16|instance */
static SmkGpu    **g_gpu;
static int        *g_pres; static int g_npres;   /* the present spawners (smk_touch: a World Expansion city has 12,692) */
static SmkLive     g_live[SMK_LIVE_MAX];
static WColl      *g_w;
static float       g_now;
static unsigned long g_gen = (unsigned long)-1;
static unsigned    g_frame;
static struct { int conv, revert, woke, del, uploads, drawn, gpu, fx; } g_st;
/* the active resident's meshes, for uploads (smk_resident each frame) */
static const N2Scene *g_cur_scene;
static const GLuint *g_cur_tex;
static const unsigned char *g_cur_mode;

static int info_by_key(uint32_t key)
{
    for (int i = 0; i < g_ninfo; i++) if (g_info[i].key == key) return i;
    return -1;
}
static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = g_keys[*(const int *)a], y = g_keys[*(const int *)b];
    return x < y ? -1 : x > y;
}
static int spawner_of(unsigned section, unsigned instance)
{
    if (!g_nsp) return -1;
    uint32_t k = (uint32_t)section << 16 | (instance & 0xffff);
    int lo = 0, hi = g_nsp - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t v = g_keys[g_keyidx[mid]];
        if (v == k) return g_keyidx[mid];
        if (v < k) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}
int smk_is_prop(unsigned section, unsigned instance) { return spawner_of(section, instance) >= 0; }

static void orthonormalise(float *m)                   /* the pack's first load (+0xe) */
{
    float *x = m, *y = m + 4, z[3], l = len3(x);
    if (l > 0) { x[0] /= l; x[1] /= l; x[2] /= l; }
    cross3(z, x, y); l = len3(z);
    if (l > 0) { z[0] /= l; z[1] /= l; z[2] /= l; }
    cross3(y, z, x);
    memcpy(m + 8, z, 12);
    m[3] = m[7] = m[11] = 0; m[15] = 1;
}

static unsigned char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = n > 0 ? (unsigned char *)malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    *len = d ? n : 0;
    return d;
}

static void gpu_free(int i)
{
    if (!g_gpu || !g_gpu[i]) return;
    SmkGpu *g = g_gpu[i];
    for (int k = 0; k < g->n; k++) {
        glDeleteBuffers(1, &g->gm[k].vbo); glDeleteBuffers(1, &g->gm[k].nbo);
        if (g->gm[k].cbo) glDeleteBuffers(1, &g->gm[k].cbo);
        glDeleteBuffers(1, &g->gm[k].ibo);
    }
    free(g); g_gpu[i] = NULL;
    g_st.gpu--;
}

static void live_delete(int li)
{
    SmkLive *L = &g_live[li];
    if (!L->used) return;
    if (L->spawner >= 0 && g_sp[L->spawner].live == li) g_sp[L->spawner].live = -1;
    for (int k = 0; k < L->nem_motion; k++) acid_auto_remove(L->em_motion[k]);   /* the dtor */
    for (int k = 0; k < L->nem_anchor; k++) acid_auto_remove(L->em_anchor[k]);
    n2prop_free(L->b); carcoll_free(L->cc);
    memset(L, 0, sizeof *L);
}

void smk_free(void)
{
    for (int i = 0; i < SMK_LIVE_MAX; i++) live_delete(i);
    for (int i = 0; i < g_nsp; i++) gpu_free(i);
    for (int i = 0; i < g_ninfo; i++) {
        carcoll_free(g_info[i].hull);
        acid_free_clones(g_info[i].t_motion); acid_free_clones(g_info[i].t_anchor);
    }
    g_fx_built = 0;
    free(g_info); free(g_sp); free(g_keys); free(g_keyidx); free(g_gpu); free(g_pres);
    g_info = NULL; g_sp = NULL; g_keys = NULL; g_keyidx = NULL; g_gpu = NULL; g_pres = NULL; g_npres = 0;
    g_ninfo = g_nsp = 0;
    g_gen = (unsigned long)-1;
    memset(&g_st, 0, sizeof g_st);
}

int smk_load(const char *root, const char *stem)
{
    smk_free();
    {   /* A/B: no props at all (the world draws their scenery as before) */
        FILE *off = fopen("ux0:data/NFSU2/nosmk.txt", "r");
        if (off) { fclose(off); printf("smokeables: off (nosmk.txt)\n"); return 0; }
    }
    char path[1100], spath[1100];
    long n = 0;
    /* a World Expansion city: its props were translated into one bundle (xpprops.inc,
       infos + hulls + placements + spawners) that stands in for both files */
    snprintf(path, sizeof path, "%s/XPSMK%s.BUN", root, stem);
    FILE *xp = fopen(path, "rb");
    if (xp) { fclose(xp); snprintf(spath, sizeof spath, "%s", path); }
    else {
        snprintf(path, sizeof path, "%s/%s.BUN", root, stem);
        snprintf(spath, sizeof spath, "%s/STREAM%s.BUN", root, stem);
    }
    unsigned char *d = read_file(path, &n);
    if (!d) return 0;
    const unsigned char *ov = NULL; long nov = 0;
    for (long o = 0; o + 8 <= n; ) {
        uint32_t id = rd32(d + o), sz = rd32(d + o + 4);
        if ((long)sz > n - o - 8) break;
        long p = (o + 8 + 15) & ~15L, end = o + 8 + (long)sz;
        if (id == 0x34026u) {                          /* SmokeableInfo[] */
            int cnt = (int)((end - p) >> 8);
            g_info = (SmkInfo *)calloc((size_t)(cnt > 0 ? cnt : 1), sizeof *g_info);
            for (int i = 0; i < cnt && g_info; i++) {
                const unsigned char *r = d + p + (long)i * 0x100;
                SmkInfo *I = &g_info[g_ninfo++];
                memcpy(I->name, r + 8, 24); I->name[24] = 0;
                I->key = rd32(r + 0x80);
                I->mass = rdf(r + 0x20);
                I->inertia[0] = rdf(r + 0x30); I->inertia[1] = rdf(r + 0x44); I->inertia[2] = rdf(r + 0x58);
                I->anchor_fx = rd32(r + 0x84); I->motion_fx = rd32(r + 0x88);
                for (int k = 0; k < 3; k++) I->cg[k] = rdf(r + 0xb0 + 4 * k);
                I->lifetime = rdf(r + 0xc0);
                I->anchor = rdf(r + 0xc4);
                I->bounce = rdf(r + 0xcc); if (I->bounce > 0.005f) I->bounce = 0.005f;   /* Init's clamps */
                I->friction = rdf(r + 0xe0); if (I->friction < 0.3f) I->friction = 0.3f;
                uint32_t ch = rd32(r + 0xdc);
                for (int c = 0; c < 19; c++) if (bhash(CATEGORY[c]) == ch) I->category = c;
                uint32_t th = rd32(r + 0xd4);           /* no match -> 0 (NONE) */
                for (int t = 0; t < 29; t++) if (bhash(acid_surfaces[t].name) == th) { I->terrain = t; break; }
                I->persistent = rd32(r + 0xe4) != 0;
            }
        } else if (id == 0x34107u) { ov = d + o + 8; nov = (long)sz / 8; }
        o = end;
    }
    int nhull = 0;
    for (int i = 0; i < g_ninfo; i++)
        if ((g_info[i].hull = carcoll_load_named(d, n, g_info[i].name)) != NULL) nhull++;
    /* the override table stays needed for the spawners: copy what we use */
    uint16_t (*ovr)[2] = NULL;
    if (ov && nov > 0) {
        ovr = (uint16_t (*)[2])malloc((size_t)nov * sizeof *ovr);
        for (long i = 0; ovr && i < nov; i++) { ovr[i][0] = rd16(ov + 8 * i); ovr[i][1] = rd16(ov + 8 * i + 2); }
    }
    free(d);

    snprintf(path, sizeof path, "%s", spath);
    FILE *f = fopen(path, "rb");
    int cap = 0, npack = 0, noinfo = 0;
    if (f) {
        long off = 0;
        unsigned char h[8];
        /* the bundle's chunk index (resource.c): only the spawner packs are read */
        int nix = 0;
        ResChunk *ix = res_chunk_index(path, &nix);
        for (int k = 0; ix ? k < nix : 1; k++) {
            uint32_t id, sz;
            if (ix) { id = ix[k].id; sz = ix[k].size; off = (long)ix[k].off; }
            else {
                if (fseek(f, off, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) break;
                id = rd32(h); sz = rd32(h + 4);
            }
            if (id == 0x34027u && sz < (1u << 20)) {   /* SmokeableSpawnerPack */
                long base = off + 8, pad = ((base + 15) & ~15L) - base;
                unsigned char *b = (unsigned char *)malloc(sz);
                if (b && (!ix || fseek(f, base, SEEK_SET) == 0) &&
                    fread(b, 1, sz, f) == sz && (long)sz > pad + 0x10) {
                    const unsigned char *pk = b + pad;
                    int cnt = (int16_t)rd16(pk + 0x0c);
                    /* UG2's 0x70-byte spawners only: a World Expansion city's own packs
                       (MW: 64-byte records, copied through by xpconv) would map garbage
                       overrides onto its scenery and hide it as "props" (11-18) */
                    if (cnt <= 0 || (long)sz - pad - 0x10 < (long)cnt * 0x70) { free(b); if (!ix) off += 8 + (long)sz; continue; }
                    npack++;
                    LOAD_PUMP();   /* the loading screen keeps moving */
                    for (int i = 0; i < cnt && pad + 0x10 + (long)(i + 1) * 0x70 <= (long)sz; i++) {
                        const unsigned char *r = pk + 0x10 + i * 0x70;
                        if (g_nsp == cap) {   /* out of memory: the props found so far stay (audit 11-17) */
                            int nc = cap ? cap * 2 : 256;
                            SmkSpawner *ns = (SmkSpawner *)realloc(g_sp, (size_t)nc * sizeof *g_sp);
                            if (!ns) break;
                            g_sp = ns; cap = nc;
                        }
                        SmkSpawner *S = &g_sp[g_nsp];
                        memset(S, 0, sizeof *S);
                        S->info = info_by_key(rd32(r));
                        if (S->info < 0) noinfo++;
                        uint16_t oi = rd16(r + 4);
                        if (!ovr || oi >= nov) continue;
                        S->section = ovr[oi][0]; S->instance = ovr[oi][1];
                        S->model = rd32(r + 8);
                        for (int k = 0; k < 3; k++) { S->mn[k] = rdf(r + 0x10 + 4 * k); S->mx[k] = rdf(r + 0x20 + 4 * k); }
                        for (int k = 0; k < 16; k++) S->M[k] = rdf(r + 0x30 + 4 * k);
                        orthonormalise(S->M);
                        S->live = -1;
                        g_nsp++;
                    }
                }
                free(b);
            }
            if (!ix) off += 8 + (long)sz;
        }
        free(ix);
        fclose(f);
    }
    free(ovr);
    if (!g_nsp) {
        printf("smokeables: %s: %d infos, no spawners -- none\n", stem, g_ninfo);
        smk_free();
        return 0;
    }
    g_keys = (uint32_t *)malloc((size_t)g_nsp * sizeof *g_keys);
    g_keyidx = (int *)malloc((size_t)g_nsp * sizeof *g_keyidx);
    g_gpu = (SmkGpu **)calloc((size_t)g_nsp, sizeof *g_gpu);
    g_pres = (int *)malloc((size_t)g_nsp * sizeof *g_pres);
    if (!g_keys || !g_keyidx || !g_gpu || !g_pres) {
        printf("smokeables: %s: out of memory, none\n", stem);
        smk_free();
        return 0;
    }
    for (int i = 0; i < g_nsp; i++) { g_keys[i] = (uint32_t)g_sp[i].section << 16 | g_sp[i].instance; g_keyidx[i] = i; }
    qsort(g_keyidx, (size_t)g_nsp, sizeof *g_keyidx, cmp_u32);
    smk_world_reset();
    printf("smokeables: %s: %d infos (%d hulls), %d spawners in %d packs (%d without an info)\n",
           stem, g_ninfo, nhull, g_nsp, npack, noinfo);
    return g_nsp;
}

void smk_world_reset(void)
{
    for (int i = 0; i < SMK_LIVE_MAX; i++) live_delete(i);
    for (int i = 0; i < g_nsp; i++) {
        SmkSpawner *S = &g_sp[i];
        S->disabled = S->info < 0 || !g_info[S->info].hull;    /* missing info: never spawns */
        S->virgin = S->present && !S->disabled;
        S->live = -1;
    }
}

/* ---- the active resident ------------------------------------------------- */
void smk_resident(const N2Scene *scene, const GLuint *mtex, const unsigned char *mmode,
                  unsigned long gen)
{
    if (!g_nsp || !scene) return;
    g_cur_scene = scene; g_cur_tex = mtex; g_cur_mode = mmode;
    if (gen == g_gen) return;
    g_gen = gen;
    for (int i = 0; i < g_nsp; i++) { g_sp[i].nmesh = 0; g_sp[i].sig = 0; }
    g_npres = 0;
    for (int m = 0; m < scene->count; m++) {
        const N2Mesh *x = &scene->meshes[m];
        if (x->world_role != 3 || !x->scen_has) continue;
        int s = spawner_of(x->scen_region, x->scen_index);
        if (s < 0 || g_sp[s].nmesh >= SMK_MAX_MESH) continue;
        g_sp[s].mesh[g_sp[s].nmesh++] = m;
        g_sp[s].sig = g_sp[s].sig * 31u + (uint32_t)x->nverts * 7u + x->texkey;
    }
    int loaded = 0;
    for (int i = 0; i < g_nsp; i++) {
        SmkSpawner *S = &g_sp[i];
        S->present = S->nmesh > 0;
        loaded += S->present;
        if (S->present) g_pres[g_npres++] = i;
        /* section load -> Spawn; unload -> Unspawn (the virgin goes, a live
           prop stays and sleeps) */
        if (S->present && !S->disabled && S->live < 0) S->virgin = 1;
        if (!S->present) S->virgin = 0;
        SmkGpu *g = g_gpu[i];
        if (!g) continue;
        if (S->present && g->sig != S->sig) { gpu_free(i); continue; }   /* other LOD meshes now */
        if (!S->present && S->live < 0) { gpu_free(i); continue; }
        if (S->present)
            for (int k = 0; k < g->n && k < S->nmesh; k++) {
                g->tex[k] = mtex ? mtex[S->mesh[k]] : 0;
                g->mode[k] = mmode ? mmode[S->mesh[k]] : N2_DRAW_OPAQUE;
            }
    }
    static int logged;
    if (logged++ < 3) printf("smokeables: resident %lu holds %d of %d props\n", gen, loaded, g_nsp);
}

/* ---- live props ---------------------------------------------------------- */
static const float ZERO16[16];

static int contacts(const SmkLive *L) { return L->touches + L->touches_prev; }   /* +0x18 */

/* CheckDampingSuitability 0x59e430 */
static int damping_state(const SmkLive *L)
{
    N2Body b; n2prop_body(L->b, &b);
    float v = len3(b.v), w = len3(b.w);
    if (v <= 2.0f && w <= 2.0f && (contacts(L) > 0 || L->dstate == 2))
        return v <= 0.8f && w <= 0.8f && g_now - L->lastcar_t >= 0.1f ? 2 : 1;
    return 0;
}
/* the integration part of Smokeable::Move 0x5b1dd0: n = trunc(dt / step) + 1
   Euler substeps, each followed by ComputeAuxiliaries 0x5a8e70 */
static void integrate(SmkLive *L, float dt)
{
    if (!L->awake || dt <= 0) return;
    int n = (int)(dt / SMK_STEP) + 1;
    float h = dt / (float)n;
    for (int i = 0; i < n; i++) {
        n2prop_substep(L->b, h);
        L->dstate = damping_state(L);
        n2prop_damp(L->b, L->dstate);
    }
}
/* the prop's model matrix (Smokeable::Move): [R | pos - cg R], z lowered by zoff */
static void model_of(const SmkLive *L, float Mn[16], float v[3])
{
    N2Body b; n2prop_body(L->b, &b);
    float cgw[3];
    memcpy(Mn, b.R, 16 * sizeof(float));
    rowmul(cgw, g_info[L->info].cg, b.R);
    Mn[12] = b.pos[0] - cgw[0]; Mn[13] = b.pos[1] - cgw[1]; Mn[14] = b.pos[2] - cgw[2] - L->zoff;
    Mn[3] = Mn[7] = Mn[11] = 0; Mn[15] = 1;
    if (v) memcpy(v, b.v, 3 * sizeof(float));
}
/* copies of an info's template list (Init: afxCopyEmitter, state 1), at the
   prop's matrix and velocity, run as auto emitters (AddAutoEmitter) */
static int start_emitters(const SmkLive *L, const AcidEmitter *t, AcidEmitter **out)
{
    int n = 0;
    float M[16], v[3];
    model_of(L, M, v);
    for (; t && n < SMK_EM; t = t->next) {
        AcidEmitter *e = acid_copy(t);
        if (!e) break;
        memcpy(e->m, M, sizeof e->m);
        memcpy(e->vel, v, sizeof e->vel);
        acid_auto_add(e);
        out[n++] = e;
        g_st.fx++;
    }
    return n;
}
static void awaken(SmkLive *L)                          /* 0x5adf90 */
{
    if (L->awake) return;
    L->awake = 1;
    g_st.woke++;
    if (!L->woken) {
        L->woken = 1; L->wake_t = g_now;
        /* AddAutoEmitter for the MOTION group (info +0x88) */
        L->motion_fx = g_info[L->info].motion_fx != 0;
        if (L->motion_fx) L->nem_motion = start_emitters(L, g_info[L->info].t_motion, L->em_motion);
    }
}
/* ConsiderSleeping 0x5a8d90 */
static void consider_sleeping(SmkLive *L)
{
    if (L->spawner >= 0 && !g_sp[L->spawner].present) { L->awake = 0; return; }   /* its section is gone */
    N2Body b; n2prop_body(L->b, &b);
    if (g_now - L->lastcar_t > 0.1f && contacts(L) > 0 && g_now - L->contact_since > 0.025f &&
        len3(b.v) < 0.1f && len3(b.w) < 0.1f)
        L->awake = 0;                                   /* PutToSleep 0x59e400 */
}

static void op_body(void *o, N2Body *b)
{
    SmkLive *L = (SmkLive *)o;
    n2prop_body(L->b, b);
    if (L->anchor != 0) { b->invmass = 0; b->Iinv = ZERO16; }   /* GetMassInverse / vt+0x60: a wall */
}
static void op_pose(void *o, const float pos[3], const float R[16], const float P[3])
{
    n2prop_body_pose(((SmkLive *)o)->b, pos, R, P);
}
static void op_advance(void *o, float t) { integrate((SmkLive *)o, t); }
/* Smokeable::ConsiderImpulse 0x5b2070 */
static float op_consider(void *o, const float J[3], const float r[3])
{
    SmkLive *L = (SmkLive *)o;
    float s = 1.0f;
    if (!L->awake) {
        if (L->anchor != 0) {
            N2Body b; n2prop_body(L->b, &b);
            float cgw[3], arm[3], t[3];
            rowmul(cgw, g_info[L->info].cg, b.R);
            for (int k = 0; k < 3; k++) arm[k] = cgw[k] + r[k];
            cross3(t, arm, J);
            float a = len3(t), jl = len3(J);
            if (jl > a) a = jl;
            if (a < L->anchor) return 1.0f;             /* still a wall */
            s = L->anchor / a;
            awaken(L);
            L->anchor = 0;
            L->anchor_fx = g_info[L->info].anchor_fx != 0;   /* ActivateAnchorEmitters */
            if (L->anchor_fx) L->nem_anchor = start_emitters(L, g_info[L->info].t_anchor, L->em_anchor);
        } else awaken(L);
    }
    return s;                                           /* breakAccum is always 0 in UG2 */
}
static void op_impulse(void *o, const float J[3], const float r[3])
{
    SmkLive *L = (SmkLive *)o;
    if (L->awake) n2prop_impulse(L->b, J, r);           /* asleep: ignored */
}
static void op_touch(void *o, int car)
{
    SmkLive *L = (SmkLive *)o;
    L->touches++;
    if (car) L->lastcar_t = g_now;                      /* EndCollision 0x59e530 */
}

static float spawner_radius(const SmkSpawner *S)
{
    float d[3] = { S->mx[0] - S->mn[0], S->mx[1] - S->mn[1], S->mx[2] - S->mn[2] };
    return 0.5f * len3(d);
}

/* VirginSmokeable::ConvertToSmokeable 0x5b1390 */
static void convert(int si)
{
    SmkSpawner *S = &g_sp[si];
    int li = -1;
    for (int i = 0; i < SMK_LIVE_MAX && li < 0; i++) if (!g_live[i].used) li = i;
    if (li < 0) return;                                 /* pool full: the virgin stays */
    const SmkInfo *I = &g_info[S->info];
    SmkLive *L = &g_live[li];
    memset(L, 0, sizeof *L);
    float M[16];
    memcpy(M, S->M, sizeof M);
    if (g_w) {                                          /* onto the ground, the lift kept as zoff */
        WCollCtx c; wcoll_ctx_init(&c);
        float p[3] = { M[12], M[13], M[14] + 1.0f }, nrm[3];
        int surf, found = 0;
        float elev = wcoll_ground(g_w, &c, p, &surf, nrm, &found);
        if (found) {
            float minz = S->mn[2] - M[14];              /* the model's lowest point */
            float rise = elev - ((minz + I->cg[2]) * fabsf(nrm[2]) + M[14]) + 0.05f;
            if (rise > 0) { L->zoff = rise; M[14] += rise; }
        }
    }
    L->b = n2prop_new(I->mass, I->inertia, I->cg);
    L->cc = carcoll_clone(I->hull);
    if (!L->b || !L->cc) { n2prop_free(L->b); carcoll_free(L->cc); memset(L, 0, sizeof *L); return; }
    float com[3], R[16];
    rowmul(com, I->cg, M);
    for (int k = 0; k < 3; k++) com[k] += M[12 + k];
    memcpy(R, M, sizeof R);
    n2prop_place(L->b, com, R);                         /* SetInitialPosition, v = w = 0 */
    static int serial;
    L->used = 1; L->spawner = si; L->info = S->info;
    L->serial = ++serial & 0xffffff;
    L->anchor = I->anchor;
    L->spawn_t = g_now; L->lastcar_t = -1e9f; L->contact_since = g_now;
    wcoll_ctx_init(&L->wc);
    L->ops.body = op_body; L->ops.pose = op_pose; L->ops.advance = op_advance;
    L->ops.consider = op_consider; L->ops.impulse = op_impulse; L->ops.touch = op_touch;
    L->ops.bounce = I->bounce; L->ops.friction = I->friction; L->ops.asleep = 1;
    N2Body b; n2prop_body(L->b, &b);
    carcoll_begin_body(L->cc, &b, SMK_STEP);
    S->live = (signed char)li; S->virgin = 0;
    S->disabled = 1;                                    /* no respawn while this world lives */
    g_st.conv++;
}

void smk_touch(const float pos[3], float rad)
{
    for (int j = 0; j < g_npres; j++) {                /* only a present spawner is drawn and can be met */
        int i = g_pres[j];
        SmkSpawner *S = &g_sp[i];
        if (!S->virgin) continue;
        if (pos[0] + rad < S->mn[0] || pos[0] - rad > S->mx[0] ||
            pos[1] + rad < S->mn[1] || pos[1] - rad > S->mx[1]) continue;
        if (pos[2] + rad < S->mn[2] - 2.0f || pos[2] - rad > S->mx[2] + 2.0f) continue;   /* another level */
        convert(i);
    }
}

void smk_begin(float dt, WColl *w)
{
    g_w = w;
    g_now += dt;
    g_frame++;
    if (!g_fx_built && acid_ndefs() > 0) {              /* SpawnSmokeables: afxGetEmitters */
        g_fx_built = 1;
        for (int i = 0; i < g_ninfo; i++) {
            g_info[i].t_motion = acid_clones(g_info[i].motion_fx);
            g_info[i].t_anchor = acid_clones(g_info[i].anchor_fx);
        }
        int nm = 0, na = 0, ni = 0;
        for (int i = 0; i < g_ninfo; i++) {
            int m = 0, a = 0;
            for (const AcidEmitter *e = g_info[i].t_motion; e; e = e->next) m++;
            for (const AcidEmitter *e = g_info[i].t_anchor; e; e = e->next) a++;
            nm += m; na += a; ni += (m + a) > 0;
        }
        printf("smokeables: emitter templates %d motion + %d anchor for %d infos\n", nm, na, ni);
    }
    for (int i = 0; i < SMK_LIVE_MAX; i++) {
        SmkLive *L = &g_live[i];
        if (!L->used) continue;
        if (L->touches && !L->touches_prev) L->contact_since = g_now - dt;
        L->touches_prev = L->touches; L->touches = 0;
        N2Body b; n2prop_body(L->b, &b);
        carcoll_begin_body(L->cc, &b, dt);              /* BeginMove 0x593210 */
        if (!L->awake) continue;
        integrate(L, dt);
        consider_sleeping(L);
        if (L->awake && L->zoff != 0) {                 /* the visual sinks back onto the body */
            L->zoff -= 0.2f * dt;
            if (L->zoff < 0) L->zoff = 0;
        }
        if (L->awake && L->nem_motion) {                /* ServiceEmitters 0x5a8c80 */
            float M[16], v[3];
            model_of(L, M, v);
            for (int k = 0; k < L->nem_motion; k++) {
                memcpy(L->em_motion[k]->m, M, sizeof M);
                memcpy(L->em_motion[k]->vel, v, sizeof v);
            }
        }
    }
    for (int i = 0; i < SMK_LIVE_MAX; i++) {            /* a moving prop converts what it meets */
        SmkLive *L = &g_live[i];
        if (!L->used || !L->awake) continue;
        N2Body b; n2prop_body(L->b, &b);
        smk_touch(b.pos, spawner_radius(&g_sp[L->spawner]));
    }
}

int smk_bodies(CarCollCar *out, int max, int with_polygons)
{
    int n = 0;
    for (int i = 0; i < SMK_LIVE_MAX && n < max; i++) {
        SmkLive *L = &g_live[i];
        if (!L->used) continue;
        CarCollCar *k = &out[n++];
        memset(k, 0, sizeof *k);
        k->c = L->cc;
        k->wc = with_polygons ? &L->wc : NULL;
        L->ops.asleep = !L->awake;
        k->ops = &L->ops; k->obj = L;
    }
    return n;
}
int smk_is_body(const CarCollCar *k)
{
    return k && k->ops && (const SmkLive *)k->obj >= g_live && (const SmkLive *)k->obj < g_live + SMK_LIVE_MAX;
}
int smk_body_sound(const CarCollCar *k, int *cat)
{
    if (!smk_is_body(k)) return 0;
    const SmkLive *L = (const SmkLive *)k->obj;
    if (cat) *cat = g_info[L->info].category;
    return L->serial;
}

int smk_body_contact(const CarCollCar *k, int *surface, float v[3])
{
    if (!smk_is_body(k)) return 0;
    const SmkLive *L = (const SmkLive *)k->obj;
    N2Body b; n2prop_body(L->b, &b);
    *surface = g_info[L->info].terrain;
    memcpy(v, b.v, sizeof b.v);
    return 1;
}

int smk_slosh_claim(int serial)
{
    for (int i = 0; i < SMK_LIVE_MAX; i++) {
        SmkLive *L = &g_live[i];
        if (!L->used || L->serial != serial) continue;
        if (g_info[L->info].category != 5 || !L->motion_fx || L->slosh) return 0;
        L->slosh = 1;
        return 1;
    }
    return 0;
}

static void live_kill(int i) { live_delete(i); g_st.del++; }

/* DeleteFarAwaySmokeableCollisionBodies 0x5ad420 */
static void delete_far(const float eye[3])
{
    int nfree = 0;
    for (int i = 0; i < SMK_LIVE_MAX; i++) nfree += !g_live[i].used;
    for (int i = 0; i < SMK_LIVE_MAX; i++) {
        SmkLive *L = &g_live[i];
        if (!L->used || g_info[L->info].persistent) continue;
        SmkSpawner *S = &g_sp[L->spawner];
        N2Body b; n2prop_body(L->b, &b);
        float rad = spawner_radius(S);
        if (!S->present) { live_kill(i); nfree++; continue; }     /* its section unloaded */
        if (g_w) {                                      /* fell through the world */
            float p[3] = { b.pos[0], b.pos[1], b.pos[2] + 1.0f }, nrm[3];
            int surf, found = 0;
            float elev = wcoll_ground(g_w, &L->wc, p, &surf, nrm, &found);
            if (found && b.pos[2] + rad < elev - 1.0f) { live_kill(i); nfree++; continue; }
        }
        if (nfree < 10 && !L->awake) {
            float d[3] = { b.pos[0] - eye[0], b.pos[1] - eye[1], b.pos[2] - eye[2] }, dl = len3(d);
            float limit = nfree < 4 ? 20.0f : 300.0f;
            if (dl > limit || (L->woken && g_now - L->wake_t > 15.0f && dl > 120.0f)) { live_kill(i); nfree++; }
        }
    }
}

void smk_end(const float eye[3])
{
    if (!g_nsp) return;
    int nfree = 0;
    for (int i = 0; i < SMK_LIVE_MAX; i++) {
        SmkLive *L = &g_live[i];
        if (!L->used) { nfree++; continue; }
        const SmkInfo *I = &g_info[L->info];
        /* CheckTimers 0x5ade10: a prop that was converted but never hit goes
           back to being a virgin after 2 s; a woken one with a lifetime dies */
        if (!L->woken && contacts(L) == 0 && g_now - L->spawn_t > 2.0f) {
            SmkSpawner *S = &g_sp[L->spawner];
            live_delete(i);
            S->disabled = 0;
            if (S->present) S->virgin = 1;
            g_st.revert++; nfree++;
            continue;
        }
        if (L->woken && I->lifetime != 0 && g_now - L->wake_t >= I->lifetime) { live_kill(i); nfree++; }
    }
    /* SmokeableThink 0x59e350 */
    if (nfree < 4 || ((g_frame + 0x4d) & 0x7f) == 0) delete_far(eye);
    if (g_frame % 600 == 0) smk_report();
}

/* ---- replay (step 8b): the props' recorded state ------------------------ */
static int g_rp_on;                         /* playback override active */
static unsigned char *g_rp_bits;            /* virgin bit per spawner while on */
static int g_rp_nbits;
static int g_rp_n;
static uint16_t g_rp_sp[SMK_LIVE_MAX];
static float g_rp_M[SMK_LIVE_MAX][16];

int smk_rp_nsp(void) { return g_nsp; }
void smk_rp_virgins(unsigned char *bits)
{
    memset(bits, 0, (size_t)(g_nsp + 7) / 8);
    for (int i = 0; i < g_nsp; i++) if (g_sp[i].virgin) bits[i >> 3] |= (unsigned char)(1u << (i & 7));
}
int smk_rp_live(int max, uint16_t *sp, float (*Mn)[16])
{
    int n = 0;
    for (int i = 0; i < SMK_LIVE_MAX && n < max; i++) {
        const SmkLive *L = &g_live[i];
        if (!L->used || L->spawner < 0 || L->spawner >= g_nsp) continue;
        sp[n] = (uint16_t)L->spawner;
        model_of(L, Mn[n], NULL);
        n++;
    }
    return n;
}
void smk_rp_set(const unsigned char *bits, int n, const uint16_t *sp, const float (*Mn)[16])
{
    int nb = (g_nsp + 7) / 8;
    if (nb > g_rp_nbits) {
        unsigned char *p = (unsigned char *)realloc(g_rp_bits, (size_t)nb);
        if (!p) return;
        g_rp_bits = p; g_rp_nbits = nb;
    }
    if (bits) memcpy(g_rp_bits, bits, (size_t)nb); else memset(g_rp_bits, 0, (size_t)nb);
    if (n > SMK_LIVE_MAX) n = SMK_LIVE_MAX;
    g_rp_n = n;
    for (int i = 0; i < n; i++) { g_rp_sp[i] = sp[i]; memcpy(g_rp_M[i], Mn[i], sizeof g_rp_M[i]); }
    g_rp_on = 1;
}
void smk_rp_off(void) { g_rp_on = 0; }

#ifdef OPENUG2_SIM_PRE_H
/* tests (sim/test): every spawner loaded, as if each section were resident */
void smk_sim_present_all(void)
{
    for (int i = 0; i < g_nsp; i++) {
        g_sp[i].present = 1;
        if (!g_sp[i].disabled && g_sp[i].live < 0) g_sp[i].virgin = 1;
    }
}
#endif
void smk_report(void)
{
    if (!g_nsp) return;
    int live = 0, awake = 0, virgins = 0, present = 0;
    for (int i = 0; i < SMK_LIVE_MAX; i++) if (g_live[i].used) { live++; awake += g_live[i].awake; }
    for (int i = 0; i < g_nsp; i++) { virgins += g_sp[i].virgin; present += g_sp[i].present; }
    printf("smokeables: %d loaded, %d virgins, %d live (%d awake); converted %d, reverted %d, woken %d, "
           "deleted %d; %d drawn/frame, %d with GPU meshes; %d emitters started\n",
           present, virgins, live, awake, g_st.conv, g_st.revert, g_st.woke, g_st.del,
           g_st.drawn, g_st.gpu, g_st.fx);
}

/* ---- drawing ------------------------------------------------------------- */
static SmkGpu *gpu_make(int si)
{
    SmkSpawner *S = &g_sp[si];
    if (!g_cur_scene || !S->present) return NULL;
    SmkGpu *g = (SmkGpu *)calloc(1, sizeof *g);
    if (!g) return NULL;
    for (int k = 0; k < S->nmesh; k++) {
        if (S->mesh[k] >= g_cur_scene->count) continue;
        N2Mesh m = g_cur_scene->meshes[S->mesh[k]];
        m.world_role = 0;
        N2Scene one = { &m, 1, 1 };
        GpuMesh *gm = upload_scene(&one);
        if (!gm) continue;
        g->gm[g->n] = gm[0];
        g->tex[g->n] = g_cur_tex ? g_cur_tex[S->mesh[k]] : 0;
        g->mode[g->n] = g_cur_mode ? g_cur_mode[S->mesh[k]] : N2_DRAW_OPAQUE;
        g->n++;
        free(gm);
        g_st.uploads++;
    }
    g->sig = S->sig;
    g_gpu[si] = g;
    g_st.gpu++;
    return g;
}

static int box_in_view(const float (*planes)[4], const float lo[3], const float hi[3])
{
    if (!planes) return 1;
    for (int p = 0; p < 6; p++) {
        const float *v = planes[p];
        float d = v[3];
        for (int c = 0; c < 3; c++) d += v[c] * (v[c] >= 0 ? hi[c] : lo[c]);
        if (d < -0.01f * (fabsf(v[0]) + fabsf(v[1]) + fabsf(v[2]))) return 0;
    }
    return 1;
}

/* the live prop's model matrix: [R | pos - cg R] with z lowered by zoff
   (Smokeable::Move), applied to the scenery mesh baked at the spawner's
   matrix: G = Mnow * inverse(Mspawn) */
static void live_matrix_of(const float Mn[16], const SmkSpawner *S, float G[16])
{
    float Mi[16];
    memset(Mi, 0, sizeof Mi);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Mi[i * 4 + j] = S->M[j * 4 + i];
    for (int j = 0; j < 3; j++)
        Mi[12 + j] = -(S->M[12] * S->M[j * 4] + S->M[13] * S->M[j * 4 + 1] + S->M[14] * S->M[j * 4 + 2]);
    Mi[15] = 1;
    mat_mul(Mn, Mi, G);
}
static void live_matrix(const SmkLive *L, const SmkSpawner *S, float G[16])
{
    float Mn[16];
    model_of(L, Mn, NULL);
    live_matrix_of(Mn, S, G);
}

void smk_draw(const RProg *rp, const float MVP[16], const float cam[3], float view_dist,
              const float (*planes)[4])
{
    if (!g_nsp) return;
    int budget = 16, drawn = 0, cur_blend = 0, cur_cut = 0;
    GLuint lasttex = (GLuint)-1;
    for (int i = 0; i < g_nsp; i++) {
        SmkSpawner *S = &g_sp[i];
        const SmkLive *L = S->live >= 0 ? &g_live[(int)S->live] : NULL;
        const float *RM = NULL;              /* replay: the recorded model matrix */
        int virgin = S->virgin;
        if (g_rp_on) {
            L = NULL;
            if ((i >> 3) < g_rp_nbits) virgin = (g_rp_bits[i >> 3] >> (i & 7)) & 1;
            for (int k = 0; k < g_rp_n; k++) if (g_rp_sp[k] == i) { RM = g_rp_M[k]; break; }
            if (!S->present || S->nmesh <= 0) continue;
        }
        if (!virgin && !L && !RM) continue;
        float lo[3], hi[3];
        if (L || RM) {
            float pc[3];
            if (L) { N2Body b; n2prop_body(L->b, &b); memcpy(pc, b.pos, sizeof pc); }
            else { pc[0] = RM[12]; pc[1] = RM[13]; pc[2] = RM[14]; }
            float r = spawner_radius(S) + 0.5f;
            for (int k = 0; k < 3; k++) { lo[k] = pc[k] - r; hi[k] = pc[k] + r; }
        } else { memcpy(lo, S->mn, sizeof lo); memcpy(hi, S->mx, sizeof hi); }
        float dx = cam[0] < lo[0] ? lo[0] - cam[0] : cam[0] > hi[0] ? cam[0] - hi[0] : 0;
        float dy = cam[1] < lo[1] ? lo[1] - cam[1] : cam[1] > hi[1] ? cam[1] - hi[1] : 0;
        if (dx * dx + dy * dy > view_dist * view_dist) continue;
        if (!box_in_view(planes, lo, hi)) continue;
        SmkGpu *g = g_gpu[i];
        if (!g) {
            if (budget <= 0) continue;
            budget -= S->nmesh;
            if (!(g = gpu_make(i))) continue;
        }
        if (L || RM) {
            float G[16], M2[16];
            if (RM) live_matrix_of(RM, S, G); else live_matrix(L, S, G);
            mat_mul(MVP, G, M2);
            glUniformMatrix4fv(rp->uMVP, 1, GL_FALSE, M2);
            render_model(rp, G);
        }
        for (int k = 0; k < g->n; k++) {
            int md = g->mode[k], blend = md == N2_DRAW_BLEND || md == N2_DRAW_ADD;
            if (g->tex[k] != lasttex) {
                if (g->tex[k]) { glUniform1f(rp->uUseTex, 1.0f); glBindTexture(GL_TEXTURE_2D, g->tex[k]); }
                else { glUniform1f(rp->uUseTex, 0.0f); glUniform3f(rp->uColor, 0.28f, 0.29f, 0.31f); }
                lasttex = g->tex[k];
            }
            int cut = md == N2_DRAW_CUTOUT;
            if (cut != cur_cut) { glUniform1f(rp->uAlphaTest, cut ? 1.0f : 0.0f); cur_cut = cut; }
            if (blend != cur_blend) {
                if (blend) { glEnable(GL_BLEND); glDepthMask(GL_FALSE); glUniform1f(rp->uTextureAlpha, 1.0f); }
                else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); glUniform1f(rp->uTextureAlpha, 0.0f); }
                cur_blend = blend;
            }
            if (blend) glBlendFunc(GL_SRC_ALPHA, md == N2_DRAW_ADD ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            draw_gpumesh(&g->gm[k]);
        }
        if (L || RM) {
            glUniformMatrix4fv(rp->uMVP, 1, GL_FALSE, MVP);
            render_model(rp, NULL);
        }
        drawn++;
    }
    if (cur_cut) glUniform1f(rp->uAlphaTest, 0.0f);
    if (cur_blend) {
        glDisable(GL_BLEND); glDepthMask(GL_TRUE); glUniform1f(rp->uTextureAlpha, 0.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    g_st.drawn = drawn;
}
