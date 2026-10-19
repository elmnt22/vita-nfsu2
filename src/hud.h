/* hud.h — OpenUG2 in-game player HUD (roadmap stage 8).
 *
 * Opt-in via --hud. Draws the game's own single-player HUD: the layout is
 * HUD_SingleRace.fng read from GLOBAL/InGameRace.bun (see feng.h), with its
 * art (InGameRace/InGameCommon, the gauge style in HUD_CustomTextures_00.bin)
 * and bitmap fonts (LANGUAGES/English.bin + LanguageTextures.bin). Code only
 * drives what the game drives: needles, the N2O level, readouts, the live
 * street map, race position/laps. The 640x480 layout keeps its proportions at
 * 16:9 by pinning each group to its nearest screen edge. No asset is copied
 * into the repo.
 *
 * HONESTY CONTRACT. This module renders what the engine actually knows and
 * nothing else. Several retail readouts have no live system behind them yet
 * (see HudState's availability flags); those draw an explicit inactive/empty
 * state rather than a plausible-looking number. Sample values exist only under
 * hud_set_preview(), which also paints a visible PREVIEW tag, so a screenshot
 * can never be mistaken for working telemetry.
 */
#ifndef OPENUG2_HUD_H
#define OPENUG2_HUD_H

#include "render.h"

/* Everything the HUD draws, sampled by the caller once per frame. The HUD
 * never reaches into engine globals: this struct is the whole contract, which
 * is what makes the layout and transform maths testable without a GL context. */
enum { HUD_LAPBOX_NONE = 0, HUD_LAPBOX_LAPS, HUD_LAPBOX_PERCENT };

typedef struct {
    float x, y;
    unsigned char rival; /* race opponent or free-roam outrun racer */
    float heading;       /* radians, world +X = 0, CCW (car +0x40c) */
} HudMapCar;

