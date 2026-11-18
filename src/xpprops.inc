/* xpprops.inc -- an expansion city's smackable props -> the game's smokeables
   (smokeable.c), included by xproads.c. Shared by the Expansion Prep Tool and the
   on-device setup (xpcity.c).

   MW (raw STREAML2RA.BUN): 366 top-level 0x34027 packs, after a 0x11 fill a 16-byte
   header (+8 s16 section, +10 first id, +12 count) and 64-byte records:
     +0 quaternion, +0x10 position (physics axes, world = (z, -x, y)), +0x20 model hash,
     +0x28 VLT key of the "smackable" collection (attributes.bin: stump, cone, ...),
     +0x2c scenery placement index (L2RA 0x34108, 6 B {section, row, flags}).
   The placement's scenery instance (its section's 0x34103 row) is what the prop
   replaces: its world box, packed rotation and position are used as they are drawn,
   its info's first solid (0x34102 +0x18) gives the model box (0x134011 +0x20 / +0x30).

   UG2 has its own per-kind physics (SmokeableInfo 0x100 B); MW's rigid-body classes
   (attributes.bin) do not fit it, so every MW kind is given the nearest UG2 prop's
   values (k_kinds, from SMOKEABLE.md's used infos) and a box hull of its model. One
   info per (kind, model). Big structures (roofs, towers, booths, boats, parked cars)
   stay scenery, as UG2 has no prop like them.

   Output (one bundle, read by smk_load in place of the companion + stream):
     0x80034020 hulls (0x39200 header, name at +0x1c / 0x39201 vertices, 48 B, +0x10),
     0x34026 infos, 0x34107 placements (one per spawner), 0x34027 packs of 0x70-byte
     spawners, one pack per section. Every chunk's data is 16-aligned. */

