/* Are smokeable props baked into the collision polygons? For every L4RA spawner
   box, count the non-ground polygons lying wholly inside the box (+0.3 m). */
#include <stdio.h>
#include <string.h>
#include "wcoll.h"
int main(void) {
    const char *root = "E:/NFS Underground 2 Vita/data/nfsug2/TRACKS/";
    char a[512], b[512];
    snprintf(a, sizeof a, "%sSTREAML4RA.BUN", root); snprintf(b, sizeof b, "%sL4RA.BUN", root);
    WColl *w = wcoll_load(a, b);
    if (!w) { printf("no wcoll\n"); return 1; }
    FILE *f = fopen("E:/re-tools/out/smk/spawners_L4RA.txt", "r");
    char nm[64]; float mn[3], mx[3]; int tot = 0, hit = 0;
    static WCollPoly p[512];
    while (fscanf(f, "%63s %f %f %f %f %f %f", nm, &mn[0], &mn[1], &mn[2], &mx[0], &mx[1], &mx[2]) == 7) {
        WCollCtx c; wcoll_ctx_init(&c);
        float pos[3] = { (mn[0]+mx[0])/2, (mn[1]+mx[1])/2, mn[2] + 1 };
        float g0[2] = { mn[0] - 0.3f, mn[1] - 0.3f }, g1[2] = { mx[0] + 0.3f, mx[1] + 0.3f };
        int sf, found; float nrm[3];
        wcoll_ground(w, &c, pos, &sf, nrm, &found);
        int n = wcoll_gather(w, &c, pos, g0, g1, p, 512), in = 0, walls = 0;
        for (int i = 0; i < n; i++) {
            int all = 1;
            for (int v = 0; v < p[i].nv; v++)
                for (int k = 0; k < 3; k++) {
                    float lo = (k < 2 ? g0[k] : mn[2] - 0.3f), hi = (k < 2 ? g1[k] : mx[2] + 0.3f);
                    if (p[i].v[v][k] < lo || p[i].v[v][k] > hi) all = 0;
                }
            if (all && p[i].n[2] < 0.7f) in++;
            if (p[i].n[2] < 0.7f) walls++;
        }
        tot++; if (in) hit++;
        if (in || tot <= 6) printf("%-22s gathered %3d walls %3d inside-box %d (pack %d)\n", nm, n, walls, in, c.pack);
    }
    printf("spawners %d with polygons inside their box: %d\n", tot, hit);
    return 0;
}
