/* xproads.c -- an expansion city's road network -> the game's free-roam road net
   (TRACKS/ROUTES<region>/RoutesFreeRoam.bin, read by airacer.c ainet_load), so the
   retail traffic and the encounter racers drive it. Shared by the Expansion Prep Tool
   and the on-device setup (xpcity.c). Port of ExpansionDev/roads (mwrn.py + mkroutes.py)
   and BeyondDev/tools/events/freeroam.py.

   MW (L2RA.BUN chunk 0x3b800, a tagged "CARP" container): RNhd counts (u16 profiles,
   nodes, segments, -, roads) followed by the arrays back to back:
     node 32 B: position f32 x3 (Y-up: world x = +8, y = -(+0), z = +4), +12 u16 index,
                +14 u16 lane profile, +16 u8 segment count, +18 u16 segments[4]
     profile 64 B: u8 lanes, u8 lanes on the left; +4 per lane u16 (width/64 << 4 | kind),
                s16 centre offset /256 m (kind 1 / 10: traffic lanes; left side: no lanes = one way)
     road 8 B (unused here)
     segment 22 B: u16 node 0, node 1, length*64, -, index, flags (0x400: a one-way segment
                runs node 1 -> node 0), +16 s8 x3 end control (-tangent), +19 s8 x3 start tangent
   Every segment gives one lane per way it carries (the innermost traffic lane); lanes are
   joined into sections by a path cover (greedy straightest, then augmenting paths: the route
   format holds 2,047 sections, MW's greedy cover alone makes 1,987), every other successor
   is a link. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xpconv.h"

#define SEC_FLAGS 0x7FE141u
#define PT_FLAGS (0x800u | 0x1000u | (0x3Fu << 19) | (1u << 25) | (1u << 26) | (1u << 27))
#define HALF_W 3.0f
#define LANE_SPEED 20
#define LK_ALWAYS 0x2222u
#define LK_COIN 0x4444u
#define LK_BACK (1u << 18)
#define MAX_SEC 2047
#define STEP 8.0f
static const float UTURN = 2.35619449f;   /* 135 degrees */

typedef struct { float x, y, z; } V3;
typedef struct { V3 *p; int n, cap; } Poly;
typedef struct { Poly pts, simp; int seg, n0, n1; } Lane;
typedef struct { int si, j; int tgt, tsi, tj; uint32_t fl; } Link;   /* tgt piece, or -1 and (tsi, tj) */

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float f32le(const uint8_t *p) { uint32_t u = u32le(p); float f; memcpy(&f, &u, 4); return f; }

static int poly_add(Poly *q, V3 v)
{
    if (q->n == q->cap) {
        int c = q->cap ? q->cap * 2 : 8;
        V3 *n = realloc(q->p, (size_t)c * sizeof *n); if (!n) return 0;
        q->p = n; q->cap = c;
    }
    q->p[q->n++] = v; return 1;
}
static float dist2(V3 a, V3 b) { return hypotf(b.x - a.x, b.y - a.y); }
static void unit2(V3 a, V3 b, float u[2]) { float l = dist2(a, b); if (l <= 0) l = 1; u[0] = (b.x - a.x) / l; u[1] = (b.y - a.y) / l; }

