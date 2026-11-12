/* pcsave.c -- importing a save of the original PC game (see pcsave.h).
 *
 * The payload, in UserProfile::SaveToBuffer's order (PC 0x52a280 via
 * 0x52a450, UserProfile = FEDatabase 0x83a9d0 +0x10 = 0x83a9e0; GC
 * SaveToBuffer__11UserProfile 80131db4 / LoadFromBuffer 80131f28):
 *   FEPlayerCarDB (0x5166b0, 0x993e bytes):
 *     0x30 FEStockCar x 0x18 (FECarRecord {?, handle "STOCK_<type>", perf
 *       measure, ?, filter 1} + the car type index), a count;
 *     20 tuned FECarConfig x 0x430 (record [handle "TUNED_CAR_%d", filter 2],
 *       PresetCarSlot 0x418, a dword), 5 FECareerCar x 0x7f2 (0x516620:
 *       record [handle CAREER_SLOT_n, filter 4], PresetCarSlot, a dword,
 *       locked, has, OwnedPartInventory 0x3c0 via 0x503f00), a count (5);
 *     6 PC-only FECarConfig records x 0x434 (filter 0x10, + an index), a count (6)
 *   PlayerCareerState (0x514530 / 0x529230; 10981 bytes, the port's career
 *     block as is: career_save is byte-identical)
 *   the EA Trax playlist (UP +0x2d8: 27 x {int song, byte mode})
 *   AudioSettings (UP +0x48, 0x3c), GameplaySettings (UP +0x38, 0x10),
 *   PlayerSettings x 2 (UP +0x84, 0x28 each), VideoSettings (UP +0x28, 0x10),
 *   QuickRaceSettings (UP +0xd4, 0x17c), the career difficulty (UP +0x250),
 *   the style points (UP +0x254), autosave (byte UP +0xa0f9), save prompts
 *   (byte UP +0xa0f8), UP +0xa0f4, HighScoresDatabase (UP +0xa0fc: 320 x 8
 *   + 0x14 race statistics dwords), UP +0x258 (0x80), the name (32),
 *   OnlineSettings (0x515bf0, 0x33 bytes), the PC controller configs (0x43e,
 *   0x5b9420).
 * Fields with no home in the port are listed in GAPS.md ("2b PC save").
 */
#include "pcsave.h"
#include "options.h"
#include "shop.h"
#include "feng.h"
#include "garage_line.h"
#include "car_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define RMC_MAGIC 0x4d433032u
#define RMC_HDR 0x24
#define RETAIL_VERSION 0x10du
#define RETAIL_SIZE 0xd692
#define PORT_VERSION 0x4f55010du      /* profile.c PROFILE_VERSION */

#define CDB_STOCK   0x0                       /* 0x30 x 0x18 */
#define CDB_TUNED   0x484                     /* 20 x 0x430 */
#define CDB_CAREER  (CDB_TUNED + 20 * 0x430)  /* 5 x 0x7f2 */
#define CDB_PC      (CDB_CAREER + 5 * 0x7f2 + 4)   /* 6 x 0x434 */
#define P_CAREER    (CDB_PC + 6 * 0x434 + 4)
#define CAREER_BYTES 10981
#define P_PLAYLIST  (P_CAREER + CAREER_BYTES)
#define P_AUDIO     (P_PLAYLIST + 0xd8)
#define P_GAMEPLAY  (P_AUDIO + 0x3c)
#define P_PLAYER    (P_GAMEPLAY + 0x10)
#define P_VIDEO     (P_PLAYER + 0x50)
#define P_QR        (P_VIDEO + 0x10)
#define P_DIFF      (P_QR + 0x17c)
#define P_STYLE     (P_DIFF + 4)
#define P_AUTOSAVE  (P_STYLE + 4)
#define P_PROMPT    (P_AUTOSAVE + 1)
#define P_A0F4      (P_PROMPT + 1)
#define P_HS        (P_A0F4 + 4)
#define P_258       (P_HS + 0xa50)
#define P_NAME      (P_258 + 0x80)
#define P_ONLINE    (P_NAME + 0x20)
#define P_CTL       (P_ONLINE + 0x33)
#define P_END       (P_CTL + 0x43e)
typedef char pcsave_layout_check[P_END == RETAIL_SIZE ? 1 : -1];

