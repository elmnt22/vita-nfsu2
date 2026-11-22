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
 * textures, decoded) into ux0:data/NFSU2/cache/shop_v1.bin, so FrontB and
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
       SHOP_KEY_SQ,                        /* Square: the decal shop's "remove all" (0xc519bfc0) */
       SHOP_KEY_HELP };                    /* Select: the screen's Help ($JOY_EVENT_FENG_HELP$) */
enum { SHOP_ACT_NONE, SHOP_ACT_PREVIEW, SHOP_ACT_RIM, SHOP_ACT_EXIT,
       SHOP_ACT_PLAY,        /* front end: Quick Race picked (unused since the QR screens) */
       SHOP_ACT_CUSTOMIZE,   /* front end: Customize Car Select accepted, car `car` */
       SHOP_ACT_QR_CAR,      /* Quick Race car select: show car `car` */
       SHOP_ACT_QR_GO,       /* Quick Race: start the race in `qr` with car `car` */
       SHOP_ACT_PROFILE,     /* a profile was loaded / reset: the garage car again (0x3a2be557) */
       SHOP_ACT_CAREER,      /* UI_StartCareer: StartCareerFreeRoam (career_nis: CareerNis1 first) */
       SHOP_ACT_CAREER_QUIT,     /* the crib's Back To Main: the career is left, UI_Main */
       SHOP_ACT_CAREER_RIDE,     /* the crib from the career menu: the career car on the platform */
       SHOP_ACT_CAREER_PARTS,    /* the crib's Change Parts: the Car Shop on the career car (shop_career_customize 1) */
       SHOP_ACT_CAREER_EVENT,    /* the crib World Map's Yes: StartCareerEventFromWorldMap(event) */
       SHOP_ACT_XP_TRIP,         /* port: the crib's Play to an expansion city (shop_trip_city / shop_trip_share) */
       SHOP_ACT_TEST_TRACK,      /* the crib's Test Track: DoStartTuning -> BeginCarTuning(entry 0) */
       SHOP_ACT_DELETE_CAR };    /* F2.4: the Customize car select's Square + Yes: DeleteTunedCar(slot) */

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
    int  slot;                /* F2.4, QR_CAR / CUSTOMIZE / QR_GO / DELETE_CAR from the Quick Race car select:
                                 the car's tuned slot, -1 its stock car; -2 = not a garage pick (crib, lot) */
    uint32_t career_handle;   /* QR_CAR from the crib car select: the career slot shown (SetRideInfo of its ride), else 0 */
    ShopQuickRace qr;         /* SHOP_ACT_QR_GO */
    int career_nis;           /* SHOP_ACT_CAREER: the first-time intro scene plays */
    const unsigned char *event;   /* SHOP_ACT_CAREER_EVENT: the career event record */
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
    int  slot;                /* F2.4: its tuned slot (TUNED_CAR_<slot>), -1 the stock car */
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
/* The same styles for a fitted car without a shop (the Show Off screen). */
void shop_car_part_anim_styles(const char *dataroot, const char *car, const N2CarConfig *cfg,
                               int *hood_style, int *hood_flag, int *door_style);
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
/* A preset ride (GLOBALB 0x30220, e.g. DDAY_PLAYER_CAR) as the garage keeps a
 * car: its CARS/ folder, configuration and rims (RideInfo::FillWithPreset
 * 0x637270, GC 80245bf8; slots N2CarConfig has no place for are listed in
 * shop.c). preset_hash = bStringHash of the name. 1 if found. */
int shop_preset_ride(const char *dataroot, uint32_t preset_hash, char car[32], N2CarConfig *cfg,
                     char rim_folder[16], int *rim_style);
/* RideInfo::BuildRandomRide 0x6374b0 + BuildRandomSkin 0x632ec0: a random
 * ride of car for an opponent whose performance paths (ePhysicsUpgradePath
 * order, levels 0..3; path 9 is re-derived as SyncVisualPartsWithPhysics
 * does) are perf_lvl, drawn with bRandom on *rng. out gets the body parts,
 * paints, vinyls, tint, neon, bulb (cfg->perf untouched); rim / rim_style as
 * shop_preset_ride's. s NULL: the database shop_part_db / shop_preset_ride
 * loaded. 0 when the database or the car type is missing. */
int shop_random_ride(Shop *s, const char *car, const int perf_lvl[10], uint32_t *rng, N2CarConfig *out,
                     char *rim, int rimsz, int *rim_style);
