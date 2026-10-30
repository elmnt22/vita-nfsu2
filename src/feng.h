/* feng.h — reader for EA FEng screen packages, their TPK art and FNTF fonts.
 *
 * NFSU2 authors every 2D screen, the in-game HUD included, as an FEng package
 * (chunk 0x00030203, or 0x00030210 = JDLZ-packed, inside the GLOBAL bundles). This
 * module reads one package into a flat object list with resolved screen
 * placement, decodes the textures it names (plain and compressed TPKs), and
 * loads the bitmap fonts its strings use. No GL here: hud.c uploads.
 *
 * Coordinate space: 640x480 with the ORIGIN AT SCREEN CENTRE, y down. An
 * object's position is its CENTRE, relative to its parent group; feng_* fills
 * in the absolute centre (ax, ay). Angles are radians, clockwise on screen.
 * tools/fng_dump.py is the Python twin of this reader and renders previews.
 */
#ifndef OPENUG2_FENG_H
#define OPENUG2_FENG_H

#include <stdint.h>

#define FENG_MAX_OBJ  512
#define FENG_MAX_RES  96
#define FENG_TEXT     48
#define FENG_DATAW    21
#define FENG_MAX_SCR  2048       /* parse caps; a package keeps only what it uses */
#define FENG_MAX_TRK  2560       /* UI_Options: 1038 scripts, 2304 tracks, 16244 key words (the largest) */
#define FENG_MAX_KEYW 17408      /* at 2048 / 13312 UI_Options lost its last rows' tracks (row 1 never lit) */

enum { FENG_IMAGE = 1, FENG_STRING = 2, FENG_GROUP = 5 };
/* Sj string justification bits; neither set = left / top */
enum { FENG_JUST_HCENTER = 1, FENG_JUST_RIGHT = 2,
       FENG_JUST_VCENTER = 4, FENG_JUST_BOTTOM = 8 };

typedef struct {
    uint32_t hash, guid, flags;
    int      type, res, parent;   /* parent: object index, -1 = top level */
    int      root;                /* top-level ancestor (self when top level) */
    float    rgba[4];             /* 0..1 */
    float    pivot[2], pos[3], size[2], uv[4];
    float    angle;               /* authored rotation, own */
    float    ax, ay, az, aangle;  /* resolved through the parent chain */
    unsigned just;
    int      lead, wrapw;         /* Sl leading between lines, Sw wrap width (0 none) */
    char     text[FENG_TEXT];     /* authored placeholder, ASCII */
    /* the SA tag as stored, the object data the scripts animate (obj+0x2c):
       words 0..3 colour as ints B,G,R,A (0..255), 4..6 pivot, 7..9
       position, 10..13 rotation quaternion, 14..16 size, 17..20 uv */
    int32_t  data[FENG_DATAW];
    short    nscr, scr0;          /* scripts: scr[scr0 .. scr0+nscr) */
} FengObj;

typedef struct { char name[40]; int type; } FengRes;   /* type 1 = texture, 2 = font */

/* FEng scripts (chunk 'Scrp' of an object, one chunk per script; loader
 * 0x5515e0). 'Sh' {hash, length ms, flags, tracks}, 'Sc' the script to chain
 * to, then per track 'FI' {type, size, interp, flags, length}, 'To' the word
 * offset into the object data and 'Kd' the keys {i32 time, value}: the first
 * key is the BASE (time -1, absolute), the others are deltas from it. */
enum { FENG_TRK_INT = 1, FENG_TRK_FLOAT, FENG_TRK_VEC2, FENG_TRK_VEC3,
       FENG_TRK_QUAT, FENG_TRK_COLOR };
