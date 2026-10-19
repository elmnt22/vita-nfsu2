/* shop.h — the Car Shop (customization) screens, built 1:1 from the game's
 * own front end.
 *
 * Every screen is a retail FEng package from FRONTEND/FrontB.lzc
 * (UI_ChooseCustomizeCategory, UI_ChoosePerformanceCategory,
 * UI_GenericParts_Browser), drawn with its own art and fonts. The menu
 * contents and order come from SPEED2.EXE (see the tables in shop.c), the
 * text from LANGUAGES/English.bin, the part names, levels and model links
 * from the GLOBALB car part database (0x80034602) and the prices from the
 * GLOBALB career unlock table (0x34A1C).
 *
 * First use extracts what the screens need (three packages and their
 * textures, decoded) into ux0:data/OpenUG2/cache/shop_v1.bin, so FrontB and
 * GLOBALB are only unpacked once.
 *
 * The shop never touches the car itself: it hands main.c a complete
 * N2CarConfig to preview or install (SHOP_ACT_PREVIEW) and main.c reports
 * back what is actually fitted (shop_set_car).
 */
#ifndef OPENUG2_SHOP_H
#define OPENUG2_SHOP_H

#include "render.h"
#include "car_config.h"

typedef struct Shop Shop;

enum { SHOP_KEY_UP = 1, SHOP_KEY_DOWN, SHOP_KEY_LEFT, SHOP_KEY_RIGHT,
       SHOP_KEY_OK, SHOP_KEY_BACK,
       SHOP_KEY_LTRIG, SHOP_KEY_RTRIG,     /* L1 / R1: the car select's filter */
       SHOP_KEY_TRI,                       /* Triangle: back, or the controller setup's reset */
       SHOP_KEY_SQ };                      /* Square: the decal shop's "remove all" (0xc519bfc0) */
enum { SHOP_ACT_NONE, SHOP_ACT_PREVIEW, SHOP_ACT_RIM, SHOP_ACT_EXIT,
       SHOP_ACT_PLAY,        /* front end: Quick Race picked (unused since the QR screens) */
       SHOP_ACT_CUSTOMIZE,   /* front end: Customize picked */
       SHOP_ACT_QR_CAR,      /* Quick Race car select: show car `car` */
       SHOP_ACT_QR_GO };     /* Quick Race: start the race in `qr` with car `car` */

/* Quick race modes (settings 0x83aab4 +0: the type) */
enum { QR_CIRCUIT = 0, QR_SPRINT = 1, QR_DRAG = 2, QR_DRIFT = 3, QR_URL = 4,
       QR_FREERUN = 9, QR_STREETX = 10 };
/* The race a Quick Race starts (0x53fc20 -> 0x89e7a0..): the settings record
   R = 0x83aac4 + type * 20 of the chosen mode. */
typedef struct {
    int  type;                /* QR_* */
    int  event;               /* track record +0x8a (Free Run: 4000) */
    char stem[8];             /* track record +0x40: L4RA .. L4RG */
    int  laps;                /* R+0xa (1..10; Lap KO forces 3) */
    int  opponents;           /* R+1 */
    int  difficulty;          /* R+2: 0 easy, 1 medium, 2 hard */
    int  traffic;             /* R+4: 0 none .. 3 maximum */
    int  catchup;             /* R+5 */
    int  reverse;             /* R+0xc (track record +0x86 reversible) */
    int  lap_ko;              /* R+0x10 */
} ShopQuickRace;

typedef struct {
    int kind;
    N2CarConfig cfg;          /* SHOP_ACT_PREVIEW / EXIT: what the car should show */
    char rim_folder[16];      /* SHOP_ACT_RIM / EXIT: CARS/WHEELS/GEOMETRY_<folder>.BIN */
    int  rim_style;           /* ... and its STYLEnn, with the size (RIM_STYLE / RIM_INNER) */
    char car[32];             /* SHOP_ACT_QR_CAR / QR_GO: CARS/ folder */
    ShopQuickRace qr;         /* SHOP_ACT_QR_GO */
} ShopAction;

/* A rim value: STYLEnn in the low byte, the inner radius (rim size, the part's
   INNER_RADIUS, <FOLDER>_STYLEnn_<inner>_<outer>) above it; 0 = the first size
   for the car's OUTER_RADIUS (record +0xdc), as the rim browser previews it. */
#define RIM_STYLE(v)        ((v) & 0xff)
#define RIM_INNER(v)        (((v) >> 8) & 0xff)
#define RIM_VALUE(st, in)   (((st) & 0xff) | ((in) & 0xff) << 8)

/* One car of the Quick Race car select (car type order, GLOBALB 0x34600). */
typedef struct {
    char folder[32];          /* CARS/<folder>, the type name */
    char make[24];            /* type record +0xc0: CARSELECT_MANUFACTURER_<make> */
    float accel, top_speed, handling;   /* 0x5b53a0 / 0x5b6a20 / 0x5b5490, 0.03..1 */
    int  mine;                /* a garage-saved configuration exists ("My Cars") */
} ShopCar;

/* Loads (or builds) the cached screen data, the strings and the car part
 * database. `car` is the CARS/ folder name; `installed` what the car wears;
 * `menus` the renderable options per slot (n2_mod_catalog). NULL on failure. */