typedef struct {
    const char *kind;       /* the MW smackable collection */
    const char *cat;        /* UG2 SmokeableCategoryNameTable entry (the sound) */
    float mass, anchor, friction;   /* tonnes, t m/s, - */
    const char *terrain;    /* acid surface name */
    uint32_t motion_fx;     /* UG2 emitter group (0: none) */
} XpKind;
#define FX_WATER 0x95c9f5fau   /* BARREL_WATERA */
#define FX_GLASS 0xe84348eau   /* GLASSWALLA */
static const XpKind k_kinds[] = {
    { "mediummetalpost",   "ROAD_SIGN_METAL", 0.005f, 0.2f, 0.5f, "METAL", 0 },
    { "trafficlight",      "METAL_BIG",       0.15f,  0.6f, 0.5f, "METAL", 0 },
    { "largemetalpost",    "METAL_BIG",       0.1f,   0.5f, 0.5f, "METAL", 0 },
    { "parking_mtr",       "PARKING_METER",   0.02f,  0.2f, 0.5f, "METAL", 0 },
    { "bbnet",             "PARKING_METER",   0.05f,  0.3f, 0.5f, "METAL", 0 },
    { "firehydrant",       "METAL_SMALL",     0.1f,   0.3f, 0.5f, "METAL", 0 },
    { "payphone",          "METAL_SMALL",     0.1f,   0.3f, 0.5f, "METAL", 0 },
    { "tix_mach",          "METAL_SMALL",     0.1f,   0.2f, 0.5f, "METAL", 0 },
    { "trafficcablelight", "METAL_SMALL",     0.02f,  0.05f, 0.5f, "METAL", 0 },
    { "news_box",          "METAL_SMALL",     0.06f,  0,    0.4f, "METAL", 0 },
    { "rack",              "METAL_SMALL",     0.03f,  0,    0.4f, "METAL", 0 },
    { "bicycle",           "METAL_SMALL",     0.015f, 0,    0.4f, "METAL", 0 },
    { "mediummetalobject", "METAL_SMALL",     0.1f,   0,    0.4f, "METAL", 0 },
    { "lightmetalobject",  "METAL_SMALL",     0.02f,  0,    0.4f, "METAL", 0 },
    { "ice_box",           "METAL_SMALL",     0.05f,  0,    0.4f, "METAL", 0 },
    { "mtl_box",           "METAL_SMALL",     0.03f,  0,    0.4f, "METAL", 0 },
    { "lwn_chr",           "CHAIR",           0.01f,  0,    0.4f, "METAL", 0 },
    { "oil_drum",          "OIL_DRUM",        0.2f,   0,    0.3f, "METAL", 0 },
    { "garbg_can",         "TRASHCAN",        0.2f,   0,    0.2f, "METAL", 0 },
    { "recyc_bin",         "TRASHCAN",        0.1f,   0,    0.2f, "METAL", 0 },
    { "dumpster",          "METAL_BIG",       1.0f,   0,    0.3f, "METAL", 0 },
    { "pop_mach",          "METAL_BIG",       0.3f,   0,    0.3f, "METAL", 0 },
    { "vend_mach",         "METAL_BIG",       0.3f,   0,    0.3f, "METAL", 0 },
    { "golfcart",          "METAL_BIG",       0.4f,   0,    0.3f, "METAL", 0 },
    { "chlink_fnc",        "METAL_FENCE",     0.2f,   0.1f, 0.4f, "METAL", 0 },
    { "rake",              "METAL_FENCE",     0.02f,  0,    0.4f, "METAL", 0 },
    { "metal_gate",        "METAL_FENCE",     0.1f,   0.2f, 0.4f, "METAL", 0 },
    { "crate",             "WOODBOX",         0.05f,  0,    0.4f, "WOOD", 0 },
    { "cardboard_box",     "WOODBOX",         0.005f, 0,    0.4f, "WOOD", 0 },
    { "surfboard",         "WOODBOX",         0.005f, 0,    0.4f, "WOOD", 0 },
    { "lightwoodenobject", "WOODBOX",         0.02f,  0,    0.4f, "WOOD", 0 },
    { "mediumwoodenobject","WOODBOX",         0.05f,  0,    0.4f, "WOOD", 0 },
    { "stump",             "ROAD_SIGN_WOOD",  0.02f,  0.1f, 0.5f, "WOOD", 0 },
    { "shrub",             "NONE",            0.01f,  0.03f, 0.6f, "WOOD", 0 },
    { "parkingarmpost",    "ROAD_SIGN_WOOD",  0.01f,  0.1f, 0.5f, "WOOD", 0 },
    { "lightwoodenpost",   "ROAD_SIGN_WOOD",  0.01f,  0.1f, 0.5f, "WOOD", 0 },
    { "mediumwoodenpost",  "ROAD_SIGN_WOOD",  0.02f,  0.15f, 0.5f, "WOOD", 0 },
    { "wood_slat_fence",   "ROAD_SIGN_WOOD",  0.02f,  0.05f, 0.5f, "WOOD", 0 },
    { "picket_fence",      "ROAD_SIGN_WOOD",  0.02f,  0.05f, 0.5f, "WOOD", 0 },
    { "panel",             "ROAD_SIGN_WOOD",  0.01f,  0,    0.5f, "WOOD", 0 },
    { "golf_flag",         "ROAD_SIGN_WOOD",  0.002f, 0.02f, 0.5f, "WOOD", 0 },
    { "woodgate",          "ROAD_SIGN_WOOD",  0.05f,  0.2f, 0.5f, "WOOD", 0 },
    { "bench",             "CHAIR",           0.05f,  0,    0.4f, "WOOD", 0 },
    { "chair",             "CHAIR",           0.01f,  0,    0.4f, "WOOD", 0 },
    { "lounge_chr",        "CHAIR",           0.01f,  0,    0.4f, "WOOD", 0 },
    { "umb_table",         "MARKET_STALL",    0.03f,  0,    0.4f, "WOOD", 0 },
    { "table",             "MARKET_STALL",    0.03f,  0,    0.4f, "WOOD", 0 },
    { "pic_table",         "MARKET_STALL",    0.05f,  0,    0.4f, "WOOD", 0 },
    { "kiosk",             "MARKET_STALL",    0.2f,   0,    0.4f, "WOOD", 0 },
    { "sawhorse",          "SAW_HORSE",       0.06f,  0,    0.4f, "WOOD", 0 },
    { "roadblock",         "SAW_HORSE",       0.05f,  0,    0.4f, "WOOD", 0 },
    { "garbg_bag",         "GARBAGE_BAG",     0.01f,  0,    0.6f, "PLASTIC", 0 },
    { "bumper",            "TRAFFIC_CONE",    0.2f,   0,    0.6f, "PLASTIC", 0 },
    { "pylon",             "TRAFFIC_CONE",    0.1f,   0,    0.6f, "PLASTIC", 0 },
    { "cone",              "TRAFFIC_CONE",    0.005f, 0,    0.6f, "PLASTIC", 0 },
    { "crsh_barrel",       "WATER_DRUM",      0.9f,   0,    0.1f, "PLASTIC", FX_WATER },
    { "crsh_barrel_frag",  "WATER_DRUM",      0.1f,   0,    0.3f, "PLASTIC", 0 },
    { "potty",             "TRASHCAN",        0.1f,   0,    0.3f, "PLASTIC", 0 },
    { "stand",             "SANDWICH_BOARD",  0.01f,  0,    0.4f, "PLASTIC", 0 },
    { "tire",              "NONE",            0.01f,  0,    0.6f, "RUBBER", 0 },
    { "glass",             "GLASS",           0.05f,  0.1f, 0.4f, "GLASS", FX_GLASS },
};
#define XP_PROP_MAXDIM 12.0f   /* a model longer than this stays scenery */
#define XP_TRUNK 0.3f          /* the half width of a tall model's pole / trunk hull */