/* The PC save import (pcsave.c). shop_car_type_name: the car type whose
 * name hashes (bStringHash) to hash, 1 found. shop_slot_ride: a saved
 * PresetCarSlot (0x418 bytes) as the garage keeps a car (FillRideWith
 * 80134e24: every slot's part by hash, the perf parts, the junkman parts),
 * 0 when not filled / unknown. shop_inv_owned: the parts a saved
 * OwnedPartInventory (0x3c0 bytes as saved) owns, as name-hash keys
 * (CareerCar.own); fitted (the car's slot, or NULL) is checked against them. */
int shop_car_type_name(const char *dataroot, uint32_t hash, char car[32]);
int shop_car_type_at(const char *dataroot, int i, char car[32]);   /* GLOBALB 0x34600 order (FEStockCar +0x14) */
int shop_slot_ride(const char *dataroot, const unsigned char *slot, char car[32], N2CarConfig *cfg,
                   char rim_folder[16], int *rim_style);
int shop_inv_owned(const char *dataroot, const char *car, const unsigned char *inv, const unsigned char *fitted,
                   uint32_t *own, int max, int *fit_n, int *fit_owned);
/* the part database alone (no shop screens), loaded once */
Shop *shop_part_db(const char *dataroot);
/* SetUpgradePart 0x61bc80: the model of the slot's first part at that
 * upgrade level (e.g. slot 0x1b FRONT_BRAKE level 1..3), 0 none. */
int shop_upgrade_model(const char *dataroot, const char *car, int slot, int level, char *out, int cap);
Shop *shop_open(const char *dataroot, const char *car, const N2CarConfig *installed,
                const N2PartMenu menus[N2_PART_COUNT], const char *rim_folder, int rim_style);
void  shop_close(Shop **s);
/* Turn an open shop into the front end: title screen (UG_LS_Splash), then the
 * main menu (UI_Main). Its actions are SHOP_ACT_PLAY / SHOP_ACT_CUSTOMIZE. */
void  shop_front_end(Shop *s);
/* The front end straight at its main menu (UI_Main), as a quit from a race
 * returns to it. */
void  shop_main_menu(Shop *s);
/* The Customize Car Select (UI_QRCarSelect, game mode 0x20), as leaving the
 * Car Shop from Customize returns to it (needs the car list set). */
void  shop_customize_cars(Shop *s);
/* The career shop the player drove into (LaunchFrontendShopFromGame
 * 8012721c), shop_desc the ShopDataDesc (career_enter_shop): type 4 (+0x50)
 * opens the Car Lot (UI_CareerCarLot); the other types are not built yet
 * (0 returned). Needs the car list set; leaving is SHOP_ACT_CAREER. */
int   shop_career_shop(Shop *s, const unsigned char *shop_desc);
/* UIMagazineReward: a front-end showcase's cover (kinds 3/4, after LaunchMagazine
   paid it); Accept is SHOP_ACT_CAREER. 0 when the database is unreadable. */
int   shop_magazine_reward(Shop *s, const unsigned char *showcase);
/* while the cover is up: its car setup's garage camera view (heading, steer,
   FOV, target[3], eye[3]); 0 otherwise */
const unsigned char *shop_cover_due(Shop *s);   /* the FE cover's photo is due this frame */
void  shop_cover_capture(const unsigned char *sc, int W, int H);   /* ScreenShot: the middle of the frame, saved */
int   shop_mag_view(const Shop *s, float cam[9]);
/* while the cover is up: its render setup's shaper rig hash and FE env map
   name ("" / 0 = none), and the car setup's camera roll in degrees */
int   shop_mag_render(const Shop *s, uint32_t *rig, char env[16], float *roll);
/* the cover's background (UI_MagazineBack), drawn before the car */
void  shop_draw_under(Shop *s, const RProg *rp, int width, int height);
/* BeginCarCustomize (entry 1: the crib's Change Parts, 8015dda0) or
   EnterShopFromFreeRoam (entry 0: a city shop of ShopDataDesc type 1..6,
   8015df74) on a Car Shop opened over the career car: the career's unlocks
   (GetPartUnlockFilter 8017107c), prices and cash. Entry 1 starts on
   UI_ChooseCustomizeCategory and BACK there leaves (back to the crib);
   entry 0 starts on PickScreenForShop's screen and BACK there asks to go
   back to Explore Mode. Either way the exit is SHOP_ACT_EXIT. Returns 0
   for a shop type the port has no screen for. shop: the ShopDataDesc
   (entry 0), its scene (+0x40) the platform. */
int   shop_beyond_customize(Shop *s, int type);   /* World Expansion shop / garage (port), non-career */
int   shop_career_customize(Shop *s, int entry, const unsigned char *shop);
/* the crib again after Change Parts, on that option (from_main: it was
   entered from the career menu) */