/* ---- the MW tables ---- */
typedef struct {
    const uint8_t *nd, *pf, *sg; int nn, np, ns;
} Rn;
static int rn_find(const uint8_t *b, size_t n, Rn *r)
{
    /* the tag table (16 B entries after the 8 B fill): counts of RNpf / RNnd / RNsg / RNrd */
    int cp = -1, cn = -1, cs = -1, cr = -1;
    for (size_t o = 8; o + 16 <= n && o < 0x400; o += 4) {
        uint32_t t = u32le(b + o);
        int c = (int)u32le(b + o + 8);
        if (t == 0x524e7066u) cp = c;        /* 'RNpf' */
        else if (t == 0x524e6e64u) cn = c;   /* 'RNnd' */
        else if (t == 0x524e7367u) cs = c;   /* 'RNsg' */
        else if (t == 0x524e7264u) cr = c;   /* 'RNrd' */
    }
    if (cp <= 0 || cn <= 0 || cs <= 0 || cr < 0 || cp > 65535 || cn > 65535 || cs > 65535 || cr > 65535) return 0;
    /* the RNhd record: the same counts, the arrays right after it */
    for (size_t h = 0x100; h + 16 <= n; h += 2) {
        if (u16le(b + h) != cp || u16le(b + h + 2) != cn || u16le(b + h + 4) != cs || u16le(b + h + 8) != cr) continue;
        size_t nd = h + 16, pf = nd + (size_t)cn * 32, rd = pf + (size_t)cp * 64, sg = rd + (size_t)cr * 8;
        if (sg + (size_t)cs * 22 > n) return 0;
        if (u16le(b + nd + 12) != 0 || u16le(b + nd + 32 + 12) != 1) continue;   /* node indices 0, 1 */
        r->nd = b + nd; r->pf = b + pf; r->sg = b + sg; r->nn = cn; r->np = cp; r->ns = cs;
        return 1;
    }
    return 0;
}
static V3 node_pos(const Rn *r, int i)
{
    const uint8_t *q = r->nd + (size_t)i * 32;
    V3 v = { f32le(q + 8), -f32le(q), f32le(q + 4) }; return v;
}
static int node_prof(const Rn *r, int i) { int p = u16le(r->nd + (size_t)i * 32 + 14); return p < r->np ? p : 0; }
static int prof_oneway(const Rn *r, int p) { return r->pf[(size_t)p * 64 + 1] == 0; }
/* the innermost traffic lane carrying that way: its offset (+ = right of node 0 -> node 1) */
static int lane_pos(const Rn *r, int p, int fwd, float *out)
{
    const uint8_t *q = r->pf + (size_t)p * 64;
    int n = q[0] > 15 ? 15 : q[0], m = q[1], one = m == 0, have = 0;
    float best = 0;
    for (int j = 0; j < n; j++) {
        unsigned t = u16le(q + 4 + 4 * j), kind = t & 15;
        if (kind != 1 && kind != 10) continue;
        float o = (float)(int16_t)u16le(q + 6 + 4 * j) / 256.0f, x;
        if (one) { if (!fwd) continue; x = o; }
        else { x = j < m ? -o : o; if ((x > 0) != (fwd != 0)) continue; }
        if (!have || fabsf(x) < fabsf(best)) { best = x; have = 1; }
    }
    *out = best; return have;
}
static void tri(const int8_t *c, float sgn, V3 fall, V3 *o)
{
    if (!c[0] && !c[1] && !c[2]) { *o = fall; return; }
    float x = c[2], y = -c[0], z = c[1], l = sqrtf(x * x + y * y + z * z);
    o->x = sgn * x / l; o->y = sgn * y / l; o->z = sgn * z / l;
}

