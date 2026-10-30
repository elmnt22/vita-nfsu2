/* The preset rides (GLOBALB 0x30220) through shop_preset_ride
   (RideInfo::FillWithPreset 0x637270): every preset's car, rims and the
   N2CarConfig fields it sets, and DDAY_PLAYER_CAR's checked.
   usage: t_preset [dataroot] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shop.h"
#include "perfparts.h"

static unsigned bhash(const char *s) { unsigned h = 0xffffffffu; while (*s) h = h * 33 + (unsigned char)*s++; return h; }
static unsigned u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* the preset names, straight from the chunk */
static int preset_names(const char *root, char names[64][33])
{
    char path[600]; snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", root);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    unsigned char h[8]; long o = 0; int n = 0;
    while (fseek(f, o, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8) {
        unsigned id = u32(h), sz = u32(h + 4);
        if (id == 0x30220) {
            unsigned char *b = malloc(sz);
            if (b && fread(b, 1, sz, f) == sz)
                for (unsigned i = 0; i + 0x338 <= sz && n < 64; i += 0x338, n++) { memcpy(names[n], b + i + 0x28, 32); names[n][32] = 0; }
            free(b); break;
        }
        o += 8 + (long)sz;
    }
    fclose(f);
    return n;
}

static void show(const char *name, const char *car, const N2CarConfig *c, const char *rim, int rs)
{
    static const char *const pl[] = { "front", "rear", "skirt", "hood", "headlight", "taillight", "engine",
                                      "audio", "spoiler", "exhaust", "scoop", "mirror", "wide" };
    printf("%-26s %-10s rim %s style %d inner %d\n", name, car, rim, rs & 0xff, (rs >> 8) & 0xff);
    printf("   ");
    if (c->body_kit) printf(" kit=%d", c->body_kit);
    for (int i = 0; i < N2_PART_COUNT; i++) if (c->parts[i]) printf(" %s=%d", pl[i], c->parts[i]);
    for (int i = 0; i < N2_PAINT_COUNT; i++) if (c->paint[i]) printf(" paint%d=%08x", i, c->paint[i]);
    for (int l = 0; l < 4; l++) {
        if (c->vinyl[l]) printf(" vinyl%d=%08x", l, c->vinyl[l]);
        for (int k = 0; k < 4; k++) if (c->vinyl_col[l][k]) printf(" vcol%d.%d=%08x", l, k, c->vinyl_col[l][k]);
    }
    for (int l = 0; l < 7; l++) {
        if (c->decal_layout[l]) printf(" dlay%d=%d", l, c->decal_layout[l]);
        for (int k = 0; k < 8; k++) if (c->decal[l][k]) printf(" decal%d.%d=%x", l, k, c->decal[l][k]);
    }
    if (c->tint) printf(" tint=%08x", c->tint);
    if (c->bulb) printf(" bulb=%06x", c->bulb);
    if (c->carbon) printf(" carbon=%x", c->carbon);
    for (int i = 0; i < 4; i++) if (c->neon[i][0] || c->neon[i][1]) printf(" neon%d=%08x/%08x", i, c->neon[i][0], c->neon[i][1]);
    if (c->hud) printf(" hud=%u", c->hud);
    for (int i = 0; i < 3; i++) if (c->hud_col[i]) printf(" hudcol%d=%06x", i, c->hud_col[i] & 0xffffff);
    if (c->purge) printf(" purge=%08x", c->purge);
    if (c->hydraulics) printf(" hydraulics=%u", c->hydraulics);
    if (c->doors) printf(" doors=%u", c->doors);
    for (int i = 0; i < 12; i++) if (c->audio_comp[i]) printf(" ice%d=%08x", i, c->audio_comp[i]);
    int np = 0; for (int i = 0; i < N2_PERF_PARTS; i++) np += c->perf[i] != 0;
    printf(" perf=%d parts:", np);
    for (int i = 0; i < N2_PERF_PARTS; i++) if (c->perf[i]) printf(" %d", i);
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "E:/NFS Underground 2 Vita/data/nfsug2";
    char names[64][33];
    int n = preset_names(root, names), bad = 0;
    printf("%d presets\n", n);
    if (n != 32) { printf("FAIL: expected 32 presets\n"); bad++; }
    for (int i = 0; i < n; i++) {
        char car[32], rim[16]; int rs = 0; N2CarConfig c;
        if (!shop_preset_ride(root, bhash(names[i]), car, &c, rim, &rs)) { printf("FAIL: %s not converted\n", names[i]); bad++; continue; }
        show(names[i], car, &c, rim, rs);
    }
    /* DDAY_PLAYER_CAR: 350Z, METAL_L2_COLOR10, ART FACTORY 6, LEX 42, the
       MANTIS wide body (KITW02), RACINGHART CP-FTUNE R, MEDIUM BLACK tint,
       performance level 2 */
    {
        char car[32], rim[16]; int rs = 0; N2CarConfig c;
        int ok = shop_preset_ride(root, 0xf7e19102u, car, &c, rim, &rs);
        int np = 0; for (int i = 0; i < N2_PERF_PARTS; i++) np += c.perf[i] != 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL: DDAY_PLAYER_CAR: %s\n", #x); bad++; } } while (0)
        CHECK(ok);
        CHECK(!strcmp(car, "350Z"));
        CHECK(c.paint[0] == 0x3c6ef96eu);            /* METAL_L2_COLOR10 */
        CHECK(c.vinyl[0] == 0x64b3f440u);            /* ART FACTORY 6 */
        CHECK(c.parts[N2_PART_SPOILER] > 1000);      /* LEX 42: the SPOILER library */
        CHECK(c.parts[N2_PART_WIDE] == 3);           /* 350Z_KITW02_BODY */
        CHECK(!strcmp(rim, "RACINGHART") && (rs & 0xff) > 0);
        CHECK(c.tint != 0);
        CHECK(np > 0);
        CHECK(c.decal[5][0] && c.decal[6][0]);       /* HKS USA / SPARCO on the quarters */
        CHECK(c.parts[N2_PART_HOOD] > 100 && (c.carbon & 1));   /* TWISTER: 350Z_STYLE07_HOOD_CF */
        /* InstallAllPerfParts(2, -1) = level 2 in every category */
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, c.perf, sizeof pp.fit);
        for (int cat = 0; cat < 9; cat++) printf("  DDAY perf category %d level %d\n", cat, perf_cat_level(&pp, cat));
    }
    if (shop_preset_ride(root, 0x12345678u, (char[32]){0}, &(N2CarConfig){0}, (char[16]){0}, &(int){0})) { printf("FAIL: unknown hash found\n"); bad++; }
    printf("%s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