typedef struct { uint32_t key; float lo[3], hi[3]; } SolidBox;
typedef struct {
    int kind; uint16_t sec, row;          /* from the MW record + its placement */
    uint32_t solid; int info;
    float mn[3], mx[3], M[16];
    int ok;
} XpSp;
static int sb_cmp(const void *a, const void *b) { uint32_t x = ((const SolidBox *)a)->key, y = ((const SolidBox *)b)->key; return x < y ? -1 : x > y; }
static int sp_sec_cmp(const void *a, const void *b)
{
    const XpSp *x = a, *y = b;
    return x->sec != y->sec ? (x->sec < y->sec ? -1 : 1) : x->row != y->row ? (x->row < y->row ? -1 : 1) : 0;
}
static size_t fill_skip(const uint8_t *p, size_t n) { size_t i = 0; while (i < n && p[i] == 0x11) i++; return i & ~(size_t)3; }

/* the solids' model boxes (0x134011 headers) of one 0x80134000 chunk */
static int solid_boxes(const uint8_t *b, size_t n, SolidBox **sb, size_t *nsb, size_t *cap)
{
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = u32le(b + q), sz = u32le(b + q + 4);
        if (q + 8 + sz > n) break;
        if (id & 0x80000000u) { if (solid_boxes(b + q + 8, sz, sb, nsb, cap)) return -1; }
        else if (id == 0x134011u) {
            const uint8_t *c = b + q + 8; size_t f = fill_skip(c, sz);
            if (sz - f >= 0x40) {
                if (*nsb == *cap) {
                    size_t nc = *cap ? *cap * 2 : 8192;
                    SolidBox *ns = realloc(*sb, nc * sizeof *ns); if (!ns) return -1;
                    *sb = ns; *cap = nc;
                }
                SolidBox *s = &(*sb)[(*nsb)++];
                s->key = u32le(c + f + 0x10);
                for (int k = 0; k < 3; k++) { s->lo[k] = f32le(c + f + 0x20 + 4 * k); s->hi[k] = f32le(c + f + 0x30 + 4 * k); }
            }
        }
        q += 8 + sz;
    }
    return 0;
}
/* one section (0x80034100): fill its spawners (sorted by section, row) */
static void sec_fill(const uint8_t *b, size_t n, XpSp *sp, int nsp)
{
    const uint8_t *sec = NULL, *inf = NULL, *ins = NULL; size_t ninf = 0, nins = 0;
    for (size_t q = 0; q + 8 <= n; ) {
        uint32_t id = u32le(b + q), sz = u32le(b + q + 4); const uint8_t *c = b + q + 8;
        if (q + 8 + sz > n) return;
        size_t f = fill_skip(c, sz);
        if (id == 0x34101u && sz >= 0x10) sec = c;
        else if (id == 0x34102u) { inf = c + f; ninf = (sz - f) / 0x48; }
        else if (id == 0x34103u) { ins = c + f; nins = (sz - f) / 0x40; }
        q += 8 + sz;
    }
    if (!sec || !ins) return;
    unsigned s = u32le(sec + 0x0c);
    int lo = 0, hi = nsp;                                   /* the first spawner of section s */
    while (lo < hi) { int m = (lo + hi) / 2; if (sp[m].sec < s) lo = m + 1; else hi = m; }
    for (int i = lo; i < nsp && sp[i].sec == s; i++) {
        XpSp *S = &sp[i];
        if (S->row >= nins) continue;
        const uint8_t *r = ins + (size_t)S->row * 0x40;
        for (int k = 0; k < 3; k++) { S->mn[k] = f32le(r + 4 * k); S->mx[k] = f32le(r + 12 + 4 * k); }
        memset(S->M, 0, sizeof S->M);
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) S->M[4 * j + k] = (float)(int16_t)(r[0x2c + 2 * (3 * j + k)] | r[0x2d + 2 * (3 * j + k)] << 8) / 8192.0f;
        for (int k = 0; k < 3; k++) S->M[12 + k] = f32le(r + 0x20 + 4 * k);
        S->M[15] = 1;
        unsigned ii = (unsigned)(r[62] | r[63] << 8);
        if (inf && ii < ninf) { S->solid = u32le(inf + (size_t)ii * 0x48 + 0x18); S->ok = 1; }
    }
}