/* ---- the lane graph ---- */
typedef struct { Lane *l; int n, cap; int **succ; int *nsucc; } G;
static float turn(const G *g, int a, int b)
{
    const Poly *P = &g->l[a].pts, *Q = &g->l[b].pts;
    if (P->n < 2 || Q->n < 2) return 0;
    float u[2], v[2]; unit2(P->p[P->n - 2], P->p[P->n - 1], u); unit2(Q->p[0], Q->p[1], v);
    float d = u[0] * v[0] + u[1] * v[1]; if (d > 1) d = 1; if (d < -1) d = -1;
    return acosf(d);
}
static void simplify(const Poly *P, Poly *o)
{
    const float ang = 0.10471976f, span = 60.0f;   /* 6 degrees */
    o->n = 0;
    if (P->n <= 2) { for (int k = 0; k < P->n; k++) poly_add(o, P->p[k]); return; }
    poly_add(o, P->p[0]);
    for (int k = 1; k < P->n - 1; k++) {
        V3 a = o->p[o->n - 1];
        float h0 = atan2f(P->p[k].y - a.y, P->p[k].x - a.x), h1 = atan2f(P->p[k + 1].y - P->p[k].y, P->p[k + 1].x - P->p[k].x);
        float d = fmodf(h1 - h0 + 3.14159265f, 6.28318531f); if (d < 0) d += 6.28318531f; d = fabsf(d - 3.14159265f);
        if (d > ang || dist2(a, P->p[k + 1]) > span) poly_add(o, P->p[k]);
    }
    poly_add(o, P->p[P->n - 1]);
}
static int build_graph(const Rn *r, G *g)
{
    int *head = malloc((size_t)r->nn * sizeof *head), *nxts = NULL;
    if (!head) return 0;
    for (int i = 0; i < r->nn; i++) head[i] = -1;
    for (int k = 0; k < r->ns; k++) {
        const uint8_t *s = r->sg + (size_t)k * 22;
        int n0 = u16le(s), n1 = u16le(s + 2);
        if (n0 >= r->nn || n1 >= r->nn) continue;
        float L = u16le(s + 4) / 64.0f; if (L < 1) L = 1;
        V3 a = node_pos(r, n0), b = node_pos(r, n1);
        float ch = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
        if (ch <= 0) ch = 1;
        V3 u = { (b.x - a.x) / ch, (b.y - a.y) / ch, (b.z - a.z) / ch }, t0, t1;
        /* +19 is the start's tangent, +16 the end's control (-tangent) (11-18: read the
           other way round every bend bowed outward, 8% of the curve off the road; this way
           0.2%, and the arc matches the stored length) */
        tri((const int8_t *)s + 19, 1, u, &t0); tri((const int8_t *)s + 16, -1, u, &t1);
        int ns = (int)(L / STEP); if (ns < 1) ns = 1;
        int pa = node_prof(r, n0), pb = node_prof(r, n1), rev = (u16le(s + 10) & 0x400) != 0, one = prof_oneway(r, pa);
        for (int fwd = 1; fwd >= 0; fwd--) {
            float x0, x1; int h0 = lane_pos(r, pa, fwd, &x0), h1 = lane_pos(r, pb, fwd, &x1);
            if (!h0 && !h1) continue;
            if (!h0) x0 = x1;
            if (!h1) x1 = x0;
            if (g->n == g->cap) {
                int c = g->cap ? g->cap * 2 : 4096; Lane *nl = realloc(g->l, (size_t)c * sizeof *nl);
                if (!nl) { free(head); return 0; }
                g->l = nl; g->cap = c;
            }
            Lane *ln = &g->l[g->n]; memset(ln, 0, sizeof *ln);
            static V3 C[1024]; int nc = ns + 1 > 1024 ? 1024 : ns + 1;
            for (int i = 0; i < nc; i++) {
                float t = (float)i / (float)(nc - 1), t2 = t * t, t3 = t2 * t;
                float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                C[i].x = h00 * a.x + h10 * t0.x * L + h01 * b.x + h11 * t1.x * L;
                C[i].y = h00 * a.y + h10 * t0.y * L + h01 * b.y + h11 * t1.y * L;
                C[i].z = h00 * a.z + h10 * t0.z * L + h01 * b.z + h11 * t1.z * L;
            }
            for (int i = 0; i < nc; i++) {
                V3 q = C[i + 1 < nc ? i + 1 : nc - 1], p0 = C[i > 0 ? i - 1 : 0];
                float dx = q.x - p0.x, dy = q.y - p0.y, dl = hypotf(dx, dy); if (dl <= 0) dl = 1;
                float x = x0 + (x1 - x0) * ((float)i / (float)(nc - 1));
                V3 o = { C[i].x + dy / dl * x, C[i].y - dx / dl * x, C[i].z };
                if (!poly_add(&ln->pts, o)) { free(head); return 0; }
            }
            int go = fwd, m0 = n0, m1 = n1;
            if (one && rev) go = !go;
            if (!go) {
                for (int i = 0, j = ln->pts.n - 1; i < j; i++, j--) { V3 t = ln->pts.p[i]; ln->pts.p[i] = ln->pts.p[j]; ln->pts.p[j] = t; }
                m0 = n1; m1 = n0;
            }
            ln->seg = k; ln->n0 = m0; ln->n1 = m1;
            g->n++;
        }
    }
    /* successors: the lanes leaving the node a lane arrives at (not its own segment's), as succ() */
    nxts = malloc((size_t)(g->n ? g->n : 1) * sizeof *nxts);
    g->succ = calloc((size_t)(g->n ? g->n : 1), sizeof *g->succ);
    g->nsucc = calloc((size_t)(g->n ? g->n : 1), sizeof *g->nsucc);
    if (!nxts || !g->succ || !g->nsucc) { free(head); free(nxts); return 0; }
    for (int i = g->n - 1; i >= 0; i--) { nxts[i] = head[g->l[i].n0]; head[g->l[i].n0] = i; }   /* lanes by start node, in order */
    for (int a = 0; a < g->n; a++) {
        int cnt = 0;
        for (int b = head[g->l[a].n1]; b >= 0; b = nxts[b]) if (g->l[b].seg != g->l[a].seg && b != a && turn(g, a, b) < UTURN) cnt++;
        if (!cnt) continue;
        g->succ[a] = malloc((size_t)cnt * sizeof **g->succ);
        if (!g->succ[a]) { free(head); free(nxts); return 0; }
        for (int b = head[g->l[a].n1]; b >= 0; b = nxts[b]) if (g->l[b].seg != g->l[a].seg && b != a && turn(g, a, b) < UTURN) g->succ[a][g->nsucc[a]++] = b;
    }
    free(head); free(nxts);
    return 1;
}