typedef struct {
    /* --- drivetrain readouts --- */
    float speed_kmh;          /* REAL: PHYS_KMH(speed) */
    float rpm, rpm_redline;   /* PROVISIONAL: the virtual AUDIO gearbox */
    int   gear;               /* <0 reverse, 0 neutral, 1..n forward */
    int   nitro_active;       /* REAL, but binary: there is no tank model */

    /* --- availability. 0 = no live system; draw the empty state. --- */
    unsigned char have_rpm;      /* provisional source present */
    unsigned char have_nos;      /* a nitrous system is FITTED to this car.
                                    0 removes the N2O arc entirely. */
    unsigned char have_turbo;    /* a turbo is FITTED. 0 removes the boost
                                    dial entirely (not a greyed-out dial). */
    unsigned char have_nitro_tank;
    unsigned char have_boost;
    unsigned char have_money;
    float nitro_tank;         /* 0..1, meaningful only if have_nitro_tank */
    float boost;              /* -1 vacuum .. 0 .. +1 full boost, if have_boost */
    long  money;              /* meaningful only if have_money */

    /* --- world placement --- */
    float car_x, car_y;
    float car_heading;        /* radians, world +X = 0, CCW */

    /* --- mode --- */
    int   racing;             /* 0 = free roam, 1 = in an event */
    int   circuit;            /* race only: 1 = laps, 0 = sprint (progress) */
    int   lap, laps;          /* circuit only */
    float sprint_progress;    /* sprint only, 0..1 */
    /* the lap box, as the game's lap widget 0x4aa1b0 fills it from the race
       manager (HUD_LAPBOX_NONE: nothing drawn) */
    int   lapbox;             /* HUD_LAPBOX_* */
    int   lapbox_num;         /* laps: race entry +9 (best lap) + 1 */
    int   lapbox_of;          /* laps: the race's lap count (racemgr +0xbe8) */
    int   lapbox_pct;         /* percent: ftol(max(0, entry +0x14) * 100) */
    /* the countdown widget 0x4ab5b0 (object 0x2da220aa) from the race
       manager: countdown = 0x5eea40 (3, 2, 1, 4 "GO!", 0 hidden),
       countdown_frac = 0x5eeb00 (0..1 through the 0.9 s step) */
    int   countdown;
    float countdown_frac;
    /* the race over widget 0x4c7f00: HUD_FEATURE_RACE_OVER, which the
       player's HUD mask (0x600090) holds alone once the player's race is
       over (0x5fff50) -- every other widget is hidden then. ro_state = the
       player's race entry +0x10 (1 finished); ro_place / ro_racers its
       place and the field (0x4ab4b0: entry +0xc / racemgr +0x24, a drift
       race the drift standings 0x56dd90 / 0x43be80); ro_alone = 0x89e7c0..cc
       sum 1; ro_score_race = drift / downhill (0x401910 / 0x42fc40): the
       score, else the time; ro_time = entry +0x198 in 1/4000 s */
    int   race_over, ro_state, ro_place, ro_racers, ro_alone, ro_score_race, ro_drag;
    int   ro_time, ro_score;
    int   dt_ms;              /* frame time for the race HUD's FEng scripts */
    /* the lap widget's times (0x4aa1b0), the split time (0x4da090) and the
       leaderboard (0x4da190 -> 0x4c6f30 / 0x4aaed0), from the race manager:
       rt_on = the player has a race entry; the race clock (1/4000 s); the
       entry's start-line crossing (+0x30, 0 none), its last and best lap
       (+0x44 / +0x6c at the finish checkpoint); the events since the last
       frame (+0xd a new lap; +0xd or +0xe for the split) and the split's
       gap (0x5f82d0); the standings: each racer's name and gap to the
       player (0x5f82d0), the player's row; lb_on = its feature (one human,
       not drift) */
    int   rt_on, rt_now;
    int   lap_start, lap_last, lap_best, lap_new;
    int   split_on, split_new, split_gap;
    int   lb_on, lb_n;
    struct { char name[24]; int gap, me; } lb[8];
    int   position, cars;     /* 0 = unknown/no opponents */

    /* --- minimap geometry: world-space polyline, borrowed, not owned --- */
    const float *route_xy;    /* interleaved x,y */
    int   route_n;
    const float *nav_xy;      /* free-roam road network */
    int   nav_n;
    const HudMapCar *map_cars; /* borrowed live positions; no synthetic dots */
    int map_car_n;
    float map_radius_m;       /* world metres to the panel edge; 0 = default.
                                 A race sets this to fit the whole route. */
    int   route_loop;         /* 1 = circuit: close the polyline */
    int   route_next;         /* index of the armed gate, -1 = none */
    const float *route_line;  /* optional road-following line to draw instead
                                 of joining route_xy points (x,y pairs) */
    int   route_line_n;
    int   wrong_way;          /* 1: show the race HUD's WRONG WAY icon */
    /* the race's start line on the minimap (racemgr +0xc10, its track
       record's +0xba heading in degrees): STARTLINEINDICATOR (0x4ec950) */
    int   map_start;
    float map_start_x, map_start_y, map_start_deg;

    /* --- navigation --- */
    int   nav_active;         /* a destination is set AND a route was solved */
    float nav_target_x, nav_target_y;
    float nav_bearing;        /* radians, world bearing to the next route node */
    float nav_dist_m;

    /* --- message indicator (free roam only) --- */
    int   message_waiting;    /* engine never sets this yet; see HUD_MSG_* */

    /* --- drift race: what the retail drift widgets read (NULL otherwise) --- */
    const struct HudDrift *drift;
    /* --- drag race: what the retail drag widgets read (NULL otherwise) --- */
    const struct HudDrag *drag;

    /* --- the player's options (options.h; 0 = the shown / MPH state) --- */
    int   kmh;                /* 0x83aa1a: 1 KM/H (0x5ea8b0), 0 MPH */
    int   no_gauges, no_pos, no_laps;   /* Player options Gauges / Race Position / Lap Info Off */
} HudState;

/* The player's car in a drag race, as SPEED2.EXE's HUD_Drag widgets read it
 * each frame: the drag tach 0x4c7090, the heat bar 0x4c7aa0, the nitrous
 * bar 0x4c7b20, the turbo dial 0x4c6b80 and the position widget 0x4ebb10. */