typedef struct {
    unsigned char type, size, interp, flags;  /* size in bytes; flags&0x7f: 0 clamp,
                                                 1 wrap, 2 ping-pong */
    unsigned char off;                        /* word offset into the data */
    short   nkey;                             /* delta keys after the base */
    int     len;                              /* ms */
    int     key0;                             /* keyw index of the base key */
} FengTrack;
typedef struct {
    uint32_t hash, chain;                     /* chain: next script, 0 none */
    int      len;                             /* ms */
    uint32_t flags;                           /* &3: 0 once, 1 loop, 2 ping-pong */
    short    ntrk, trk0;
    short    nev, ev0;                        /* its events: ev[ev0 .. ev0+nev) */
} FengScript;
/* FEng messages. A script's 'EV' tag {msg, target, 0, ?, -1, time ms} sends
 * msg when the script's clock passes time; target -4 = the package's 'Targ'
 * list for msg ('Tc' count, 'Mt' {msg, guid...}), else an object guid. An
 * object's 'MsgR' chunk ('Mi' msg, 'Mc' count, then 'Ri' command, 'Ru'
 * argument, 'Rt' per response) answers it; command 0 runs script Ru on the
 * object itself (e.g. Chyron_FE's EVENT HANDLER SHORT -> the pieces). */
typedef struct { uint32_t msg; int32_t target; int time; } FengEvent;
typedef struct { uint32_t msg, guid; } FengTarg;
typedef struct { uint32_t msg, cmd, arg; short obj; } FengResp;
#define FENG_MAX_EV   1024
#define FENG_MAX_TG   2048
#define FENG_MAX_RSP  4096
#define FENG_SCR_INIT 0x001744b3u             /* "INIT", the start script */

typedef struct {
    char    name[40];
    int     nobj, nres;
    FengObj obj[FENG_MAX_OBJ];
    FengRes res[FENG_MAX_RES];
    int     nscr, ntrk, nkeyw;
    FengScript *scr;                          /* nscr / ntrk / nkeyw entries, heap */
    FengTrack  *trk;
    int32_t    *keyw;
    int        nev, ntg, nrsp;                /* the messages, heap */
    FengEvent  *ev;
    FengTarg   *tg;
    FengResp   *rsp;
} FengPackage;

/* bStringHash: h = h*33 + c from 0xffffffff. */
uint32_t feng_hash(const char *s);
/* FEHashUpper (0x505450): the same over the upper-cased string -- object and
   script names ("SP_DRIFT_TOTAL", "FadeIn") are hashed this way. */
uint32_t feng_hash_upper(const char *s);

/* The running scripts of one package instance (the per-object FEng state
 * the retail engine keeps in the objects themselves). */
typedef struct {
    const FengPackage *p;
    int32_t data[FENG_MAX_OBJ][FENG_DATAW];   /* the animated object data */
    short   cur[FENG_MAX_OBJ];                /* script index, -1 none */
    int     t[FENG_MAX_OBJ];                  /* its time, ms */
    unsigned char hidden[FENG_MAX_OBJ];
    unsigned char trk_done[FENG_MAX_TRK];     /* track flags bit 7 (0x5354f0) */
    int32_t keyw[FENG_MAX_KEYW];              /* the keys: setting a colour moves
                                                 the scripts' base keys (0x517ad0) */
    uint32_t game_msg[8]; int ngame;          /* script events to the game (target -1) */
} FengAnim;
/* a script event the package sent to the game (SendMessageToGame GC 80147b20,
   e.g. EXIT_COMPLETE 0xe1fde1d1 at the end of FORWARD): 1 and taken if queued */
int  feng_anim_game_msg(FengAnim *a, uint32_t msg);
/* every object to its authored data, hidden as authored, INIT running */
void feng_anim_init(FengAnim *a, const FengPackage *p);
/* 0x51cf70: switch obj to script `hash` (a name hash, feng_hash_upper);
   restart: back to its start even when it already runs. 0 = no such script */
int  feng_anim_set(FengAnim *a, int obj, uint32_t hash, int restart);
/* 0x51d040: obj runs script `hash` and it has not reached its end */
int  feng_anim_running(const FengAnim *a, int obj, uint32_t hash);
/* obj's authored data with script `hash` evaluated at t ms (a copy of an
   object running its own script, e.g. the Credits' rolled lines 0x49cad0) */