/* ---- the path cover ---- */
typedef struct { float t; int a, b; } Edge;
static int edge_cmp(const void *x, const void *y)
{
    const Edge *p = x, *q = y;
    if (p->t != q->t) return p->t < q->t ? -1 : 1;
    if (p->a != q->a) return p->a - q->a;
    return p->b - q->b;
}
/* sections routes() makes: one per chain head, one per cycle */
static int chain_count(int n, const int *nxt, const int *prv, unsigned char *seen)
{
    int c = 0;
    memset(seen, 0, (size_t)n);
    for (int pass = 0; pass < 2; pass++)
        for (int s = 0; s < n; s++) {
            if (seen[s] || (pass == 0 && prv[s] >= 0)) continue;
            c++;
            for (int a = s; a >= 0 && !seen[a]; a = nxt[a]) seen[a] = 1;
        }
    return c;
}
static int closes(int n, const int *nxt, int a, int b)   /* a -> b would close a cycle */
{
    for (int x = b, k = 0; x >= 0 && k < n; x = nxt[x], k++) if (x == a) return 1;
    return 0;
}
/* only when the cover is still over MAX_SEC: join the remaining ends across the gentlest turns
   over UTURN, as few as fit (freeroam.py hairpin_joins). Rockport's true road curves (11-18)
   make every sharp Y junction end a section, ~3,200 of them; these joins are no links */
