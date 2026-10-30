/* Sim spawn finder for prop tests: L4RA spawners of one name with a straight
   15 m run of ROAD (surface 1) up to them, flat enough to drive. Prints
   OPENUG2_SIM_SPAWN values (x y heading_deg) aimed at the prop. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "wcoll.h"
int main(int argc, char **argv) {
    const char *want = argc > 1 ? argv[1] : "ZCS_Barrel_A";
    const char *root = "E:/NFS Underground 2 Vita/data/nfsug2/TRACKS";
    char a[512], b[512];
    snprintf(a, sizeof a, "%s/STREAML4RA.BUN", root); snprintf(b, sizeof b, "%s/L4RA.BUN", root);
    WColl *w = wcoll_load(a, b);
    if (!w) return 1;
    FILE *f = fopen("E:/re-tools/out/smk/spawners_L4RA.txt", "r");
    char nm[64]; float mn[3], mx[3]; int shown = 0;
    static WCollCtx ctx;
    while (shown < 8 && fscanf(f, "%63s %f %f %f %f %f %f", nm, &mn[0], &mn[1], &mn[2], &mx[0], &mx[1], &mx[2]) == 7) {
        if (strcmp(nm, want)) continue;
        float c[3] = { (mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, mn[2] };
        for (int d = 0; d < 32; d++) {
            float ang = d * 6.2831853f / 32, dx = cosf(ang), dy = sinf(ang);
            int ok = 1;
            for (float s = 3.0f; s <= 18.0f && ok; s += 1.5f) {
                float p[3] = { c[0] - dx * s, c[1] - dy * s, c[2] + 1.0f }, n[3];
                int surf = 0, found = 0;
                float z = wcoll_ground(w, &ctx, p, &surf, n, &found);
                ok = found && surf == 1 && fabsf(z - c[2]) < 1.0f && n[2] > 0.95f;
            }
            if (!ok) continue;
            printf("%s at (%.1f %.1f %.1f): OPENUG2_SIM_SPAWN=\"%.1f %.1f %.1f\"\n", nm, c[0], c[1], c[2],
                   c[0] - dx * 15, c[1] - dy * 15, ang * 57.29578f);
            shown++;
            break;
        }
    }
    return 0;
}
