/* A live smokeable end to end: an L4RA water barrel (ZCS_Barrel_A) is
   converted by a touch, kicked sideways, then stepped with its polygons
   (smk_begin / carcoll_run_all / smk_end) and the particle system. Prints
   its path, sleep and the BARREL_WATERA particles its motion emitter makes. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wcoll.h"
#include "carcoll.h"
#include "smokeable.h"
#include "acid.h"
void smk_sim_present_all(void);
static unsigned char *rd(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}
int main(int argc, char **argv) {
    const char *want = argc > 1 ? argv[1] : "ZCS_Barrel_A";
    float kick = argc > 2 ? (float)atof(argv[2]) : 1.5f;
    const char *root = "E:/NFS Underground 2 Vita/data/nfsug2/TRACKS";
    char a[512], b[512];
    snprintf(a, sizeof a, "%s/STREAML4RA.BUN", root); snprintf(b, sizeof b, "%s/L4RA.BUN", root);
    WColl *w = wcoll_load(a, b);
    long gl; unsigned char *g = rd("E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN", &gl);
    printf("acid: %d records\n", g ? acid_load(g, gl, 1) : -1);
    if (!w || !smk_load(root, "L4RA")) { printf("load failed\n"); return 1; }
    smk_sim_present_all();
    printf("ndefs %d\n", acid_ndefs());
    for (int i = 0; i < acid_ndefs(); i++) {
        AcidDef *d = acid_def_at(i);
        if (d->group == 0x95c9f5fau || d->group == 0xe84348eau || d->group == 0x95c9f5fbu)
            printf("def %s group %08x vis %.1f on %.2f/%.2f off %.2f/%.2f rate %.1f life %.2f speed %.1f\n", d->name, d->group,
                   d->vis, d->on_m, d->on_v, d->off_m, d->off_v, d->rate, d->life, d->speed);
    }
    FILE *f = fopen("E:/re-tools/out/smk/spawners_L4RA.txt", "r");
    char nm[64]; float mn[3], mx[3], c[3] = { 0 }; int found = 0;
    while (fscanf(f, "%63s %f %f %f %f %f %f", nm, &mn[0], &mn[1], &mn[2], &mx[0], &mx[1], &mx[2]) == 7)
        if (!strcmp(nm, want)) { for (int k = 0; k < 3; k++) c[k] = (mn[k] + mx[k]) / 2; found = 1; break; }
    if (!found) { printf("no %s\n", want); return 1; }
    printf("%s at (%.2f %.2f %.2f)\n", want, c[0], c[1], c[2]);
    {   float cam[1][3] = { { c[0] - 6, c[1], c[2] + 2 } }; acid_set_cameras((const float (*)[3])cam, 1); }
    if (getenv("T_DIRECT")) {   /* the emitter alone */
        AcidEmitter *t = acid_clones(0x95c9f5fau), *e = acid_copy(t);
        memset(e->m, 0, sizeof e->m); e->m[0] = e->m[5] = e->m[10] = e->m[15] = 1;
        memcpy(e->m + 12, c, sizeof c);
        acid_auto_add(e);
        for (int i = 0; i < 10; i++) acid_tick(1.0f / 60);
        printf("direct: state %d timer %.2f particles %d groups %d\n", e->state, e->timer, acid_live_particles(), acid_live_groups());
        return 0;
    }
    smk_begin(1.0f / 60, w);                       /* the templates */
    smk_touch(c, 0.2f);
    static CarCollCar cs[32];
    int n = smk_bodies(cs, 32, 1);
    printf("live bodies %d\n", n);
    if (!n) return 1;
    {   /* a kick at the top: ConsiderImpulse wakes it, then the impulse */
        float J[3] = { kick, 0, 0 }, r[3] = { 0, 0, 0.5f };
        float s = cs[0].ops->consider(cs[0].obj, J, r);
        J[0] *= s; cs[0].ops->impulse(cs[0].obj, J, r);
    }
    smk_end(c);
    for (int fr = 0; fr < 600; fr++) {
        smk_begin(1.0f / 60, w);
        memset(cs, 0, sizeof cs);
        n = smk_bodies(cs, 32, 1);
        CarCollHit h[32];
        int nh = carcoll_run_all(cs, n, w, h, 32, NULL, NULL);
        smk_end(c);
        acid_tick(1.0f / 60);
        if (fr % 30 == 0 || fr == 599) {
            N2Body pb; if (n) cs[0].ops->body(cs[0].obj, &pb);
            printf("f%3d bodies %d hits %2d pos (%.2f %.2f %.2f) v (%.2f %.2f %.2f) up.z %.2f  particles %d groups %d\n",
                   fr, n, nh, n ? pb.pos[0] : 0, n ? pb.pos[1] : 0, n ? pb.pos[2] : 0,
                   n ? pb.v[0] : 0, n ? pb.v[1] : 0, n ? pb.v[2] : 0, n ? pb.R[10] : 0,
                   acid_live_particles(), acid_live_groups());
        }
    }
    smk_report();
    return 0;
}