#define REC_HANDLE 4      /* FECarRecord +4 */
#define SLOT_SIZE 0x418   /* PresetCarSlot */
#define HS_BYTES 0xa00    /* 320 x {short track, char kind, char direction, int value} */
#define OWN_MAX 1024      /* career.h CAREER_OWN_MAX */

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const unsigned char *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* 0x6fe173 (as profile.c's rmc_crc) */
static uint32_t rmc_crc(const unsigned char *p, long n) {
    static uint32_t t[256]; static int ok;
    if (!ok) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; k++) c = c & 0x80000000u ? (c << 1) ^ 0x04c11db7u : c << 1;
            t[i] = c;
        }
        ok = 1;
    }
    if (n < 4) return 0;
    uint32_t crc = ~((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]);
    for (long i = 4; i < n; i++) crc = ((crc << 8) | p[i]) ^ t[crc >> 24];
    return ~crc;
}

static unsigned char *read_all(const char *path, long *len) {
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = n > 0 && n < (1 << 20) ? (unsigned char *)malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = n;
    return b;
}

/* the container checks of the PC load (0x6fd702): header CRC, block A
   {0x10d, 0xd692} and its CRC; the payload CRC only reported */
static const unsigned char *container_payload(const unsigned char *f, long n, int *crc_ok) {
    if (n != RMC_HDR + RETAIL_SIZE || rd32(f) != RMC_MAGIC || rd32(f + 4) != (uint32_t)n ||
        rd32(f + 8) != 8 || rd32(f + 0xc) != RETAIL_SIZE || rd32(f + 0x18) != rmc_crc(f, 0x18) ||
        rd32(f + 0x10) != rmc_crc(f + 0x1c, 8) || rd32(f + 0x1c) != RETAIL_VERSION || rd32(f + 0x20) != RETAIL_SIZE)
        return NULL;
    if (crc_ok) *crc_ok = rd32(f + 0x14) == rmc_crc(f + RMC_HDR, RETAIL_SIZE);
    return f + RMC_HDR;
}
static int name_from(const unsigned char *p, char name[32]) {
    memcpy(name, p + P_NAME, 31); name[31] = 0;
    size_t l = strlen(name);
    if (!l || l > 7 || strchr(name, ' ') || strchr(name, '/') || strchr(name, '\\') || name[0] == '.') return 0;
    return 1;
}
int pcsave_peek(const char *savefile, char name[32]) {
    long n; unsigned char *f = read_all(savefile, &n);
    const unsigned char *p = f ? container_payload(f, n, NULL) : NULL;
    int ok = p && name_from(p, name);
    free(f);
    return ok;
}

typedef struct { unsigned char *p; long n, cap; } Buf;
static void put(Buf *b, const void *p, long n) {
    if (b->n + n > b->cap) {
        long c = b->cap ? b->cap : 4096;
        while (c < b->n + n) c *= 2;
        unsigned char *q = (unsigned char *)realloc(b->p, (size_t)c);
        if (!q) return;
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, p, (size_t)n); b->n += n;
}
static void put_u32(Buf *b, uint32_t v) { put(b, &v, 4); }
static void put_block_hdr(Buf *b, const char tag[4], long n) { put(b, tag, 4); put_u32(b, (uint32_t)n); }

/* the options (UI_Options rows) from the save's settings blocks; the port's
   own (the Vita bindings, the rear touchpad) keep their defaults */
static void options_from(N2Options *o, const unsigned char *p) {
    options_defaults(o);
    const unsigned char *au = p + P_AUDIO, *gp = p + P_GAMEPLAY, *ps = p + P_PLAYER, *pl = p + P_PLAYLIST;
    o->master = rdf(au + 0x00); o->speech = rdf(au + 0x04); o->fe_music = rdf(au + 0x08);
    o->ig_music = rdf(au + 0x0c); o->sfx = rdf(au + 0x10); o->engine = rdf(au + 0x14); o->tire = rdf(au + 0x18);
    o->stereo = (int)rd32(au + 0x28);            /* UP +0x70 */
    o->shuffle = rd32(au + 0x30) != 0;           /* UP +0x78 EA Trax play mode */
    o->mirror = gp[0]; o->kmh = gp[2]; o->race_map = gp[3]; o->explore_map = gp[4];
    o->jump_cam = rdf(gp + 8) > 0.0f;            /* UP +0x40: 0 / 127.5 */
    o->career_diff = (int)rd32(p + P_DIFF);
    o->save_on = p[P_PROMPT]; o->autosave = p[P_AUTOSAVE];
    for (int i = 0; i < 27; i++) { o->pl_song[i] = (unsigned char)rd32(pl + 8 * i); o->pl_mode[i] = pl[8 * i + 4]; }
    o->playlist_set = 1;
    OptPlayer *q = &o->p[0];                     /* player 0 (UP +0x84) */
    q->gauges = ps[0]; q->race_pos = ps[1]; q->lap_info = ps[2]; q->score = ps[3];
    q->feedback = ps[4]; q->leaderboard = ps[5]; q->ask_trans = ps[6];
    q->camera = (int)rd32(ps + 0x10); q->split = ps[0x14]; q->manual = ps[0x15]; q->stability_off = ps[0x16];
}

