#ifndef OPENUG2_CAR_CONFIG_H
#define OPENUG2_CAR_CONFIG_H
#include <string.h>
#include <math.h>

enum {
    N2_PART_FRONT, N2_PART_REAR, N2_PART_SKIRT, N2_PART_HOOD,
    N2_PART_HEADLIGHT, N2_PART_TAILLIGHT, N2_PART_ENGINE, N2_PART_AUDIO,
    N2_PART_SPOILER, N2_PART_EXHAUST, N2_PART_SCOOP, N2_PART_MIRROR, N2_PART_WIDE, N2_PART_COUNT
};

static const char *const n2_part_labels[N2_PART_COUNT] = {
    "Front bumper", "Rear bumper", "Side skirts", "Hood", "Headlights",
    "Taillights", "Engine cover", "Trunk audio", "Spoiler", "Exhaust", "Roof scoop", "Side mirrors", "Wide body"
};

/* Zero part values follow the kit preset. Local overrides encode KITnn as
 * 1+nn and STYLEnn as 101+nn. Library choices are catalog values >=1000. */
/* Paint Shop categories (UI_Paint, 0x53bd50 order): base paint, spoiler,
   roof scoop, mirrors, rims, spinners, brakes, engine accents, muffler tip.
   paint[] holds the installed paint part's hash (0 = the car's stock). */
#define N2_PAINT_COUNT 9
#define N2_PERF_PARTS 128   /* = PERF_NPARTS */
typedef struct {
    int body_kit, hood_style, spoiler_style, wheel_style_id;
    int parts[N2_PART_COUNT];
    unsigned int paint[N2_PAINT_COUNT];
    /* the Performance Shop's fitted parts (PerfConfig +0x330, index = the
       package part's +0); they change no geometry */
    unsigned char perf[N2_PERF_PARTS];
    /* the Graphics Shop's vinyls: VINYL_LAYER0..3 (car slots 0x40..0x43)
       hold a vinyl part (0 none), VINYL_COLOURl_c (0x4d + l*4 + c) its
       colours, a VINYL_PAINT part (0 = the car's default, 0x637050) */
    unsigned int vinyl[4];
    unsigned int vinyl_col[4][4];
    /* the Graphics Shop's decals (0x5469f0 / 0x61ff40): seven locations,
       car slots 0x34..0x3a = DECAL_HOOD, _FRONT_WINDOW, _REAR_WINDOW,
       _LEFT_DOOR, _RIGHT_DOOR, _LEFT_QUARTER, _RIGHT_QUARTER, each with a
       layout (its location part: 1 = the MEDIUM shape, 2 = SMALL; 0 = 1)
       and eight decal slots 0x61 + loc*8 + n holding a DECAL_TEXTURE
       part's NAME hash (0 none) */
    unsigned char decal_layout[8];   /* [7] unused: keeps the struct free of padding (memcmp) */
    unsigned int decal[7][8];
    /* the Car Specialties' Window Tint: the WINDOW_TINT part's light
       material (attribute 0x6ba02c05, WINDSHIELD_TINT_L<n>_<COLOUR>) worn
       by the windows in place of WINDSHIELD; 0 the stock glass */
    unsigned int tint;
    /* the Car Specialties' Lights: the HEADLIGHT_BULB part's colour as the
       game packs it (0x634800 -> render info +0x380: RED | GREEN << 8 |
       BLUE << 16 of the part's attributes); 0 the STOCK bulb */
    unsigned int bulb;
    /* Carbon Fiber (Body Shop): 1 the hood (a CARBONFIBRE hood part),
       2 KIT_CARBON (slot 0x5d), 4 TRUNK_CARBON (slot 0x60) */
    unsigned int carbon;
    /* Neon (Car Specialties): slots 0x9a NEON (underglow), 0x9b
       NEON_ENGINE, 0x9c NEON_CABIN, 0x9d NEON_TRUNK, each the NEON part's
       colours as 0x61fc20 reads them: [0] RED | GREEN << 8 | BLUE << 16 |
       ALPHA << 24, [1] the same of RED2 .. ALPHA2; both 0 = NO NEON */
    unsigned int neon[4][2];
    /* Custom Gauges (Car Specialties): the CUSTOM_HUD part's HUDINDEX
       (slot 0xa3, 0 = STOCK) and the CUSTOM_HUD_PAINT colours, 0x01RRGGBB
       (0 = none: white), [0] slot 0xa4 the backing (tach + turbo fill),
       [1] 0xa6 the dial (RPM numbers + turbo lines), [2] 0xa5 the needle
       (needle + turbo dial) -- 0x4f5254 / 0x50f650 */
    unsigned int hud;
    unsigned int hud_col[3];
    /* Car Specialties (0x5597b0 strip, parts lists 0x539960):
       purge = the NOS_PURGE part (slot 0xa2, DB group 59) as its database
       hash, 0 none -- its HOODEMITTER / WHEELEMITTER groups, HOOD/TIRE HSL
       shifts and HOODLEFT / HOODRIGHT / WHEELLEFT / WHEELRIGHT nozzles feed
       the NOS_BLOWOFF effects (0x634800); hydraulics = the HYDRAULICS part's
       level (slot 0xa1, group 58: 0 none, 1..3); doors = the DOOR_STYLE
       part's ANIMSTYLE (slot 0xa0, group 57: 0 stock, 1 scissor, 2 suicide) */
    unsigned int purge;
    unsigned int hydraulics;
    unsigned int doors;
    /* Car Specialties -> Trunk Audio -> Audio Components: car slots 0x23..0x2e
       TRUNK_AUDIO_COMP_0..11 (DB groups 61 SMALL / 62 MEDIUM / 63 LARGE,
       model AUDIO) as the part's database hash, 0 none; installing a trunk
       layout puts its "NO COMPONENTS" part in every slot (0x5474a0) */
    unsigned int audio_comp[12];
} N2CarConfig;