typedef struct HudDrag {
    int   gear;               /* car +0x434: 0 R, 1 N, 2.. forward */
    float rpm;                /* car +0x440 */
    float speed;              /* car +0x430, |forward speed| m/s */
    float maxrpm;             /* engine record +8 (P +0x1f8): the needle's full scale */
    float redline;            /* engine record +4 (P +0x1f4) */
    float ang0, ang1;         /* car record +0x7f0 [+0xc] / [+0x10]: the needle's sweep, deg */
    int   phys;               /* car +0x28 == 1: the physics mover (else no shift grading) */
    int   shifting;           /* drivetrain +0x48 (0x5ecd70) */
    int   cut;                /* drivetrain +0x5c > 0 (0x5ece20) */
    float grade;              /* drivetrain +0x68 (0x5ecea0) */
    int   klass;              /* the performance class (0x5b5330): 0x7fbf60 row */
    float q[10], win[10], up[10];   /* per gear: 0x594f20(rpm, g, 0), 0x594f00(g), 0x5ecd30(g) */
    float heat;               /* engine +0x8c (-1 off, 2 blown) */
    int   turbo; float boost; /* engine +0x98 > 0, +0x9c (0x5ecec0) */
    int   nos; float nos_v;   /* a nitrous system; 0x5925b0 (the bottle, 0..1) */
    int   race_state;         /* *0x890118: 4 racing */
    int   mph;                /* the profile's units (0x83aa1a 0: mph) */
} HudDrag;

/* The player's drift state, as SPEED2.EXE's drift HUD widgets read it each
 * frame (0x4da590 angle/messages, 0x4da900 -> 0x4c85d0 style points,
 * 0x4c88e0 -> 0x4ab780 the running drift's points and title). */
typedef struct HudDrift {
    float slip_deg;           /* 0x59fa10(car+0x40) * 57.2958 */
    int   gear;               /* car +0x434: 0 R, 1 N, 2.. forward */
    float fwd_speed;          /* car +0x42c */
    int   turbo;              /* the car has a turbo (0x457b00 > 0) */
    unsigned char too_slow;   /* scorer +0x17 */
    unsigned char losing_mult;/* scorer +0x16 */
    float wheelf, prox;       /* scorer +0x18, +0x3c */
    int   level, next;        /* scorer +0x1c multiplier, +0x20 points to the next */
    /* style moment 0 (STYLE_DRIFT) */
    int   m0_shown;           /* in this frame's list of shown moments (0x56fd10) */
    int   m0_state;           /* +0x14: 0 running, 3 banked, 4 failed */
    float m0_cur, m0_mult;    /* +0x24 points, +0x1c */
    float m0_banked, m0_best; /* +0x2c, +0x44 */
    uint32_t m0_label;        /* table +0 (DRIFT) */
    int   m0_cont;            /* table +6 */
    uint32_t title;           /* a drift title this frame (0x4ac120), 0 none */
    int   clock;              /* the game clock, 4000 ticks a second (0x86518c) */
    int   dt_ms;              /* this frame, for the FEng scripts */
} HudDrift;

/* Opaque GL-side resources. */
typedef struct Hud Hud;

/* Load the HUD art from `dataroot`. Returns NULL only if allocation fails; a
 * missing container or texture is NOT fatal — the affected element is skipped
 * and named by hud_missing(). */
Hud *hud_init(const char *dataroot);
/* The Street X HUD (0x5f1670 picks it when 0x89e7e3 is set):
 * HUD_Short_Track.fng from GLOBAL/InGameRace.bun -- HUD_SingleRace's
 * widgets without the N2O gauge. Drawn and fed like hud_init's. */
Hud *hud_init_short(const char *dataroot);
/* The drift race HUD: HUD_Drift.fng from GLOBAL/InGameDrift.bun, with the
 * package's FEng scripts running and the retail drift widgets ported. Drawn
 * by hud_draw like the other; HudState.drift feeds it. */
Hud *hud_init_drift(const char *dataroot, int turbo);   /* turbo: the car has one */
/* The drag race HUD: HUD_Drag.fng from GLOBAL/InGameDrag.bun, scripts
 * running, the retail drag widgets ported. redline / maxrpm: the player's
 * engine record +4 / +8, which pick the tach's rev scale art (0x5ed9b0).
 * HudState.drag feeds it. */
Hud *hud_init_drag(const char *dataroot, float redline, float maxrpm);
/* The player's Custom Gauges (0x4f5254): the gauge style (HUDINDEX: which
 * GLOBAL/HUD_CustomTextures_NN.bin and <NAME>_CUSTOM_NN art) and the three
 * colours (0x01RRGGBB, 0 = white) the HUDs made after this call use.
 * Returns 1 when that changed. */
int hud_custom(int style, const unsigned col[3]);
/* A whole FEng screen package (e.g. UI_PostRaceResults.fng): every object is
 * drawn from its live FEng state (scripts running), centred at 4:3. The
 * caller fills its strings by object name, as the game's screen code does. */
