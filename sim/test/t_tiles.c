/* Tile streaming check (PC, sim objects): drives world_tiles_assemble along a
 * path through L4RA with the read-ahead running, and compares every
 * assembled scene against an independent full decode of the tiles in reach
 * with the placement rules (ground by per-mesh reach, objects by tile
 * centre, LOD pair roles, far full models without prelight). Also prints the
 * kept tile memory against what keeping every tile fully decoded costs.
 *
 *   built + run by sim/test/run_tiles.sh */
#include "../../src/world_tiles.c"
#undef main

typedef struct { char name[32]; int nv, ni, role, vcol; uint64_t h; } Sig;

static uint64_t fnv(const void *p, size_t n, uint64_t h) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
static Sig sig_of(const N2Mesh *m, int role, int vcol) {
    Sig s; memset(&s, 0, sizeof s);
    memcpy(s.name, m->sname, sizeof s.name);
    s.nv = m->nverts; s.ni = m->nidx; s.role = role; s.vcol = vcol;
    uint64_t h = 1469598103934665603ull;
    h = fnv(m->verts, (size_t)m->nverts * (m->authored_normals ? 8 : 5) * 4, h);
    h = fnv(m->idx, (size_t)m->nidx * 2, h);
    if (vcol && m->vcol) h = fnv(m->vcol, (size_t)m->nverts * 4, h);
    s.h = h;
    return s;
}
static int sig_cmp(const void *a, const void *b) {
    const Sig *x = (const Sig *)a, *y = (const Sig *)b;
    if (x->h != y->h) return x->h < y->h ? -1 : 1;
    if (x->role != y->role) return x->role - y->role;
    if (x->vcol != y->vcol) return x->vcol - y->vcol;
    return memcmp(x->name, y->name, sizeof x->name);
}

/* the expected scene + what all its tiles cost decoded in full */
static int expect(const char *track, float fx, float fy, float radius, Sig **out, size_t *full_bytes) {
    int n = 0, cap = 4096;
    Sig *s = (Sig *)malloc((size_t)cap * sizeof *s);
    float reach = radius + 60.0f;
    *full_bytes = 0;
    for (int k = 0; k < g_wt_nindex; k++) {
        const WtIndexEntry *e = &g_wt_index[k];
        if (e->x == -32768 || wt_tile_d2(e, fx, fy) > reach * reach) continue;
        WtLoaded t;
        if (!wt_read_decode_t(track, e->x, e->y, &t, 1, 0)) { printf("expect: tile %d %d unreadable\n", e->x, e->y); exit(2); }
        *full_bytes += t.bytes;
        int tile_objects = wt_objects_at(e, fx, fy, radius);
        float tcx = ((float)e->x + 0.5f) * WTILE_SIZE - fx, tcy = ((float)e->y + 0.5f) * WTILE_SIZE - fy;
        int near = tcx * tcx + tcy * tcy < WT_LOD_NEAR_M * WT_LOD_NEAR_M;
        for (int i = 0; i < t.n; i++) {
            const N2Mesh *m = &t.m[i];
            const float *b = m->world_bounds;
            int ground = m->cat == N2_ROAD || m->cat == N2_TERRAIN;
            if (ground ? wt_rect_d2(b[0], b[1], b[3], b[4], fx, fy) > reach * reach : !tile_objects) continue;
            int role = m->world_role, drop = 0;
            if (role == 1 || role == 2) {
                if (near && role == 2) continue;
                if (near) role = 0; else if (role == 1) drop = 1;
            }
            if (n == cap) { cap *= 2; s = (Sig *)realloc(s, (size_t)cap * sizeof *s); }
            s[n++] = sig_of(m, role, !drop && m->vcol);
        }
        wt_release(&t);
    }
    qsort(s, (size_t)n, sizeof *s, sig_cmp);
    *out = s;
    return n;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *track = "STREAML4RA", *troot = "ux0:data/OpenUG2/TRACKS";
    float radius = 400.0f;
    /* out through downtown and back, 80 m steps (a build every ~80 m) */
    static const float way[][2] = { { 800, -400 }, { -800, 200 }, { 400, -300 }, { 400, 900 } };
    float px = way[0][0], py = way[0][1];
    N2Scene prev = { 0 }, prev_v = { 0 };
    int steps = 0, bad = 0;
    size_t worst_kept = 0, worst_full = 0;
    for (int w = 1; w < 4; w++) {
        float dx = way[w][0] - px, dy = way[w][1] - py, len = sqrtf(dx * dx + dy * dy);
        int ns = (int)(len / 80.0f);
        for (int i = 0; i <= ns; i++) {
            float fx = px + dx * (float)i / (float)ns, fy = py + dy * (float)i / (float)ns;
            float ax = px + dx * (float)(i + 1) / (float)ns, ay = py + dy * (float)(i + 1) / (float)ns;
            N2Scene sc = { 0 }, vi = { 0 };
            int got = world_tiles_assemble(&sc, &vi, troot, track, fx, fy, radius, 0, NULL);
            if (got <= 0) { printf("step %d: assemble failed\n", steps); return 1; }
            /* the scene's sky is the global tile's: compare world meshes only */
            Sig *have = (Sig *)malloc((size_t)sc.count * sizeof *have);
            int nh = 0;
            for (int m = 0; m < sc.count; m++) {
                if (sc.meshes[m].cat == N2_SKY) continue;
                have[nh++] = sig_of(&sc.meshes[m], sc.meshes[m].world_role, sc.meshes[m].vcol != NULL);
            }
            qsort(have, (size_t)nh, sizeof *have, sig_cmp);
            Sig *want; size_t full;
            int nw = expect(track, fx, fy, radius, &want, &full);
            int same = nh == nw;
            for (int m = 0; same && m < nh; m++) same = !sig_cmp(&have[m], &want[m]);
            size_t kept = 0;
            for (int t = 0; t < g_wt_nloaded; t++) kept += g_wt_loaded[t].bytes;
            if (kept > worst_kept) worst_kept = kept;
            if (full > worst_full) worst_full = full;
            printf("step %3d at (%5.0f, %5.0f): %5d meshes %s (expected %d) | kept %6zu KB vs all-full %6zu KB\n",
                   steps, fx, fy, nh, same ? "OK  " : "DIFF", nw, kept >> 10, full >> 10);
            if (!same) bad++;
            free(have); free(want);
            /* the old resident goes once the new one is up */
            wt_scene_drop(&prev); wt_scene_drop(&prev_v);
            prev = sc; prev_v = vi;
            world_tiles_aim(ax, ay);
            SDL_Delay(400);   /* the read-ahead's time between builds */
            steps++;
        }
        px = way[w][0]; py = way[w][1];
    }
    printf("%d steps, %d differ; peak kept %zu KB vs all-full %zu KB\n", steps, bad, worst_kept >> 10, worst_full >> 10);
    return bad ? 1 : 0;
}