#define SEC_MARGIN 32
static int hairpin_joins(const G *g, int *nxt, int *prv)
{
    int n = g->n, heads = 0;
    for (int a = 0; a < n; a++) heads += prv[a] < 0;
    if (heads <= MAX_SEC - SEC_MARGIN) return 1;
    int maxnode = 0;
    for (int a = 0; a < n; a++) { if (g->l[a].n0 > maxnode) maxnode = g->l[a].n0; if (g->l[a].n1 > maxnode) maxnode = g->l[a].n1; }
    int *head = malloc((size_t)(maxnode + 1) * sizeof *head), *nx = malloc((size_t)n * sizeof *nx);
    unsigned char *seen = malloc((size_t)n);
    Edge *e = NULL; long ne = 0, cap = 0; int ok = 0;
    if (!head || !nx || !seen) goto out;
    for (int i = 0; i <= maxnode; i++) head[i] = -1;
    for (int i = n - 1; i >= 0; i--) { nx[i] = head[g->l[i].n0]; head[g->l[i].n0] = i; }
    for (int a = 0; a < n; a++) {
        if (nxt[a] >= 0) continue;
        for (int b = head[g->l[a].n1]; b >= 0; b = nx[b]) {
            if (b == a || prv[b] >= 0 || g->l[b].seg == g->l[a].seg) continue;
            float t = turn(g, a, b);
            if (t < UTURN) continue;
            if (ne == cap) { long c = cap ? cap * 2 : 4096; Edge *m = realloc(e, (size_t)c * sizeof *m); if (!m) goto out; e = m; cap = c; }
            e[ne].t = t; e[ne].a = a; e[ne].b = b; ne++;
        }
    }
    if (ne) qsort(e, (size_t)ne, sizeof *e, edge_cmp);
    long k = 0; int target = MAX_SEC - SEC_MARGIN;
    while (k < ne) {
        while (k < ne && heads > target) {
            int a = e[k].a, b = e[k].b; k++;
            if (nxt[a] < 0 && prv[b] < 0 && !closes(n, nxt, a, b)) { nxt[a] = b; prv[b] = a; heads--; }
        }
        int over = chain_count(n, nxt, prv, seen) - (MAX_SEC - 8);
        if (over <= 0) break;
        target = heads - over;
    }
    ok = 1;
out:
    free(head); free(nx); free(seen); free(e);
    return ok;
}
static int cover(const G *g, int *nxt, int *prv)
{
    int n = g->n; long ne = 0;
    for (int a = 0; a < n; a++) { nxt[a] = prv[a] = -1; ne += g->nsucc[a]; }
    Edge *e = malloc((size_t)(ne ? ne : 1) * sizeof *e);
    if (!e) return 0;
    long k = 0;
    for (int a = 0; a < n; a++) for (int i = 0; i < g->nsucc[a]; i++) { e[k].t = turn(g, a, g->succ[a][i]); e[k].a = a; e[k].b = g->succ[a][i]; k++; }
    qsort(e, (size_t)ne, sizeof *e, edge_cmp);
    for (long i = 0; i < ne; i++) if (nxt[e[i].a] < 0 && prv[e[i].b] < 0) { nxt[e[i].a] = e[i].b; prv[e[i].b] = e[i].a; }
    free(e);
    /* augmenting paths (Kuhn), iterative: a stack of (piece, next successor to try, the successor taken) */
    int *seen = calloc((size_t)(n ? n : 1), sizeof *seen), *sa = malloc((size_t)(n + 1) * sizeof *sa),
        *si = malloc((size_t)(n + 1) * sizeof *si), *sb = malloc((size_t)(n + 1) * sizeof *sb);
    if (!seen || !sa || !si || !sb) { free(seen); free(sa); free(si); free(sb); return 0; }
    int stamp = 0;
    for (int a0 = 0; a0 < n; a0++) {
        if (nxt[a0] >= 0) continue;
        stamp++;
        int top = 0; sa[0] = a0; si[0] = 0; int found = 0;
        while (top >= 0 && !found) {
            int a = sa[top];
            if (si[top] >= g->nsucc[a]) { top--; continue; }
            int b = g->succ[a][si[top]++];
            if (seen[b] == stamp) continue;
            seen[b] = stamp; sb[top] = b;
            if (prv[b] < 0) found = 1;
            else if (top + 1 <= n) { top++; sa[top] = prv[b]; si[top] = 0; }
        }
        if (found) for (int f = top; f >= 0; f--) { nxt[sa[f]] = sb[f]; prv[sb[f]] = sa[f]; }
    }
    free(seen); free(sa); free(si); free(sb);
    return hairpin_joins(g, nxt, prv);
}

/* ---- the route file ---- */
typedef struct { int (*slot)[2]; int nslot; } Slots;   /* per point: up to two link indices */
typedef struct { Link *v; int n, cap; } Links;
static int links_add(Links *L, Link k)
{
    if (L->n == L->cap) { int c = L->cap ? L->cap * 2 : 8192; Link *n = realloc(L->v, (size_t)c * sizeof *n); if (!n) return 0; L->v = n; L->cap = c; }
    L->v[L->n++] = k; return 1;
}
static int slot_cnt(const int s[2]) { return (s[0] >= 0) + (s[1] >= 0); }
static void put(Slots *S, Links *L, int si, int i, Link k, int back_ok, int step, long *dropped)
{
    int j = i, np = S[si].nslot;
    while (j >= 0 && j < np && slot_cnt(S[si].slot[j]) >= 2) { if (!back_ok) break; j += step; }
    if (j < 0 || j >= np || slot_cnt(S[si].slot[j]) >= 2) { (*dropped)++; return; }
    int *s = S[si].slot[j]; s[s[0] >= 0 ? 1 : 0] = L->n;
    k.si = si; k.j = j;
    if (!links_add(L, k)) (*dropped)++;
}
static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); w32(p, u); }
static int clampi(long v, long lo, long hi) { return (int)(v < lo ? lo : v > hi ? hi : v); }