void feng_anim_eval_at(FengAnim *a, int obj, uint32_t hash, int t_ms);
/* 0x50ca00 / 0x50ca50: hide / show, into a group's children too */
void feng_anim_hide(FengAnim *a, int obj, int hide);
/* 0x54ce60 for every object: advance the scripts by dt ms and evaluate */
void feng_anim_tick(FengAnim *a, int dt_ms);
/* 0x51d7f0: colour 0xAARRGGBB into the object data */
void feng_anim_color(FengAnim *a, int obj, uint32_t argb);
/* 0x5178d0 field 2: the object's position (its data, centre for images,
   the text anchor for strings) and every script's position base key */
void feng_anim_pos(FengAnim *a, int obj, float x, float y);
/* 0x51ced0: the rotation, degrees (clockwise on screen); 0x517db0: the size */
void feng_anim_rot(FengAnim *a, int obj, float deg);
void feng_anim_size(FengAnim *a, int obj, float x, float y);
int  feng_anim_msg(FengAnim *a, int obj, uint32_t msg);   /* its responses run; the count */
void feng_anim_uv(FengAnim *a, int obj, float u0, float v0, float u1, float v1);   /* data words 17..20 */

/* The screen placement of an animated object, through its parent groups
 * (0x51b0b0 / 0x52c5d0): colour (0..1) multiplied down the chain, the
 * object's centre, its scale (size, x group sizes) and rotation (radians,
 * clockwise). hidden: it or a parent is hidden. */
typedef struct { float rgba[4]; float x, y, sx, sy, angle; int hidden; } FengPlace;
void feng_anim_place(const FengAnim *a, int obj, FengPlace *out);
/* TPK key of a resource: hash of its upper-cased name without extension. */
uint32_t feng_tex_key(const char *resname);

/* Find package `name` (case-insensitive, e.g. "HUD_SingleRace.fng") in a
 * bundle. Returns 1 and fills *out on success. */
int  feng_load_package(const unsigned char *bun, long len, const char *name,
                       FengPackage *out);
/* The script tables feng_load_package allocated (not the package itself). */
void feng_free_package(FengPackage *p);
/* First object with this name hash, or -1. */
int  feng_find(const FengPackage *p, uint32_t hash);
/* 1 when `obj` is `anc` or lies inside group `anc`. */
int  feng_inside(const FengPackage *p, int obj, int anc);

/* A decoded texture: RGBA8, top row first. `blend` is the record's +0x4a
 * draw byte (HUD art: 1 normal, 2 alpha-mask shape, 6 additive). */
typedef struct { int w, h; unsigned char *rgba; unsigned char blend; } FengImage;

/* One pass over every TPK in a bundle (plain 0x33310004 tables and
 * compressed 0x33310003 indexes with JDLZ/HUFF blobs). For each keys[i] whose
 * out[i].rgba is still NULL and that this bundle holds, decodes into out[i].
 * Formats: P8, BGRA8, DXT1, DXT3, DXT5. Returns how many were filled. */
int  feng_tpk_load(const unsigned char *d, long len, const uint32_t *keys,
                   int nkeys, FengImage *out);
/* The same from a file, reading only the wanted plain records' bytes;
 * -1 when the file is a JDLZ bundle or unreadable (read it whole then). */
int  feng_tpk_load_file(const char *path, const uint32_t *keys, int nkeys, FengImage *out);
void feng_image_free(FengImage *img);

/* FNTF bitmap font (LANGUAGES/<lang>.bin chunk 0x00030201). Glyph metrics are
 * atlas pixels, which are also 640x480 FEng units. */
typedef struct {
    unsigned short u, v, adv;
    unsigned char  w, h, yo, ok;
    signed char    xo;
} FengGlyph;
typedef struct {
    char      name[40];           /* lower-case, no extension */
    int       line;               /* line height */
    uint32_t  atlas_key;          /* TPK key of the atlas (LanguageTextures.bin) */
    FengGlyph g[256];
} FengFont;

/* Load font `resname` ("Font_ConduitMdITCTT38bi.ffn" or the bare chunk name)
 * from a language file. Returns 1 on success. */
int   feng_load_font(const unsigned char *lang, long len, const char *resname,
                     FengFont *out);
/* Unscaled advance width of an ASCII string. */
float feng_text_width(const FengFont *f, const char *s);

#endif
