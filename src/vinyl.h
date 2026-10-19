/* vinyl.h -- the Graphics Shop's vinyls: the part table and the car skin
 * composite, ported from SPEED2.EXE v1.2.
 *
 * A vinyl is a GLOBALB car part of group 50 (VINYL, model VINYLS) fitted in
 * one of four layer slots, VINYL_LAYER0..3 = car slots 0x40..0x43, each with
 * four colour slots VINYL_COLOURl_0..3 = 0x4d + l*4 + c holding a VINYL_PAINT
 * part (group 49, BRAND_NAME 0x3e871f1). Its attributes: TEXTURE (the art,
 * "<CAR>_<TEXTURE>" and "<CAR>_<TEXTURE>_MASK" in CARS/<CAR>/VINYLS.BIN),
 * REMAP (recoloured from the colour slots), NUMREMAPCOLOURS (how many colour
 * rows the Paint screen shows), NUMCOLOURS 0x40 = a Unique vinyl that takes
 * all four layers, LANGUAGEHASH (a few have a display name). The part's
 * upgrade byte: low 5 bits the vinyl group (0x546030's 28 categories), the
 * top 3 the unlock level.
 *
 * The skin (0x61d990 -> 0x611f10): the base paint, then layers 1..4 (the
 * vinyls) and 5, the car's door lines (<CAR>_DOORLINE[_KIT|_WIDEBODY] in
 * TEXTURES.BIN, 0x612b70), each blended by its mask's value a: t = a/128
 * capped at 1 (0x611b00 weighted sum). A REMAP vinyl's pixel is the sum of
 * its colours weighted by the art's R, G and B. Body meshes textured
 * <CAR>_SKIN1 (also _SKIN1B, GLOBAL_SKIN1) wear it (0x631e20 -> DUMMY_SKIN).
 * Here the layers are composited once over an empty overlay that the paint
 * shows through (shader: mix(paint, overlay.rgb, overlay.a)), so a paint
 * change needs no new composite. */
#ifndef OPENUG2_VINYL_H
#define OPENUG2_VINYL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VINYL_LAYERS 4
#define VINYL_GROUPS 29        /* upgrade byte & 0x1f: 0..27 listed, 28 misc */

typedef struct {
    uint32_t hash;             /* the part */
    char tex[32];              /* TEXTURE */
    char name[24];             /* the database's own name ("1A"), shown when no LANGUAGEHASH */
    uint32_t lang;             /* LANGUAGEHASH, 0 none */
    unsigned char group;       /* upgrade & 0x1f */
    unsigned char level;       /* upgrade >> 5 */
    unsigned char remap;       /* REMAP */
    unsigned char ncol;        /* NUMREMAPCOLOURS */
    unsigned char unique;      /* NUMCOLOURS == 0x40 */
} VinylPart;

/* The part table from GLOBALB chunk 0x80034602's payload. Idempotent. */
int vinyl_db_load(const unsigned char *db, long len);
int vinyl_count(void);
const VinylPart *vinyl_at(int i);
const VinylPart *vinyl_find(uint32_t hash);

/* A decal: a GLOBALB part of group 52 (DECAL_TEXTURE), its NAME hash the
   texture's ("<NAME>_RECT" / "_WIDE" / "_SQUARE" in CARS/TEXTURES.BIN,
   0x61ff40), shown by its LANGUAGEHASH or the database name (0x546f40). */
typedef struct {
    uint32_t hash;             /* the part */
    uint32_t name;             /* NAME: the texture's base hash */
    uint32_t brand;            /* BRAND_NAME */
    uint32_t lang;             /* LANGUAGEHASH, 0 none */
    char dbg[32];              /* the database's own name */
} DecalPart;
int decal_count(void);
const DecalPart *decal_at(int i);
const DecalPart *decal_by_name(uint32_t name);

/* One layer of the composite: the vinyl part and its three colours (RGB). */
typedef struct {
    const VinylPart *part;     /* NULL: an empty layer */
    unsigned char col[3][3];
} VinylLayer;

/* The overlay (RGBA, w*h*4, malloc'd; NULL when nothing lands on the car):
   `path` the car's VINYLS.BIN (only the slots it needs are read: the file is
   ~14 MB), `textures` its TEXTURES.BIN in memory, `doorline`
   the door line texture's suffix ("", "_KIT", "_WIDEBODY"; NULL none),
   `size` the overlay's side (the art's 512, or less: each texel then takes
   the box of art under it). Cut-away texels carry a neighbour's colour.
   The open file, the last few decoded slots and the door lines are kept
   for the next call; vinyl_cache_release() lets them go. */
unsigned char *vinyl_compose(const char *path,
                             const unsigned char *textures, long tlen,
                             const char *car, const VinylLayer layer[VINYL_LAYERS],
                             const char *doorline, int size, int *w, int *h);
void vinyl_cache_release(void);

#ifdef __cplusplus
}
#endif
#endif
