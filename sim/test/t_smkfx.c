/* The 8.5 prop effects: an L4RA water barrel (ZCS_Barrel_A) kicked over
   against the world. Checks its contact surface (info +0xd0), the generic
   contact effect from its hits (0x5a9620 -> acid_contact, as main.c's
   smk_hit_fx), and the water-drum slosh (0x46ae00): one claim per prop and
   three SND_BarrelSplash voices at 2000/4000/7000 from the collision sound. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wcoll.h"
#include "carcoll.h"
#include "smokeable.h"
#include "acid.h"
#include "audio.h"
#include "carsfx.h"
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
    const char *data = "E:/NFS Underground 2 Vita/data/nfsug2";
    char a[512], b[512], root[512];
    snprintf(root, sizeof root, "%s/TRACKS", data);
    snprintf(a, sizeof a, "%s/STREAML4RA.BUN", root); snprintf(b, sizeof b, "%s/L4RA.BUN", root);
    WColl *w = wcoll_load(a, b);
    long gl; unsigned char *g = rd("E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN", &gl);
    printf("acid: %d records\n", g ? acid_load(g, gl, 1) : -1);
    if (!w || !smk_load(root, "L4RA")) { printf("load failed\n"); return 1; }
    smk_sim_present_all();
    printf("carsfx maps %d\n", carsfx_init(data));
    {   int coll[1] = { 0 }; audio_load_carfx_banks(data, coll, 1); }
    carsfx_set_slosh_claim(smk_slosh_claim);
    FILE *f = fopen("E:/re-tools/out/smk/spawners_L4RA.txt", "r");
    char nm[64]; float mn[3], mx[3], c[3] = { 0 }; int found = 0;
    while (fscanf(f, "%63s %f %f %f %f %f %f", nm, &mn[0], &mn[1], &mn[2], &mx[0], &mx[1], &mx[2]) == 7)
        if (!strcmp(nm, want)) { for (int k = 0; k < 3; k++) c[k] = (mn[k] + mx[k]) / 2; found = 1; break; }
    if (!found) { printf("no %s\n", want); return 1; }
    printf("%s at (%.2f %.2f %.2f)\n", want, c[0], c[1], c[2]);
    {   float cam[1][3] = { { c[0] - 6, c[1], c[2] + 2 } }; acid_set_cameras((const float (*)[3])cam, 1); }
    CarSfxListener L; memset(&L, 0, sizeof L);
    L.cam[0] = c[0] - 6; L.cam[1] = c[1]; L.cam_fwd[0] = 1; L.car[0] = c[0] - 6; L.car[1] = c[1]; L.car_fwd[0] = 1;
    L.cam_z = c[2] + 2; L.coll = 1;
    carsfx_update(1.0f / 60, &L, NULL, 0, NULL, 0);
    smk_begin(1.0f / 60, w);
    smk_touch(c, 0.2f);
    static CarCollCar cs[32];
    int n = smk_bodies(cs, 32, 1);
    if (!n) { printf("no live body\n"); return 1; }
    int surf = -1, cat = 0; float v[3];
    smk_body_contact(&cs[0], &surf, v);
    int serial = smk_body_sound(&cs[0], &cat);
    printf("live: serial %d category %d surface %d (%s)\n", serial, cat, surf, surf >= 0 ? acid_surfaces[surf].name : "?");
    printf("claim before wake: %d (want 0)\n", smk_slosh_claim(serial));
    {   float J[3] = { kick, 0, 0 }, r[3] = { 0, 0, 0.5f };
        float s = cs[0].ops->consider(cs[0].obj, J, r);
        J[0] *= s; cs[0].ops->impulse(cs[0].obj, J, r); }
    smk_end(c);
    int hits = 0, fxhits = 0, maxp = 0, slosh_seen = 0;
    for (int fr = 0; fr < 300; fr++) {
        smk_begin(1.0f / 60, w);
        memset(cs, 0, sizeof cs);
        n = smk_bodies(cs, 32, 1);
        CarCollHit h[32];
        int nh = carcoll_run_all(cs, n, w, h, 32, NULL, NULL);
        if (nh > 32) nh = 32;
        int before = acid_live_particles();
        for (int i = 0; i < nh; i++) {                  /* main.c smk_hit_fx, prop vs polygon */
            int sa; float va[3];
            if (h[i].other >= 0 || !smk_body_contact(&cs[h[i].car], &sa, va)) continue;
            acid_contact(acid_contact_surface(sa, h[i].surface), h[i].point, h[i].impulse, h[i].slide, va, NULL);
            int pc = 0; smk_body_sound(&cs[h[i].car], &pc);
            carsfx_coll_prop(3, 1000 + smk_body_sound(&cs[h[i].car], NULL), -1, h[i].poly, h[i].point, h[i].impulse,
                             NULL, smk_body_sound(&cs[h[i].car], NULL), pc);
            hits++;
        }
        {   static int pr[64][3]; int np = carcoll_pairs(pr, 64);
            for (int i = 0; i < np; i++) { pr[i][0] = 1000 + serial; pr[i][1] = -1; }
            carsfx_coll_pairs(3, (const int (*)[3])pr, np); }
        smk_end(c);
        acid_tick(1.0f / 60);
        carsfx_update(1.0f / 60, &L, NULL, 0, NULL, 0);
        if (acid_live_particles() > before) fxhits++;
        if (acid_live_particles() > maxp) maxp = acid_live_particles();
        int on = 0;
        for (int k = 278; k < AUDIO_FXVOICES; k++) if (g_fxvoice[k].on) {
            on++;
            if (!slosh_seen) printf("f%3d slosh voice %d set %d clip %d pitch %.3f gain %.3f\n", fr, k,
                                    g_fxvoice[k].set, g_fxvoice[k].clip, g_fxvoice[k].pitch, g_fxvoice[k].gain);
        }
        if (on) slosh_seen = 1;
        if (fr % 30 == 0) printf("f%3d hits %d slosh voices on %d particles %d\n", fr, nh, on, acid_live_particles());
    }
    printf("prop-world hits %d, frames with new particles %d, max particles %d, slosh seen %d\n",
           hits, fxhits, maxp, slosh_seen);
    printf("claim after: %d (want 0, already sloshed)\n", smk_slosh_claim(serial));
    return 0;
}