Hud *hud_init_screen(const char *dataroot, const char *bundle, const char *package);
void hud_screen_text(Hud *h, const char *name, const char *text);
void hud_screen_label(Hud *h, const char *name, uint32_t label);   /* 0x50c900 */
void hud_screen_label_h(Hud *h, uint32_t obj, uint32_t label);     /* the object by its name hash */
void hud_screen_text_h(Hud *h, uint32_t obj, const char *text);
void hud_screen_script_h(Hud *h, uint32_t obj, const char *script);
void hud_screen_hide_h(Hud *h, uint32_t obj, int hide);
const char *hud_screen_string(const Hud *h, uint32_t label);      /* English.bin */
void hud_screen_script(Hud *h, const char *name, const char *script);
void hud_screen_hide(Hud *h, const char *name, int hide);
/* an object's position (centre for images, the text anchor for strings)
   and size (images: pixels, strings: scale) as authored, i.e. as a screen
   constructor reads them (0x51ff80) before any script runs -- the stats
   screen's *_MASTER boxes chain INIT into a hidden, collapsed script;
   0 = no such object */
int  hud_screen_box(const Hud *h, const char *name, float *x, float *y, float *w, float *ht);
/* 0x5178d0 field 2: move an object (and its scripts' base position) */
void hud_screen_pos(Hud *h, const char *name, float x, float y);
void hud_screen_pos_reset(Hud *h, const char *name);   /* back to its authored place */
/* 0x517db0: an object's size (images: pixels) */
void hud_screen_size(Hud *h, const char *name, float w, float ht);
/* 0x50c990: draw an image with another of the package's resources (by its
   name, e.g. "U2_Menu_Arrow_White.tga"); 0 = no such object or resource */
int  hud_screen_res(Hud *h, const char *name, const char *resname);
/* every object that has `script` starts it (an FEng message such as the
   screen manager's FadeIn on show) */
void hud_screen_script_all(Hud *h, const char *script);
/* An IconScroller (0x543a90 with OPTION_MASTER / "option_") on a screen: the
 * n icons (TPK keys, from `bundle`) in a row centred on OPTION_MASTER, 40
 * apart, the selected one pulsing (0x538760), the others at 0.8 size --
 * the same strip the front end draws (shop.c). Returns the icons loaded. */
int  hud_screen_strip(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n);
void hud_screen_strip_select(Hud *h, int sel);
void hud_screen_strip_lock(Hud *h, int i, int lock);
/* bPrintf "%$d": 12,345 (0x43e950, '$' = locale digit grouping) */
void hud_num(char *out, int cap, int v);
/* 0x577240 -> 0x576d30 (English): a race time in 1/4000 s, " 1:23.45" */
void hud_time_str(char *out16, int t);
/* the same with 0x576d30's flags (1 negative, 2 '+', 4 zero template, 8 keep the last digit) */
void hud_time_fmt(char *out16, int t, int flags);
void hud_screen_draw(Hud *h, const RProg *rp, GpuMesh *quad, int width, int height, int dt_ms);
void hud_free(Hud **h);

/* Rasterise the free-roam road network (polylines through next[], with
 * per-node half widths) into the minimap's street texture, like the retail
 * radar: light roads, wider = brighter, on dark blocks. Once, after load. */
void hud_map_build(Hud *h, const float *xy, const int *next,
                   const float *half_width, int n);

/* The minimap's map (0x4ca840): TRACKS/TRACKMAP<event>.BIN's texture
 * TRACKMAP<event> on Track4000_map, windowed by the event's track record
 * (race table +0xac / +0xb0 the map's top-left corner, +0xb4 its size in
 * metres, +0xc0 the zoom: a 0.3 / zoom window of it across the panel).
 * Free roam uses the region's record (4000 ...). Loads once per event;
 * 0 when the file is missing (the street map then stays). */
int hud_set_trackmap(Hud *h, const char *dataroot, int event,
                     float x0, float y0, float size, float zoom);

/* One frame. Restores every GL state and uniform it touches. */
void hud_draw(Hud *h, const RProg *rp, GpuMesh *quad,
              const HudState *s, int width, int height);

/* Garage / pause menu: a title, rows of "LABEL   < value >", one selected,
 * and a hint line, in the game's own HUD font over a dark panel. Drawn
 * instead of the driving HUD. Strings are ASCII; the font has capitals. */