static int routes(const G *g, FILE *w, XpRoadStats *st)
{
    int n = g->n, ok = 0;
    int *nxt = malloc((size_t)(n ? n : 1) * sizeof *nxt), *prv = malloc((size_t)(n ? n : 1) * sizeof *prv);
    int *seen = calloc((size_t)(n ? n : 1), 1 * sizeof(int)), *chain = malloc((size_t)(n ? n : 1) * sizeof *chain);
    int *cstart = malloc((size_t)(n + 1) * sizeof *cstart), *order = malloc((size_t)(n ? n : 1) * 2 * sizeof *order);
    int *wsid = malloc((size_t)(n ? n : 1) * sizeof *wsid), *wi0 = malloc((size_t)(n ? n : 1) * sizeof *wi0), *wi1 = malloc((size_t)(n ? n : 1) * sizeof *wi1);
    Poly *secs = NULL; Slots *S = NULL; Links L = {0}; int nsec = 0;
    if (!nxt || !prv || !seen || !chain || !cstart || !order || !wsid || !wi0 || !wi1) goto out;
    if (!cover(g, nxt, prv)) goto out;
    /* chains: start where nothing continues in, then the leftovers (cycles cut where entered) */
    int no = 0, nch = 0, nc = 0;
    for (int a = 0; a < n; a++) if (prv[a] < 0) order[no++] = a;
    for (int a = 0; a < n; a++) order[no++] = a;
    for (int q = 0; q < no; q++) {
        int s = order[q]; if (seen[s]) continue;
        cstart[nch++] = nc; int a = s, last = -1;
        while (a >= 0 && !seen[a]) { seen[a] = 1; chain[nc++] = a; last = a; a = nxt[a]; }
        if (a >= 0) nxt[last] = -1;
    }
    cstart[nch] = nc;
    if (nch > MAX_SEC) { st->sections = nch; goto out; }
    nsec = nch;
    secs = calloc((size_t)nsec, sizeof *secs); S = calloc((size_t)nsec, sizeof *S);
    if (!secs || !S) goto out;
    for (int c = 0; c < nch; c++) {
        Poly *P = &secs[c];
        for (int k = cstart[c]; k < cstart[c + 1]; k++) {
            int a = chain[k]; const Poly *Q = &g->l[a].simp;
            if (P->n) {
                if (dist2(P->p[P->n - 1], Q->p[0]) < 0.5f) P->n--;
                else {   /* a Hermite connector across the gap (writers._hermite, step 8) */
                    V3 A = P->p[P->n - 1], B = Q->p[0]; float ta[2], tb[2];
                    if (P->n > 1) unit2(P->p[P->n - 2], A, ta); else unit2(A, B, ta);
                    unit2(Q->p[0], Q->p[1], tb);
                    float d = dist2(A, B); int m = (int)(d / 8.0f) + 1; if (m < 2) m = 2;
                    for (int i = 1; i < m - 1; i++) {
                        float t = (float)i / (float)(m - 1), t2 = t * t, t3 = t2 * t;
                        float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                        V3 v = { h00 * A.x + h10 * d * ta[0] + h01 * B.x + h11 * d * tb[0],
                                 h00 * A.y + h10 * d * ta[1] + h01 * B.y + h11 * d * tb[1], A.z + (B.z - A.z) * t };
                        if (!poly_add(P, v)) goto out;
                    }
                }
            }
            wsid[a] = c; wi0[a] = P->n;
            for (int i = 0; i < Q->n; i++) if (!poly_add(P, Q->p[i])) goto out;
            wi1[a] = P->n - 1;
        }
        if (P->n > 32767) goto out;
        S[c].nslot = P->n; S[c].slot = malloc((size_t)(P->n ? P->n : 1) * sizeof *S[c].slot);
        if (!S[c].slot) goto out;
        for (int i = 0; i < P->n; i++) S[c].slot[i][0] = S[c].slot[i][1] = -1;
    }
    /* links: where a section ends the sharpest other branch always, the rest a coin */
    long dropped = 0;
    int outs[64];
    for (int a = 0; a < n; a++) {
        int no2 = 0;
        for (int i = 0; i < g->nsucc[a] && no2 < 64; i++) if (g->succ[a][i] != nxt[a]) outs[no2++] = g->succ[a][i];
        if (!no2) continue;
        for (int i = 1; i < no2; i++) for (int j = i; j > 0 && turn(g, a, outs[j - 1]) > turn(g, a, outs[j]); j--) { int t = outs[j]; outs[j] = outs[j - 1]; outs[j - 1] = t; }
        int sid = wsid[a], i0 = wi0[a], i1 = wi1[a];
        Link k = { 0, 0, -1, 0, 0, 0 };
        if (nxt[a] < 0) { k.tgt = outs[no2 - 1]; k.fl = LK_ALWAYS; put(S, &L, sid, i1, k, 0, -1, &dropped); no2--; }
        for (int i = 0; i < no2; i++) {
            k.tgt = outs[i]; k.fl = LK_COIN;
            put(S, &L, sid, nxt[a] < 0 ? (i1 - 1 > i0 ? i1 - 1 : i0) : i1, k, 1, -1, &dropped);
        }
    }
    /* back links at each forward link's far end */
    int nf = L.n;
    for (int f = 0; f < nf; f++) {
        Link fw = L.v[f]; int b = fw.tgt, tsid = wsid[b], ti = wi0[b];
        int first = ti == 0;
        if (first) for (int q = 0; q < 2; q++) { int x = S[tsid].slot[0][q]; if (x >= 0 && (L.v[x].fl & LK_BACK) && (L.v[x].fl & 0xF) == 2) first = 0; }
        Link k = { 0, 0, -1, fw.si, fw.j, LK_BACK | (first ? LK_ALWAYS : LK_COIN) };
        put(S, &L, tsid, ti, k, 1, 1, &dropped);
    }
    if (L.n > 65534) goto out;
    /* 0x34121 sections, 0x34122 links */
    long body = 0;
    for (int c = 0; c < nsec; c++) body += 0x8c + 0x38L * secs[c].n;
    uint8_t h8[8]; w32(h8, 0x34121); w32(h8 + 4, (uint32_t)body);
    if (fwrite(h8, 1, 8, w) != 8) goto out;
    for (int c = 0; c < nsec; c++) {
        const Poly *P = &secs[c]; int sid = c + 1;
        uint8_t h[0x8c]; memset(h, 0, sizeof h);
        w16(h + 0x0a, (unsigned)sid); w16(h + 0x34, (unsigned)P->n);
        float run = 0, mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
        for (int k = 0; k < P->n; k++) {
            if (k) run += dist2(P->p[k - 1], P->p[k]);
            if (P->p[k].x < mnx) mnx = P->p[k].x;
            if (P->p[k].y < mny) mny = P->p[k].y;
            if (P->p[k].x > mxx) mxx = P->p[k].x;
            if (P->p[k].y > mxy) mxy = P->p[k].y;
        }
        wf(h + 0x38, 0); wf(h + 0x3c, run);
        wf(h + 0x60, mnx - HALF_W); wf(h + 0x64, mny - HALF_W); wf(h + 0x68, mxx + HALF_W); wf(h + 0x6c, mxy + HALF_W);
        w32(h + 0x70, SEC_FLAGS); h[0x85] = 1;
        if (fwrite(h, 1, sizeof h, w) != sizeof h) goto out;
        float prog = 0;
        for (int k = 0; k < P->n; k++) {
            uint8_t r[0x38]; memset(r, 0, sizeof r);
            V3 p = P->p[k]; float dv[2], seg = 0;
            if (k) prog += dist2(P->p[k - 1], p);
            if (k + 1 < P->n) { unit2(p, P->p[k + 1], dv); seg = dist2(p, P->p[k + 1]); }
            else unit2(P->p[k - 1 >= 0 ? k - 1 : 0], p, dv);
            wf(r, p.x); wf(r + 4, p.y); wf(r + 8, prog); w32(r + 12, PT_FLAGS | (uint32_t)sid);
            w16(r + 0x10, (unsigned)clampi((long)(seg * 256.0f), 0, 65535));
            w16(r + 0x12, (unsigned)(uint16_t)(int16_t)clampi(lroundf(p.z * 8.0f), -32768, 32767));
            w16(r + 0x14, (unsigned)(HALF_W * 256)); w16(r + 0x16, (unsigned)(HALF_W * 256));
            w16(r + 0x18, (unsigned)(uint16_t)(int16_t)lroundf(dv[0] * 32767.0f));
            w16(r + 0x1a, (unsigned)(uint16_t)(int16_t)lroundf(dv[1] * 32767.0f));
            int *s = S[c].slot[k];
            w16(r + 0x1c, s[0] >= 0 ? (unsigned)s[0] : 0xFFFFu); w16(r + 0x1e, s[1] >= 0 ? (unsigned)s[1] : 0xFFFFu);
            static const uint8_t lanes[10] = { 0, 0, 0xFF, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
            static const uint8_t speed[10] = { 100, 100, 6, LANE_SPEED, 6, 6, 6, 6, 6, 6 };
            memcpy(r + 0x22, lanes, 10); memcpy(r + 0x2c, speed, 10);
            if (fwrite(r, 1, sizeof r, w) != sizeof r) goto out;
        }
    }
    w32(h8, 0x34122); w32(h8 + 4, (uint32_t)(4 + 32L * L.n));
    uint8_t z4[4] = { 0 };
    if (fwrite(h8, 1, 8, w) != 8 || fwrite(z4, 1, 4, w) != 4) goto out;
    for (int i = 0; i < L.n; i++) {
        const Link *k = &L.v[i]; int tsid, ti;
        if (k->tgt >= 0) { tsid = wsid[k->tgt] + 1; ti = wi0[k->tgt]; } else { tsid = k->tsi + 1; ti = k->tj; }
        uint8_t r[32]; memset(r, 0, sizeof r);
        wf(r, 0); w16(r + 4, (unsigned)ti); w16(r + 6, (unsigned)k->j); w16(r + 8, (unsigned)tsid); w16(r + 10, (unsigned)(k->si + 1));
        w32(r + 12, k->fl);
        for (int q = 0; q < 4; q++) w16(r + 0x18 + 2 * q, 0xFFFFu);
        if (fwrite(r, 1, sizeof r, w) != sizeof r) goto out;
    }
    st->sections = nsec; st->links = L.n; st->dropped = dropped;
    for (int c = 0; c < nsec; c++) st->points += secs[c].n;
    ok = 1;
out:
    if (secs) for (int c = 0; c < nsec; c++) free(secs[c].p);
    if (S) for (int c = 0; c < nsec; c++) free(S[c].slot);
    free(secs); free(S); free(L.v);
    free(nxt); free(prv); free(seen); free(chain); free(cstart); free(order); free(wsid); free(wi0); free(wi1);
    return ok;
}

int xp_routes(const char *bun_path, const char *out_path, int game, XpRoadStats *st)
{
    XpRoadStats dummy; if (!st) st = &dummy;
    memset(st, 0, sizeof *st);
    if (game != XP_GAME_MW) return XP_ERR_FORMAT;   /* Carbon's road network: with Carbon */
    FILE *f = fopen(bun_path, "rb"); if (!f) return XP_ERR_OPEN_IN;
    uint8_t *chunk = NULL; size_t clen = 0; int rc = XP_ERR_FORMAT;
    for (;;) {   /* the top-level 0x3b800 chunk, read alone */
        uint8_t h[8];
        if (fread(h, 1, 8, f) != 8) break;
        uint32_t id = u32le(h), sz = u32le(h + 4);
        if (id == 0x3b800) {
            chunk = malloc(sz ? sz : 1); if (!chunk) { rc = XP_ERR_MEMORY; break; }
            if (fread(chunk, 1, sz, f) != sz) { rc = XP_ERR_READ; free(chunk); chunk = NULL; break; }
            clen = sz; break;
        }
        if (fseek(f, (long)sz, SEEK_CUR)) break;
    }
    fclose(f);
    if (!chunk) return rc;
    Rn r; G g; memset(&g, 0, sizeof g);
    rc = XP_ERR_FORMAT;
    if (rn_find(chunk, clen, &r)) {
        rc = XP_ERR_MEMORY;
        if (build_graph(&r, &g)) {
            int okp = 1;
            for (int i = 0; i < g.n && okp; i++) { simplify(&g.l[i].pts, &g.l[i].simp); okp = g.l[i].simp.n >= 2; }
            st->lanes = g.n;
            for (int i = 0; i < g.n; i++) if (!g.nsucc[i]) st->dead++;
            FILE *w = okp ? fopen(out_path, "wb") : NULL;
            if (!okp) rc = XP_ERR_FORMAT;
            else if (!w) rc = XP_ERR_OPEN_OUT;
            else {
                int ok = routes(&g, w, st);
                if (fclose(w)) ok = 0;
                rc = ok ? XP_OK : st->sections > MAX_SEC ? XP_ERR_FORMAT : XP_ERR_WRITE;
                if (!ok) remove(out_path);
            }
        }
    }
    for (int i = 0; i < g.n; i++) { free(g.l[i].pts.p); free(g.l[i].simp.p); }
    if (g.succ) for (int i = 0; i < g.n; i++) free(g.succ[i]);
    free(g.succ); free(g.nsucc); free(g.l); free(chunk);
    return rc;
}