/* a record's PresetCarSlot by handle (QuickRaceSettings +4, the current
   car): tuned, career or PC-only; stock cars give their type only */
static const unsigned char *find_car(const unsigned char *p, const char *dataroot, uint32_t h, char car[32]) {
    car[0] = 0;
    if (!h) return NULL;
    for (int i = 0; i < 20; i++) {
        const unsigned char *r = p + CDB_TUNED + i * 0x430;
        if (rd32(r + REC_HANDLE) == h) return r + 0x14;
    }
    for (int i = 0; i < 5; i++) {
        const unsigned char *r = p + CDB_CAREER + i * 0x7f2;
        if (rd32(r + REC_HANDLE) == h) return r + 0x14;
    }
    for (int i = 0; i < 6; i++) {
        const unsigned char *r = p + CDB_PC + i * 0x434;
        if (rd32(r + REC_HANDLE) == h) return r + 0x14;
    }
    for (int i = 0; i < 0x30; i++) {
        const unsigned char *r = p + CDB_STOCK + i * 0x18;
        if (rd32(r + REC_HANDLE) == h) { shop_car_type_at(dataroot, (int)rd32(r + 0x14), car); return NULL; }
    }
    return NULL;
}

/* the garage (F2.4): retail's tuned slots as they are, "#<slot> <car line>"
   + " TUNE" and the slot's five tunings (PresetCarSlot +0x348, 0x29 each) --
   two of one model stay two cars */
static int garage_add(Buf *g, int gslot, const char *dataroot, const unsigned char *slot, char car[32]) {
    static char line[2048];
    N2CarConfig cfg; char rim[16]; int rs = 0;
    car[0] = 0;
    if (!slot || !rd32(slot)) return 0;
    if (!shop_slot_ride(dataroot, slot, car, &cfg, rim, &rs)) return 0;
    garage_format_line(line, sizeof line, car, &cfg, rim, rs);
    char hd[16]; snprintf(hd, sizeof hd, "#%d ", gslot);
    put(g, hd, (long)strlen(hd));
    put(g, line, (long)strlen(line));
    put(g, " TUNE", 5);
    for (int s = 0; s < 5; s++) {
        char hx[2 * 0x29 + 2]; int n = 0;
        hx[n++] = ' ';
        for (int k = 0; k < 0x29; k++) n += snprintf(hx + n, sizeof hx - n, "%02x", slot[0x348 + s * 0x29 + k]);
        put(g, hx, n);
    }
    put(g, "\n", 1);
    return 1;
}