/* the player car's stock base paint part (record +0x84c): the Paint Shop
   starts on it and on its brand when no paint is installed */
void shop_set_stock_paint(uint32_t part_hash);
/* the player car's raw GLOBALB physics record (NULL: none): the
   Performance Shop's stat sliders are built from it (0x5ac4f0) */
void shop_set_car_rec(const unsigned char *rec);
/* The garage camera view for the screen and entry shown: a car slot id of
   the view table (0x7f4608), its low 16 bits; a vinyl group + 1 above them
   (0x7f5d40); 0 the default view. */
int shop_view(const Shop *s);
/* The opening parts the screen shows (0x521040 the parts browser's +0x54,
   0x50f290 engine / trunk neon, 0x4c1d40 the engine view 0x44): bit 0 the
   hood, 1 the trunk, 2 both doors. hood_style: 0 REG, 2 a split hood, 4 a
   VERTSPLIT one (0x61f820); hood_flag: the STYLE22 hood; door_style: the
   doors' ANIMSTYLE, WIDE1..4 (3..6) for scissor doors on a wide body. */
int shop_parts_open(Shop *s, int *hood_style, int *hood_flag, int *door_style);
/* A part's database attributes without a shop (GLOBALB 0x80034602, read
   once and kept): vals[i] = the raw value of keys[i]; returns a mask of
   the keys found (0: no such part). */
/* The Nitrous Purge browser's Square preview (0x568d30 +0x116): the
   highlighted purge part while it runs, else 0 -- main.c sprays it on the
   showroom car and plays FX_PURGE. */
unsigned shop_purge_preview(Shop *s);
unsigned shop_part_attrs(const char *dataroot, uint32_t part, const uint32_t *keys, int n, uint32_t *vals);
/* A generic part's model name (model 0, or 1 the _PAINT twin; no _A LOD
   letter), e.g. AUDIO_COMP_SPEAKER0_8: 1, or 0 if it has none. */
int shop_part_model(const char *dataroot, uint32_t part, int model, char *out, int cap);
Shop *shop_open(const char *dataroot, const char *car, const N2CarConfig *installed,
                const N2PartMenu menus[N2_PART_COUNT], const char *rim_folder, int rim_style);
void  shop_close(Shop **s);
/* Turn an open shop into the front end: title screen (UG_LS_Splash), then the
 * main menu (UI_Main). Its actions are SHOP_ACT_PLAY / SHOP_ACT_CUSTOMIZE. */
void  shop_front_end(Shop *s);
/* The front end straight at its main menu (UI_Main), as a quit from a race
 * returns to it. */
void  shop_main_menu(Shop *s);
/* Shows PC_Loading (the game's loading screen); shop_front_end ends it. */
void  shop_loading(Shop *s);
/* PC_Loading alone (no menus, no part database): a loading screen to keep. */
Shop *shop_open_loading(const char *dataroot);
/* Custom loading text (NULL/"" = the authored "LOADING") and a second line. */
void  shop_loading_text(Shop *s, const char *line1, const char *line2);
/* the same with a third line under the second */
void  shop_loading_text3(Shop *s, const char *line1, const char *line2, const char *line3);

/* The Quick Race data: the GLOBALB 0x34201 track records (0x128 bytes each,
 * kept by the caller) and the selectable cars, `cur` the one in use. */
void  shop_set_tracks(Shop *s, const unsigned char *recs, int n);
void  shop_set_cars(Shop *s, const ShopCar *cars, int n, const char *cur);

/* What main.c actually fitted after the last request (or after a car switch). */
void  shop_set_car(Shop *s, const char *car, const N2CarConfig *fitted,
                   const N2PartMenu menus[N2_PART_COUNT]);

/* After a SHOP_ACT_PREVIEW: what the car really shows (a part that could not
 * be attached leaves the previous one). */
void  shop_preview_result(Shop *s, const N2CarConfig *shown);

void  shop_input(Shop *s, int key);
/* The controller setup's button capture: 1 just started (the caller drops
 * the press that started it and arms it), 2 waiting; shop_ctl_button gives
 * the pressed button (options.h VB_*). */
int   shop_ctl_capture(const Shop *s);
/* The option rows (shop.c's UI_Options logic on g_opt) for the pause menu's
 * UI_PauseOptions: cat 0 audio, 2 gameplay, 3 player; the row ids, a
 * slider's level (0..1, NULL for a toggle), its label and value strings
 * (English.bin hashes), one step left / right. */
int   shop_pause_rows(int cat, int drag, int roam, int *rows, int cap);
int   shop_row_slider(int r);
float *shop_row_level(int r);
int   shop_row_disabled(int r);
void  shop_row_change(int r, int step);
uint32_t shop_row_name(int r);
uint32_t shop_row_value(int r);
void  shop_ctl_arm(Shop *s);
void  shop_ctl_button(Shop *s, int vb);
/* Per frame: advances animation and the preview debounce. dt in seconds. */
void  shop_update(Shop *s, float dt);
/* 1 and fills *a when there is an action for main.c. */
int   shop_poll(Shop *s, ShopAction *a);
void  shop_draw(Shop *s, const RProg *rp, GpuMesh *quad, int width, int height);

#endif
