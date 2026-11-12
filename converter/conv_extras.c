/* conv_extras.c -- OpenUG2 PC converter: the caches the Vita otherwise
 * writes the first time it needs them, built with the game's own code
 * (compiled like the world sources, so the files are the console's):
 *
 *  - <track>.BUN.compact.strip  the 1 MB bundle world tiles keep (resource.c
 *    pin_strip); without it the first load reads the 57 MB compact copy
 *  - <stem>.focus               the track's authored start (main.c
 *    track_focus_cached), the one reader of the scenery the strip drops
 *  - mods_<CAR>.bin             each player car's parts lists (main.c
 *    mod_catalog_cached, ~330 ms a car on the Vita); the car is read stock,
 *    as the car lot and Quick Race first show it
 *  - shop_vNN.bin               the shop screens and their art (shop.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nfsu2.h"
#include "car_mod.h"
#include "resource.h"
#include "world_instance.h"
#include "shop.h"

#define MOD_CACHE_MAGIC "OUG2MC01"   /* main.c */

/* main.c k_player_cars */
static const char *const k_cars[] = {
    "240SX", "3000GT", "350Z", "A3", "CELICA", "CIVIC", "COROLLA", "ECLIPSE",
    "ESCALADE", "FOCUS", "G35", "GOLF", "GTO", "HUMMER", "IMPREZA", "IMPREZAWRX",
    "IS300", "LANCER", "LANCEREVO8", "MIATA", "MUSTANGGT", "NAVIGATOR", "NEON",
    "PEUGOT", "RSX", "RX7", "RX8", "S2000", "SENTRA", "SKYLINE", "SUPRA",
    "TIBURON", "TT" };
#define NCARS ((int)(sizeof k_cars / sizeof *k_cars))
int conv_car_count(void) { return NCARS; }
const char *conv_car_name(int i) { return i >= 0 && i < NCARS ? k_cars[i] : NULL; }

static int mods_cached(const char *fp, long clen) {
    FILE *f = fopen(fp, "rb");
    if (!f) return 0;
    char m[8]; long key = 0;
    int ok = fread(m, 8, 1, f) == 1 && !memcmp(m, MOD_CACHE_MAGIC, 8) &&
             fread(&key, sizeof key, 1, f) == 1 && key == clen;
    fclose(f);
    return ok;
}

/* 1 written, already current or no such car here; 0 a read or write failed */
int conv_car_mods(const char *dataroot, int i) {
    const char *car = conv_car_name(i);
    if (!car) return 0;
    char path[1024], fp[1024];
    snprintf(path, sizeof path, "%s/CARS/%s/GEOMETRY.BIN", dataroot, car);
    snprintf(fp, sizeof fp, "ux0:data/NFSU2/cache/mods_%s.bin", car);
    struct stat st;
    if (stat(path, &st) != 0) return 1;   /* a car this install has no model for (vinyls only) */
    if (mods_cached(fp, (long)st.st_size)) return 1;
    long clen = 0;
    unsigned char *cdata = n2_read_file(path, &clen);
    if (!cdata) return 0;
    static N2PartMenu menus[N2_PART_COUNT];
    N2Scene scene = {0};
    N2CarConfig stock = {0};
    int ok = n2_load_car(cdata, clen, &scene, NULL, 0, &stock) > 0;
    if (ok) n2_mod_catalog(dataroot, cdata, clen, &scene, menus);
    n2_free_scene(&scene);
    free(cdata);
    if (!ok) return 0;
    mkdir("ux0:data/NFSU2/cache", 0777);
    char tmp[1040]; snprintf(tmp, sizeof tmp, "%s.tmp", fp);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    ok = fwrite(MOD_CACHE_MAGIC, 8, 1, f) == 1 && fwrite(&clen, sizeof clen, 1, f) == 1;
    for (int p = 0; ok && p < N2_PART_COUNT; p++)
        ok = fwrite(&menus[p].count, sizeof menus[p].count, 1, f) == 1 &&
             (!menus[p].count || fwrite(menus[p].options, sizeof menus[p].options[0],
                                        (size_t)menus[p].count, f) == (size_t)menus[p].count);
    if (fclose(f) != 0) ok = 0;
    if (ok) { remove(fp); ok = rename(tmp, fp) == 0; }
    if (!ok) remove(tmp);
    printf("converter: %s parts list %s\n", car, ok ? "written" : "FAILED");
    return ok;
}

/* ---- tracks ---------------------------------------------------------------- */
static long file_len(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}
static int file_has(const char *path) { return file_len(path) > 0; }

/* bun: STREAML4RA; troot: ux0:data/NFSU2/TRACKS */
static void strip_path(char *out, size_t cap, const char *bun) {
    snprintf(out, cap, "ux0:data/NFSU2/cache/%s.BUN.compact.strip", bun);
}
static void focus_path(char *out, size_t cap, const char *bun) {
    snprintf(out, cap, "ux0:data/NFSU2/cache/%s.focus", bun + 6);
}
int conv_track_extras_done(const char *bun) {
    char a[256], b[256];
    strip_path(a, sizeof a, bun); focus_path(b, sizeof b, bun);
    return file_has(a) && file_has(b);
}
/* the track's compact bundle is in the cache already (the conversion made it) */
int conv_track_extras(const char *troot, const char *bun) {
    char cp[1024], sp[1024], fp[256];
    snprintf(cp, sizeof cp, "%s/%s.BUN", troot, bun + 6);
    snprintf(sp, sizeof sp, "%s/%s.BUN", troot, bun);
    focus_path(fp, sizeof fp, bun);
    res_unpin();
    long slen = 0, clen = 0;
    unsigned char *strm = res_bundle(sp, &slen);   /* pinned, the full compact copy */
    if (!strm) { printf("converter: %s: bundle unreadable\n", bun); return 0; }
    /* main.c track_focus_cached: keyed by both files' lengths */
    long key[2] = { file_len(cp), file_len(sp) };
    float xy[2];
    unsigned char *comp = n2_read_file(cp, &clen);
    int ok = comp && winst_default_focus(comp, clen, strm, slen, xy) && key[0] > 0 && key[1] > 0;
    free(comp);
    if (ok) {
        FILE *f = fopen(fp, "wb");
        ok = f && fwrite(key, sizeof key, 1, f) == 1 && fwrite(xy, sizeof(float) * 2, 1, f) == 1;
        if (f && fclose(f) != 0) ok = 0;
        if (!ok) remove(fp);
    }
    printf("converter: %s start %s (%.3f, %.3f)\n", bun, ok ? "written" : "FAILED", ok ? xy[0] : 0, ok ? xy[1] : 0);
    res_pin_strip();   /* writes <bundle>.compact.strip */
    res_unpin();
    char st[256]; strip_path(st, sizeof st, bun);
    return ok && file_has(st);
}

/* ---- shop ------------------------------------------------------------------ */
const char *conv_shop_file(void) {   /* its name in the cache folder */
    const char *p = strrchr(shop_cache_file(), '/');
    return p ? p + 1 : shop_cache_file();
}
int conv_shop_done(void) { return file_has(shop_cache_file()); }
int conv_shop(void) {
    int ok = conv_shop_done() || shop_build_cache("ux0:data/NFSU2");
    printf("converter: shop screens %s\n", ok ? "ready" : "FAILED");
    return ok;
}