/* the career cars, as career_cars_save writes them (CCAR version 6) */
static void career_cars(Buf *b, const unsigned char *p, const char *dataroot, PcSaveInfo *info) {
    static uint32_t own[OWN_MAX];
    static char line[2048];
    put_u32(b, 6); put_u32(b, 5);
    for (int k = 0; k < 5; k++) {
        const unsigned char *r = p + CDB_CAREER + k * 0x7f2, *slot = r + 0x14, *inv = r + 0x432;
        uint8_t locked = r[0x430], has = r[0x431];
        char car[32] = ""; N2CarConfig cfg; char rim[16] = ""; int rs = 0;
        uint32_t stock = 0;
        line[0] = 0;
        if (has && rd32(slot) && shop_slot_ride(dataroot, slot, car, &cfg, rim, &rs)) {
            char st[48]; snprintf(st, sizeof st, "STOCK_%s", car);
            stock = feng_hash(st);   /* FEStockCar's handle: career_stock_handle */
            garage_format_line(line, sizeof line, car, &cfg, rim, rs);
            info->ncareer++;
        } else if (has) {
            printf("pcsave: career slot %d: car not read, left empty\n", k);
            has = 0;
        }
        uint16_t len = (uint16_t)strlen(line);
        put_u32(b, rd32(r + REC_HANDLE)); put(b, &locked, 1); put(b, &has, 1); put_u32(b, stock);
        put(b, &len, 2); put(b, line, len);
        /* OwnedPartInventory: the entity words != 0 (AnyOwned), the perf bits
           (+0x15c), the spent cash (+0x294), the owned parts */
        uint8_t ent[12]; memset(ent, 0, sizeof ent);
        for (int e = 1; e < 0x57 && has; e++) if (rd32(inv + 4 * e)) ent[e >> 3] |= (uint8_t)(1u << (e & 7));
        put(b, ent, 12);
        if (has) put(b, inv + 0x15c, 16); else { uint8_t z[16] = {0}; put(b, z, 16); }
        put_u32(b, has ? rd32(inv + 0x294) : 0);
        int fn = 0, fo = 0;
        uint16_t nown = has ? (uint16_t)shop_inv_owned(dataroot, car, inv, slot, own, OWN_MAX, &fn, &fo) : 0;
        put(b, &nown, 2); put(b, own, 4L * nown);
        info->nown += nown; info->fit_n += fn; info->fit_owned += fo;
        float stars = 0.0f;   /* the game rates the car on load (SHOP_ACT_PROFILE) */
        put(b, &stars, 4);
        uint8_t legacy[12] = {0};
        put(b, legacy, 12);
        if (has) put(b, slot + 0x348, 5 * 0x29);   /* the tunings */
        else { uint8_t t[5 * 0x29]; memset(t, 100, sizeof t); put(b, t, sizeof t); }
        for (int i = 0; i < 0x44; i++) put_u32(b, has ? rd32(inv + 0x2a0 + 4 * i) : 0);   /* the perf brands */
        if (has) printf("pcsave: career slot %d %s: %d parts owned, fitted upgrades owned %d/%d, spent %u\n",
                        k, car, nown, fo, fn, (unsigned)rd32(inv + 0x294));
    }
}

static int write_container(const char *path, const unsigned char *pay, long pn) {
    long total = RMC_HDR + pn;
    unsigned char *f = (unsigned char *)malloc((size_t)total);
    if (!f) return 0;
    memset(f, 0, RMC_HDR);
    uint32_t v;
#define W32(o, x) do { v = (x); memcpy(f + (o), &v, 4); } while (0)
    W32(0x1c, PORT_VERSION); W32(0x20, (uint32_t)pn);
    memcpy(f + RMC_HDR, pay, (size_t)pn);
    W32(0x00, RMC_MAGIC); W32(0x04, (uint32_t)total); W32(0x08, 8); W32(0x0c, (uint32_t)pn);
    W32(0x10, rmc_crc(f + 0x1c, 8));
    W32(0x14, rmc_crc(f + RMC_HDR, pn));
    W32(0x18, rmc_crc(f, 0x18));
#undef W32
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *o = fopen(tmp, "wb");
    int ok = o && fwrite(f, 1, (size_t)total, o) == (size_t)total;
    if (o && fclose(o) != 0) ok = 0;
    free(f);
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    return rename(tmp, path) == 0;
}