void  shop_crib_return(Shop *s, int from_main);
void  shop_crib_return_map(Shop *s, int from_main);
int   shop_crib_from_main(const Shop *s);
/* World Expansion trips (port): the crib opened at a city's garage (city 1 Rockport,
   2 Palmont; its Back / Play-there go back to that city), and the trip the crib's
   Play asked for (SHOP_ACT_XP_TRIP): its city and garage choice (1 share, 0 fresh) */
void  shop_crib_open(Shop *s, int city);
int   shop_trip_city(const Shop *s);
int   shop_trip_share(const Shop *s);
/* the profile's "Share Your Garage?" answer for a city: 1 share, 0 start fresh, -1 not asked */
int   xp_garage_choice(int city);
void  xp_garage_choice_set(int city, int share);
void  xp_garage_forget(const char *name);   /* XF4: a deleted profile's answers */
/* the UniquesBrowser ended the world challenge (a part chosen or forfeited): the save is due */
int   shop_uniques_done(const Shop *s);
float shop_rate(const Shop *s, const N2CarConfig *c, const char *rim_folder, int rim_style);   /* 0x5234c0 */
float shop_rate_car(Shop *s, const char *car, const N2CarConfig *c, const char *rim_folder, int rim_style);   /* another car's */
/* After the Car Shop (0x55bdf0): the save question when the profile
 * changed since profile_snapshot() and save prompts are on, over the screen. */
void  shop_save_check(Shop *s);
/* (GitHub #3 S4) ExitToRoamHelper 8015dd24: the career shop's way out when IsDirty --
   EnterMemcard(0x41) over the shop (asks to save unless autosave). shop_mc_busy is 1
   while those screens are up; free roam starts once it is 0 (MemcardExitShopCallback) */
int   shop_exit_memcard(Shop *s);
int   shop_mc_busy(const Shop *s);
/* Shows PC_Loading (the game's loading screen); shop_front_end ends it. */
void  shop_loading(Shop *s);
/* The screens' cache (first shop use builds it): its path, and building it
 * without opening a shop (the PC converter). */
const char *shop_cache_file(void);
int   shop_build_cache(const char *dataroot);
/* PC_Loading alone (no menus, no part database): a loading screen to keep. */
Shop *shop_open_loading(const char *dataroot);
/* Custom loading text (NULL/"" = the authored "LOADING") and a second line. */
void  shop_loading_text(Shop *s, const char *line1, const char *line2);
/* the same with a third line under the second */
void  shop_loading_text3(Shop *s, const char *line1, const char *line2, const char *line3);
/* Which loading screen the next load shows (GC ShowLoadingScreen): the
   career picks PC_Loading or the console Loading_Controller (a Vita
   picture labelled from g_opt) by chance, every other mode the latter. */
void  shop_loading_pick(Shop *s, int career);

/* The Quick Race data: the GLOBALB 0x34201 track records (0x128 bytes each,
 * kept by the caller) and the selectable cars, `cur` the one in use. */
void  shop_set_tracks(Shop *s, const unsigned char *recs, int n);
void  shop_set_cars(Shop *s, const ShopCar *cars, int n, const char *cur);

/* What main.c actually fitted after the last request (or after a car switch). */
void  shop_set_car(Shop *s, const char *car, const N2CarConfig *fitted,
                   const N2PartMenu menus[N2_PART_COUNT]);

/* The garage's platform, GarageMainScreen::ChangePlatform's PlatformNameMap
   (GC 8035b198) index: 1 SHOWROOM, 3 CARLOT01, 4 CRIB01, ... */
int shop_platform(const Shop *s);
/* the front end's ambience zone (GetAmbientZone 0x46b330, FE branch) for the
   shop s is in; career = a career front end */
int shop_fe_ambient_zone(const Shop *s, int career);
/* the front end is on UI_Main (the main menu strip) */
int shop_on_main_menu(const Shop *s);
int shop_boot_pending(const Shop *s);   /* splash / boot load: no garage, no car yet */
/* the hash of a rim part's "<MODEL>_STYLEnn" (attribute 0x10c98090), 0 = unknown */
uint32_t shop_rim_base(const char *dataroot, const char *folder, int value);
#ifdef OPENUG2_SIM_PRE_H
int shop_sim_auto_key(const Shop *s);   /* test aid: the career autopilot (main.c OPENUG2_SIM_AUTO) */
int shop_sim_screen(const Shop *s);
#endif

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
/* the game mode is career (DAT_803ce800 == 1): every MenuScreen alternates the
   player's name and cash (MenuScreen ctor 801515a0 -> SetAlternatingPlayerCash) */
void  shop_set_career_mode(int on);

int shop_take_tt(Shop *s);
void shop_perf_select_tt(Shop *s);
#endif