typedef struct { int kind; uint32_t solid; float lo[3], hi[3]; } XpInfo;

/* chunk writing (xpevents.inc's Buf, zero-filled): every chunk's data 16-aligned in
   the file (an id-0 pad chunk in front) */
static int g_pb_oom;
static void pb_put(Buf *o, const void *d, size_t k) { uint8_t *p = buf_grow(o, k); if (!p) { g_pb_oom = 1; return; } if (d) memcpy(p, d, k); }
static void pb_u32(Buf *o, uint32_t v) { uint8_t b[4]; pu32(b, v); pb_put(o, b, 4); }
static size_t pb_open(Buf *o, uint32_t id) { pb_u32(o, id); size_t at = o->n; pb_u32(o, 0); return at; }
static void pb_close(Buf *o, size_t at) { if (!g_pb_oom) pu32(o->b + at, (uint32_t)(o->n - at - 4)); }
static void ob_align_hdr(Buf *o)
{
    if (((o->n + 8) & 15) == 0) return;
    size_t k = (16 - ((o->n + 16) & 15)) & 15;              /* a pad chunk of 8 + k bytes */
    pb_u32(o, 0); pb_u32(o, (uint32_t)k); pb_put(o, NULL, k);
}

int xp_props(const char *raw_companion, const char *raw_stream, const char *out_path, int game, XpPropStats *st)
{
    XpPropStats z; memset(&z, 0, sizeof z); if (!st) st = &z; memset(st, 0, sizeof *st);
    if (game != XP_GAME_MW) return XP_ERR_FORMAT;
    size_t pln = 0; uint8_t *pl = read_top(raw_companion, 0x34108u, &pln);
    if (!pl) return XP_ERR_OPEN_IN;
    uint32_t np = (uint32_t)(pln / 6);
    int nk = (int)(sizeof k_kinds / sizeof *k_kinds);
    uint32_t kh[sizeof k_kinds / sizeof *k_kinds];
    for (int k = 0; k < nk; k++) kh[k] = vlt_hash((const uint8_t *)k_kinds[k].kind, strlen(k_kinds[k].kind));
    FILE *f = fopen(raw_stream, "rb");
    if (!f) { free(pl); return XP_ERR_OPEN_IN; }
    XpSp *sp = NULL; int nsp = 0, cap = 0, rc = XP_OK;
    SolidBox *sb = NULL; size_t nsb = 0, sbcap = 0;
    uint8_t *buf = NULL; size_t bcap = 0;
    /* pass 1: the spawner packs, the solids' boxes */
    for (int pass = 0; pass < 2 && rc == XP_OK; pass++) {
        fseek(f, 0, SEEK_SET);
        for (;;) {
            uint8_t h[8];
            if (fread(h, 1, 8, f) != 8) break;
            uint32_t id = u32le(h), sz = u32le(h + 4);
            int want = pass == 0 ? (id == 0x34027u || id == 0x80134000u) : id == 0x80034100u;
            if (!want) { if (fseek(f, (long)sz, SEEK_CUR)) break; continue; }
            if (sz > bcap) { uint8_t *nb = realloc(buf, sz); if (!nb) { rc = XP_ERR_MEMORY; break; } buf = nb; bcap = sz; }
            if (fread(buf, 1, sz, f) != sz) { rc = XP_ERR_READ; break; }
            if (id == 0x80134000u) { if (solid_boxes(buf, sz, &sb, &nsb, &sbcap)) { rc = XP_ERR_MEMORY; break; } }
            else if (id == 0x80034100u) sec_fill(buf, sz, sp, nsp);
            else {
                size_t p = fill_skip(buf, sz); if (sz - p < 16) continue;
                const uint8_t *pk = buf + p; int cnt = (int16_t)(pk[12] | pk[13] << 8);
                for (int i = 0; i < cnt && p + 16 + (size_t)(i + 1) * 64 <= sz; i++) {
                    const uint8_t *r = pk + 16 + i * 64;
                    st->mw++;
                    uint32_t key = u32le(r + 0x28), oi = u32le(r + 0x2c);
                    int k = 0; while (k < nk && kh[k] != key) k++;
                    if (k == nk) { st->other_kind++; continue; }
                    if (oi >= np) { st->bad++; continue; }
                    if (nsp == cap) {
                        int nc = cap ? cap * 2 : 4096;
                        XpSp *ns = realloc(sp, (size_t)nc * sizeof *ns); if (!ns) { rc = XP_ERR_MEMORY; break; }
                        sp = ns; cap = nc;
                    }
                    XpSp *S = &sp[nsp++]; memset(S, 0, sizeof *S);
                    S->kind = k; S->sec = (uint16_t)(pl[6 * oi] | pl[6 * oi + 1] << 8); S->row = (uint16_t)(pl[6 * oi + 2] | pl[6 * oi + 3] << 8);
                    S->info = -1;
                }
            }
        }
        if (pass == 0) {
            if (nsp) qsort(sp, (size_t)nsp, sizeof *sp, sp_sec_cmp);
            if (nsb) qsort(sb, nsb, sizeof *sb, sb_cmp);
        }
    }
    fclose(f); free(buf); free(pl);
    /* infos: one per (kind, solid) with a model box that fits */
    XpInfo *inf = NULL; int ninf = 0, icap = 0;
    for (int i = 0; rc == XP_OK && i < nsp; i++) {
        XpSp *S = &sp[i];
        if (!S->ok) { st->bad++; continue; }
        SolidBox key = { S->solid }, *b = nsb ? bsearch(&key, sb, nsb, sizeof *sb, sb_cmp) : NULL;
        if (!b) { st->no_model++; continue; }
        float d[3] = { b->hi[0] - b->lo[0], b->hi[1] - b->lo[1], b->hi[2] - b->lo[2] };
        if (d[0] > XP_PROP_MAXDIM || d[1] > XP_PROP_MAXDIM || d[2] > XP_PROP_MAXDIM || d[0] < 0 || d[1] < 0 || d[2] < 0) { st->too_big++; continue; }
        int j = 0; while (j < ninf && !(inf[j].kind == S->kind && inf[j].solid == S->solid)) j++;
        if (j == ninf) {
            if (ninf == icap) {
                int nc = icap ? icap * 2 : 256;
                XpInfo *ni = realloc(inf, (size_t)nc * sizeof *ni); if (!ni) { rc = XP_ERR_MEMORY; break; }
                inf = ni; icap = nc;
            }
            XpInfo *I = &inf[ninf++]; I->kind = S->kind; I->solid = S->solid;
            memcpy(I->lo, b->lo, 12); memcpy(I->hi, b->hi, 12);
            for (int k = 0; k < 3; k++)                      /* flat panes: GJK wants some depth */
                if (I->hi[k] - I->lo[k] < 0.1f) { float m = 0.5f * (I->lo[k] + I->hi[k]); I->lo[k] = m - 0.05f; I->hi[k] = m + 0.05f; }
            /* a tall model with an arm or a crown (street lights, trees): a box round
               its whole model would stand in the road under the arm; the hull is the
               pole / trunk at its origin instead (a guess: GAPS "XP props") */
            if (d[2] > 3.0f && (d[0] > 1.5f || d[1] > 1.5f) &&
                b->lo[0] < 0 && b->hi[0] > 0 && b->lo[1] < 0 && b->hi[1] > 0)
                for (int k = 0; k < 2; k++) {
                    if (I->lo[k] < -XP_TRUNK) I->lo[k] = -XP_TRUNK;
                    if (I->hi[k] > XP_TRUNK) I->hi[k] = XP_TRUNK;
                }
        }
        S->info = j;
        st->props++;
    }
    free(sb);
    st->infos = ninf;
    Buf o = {0}; g_pb_oom = 0;
    if (rc == XP_OK) {
        /* hulls */
        ob_align_hdr(&o);
        size_t hc = pb_open(&o, 0x80034020u);
        for (int j = 0; j < ninf; j++) {
            char nm[32]; snprintf(nm, sizeof nm, "XPS_%04d", j);
            ob_align_hdr(&o);
            size_t c = pb_open(&o, 0x39200u);
            uint8_t hd[0x48]; memset(hd, 0, sizeof hd);
            pu32(hd + 0x18, bstr_hash(nm)); memcpy(hd + 0x1c, nm, strlen(nm));
            pb_put(&o, hd, sizeof hd); pb_close(&o, c);
            c = pb_open(&o, 0x39201u);                    /* follows directly: its data 16-aligned */
            for (int v = 0; v < 8; v++) {
                uint8_t vx[48]; memset(vx, 0, sizeof vx);
                for (int k = 0; k < 3; k++) pf32(vx + 0x10 + 4 * k, (v >> k & 1) ? inf[j].hi[k] : inf[j].lo[k]);
                pb_put(&o, vx, sizeof vx);
            }
            pb_close(&o, c);
        }
        pb_close(&o, hc);
        /* infos */
        ob_align_hdr(&o);
        size_t c = pb_open(&o, 0x34026u);
        for (int j = 0; j < ninf; j++) {
            const XpKind *K = &k_kinds[inf[j].kind];
            uint8_t r[0x100]; memset(r, 0, sizeof r);
            char nm[32]; snprintf(nm, sizeof nm, "XPS_%04d", j);
            memcpy(r + 8, nm, strlen(nm));
            float d[3], m = K->mass;
            for (int k = 0; k < 3; k++) d[k] = inf[j].hi[k] - inf[j].lo[k];
            pf32(r + 0x20, m); pf32(r + 0x24, 1); pf32(r + 0x28, 1); pf32(r + 0x2c, 1);
            pf32(r + 0x30, m / 12.0 * (d[1] * d[1] + d[2] * d[2]));   /* a solid box */
            pf32(r + 0x44, m / 12.0 * (d[0] * d[0] + d[2] * d[2]));
            pf32(r + 0x58, m / 12.0 * (d[0] * d[0] + d[1] * d[1]));
            pf32(r + 0x6c, 1); pf32(r + 0x70, 1);
            pu32(r + 0x80, bstr_hash(nm));
            pu32(r + 0x88, K->motion_fx);
            for (int k = 0; k < 3; k++) pf32(r + 0xb0 + 4 * k, 0.5 * (inf[j].lo[k] + inf[j].hi[k]));
            pf32(r + 0xc4, K->anchor);
            pf32(r + 0xcc, 0.1);
            pu32(r + 0xd4, bstr_hash(K->terrain));
            pu32(r + 0xdc, bstr_hash(K->cat));
            pf32(r + 0xe0, K->friction);
            pb_put(&o, r, sizeof r);
        }
        pb_close(&o, c);
        /* placements, one per kept spawner (in spawner order) */
        ob_align_hdr(&o);
        c = pb_open(&o, 0x34107u);
        for (int i = 0; i < nsp; i++) {
            if (sp[i].info < 0) continue;
            uint16_t r[4] = { sp[i].sec, sp[i].row, 0, 0 }; pb_put(&o, r, 8);
        }
        pb_close(&o, c);
        /* the packs, one per section */
        int oi = 0, id0 = 0;
        for (int i = 0; i < nsp; ) {
            int e = i; while (e < nsp && sp[e].sec == sp[i].sec) e++;
            int cnt = 0; for (int k = i; k < e; k++) cnt += sp[k].info >= 0;
            if (cnt) {
                ob_align_hdr(&o);
                c = pb_open(&o, 0x34027u);
                uint8_t hd[16]; memset(hd, 0, sizeof hd);
                pu16(hd + 8, sp[i].sec); pu16(hd + 10, (unsigned)id0); pu16(hd + 12, (unsigned)cnt);
                pb_put(&o, hd, 16);
                for (int k = i; k < e; k++) {
                    const XpSp *S = &sp[k];
                    if (S->info < 0) continue;
                    uint8_t r[0x70]; memset(r, 0, sizeof r);
                    char nm[32]; snprintf(nm, sizeof nm, "XPS_%04d", S->info);
                    pu32(r, bstr_hash(nm)); pu16(r + 4, (unsigned)oi++); pu32(r + 8, S->solid);
                    for (int q = 0; q < 3; q++) { pf32(r + 0x10 + 4 * q, S->mn[q]); pf32(r + 0x20 + 4 * q, S->mx[q]); }
                    for (int q = 0; q < 16; q++) pf32(r + 0x30 + 4 * q, S->M[q]);
                    pb_put(&o, r, sizeof r);
                }
                pb_close(&o, c);
                id0 += cnt; st->packs++;
            }
            i = e;
        }
        if (g_pb_oom) rc = XP_ERR_MEMORY;
    }
    free(sp); free(inf);
    if (rc == XP_OK) {
        FILE *w = fopen(out_path, "wb");
        if (!w) rc = XP_ERR_OPEN_OUT;
        else {
            int ok = fwrite(o.b, 1, o.n, w) == o.n;
            ok &= fclose(w) == 0;
            if (!ok) { remove(out_path); rc = XP_ERR_WRITE; }
        }
    }
    free(o.b);
    return rc;
}
