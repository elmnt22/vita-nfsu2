/* Career lock walls / free-roam scenery groups per mode (world_scenery.h). */
#include <stdio.h>
#include <stdlib.h>
#include "world_scenery.h"
int g_wg_career_stage = -1, g_wg_career_owns_car;
static int count(const WGTable *t, int ev, const char *grp, int *vis) {
    WGSelection s; if (!wg_selection_open(t, ev, &s)) return -1;
    int n = 0; *vis = 0;
    for (size_t p = 0; p < t->group_bytes;) {
        const unsigned char *g = t->groups + p; uint32_t refs = wg_u32(g + 48);
        if (!strcmp((const char *)g + 8, grp))
            for (uint32_t j = 0; j < refs; j++) {
                const unsigned char *r = t->overrides + 8 * wg_u16(g + 52 + 2 * j);
                n++; *vis += wg_selection_visible(&s, wg_u16(r), wg_u16(r + 2));
            }
        p += (size_t)((52ULL + 2ULL * refs + 3) & ~3ULL);
    }
    free(s.items); return n;
}
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(len); fread(d, 1, len, f); fclose(f);
    WGTable t; if (!wg_open_file(d, len, &t)) { printf("no group table\n"); return 1; }
    static const char *grp[] = { "BARRIERS_CAREER0", "BARRIERS_CAREER1", "BARRIERS_CAREER4", "BARRIERS_DEMO",
                                 "FREE_ROAM", "FREE_ROAM_STAGE0", "FREE_ROAM_STAGE0A" };
    struct { int stage, car, ev; const char *what; } m[] = {
        { -1, 0, -1, "debug roam" }, { 0, 0, -1, "career s0 no car" }, { 0, 1, -1, "career s0 car" },
        { 1, 1, -1, "career s1" }, { 4, 1, -1, "career s4" }, { 1, 1, 0, "unfiltered" } };
    for (int i = 0; i < 6; i++) {
        g_wg_career_stage = m[i].stage; g_wg_career_owns_car = m[i].car;
        printf("%-18s", m[i].what);
        for (int k = 0; k < 7; k++) { int v, n = count(&t, m[i].ev, grp[k], &v); printf(" %s %d/%d", grp[k] + (k < 4 ? 9 : 0), v, n); }
        printf("\n");
    }
    return 0;
}
