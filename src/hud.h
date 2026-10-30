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
/* a career trigger zone's minimap element (Minimap::AddUnlockedTriggerZone
   PC 0x4ec8c0): its TrackPathZone box centre, MinimapElementType 5..0x12
   (0x4ac1e0 of the radar filter 0x500de0), a sponsor event's
   MINIMAP_ICON_CROSS (0x4ac460) and its SyncBlink script (zone +0x14:
   0 Static, 1 Normal, 2 Dramatic, 0x4ac620) */
typedef struct {
    float x, y;
    int   type, cross, blink;
} HudMapZone;

typedef struct {
    /* --- drivetrain readouts --- */
    float speed_kmh;          /* REAL: PHYS_KMH(speed) */
    float rpm, rpm_redline;   /* PROVISIONAL: the virtual AUDIO gearbox */
    int   gear;               /* <0 reverse, 0 neutral, 1..n forward */
    int   gear_out;           /* car +0x448 == 0 (gearbox mid-shift): orange gear */
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
    int   split_mode;         /* the Split Time option (player +0x14): 0 Leader 1 Lap Record 2 Best Lap 3 Last Lap */
    /* the Race / Explore Map option (0x83aa1b race, 0x83aa1c free roam):
       map_off = 0 Off (DetermineHudFeatures 0x600090 drops the minimap),
       map_static = 1 Static (Minimap::Tick 0x4ca840 turns it only at 2 Rotate) */
    int   map_off, map_static;
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
    /* career free roam: the renderable zone elements (IsRenderable 0x4ca6d0
       already applied) and Minimap::Tick's reach in window units: 10 in
       free roam with stage 0 / a world challenge / the GPS on, else 1
       (0x78e090); 0 = 1 */
    const HudMapZone *map_zones;
    int   map_zone_n;
    float map_far;
    /* MinimapWrapper::UpdateGPS 0x4c8410 (GPSManager active in career free
       roam): GetGPSStatus 0..4, the controller following a route (state 1/2),
       the target's minimap art position (GPSManager +0x20) and its reach
       (GetScreenPositionForWorldCoordinate 0x4cae60: 10 or 1) */
    int   gps_on, gps_status, gps_following;
    float gps_x, gps_y, gps_far;

    /* --- navigation --- */
    int   nav_active;         /* a destination is set AND a route was solved */
    float nav_target_x, nav_target_y;
    float nav_bearing;        /* radians, world bearing to the next route node */
    float nav_dist_m;

    /* --- message indicator (free roam only) --- */
    int   message_waiting;    /* plain HUDs' FE_SMS gate (G_SMS); the indicator HUD uses sms_* */
    /* career free roam's SMS (SMSMessageManager::Update 0x4db8c0, GC 80113978;
       0 elsewhere): sms_unread = SMSMessageQueue +0x1404 (GetNumMessagesUnread
       fast); sms_env = the newest unread message's SMSMessageData +5 category
       (UpdateSMS's loop: slots count-1 down to 1, never 0; 0 none = the mail);
       sms_stage0 = Stage0StateManager HasControl (0x502420: 0x13 holds in 2) */
    int   sms_unread, sms_env, sms_stage0;
    /* free roam (the EventIndicator's feature): engage = a challenger waits
       (event 9, TryToEngagePlayer); outrun = race mode 7 with the player's
       distance from the leader (GetDistanceFromLeader 0x4c9450, > 0
       behind), the limit (GetGetAwayDistance) and the player's standing */
    int   roam, engage, outrun, outrun_rank;
    float outrun_d, outrun_limit;
    /* the career world challenge running (DetermineHudFeatures bit 4,
       ShouldShowCountdownTimer): ShowcaseCountdown with the seconds left */
    int   wc_on;
    float wc_left;
    /* ShowcaseState::IsInMagazineMode (ShowcaseState +0x28 GC / +0x1d PC):
       1 while the photo screen UI_ShowCase_Preview (ShowcaseMagazineDialog)
       is up -- its ctor GC 800e8a08 / PC 4ce770 calls SetMagazineMode(1),
       LeaveThisScreen GC 800e9198 and the dtor GC 800e8c10 SetMagazineMode(0).
       Player::DetermineHudFeatures (GC 802158f8 / PC 600090) then returns 0
       (as it does for IsInPreviewMode, the "Control Car" option inside the
       same screen): every HUD feature is off, so the driving HUD draws
       nothing, the showcase reward indicator included (it waits, see
       hud_showcase_reward). */
    int   magazine;

    /* --- drift race: what the retail drift widgets read (NULL otherwise) --- */
    const struct HudDrift *drift;
    /* --- drag race: what the retail drag widgets read (NULL otherwise) --- */
    const struct HudDrag *drag;
    /* --- a Test Track test run: the DynoTuningStats panel (NULL otherwise) --- */
    const struct HudDyno *dyno;
    /* --- the style moment popups of HUD_SingleRace (CunningStuntSpread,
       0x4da900 -> 0x4c88e0 / 0x4da710 -> 0x4ab780); NULL: the HUD feature
       0x20000 (the player's Score option, 0x83aa67) is off --- */
    const struct HudStyle *style;

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
    int   gear_out;           /* car +0x448 == 0 (gearbox mid-shift): gear drawn 0xffff8000 */
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
    int   tuning;             /* 0x89e7e6: a Test Track run (StartCarTuningRace 0x526970) */
} HudDrag;

