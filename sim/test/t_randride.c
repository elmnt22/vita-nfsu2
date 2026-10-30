/* shop_random_ride (RideInfo::BuildRandomRide 0x6374b0 + BuildRandomSkin
   0x632ec0): 3 seeds x a few cars at all-paths levels 0 and 3, the
   N2CarConfig fields shown with the part names from GLOBALB's database.
   usage: t_randride [dataroot] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shop.h"

static unsigned u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned char *g_db; static long g_str = -1, g_parts = -1, g_plen;

static void load_names(const char *root)
{
    char path[600]; snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", root);
    FILE *f = fopen(path, "rb"); if (!f) return;
    unsigned char h[8]; long o = 0;
    while (fseek(f, o, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8) {
        unsigned id = u32(h), sz = u32(h + 4);
        if (id == 0x80034602u) {
            g_db = malloc(sz); fread(g_db, 1, sz, f);
            for (long q = 0; q + 8 <= (long)sz; ) {
                unsigned cid = u32(g_db + q), cs = u32(g_db + q + 4);
                if (cid == 0x34606) g_str = q + 8;
                if (cid == 0x34604) { g_parts = q + 8; g_plen = cs; }
                q += 8 + cs;
            }
            break;
        }
        o += 8 + (long)sz;
    }
    fclose(f);
}
static const char *nm(unsigned hash)
{
    static char buf[8][64]; static int k;
    if (!hash) return "-";
    for (long q = g_parts; g_db && q + 14 <= g_parts + g_plen; q += 14)
        if (u32(g_db + q) == hash) return (const char *)g_db + g_str + (g_db[q + 8] | g_db[q + 9] << 8) * 4L;
    k = (k + 1) & 7; snprintf(buf[k], 64, "%08x", hash); return buf[k];
}

static void show(const N2CarConfig *c, const char *rim, int rs)
{
    static const char *const pl[] = { "front", "rear", "skirt", "hood", "headlight", "taillight", "engine",
                                      "audio", "spoiler", "exhaust", "scoop", "mirror", "wide" };
    printf("    rim %s style %d inner %d |", rim, rs & 0xff, (rs >> 8) & 0xff);
    if (c->body_kit) printf(" kit=%d", c->body_kit);
    for (int i = 0; i < N2_PART_COUNT; i++) if (c->parts[i]) printf(" %s=%d", pl[i], c->parts[i]);
    if (c->carbon) printf(" carbon=%x", c->carbon);
    if (c->tint) printf(" tint=%08x", c->tint);
    if (c->bulb) printf(" bulb=%06x", c->bulb);
    if (c->neon[0][0] || c->neon[0][1]) printf(" neon=%08x/%08x", c->neon[0][0], c->neon[0][1]);
    printf("\n    paint:");
    for (int i = 0; i < N2_PAINT_COUNT; i++) if (c->paint[i]) printf(" [%d]%s", i, nm(c->paint[i]));
    printf("\n");
    for (int l = 0; l < 4; l++) {
        int any = c->vinyl[l] != 0;
        if (!any) continue;
        printf("    vinyl%d %s cols", l, nm(c->vinyl[l]));
        for (int k = 0; k < 4; k++) printf(" %s", nm(c->vinyl_col[l][k]));
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "E:/NFS Underground 2 Vita/data/nfsug2";
    static const char *const cars[] = { "SKYLINE", "FOCUS", "HUMMER" };
    int bad = 0;
    load_names(root);
    if (!shop_part_db(root)) { printf("FAIL: no part database\n"); return 1; }
    for (int ci = 0; ci < 3; ci++)
        for (int lvl = 0; lvl <= 3; lvl += 3)
            for (unsigned seed = 1; seed <= 3; seed++) {
                int perf[10]; for (int i = 0; i < 10; i++) perf[i] = lvl;
                uint32_t rng = seed * 0x9e3779b9u;
                N2CarConfig c; char rim[16] = ""; int rs = 0;
                if (!shop_random_ride(NULL, cars[ci], perf, &rng, &c, rim, sizeof rim, &rs)) { printf("FAIL: %s\n", cars[ci]); bad++; continue; }
                printf("%s level %d seed %u (rng after %08x)\n", cars[ci], lvl, seed, rng);
                show(&c, rim, rs);
            }
    printf(bad ? "FAILED\n" : "ok\n");
    return bad != 0;
}