#define HUD_MENU_ROWS 10
typedef struct {
    const char *title;
    const char *label[HUD_MENU_ROWS];
    const char *value[HUD_MENU_ROWS];   /* NULL: a plain action row */
    int rows, sel;
    const char *hint;
    const char *status;                 /* optional line under the rows */
} HudMenu;
void hud_draw_menu(Hud *h, const RProg *rp, GpuMesh *quad, const HudMenu *m,
                   int width, int height);
/* One large centred line (countdown, FINISHED), optional smaller line below. */
void hud_draw_banner(Hud *h, const RProg *rp, GpuMesh *quad, const char *text,
                     const char *sub, int width, int height);

/* Clearly-labelled sample values for layout review. Off by default. */
void hud_set_preview(Hud *h, int on);
void hud_race_reset(Hud *h);   /* race start: per-race widget state of a kept race HUD */
int  hud_preview(const Hud *h);

/* Space-separated names of art that failed to load, "" when all present. */
const char *hud_missing(const Hud *h);

/* ---- pure helpers, unit-tested without GL (see tools/hud_test.c) ---- */

/* Signed angle from `from` to `to`, wrapped to (-pi, +pi]. The navigation
 * arrow rotates by this, so the wrap must be exact or the arrow spins the long
 * way round every time the car crosses the +/-pi seam. */
float hud_angle_delta(float from, float to);

/* World -> minimap transform. The map is a north-up-or-heading-up window of
 * radius `radius_m` around (cx,cy); `rot` is the rotation applied (0 for
 * north-up, -heading-pi/2 for heading-up). Writes normalised map coordinates
 * in [-1,1] where 1 = the window edge; the caller scales into its panel.
 * Points outside the window are returned unclamped so the caller can cull. */
void hud_map_project(float wx, float wy, float cx, float cy,
                     float radius_m, float rot, float *mx, float *my);

/* The secondary status line, as a pure function of the state, because the
 * rule it encodes is a correctness requirement rather than a style choice:
 * a SPRINT has no laps, so printing "LAP 1/2" there would be a fabricated
 * circuit readout. Circuits get laps, sprints get real gate progress, and
 * free roam gets nothing. Always NUL-terminates. */
void hud_status_text(const HudState *s, char *out, int cap);

/* Is the N2O section present at all? Fitted parts are removed, not greyed
 * out: an unfitted car shows no nitrous bar and the cluster closes up around
 * the gap. `have_turbo` is the matching switch for the boost dial (the
 * Tach group's 3rdperson_Turbo* sub-group of HUD_SingleRace). */
int hud_shows_nos(const HudState *s);

/* Money is hidden during a race, and shown in free roam only when a career
 * economy actually exists. Both halves are requirements, so both are tested. */
int hud_shows_money(const HudState *s);

/* Fraction of texels whose quantised colour equals their left neighbour's.
 * HUD art is flat-shaded, so this is high; a mis-decoded record is noise, so
 * it is low. Measured on the shipped packs: every usable sprite scores
 * 0.719..0.996 (HUD_CINGULAR_LOGO 0.992, MINIMAP_BORDER 0.978, the 16x16
 * MINIMAP_ICON_CAR 0.719), while ARROW_MARKERA -- which decodes to coloured
 * noise -- scores 0.192. hud_init refuses anything below HUD_ART_RUN_MIN.
 * rgb is w*h*3, top-down. */
#define HUD_ART_RUN_MIN 0.50f
float hud_art_run_fraction(const unsigned char *rgb, int w, int h);

/* Hit-test a click in NDC against the minimap panel and convert it to world
 * coordinates, so a destination can be chosen from the map with no developer
 * UI present. Returns 0, leaving the outputs alone, when the point is outside
 * the panel. The inverse of the transform hud_draw uses, same frame. */
/* Minimap panel radius in HUD units (screen height = 2.0): the 64-unit half
 * size of HUD_SingleRace's Track4000_map slot over the 240-unit half height. */
#define HUD_MAP_R (64.0f / 240.0f)

int hud_map_pick(float ndc_x, float ndc_y,
                 int width, int height, const HudState *s,
                 float *wx, float *wy);

/* Uniform-scale layout: converts a position in HUD units (a 2 x 2/aspect box
 * whose HEIGHT is always 2.0, so a unit is the same on screen at 16:9 and 4:3)
 * into NDC. anchor_x/anchor_y in [-1,1] select the screen corner to hang off.
 * This is what keeps proportions and safe margins identical across aspects. */
void hud_place(float aspect, float anchor_x, float anchor_y,
               float off_x, float off_y, float *ndc_x, float *ndc_y);

#endif