/* The Test Track's test run: DynoTuningStats (HUD feature 0x800 of
 * DetermineTuningHUDFeatures GC 80216658; ctor 80112aec, Setup 80112c30,
 * Update 80113208 / PC 4c9ce0) reads the player's RacingCarStats (race
 * entry +0x1a0, 0x5f7410). Times in 1/4000 s, 0 = none. */
typedef struct HudDyno {
    int   kind;               /* 0 drag, 1 drift, 2 the rest (Setup's three branches) */
    int   setting;            /* GetCurrentTuningSetting: 0 circuit, 3 street X, 4 URL (the title) */
    float burnout;            /* stats +0x28, m (-1 none) */
    int   t60;                /* stats +8: race time at 60 mph (100 km/h metric) */
    float qm_speed;           /* stats +0x10: m/s at the quarter mile, -1 none */
    int   qm_time;            /* stats +0x14 */
    float g_cur, g_max;       /* stats +0x24 / +0x20 */
    float speed;              /* |car speed| m/s (the drag panel's 72f68cc9) */
    int   best_lap;           /* RacingCar::GetBestLapTime */
    int   best_score;         /* GetBestLapScore (drift) */
    float best_drift, best_drift_s;   /* drift moment +0x44 / +0x40 */
    int   now, last_cross;    /* the race clock, GetLastStartLineCrossingTime (0 none) */
    int   new_lap;            /* RacingCar +0xd */
} HudDyno;

/* The player's style moments shown this frame (the accumulator's shown list,
 * 0x56fd10), for the SP_Trick_Point / SP_Trick_Type popups. */
typedef struct HudStyle {
    int n;
    struct {
        int index;            /* moment +0xc */
        int cont;             /* table +6 */
        int state;            /* +0x14 */
        float cur, mult;      /* +0x24, +0x1c */
        uint32_t label;       /* table +0 */
    } m[39];
    int show_pts;             /* +0x79: the player's bottle > 0 and 0x89e7e7 */
    int clock;                /* the game clock, 4000 ticks a second (0x86518c) */
} HudStyle;

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
/* a script event of the screen to the game (target -1, e.g. EXIT_COMPLETE
   0xe1fde1d1 after FORWARD): 1 and taken if it was sent */
int  hud_screen_game_msg(Hud *h, uint32_t msg);
void hud_screen_hide_h(Hud *h, uint32_t obj, int hide);
/* the screen's strings wrap at their Sw width (0x51c110, the FEng string layout); off by
   default: the screens ported before it keep their layout until checked */
void hud_screen_wrap(Hud *h, int on);
int  hud_screen_break(Hud *h, const char *name, float width, const char *txt, char (*out)[160], int max);   /* CTextScroller lines */
const char *hud_screen_string(const Hud *h, uint32_t label);      /* English.bin */
void hud_screen_script(Hud *h, const char *name, const char *script);
void hud_screen_script_once(Hud *h, const char *name, const char *script);   /* unless already running */
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
/* objects by package index (FEngFindObject once, then the FEng setters):
   -1 = no such object, the setters ignore it */
int  hud_screen_find(const Hud *h, const char *name);
int  hud_screen_find_h(const Hud *h, uint32_t obj);
void hud_screen_obj_pos(Hud *h, int o, float x, float y);      /* FEngSetCenter (absolute for a top-level object) */
void hud_screen_obj_size(Hud *h, int o, float w, float ht);    /* FEngSetSize */
void hud_screen_obj_rot(Hud *h, int o, float deg);             /* FEngSetRotationZ */
void hud_screen_obj_color(Hud *h, int o, uint32_t argb);       /* FEngSetColor 0xAARRGGBB */
void hud_screen_obj_hide(Hud *h, int o, int hide);             /* FEngSetVisible / Invisible */
void hud_screen_obj_script(Hud *h, int o, const char *script); /* FEngSetScript */
int  hud_screen_obj_msg(Hud *h, int o, const char *msg);   /* an FEng message (FEHashUpper(msg)); responses run */
void hud_screen_obj_uv(Hud *h, int o, float u0, float v0, float u1, float v1);   /* the image's uv rectangle */
void hud_screen_obj_xtex(Hud *h, int o, int which);            /* FEngSetTextureHash: 0 own, 1 MINIMAP_ICON_CROSS, 2 MINIMAP_ICON_TICK */
void hud_screen_obj_tex_of(Hud *h, int o, int src);            /* o drawn with src's texture (FEImage +0x24 copied) */
uint32_t hud_screen_obj_argb(const Hud *h, int o);             /* the authored colour */
int  hud_screen_obj_box(const Hud *h, int o, float *x, float *y, float *w, float *ht);   /* authored centre / size */
/* An IconScroller (0x543a90 with OPTION_MASTER / "option_") on a screen: the
 * n icons (TPK keys, from `bundle`) in a row centred on OPTION_MASTER, 40
 * apart, the selected one pulsing (0x538760), the others at 0.8 size --
 * the same strip the front end draws (shop.c). Returns the icons loaded. */