/* The side mirrors' library of a car, 6 MIRRORS_BODY, 7 MIRRORS_POST,
   8 MIRRORS_SUV, 9 MIRRORS_HUMMER (GLOBALB chunk 0x34607, slot 0x20 =
   WING_MIRROR, read by 0x6101e0: the slot's models are the car itself and
   MIRRORS_POST, but for the cars listed here). Every car mounts them on
   its MIRROR_BODY_LEFT / _RIGHT markers (0x634800). */
static int n2_mirror_lib(const char *car) {
    static const struct { const char *car; int lib; } t[] = {
        { "MIATA", 6 }, { "HUMMER", 9 }, { "NAVIGATOR", 8 }, { "ESCALADE", 8 }, { "TIBURON", 6 },
        { "SUPRA", 6 }, { "TT", 6 }, { "3000GT", 6 }, { "RX7", 6 } };
    for (int i = 0; i < (int)(sizeof t / sizeof t[0]); i++) if (!strcmp(t[i].car, car)) return t[i].lib;
    return 7;
}
/* The headlight colour, RGB / 128 as the world headlight's HEADLIGHTCOLOR
   (0x5bdaa0), relative to the STOCK bulb's red (0x64) so the stock lamps
   keep the brightness the light passes were tuned with. */
static void n2_bulb_rgb(const N2CarConfig *cfg, float out[3]) {
    unsigned c = cfg && cfg->bulb ? cfg->bulb : 0x4b5f64u;   /* STOCK: RED 0x64 GREEN 0x5f BLUE 0x4b */
    for (int i = 0; i < 3; i++) out[i] = (float)((c >> (8 * i)) & 0xff) / 100.0f;
}
/* Whether a car mesh wears carbon fibre, as 0x634800 picks each part's
   texture table: table A (+0xac8) swaps both skin keys 0xa7366ae6 and
   0x3c84d757 for CARBONFIBRE, table B (+0xf58) only 0xa7366ae6.
   cls (N2Mesh.car_cf): 1 bumper (A with the kit), 2 skirt / body / door
   (B with the kit), 3 wide body (A with the kit), 4 hood (A with a carbon
   hood), 5 trunk (A converted, else B with the kit). */
#define N2_CF_TEX 0x721aff7cu    /* CARBONFIBRE (GLOBALB texture) */
#define N2_CF_MAT 0x721afddcu    /* CARBONFIBER (light material) */
static int n2_carbon_mesh(const N2CarConfig *cfg, int cls, uint32_t key) {
    if (!cfg || !cls || !cfg->carbon) return 0;
    int a = key == 0xa7366ae6u || key == 0x3c84d757u, b = key == 0xa7366ae6u;
    int kit = (cfg->carbon & 2) != 0;
    switch (cls) {
    case 1: case 3: return kit && a;
    case 2: return kit && b;
    case 4: return (cfg->carbon & 1) && a;
    case 5: return (cfg->carbon & 4) ? a : kit && b;
    }
    return 0;
}
/* A neon's colour now (0x6227ce): each channel base + (int)(delta x
   |sin(4.7 t)|), delta = colour2 - colour1; the alpha also gets
   (int)(1.2 |sin(6.1 t) sin(7.2 t)|) and is kept in 0..254. t is the
   world time in seconds (ticks x 0.00025); the front end holds t = 1.
   Returns R | G << 8 | B << 16 | A << 24, 0 for NO NEON. */
static unsigned n2_neon_now(const N2CarConfig *cfg, int slot, float t) {
    unsigned a = cfg->neon[slot][0], b = cfg->neon[slot][1], out = 0;
    if (!a && !b) return 0;
    float k = fabsf(sinf(4.7f * t));
    for (int c = 0; c < 4; c++) {
        int x = (int)(a >> (8 * c) & 0xff), y = (int)(b >> (8 * c) & 0xff);
        int v = x + (int)((float)(y - x) * k);
        if (c == 3) {
            v += (int)(1.2f * fabsf(sinf(6.1f * t) * sinf(7.2f * t)));
            if (v > 254) v = 254;
        }
        if (v < 0) v = 0; if (v > 255) v = 255;
        out |= (unsigned)v << (8 * c);
    }
    return out;
}
typedef struct { int value; char label[48]; } N2PartOption;
typedef struct { int count; N2PartOption options[256]; } N2PartMenu;

#endif