int pcsave_import(const char *savefile, const char *dataroot, const char *profdir, PcSaveInfo *info) {
    PcSaveInfo dummy;
    if (!info) info = &dummy;
    memset(info, 0, sizeof *info);
    long n; unsigned char *f = read_all(savefile, &n);
    if (!f) { snprintf(info->msg, sizeof info->msg, "cannot read %s", savefile); return 0; }
    const unsigned char *p = container_payload(f, n, &info->crc_ok);
    if (!p) { snprintf(info->msg, sizeof info->msg, "not an NFSU2 PC save (20CM, version 0x10d, 0xd692 bytes)"); free(f); return 0; }
    if (!name_from(p, info->name)) { snprintf(info->msg, sizeof info->msg, "the save's profile name is not usable"); free(f); return 0; }
    if (!info->crc_ok) printf("pcsave: %s: the payload CRC does not match (the PC game would call it corrupt); importing anyway\n", info->name);
    if (rd32(p + CDB_TUNED - 4) > 0x30 || rd32(p + CDB_PC - 4) != 5 || rd32(p + P_CAREER - 4) > 6)
        printf("pcsave: unexpected FEPlayerCarDB counts %u %u %u\n", (unsigned)rd32(p + CDB_TUNED - 4),
               (unsigned)rd32(p + CDB_PC - 4), (unsigned)rd32(p + P_CAREER - 4));
    Buf b = {0};
    char nm[32]; memset(nm, 0, sizeof nm); snprintf(nm, sizeof nm, "%s", info->name);
    put_block_hdr(&b, "NAME", sizeof nm); put(&b, nm, sizeof nm);
    {   N2Options o; options_from(&o, p);
        put_block_hdr(&b, "OPTN", 4 + (long)sizeof o); put_u32(&b, (uint32_t)sizeof o); put(&b, &o, sizeof o); }
    {   Buf g = {0};
        char cur[32] = "", tc[32];
        uint32_t curh = rd32(p + P_QR + 4);
        const unsigned char *cs = find_car(p, dataroot, curh, cur);
        int curslot = -1, used[20] = {0};
        for (int i = 0; i < 20; i++) {
            const unsigned char *r = p + CDB_TUNED + i * 0x430;
            if (rd32(r + REC_HANDLE) && garage_add(&g, i, dataroot, r + 0x14, tc)) {
                info->ntuned++; used[i] = 1;
                if (rd32(r + REC_HANDLE) == curh) { curslot = i; snprintf(cur, sizeof cur, "%s", tc); }
            }
        }
        /* the current car a career / PC-only record: its build into the first free
           tuned slot (the front end's current car is a garage car in the port) */
        if (cs && rd32(cs) && curslot < 0)
            for (int i = 0; i < 20 && curslot < 0; i++)
                if (!used[i] && garage_add(&g, i, dataroot, cs, tc)) { curslot = i; snprintf(cur, sizeof cur, "%s", tc); }
        if (cur[0]) {
            char l[64];
            if (curslot >= 0) snprintf(l, sizeof l, "CURRENT %s#%d\n", cur, curslot);
            else snprintf(l, sizeof l, "CURRENT %s\n", cur);
            put(&g, l, (long)strlen(l));
            snprintf(info->current, sizeof info->current, "%s", cur);
        }
        put_block_hdr(&b, "GRGE", g.n); if (g.n) put(&b, g.p, g.n);
        free(g.p); }
    put_block_hdr(&b, "HSCR", HS_BYTES); put(&b, p + P_HS, HS_BYTES);
    for (int i = 0; i < 320; i++) if (p[P_HS + 8 * i] | p[P_HS + 8 * i + 1]) info->nhs++;
    {   Buf c = {0};
        career_cars(&c, p, dataroot, info);
        put_block_hdr(&b, "CCAR", c.n); put(&b, c.p, c.n);
        free(c.p); }
    put_block_hdr(&b, "CARR", CAREER_BYTES); put(&b, p + P_CAREER, CAREER_BYTES);
    char dir[600], path[600];
    snprintf(dir, sizeof dir, "%s/%s", profdir, info->name);
    mkdir(profdir, 0777); mkdir(dir, 0777);
    snprintf(path, sizeof path, "%s/%s", dir, info->name);
    int ok = b.p && write_container(path, b.p, b.n);
    free(b.p); free(f);
    snprintf(info->msg, sizeof info->msg, ok ? "%s: %d tuned cars, %d career cars, %d high scores%s"
                                             : "%s: cannot write the profile",
             info->name, info->ntuned, info->ncareer, info->nhs, info->crc_ok ? "" : " (save CRC mismatch)");
    printf("pcsave: %s\n", info->msg);
    return ok;
}

int pcsave_find_new(const char *profdir, char *path, int cap, char name[32]) {
    DIR *d = opendir(PCSAVE_DIR);
    struct dirent *e;
    int found = 0;
    while (d && !found && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[600], q[600];
        snprintf(p, sizeof p, "%s/%s/%s", PCSAVE_DIR, e->d_name, e->d_name);   /* the PC's folder copied */
        if (!pcsave_peek(p, name)) {
            snprintf(p, sizeof p, "%s/%s", PCSAVE_DIR, e->d_name);            /* or the file alone */
            if (!pcsave_peek(p, name)) continue;
        }
        snprintf(q, sizeof q, "%s/%s/%s", profdir, name, name);
        FILE *f = fopen(q, "rb");
        if (f) { fclose(f); continue; }
        snprintf(path, (size_t)cap, "%s", p);
        found = 1;
    }
    if (d) closedir(d);
    return found;
}