int  hud_screen_strip(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n);
void hud_screen_strip_select(Hud *h, int sel);
void hud_screen_strip_lock(Hud *h, int i, int lock);
int  hud_screen_strip_more(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n);   /* the missing icons from another bundle */
/* 0x50c990 with a texture from a file: the object `obj` drawn with the TPK
   texture `texname` of `path` (opaque: its alpha forced to 255, as the
   TRACKMAP textures need); up to 2 per screen. 0 = not loaded */
int  hud_screen_tex_file(Hud *h, uint32_t obj, const char *path, const char *texname, int opaque);
/* the same with the texture's bin hash (bStringHash of its name) given
   directly, e.g. a MagazineSetup's mast / extra keys in
   FRONTEND/MAGAZINES_STREAMING_{LOW,HIGH}.bin (compressed-index JDLZ TPKs).
   Calling it again for an object already overridden replaces that slot. */
int  hud_screen_tex_key(Hud *h, uint32_t obj, const char *path, uint32_t key, int opaque);
int  hud_screen_strip_tex(Hud *h, uint32_t obj, int i);   /* the object drawn with strip icon i */
/* FEngSetTextureHash for a screen: hud_screen_keys loads TPK keys from a
   bundle into the screen's own table (up to 16 in all, callable again,
   freed by hud_free; returns the number found); hud_screen_obj_key draws
   object o with key's texture -- one loaded so, else the package's own
   resource with that key; an unknown key leaves o as it is */
int  hud_screen_keys(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n);
void hud_screen_obj_key(Hud *h, int o, uint32_t key);
/* The free-roam EventIndicator's events (EventIndicator::AddEvent 0x4c9170 /
   RemoveEvent 0x4dab80) for the HUD that has it, applied on its next frame:
   EventIndicatorType 1..0x14 (career zones by career_zone_indicator, 0x14 the
   stage-0 intro with its param in s). The random encounter (9) comes from
   HudState.engage. hud_event_reset: every slot empty (a new free roam). */
void hud_event_add(int type, int param);
void hud_event_remove(int type);
void hud_event_reset(void);
/* FEngHud::ShowShowcaseReward (PC 0x4ac170, GC 80114df0) ->
   ShowcaseRewardIndicator::QueueMessage (GC 80108b90): call it where the
   showcase cash is paid (PlayerCareerState::NotifyShowcaseCompleted PC
   0x541210, from ChooseMagazineForShowcase at photo-screen entry) with the
   showcase kind (record +0x24: 1/3 magazine, 2/4 DVD), the base reward
   (record +0x26) and the star part (ftol(stars * purse + 0.005)). Queued in
   class statics (messageQueued, mMagType, baseReward, starRatingReward,
   mDelayFrames = 4); the in-game HUD's Cash_Reward string then shows
   "Bank Reward for Magazine/DVD: <base + star>" with its ZoomIn script once
   the widget's feature (hi 0x80) has been on for 4 frames -- i.e. after the
   photo screen closes (HudState.magazine back to 0). A second call before
   it shows replaces the first (one set of statics). */
void hud_showcase_reward(int kind, int base, int star);
/* FEngHud::ShowPickedUpBank: a bank zone's cash -- "Bank Reward: <cash>"
   (Cash_Bank, Cash_Animate) over the money box, UI sound 0x11 */
void hud_bank_pickup(int cash);
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
/* a polyline in FEng space (640x480 centred, y down), colour 0xAARRGGBB */
void hud_fe_lines(const RProg *rp, GpuMesh *quad, int width, int height,
                  const float *xy, int n, uint32_t argb, float thick);
/* One large centred line (countdown, FINISHED), optional smaller line below. */
void hud_draw_movie(const RProg *rp, GpuMesh *quad, GLuint tex, int tw, int th, int width, int height);
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

/* The pad glyphs the console FNGs' $JOY_EVENT_...$ prompts draw as (a
 * 64x64 RGBA texture, made on first use): the face buttons, the d-pad with
 * its up/down or left/right arms lit, the left / right stick. */
enum { HUD_PAD_CROSS, HUD_PAD_CIRCLE, HUD_PAD_TRIANGLE, HUD_PAD_SQUARE,
       HUD_PAD_DPAD_UD, HUD_PAD_DPAD_LR, HUD_PAD_LSTICK, HUD_PAD_RSTICK, HUD_PAD_N };
GLuint hud_pad_glyph(int b);

#endif
