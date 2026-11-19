/* hud.c — in-game player HUD. See hud.h for the honesty contract.
 *
 * The layout is the game's own: HUD_SingleRace.fng, read from the retail
 * bundles through feng.c, with its art and bitmap fonts. Everything is drawn
 * through the existing shader as transformed quads (sprites) or one small
 * dynamic quad batch (glyphs, the N2O sector); the HUD adds no shader. All state it touches is saved
 * and restored, so it cannot leak blending/depth/uniforms into the world pass
 * or the developer UI.
 */
#include "hud.h"
#include "feng.h"
#include "options.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------------------------------------------------------- helpers */

float hud_angle_delta(float from, float to) {
    float d = to - from;
    /* fmod alone leaves (-2pi, 2pi); fold once more to land in (-pi, pi]. */
    d = (float)fmod((double)d, 2.0 * M_PI);
    if (d <= -(float)M_PI) d += 2.0f * (float)M_PI;
    if (d >   (float)M_PI) d -= 2.0f * (float)M_PI;
    return d;
}

void hud_map_project(float wx, float wy, float cx, float cy,
                     float radius_m, float rot, float *mx, float *my) {
    if (radius_m < 1e-3f) radius_m = 1e-3f;
    float dx = (wx - cx) / radius_m, dy = (wy - cy) / radius_m;
    float c = cosf(rot), s = sinf(rot);
    *mx = dx * c - dy * s;
    *my = dx * s + dy * c;
}

void hud_status_text(const HudState *s, char *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!s || !s->racing) return;
    if (s->circuit) {
        int lap = s->lap < 1 ? 1 : s->lap, laps = s->laps < 1 ? 1 : s->laps;
        if (lap > laps) lap = laps;
        snprintf(out, (size_t)cap, "LAP %d/%d", lap, laps);
    } else {
        float p = s->sprint_progress;
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        snprintf(out, (size_t)cap, "%d PCT", (int)(p * 100.0f + 0.5f));
    }
}

int hud_shows_nos(const HudState *s) { return s && s->have_nos; }

int hud_shows_money(const HudState *s) {   /* the countdown hides PickedUpBankIndicator */
    return s && !s->racing && s->have_money && !s->wc_on;
}

float hud_art_run_fraction(const unsigned char *rgb, int w, int h) {
    if (!rgb || w < 2 || h < 1) return 1.0f;      /* nothing to judge */
    long runs = 0, cmp = 0;
    for (int y = 0; y < h; y++)
        for (int x = 1; x < w; x++) {
            const unsigned char *a = rgb + (((long)y * w) + x - 1) * 3;
            const unsigned char *b = a + 3;
            /* 5 bits per channel: ignores dithering, keeps real edges */
            if ((a[0] >> 3) == (b[0] >> 3) && (a[1] >> 3) == (b[1] >> 3) &&
                (a[2] >> 3) == (b[2] >> 3)) runs++;
            cmp++;
        }
    return cmp ? (float)runs / (float)cmp : 1.0f;
}

void hud_place(float aspect, float anchor_x, float anchor_y,
               float off_x, float off_y, float *ndc_x, float *ndc_y) {
    if (aspect < 1e-3f) aspect = 1e-3f;
    *ndc_x = anchor_x + off_x / aspect;
    *ndc_y = anchor_y + off_y;
}

/* --------------------------------------------------------------- resources */

/* The HUD is EA's own single-player layout, HUD_SingleRace.fng, read from the
 * game's bundles at start-up (see feng.h): every sprite, string, size, colour
 * and position below comes from that package, and code only drives what the
 * game drives -- needle angles, the N2O level, readout text and the live map.
 * Only the objects named here are drawn: the package also carries mode
 * screens (drag/drift readouts, getaway meter, dyno stats, online status,
 * standings, race timer) that this engine has no system behind.
 * Hashes are FEng object name hashes as tools/fng_dump.py lists them. */
#define FE_TACH_GROUP    0x5164d4eau
#define FE_TURBO_GROUP   0xc5d551b7u
#define FE_TURBO_NEEDLE  0x6d5ece44u
#define FE_TURBO_RED_1   0x1a33cbb5u   /* 3rdperson_TurboRed_1 */
#define FE_TURBO_RED_2   0x1a33cbb6u   /* 3rdperson_TurboRed_2 */
#define FE_RPM_NEEDLE    0xf0250dacu
#define FE_SPEED         0xc1d5ff50u
#define FE_GEAR          0xb84589beu
#define FE_SPEED_UNITS   0xc3383b63u   /* the tach's "KM/H" (0x4c68d0: MPH 0x8569ab44 / KM/H 0x8569a25f) */
#define FE_SHIFT         0x25cf2f74u
/* NitrousGauge 0x4f6a90 (GC 8010d210): FEHashUpper names */
#define FE_NOS_TRACK     0x11495cd6u   /* NOS_Line (+0x20): the first bottle, full, shown above one */
#define FE_NOS_BACKING   0x11461255u   /* NOS_Fill: untouched by the gauge */
#define FE_NOS_FILL      0x39e180c4u   /* NOS_GameBreaker (+0x24): the level under NOS_Mask */
#define FE_MAP_MASK      0x40cdc515u   /* the dark disc = panel body */
#define FE_MAP_BORDER    0x382d2fc9u   /* group: ring quadrant x4 */
#define FE_MAP_SLOT      0x90fe80f3u   /* Track4000_map: where the live map goes */
#define FE_MAP_NORTH     0xf2a54430u
#define FE_MAP_PLAYER    0xeb24e169u
#define FE_MAP_GPS       0x4f649313u
#define FE_MAP_GATE      0x7af1ca63u
#define FE_RACE_CORNER   0x411301e3u
#define FE_POS_GROUP     0xe8434eb4u
#define FE_POS_NUM       0x9c183bf8u
#define FE_POS_SUFFIX    0x44f24239u
#define FE_POS_OF        0x3bbd6268u
#define FE_LAP_GROUP     0x70f90bd4u
#define FE_LAP_LABEL     0x8b7e6ea1u
#define FE_LAP_NUM       0x57f0e3a5u
#define FE_LAP_OF        0xf2685238u
#define FE_LAP_BOX       0xa86c3b9bu   /* HUD_Drift's 3rdperson_BoxLaps, top level */
#define FE_BANK_GROUP    0x3f8644cau
#define FE_BANK_VALUE    0x1930b057u
#define FE_SMS           0x4b9b8f5au
#define FE_WRONG_WAY     0xc82fa200u   /* icon_Wrongway: hidden until code shows it */
#define FE_COUNTDOWN     0x2da220aau   /* the countdown string (widget 0x4f7340 / 0x4ab5b0) */

#define HUD_PACKAGE      "HUD_SingleRace.fng"
#define HUD_ARROW_KEY    0x985c190cu   /* ARROW_MARKERA (InGameCommon) */
#define HUD_CROSS_KEY    0xb90f50bbu   /* MINIMAP_ICON_CROSS (InGameCommon) */
#define HUD_TICK_KEY     0x62bc0dfcu   /* MINIMAP_ICON_TICK (InGameCommon): the world map's checkboxes */

/* Visibility gates: a drawn object shows only while its gate is open. */
enum { G_ALWAYS, G_TURBO, G_NOS, G_RACE, G_POS, G_LAPS, G_BANK, G_SMS, G_COUNT };
/* Objects whose drawing code overrides the authored state. */
enum { R_NONE, R_RPM_NEEDLE, R_TURBO_NEEDLE, R_SPEED, R_GEAR, R_SHIFT,
       R_NOS_FILL, R_MAP_SLOT, R_MAP_NORTH, R_POS_NUM, R_POS_SUFFIX, R_POS_OF,
       R_LAP_LABEL, R_LAP_NUM, R_LAP_OF, R_BANK_VALUE, R_COUNTDOWN, R_UNITS,
       R_ANIM, R_NOS_LINE, R_CORNER };   /* drift HUD: a ported widget's object, drawn from its FEng state */

/* Measured dial geometry: the rev scale is the shipped 3rdperson_10500lines
 * art, whose "0" sits straight below the hub and whose numerals advance
 * 22.5 degrees clockwise per 1000 rpm ("8" straight up); the needle art
 * points down at angle 0. Checked by rendering the needle at 0/4000/8000 rpm
 * over the shipped dial with tools/fng_dump.py. */
#define RPM_DEG_PER_1000  22.5f
#define RPM_SCALE_MAX     10500.0f
/* the turbo needle reads "0" at its authored 123 degrees (upper left); -1
   (full vacuum, "-30") is the red zone at lower left, +1 ("30") the red zone
   at upper right. Checked by rendering the needle at 0..240 degrees over the
   shipped 3rdperson_Turbolines dial with tools/fng_dump.py. */
#define BOOST_DEG_ZERO    123.0f
#define BOOST_DEG_SPAN    97.0f

#define HUD_NFONT 4
#define HUD_QMAX  64          /* quads per batch: one string or one N2O sector */

/* Arc geometry MEASURED from the shipped Tach_NOS_Alpha art at load time, in
 * art-normalised coordinates (fractions of the art's WIDTH, y up from its
 * bottom edge). Drives the N2O level sector when a tank model exists. */
struct HudArc {
    int   ok;
    float cx, cy, r;      /* fitted circle */
    float a0, a1;         /* sweep start/end in radians, a0 = the low end */
};

typedef struct { GLuint tex; int w, h; unsigned char additive; } HudTex;
typedef struct { GLuint tex; int aw, ah, ok; FengFont f; } HudFont;
/* a decoded image waiting for the GL thread: its texture goes to *dst
   (and *ok = tex != 0 for a font) -- hud_screen_prefetch */
struct HudPendUp { GLuint *dst; int *ok; FengImage im; };

struct Hud {
    FengPackage *pk;                   /* NULL: package missing, arrow only */
    HudTex  rt[FENG_MAX_RES];          /* per package resource */
    HudFont font[HUD_NFONT];
    signed char font_of[FENG_MAX_RES]; /* resource -> font slot, -1 none */
    short   draw[FENG_MAX_OBJ];        /* drawn objects, far to near */
    int     ndraw;
    unsigned char role[FENG_MAX_OBJ], gate[FENG_MAX_OBJ];
    unsigned char tach[FENG_MAX_OBJ];   /* in the tach group (the Gauges option) */
    unsigned char mmap[FENG_MAX_OBJ];   /* the minimap (the Race / Explore Map option) */
    int     o_player, o_gps, o_gate, o_track, o_wrong;
    int     o_bank_label;          /* "Bank:" (0x31b0d166), the free-roam box's left reach */
    struct HudArc nos_arc;
    GLuint  arrow_tex;
    int     preview;
    char    missing[512];
    char    dataroot[512];             /* for the string table (lap box label) */
    /* smoothed display state: a needle that snaps looks broken, and a dial fed
       straight from a 60 Hz sample jitters on every gear change. */
    float   needle_rpm, needle_speed, nos_glow, arrow_angle;
    int     tach_cls;              /* the dial art loaded (0x60fe10 class + 1; 0 = the package default) */
    float   tach_sweep, tach_lim;  /* Tachometer +0xc4: limiter / scale * 235 deg; the limiter */
    int     arrow_primed;
    /* street map: luminance texture over [map_x0, map_x0+map_ext] etc. and a
       circular fan whose texture coordinates are rewritten every frame */
    GLuint  map_tex;
    float   map_x0, map_y0, map_ext;
    /* the event's TRACKMAP (0x4ca840) and the indicator objects 0x4db900
       hands out: AIRACERINDICATOR_0..7, STARTLINEINDICATOR_0, the first
       PLAYERCARINDICATOR in the package */
    GLuint  tm_tex;
    int     tm_event;
    float   tm_x0, tm_y0, tm_size, tm_zoom;
    int     o_aiind[8], o_startind, o_playerind;
    /* MinimapEntityNames (PC 0x7f6828) for types 5..0x12: <NAME>INDICATOR_%d,
       each type's own pool (AllocateMinimapEntity: the free ones in order) */
    int     o_zind[14][9];
    GLuint  cross_tex;                 /* MINIMAP_ICON_CROSS (InGameCommon 0xb90f50bb) */
    GLuint  tick_tex;                  /* MINIMAP_ICON_TICK (0x62bc0dfc) */
    GLuint  fan_vbo, fan_nbo, fan_ibo;
    GLuint  qb_vbo, qb_nbo, qb_ibo;    /* dynamic quads: glyphs, N2O sector */
    struct HudDriftW *dw;              /* drift HUD: scripts + widget state */
    /* built off the GL thread (defer): the textures wait in pend until
       hud_pend_upload; the quad batch is made then too */
    struct HudPendUp *pend;
    int npend, cappend, upi, defer;
};

/* The drift HUD (HUD_Drift.fng; HUD object 0x4f8bf0). The package's FEng
 * scripts run (feng_anim_*), and the objects of the widgets ported so far are
 * drawn from that live state with the strings the widgets write:
 *   0x4f6c00 / 0x4da590  drift angle, too slow / losing multiplier, the drift
 *                        title comments, and the running drift's points and
 *                        title (its popup node at +0x20, filled by 0x4c88e0);
 *   0x4f7ad0 / 0x4da900 -> 0x4c85d0  total, multiplier, points to the next
 *                        multiplier, best drift; 0x4ab780 the popup.
 * Objects of widgets not ported yet (leaderboard, lap timer, split time,
 * hurry, countdown, race-over message) are not drawn. */
#define EI_ENV  (2 + 20)
#define EI_NTEX (EI_ENV + 8)
struct HudDriftW {
    FengAnim anim;
    short res_ov[FENG_MAX_OBJ];   /* a screen's code-set image: resource + 1 (0x50c990) */
    unsigned char xt[FENG_MAX_OBJ];    /* FEngSetTextureHash by key: 1 MINIMAP_ICON_CROSS, 2 MINIMAP_ICON_TICK */
    unsigned char uvo[FENG_MAX_OBJ];   /* the image's code-set uv window (data words 17..20) is drawn */
    /* strings the widgets set, each its own length (English.bin runs to
       931 characters; a fixed 48 cut the post-race dialog's question) */
    char   *txt[FENG_MAX_OBJ];
    unsigned short txtcap[FENG_MAX_OBJ];
    unsigned char has_txt[FENG_MAX_OBJ];
    unsigned char wl[FENG_MAX_OBJ];      /* a ported widget's object */
    unsigned char *lang; uint32_t *sh; const char **st; int nstr;   /* English.bin */
    int turbo;
    /* 0x4f6c00 */
    int o_slow, o_wwc, o_comment, o_angle, o_angle_deg, o_back1, o_back3, o_point, o_title;
    struct { int cont; float pts; int chain, time; uint32_t label; int fade, state, failed; } node;
    int node_on;                        /* +0x40 */
    int comment_on, comment_t;          /* +0x95, +0x98 */
    /* 0x4f7ad0 */
    int o_back03, o_total, o_total_title, o_box01, o_next_ind, o_mult, o_next_pts,
        o_best_back, o_best_text, o_best;
    uint32_t c60, c64, c68;             /* +0x60 +0x64 +0x68 colours */
    float f70;                          /* +0x70 blink scale */
    int t74, b78, lvl7c, b79;
    int screen;                         /* a full-screen package: no edge pinning (2: left edge, PC_Loading) */
    int wrap;                           /* strings wrap at their Sw width (hud_screen_wrap) */
    /* 0x4f7170 / 0x4c7f00: RaceOverMessage (+0x20), RaceOverBacking (+0x24),
       the feature on (0x4c6690), its scripts started (+0x28) */
    int o_ro_msg, o_ro_back, ro_on, ro_started;
    /* an IconScroller strip on a screen (hud_screen_strip) */
    GLuint strip_tex[16]; unsigned char strip_add[16], strip_lock[16];
    int strip_n, strip_sel, strip_master, strip_ms;
    float strip_scroll, strip_pulse;
    /* 0x4f6420 / 0x4aafc0: WrongWayImage (+0x20), WrongWay_CountDown (+0x24),
       faded in (+0x28) */
    int o_ww, o_wwc2, ww_in;
    /* CunningStuntSpread (0x4f7ad0) outside drift: SP_Trick_Point (+0x34),
       SP_Trick_Type (+0x38) and the popup node list (+0x20, nodes 0x2c
       bytes from 0x4c7e20) */
    int o_stp, o_stt;
    struct { int cont; float pts; int chain, index, time; uint32_t label; int fade, state, failed; } sn[16];
    int nsn;
    /* 0x4f5c40: CurrentLapTime (+0x2c), Lap_Time_Title / Text (+0x34 / +0x38);
       +0x54 / +0x58 / +0x5c / +0x64 / +0x65 */
    int o_lt, o_ltt, o_ltx, lt54, lt58, lt5c, lt64, lt65;
    /* 0x4f6030: SplitTimeText (+0x20); +0x28 shown, +0x2c since */
    int o_sp, sp28, sp2c;
    /* 0x4f6130: rows 1..4 LBData / LeaderText / LBTextBacking / LBBacking;
       +0x1bc the view's start, +0x1c0 the names view */
    int o_lbd[4], o_lbt[4], o_lbtb[4], o_lbb[4], lb1bc, lb1c0;
    /* the drag HUD (HUD_Drag.fng): 0x4f6500 / 0x4c7090 the drag tach --
       RPM_fill, 3rdPersonTachRPMNumbers (+0x2c), RPM_Redline (+0x28),
       3rdPersonNeedle (+0x20), 3rdPersonGear (+0x70), ShiftMessage (+0x74),
       3rdPersonSpeedUnits (+0x78), 3rdPersonSpeed (+0x7c),
       Drag_Position_Backing_03, Shift_light (+0x30) */
    int drag, drag_on;                  /* drag_on: the features' state (0x4c6690), -1 unset */
    int o_rpmfill, o_rpmnum, o_redline, o_needle, o_gear, o_shmsg, o_units, o_speed, o_pb03, o_shlight;
    int g80, g84;                       /* +0x80 last frame's gear, +0x84 this frame's */
    unsigned char f88, f89, f8a, f8b, f8c, f8d, f8e, fa4;   /* message / blink latches; +0xa4 no scale art */
    uint32_t c98, c9c;                  /* the gear's and the needle's authored colours */
    HudTex ov_num, ov_red;              /* the rev scale and redline art by the engine (0x5ed9b0) */
    /* 0x4f69e0 / 0x4c7aa0: HEAT_Bar (+0x20) */
    int o_heat;
    /* 0x4f6a90 / 0x4c7b20: NOS_Line (+0x20), NOS_GameBreaker (+0x24), NOS_Fill */
    int o_nosline, o_nosgb, o_nosfill;
    /* 0x4f5a70 / 0x4c6b80: 3rdperson_Turbofill / _Turbolines / _TurboDial (+0x28) / _TurboRed_1 / _2 */
    int o_tb[5];
    /* 0x4f4ac0 / 0x4ebb10: RacePositionNumber (+0x28), RacePositionNth (+0x2c),
       NumberRacers (+0x30), RaceTime (+0x38), PositionBacking (+0x3c),
       Drag_Position_Backing_01 / _02; +0x4c the last lap event, +0x50 the time shown */
    int o_pnum, o_pnth, o_pof, o_ptime, o_pback, o_pb01, o_pb02, p4c, p50;
    /* free roam: GetAwayMeter (0x4f87e0) +0x44 frame count, +0x24 last
       distance; EventIndicator (0x4f83d0) one event slot (+0x60: state,
       its clock), the D-pad icon lit, the swapped art */
    int o_gw[7], o_ei[6];
    int o_sc[4], sc_on;           /* ShowcaseCountdown: group 30d59b88 + its registered objects */
    int o_cr;                     /* ShowcaseRewardIndicator: Cash_Reward 49106ce2 (+0x20) */
    int o_cb;                     /* PickedUpBankIndicator: Cash_Bank (+0x20) */
    int o_gps[3], o_gpst[2];      /* GPS_Search_Group, GPS_SEARCH_Text, GPS_SEARCH_ERROR; the target group's selectors */
    int gps_edge;                 /* MinimapWrapper +0x35: the last status was 3 */
    int gw44, gw_was, ei_lit, gw_ok;
    float gw24;
    /* EventIndicator +0x60: 8 EngageEvents (0x14 each: +0 state, +1 type,
       +4 the start time, +0x10 AddEvent's param); the clock (ms); the
       random encounter's last engage (NotifyRandomEncounterCar); the type
       whose art [5] shows (FEngSetTextureHash keeps it) */
    struct { signed char state, type; int t0, param; } ei[8];
    int ei_ms, ei_eng9, ei_icon;
    HudTex ei_tex[EI_NTEX];   /* [0] the D-pad icon lit, [1] unlit, [2 + type] GetEventTypeTexHash,
                                 [EI_ENV + cat] the SMS envelopes (0 = ENGAGE_SMS_MAIL) */
    /* the SMS: [6] Engage_SMS_mail 4b9b8f5a / [7] its count 668c9cd6 (+0x38 / +0x3c);
       [6]'s FEngSetTextureHash (EI_ENV index, kept); SMSMessageManager +0 last
       unread; this frame's newest-unread category and Stage0 HasControl */
    int o_sms[2], sms_img, sms_last, sms_env, sms_s0;
    /* a screen's FEngSetTextureHash by key (hud_screen_keys / _obj_key):
       the loaded keys, and per object its texture + 1 */
    uint32_t sk_key[16]; HudTex sk_tex[16]; int sk_n;
    unsigned char sk_o[FENG_MAX_OBJ];
    /* a screen object's image swapped for a texture from a file (the engage
       dialog's TRACKMAP<event>) */
    int tov_obj[2];
    HudTex tov[2];
    unsigned char tov_borrow[2];        /* tov[k].tex is a strip icon's (hud_screen_strip_tex): not ours to delete */
    /* DynoTuningStats (the Test Track's test run): group d292009b, the
       registered objects shown with the feature (dy_reg), the message
       d1b15e68 (+0x34); ShowDragTuningMessage's once flags (+0x30 / +0x32 /
       +0x33); CurrentTimeDisplay's lap stamp / held time (+0x34 / +0x38) */
    int o_dy, o_dy_msg, dy_on, dy_kind;
    unsigned char dy_reg[FENG_MAX_OBJ];
    unsigned char dy_m0, dy_m2, dy_m3;
    int dy_t34, dy_t38;
};

#define MAP_FAN 48            /* rim vertices of the clipped street map */
#define MAP_TEX 2048          /* street texture size (luminance, 4 MB) */

/* The minimap panel in FEng units: the Track4000_map slot's centre and half
 * size, and the screen edge it is pinned to. hud_init copies them from the
 * loaded package; these defaults ARE those numbers, so hud_map_pick agrees
 * with the draw even before (or without) a load. */
static float g_map_fx = -223.0f, g_map_fy = 142.0f, g_map_half = 64.0f;
static int   g_map_pin = -1;

/* Widescreen: FEng lays the HUD out on 640x480. Everything keeps its 4:3
 * size (one FEng unit = 1/480 of the screen height) and hangs off the edge
 * its top-level group sits nearest, so the minimap stays bottom-left and the
 * cluster bottom-right at 16:9 instead of drifting inward or stretching. */
static int hud_pin_of(float ax) { return ax < -106.0f ? -1 : ax > 106.0f ? 1 : 0; }
/* a whole screen's pin: centred 4:3, or the left edge (screen 2) */
/* -2: PC_Loading's strip centred under the widescreen art's logo
   (hud_wide_loading_pin) */
#define screen_pin(w) ((w)->screen == 2 ? (hud_wide_loading_tex() ? -2 : -1) : 0)

static void fe_ndc(float asp, int pin, float fx, float fy, float *nx, float *ny) {
    float p = pin == -2 ? hud_wide_loading_pin(asp) : (float)pin;
    *nx = p + (fx - p * 320.0f) / (240.0f * asp);
    *ny = -fy / 240.0f;
}

/* Least-squares circle through the arc's opaque texels (Kasa fit), plus the
 * angular sweep taken from the largest empty gap around that circle. Both come
 * from the art, so the level follows the scale the art actually draws. */
static void hud_measure_arc(const FengImage *t, struct HudArc *out) {
    out->ok = 0;
    if (!t->rgba || t->w < 8 || t->h < 8) return;
    #define ARC_A(x, y) (t->rgba[(((long)(y) * t->w) + (x)) * 4 + 3])
    double Sxx=0,Sxy=0,Syy=0,Sx=0,Sy=0,Sxz=0,Syz=0,Sz=0; long n=0;
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            if (ARC_A(x, y) < 160) continue;
            /* normalise by WIDTH on both axes so the fit stays circular, and
               flip y so it runs up from the art's bottom edge */
            double px = (x + 0.5) / (double)t->w;
            double py = (t->h - 0.5 - y) / (double)t->w;
            double z = px*px + py*py;
            Sxx += px*px; Sxy += px*py; Syy += py*py;
            Sx += px; Sy += py; Sxz += px*z; Syz += py*z; Sz += z; n++;
        }
    if (n < 64) return;
    double N = (double)n;
    /* normal equations for z + D*px + E*py + F = 0 */
    double A[3][4] = {
        { Sxx, Sxy, Sx, -Sxz },
        { Sxy, Syy, Sy, -Syz },
        { Sx,  Sy,  N,  -Sz  },
    };
    for (int c = 0; c < 3; c++) {                 /* Gaussian elimination */
        int piv = c;
        for (int r2 = c + 1; r2 < 3; r2++)
            if (fabs(A[r2][c]) > fabs(A[piv][c])) piv = r2;
        if (fabs(A[piv][c]) < 1e-12) return;
        if (piv != c) for (int k = 0; k < 4; k++) {
            double tmp = A[c][k]; A[c][k] = A[piv][k]; A[piv][k] = tmp; }
        for (int r2 = 0; r2 < 3; r2++) {
            if (r2 == c) continue;
            double f = A[r2][c] / A[c][c];
            for (int k = c; k < 4; k++) A[r2][k] -= f * A[c][k];
        }
    }
    double D = A[0][3] / A[0][0], E = A[1][3] / A[1][1], F = A[2][3] / A[2][2];
    double cx = -D * 0.5, cy = -E * 0.5;
    double rr = cx*cx + cy*cy - F;
    if (rr <= 1e-9) return;
    double r = sqrt(rr);
    if (r < 0.05 || r > 4.0) return;

    /* angular occupancy, 1-degree bins, to find where the arc is NOT */
    unsigned char bin[360]; memset(bin, 0, sizeof bin);
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            if (ARC_A(x, y) < 160) continue;
            double px = (x + 0.5) / (double)t->w;
            double py = (t->h - 0.5 - y) / (double)t->w;
            double a = atan2(py - cy, px - cx) * 180.0 / M_PI;
            if (a < 0) a += 360.0;
            int b = (int)a; if (b >= 0 && b < 360) bin[b] = 1;
        }
    #undef ARC_A
    int best_len = 0, best_start = -1, run = 0, run_start = 0;
    for (int i = 0; i < 720; i++) {          /* two turns: handles the wrap */
        int b = i % 360;
        if (!bin[b]) { if (!run) run_start = b; run++;
                       if (run > best_len && run <= 360) { best_len = run; best_start = run_start; } }
        else run = 0;
    }
    if (best_len < 20 || best_len > 340) return;   /* not an arc */
    /* the sweep is the complement of the gap, travelled clockwise */
    double gap_end = (best_start + best_len) % 360;      /* arc CCW start */
    double gap_start = best_start;                       /* arc CCW end   */
    double a1 = gap_end, a0 = gap_start;
    while (a0 < a1) a0 += 360.0;                 /* a0 (empty) is CW-first */
    out->cx = (float)cx; out->cy = (float)cy; out->r = (float)r;
    out->a0 = (float)(a0 * M_PI / 180.0);
    out->a1 = (float)(a1 * M_PI / 180.0);
    out->ok = 1;
}

/* Upload one decoded RGBA image with HUD-appropriate sampling. Clamping
   matters: these are sprites on a transparent field, and REPEAT bleeds the
   opposite edge into the border of every rotated element. */
static GLuint hud_upload(const FengImage *t) {
    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) return 0;
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, t->rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}

/* The texture for *dst: uploaded now, or -- a Hud built off the GL thread
   (h->defer) -- the decoded image kept (taken from *im) for hud_pend_upload.
   ok, if given: a font's ok flag, set to tex != 0 once uploaded. */
static void hud_upload_to(Hud *h, GLuint *dst, FengImage *im, int *ok) {
    if (!h->defer) {
        *dst = hud_upload(im);
        if (ok) *ok = *dst != 0;
        return;
    }
    if (h->npend == h->cappend) {
        int cap = h->cappend ? h->cappend * 2 : 32;
        struct HudPendUp *np = (struct HudPendUp *)realloc(h->pend, (size_t)cap * sizeof *np);
        if (!np) return;                /* as a failed upload: no texture */
        h->pend = np; h->cappend = cap;
    }
    struct HudPendUp *p = &h->pend[h->npend++];
    p->dst = dst; p->ok = ok; p->im = *im;
    im->rgba = NULL;                    /* the caller's feng_image_free leaves it */
}
/* GL thread: up to `budget` bytes of the waiting images uploaded (at least
   one); 1 when none wait any more */
static int hud_pend_upload(Hud *h, long budget) {
    long done = 0;
    while (h->upi < h->npend && (done == 0 || done < budget)) {
        struct HudPendUp *p = &h->pend[h->upi++];
        *p->dst = hud_upload(&p->im);
        if (p->ok) *p->ok = *p->dst != 0;
        done += (long)p->im.w * p->im.h * 4;
        feng_image_free(&p->im);
    }
    if (h->upi < h->npend) return 0;
    free(h->pend); h->pend = NULL;
    h->npend = h->cappend = h->upi = 0;
    return 1;
}

static void hud_note_missing(Hud *h, const char *what) {
    size_t used = strlen(h->missing);
    snprintf(h->missing + used, sizeof h->missing - used, "%s%s",
             used ? " " : "", what);
}

/* Which objects are drawn, behind which gate, with which override. */
static void hud_classify(Hud *h) {
    const FengPackage *p = h->pk;
    int tach = feng_find(p, FE_TACH_GROUP), turbo = feng_find(p, FE_TURBO_GROUP);
    int mask = feng_find(p, FE_MAP_MASK), border = feng_find(p, FE_MAP_BORDER);
    int slot = feng_find(p, FE_MAP_SLOT), north = feng_find(p, FE_MAP_NORTH);
    int corner = feng_find(p, FE_RACE_CORNER), pos = feng_find(p, FE_POS_GROUP);
    int laps = feng_find(p, FE_LAP_GROUP), bank = feng_find(p, FE_BANK_GROUP);
    int sms = feng_find(p, FE_SMS);
    static const struct { uint32_t hash; unsigned char role; } roles[] = {
        { FE_RPM_NEEDLE, R_RPM_NEEDLE }, { FE_TURBO_NEEDLE, R_TURBO_NEEDLE },
        { FE_SPEED, R_SPEED }, { FE_GEAR, R_GEAR }, { FE_SHIFT, R_SHIFT },
        { FE_NOS_FILL, R_NOS_FILL }, { FE_NOS_TRACK, R_NOS_LINE }, { FE_MAP_SLOT, R_MAP_SLOT },
        { FE_MAP_NORTH, R_MAP_NORTH }, { FE_POS_NUM, R_POS_NUM },
        { FE_POS_SUFFIX, R_POS_SUFFIX }, { FE_POS_OF, R_POS_OF },
        { FE_LAP_LABEL, R_LAP_LABEL }, { FE_LAP_NUM, R_LAP_NUM },
        { FE_LAP_OF, R_LAP_OF }, { FE_BANK_VALUE, R_BANK_VALUE }, { FE_SPEED_UNITS, R_UNITS },
    };
    h->ndraw = 0;
    for (int i = 0; i < p->nobj; i++) {
        const FengObj *o = &p->obj[i];
        h->role[i] = R_NONE; h->gate[i] = G_ALWAYS;
        h->tach[i] = tach >= 0 && feng_inside(p, i, tach);
        h->mmap[i] = i == mask || i == slot || i == north || (border >= 0 && feng_inside(p, i, border));
        int in = (tach >= 0 && feng_inside(p, i, tach)) || i == mask || i == slot ||
                 (border >= 0 && feng_inside(p, i, border)) || i == north ||
                 i == corner || (pos >= 0 && feng_inside(p, i, pos)) ||
                 (laps >= 0 && feng_inside(p, i, laps)) ||
                 (bank >= 0 && feng_inside(p, i, bank)) || i == sms;
        /* HUD_Drift keeps the lap readout at top level, not in a group */
        int lap_obj = laps < 0 && (o->hash == FE_LAP_LABEL || o->hash == FE_LAP_NUM ||
                                   o->hash == FE_LAP_OF || o->hash == FE_LAP_BOX);
        if (lap_obj) in = 1;
        if (o->hash == FE_COUNTDOWN && o->type == FENG_STRING && !(h->dw && h->dw->screen)) {
            h->role[i] = R_COUNTDOWN;   /* HUD_SingleRace and HUD_Drift both have it */
            int k = h->ndraw++;
            while (k > 0 && p->obj[h->draw[k - 1]].az < o->az) { h->draw[k] = h->draw[k - 1]; k--; }
            h->draw[k] = (short)i;
            continue;
        }
        if (h->dw && h->dw->wl[i] && o->type != FENG_GROUP) {
            h->role[i] = R_ANIM;
            int k = h->ndraw++;
            /* ties: an earlier object is in front (Chyron_FE: the ring (0) over
               the logo (5) over the bar pieces (6..8), all at z 100) */
            while (k > 0 && p->obj[h->draw[k - 1]].az <= o->az) { h->draw[k] = h->draw[k - 1]; k--; }
            h->draw[k] = (short)i;
            continue;
        }
        if (!in || o->type == FENG_GROUP) continue;
        for (size_t r = 0; r < sizeof roles / sizeof roles[0]; r++)
            if (o->hash == roles[r].hash) h->role[i] = roles[r].role;
        /* a placeholder the game reveals by script stays hidden unless code
           drives it */
        if (o->rgba[3] <= 0.0f && h->role[i] == R_NONE) continue;
        /* the turbo widget (0x4f5a70) owns _TurboRed_1 / _2 by name: HUD_Short_Track
           has _TurboRed_2 outside the turbo group (a red tick by the tach) */
        if ((turbo >= 0 && feng_inside(p, i, turbo)) || o->hash == FE_TURBO_RED_1 ||
            o->hash == FE_TURBO_RED_2) h->gate[i] = G_TURBO;
        else if (o->hash == FE_NOS_TRACK || o->hash == FE_NOS_BACKING ||
                 o->hash == FE_NOS_FILL) h->gate[i] = G_NOS;
        /* 3rdperson_BoxCorner (0x411301e3): always drawn. Nothing in SPEED2.EXE names
           its hash, and it is authored visible (alpha 255, not mode-hidden), so free
           roam keeps it behind "Bank:" too (it was gated to races before 11-13). */
        else if (i == corner) { h->gate[i] = G_ALWAYS; h->role[i] = R_CORNER; }
        else if (pos >= 0 && feng_inside(p, i, pos)) h->gate[i] = G_POS;
        else if ((laps >= 0 && feng_inside(p, i, laps)) || lap_obj) h->gate[i] = G_LAPS;
        else if (bank >= 0 && feng_inside(p, i, bank)) h->gate[i] = G_BANK;
        else if (i == sms) h->gate[i] = G_SMS;
        /* painter's order: farthest (largest z) first, authored order on ties */
        int k = h->ndraw++;
        while (k > 0 && p->obj[h->draw[k - 1]].az < o->az) { h->draw[k] = h->draw[k - 1]; k--; }
        h->draw[k] = (short)i;
    }
    h->o_player = feng_find(p, FE_MAP_PLAYER);
    h->o_bank_label = feng_find(p, 0x31b0d166u);
    h->o_gps = feng_find(p, FE_MAP_GPS);
    h->o_gate = feng_find(p, FE_MAP_GATE);
    h->o_wrong = feng_find(p, FE_WRONG_WAY);
    for (int k = 0; k < 8; k++) {
        char nm[32]; snprintf(nm, sizeof nm, "AIRACERINDICATOR_%d", k);
        h->o_aiind[k] = feng_find(p, feng_hash_upper(nm));
    }
    h->o_startind = feng_find(p, feng_hash_upper("STARTLINEINDICATOR_0"));
    h->o_playerind = -1;
    for (int k = 0; k < 8 && h->o_playerind < 0; k++) {
        char nm[32]; snprintf(nm, sizeof nm, "PLAYERCARINDICATOR_%d", k);
        h->o_playerind = feng_find(p, feng_hash_upper(nm));
    }
    {
        static const char *const zn[14] = { "SHOWCASE", "CRIB", "PAINTSHOP", "PARTSSHOP", "ICESHOP",
            "PERFORMANCESHOP", "CARLOT", "CIRCUITEVENT", "DRAGEVENT", "STREETEVENT", "DRIFTEVENT",
            "SPRINTEVENT", "URLEVENT", "SUVEVENT" };
        for (int t = 0; t < 14; t++) for (int k = 0; k < 9; k++) {
            char nm[40]; snprintf(nm, sizeof nm, "%sINDICATOR_%d", zn[t], k);
            h->o_zind[t][k] = feng_find(p, feng_hash_upper(nm));
#ifdef OPENUG2_SIM_PRE_H
            if (k == 0 && h->o_zind[t][0] >= 0 && getenv("OPENUG2_HUD_DUMP")) {
                const FengObj *zo = &p->obj[h->o_zind[t][0]];
                printf("hud zone icon %s: res %s size %.0fx%.0f rgba %.2f %.2f %.2f %.2f\n", nm,
                       zo->res >= 0 && zo->res < p->nres ? p->res[zo->res].name : "-", zo->size[0], zo->size[1],
                       zo->rgba[0], zo->rgba[1], zo->rgba[2], zo->rgba[3]);
            }
#endif
        }
    }
    h->o_track = feng_find(p, FE_NOS_TRACK);
    /* a whole screen's 0x90fe80f3 (the world map's map image) is not the HUD's
       minimap slot: the globals stay the race HUD's */
    if (slot >= 0 && !(h->dw && h->dw->screen)) {
        const FengObj *o = &p->obj[slot];
        g_map_fx = o->ax; g_map_fy = o->ay;
        g_map_half = 0.5f * fabsf(o->size[0]);
        g_map_pin = hud_pin_of(p->obj[o->root].ax);
    }
}

/* Every texture the drawn objects name, from the packs that ship them. The
   tach face, needle and rev scale are the player-selectable gauge style:
   HUD_CustomTextures_NN.bin names them <NAME>_CUSTOM_NN (style 00 here). */
static const char *HUD_PACKS[] = {
    "GLOBAL/InGameRace.bun",
    "GLOBAL/InGameCommon.bun",
    "GLOBAL/HUD_CustomTextures_%02d.bin",   /* the gauge style */
};
static int g_hud_style;                   /* HUDINDEX, 0 = STOCK */
static unsigned g_hud_col[3];             /* backing, dial, needle: 0x01RRGGBB, 0 = white */
int hud_custom(int style, const unsigned col[3]) {
    if (style < 0 || style > 10) style = 0;
    int changed = style != g_hud_style || memcmp(col, g_hud_col, sizeof g_hud_col);
    g_hud_style = style; memcpy(g_hud_col, col, sizeof g_hud_col);
    return changed;
}
/* 0x4f5254: the tach objects painted with the gauge colours (0x51d860 =
   FEObject SetColor, so the colour replaces the authored one): backing
   (slot 0xa4) tach_fill + 3rdperson_Turbofill, dial (0xa6)
   3rdPersonTachRPMNumbers + 3rdperson_Turbolines, needle (0xa5)
   3rdPersonNeedle + 3rdperson_TurboDial. No colour part = white. */
static void hud_paint_gauges(FengPackage *p) {
    static const char *const nm[6] = { "tach_fill", "3rdperson_Turbofill", "3rdPersonTachRPMNumbers",
                                       "3rdperson_Turbolines", "3rdPersonNeedle", "3rdperson_TurboDial" };
    int n = 0;
    for (int k = 0; k < 6; k++) {
        int o = feng_find(p, feng_hash_upper(nm[k]));
        if (o < 0) continue;
        unsigned c = g_hud_col[k / 2];
        int r = c ? (int)(c >> 16 & 0xff) : 255, g = c ? (int)(c >> 8 & 0xff) : 255, b = c ? (int)(c & 0xff) : 255;
        p->obj[o].rgba[0] = (float)r / 255.0f; p->obj[o].rgba[1] = (float)g / 255.0f; p->obj[o].rgba[2] = (float)b / 255.0f;
        p->obj[o].rgba[3] = 1.0f;
        p->obj[o].data[0] = b; p->obj[o].data[1] = g; p->obj[o].data[2] = r; p->obj[o].data[3] = 255;
        n++;
    }
    if (g_hud_style || g_hud_col[0] || g_hud_col[1] || g_hud_col[2])
        printf("player HUD: custom gauges style %02d, %d objects painted\n", g_hud_style, n);
}
enum { HUD_NPACK = (int)(sizeof HUD_PACKS / sizeof HUD_PACKS[0]) };

/* a bundle, unpacked when it is JDLZ (FRONTEND/FrontB.lzc) */
/* The in-game screens (SMS, world map, engage dialogs) load their package
   on every open; on the Vita card re-reading these ~2.5 MB cost about half
   a second each time. Kept in memory after the first read; callers get
   their own copy to free. */
static const char *const k_hud_cached[] = { "/InGameCommon.bun", "/InGameRace.bun", "", "/GLOBALA.BUN" };
#define HUD_NCACHED (int)(sizeof k_hud_cached / sizeof k_hud_cached[0])
static unsigned char *g_hfc_data[HUD_NCACHED];
static long g_hfc_len[HUD_NCACHED];
static volatile int g_hfc_lock;
static unsigned char *hud_read_file(const char *path, long *len) {
    int slot = -1;
    size_t pl = strlen(path);
    char langf[40];   /* slot 2: the current language's file */
    snprintf(langf, sizeof langf, "/%s.bin", k_n2_lang_file[n2_lang()]);
    for (int i = 0; i < HUD_NCACHED && slot < 0; i++) {
        const char *c = i == 2 ? langf : k_hud_cached[i];
        size_t cl = strlen(c);
        if (pl >= cl && !strcmp(path + pl - cl, c)) slot = i;
    }
    if (slot < 0) return n2_read_file(path, len);
    unsigned char *d = NULL;
    for (int n_ = 0; __sync_lock_test_and_set(&g_hfc_lock, 1); n_++) if (n_ > 64) SDL_Delay(1);
    if (g_hfc_data[slot] && (d = (unsigned char *)malloc((size_t)g_hfc_len[slot])) != NULL) {
        memcpy(d, g_hfc_data[slot], (size_t)g_hfc_len[slot]);
        *len = g_hfc_len[slot];
    }
    __sync_lock_release(&g_hfc_lock);
    if (d) return d;
    d = n2_read_file(path, len);
    if (!d || *len <= 0) return d;
    unsigned char *keep = (unsigned char *)malloc((size_t)*len);
    if (!keep) return d;
    memcpy(keep, d, (size_t)*len);
    for (int n_ = 0; __sync_lock_test_and_set(&g_hfc_lock, 1); n_++) if (n_ > 64) SDL_Delay(1);
    if (!g_hfc_data[slot]) { g_hfc_data[slot] = keep; g_hfc_len[slot] = *len; keep = NULL; }
    __sync_lock_release(&g_hfc_lock);
    free(keep);
    return d;
}

static unsigned char *hud_read_bundle(const char *path, long *len) {
    unsigned char *d = hud_read_file(path, len);
    if (!d || *len < 16 || memcmp(d, "JDLZ", 4)) return d;
    long ul = (long)n2_u32(d + 8);
    unsigned char *u = (unsigned char *)malloc((size_t)ul);
    if (u) ul = n2_jdlz(d, (int)*len, u, (int)ul);
    free(d);
    if (!u || ul <= 0) { free(u); *len = 0; return NULL; }
    *len = ul;
    return u;
}

/* Decoded art / font atlases kept between screen opens (HW 11-02: each
   SMS / Pause / engage dialog open re-walked every pack and re-decoded its
   art, ~0.6 s, and its fonts, ~0.16 s). Same pixels as a fresh load: an
   entry is what the full search over the same source set found, including
   "not found" (rgba NULL), so a fully answered load skips the packs. LRU,
   capped; callers get their own copy. */
#define HIC_MAX 192
#define HIC_CAP_BYTES (4L * 1024 * 1024)
static struct { uint32_t src, key; FengImage im; unsigned age; } g_hic[HIC_MAX];
static int g_hic_n; static long g_hic_bytes; static unsigned g_hic_clock;
static volatile int g_hic_lock;
/* the holder copies pictures under it: a waiter backs off after a short spin,
   so a higher-priority thread on the same core cannot starve it (audit 11-17) */
static void hic_lock(void) { for (int n = 0; __sync_lock_test_and_set(&g_hic_lock, 1); n++) if (n > 64) SDL_Delay(1); }
static uint32_t hic_src(const char *own, int screen, int style) {
    uint32_t h = 2166136261u;
    for (const char *c = own ? own : ""; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    return (h ^ (uint32_t)screen * 0x9e3779b9u) + (uint32_t)style * 0x85ebca6bu;
}
/* fill the empty slots whose key is cached; 1 when every wanted key was answered */
static int hic_fill(uint32_t src, const uint32_t *keys, int nk, FengImage *img) {
    int all = 1;
    hic_lock();
    for (int i = 0; i < nk; i++) {
        if (!keys[i] || img[i].rgba) continue;
        int e = -1;
        for (int j = 0; j < g_hic_n && e < 0; j++) if (g_hic[j].src == src && g_hic[j].key == keys[i]) e = j;
        if (e < 0) { all = 0; continue; }
        g_hic[e].age = ++g_hic_clock;
        const FengImage *c = &g_hic[e].im;
        if (!c->rgba) continue;          /* known missing from these sources */
        size_t n = (size_t)c->w * c->h * 4;
        img[i] = *c;
        img[i].rgba = (unsigned char *)malloc(n);
        if (img[i].rgba) memcpy(img[i].rgba, c->rgba, n); else { img[i].w = img[i].h = 0; all = 0; }
    }
    __sync_lock_release(&g_hic_lock);
    return all;
}
/* remember what the full search found for each wanted key */
static void hic_store(uint32_t src, const uint32_t *keys, int nk, const FengImage *img) {
    hic_lock();
    for (int i = 0; i < nk; i++) {
        if (!keys[i]) continue;
        int dup = 0;
        for (int j = 0; j < g_hic_n && !dup; j++) dup = g_hic[j].src == src && g_hic[j].key == keys[i];
        if (dup) continue;
        long n = img[i].rgba ? (long)img[i].w * img[i].h * 4 : 0;
        if (n > HIC_CAP_BYTES / 4) continue;   /* a full-screen picture: not worth holding */
        while (g_hic_n && (g_hic_n == HIC_MAX || g_hic_bytes + n > HIC_CAP_BYTES)) {   /* drop the oldest */
            int o = 0;
            for (int j = 1; j < g_hic_n; j++) if (g_hic[j].age < g_hic[o].age) o = j;
            g_hic_bytes -= g_hic[o].im.rgba ? (long)g_hic[o].im.w * g_hic[o].im.h * 4 : 0;
            free(g_hic[o].im.rgba);
            g_hic[o] = g_hic[--g_hic_n];
        }
        FengImage c = img[i];
        if (n) { c.rgba = (unsigned char *)malloc((size_t)n); if (!c.rgba) continue; memcpy(c.rgba, img[i].rgba, (size_t)n); }
        g_hic[g_hic_n].src = src; g_hic[g_hic_n].key = keys[i]; g_hic[g_hic_n].im = c;
        g_hic[g_hic_n].age = ++g_hic_clock; g_hic_n++;
        g_hic_bytes += n;
    }
    __sync_lock_release(&g_hic_lock);
}

static void hud_load_art(Hud *h, const char *dataroot, const char *own) {
    const FengPackage *p = h->pk;
    unsigned char need[FENG_MAX_RES]; memset(need, 0, sizeof need);
    for (int k = 0; k < h->ndraw; k++) {
        const FengObj *o = &p->obj[h->draw[k]];
        if (o->type != FENG_STRING && h->role[h->draw[k]] != R_MAP_SLOT &&
            o->res >= 0 && o->res < p->nres) need[o->res] = 1;
    }
    int extra[14] = { h->o_player, h->o_gps, h->o_gate, h->o_wrong, h->o_startind, h->o_playerind,
                      h->o_aiind[0], h->o_aiind[1], h->o_aiind[2], h->o_aiind[3],
                      h->o_aiind[4], h->o_aiind[5], h->o_aiind[6], h->o_aiind[7] };
    for (int e = 0; e < 14; e++)
        if (extra[e] >= 0 && p->obj[extra[e]].res >= 0 && p->obj[extra[e]].res < p->nres)
            need[p->obj[extra[e]].res] = 1;
    for (int t = 0; t < 14; t++) for (int k = 0; k < 9; k++) {
        int o = h->o_zind[t][k];
        if (o >= 0 && p->obj[o].res >= 0 && p->obj[o].res < p->nres) need[p->obj[o].res] = 1;
    }
    /* keys: [2r] plain name, [2r+1] custom gauge name, last = the nav arrow */
    uint32_t keys[FENG_MAX_RES * 2 + 2];
    FengImage img[FENG_MAX_RES * 2 + 2];
    memset(img, 0, sizeof img);
    for (int r = 0; r < p->nres; r++) {
        char custom[64];
        const char *nm = p->res[r].name;
        int n = 0;
        while (nm[n] && nm[n] != '.' && n < 40) { custom[n] = nm[n]; n++; }
        snprintf(custom + n, sizeof custom - (size_t)n, "_CUSTOM_%02d", g_hud_style);
        keys[2*r]   = need[r] && p->res[r].type == 1 ? feng_tex_key(nm) : 0;
        keys[2*r+1] = need[r] && p->res[r].type == 1 ? feng_tex_key(custom) : 0;
    }
    int nk = p->nres * 2;
    keys[nk++] = HUD_TICK_KEY;
    keys[nk++] = HUD_CROSS_KEY;
    keys[nk++] = HUD_ARROW_KEY;
    /* -1: the package's own bundle; HUD_NPACK: a screen's menu art (GLOBALB) */
    int last = h->dw && h->dw->screen ? HUD_NPACK + 1 : HUD_NPACK;
    /* every package's art from the image cache (the same 4 MB LRU): whole
       screens reopen often, and so do the race HUDs, the chyrons and the SMS
       screens (hw_1109: ~0.6 s of art per HUD load, every race / career entry) */
    int cached = 1;
    uint32_t src = hic_src(own, h->dw && h->dw->screen, g_hud_style);
    int answered = cached && hic_fill(src, keys, nk, img);
    for (int f = own ? -1 : 0; f < last && !answered; f++) {
        char path[1024], pack[64];
        pack[0] = 0;
        if (f >= 0 && f < HUD_NPACK) snprintf(pack, sizeof pack, HUD_PACKS[f], g_hud_style);
        snprintf(path, sizeof path, "%s/%s", dataroot, f < 0 ? own : f == HUD_NPACK
                 ? "GLOBAL/GLOBALB.BUN" : pack);
        if (feng_tpk_load_file(path, keys, nk, img) >= 0) continue;   /* only the wanted images' bytes */
        long len = 0;
        unsigned char *d = hud_read_bundle(path, &len);
        if (!d) continue;
        feng_tpk_load(d, len, keys, nk, img);
        free(d);
    }
    if (cached && !answered) hic_store(src, keys, nk, img);
    for (int r = 0; r < p->nres; r++) {
        if (!need[r]) continue;
        FengImage *im = img[2*r].rgba ? &img[2*r] : &img[2*r+1];
        if (!im->rgba) { hud_note_missing(h, p->res[r].name); continue; }
        h->rt[r].w = im->w; h->rt[r].h = im->h;
        h->rt[r].additive = im->blend == 6;
#ifdef OPENUG2_SIM_PRE_H
        if (getenv("OPENUG2_HUD_DUMP")) {   /* sim: the art as decoded, for checking */
            int amin = 255, amax = 0;
            for (long i = 0; i < (long)im->w * im->h; i++) {
                int a = im->rgba[i*4+3]; if (a < amin) amin = a; if (a > amax) amax = a; }
            printf("hud art %s %dx%d blend %d alpha %d..%d\n", p->res[r].name, im->w, im->h, im->blend, amin, amax);
            char fn[128]; snprintf(fn, sizeof fn, "hudart_%s.tga", p->res[r].name);
            for (char *c = fn; *c; c++) if (*c == '/' || *c == '\\') *c = '_';
            FILE *f = fopen(fn, "wb");
            if (f) {
                unsigned char hd[18] = { 0, 0, 2 }; hd[12] = im->w & 255; hd[13] = im->w >> 8;
                hd[14] = im->h & 255; hd[15] = im->h >> 8; hd[16] = 32; hd[17] = 0x28;
                fwrite(hd, 1, 18, f);
                for (long i = 0; i < (long)im->w * im->h; i++) {
                    unsigned char px[4] = { im->rgba[i*4+2], im->rgba[i*4+1], im->rgba[i*4], im->rgba[i*4+3] };
                    fwrite(px, 1, 4, f); }
                fclose(f);
            }
        }
#endif
        if (h->o_track >= 0 && p->obj[h->o_track].res == r)
            hud_measure_arc(im, &h->nos_arc);
        hud_upload_to(h, &h->rt[r].tex, im, NULL);   /* last: a deferred upload takes the pixels */
    }
    /* The arrow record can decode to garbage -- ARROW_MARKERA in InGameCommon
       does exactly that. HUD art is flat-shaded, so a noise test on it is
       reliable; a rejected arrow falls back to the drawn chevron. */
    if (img[nk - 2].rgba) hud_upload_to(h, &h->cross_tex, &img[nk - 2], NULL);
    if (h->dw && h->dw->screen && img[nk - 3].rgba) hud_upload_to(h, &h->tick_tex, &img[nk - 3], NULL);
    FengImage *ar = &img[nk - 1];
    if (ar->rgba) {
        long n = (long)ar->w * ar->h;
        unsigned char *rgb = (unsigned char *)malloc((size_t)n * 3);
        if (rgb) {
            for (long i = 0; i < n; i++) memcpy(rgb + i*3, ar->rgba + i*4, 3);
            if (hud_art_run_fraction(rgb, ar->w, ar->h) >= HUD_ART_RUN_MIN)
                hud_upload_to(h, &h->arrow_tex, ar, NULL);
            free(rgb);
        }
    }
    for (int i = 0; i < nk; i++) feng_image_free(&img[i]);
}

/* Fonts the drawn strings use: glyph tables from LANGUAGES/English.bin,
   atlases from LANGUAGES/LanguageTextures.bin. */
static void hud_load_fonts(Hud *h, const char *dataroot) {
    const FengPackage *p = h->pk;
    memset(h->font_of, -1, sizeof h->font_of);
    char path[1024];
    n2_lang_path(path, sizeof path, dataroot);
    long len = 0;
    unsigned char *lang = hud_read_file(path, &len);
    /* the front end's ConduitMdITC_TT21i is in GLOBAL/GLOBALA.BUN (FNTF + its
       own atlas TPK), not in the language file */
    long glen = 0;
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALA.BUN", dataroot);
    unsigned char *globa = h->dw && h->dw->screen ? hud_read_file(path, &glen) : NULL;
    int nf = 0;
    for (int k = 0; k < h->ndraw && (lang || globa); k++) {
        const FengObj *o = &p->obj[h->draw[k]];
        if (o->type != FENG_STRING || o->res < 0 || o->res >= p->nres) continue;
        /* one resource row per font, so the row is the identity */
        if (h->font_of[o->res] >= 0 || nf >= HUD_NFONT) continue;
        if ((lang && feng_load_font(lang, len, p->res[o->res].name, &h->font[nf].f)) ||
            (globa && feng_load_font(globa, glen, p->res[o->res].name, &h->font[nf].f)))
            h->font_of[o->res] = (signed char)nf++;
        else hud_note_missing(h, p->res[o->res].name);
    }
    free(lang);
    if (!nf) { free(globa); return; }
    snprintf(path, sizeof path, "%s/LANGUAGES/LanguageTextures.bin", dataroot);
    uint32_t keys[HUD_NFONT]; FengImage img[HUD_NFONT];
    memset(img, 0, sizeof img);
    for (int f = 0; f < nf; f++) keys[f] = h->font[f].f.atlas_key;
    /* the atlases are shared by every screen: from the image cache */
    uint32_t src = hic_src("LanguageTextures", globa != NULL, 0);
    int answered = hic_fill(src, keys, nf, img);
    if (!answered && feng_tpk_load_file(path, keys, nf, img) < 0) {
        unsigned char *d = n2_read_file(path, &len);
        if (!d) { hud_note_missing(h, "LanguageTextures.bin"); free(globa); return; }
        feng_tpk_load(d, len, keys, nf, img);
        free(d);
    }
    if (globa && !answered) feng_tpk_load(globa, glen, keys, nf, img);
    free(globa);
    if (!answered) hic_store(src, keys, nf, img);
    for (int f = 0; f < nf; f++) {
        if (img[f].rgba) {
            h->font[f].aw = img[f].w; h->font[f].ah = img[f].h;
            hud_upload_to(h, &h->font[f].tex, &img[f], &h->font[f].ok);
        } else hud_note_missing(h, h->font[f].f.name);
        feng_image_free(&img[f]);
    }
}

static void drift_init(Hud *h, const char *dataroot, int turbo);
static void drift_mark(Hud *h, int turbo);
static void drag_mark(Hud *h);
static void drag_init(Hud *h, const char *dataroot, float redline, float maxrpm);
static const char *dw_str(const struct HudDriftW *w, uint32_t hash);
static void dw_load_strings(struct HudDriftW *w, const char *dataroot);
static void dw_text(struct HudDriftW *w, int o, const char *s);
static void dw_label(struct HudDriftW *w, int o, uint32_t hash);
static int dw_obj(Hud *h, const char *name);
static void gw_mark(Hud *h);
static void gps_update(Hud *h, const HudState *s);
static void gw_load(Hud *h, const char *dataroot);
static void gw_reset(Hud *h);
static void dy_mark(Hud *h);
static void dy_init(Hud *h);
/* the race over widget's objects (0x4f7170) are run by their FEng scripts */
static void ro_mark(Hud *h) {
    gw_mark(h);
    dy_mark(h);
    static const char *const names[] = { "RaceOverMessage", "RaceOverBacking",
                                         "WrongWayImage", "WrongWay_CountDown",
                                         "CurrentLapTime", "Lap_Time_Title", "Lap_Time_Text",
                                         "SplitTimeText", "SP_Trick_Point", "SP_Trick_Type" };
    for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
        int o = dw_obj(h, names[k]);
        if (o >= 0) h->dw->wl[o] = 1;
    }
    for (int r = 1; r <= 4; r++) {                       /* the leaderboard's rows */
        static const char *const f[4] = { "LBData_%d", "LeaderText_%d", "LBTextBacking_%d", "LBBacking_%d" };
        for (int q = 0; q < 4; q++) {
            char nm[32]; snprintf(nm, sizeof nm, f[q], r);
            int o = dw_obj(h, nm);
            if (o >= 0) h->dw->wl[o] = 1;
        }
    }
}
/* 0x4f7170: the objects found, hidden until the feature is on (0x4c6690(0, 0)) */
static void ro_init(Hud *h) {
    struct HudDriftW *w = h->dw;
    dy_init(h);
    w->o_ro_msg = dw_obj(h, "RaceOverMessage");
    w->o_ro_back = dw_obj(h, "RaceOverBacking");
    if (w->o_ro_msg >= 0) feng_anim_hide(&w->anim, w->o_ro_msg, 1);
    if (w->o_ro_back >= 0) feng_anim_hide(&w->anim, w->o_ro_back, 1);
    w->ro_on = 0; w->ro_started = 0;
    w->o_ww = dw_obj(h, "WrongWayImage");
    w->o_wwc2 = dw_obj(h, "WrongWay_CountDown");
    w->ww_in = 0;
    w->o_lt = dw_obj(h, "CurrentLapTime");
    w->o_ltt = dw_obj(h, "Lap_Time_Title");
    w->o_ltx = dw_obj(h, "Lap_Time_Text");
    w->lt54 = w->lt58 = w->lt5c = w->lt64 = w->lt65 = 0;
    w->o_sp = dw_obj(h, "SplitTimeText");
    if (w->o_sp >= 0) feng_anim_hide(&w->anim, w->o_sp, 1);
    w->sp28 = 0; w->sp2c = 0;
    w->o_stp = dw_obj(h, "SP_Trick_Point");                /* 0x4f7ad0 */
    w->o_stt = dw_obj(h, "SP_Trick_Type");
    if (w->o_stp >= 0) feng_anim_hide(&w->anim, w->o_stp, 1);
    if (w->o_stt >= 0) feng_anim_hide(&w->anim, w->o_stt, 1);
    w->nsn = 0;
    for (int r = 0; r < 4; r++) {
        char nm[32];
        snprintf(nm, sizeof nm, "LBData_%d", r + 1); w->o_lbd[r] = dw_obj(h, nm);
        snprintf(nm, sizeof nm, "LeaderText_%d", r + 1); w->o_lbt[r] = dw_obj(h, nm);
        snprintf(nm, sizeof nm, "LBTextBacking_%d", r + 1); w->o_lbtb[r] = dw_obj(h, nm);
        snprintf(nm, sizeof nm, "LBBacking_%d", r + 1); w->o_lbb[r] = dw_obj(h, nm);
    }
    w->lb1bc = 0; w->lb1c0 = 0;
    gw_reset(h);
}

/* drift: 0 the plain HUD, 1 HUD_Drift, 2 a whole screen, 3 HUD_SingleRace,
   4 HUD_Drag (redline / maxrpm for its rev scale) */
/* defer: built off the GL thread -- no GL call; the textures wait in h->pend
   and the quad batch is not made (hud_pend_upload + hud_quad_batch later) */
static Hud *hud_build(const char *dataroot, const char *const *pkg_from, int nfrom,
                      const char *package, int drift, int turbo, float redline, float maxrpm, int defer);
static void hud_quad_batch(Hud *h);
static Hud *hud_init_pkg2(const char *dataroot, const char *const *pkg_from, int nfrom,
                          const char *package, int drift, int turbo, float redline, float maxrpm) {
    return hud_build(dataroot, pkg_from, nfrom, package, drift, turbo, redline, maxrpm, 0);
}
static Hud *hud_init_pkg(const char *dataroot, const char *const *pkg_from, int nfrom,
                         const char *package, int drift, int turbo) {
    return hud_init_pkg2(dataroot, pkg_from, nfrom, package, drift, turbo, 0.0f, 0.0f);
}
static Hud *hud_build(const char *dataroot, const char *const *pkg_from, int nfrom,
                      const char *package, int drift, int turbo, float redline, float maxrpm, int defer) {
    Hud *h = (Hud *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->defer = defer;
    if (!dataroot) dataroot = ".";
    snprintf(h->dataroot, sizeof h->dataroot, "%s", dataroot);
    memset(h->font_of, -1, sizeof h->font_of);

    h->pk = (FengPackage *)malloc(sizeof *h->pk);
    int loaded = 0;
    Uint64 tq[6]; tq[0] = tq[1] = SDL_GetPerformanceCounter();   /* the load's phases, logged */
    for (int f = 0; f < nfrom && h->pk && !loaded; f++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dataroot, pkg_from[f]);
        long len = 0;
        unsigned char *d = hud_read_bundle(path, &len);
        if (!d) continue;
        tq[1] = SDL_GetPerformanceCounter();
        loaded = feng_load_package(d, len, package, h->pk);
        free(d);
    }
    tq[2] = SDL_GetPerformanceCounter();
    if (loaded && drift != 2) hud_paint_gauges(h->pk);   /* the race HUDs */
    if (loaded && drift) {
        h->dw = (struct HudDriftW *)calloc(1, sizeof *h->dw);
        /* none until ro_init finds them: calloc's 0 is object 0 (a whole
           screen's first object -- UI_Status_DVD's "Not Available" -- was
           skipped as the wrong-way image) */
        if (h->dw) h->dw->o_ww = h->dw->o_ro_msg = h->dw->o_ro_back = -1;
        if (h->dw && drift == 1) drift_mark(h, turbo);
        if (h->dw && drift == 4) drag_mark(h);
        if (h->dw && (drift == 1 || drift == 3 || drift == 4)) ro_mark(h);   /* the race HUDs */
        if (h->dw && drift == 2) {          /* a whole screen: every object, centred 4:3 */
            memset(h->dw->wl, 1, sizeof h->dw->wl);
            h->dw->screen = 1;
        }
    }
    if (!loaded) {
        free(h->pk); h->pk = NULL;
        hud_note_missing(h, package);
        printf("player HUD: %s not found -- only the navigation arrow draws\n", package);
    } else {
        hud_classify(h);
        hud_load_art(h, dataroot, drift ? pkg_from[0] : NULL);
        tq[3] = SDL_GetPerformanceCounter();
        hud_load_fonts(h, dataroot);
        tq[4] = SDL_GetPerformanceCounter();
        if (h->dw && drift == 1) drift_init(h, dataroot, turbo);
        if (h->dw && drift == 3) {
            feng_anim_init(&h->dw->anim, h->pk);
            dw_load_strings(h->dw, dataroot);
            gw_load(h, dataroot);
        }
        if (h->dw && drift == 4) drag_init(h, dataroot, redline, maxrpm);
        if (h->dw && (drift == 1 || drift == 3 || drift == 4)) ro_init(h);
        if (h->dw && drift == 2) {
            feng_anim_init(&h->dw->anim, h->pk);
            dw_load_strings(h->dw, dataroot);
        } else if (h->dw && n2_lang() != 0) dw_load_strings(h->dw, dataroot);   /* the race HUDs' labels in that language */
        if (h->dw && h->dw->nstr) feng_apply_labels(h->pk, h->dw->sh, (const char *const *)h->dw->st, h->dw->nstr, drift == 2);
        tq[5] = SDL_GetPerformanceCounter();
        {   double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
            printf("player HUD: %s load ms: read %.1f parse %.1f art %.1f fonts %.1f rest %.1f\n", package,
                   (tq[1] - tq[0]) * ms, (tq[2] - tq[1]) * ms, (tq[3] - tq[2]) * ms,
                   (tq[4] - tq[3]) * ms, (tq[5] - tq[4]) * ms);
        }
        printf("player HUD: %s, %d objects, %d drawn; minimap at (%.0f,%.0f) r %.0f pin %d\n",
               h->pk->name, h->pk->nobj, h->ndraw, (double)g_map_fx, (double)g_map_fy,
               (double)g_map_half, g_map_pin);
        if (h->nos_arc.ok)
            printf("player HUD: N2O arc centre (%.3f, %.3f)w r %.3fw sweep %.0f..%.0f deg\n",
                   h->nos_arc.cx, h->nos_arc.cy, h->nos_arc.r,
                   h->nos_arc.a0 * 180.0f / (float)M_PI, h->nos_arc.a1 * 180.0f / (float)M_PI);
    }

    if (!defer) hud_quad_batch(h);
    h->needle_rpm = h->needle_speed = 0.0f;
    return h;
}

/* dynamic quad batch: positions/UVs per draw, fixed normals and indices */
static void hud_quad_batch(Hud *h) {
    {
        float nrm[HUD_QMAX * 4 * 3];
        unsigned short idx[HUD_QMAX * 6];
        for (int i = 0; i < HUD_QMAX * 4; i++) { nrm[i*3] = 0; nrm[i*3+1] = 0; nrm[i*3+2] = 1; }
        for (int q = 0; q < HUD_QMAX; q++) {
            unsigned short b = (unsigned short)(q * 4);
            idx[q*6] = b; idx[q*6+1] = b + 1; idx[q*6+2] = b + 2;
            idx[q*6+3] = b; idx[q*6+4] = b + 2; idx[q*6+5] = b + 3;
        }
        glGenBuffers(1, &h->qb_vbo);
        glGenBuffers(1, &h->qb_nbo);
        glGenBuffers(1, &h->qb_ibo);
        glBindBuffer(GL_ARRAY_BUFFER, h->qb_nbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof nrm, nrm, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, h->qb_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, h->qb_vbo);
        glBufferData(GL_ARRAY_BUFFER, HUD_QMAX * 4 * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    }
}

/* a race HUD kept across races (the original builds its HUD per race):
   the race-over / wrong way / lap time / split / leaderboard widgets start
   clean, else the lap widget's last-lap stamp from the previous race
   (later than the new race clock) keeps "BEST LAP TIME" up all lap 1 */
void hud_race_reset(Hud *h) {
    if (h && h->pk && h->dw && !h->dw->screen) ro_init(h);
}

Hud *hud_init(const char *dataroot) {
    static const char *const from[] = { "GLOBAL/InGameRace.bun", "GLOBAL/InGameFreeRoam.bun" };
    return hud_init_pkg(dataroot, from, 2, HUD_PACKAGE, 3, 0);
}

Hud *hud_init_short(const char *dataroot) {
    static const char *const from[] = { "GLOBAL/InGameRace.bun" };
    return hud_init_pkg(dataroot, from, 1, "HUD_Short_Track.fng", 3, 0);
}

Hud *hud_init_drift(const char *dataroot, int turbo) {
    static const char *const from[] = { "GLOBAL/InGameDrift.bun" };
    return hud_init_pkg(dataroot, from, 1, "HUD_Drift.fng", 1, turbo);
}

Hud *hud_init_drag(const char *dataroot, float redline, float maxrpm) {
    static const char *const from[] = { "GLOBAL/InGameDrag.bun" };
    return hud_init_pkg2(dataroot, from, 1, "HUD_Drag.fng", 4, 0, redline, maxrpm);
}

/* ---- screens decoded ahead (hud_screen_prefetch) ----
   A slot: one screen package, built by the worker with hud_build(defer), its
   images then uploaded by the pump a few per frame. hud_init_screen takes a
   matching slot instead of loading; what it hands out is the same Hud a
   load at that moment would build (same files, same code). */
#define HUD_NPRE 4
enum { PRE_FREE, PRE_QUEUED, PRE_DECODING, PRE_DECODED, PRE_READY };
static struct {
    char root[512], bun[64], pkg[64];
    int style;                          /* g_hud_style it was built with */
    Hud *h;
    volatile int st;
    double ms;                          /* the worker's build time */
    long kb;                            /* decoded pixels, KB */
    long hkb;                           /* the Hud's own structs + string table, KB (English.bin copy not counted) */
} g_pre[HUD_NPRE];
static SDL_Thread *g_pre_thr;
static volatile int g_pre_run;          /* the worker is running */
static struct { int n, set; char root[512], bun[HUD_NPRE][64], pkg[HUD_NPRE][64]; } g_pre_want;
/* screens built once on the worker only to fill the image cache (hic), then
   dropped: their first open then skips the pack walk (play1116: the first
   SMS mailbox / message open was ~0.8 s on the frame thread, 650 ms of it
   the art). Same build as an open, nothing held but the capped cache. */
#define HUD_NWARM 8
static struct { volatile int st; int n; char root[512], bun[HUD_NWARM][64], pkg[HUD_NWARM][64]; } g_warm;   /* st: 0 none, 1 queued, 2 running */

static int hud_pre_thread(void *u) {
    (void)u;
    for (int i = 0; i < HUD_NPRE; i++) {
        if (!__sync_bool_compare_and_swap(&g_pre[i].st, PRE_QUEUED, PRE_DECODING)) continue;
        Uint64 t0 = SDL_GetPerformanceCounter();
        const char *from[1] = { g_pre[i].bun };
        Hud *h = hud_build(g_pre[i].root, from, 1, g_pre[i].pkg, 2, 0, 0.0f, 0.0f, 1);
        long px = 0, hb = 0;
        if (h) {
            for (int k = 0; k < h->npend; k++) px += (long)h->pend[k].im.w * h->pend[k].im.h * 4;
            hb = (long)sizeof *h + (h->pk ? (long)sizeof *h->pk : 0) +
                 (h->dw ? (long)sizeof *h->dw + (long)h->dw->nstr * (long)(sizeof *h->dw->sh + sizeof *h->dw->st) : 0);
        }
        g_pre[i].ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        g_pre[i].kb = px / 1024;
        g_pre[i].hkb = hb / 1024;
        g_pre[i].h = h;
        __sync_synchronize();
        g_pre[i].st = PRE_DECODED;
    }
    if (__sync_bool_compare_and_swap(&g_warm.st, 1, 2)) {
        Uint64 t0 = SDL_GetPerformanceCounter();
        for (int k = 0; k < g_warm.n; k++) {
            const char *from[1] = { g_warm.bun[k] };
            Hud *h = hud_build(g_warm.root, from, 1, g_warm.pkg[k], 2, 0, 0.0f, 0.0f, 1);
            hud_free(&h);                   /* deferred: no GL object was made */
        }
        hic_lock();
        long kb = g_hic_bytes / 1024; int ne = g_hic_n;
        __sync_lock_release(&g_hic_lock);
        printf("player HUD: %d screens warmed on the worker in %.0f ms (image cache %d entries, %ld KB)\n", g_warm.n,
               (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency(), ne, kb);
        __sync_synchronize();
        g_warm.st = 0;
    }
    __sync_synchronize();
    g_pre_run = 0;
    return 0;
}
static void hud_pre_finish(Hud *h) {    /* GL thread: what the worker could not do */
    if (!h || !h->defer) return;
    hud_pend_upload(h, 0x7fffffffL);
    hud_quad_batch(h);
    h->defer = 0;
}
/* GL thread, the worker idle: the slots made to match the wanted list */
static void hud_pre_apply(void) {
    int queued = 0;
    for (int i = 0; i < HUD_NPRE; i++) {
        int keep = 0;
        if (g_pre[i].st == PRE_FREE) continue;
        for (int k = 0; k < g_pre_want.n && !keep; k++)
            keep = !strcmp(g_pre[i].root, g_pre_want.root) && !strcmp(g_pre[i].bun, g_pre_want.bun[k]) &&
                   !strcmp(g_pre[i].pkg, g_pre_want.pkg[k]) && g_pre[i].style == g_hud_style;
        if (keep) continue;
        hud_free(&g_pre[i].h);
        g_pre[i].st = PRE_FREE;
    }
    for (int k = 0; k < g_pre_want.n; k++) {
        int have = 0, fr = -1;
        for (int i = 0; i < HUD_NPRE; i++) {
            if (g_pre[i].st == PRE_FREE) { if (fr < 0) fr = i; continue; }
            if (!strcmp(g_pre[i].bun, g_pre_want.bun[k]) && !strcmp(g_pre[i].pkg, g_pre_want.pkg[k])) have = 1;
        }
        if (have || fr < 0) continue;
        snprintf(g_pre[fr].root, sizeof g_pre[fr].root, "%s", g_pre_want.root);
        snprintf(g_pre[fr].bun, sizeof g_pre[fr].bun, "%s", g_pre_want.bun[k]);
        snprintf(g_pre[fr].pkg, sizeof g_pre[fr].pkg, "%s", g_pre_want.pkg[k]);
        g_pre[fr].style = g_hud_style;
        g_pre[fr].h = NULL;
        g_pre[fr].st = PRE_QUEUED;
        queued++;
    }
    if (!queued) return;
    g_pre_run = 1;
    __sync_synchronize();
    g_pre_thr = SDL_CreateThread(hud_pre_thread, "hud-prefetch", NULL);
    if (!g_pre_thr) {                   /* no thread: the screens load when opened, as before */
        g_pre_run = 0;
        for (int i = 0; i < HUD_NPRE; i++) if (g_pre[i].st == PRE_QUEUED) g_pre[i].st = PRE_FREE;
    }
}
void hud_screen_prefetch(const char *dataroot, const char *const *bundle, const char *const *package, int n) {
    if (n > HUD_NPRE) n = HUD_NPRE;
    g_pre_want.n = n < 0 ? 0 : n;
    snprintf(g_pre_want.root, sizeof g_pre_want.root, "%s", dataroot ? dataroot : ".");
    for (int k = 0; k < g_pre_want.n; k++) {
        snprintf(g_pre_want.bun[k], sizeof g_pre_want.bun[k], "%s", bundle[k]);
        snprintf(g_pre_want.pkg[k], sizeof g_pre_want.pkg[k], "%s", package[k]);
    }
    g_pre_want.set = 1;
    hud_screen_prefetch_pump();         /* now, unless the worker is still busy */
}
void hud_screen_warm(const char *dataroot, const char *const *bundle, const char *const *package, int n) {
    if (g_warm.st || n <= 0) return;
    if (n > HUD_NWARM) n = HUD_NWARM;
    snprintf(g_warm.root, sizeof g_warm.root, "%s", dataroot ? dataroot : ".");
    for (int k = 0; k < n; k++) {
        snprintf(g_warm.bun[k], sizeof g_warm.bun[k], "%s", bundle[k]);
        snprintf(g_warm.pkg[k], sizeof g_warm.pkg[k], "%s", package[k]);
    }
    g_warm.n = n;
    __sync_synchronize();
    g_warm.st = 1;
    hud_screen_prefetch_pump();
}
void hud_screen_prefetch_pump(void) {
    if (g_pre_thr && !g_pre_run) { SDL_WaitThread(g_pre_thr, NULL); g_pre_thr = NULL; }
    if (g_pre_want.set && !g_pre_thr) { g_pre_want.set = 0; hud_pre_apply(); }
    if (g_warm.st == 1 && !g_pre_thr) {     /* a warm-only job: the same worker */
        g_pre_run = 1;
        __sync_synchronize();
        g_pre_thr = SDL_CreateThread(hud_pre_thread, "hud-prefetch", NULL);
        if (!g_pre_thr) { g_pre_run = 0; g_warm.st = 0; }
    }
    /* one slot's images a frame, ~256 KB of pixels (an upload is a copy
       into GPU memory; the screens' art is mostly 64..256 px images) */
    for (int i = 0; i < HUD_NPRE; i++) {
        if (g_pre[i].st != PRE_DECODED) continue;
        Hud *h = g_pre[i].h;
        if (h && h->defer && !hud_pend_upload(h, 256 * 1024)) return;
        if (h && h->defer) { hud_quad_batch(h); h->defer = 0; }
        g_pre[i].st = PRE_READY;
        printf("player HUD: %s ready ahead (worker %.1f ms, %ld KB of textures, %ld KB of structs)\n",
               g_pre[i].pkg, g_pre[i].ms, g_pre[i].kb, g_pre[i].hkb);
        return;
    }
}
/* a ready (or still decoding) slot for this screen, taken; NULL: load it */
static Hud *hud_pre_take(const char *dataroot, const char *bundle, const char *package) {
    for (int i = 0; i < HUD_NPRE; i++) {
        if (g_pre[i].st == PRE_FREE || strcmp(g_pre[i].pkg, package) || strcmp(g_pre[i].bun, bundle) ||
            strcmp(g_pre[i].root, dataroot ? dataroot : ".") || g_pre[i].style != g_hud_style) continue;
        Uint64 t0 = SDL_GetPerformanceCounter();
        if (__sync_bool_compare_and_swap(&g_pre[i].st, PRE_QUEUED, PRE_FREE)) return NULL;   /* not begun */
        for (int wt = 0; g_pre[i].st == PRE_DECODING; wt++) {   /* the worker is on it: the rest of its build */
            SDL_Delay(1);
            if (wt == 3000) printf("player HUD: %s still decoding after 3 s (worker stuck?)\n", package);   /* audit 11-17 */
        }
        __sync_synchronize();
        Hud *h = g_pre[i].h;
        int was = g_pre[i].st;
        g_pre[i].h = NULL; g_pre[i].st = PRE_FREE;
        hud_pre_finish(h);
        printf("player HUD: %s from the prefetch (%s), %.1f ms at the open\n", package,
               was == PRE_READY ? "ready" : "uploaded now",
               (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
        return h;
    }
    return NULL;
}

Hud *hud_init_screen(const char *dataroot, const char *bundle, const char *package) {
    const char *from[1] = { bundle };
    Hud *h = hud_pre_take(dataroot, bundle, package);
    if (!h) h = hud_init_pkg(dataroot, from, 1, package, 2, 0);
    /* PC_Loading: "LOADING", its gradient and circles are one strip on the
       left edge, as the front end pins it (shop.c PK_LOADING) */
    if (h && h->dw && !strcmp(package, "PC_Loading.fng")) {
        h->dw->screen = 2;
        /* GUESS: its keyboard controls group (af4a85eb) stays hidden on the
           Vita, as shop.c prepare_package does (GAPS "F") */
        hud_screen_hide_h(h, 0xaf4a85ebu, 1);
    }
    return h;
}

/* The loading screens' backdrop PC_load.tga (PC_Loading, Loading) in the
   widescreen mod's 2:1 art (nfsmods.xyz/mod/3786, made with the Custom FNGs
   Pack): on the PC the whole texture stretches over a 16:9 screen in place
   of the 4:3 picture. Shipped with the app (tools/pcload_wide.py ->
   app0:assets/pc_load_wide.dxt, DXT1 1024x512), so no mod files are needed;
   ux0:data/NFSU2/pc_load_wide.dxt replaces it for testing. Loaded once and
   kept (256 KB); 0 when missing: the 4:3 art is drawn as before. */
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
static void wide_dxt1(const unsigned char *src, int w, int h, unsigned char *out) {
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++, src += 8) {
            unsigned c0 = (unsigned)src[0] | (unsigned)src[1] << 8, c1 = (unsigned)src[2] | (unsigned)src[3] << 8;
            unsigned char p[4][4];
            for (int k = 0; k < 2; k++) {
                unsigned c = k ? c1 : c0;
                p[k][0] = (unsigned char)((c >> 11 & 31) * 255 / 31);
                p[k][1] = (unsigned char)((c >> 5 & 63) * 255 / 63);
                p[k][2] = (unsigned char)((c & 31) * 255 / 31);
                p[k][3] = 255;
            }
            for (int ch = 0; ch < 3; ch++) {
                if (c0 > c1) {
                    p[2][ch] = (unsigned char)((2 * p[0][ch] + p[1][ch]) / 3);
                    p[3][ch] = (unsigned char)((p[0][ch] + 2 * p[1][ch]) / 3);
                } else {
                    p[2][ch] = (unsigned char)((p[0][ch] + p[1][ch]) / 2);
                    p[3][ch] = 0;
                }
            }
            p[2][3] = 255; p[3][3] = c0 > c1 ? 255 : 0;
            uint32_t bits = (uint32_t)src[4] | (uint32_t)src[5] << 8 | (uint32_t)src[6] << 16 | (uint32_t)src[7] << 24;
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    memcpy(out + ((size_t)(by * 4 + y) * w + bx * 4 + x) * 4, p[bits >> (2 * (y * 4 + x)) & 3], 4);
        }
}
GLuint hud_wide_loading_tex(void) {
    static GLuint tex; static int tried;
    if (tried) return tex;
    tried = 1;
    static const char *const k_path[2] = { "ux0:data/NFSU2/pc_load_wide.dxt", "app0:assets/pc_load_wide.dxt" };
    for (int k = 0; k < 2 && !tex; k++) {
        FILE *f = fopen(k_path[k], "rb");
        if (!f) continue;
        unsigned char hd[16];
        unsigned char *px = NULL;
        int w = 0, ht = 0;
        size_t n = 0;
        if (fread(hd, 1, 16, f) == 16 && !memcmp(hd, "OUWIDE01", 8) && hd[12] == 1) {
            w = hd[8] | hd[9] << 8; ht = hd[10] | hd[11] << 8;
            n = (size_t)w * ht / 2;
            if (w >= 4 && ht >= 4 && !(w & 3) && !(ht & 3)) px = (unsigned char *)malloc(n);
            if (px && fread(px, 1, n, f) != n) { free(px); px = NULL; }
        }
        fclose(f);
        if (!px) { printf("loading screen: %s is not a pc_load_wide.dxt, ignored\n", k_path[k]); continue; }
        glGenTextures(1, &tex);
        if (tex) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            unsigned char *rgba = NULL;
            if (g_tex_s3tc)
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, w, ht, 0, (GLsizei)n, px);
            else if ((rgba = (unsigned char *)malloc((size_t)w * ht * 4)) != NULL) {
                wide_dxt1(px, w, ht, rgba);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, ht, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                free(rgba);
            } else { glDeleteTextures(1, &tex); tex = 0; }
            if (tex) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                printf("loading screen: widescreen backdrop %s (%dx%d%s)\n", k_path[k], w, ht, g_tex_s3tc ? ", DXT1" : ", decoded");
            }
        }
        free(px);
    }
    return tex;
}
float hud_wide_loading_pin(float asp) {
    /* nx = p + (fx - 320 p) / (240 asp) = the logo's centre at the strip's
       centre fx -82 (its "LOADING" left edge ~-161, last circle's right
       edge ~-2; Vita HW shot 11-13) */
    float k = 1.0f - 320.0f / (240.0f * asp);
    if (k < 0.02f) return -1.0f;   /* 4:3: the authored layout */
    float p = (HUD_WIDE_LOGO_CX * 2.0f - 1.0f + 82.0f / (240.0f * asp)) / k;
    return p < -1.0f ? -1.0f : p > 0.0f ? 0.0f : p;
}
int hud_is_wide_loading_res(const char *name) {
    /* PC_load.tga (PC_Loading, Loading, UG_LS_Splash). Not the console's
       U2_loading_BG.tga (Loading_Controller, Loading_Tips): the picture
       without its logo, under the screen's own UG_load_logo -- the wide art
       there showed two logos (sim 11-13) */
    static const char *const k[1] = { "pc_load" };
    if (!name) return 0;
    for (int j = 0; j < 1; j++) {
        int i = 0;
        for (; k[j][i]; i++) {
            char c = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i];
            if (c != k[j][i]) break;
        }
        if (!k[j][i] && (!name[i] || name[i] == '.')) return 1;
    }
    return 0;
}

static int screen_obj(const Hud *h, const char *name) {
    return h && h->pk && h->dw ? feng_find(h->pk, feng_hash_upper(name)) : -1;
}
void hud_screen_text(Hud *h, const char *name, const char *text) {
    int o = screen_obj(h, name);
    if (o >= 0) dw_text(h->dw, o, text);
}
void hud_screen_label(Hud *h, const char *name, uint32_t label) {
    int o = screen_obj(h, name);
    if (o >= 0) dw_label(h->dw, o, label);
}
void hud_screen_text_h(Hud *h, uint32_t obj, const char *text) {
    int o = h && h->pk && h->dw ? feng_find(h->pk, obj) : -1;
    if (o >= 0) dw_text(h->dw, o, text);
}
void hud_screen_label_h(Hud *h, uint32_t obj, uint32_t label) {
    int o = h && h->pk && h->dw ? feng_find(h->pk, obj) : -1;
    if (o >= 0) dw_label(h->dw, o, label);
}
void hud_screen_script_h(Hud *h, uint32_t obj, const char *script) {
    int o = h && h->pk && h->dw ? feng_find(h->pk, obj) : -1;
    if (o >= 0) feng_anim_set(&h->dw->anim, o, feng_hash_upper(script), 1);
}
int hud_screen_game_msg(Hud *h, uint32_t msg) { return h && h->dw ? feng_anim_game_msg(&h->dw->anim, msg) : 0; }
void hud_screen_wrap(Hud *h, int on) { if (h && h->dw) h->dw->wrap = on; }
/* CTextScroller::SetText (GC): the text broken into lines of the named
   string's font that fit its Sw width (else `width`), at the spaces; '\n'
   and '^' break too. Returns the line count. */
int hud_screen_break(Hud *h, const char *name, float width, const char *txt, char (*out)[160], int max) {
    int o = screen_obj(h, name), n = 0;
    if (o < 0 || !txt || max <= 0) return 0;
    const FengObj *ob = &h->pk->obj[o];
    int fi = ob->res >= 0 && ob->res < h->pk->nres ? h->font_of[ob->res] : -1;
    if (fi < 0 || !h->font[fi].ok) return 0;
    const HudFont *F = &h->font[fi];
    float w = ob->wrapw > 0 ? (float)ob->wrapw : width;
    char ln[160]; int len = 0, sp = -1;
    for (const char *q = txt; ; q++) {
        int end = !*q, brk = *q == '\n' || *q == '^';
        if (!end && !brk && len < 159) {
            ln[len++] = *q; ln[len] = 0;
            if (*q == ' ') sp = len - 1;
            if (sp > 0 && feng_text_width(&F->f, ln) > w) {   /* the line ends at the last space */
                ln[sp] = 0;
                snprintf(out[n++], 160, "%s", ln);
                if (n >= max) return n;
                len = (int)strlen(ln + sp + 1);
                memmove(ln, ln + sp + 1, (size_t)len + 1);
                sp = -1;
            }
            continue;
        }
        ln[len] = 0;
        if (len || brk) { snprintf(out[n++], 160, "%s", ln); if (n >= max) return n; }
        len = 0; sp = -1;
        if (end) break;
    }
    return n;
}
void hud_screen_hide_h(Hud *h, uint32_t obj, int hide) {
    int o = h && h->pk && h->dw ? feng_find(h->pk, obj) : -1;
    if (o >= 0) feng_anim_hide(&h->dw->anim, o, hide);
}
const char *hud_screen_string(const Hud *h, uint32_t label) {
    return h && h->dw ? dw_str(h->dw, label) : "";
}
void hud_screen_script(Hud *h, const char *name, const char *script) {
    int o = screen_obj(h, name);
    if (o >= 0) feng_anim_set(&h->dw->anim, o, feng_hash_upper(script), 1);
}
void hud_screen_script_once(Hud *h, const char *name, const char *script) {   /* FEngIsScriptRunning, else FEngSetScript */
    int o = screen_obj(h, name);
    uint32_t hs = feng_hash_upper(script);
    if (o >= 0 && !feng_anim_running(&h->dw->anim, o, hs)) feng_anim_set(&h->dw->anim, o, hs, 1);
}
void hud_screen_script_all(Hud *h, const char *script) {
    if (!h || !h->dw) return;
    uint32_t hs = feng_hash_upper(script);
    for (int i = 0; i < h->pk->nobj; i++) feng_anim_set(&h->dw->anim, i, hs, 1);
}
void hud_screen_hide(Hud *h, const char *name, int hide) {
    int o = screen_obj(h, name);
    if (o >= 0) feng_anim_hide(&h->dw->anim, o, hide);
}
int hud_screen_box(const Hud *h, const char *name, float *x, float *y, float *w, float *ht) {
    int o = screen_obj(h, name);
    if (o < 0) return 0;
    const int32_t *d = h->pk->obj[o].data;
    float v[4]; memcpy(&v[0], &d[7], 4); memcpy(&v[1], &d[8], 4); memcpy(&v[2], &d[14], 4); memcpy(&v[3], &d[15], 4);
    *x = v[0]; *y = v[1]; *w = v[2]; *ht = v[3];
    return 1;
}
void hud_screen_pos(Hud *h, const char *name, float x, float y) {
    int o = screen_obj(h, name);
    if (o >= 0) feng_anim_pos(&h->dw->anim, o, x, y);
}
void hud_screen_size(Hud *h, const char *name, float w, float ht) {
    int o = screen_obj(h, name);
    if (o >= 0) feng_anim_size(&h->dw->anim, o, w, ht);
}
int hud_screen_res(Hud *h, const char *name, const char *resname) {
    int o = screen_obj(h, name);
    if (o < 0) return 0;
    for (int r = 0; r < h->pk->nres; r++)
        if (!strcmp(h->pk->res[r].name, resname)) { h->dw->res_ov[o] = (short)(r + 1); return 1; }
    return 0;
}
int hud_screen_find(const Hud *h, const char *name) { return screen_obj(h, name); }
int hud_screen_find_h(const Hud *h, uint32_t obj) {
    return h && h->pk && h->dw ? feng_find(h->pk, obj) : -1;
}
static int obj_ok(const Hud *h, int o) { return h && h->pk && h->dw && o >= 0 && o < h->pk->nobj; }
void hud_screen_obj_pos(Hud *h, int o, float x, float y) { if (obj_ok(h, o)) feng_anim_pos(&h->dw->anim, o, x, y); }
void hud_screen_obj_size(Hud *h, int o, float w, float ht) { if (obj_ok(h, o)) feng_anim_size(&h->dw->anim, o, w, ht); }
void hud_screen_obj_rot(Hud *h, int o, float deg) { if (obj_ok(h, o)) feng_anim_rot(&h->dw->anim, o, deg); }
void hud_screen_obj_color(Hud *h, int o, uint32_t argb) { if (obj_ok(h, o)) feng_anim_color(&h->dw->anim, o, argb); }
void hud_screen_obj_hide(Hud *h, int o, int hide) { if (obj_ok(h, o)) feng_anim_hide(&h->dw->anim, o, hide); }
void hud_screen_obj_script(Hud *h, int o, const char *script) {
    if (obj_ok(h, o)) feng_anim_set(&h->dw->anim, o, feng_hash_upper(script), 1);
}
void hud_screen_obj_uv(Hud *h, int o, float u0, float v0, float u1, float v1) {
    if (!obj_ok(h, o)) return;
    feng_anim_uv(&h->dw->anim, o, u0, v0, u1, v1);
    h->dw->uvo[o] = 1;
}
int hud_screen_obj_msg(Hud *h, int o, const char *msg) {
    return obj_ok(h, o) ? feng_anim_msg(&h->dw->anim, o, feng_hash_upper(msg)) : 0;
}
void hud_screen_obj_xtex(Hud *h, int o, int which) { if (obj_ok(h, o)) h->dw->xt[o] = (unsigned char)which; }
void hud_screen_obj_tex_of(Hud *h, int o, int src) {
    if (!obj_ok(h, o) || !obj_ok(h, src)) return;
    h->dw->res_ov[o] = (short)(h->dw->res_ov[src] ? h->dw->res_ov[src] : h->pk->obj[src].res + 1);
    h->dw->xt[o] = h->dw->xt[src];
    h->dw->sk_o[o] = h->dw->sk_o[src];
}
int hud_screen_keys(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n) {
    if (!h || !h->dw || !h->pk || !keys || n <= 0) return 0;
    struct HudDriftW *w = h->dw;
    uint32_t want[16]; int nw = 0;
    for (int i = 0; i < n && nw < 16; i++) {             /* the keys not loaded yet */
        int k = 0, d = 0;
        while (k < w->sk_n && w->sk_key[k] != keys[i]) k++;
        for (int j = 0; j < nw; j++) d |= want[j] == keys[i];
        if (k == w->sk_n && !d) want[nw++] = keys[i];
    }
    if (nw > 16 - w->sk_n) nw = 16 - w->sk_n;
    if (nw <= 0) return 0;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dataroot ? dataroot : ".", bundle);
    FengImage img[16]; memset(img, 0, sizeof img);
    /* the image cache as the art (a key's search is this one bundle) */
    uint32_t src = hic_src(bundle, 3, 0);
    if (!hic_fill(src, want, nw, img)) {
        for (int i = 0; i < nw; i++) feng_image_free(&img[i]);
        memset(img, 0, sizeof img);
        if (feng_tpk_load_file(path, want, nw, img) < 0) {
            long len = 0;
            unsigned char *d = hud_read_bundle(path, &len);
            if (!d) { printf("screen: %s missing\n", path); return 0; }
            feng_tpk_load(d, len, want, nw, img);
            free(d);
        }
        hic_store(src, want, nw, img);
    }
    int got = 0;
    for (int i = 0; i < nw; i++) {
        if (!img[i].rgba) { printf("screen: %s holds no %08x\n", bundle, (unsigned)want[i]); continue; }
        HudTex *t = &w->sk_tex[w->sk_n];
        t->tex = hud_upload(&img[i]); t->w = img[i].w; t->h = img[i].h; t->additive = img[i].blend == 6;
        w->sk_key[w->sk_n++] = want[i];
        feng_image_free(&img[i]);
        got++;
    }
    return got;
}
void hud_screen_obj_key(Hud *h, int o, uint32_t key) {   /* FEngSetTextureHash 0x50c990 */
    if (!obj_ok(h, o)) return;
    struct HudDriftW *w = h->dw;
    for (int k = 0; k < w->sk_n; k++)
        if (w->sk_key[k] == key) { w->sk_o[o] = (unsigned char)(k + 1); w->xt[o] = 0; return; }
    for (int r = 0; r < h->pk->nres; r++)
        if (h->pk->res[r].type == 1 && feng_tex_key(h->pk->res[r].name) == key) {
            w->res_ov[o] = (short)(r + 1); w->sk_o[o] = 0; w->xt[o] = 0; return;
        }
}
uint32_t hud_screen_obj_argb(const Hud *h, int o) {   /* as authored */
    if (!obj_ok(h, o)) return 0;
    const float *c = h->pk->obj[o].rgba;
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        int b = (int)(c[k] * 255.0f + 0.5f);
        v |= (uint32_t)(b < 0 ? 0 : b > 255 ? 255 : b) << (k == 3 ? 24 : 16 - 8 * k);
    }
    return v;
}
int hud_screen_obj_box(const Hud *h, int o, float *x, float *y, float *w, float *ht) {   /* as authored, absolute */
    if (!obj_ok(h, o)) return 0;
    const FengObj *ob = &h->pk->obj[o];
    *x = ob->ax; *y = ob->ay; *w = ob->size[0]; *ht = ob->size[1];
    return 1;
}
void hud_screen_pos_reset(Hud *h, const char *name) {
    int o = screen_obj(h, name);
    if (o < 0) return;
    float x, y; memcpy(&x, &h->pk->obj[o].data[7], 4); memcpy(&y, &h->pk->obj[o].data[8], 4);
    feng_anim_pos(&h->dw->anim, o, x, y);
}
int hud_screen_strip(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n) {
    if (!h || !h->dw || !h->pk || n <= 0) return 0;
    struct HudDriftW *w = h->dw;
    if (n > 16) n = 16;
    for (int i = 0; i < w->strip_n; i++)
        if (w->strip_tex[i]) glDeleteTextures(1, &w->strip_tex[i]);
    memset(w->strip_tex, 0, sizeof w->strip_tex);
    w->strip_master = feng_find(h->pk, feng_hash_upper("OPTION_MASTER"));
    w->strip_n = n; w->strip_sel = 0; w->strip_scroll = 0.0f; w->strip_pulse = 0.0f; w->strip_ms = 0;
    memset(w->strip_lock, 0, sizeof w->strip_lock);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dataroot ? dataroot : ".", bundle);
    FengImage img[16]; memset(img, 0, sizeof img);
    if (feng_tpk_load_file(path, keys, n, img) < 0) {
        long len = 0;
        unsigned char *d = n2_read_file(path, &len);
        if (!d) return 0;
        feng_tpk_load(d, len, keys, n, img);
        free(d);
    }
    int got = 0;
    for (int i = 0; i < n; i++) {
        if (!img[i].rgba) continue;
        w->strip_tex[i] = hud_upload(&img[i]);
        w->strip_add[i] = img[i].blend == 6;
        feng_image_free(&img[i]);
        got++;
    }
    return got;
}
/* the strip's icons still missing loaded from another bundle (a strip
   mixing GLOBALB's and InGameCommon's icons); returns those found */
int hud_screen_strip_more(Hud *h, const char *dataroot, const char *bundle, const uint32_t *keys, int n) {
    if (!h || !h->dw || n <= 0) return 0;
    struct HudDriftW *w = h->dw;
    if (n > w->strip_n) n = w->strip_n;
    uint32_t want[16]; int idx[16], m = 0;
    for (int i = 0; i < n; i++) if (!w->strip_tex[i]) { want[m] = keys[i]; idx[m++] = i; }
    if (!m) return 0;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dataroot ? dataroot : ".", bundle);
    FengImage img[16]; memset(img, 0, sizeof img);
    if (feng_tpk_load_file(path, want, m, img) < 0) {
        long len = 0;
        unsigned char *d = n2_read_file(path, &len);
        if (!d) return 0;
        feng_tpk_load(d, len, want, m, img);
        free(d);
    }
    int got = 0;
    for (int k = 0; k < m; k++) {
        if (!img[k].rgba) continue;
        w->strip_tex[idx[k]] = hud_upload(&img[k]);
        w->strip_add[idx[k]] = img[k].blend == 6;
        feng_image_free(&img[k]);
        got++;
    }
    return got;
}
/* an option that can't be picked: drawn at the locked alpha (0x51fd60, 150) */
void hud_screen_strip_lock(Hud *h, int i, int lock) {
    if (h && h->dw && i >= 0 && i < 16) h->dw->strip_lock[i] = (unsigned char)(lock != 0);
}
void hud_screen_strip_select(Hud *h, int sel) {
    if (!h || !h->dw || sel < 0 || sel >= h->dw->strip_n || sel == h->dw->strip_sel) return;
    h->dw->strip_sel = sel;
    h->dw->strip_pulse = 0.0f;
}
/* the strip's easing per 1/60 s frame: scroll 0.3 of the way to the
   selection ([+0xb4]), pulse phase += 0.03 over 0..2 */
static void strip_tick(struct HudDriftW *w, int dt_ms) {
    w->strip_ms += dt_ms;
    for (; w->strip_ms >= 17; w->strip_ms -= 17) {
        float target = (float)w->strip_sel;
        w->strip_scroll += (target - w->strip_scroll) * 0.3f;
        if (fabsf(target - w->strip_scroll) < 0.001f) w->strip_scroll = target;
        w->strip_pulse += 0.03f;
        if (w->strip_pulse > 2.0f) w->strip_pulse = 0.0f;
    }
}
void hud_screen_draw(Hud *h, const RProg *rp, GpuMesh *quad, int width, int height, int dt_ms) {
    if (!h || !h->dw) return;
    feng_anim_tick(&h->dw->anim, dt_ms);
    if (h->dw->strip_n) strip_tick(h->dw, dt_ms);
    HudState s; memset(&s, 0, sizeof s);
    hud_draw(h, rp, quad, &s, width, height);
}

void hud_free(Hud **hp) {
    if (!hp || !*hp) return;
    Hud *h = *hp;
    for (int i = h->upi; i < h->npend; i++) feng_image_free(&h->pend[i].im);   /* never uploaded */
    free(h->pend);
    for (int r = 0; r < FENG_MAX_RES; r++)
        if (h->rt[r].tex) glDeleteTextures(1, &h->rt[r].tex);
    for (int f = 0; f < HUD_NFONT; f++)
        if (h->font[f].tex) glDeleteTextures(1, &h->font[f].tex);
    if (h->arrow_tex) glDeleteTextures(1, &h->arrow_tex);
    if (h->cross_tex) glDeleteTextures(1, &h->cross_tex);
    if (h->tick_tex) glDeleteTextures(1, &h->tick_tex);
    if (h->map_tex) glDeleteTextures(1, &h->map_tex);
    if (h->tm_tex) glDeleteTextures(1, &h->tm_tex);
    if (h->fan_vbo) glDeleteBuffers(1, &h->fan_vbo);
    if (h->fan_nbo) glDeleteBuffers(1, &h->fan_nbo);
    if (h->fan_ibo) glDeleteBuffers(1, &h->fan_ibo);
    if (h->qb_vbo) glDeleteBuffers(1, &h->qb_vbo);
    if (h->qb_nbo) glDeleteBuffers(1, &h->qb_nbo);
    if (h->qb_ibo) glDeleteBuffers(1, &h->qb_ibo);
    if (h->dw) {
        for (int i = 0; i < h->dw->strip_n; i++)
            if (h->dw->strip_tex[i]) glDeleteTextures(1, &h->dw->strip_tex[i]);
        if (h->dw->ov_num.tex) glDeleteTextures(1, &h->dw->ov_num.tex);
        if (h->dw->ov_red.tex) glDeleteTextures(1, &h->dw->ov_red.tex);
        for (int k = 0; k < EI_NTEX; k++) if (h->dw->ei_tex[k].tex) glDeleteTextures(1, &h->dw->ei_tex[k].tex);
        for (int k = 0; k < h->dw->sk_n; k++) if (h->dw->sk_tex[k].tex) glDeleteTextures(1, &h->dw->sk_tex[k].tex);
        for (int k = 0; k < 2; k++) if (h->dw->tov[k].tex && !h->dw->tov_borrow[k]) glDeleteTextures(1, &h->dw->tov[k].tex);
        for (int i = 0; i < FENG_MAX_OBJ; i++) free(h->dw->txt[i]);
        free(h->dw->lang); free(h->dw->sh); free((void *)h->dw->st); free(h->dw);
    }
    feng_free_package(h->pk);
    free(h->pk);
    free(h);
    *hp = NULL;
}

/* One capsule (thick segment) into an 8-bit image: value = max(existing,
   shade), with a one-pixel soft edge. Coordinates in pixels. */
static void map_capsule(unsigned char *img, int size, float ax, float ay,
                        float bx, float by, float r, float shade) {
    int x0 = (int)floorf((ax < bx ? ax : bx) - r - 1), x1 = (int)ceilf((ax > bx ? ax : bx) + r + 1);
    int y0 = (int)floorf((ay < by ? ay : by) - r - 1), y1 = (int)ceilf((ay > by ? ay : by) + r + 1);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= size) x1 = size - 1; if (y1 >= size) y1 = size - 1;
    float ex = bx - ax, ey = by - ay, l2 = ex*ex + ey*ey;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = l2 > 1e-6f ? (px*ex + py*ey) / l2 : 0.0f;
            if (t < 0) t = 0; else if (t > 1) t = 1;
            float dx = px - t*ex, dy = py - t*ey;
            float d = sqrtf(dx*dx + dy*dy) - r;          /* <0 inside */
            float cover = d <= -0.5f ? 1.0f : d >= 0.5f ? 0.0f : 0.5f - d;
            if (cover <= 0.0f) continue;
            unsigned char *p = &img[(size_t)y*size + x];
            float v = *p + (shade - *p) * cover;
            if (v > *p) *p = (unsigned char)v;
        }
}

void hud_map_build(Hud *h, const float *xy, const int *next,
                   const float *half_width, int n) {
    if (!h || !xy || !next || n < 2) return;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int i = 0; i < n; i++) {
        if (xy[i*2] < x0) x0 = xy[i*2]; if (xy[i*2] > x1) x1 = xy[i*2];
        if (xy[i*2+1] < y0) y0 = xy[i*2+1]; if (xy[i*2+1] > y1) y1 = xy[i*2+1];
    }
    float ext = (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 200.0f;
    if (!(ext > 0.0f) || ext > 100000.0f) return;
    x0 -= 100.0f; y0 -= 100.0f;
    unsigned char *img = (unsigned char *)malloc((size_t)MAP_TEX * MAP_TEX);
    if (!img) return;
    memset(img, 40, (size_t)MAP_TEX * MAP_TEX);              /* dark blocks */
    float s = MAP_TEX / ext;                                   /* px per metre */
    /* two passes: a darker kerb outline under every road, then the roads */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            int j = next[i];
            if (j < 0 || j >= n) continue;
            float hw = half_width ? 0.5f*(half_width[i] + half_width[j]) : 5.0f;
            if (!(hw > 0.5f)) hw = 5.0f;
            if (hw > 30.0f) hw = 30.0f;
            float r = hw * s;
            if (r < 1.2f) r = 1.2f;                            /* keep alleys visible */
            float shade = hw >= 9.0f ? 235.0f : 150.0f + hw * 8.0f;   /* wide = bright */
            float ax = (xy[i*2]-x0)*s, ay = (xy[i*2+1]-y0)*s;
            float bx = (xy[j*2]-x0)*s, by = (xy[j*2+1]-y0)*s;
            if ((ax-bx)*(ax-bx) + (ay-by)*(ay-by) > 400.0f*s*400.0f*s) continue;  /* route jump */
            if (pass == 0) map_capsule(img, MAP_TEX, ax, ay, bx, by, r + 1.0f, 70.0f);
            else           map_capsule(img, MAP_TEX, ax, ay, bx, by, r, shade);
        }
    glGenTextures(1, &h->map_tex);
    glBindTexture(GL_TEXTURE_2D, h->map_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, MAP_TEX, MAP_TEX, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, img);
    free(img);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    h->map_x0 = x0; h->map_y0 = y0; h->map_ext = ext;

    /* the fan: centre + MAP_FAN rim vertices; positions/UVs filled per frame */
    float nrm[(MAP_FAN + 1) * 3];
    for (int i = 0; i <= MAP_FAN; i++) { nrm[i*3] = 0; nrm[i*3+1] = 0; nrm[i*3+2] = 1; }
    unsigned short idx[MAP_FAN * 3];
    for (int i = 0; i < MAP_FAN; i++) {
        idx[i*3] = 0; idx[i*3+1] = (unsigned short)(1 + i);
        idx[i*3+2] = (unsigned short)(1 + (i + 1) % MAP_FAN);
    }
    glGenBuffers(1, &h->fan_vbo);
    glGenBuffers(1, &h->fan_nbo);
    glGenBuffers(1, &h->fan_ibo);
    glBindBuffer(GL_ARRAY_BUFFER, h->fan_nbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof nrm, nrm, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, h->fan_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, h->fan_vbo);
    glBufferData(GL_ARRAY_BUFFER, (MAP_FAN + 1) * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    printf("minimap: street map %dx%d over %.0f m (%.1f m/px) from %d road nodes\n",
           MAP_TEX, MAP_TEX, (double)ext, (double)(ext / MAP_TEX), n);
}

/* the fan's buffers (centre + MAP_FAN rim vertices, filled per frame) */
static void hud_map_fan(Hud *h) {
    if (h->fan_vbo) return;
    float nrm[(MAP_FAN + 1) * 3];
    for (int i = 0; i <= MAP_FAN; i++) { nrm[i*3] = 0; nrm[i*3+1] = 0; nrm[i*3+2] = 1; }
    unsigned short idx[MAP_FAN * 3];
    for (int i = 0; i < MAP_FAN; i++) {
        idx[i*3] = 0; idx[i*3+1] = (unsigned short)(1 + i);
        idx[i*3+2] = (unsigned short)(1 + (i + 1) % MAP_FAN);
    }
    glGenBuffers(1, &h->fan_vbo);
    glGenBuffers(1, &h->fan_nbo);
    glGenBuffers(1, &h->fan_ibo);
    glBindBuffer(GL_ARRAY_BUFFER, h->fan_nbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof nrm, nrm, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, h->fan_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, h->fan_vbo);
    glBufferData(GL_ARRAY_BUFFER, (MAP_FAN + 1) * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
}
int hud_set_trackmap(Hud *h, const char *dataroot, int event,
                     float x0, float y0, float size, float zoom) {
    if (!h || !(size > 0) || !(zoom > 0)) return 0;
    h->tm_x0 = x0; h->tm_y0 = y0; h->tm_size = size; h->tm_zoom = zoom;
    if (event == h->tm_event && h->tm_tex) return 1;
    if (!h->pk || feng_find(h->pk, FE_MAP_SLOT) < 0) return 0;  /* no minimap in this HUD */
    char path[1024], name[32];
    snprintf(path, sizeof path, "%s/TRACKS/TRACKMAP%d.BIN", dataroot ? dataroot : ".", event);
    snprintf(name, sizeof name, "TRACKMAP%d", event);
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d) { printf("minimap: %s missing\n", path); return 0; }
    uint32_t key = 0xffffffffu;                                  /* bin hash of the name */
    for (const char *c = name; *c; c++) key = key * 33u + (unsigned char)*c;
    FengImage img; memset(&img, 0, sizeof img);
    int ok = feng_tpk_load(d, len, &key, 1, &img) && img.rgba;
    free(d);
    if (!ok) { printf("minimap: %s holds no %s\n", path, name); return 0; }
    /* its alpha is 0 throughout: Track4000_map draws it by colour alone */
    for (int i = 0; i < img.w * img.h; i++) img.rgba[i * 4 + 3] = 255;
    if (!h->tm_tex) glGenTextures(1, &h->tm_tex);
    glBindTexture(GL_TEXTURE_2D, h->tm_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    feng_image_free(&img);
    hud_map_fan(h);
    h->tm_event = event;
    printf("minimap: %s, %.0f m from (%.0f, %.0f), zoom %.2f\n", name, (double)size, (double)x0, (double)y0, (double)zoom);
    return 1;
}

/* The World Expansion's minimap (beyond.h): a converted square map in the
   TRACKMAP layout. "BMAP", u32 w, u32 h, f32 x0, y0 (top-left), size, zoom,
   then w*h RGBA. Kept under the pseudo event -2 so the city's TRACKMAP
   reloads when the track switches back. */
int hud_set_trackmap_file(Hud *h, const char *path) {
    if (!h) return 0;
    static char cur[256], bad[256];   /* the map loaded / the file that failed (one per world) */
    if (h->tm_event == -2 && h->tm_tex && !strcmp(cur, path)) return 1;
    if (!h->pk || feng_find(h->pk, FE_MAP_SLOT) < 0) return 0;
    if (!strcmp(bad, path)) return 0;
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    uint32_t w = 0, hh = 0; float fl[4] = { 0 };
    if (d && len >= 28 && !memcmp(d, "BMAP", 4)) { memcpy(&w, d + 4, 4); memcpy(&hh, d + 8, 4); memcpy(fl, d + 12, 16); }
    if (!d || !w || w > 4096 || hh != w || (long)(28 + (size_t)w * hh * 4) > len || !(fl[2] > 0) || !(fl[3] > 0)) {
        printf("minimap: %s missing or unreadable\n", path);
        free(d); snprintf(bad, sizeof bad, "%s", path); return 0;
    }
    if (!h->tm_tex) glGenTextures(1, &h->tm_tex);
    glBindTexture(GL_TEXTURE_2D, h->tm_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)w, (int)hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, d + 28);
    free(d);
    snprintf(cur, sizeof cur, "%s", path);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    hud_map_fan(h);
    h->tm_x0 = fl[0]; h->tm_y0 = fl[1]; h->tm_size = fl[2]; h->tm_zoom = fl[3];
    h->tm_event = -2;
    printf("minimap: %s, %ux%u, %.0f m from (%.0f, %.0f), zoom %.2f\n", path, (unsigned)w, (unsigned)hh,
           (double)fl[2], (double)fl[0], (double)fl[1], (double)fl[3]);
    return 1;
}

void hud_set_preview(Hud *h, int on) { if (h) h->preview = on ? 1 : 0; }
int  hud_preview(const Hud *h) { return h ? h->preview : 0; }
const char *hud_missing(const Hud *h) { return h ? h->missing : ""; }

/* ----------------------------------------------------------------- drawing */

typedef struct { const RProg *rp; GpuMesh *quad; float asp; } HudDC;

/* Place the unit quad (local 0..1) as a rotated rectangle centred at (cx,cy)
 * NDC, sized (w,h) in HUD units. Width is divided by the aspect so a square
 * stays square at 16:9 and at 4:3 alike — the whole reason the layout is
 * authored in units rather than NDC. */
static void hud_xform(float asp, float w, float h, float cx, float cy,
                      float rot, float *M) {
    /* Texture rows run top-down but the quad's v=0 corner is at the BOTTOM, so
       every sprite would render upside down. Negating the height maps v=0 to
       the top instead, which is also the right handedness for rotation: local
       +v then points "down the image", as the art is authored. */
    h = -h;
    float wx = w / asp, c = cosf(rot), s = sinf(rot);
    /* rotation happens in SCREEN space, so the x component is un-squashed,
       rotated, then re-squashed; otherwise a rotated icon shears at 4:3. */
    M[0] = wx * c;        M[1] = w * s;    M[2] = 0; M[3] = 0;
    M[4] = -h * s / asp;  M[5] = h * c;    M[6] = 0; M[7] = 0;
    M[8] = 0; M[9] = 0;   M[10] = 1;       M[11] = 0;
    M[12] = cx - 0.5f * (M[0] + M[4]);
    M[13] = cy - 0.5f * (M[1] + M[5]);
    M[14] = 0; M[15] = 1;
}

/* Two different shader branches, and picking the wrong one is silent:
 * uUnlit outputs uColor and NEVER samples the texture, so a sprite drawn
 * through it is a flat filled rectangle. uEmissiveTex is the textured unlit
 * path -- t.rgb * uColor with alpha t.a * uAlpha -- which is exactly an
 * alpha-blended HUD sprite tinted by uColor. */
static void hud_blit(const HudDC *dc, GLuint tex, float w, float h,
                     float cx, float cy, float rot,
                     float r, float g, float b, float a) {
    if (a <= 0.0f) return;
    float M[16];
    hud_xform(dc->asp, w, h, cx, cy, rot, M);
    glUniformMatrix4fv(dc->rp->uMVP, 1, GL_FALSE, M);
    if (tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1f(dc->rp->uUseTex, 1.0f);
        glUniform1f(dc->rp->uEmissiveTex, 1.0f);
        glUniform1f(dc->rp->uUnlit, 0.0f);
    } else {
        glUniform1f(dc->rp->uUseTex, 0.0f);
        glUniform1f(dc->rp->uEmissiveTex, 0.0f);
        glUniform1f(dc->rp->uUnlit, 1.0f);
    }
    glUniform3f(dc->rp->uColor, r, g, b);
    glUniform1f(dc->rp->uAlpha, a);
    draw_gpumesh(dc->quad);
}

/* `size` is the CAP HEIGHT of the text in HUD units. draw_text's px/py are the
 * size of one font cell and the glyph is 5 cells tall, hence the /5 -- passing
 * the cap height straight through makes every label five times too big. */
static void hud_label(const HudDC *dc, const char *s, float cx, float cy,
                      float size, float r, float g, float b, float a) {
    float py = size / 5.0f, px = py / dc->asp;
    glUniform1f(dc->rp->uUseTex, 0.0f);
    glUniform1f(dc->rp->uEmissiveTex, 0.0f);
    glUniform1f(dc->rp->uUnlit, 1.0f);
    glUniform3f(dc->rp->uColor, r, g, b);
    glUniform1f(dc->rp->uAlpha, a);
    /* draw_text takes a TOP-LEFT origin; centre the run on (cx,cy). */
    draw_text(dc->quad, dc->rp->uMVP, s,
              cx - (float)strlen(s) * 2.0f * px, cy + 2.5f * py, px, py);
}

/* ------------------------------------------------------------ FEng drawing */

/* Draw object i's own art centred on FEng point (fx, fy), turned `angle`
 * radians clockwise about its authored pivot, tinted rgba. */
static void fe_sprite_at(Hud *h, const HudDC *dc, int i, float fx, float fy,
                         float angle, const float *rgba) {
    const FengObj *o = &h->pk->obj[i];
    if (o->res < 0 || o->res >= h->pk->nres || !h->rt[o->res].tex) return;
    const HudTex *t = &h->rt[o->res];
    /* rotate about centre + pivot: centre' = P + R(angle) * (centre - P) */
    float px = o->pivot[0], py = o->pivot[1], c = cosf(angle), s = sinf(angle);
    float cx = fx + px + (-px * c + py * s), cy = fy + py + (-px * s - py * c);
    float nx, ny;
    fe_ndc(dc->asp, hud_pin_of(h->pk->obj[o->root].ax), cx, cy, &nx, &ny);
    if (t->additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    /* FEng turns clockwise on a y-down screen; hud_xform is CCW, y-up */
    hud_blit(dc, t->tex, fabsf(o->size[0]) / 240.0f, fabsf(o->size[1]) / 240.0f,
             nx, ny, -angle, rgba[0], rgba[1], rgba[2], rgba[3]);
    if (t->additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void fe_sprite(Hud *h, const HudDC *dc, int i) {
    const FengObj *o = &h->pk->obj[i];
    fe_sprite_at(h, dc, i, o->ax, o->ay, o->aangle, o->rgba);
}

/* Object i's art at an NDC point (minimap markers), unrotated. */
static void fe_sprite_ndc(Hud *h, const HudDC *dc, int i, float nx, float ny,
                          const float *rgba) {
    if (i < 0) return;
    const FengObj *o = &h->pk->obj[i];
    if (o->res < 0 || o->res >= h->pk->nres || !h->rt[o->res].tex) return;
    hud_blit(dc, h->rt[o->res].tex, fabsf(o->size[0]) / 240.0f,
             fabsf(o->size[1]) / 240.0f, nx, ny, 0.0f,
             rgba[0], rgba[1], rgba[2], rgba[3]);
}

/* Draw `nq` quads from the batch (x,y,z,u,v per vertex, NDC) with one texture. */
static void hud_batch(Hud *h, const HudDC *dc, GLuint tex, const float *v, int nq,
                      const float *rgba) {
    if (nq <= 0 || !tex) return;
    glBindBuffer(GL_ARRAY_BUFFER, h->qb_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nq * 4 * 5 * sizeof(float)), v, GL_DYNAMIC_DRAW);
    static const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(dc->rp->uMVP, 1, GL_FALSE, ident);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1f(dc->rp->uUseTex, 1.0f);
    glUniform1f(dc->rp->uEmissiveTex, 1.0f);
    glUniform1f(dc->rp->uUnlit, 0.0f);
    glUniform3f(dc->rp->uColor, rgba[0], rgba[1], rgba[2]);
    glUniform1f(dc->rp->uAlpha, rgba[3]);
    GpuMesh m; memset(&m, 0, sizeof m);
    m.vbo = h->qb_vbo; m.nbo = h->qb_nbo; m.ibo = h->qb_ibo; m.nidx = nq * 6;
    draw_gpumesh(&m);
}

static void put_vert(float *v, float x, float y, float u, float t) {
    v[0] = x; v[1] = y; v[2] = 0.0f; v[3] = u; v[4] = t;
}

/* The console FNGs' button prompts: a text object whose string is the token
 * $JOY_EVENT_FENG_<button>$ drew the pad button's glyph from the console's
 * button font. The PC build has no such font (it hides the prompts and puts
 * up the mouse bar UI_PC_Help_Bar instead), so the Vita draws its own
 * PlayStation glyphs, one line of the object's font high. The buttons are
 * the ones main.c binds to the pad events: FENG_SELECT = PAD_ACCEPT (Cross),
 * FENG_CANCEL = PAD_BACK (Circle), FENG_BUTTON1 = Triangle, FENG_BUTTON0 =
 * Square. The menus (shop.c) draw the same glyphs through hud_pad_glyph,
 * with the d-pad and stick ones their help text adds. */
enum { BTN_CROSS = HUD_PAD_CROSS, BTN_CIRCLE = HUD_PAD_CIRCLE, BTN_TRIANGLE = HUD_PAD_TRIANGLE,
       BTN_SQUARE = HUD_PAD_SQUARE, BTN_N = HUD_PAD_N };
static GLuint g_btn_tex[BTN_N];
static int btn_of_token(const char *t) {
    static const struct { const char *tok; int b; } k[] = {
        { "$JOY_EVENT_FENG_SELECT$", BTN_CROSS }, { "$JOY_EVENT_FENG_CANCEL$", BTN_CIRCLE },
        { "$JOY_EVENT_FENG_BUTTON1$", BTN_TRIANGLE }, { "$JOY_EVENT_FENG_BUTTON0$", BTN_SQUARE },
        { "$JOY_EVENT_FENG_LTRIGGER$", HUD_PAD_LBTN }, { "$JOY_EVENT_FENG_RTRIGGER$", HUD_PAD_RBTN },
        { "$JOY_EVENT_FENG_START$", HUD_PAD_START } };
    for (size_t n = 0; n < sizeof k / sizeof k[0]; n++) if (!strcmp(t, k[n].tok)) return k[n].b;
    return -1;
}
static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float ex = px - ax - t * dx, ey = py - ay - t * dy;
    return sqrtf(ex * ex + ey * ey);
}
static GLuint btn_tex(int b) {
    if (g_btn_tex[b]) return g_btn_tex[b];
    enum { S = 64 };
    static const unsigned char col[BTN_N][3] = {
        { 124, 178, 232 }, { 255, 102, 102 }, { 64, 226, 160 }, { 255, 105, 248 },
        { 235, 235, 235 }, { 235, 235, 235 }, { 235, 235, 235 }, { 235, 235, 235 },
        { 235, 235, 235 }, { 235, 235, 235 }, { 235, 235, 235 } };
    unsigned char *img = malloc(S * S * 4);
    if (!img) return 0;
    const float c = 0.5f * S, rd = 30.0f, st = 2.6f;   /* disc radius, half stroke */
    const float tr = 13.0f;                            /* triangle circumradius */
    float tx[3], ty[3];
    for (int k = 0; k < 3; k++) {
        float a = -1.5707963f + (float)k * 2.0943951f;
        tx[k] = c + tr * cosf(a); ty[k] = c + 2.0f + tr * sinf(a);
    }
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float px = x + 0.5f, py = y + 0.5f, dx = px - c, dy = py - c;
            float r = sqrtf(dx * dx + dy * dy), d;
            if (b == BTN_CROSS)
                d = fminf(seg_dist(px, py, c - 11, c - 11, c + 11, c + 11),
                          seg_dist(px, py, c - 11, c + 11, c + 11, c - 11));
            else if (b == BTN_CIRCLE) d = fabsf(r - 12.0f);
            else if (b == BTN_TRIANGLE)
                d = fminf(seg_dist(px, py, tx[0], ty[0], tx[1], ty[1]),
                          fminf(seg_dist(px, py, tx[1], ty[1], tx[2], ty[2]),
                                seg_dist(px, py, tx[2], ty[2], tx[0], ty[0])));
            else if (b == BTN_SQUARE) d = fabsf(fmaxf(fabsf(dx), fabsf(dy)) - 10.5f);
            else if (b == HUD_PAD_DPAD_UD || b == HUD_PAD_DPAD_LR) {
                /* a d-pad: the plus, its two lit arms in white, the others grey */
                float ax = fabsf(dx), ay = fabsf(dy), w = 6.0f, l = 22.0f;
                int in = (ax <= w && ay <= l) || (ay <= w && ax <= l);
                int lit = b == HUD_PAD_DPAD_UD ? ay > w : ax > w;
                d = in ? 0.0f : 9.0f;
                if (in && !lit) d = 1.6f;   /* half-lit: grey (see sym below) */
            } else if (b == HUD_PAD_START) {   /* START: the Vita's small pill button, outlined */
                d = fabsf(seg_dist(px, py, c - 11.0f, c, c + 11.0f, c) - 6.5f);
            } else if (b == HUD_PAD_LBTN || b == HUD_PAD_RBTN) {
                /* a shoulder button: its edge as a bar along the top, the letter below */
                float x0 = c - 5.0f, t = c - 6.0f, m = c + 4.0f, bt = c + 14.0f;
                d = seg_dist(px, py, c - 13.0f, c - 15.0f, c + 13.0f, c - 15.0f);
                d = fminf(d, seg_dist(px, py, x0, t, x0, bt));
                if (b == HUD_PAD_LBTN) d = fminf(d, seg_dist(px, py, x0, bt, c + 7.0f, bt));
                else {
                    d = fminf(d, fminf(seg_dist(px, py, x0, t, c + 1.0f, t), seg_dist(px, py, x0, m, c + 1.0f, m)));
                    if (px >= c + 1.0f)
                        d = fminf(d, fabsf(sqrtf((px - c - 1.0f) * (px - c - 1.0f) + (py - c + 1.0f) * (py - c + 1.0f)) - 5.0f));
                    d = fminf(d, seg_dist(px, py, c, m, c + 8.0f, bt));
                }
            } else {   /* a stick: a cap with L or R on it */
                float x0 = c - 6.0f, t = c - 10.0f, m = c, bt = c + 10.0f;
                d = fminf(seg_dist(px, py, x0, t, x0, bt), 99.0f);
                if (b == HUD_PAD_LSTICK) d = fminf(d, seg_dist(px, py, x0, bt, c + 7.0f, bt));
                else {
                    d = fminf(d, fminf(seg_dist(px, py, x0, t, c, t), seg_dist(px, py, x0, m, c, m)));
                    if (px >= c) d = fminf(d, fabsf(sqrtf((px - c) * (px - c) + (py - c + 5.0f) * (py - c + 5.0f)) - 5.0f));
                    d = fminf(d, seg_dist(px, py, c - 1.0f, m, c + 7.0f, bt));
                }
            }
            float sym = fminf(fmaxf(st + 0.5f - d, 0.0f), 1.0f);
            float disc = fminf(fmaxf(rd + 0.5f - r, 0.0f), 1.0f);
            float ring = fminf(fmaxf(1.5f - fabsf(r - (rd - 1.5f)), 0.0f), 1.0f);
            if ((b == HUD_PAD_DPAD_UD || b == HUD_PAD_DPAD_LR) && d > 1.0f && d < 2.0f) sym = 0.45f;
            float g = 28.0f + 120.0f * ring;               /* dark face, light rim */
            unsigned char *p = img + (y * S + x) * 4;
            for (int k = 0; k < 3; k++) p[k] = (unsigned char)(g + (col[b][k] - g) * sym);
            p[3] = (unsigned char)(255.0f * disc);
        }
    FengImage im; memset(&im, 0, sizeof im);
    im.w = S; im.h = S; im.rgba = img;
    g_btn_tex[b] = hud_upload(&im);
    free(img);
    return g_btn_tex[b];
}
GLuint hud_pad_glyph(int b) { return b >= 0 && b < BTN_N ? btn_tex(b) : 0; }
/* the glyph a line high, placed as the object's justification places text */
static void fe_button_at(Hud *h, const HudDC *dc, int i, int b, float lineh,
                         float x, float y, const float *rgba) {
    const FengObj *o = &h->pk->obj[i];
    GLuint tex = btn_tex(b);
    if (!tex) return;
    float s = lineh;
    if (o->just & FENG_JUST_HCENTER) x -= 0.5f * s;
    else if (o->just & FENG_JUST_RIGHT) x -= s;
    if (o->just & FENG_JUST_VCENTER) y -= 0.5f * s;
    else if (o->just & FENG_JUST_BOTTOM) y -= s;
    int pin = h->dw && h->dw->screen ? screen_pin(h->dw) : hud_pin_of(h->pk->obj[o->root].ax);
    float ax, ay, bx, by, v[20];
    fe_ndc(dc->asp, pin, x, y, &ax, &ay);
    fe_ndc(dc->asp, pin, x + s, y + s, &bx, &by);
    put_vert(v,      ax, ay, 0.0f, 0.0f);
    put_vert(v + 5,  bx, ay, 1.0f, 0.0f);
    put_vert(v + 10, bx, by, 1.0f, 1.0f);
    put_vert(v + 15, ax, by, 0.0f, 1.0f);
    const float white[4] = { 1.0f, 1.0f, 1.0f, rgba[3] };
    hud_batch(h, dc, tex, v, 1, white);
}

/* An FEng string in the game's own bitmap font, anchored and justified as
 * authored (Sj), at the object's scale. */
static void fe_text_at(Hud *h, const HudDC *dc, int i, const char *txt,
                       float x, float y, float sc, const float *rgba) {
    const FengObj *o = &h->pk->obj[i];
    if (!txt || !*txt || o->res < 0 || o->res >= h->pk->nres) return;
    int fi = h->font_of[o->res];
    if (fi < 0 || !h->font[fi].ok) return;
    const HudFont *F = &h->font[fi];
    if (*txt == '$') {
        int b = btn_of_token(txt);
        if (b >= 0) { fe_button_at(h, dc, i, b, (float)F->f.line * sc, x, y, rgba); return; }
    }
    /* 0x51c110: '\n' and '^' start a line, (leading Sl + the font's line)
       apart; each line justified on its own, the block centred / bottomed
       as a whole (the height: 0x50bf40 for left/top text, else lines x
       (leading + line)) */
    char wrapped[1024];
    if (h->dw && h->dw->wrap && o->wrapw > 0) {         /* the line breaks at the last space before Sw */
        int n = 0, line0 = 0, sp = -1;
        for (const char *q = txt; *q && n < (int)sizeof wrapped - 1; q++) {
            char ch = *q == '\n' ? '^' : *q;
            wrapped[n++] = ch;
            if (ch == '^') { line0 = n; sp = -1; continue; }
            if (ch == ' ') sp = n - 1;
            wrapped[n] = 0;
            if (sp >= 0 && feng_text_width(&F->f, wrapped + line0) > (float)o->wrapw) {
                wrapped[sp] = '^'; line0 = sp + 1; sp = -1;
            }
        }
        wrapped[n] = 0;
        txt = wrapped;
    }
    int nl = 1;
    for (const char *c = txt; *c; c++) if (*c == '\n' || *c == '^') nl++;
    float lh = ((float)o->lead + (float)F->f.line) * sc;
    float hgt = (o->just & 5) ? lh * (float)nl : lh * (float)(nl - 1) + (float)F->f.line * sc;
    if (o->just & FENG_JUST_VCENTER) y -= 0.5f * hgt;
    else if (o->just & FENG_JUST_BOTTOM) y -= hgt;
    float xl = x;
    int pin = h->dw && h->dw->screen ? screen_pin(h->dw) : hud_pin_of(h->pk->obj[o->root].ax), nq = 0;
    float v[HUD_QMAX * 4 * 5];
    int line_start = 1;
    for (const char *c = txt; *c && nq < HUD_QMAX; c++) {
        if (line_start) {
            char ln[256]; int n = 0;
            for (const char *e = c; *e && *e != '\n' && *e != '^' && n < 255; e++) ln[n++] = *e;
            ln[n] = 0;
            float w = feng_text_width(&F->f, ln) * sc;
            x = xl;
            if (o->just & FENG_JUST_HCENTER) x -= 0.5f * w;
            else if (o->just & FENG_JUST_RIGHT) x -= w;
            line_start = 0;
        }
        if (*c == '\n' || *c == '^') { y += lh; line_start = 1; continue; }
        const FengGlyph *g = &F->f.g[(unsigned char)*c];
        if (!g->ok) continue;
        if (g->w && g->h) {
            float x0 = x + g->xo * sc, y0 = y + g->yo * sc;
            float x1 = x0 + g->w * sc, y1 = y0 + g->h * sc;
            float ax, ay, bx, by;
            fe_ndc(dc->asp, pin, x0, y0, &ax, &ay);
            fe_ndc(dc->asp, pin, x1, y1, &bx, &by);
            float u0 = (float)g->u / F->aw, v0 = (float)g->v / F->ah;
            float u1 = (float)(g->u + g->w) / F->aw, v1 = (float)(g->v + g->h) / F->ah;
            float *q = v + nq * 20;
            put_vert(q,      ax, ay, u0, v0);
            put_vert(q + 5,  bx, ay, u1, v0);
            put_vert(q + 10, bx, by, u1, v1);
            put_vert(q + 15, ax, by, u0, v1);
            nq++;
            /* a wrapping screen's long message: the full batch out, the rest after it */
            if (nq == HUD_QMAX && h->dw && h->dw->wrap) { hud_batch(h, dc, F->tex, v, nq, rgba); nq = 0; }
        }
        x += g->adv * sc;
    }
    hud_batch(h, dc, F->tex, v, nq, rgba);
}

static void fe_text(Hud *h, const HudDC *dc, int i, const char *txt,
                    const float *rgba) {
    const FengObj *o = &h->pk->obj[i];
    fe_text_at(h, dc, i, txt, o->ax, o->ay, o->size[0], rgba);
}

/* The N2O level: Tach_NOS drawn only inside the sector from the scale's empty
 * end to `fill`, about the arc centre measured from Tach_NOS_Alpha. Outside
 * the art the clamped edge texel is black, which adds nothing. */
static void fe_nos_sector(Hud *h, const HudDC *dc, int i, float fill,
                          const float *rgba) {
    const FengObj *o = &h->pk->obj[i];
    const struct HudArc *arc = &h->nos_arc;
    if (!arc->ok || o->res < 0 || !h->rt[o->res].tex) return;
    float sw = fabsf(o->size[0]), sh = fabsf(o->size[1]);
    float left = o->ax - 0.5f * sw, top = o->ay - 0.5f * sh;
    float cx = left + arc->cx * sw, cy = top + sh - arc->cy * sw;
    float R = arc->r * sw * 1.35f;
    float a_end = arc->a0 + (arc->a1 - arc->a0) * fill;
    int pin = hud_pin_of(h->pk->obj[o->root].ax);
    const int K = 32;
    float v[32 * 20], cnx, cny;
    fe_ndc(dc->asp, pin, cx, cy, &cnx, &cny);
    for (int k = 0; k < K; k++) {
        float t0 = arc->a0 + (a_end - arc->a0) * (float)k / K;
        float t1 = arc->a0 + (a_end - arc->a0) * (float)(k + 1) / K;
        float p0x = cx + cosf(t0) * R, p0y = cy - sinf(t0) * R;   /* art y-up */
        float p1x = cx + cosf(t1) * R, p1y = cy - sinf(t1) * R;
        float n0x, n0y, n1x, n1y;
        fe_ndc(dc->asp, pin, p0x, p0y, &n0x, &n0y);
        fe_ndc(dc->asp, pin, p1x, p1y, &n1x, &n1y);
        float *q = v + k * 20;
        put_vert(q,      cnx, cny, (cx - left) / sw, (cy - top) / sh);
        put_vert(q + 5,  n0x, n0y, (p0x - left) / sw, (p0y - top) / sh);
        put_vert(q + 10, n1x, n1y, (p1x - left) / sw, (p1y - top) / sh);
        put_vert(q + 15, n1x, n1y, (p1x - left) / sw, (p1y - top) / sh);
    }
    if (h->rt[o->res].additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    hud_batch(h, dc, h->rt[o->res].tex, v, K, rgba);
    if (h->rt[o->res].additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* ---------------------------------------------------------------- minimap */

#define MAP_VIEW  260.0f   /* world metres from centre to panel edge */

/* One definition of where the panel sits, shared by the draw and the pick so
   they can never drift apart. */
static void hud_map_center(float asp, float *cx, float *cy) {
    fe_ndc(asp, g_map_pin, g_map_fx, g_map_fy, cx, cy);
}
static float hud_map_r(void) { return g_map_half / 240.0f; }

static float hud_map_rot(const HudState *s) {
    if (s->map_static) return 0.0f;   /* Minimap::Tick 0x4ca840: turned only at Map 2 Rotate */
    return (float)M_PI * 0.5f - s->car_heading;
}

int hud_map_pick(float ndc_x, float ndc_y,
                 int width, int height, const HudState *s,
                 float *wx, float *wy) {
    if (!s || !wx || !wy || width <= 0 || height <= 0) return 0;
    float asp = (float)width / (float)height, cx, cy, mr = hud_map_r();
    hud_map_center(asp, &cx, &cy);
    /* NDC -> normalised panel coordinates (the exact inverse of the draw) */
    float mx = (ndc_x - cx) * asp / mr, my = (ndc_y - cy) / mr;
    if (mx * mx + my * my > 1.0f) return 0;          /* outside the round panel */
    float rot = hud_map_rot(s), c = cosf(-rot), sn = sinf(-rot);
    float dx = mx * c - my * sn, dy = mx * sn + my * c;
    *wx = s->car_x + dx * MAP_VIEW;
    *wy = s->car_y + dy * MAP_VIEW;
    return 1;
}

/* 0x4ca840's indicators over the TRACKMAP: each one's offset from the car
 * in window units (0.5 = the panel's half width), turned with the map,
 * hidden from 1.0 out, held at the rim from 0.55 (0x78e08c) -- the start
 * line's (flag 1) hidden there instead -- and turned by its heading less
 * the car's. The player's at the centre; the GPS target in free roam. */
static void hud_map_icon_ex(Hud *h, const HudDC *dc, int o, GLuint tex, const float *rgba,
                            float wx, float wy, float wh, int flag1,
                            const HudState *s, float cx, float cy, float mr, float view, float rot) {
    if (o < 0) return;
    float mx, my;
    hud_map_project(wx, wy, s->car_x, s->car_y, view, rot, &mx, &my);
    float d = 0.5f * sqrtf(mx * mx + my * my);                      /* window units */
    if (!(d < (s->map_far > 0.0f ? s->map_far : 1.0f))) return;   /* 0x78e090 / 10 */
    if (0.55f < d) {
        if (flag1) return;
        mx *= 0.55f / d; my *= 0.55f / d;
    }
    const FengObj *ob = &h->pk->obj[o];
    if (!tex) {
        if (ob->res < 0 || ob->res >= h->pk->nres || !h->rt[ob->res].tex) return;
        tex = h->rt[ob->res].tex;
    }
    if (!rgba) rgba = ob->rgba;
    hud_blit(dc, tex, fabsf(ob->size[0]) / 240.0f, fabsf(ob->size[1]) / 240.0f,
             cx + mx * mr / dc->asp, cy + my * mr, wh - ((float)M_PI * 0.5f - rot),   /* less local_64 (0 at Static) */
             rgba[0], rgba[1], rgba[2], rgba[3]);
}
static void hud_map_icon(Hud *h, const HudDC *dc, int o, float wx, float wy, float wh, int flag1,
                         const HudState *s, float cx, float cy, float mr, float view, float rot) {
    hud_map_icon_ex(h, dc, o, 0, NULL, wx, wy, wh, flag1, s, cx, cy, mr, view, rot);
}
/* the career zones (types 5..0x12, flags 0: held at the rim), each from its
   type's pool in zone order; the blink script runs on the object
   (SyncBlink 0x4ac620) and its colour is the animated one */
static void hud_draw_map_zones(Hud *h, const HudDC *dc, const HudState *s,
                               float cx, float cy, float mr, float view, float rot) {
    static const char *const blink[3] = { "Static", "Normal", "Dramatic" };
    int used[14] = { 0 };
    for (int i = 0; i < s->map_zone_n; i++) {
        const HudMapZone *z = &s->map_zones[i];
        int t = z->type - 5;
        if (t < 0 || t >= 14 || used[t] >= 9) continue;
        int o = h->o_zind[t][used[t]];
        if (o < 0) continue;                     /* the pool is spent: no entity */
        used[t]++;
        float col[4];
        const float *rgba = NULL;
        if (h->dw) {
            feng_anim_set(&h->dw->anim, o, feng_hash_upper(blink[z->blink >= 0 && z->blink < 3 ? z->blink : 0]), 0);
            FengPlace pl;
            feng_anim_place(&h->dw->anim, o, &pl);
            memcpy(col, pl.rgba, sizeof col);
            rgba = col;
        }
        hud_map_icon_ex(h, dc, o, z->cross && h->cross_tex ? h->cross_tex : 0, rgba,
                        z->x, z->y, 0.0f, 0, s, cx, cy, mr, view, rot);
    }
}
static void hud_draw_map_icons(Hud *h, const HudDC *dc, const HudState *s,
                               float cx, float cy, float mr, float view, float rot) {
    if (s->map_zones) hud_draw_map_zones(h, dc, s, cx, cy, mr, view, rot);
    if (s->map_start)                                               /* 0x4ec950: type 1, flag 1 */
        hud_map_icon(h, dc, h->o_startind, s->map_start_x, s->map_start_y,
                     s->map_start_deg * (float)M_PI / 180.0f, 1, s, cx, cy, mr, view, rot);
    int n = 0;
    if (s->map_cars) for (int i = 0; i < s->map_car_n && n < 8; i++) {   /* type 3: the AI racers */
        const HudMapCar *m = &s->map_cars[i];
        if (!m->rival) continue;
        hud_map_icon(h, dc, h->o_aiind[n++], m->x, m->y, m->heading, 0, s, cx, cy, mr, view, rot);
    }
    if (s->gps_on && h->dw) {                                      /* the GPS target group (UpdateGPS) */
        HudState t = *s;
        t.map_far = s->gps_far > 0.0f ? s->gps_far : 1.0f;            /* 0x4cae60: beyond the reach, off the map */
        for (int k = 0; k < 2; k++)
            hud_map_icon(h, dc, h->dw->o_gpst[k], s->gps_x, s->gps_y, s->car_heading, 0, &t, cx, cy, mr, view, rot);
    } else if (!s->racing && s->nav_active && h->o_gps >= 0) {
        float px, py, mx, my;
        hud_map_project(s->nav_target_x, s->nav_target_y, s->car_x, s->car_y, view, rot, &mx, &my);
        float l = sqrtf(mx * mx + my * my);
        if (l > 0.88f) { mx *= 0.88f / l; my *= 0.88f / l; }
        px = cx + mx * mr / dc->asp; py = cy + my * mr;
        fe_sprite_ndc(h, dc, h->o_gps, px, py, h->pk->obj[h->o_gps].rgba);
    }
    if (h->o_playerind >= 0) {                                      /* type 4: at the centre */
        const FengObj *ob = &h->pk->obj[h->o_playerind];
        if (ob->res >= 0 && ob->res < h->pk->nres && h->rt[ob->res].tex)
            hud_blit(dc, h->rt[ob->res].tex, fabsf(ob->size[0]) / 240.0f, fabsf(ob->size[1]) / 240.0f,
                     cx, cy, s->car_heading - ((float)M_PI * 0.5f - rot),   /* heading less local_64 (0x4ca840) */
                     ob->rgba[0], ob->rgba[1], ob->rgba[2], ob->rgba[3]);
    }
}

/* Everything that lives INSIDE the panel, drawn at the Track4000_map slot's
 * place in the draw order (the mask disc below it, north arrow above). */
static void hud_draw_map(Hud *h, const HudDC *dc, const HudState *s) {
    float cx, cy, mr = hud_map_r();
    hud_map_center(dc->asp, &cx, &cy);

    /* Heading-up, like the original: the world rotates so the car's forward
       axis points at the top of the panel. World heading 0 = +X, and "up" on
       the panel is +Y, hence the quarter-turn offset. */
    float rot = hud_map_rot(s);
    /* the event's TRACKMAP: a 0.3 / zoom window across the panel (0x4ca840) */
    const int tm = h->tm_tex != 0;
    const float view = tm ? 0.5f * (0.3f / h->tm_zoom) * h->tm_size
                          : s->map_radius_m > 1.0f ? s->map_radius_m : MAP_VIEW;
    const float k = mr / 0.30f;      /* marker sizes were tuned on a 0.30 panel */

    /* Everything inside the panel is projected through the same measured
       transform: world metres -> normalised panel -> NDC. */
    #define MAP_PT(wx, wy, outx, outy, inside) do {                         \
        float _mx, _my;                                                     \
        hud_map_project((wx), (wy), s->car_x, s->car_y, view, rot,          \
                        &_mx, &_my);                                        \
        (inside) = (_mx*_mx + _my*_my) < 0.92f * 0.92f;                     \
        (outx) = cx + _mx * mr / dc->asp;                                   \
        (outy) = cy + _my * mr;                                             \
    } while (0)

    /* street map: one textured fan clipped to the panel. Each rim vertex
       samples the world point under it (inverse of MAP_PT), so the map turns
       and scrolls exactly like the overlays drawn on top of it. */
    if (h->map_tex || tm) {
        const float RIM = 0.92f;
        float v[(MAP_FAN + 1) * 5];
        float rc = cosf(-rot), rs = sinf(-rot);
        for (int i = 0; i <= MAP_FAN; i++) {
            float mx = 0.0f, my = 0.0f;
            if (i) { float a = (float)(i - 1) * 2.0f * (float)M_PI / MAP_FAN;
                     mx = cosf(a) * RIM; my = sinf(a) * RIM; }
            float wx = s->car_x + (mx*rc - my*rs) * view;
            float wy = s->car_y + (mx*rs + my*rc) * view;
            v[i*5]   = cx + mx * mr / dc->asp;
            v[i*5+1] = cy + my * mr;
            v[i*5+2] = 0.0f;
            if (tm) {                                   /* top-left origin, y down */
                v[i*5+3] = (wx - h->tm_x0) / h->tm_size;
                v[i*5+4] = (h->tm_y0 - wy) / h->tm_size;
            } else {
                v[i*5+3] = (wx - h->map_x0) / h->map_ext;
                v[i*5+4] = (wy - h->map_y0) / h->map_ext;
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, h->fan_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_DYNAMIC_DRAW);
        static const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        glUniformMatrix4fv(dc->rp->uMVP, 1, GL_FALSE, ident);
        glBindTexture(GL_TEXTURE_2D, tm ? h->tm_tex : h->map_tex);
        glUniform1f(dc->rp->uUseTex, 1.0f);
        glUniform1f(dc->rp->uEmissiveTex, 1.0f);
        glUniform1f(dc->rp->uUnlit, 0.0f);
        glUniform3f(dc->rp->uColor, 1.0f, 1.0f, 1.0f);
        glUniform1f(dc->rp->uAlpha, tm ? 1.0f : 0.92f);   /* Track4000_map: rgba 255 */
        GpuMesh fan; memset(&fan, 0, sizeof fan);
        fan.vbo = h->fan_vbo; fan.nbo = h->fan_nbo; fan.ibo = h->fan_ibo;
        fan.nidx = MAP_FAN * 3;
        draw_gpumesh(&fan);
    }
    /* a TRACKMAP has the race's route drawn in it (Bayview). The World
       Expansion's city map is one picture for every race: its route is drawn
       over it (the road-following race line), then the icons. */
    const int tm_route = tm && h->tm_event == -2 && s->racing && s->route_line && s->route_line_n > 1;
    if (tm && !tm_route) { hud_draw_map_icons(h, dc, s, cx, cy, mr, view, rot); return; }
    /* free-roam road network: dots, cheap and cullable (no street map only) */
    if (!h->map_tex && !s->racing && s->nav_xy && s->nav_n > 0) {
        int drawn = 0;
        for (int i = 0; i < s->nav_n && drawn < 900; i++) {
            float px, py; int in;
            MAP_PT(s->nav_xy[i*2], s->nav_xy[i*2+1], px, py, in);
            if (!in) continue;
            hud_blit(dc, 0, 0.012f * k, 0.012f * k, px, py, 0.0f,
                     0.40f, 0.46f, 0.55f, 0.85f);
            drawn++;
        }
    }
    /* race route: an ordered polyline, so draw real segments. A circuit's
       last gate joins back to its first, which is why route_loop exists --
       otherwise the lap has a visible gap across the start line. */
    /* the road-following racing line when there is one, else the gates */
    const float *rxy = s->route_line && s->route_line_n > 1 ? s->route_line : s->route_xy;
    int rn = s->route_line && s->route_line_n > 1 ? s->route_line_n : s->route_n;
    if (rxy && rn > 1) {
        const float RIM = 0.92f;
        int segs = s->route_loop ? rn : rn - 1;
        for (int i = 0; i < segs; i++) {
            int j = (i + 1) % rn;
            /* a racing line can hold more than one run (branches): never
               join two points that are not neighbours on the road */
            if (rxy == s->route_line) {
                float gx = rxy[j*2] - rxy[i*2], gy = rxy[j*2+1] - rxy[i*2+1];
                if (gx*gx + gy*gy > 80.0f*80.0f) continue;
            }
            float p0x, p0y, p1x, p1y;
            hud_map_project(rxy[i*2], rxy[i*2+1],
                            s->car_x, s->car_y, view, rot, &p0x, &p0y);
            hud_map_project(rxy[j*2], rxy[j*2+1],
                            s->car_x, s->car_y, view, rot, &p1x, &p1y);
            /* CLIP, do not cull: a leg longer than the window has BOTH ends
               outside while still crossing it, and dropping those is what
               leaves the race minimap blank on a long circuit. */
            float ex = p1x - p0x, ey = p1y - p0y;
            float aq = ex*ex + ey*ey;
            if (aq < 1e-12f) continue;
            float bq = 2.0f * (p0x*ex + p0y*ey);
            float cq = p0x*p0x + p0y*p0y - RIM*RIM;
            float disc = bq*bq - 4.0f*aq*cq;
            if (disc < 0.0f) continue;                  /* misses the panel */
            float sq = sqrtf(disc);
            float t0 = (-bq - sq) / (2.0f*aq), t1 = (-bq + sq) / (2.0f*aq);
            if (t0 < 0.0f) t0 = 0.0f;
            if (t1 > 1.0f) t1 = 1.0f;
            if (t1 <= t0) continue;                     /* outside [0,1] */
            float c0x = p0x + ex*t0, c0y = p0y + ey*t0;
            float c1x = p0x + ex*t1, c1y = p0y + ey*t1;
            float ax = cx + c0x * mr / dc->asp, ay = cy + c0y * mr;
            float bx = cx + c1x * mr / dc->asp, by = cy + c1y * mr;
            float dx = (bx - ax) * dc->asp, dy = by - ay;
            float len = sqrtf(dx*dx + dy*dy);
            if (len < 1e-5f) continue;
            hud_blit(dc, 0, len, 0.020f * k, (ax+bx)*0.5f, (ay+by)*0.5f,
                     atan2f(dy, dx), tm_route ? 1.0f : 0.30f, tm_route ? 1.0f : 0.80f, 1.0f, 0.95f);   /* over a city map: white, as the TRACKMAPs draw it */
        }
    }
    if (tm) { hud_draw_map_icons(h, dc, s, cx, cy, mr, view, rot); return; }
    /* the armed gate: the one checkpoint that actually counts next */
    if (s->route_xy && s->route_next >= 0 && s->route_next < s->route_n && h->o_gate >= 0) {
        float px, py; int in;
        MAP_PT(s->route_xy[s->route_next*2], s->route_xy[s->route_next*2+1],
               px, py, in);
        if (in) fe_sprite_ndc(h, dc, h->o_gate, px, py, h->pk->obj[h->o_gate].rgba);
    }
    /* destination marker, only while guidance is live */
    if (s->nav_active && h->o_gps >= 0) {
        float px, py; int in;
        MAP_PT(s->nav_target_x, s->nav_target_y, px, py, in);
        if (!in) {   /* clamp to the rim so the player still gets a bearing */
            float dx = px - cx, dy = py - cy;
            float l = sqrtf(dx*dx*dc->asp*dc->asp + dy*dy);
            if (l > 1e-5f) { px = cx + dx / l * mr * 0.88f;
                             py = cy + dy / l * mr * 0.88f; }
        }
        fe_sprite_ndc(h, dc, h->o_gps, px, py, h->pk->obj[h->o_gps].rgba);
    }
    /* Vehicle dots remain visible over the street texture and race route.
       Cull using the whole dot radius so markers cannot bleed over the rim. */
    if (s->map_cars) for (int i = 0; i < s->map_car_n; i++) {
        const HudMapCar *m = &s->map_cars[i];
        float px, py; int in;
        MAP_PT(m->x, m->y, px, py, in);
        float dx = (px-cx)*dc->asp/mr, dy = (py-cy)/mr;
        if (!in || dx*dx+dy*dy > 0.85f*0.85f) continue;
        /* A small disc built from crossed rectangles, with a dark outline. */
        hud_blit(dc, 0, 0.027f * k, 0.027f * k, px, py, 0.0f, 0.04f, 0.04f, 0.04f, 1.0f);
        for (int q = 0; q < 2; q++)
            hud_blit(dc, 0, (q ? 0.014f : 0.021f) * k, (q ? 0.021f : 0.014f) * k,
                     px, py, 0.0f, 1.0f, m->rival ? 0.42f : 0.88f,
                     m->rival ? 0.12f : 0.24f, 1.0f);
    }
    #undef MAP_PT
    /* the player's icon (element type 4) turns like the others, by its
       heading less local_64 (0x4ca840): 0 at Map Rotate (the map turns
       under it), the car's heading at Map Static */
    if (h->o_player >= 0) {
        const FengObj *po = &h->pk->obj[h->o_player];
        if (po->res >= 0 && po->res < h->pk->nres && h->rt[po->res].tex)
            hud_blit(dc, h->rt[po->res].tex, fabsf(po->size[0]) / 240.0f, fabsf(po->size[1]) / 240.0f,
                     cx, cy, s->car_heading - ((float)M_PI * 0.5f - rot),
                     po->rgba[0], po->rgba[1], po->rgba[2], po->rgba[3]);
    }
}

/* ------------------------------------------------------- the FEng screen */

/* ------------------------------------------------------------- drift HUD */

/* the objects of the ported widgets, by name (FEHashUpper) */
static int dw_obj(Hud *h, const char *name) {
    return feng_find(h->pk, feng_hash_upper(name));
}

static void drift_mark(Hud *h, int turbo) {
    struct HudDriftW *w = h->dw;
    /* 0x4f6c00's list: SlowMessage, WrongWay_CountDown, Comment_DATA, the
       angle readout and the turbo / no-turbo backing; the popup's two
       strings are fetched (hidden) but not listed. 0x4f7ad0's list: the
       total, the multiplier box, and in a drift race the best drift. */
    static const char *const names[] = {
        "SlowMessage", "WrongWay_CountDown", "Comment_DATA", "Angle_Indicator_DATA",
        "Angle_degrees", "Drift_Angle_Backing1", "Drift_Angle_Backing3",
        "SP_Drift_Point", "SP_Drift_Title",
        "Style_Point_Backing_03", "SP_DRIFT_TOTAL", "Style_TotalDrift_Title",
        "CurrentSpeed_Trap_Backing_01", "NextSpeeed_Trap_Indicator", "drift_multiplier_text",
        "SpeedTrap_Speed_DATA", "BestDriftBacking", "BestLapTimeText", "BestDrift",
    };
    for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
        int o = dw_obj(h, names[k]);
        if (o >= 0) w->wl[o] = 1;
    }
    w->turbo = turbo;
}

static const char *dw_str(const struct HudDriftW *w, uint32_t hash) {
    for (int i = 0; i < w->nstr; i++) if (w->sh[i] == hash) return w->st[i];
    const char *p = options_port_str(hash);   /* the port's own option labels */
    return p ? p : "";
}

/* LANGUAGES/English.bin chunk 0x39000: the string table (0x4ffa80) */
static void str_load(const char *dataroot, unsigned char **plang, uint32_t **psh,
                     const char ***pst, int *pn) {
    char path[1024];
    long len = 0;
    n2_lang_path(path, sizeof path, dataroot);
    unsigned char *lang = *plang = hud_read_file(path, &len);
    if (!lang) return;
    for (long o = 0; o + 8 <= len; ) {
        uint32_t id = n2_u32(lang + o), sz = n2_u32(lang + o + 4);
        long p = o + 8;
        if (p + (long)sz > len) break;
        if (id == 0x00039000u && sz >= 16) {
            uint32_t n = n2_u32(lang + p + 4), toff = n2_u32(lang + p + 8);
            uint32_t soff = n2_u32(lang + p + 12);
            if ((long)toff + (long)n * 8 > (long)sz || n >= 20000) return;
            uint32_t *sh = *psh = (uint32_t *)malloc(n * sizeof *sh);
            const char **st = *pst = (const char **)malloc(n * sizeof *st);
            if (!sh || !st) return;
            for (uint32_t i = 0; i < n; i++) {
                sh[i] = n2_u32(lang + p + toff + i * 8);
                uint32_t so = n2_u32(lang + p + toff + i * 8 + 4);
                st[i] = (long)soff + (long)so < (long)sz ? (const char *)lang + p + soff + so : "";
            }
            *pn = (int)n;
            return;
        }
        o = p + sz;
    }
}
static void dw_load_strings(struct HudDriftW *w, const char *dataroot) {
    if (w->lang) return;   /* already loaded (drift/drag init + hud_build both ask): the copy leaked ~300 KB a race */
    str_load(dataroot, &w->lang, &w->sh, &w->st, &w->nstr);
}
/* the plain race HUD's own copy (its lap box label) */
static struct { unsigned char *lang; uint32_t *sh; const char **st; int nstr, tried; } g_hstr;
static const char *hud_str(const char *dataroot, uint32_t hash) {
    if (!g_hstr.tried && dataroot) { g_hstr.tried = 1; str_load(dataroot, &g_hstr.lang, &g_hstr.sh, &g_hstr.st, &g_hstr.nstr); }
    for (int i = 0; i < g_hstr.nstr; i++) if (g_hstr.sh[i] == hash) return g_hstr.st[i];
    return "";
}

static void dw_text(struct HudDriftW *w, int o, const char *s) {
    if (o < 0) return;
    if (!s) s = "";
    if (s == w->txt[o]) { w->has_txt[o] = 1; return; }
    size_t n = strlen(s);
    if (n >= 0xffff) n = 0xfffe;
    if (n + 1 > w->txtcap[o]) {
        size_t cap = (n + 16) & ~(size_t)15;
        char *t = (char *)realloc(w->txt[o], cap);
        if (!t) return;
        w->txt[o] = t; w->txtcap[o] = (unsigned short)cap;
    }
    memcpy(w->txt[o], s, n); w->txt[o][n] = 0;
    w->has_txt[o] = 1;
}
/* 0x50c900: a string by its label hash */
static void dw_label(struct HudDriftW *w, int o, uint32_t hash) { dw_text(w, o, dw_str(w, hash)); }

/* bPrintf "%$d" (0x43e950, flag '$' 0x2000): digits grouped by 3 with the
   locale's thousands character (0x7eb3f0: decimal, thousands, 3 -- English
   '.', ',', see n2_lang_thou) */
static void dw_num(char *out, int cap, int v) {
    char d[16]; int n = 0;
    unsigned u = v < 0 ? (unsigned)-(long)v : (unsigned)v;
    do { d[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 15);
    int k = 0;
    if (v < 0 && k < cap - 1) out[k++] = '-';
    for (int i = n - 1; i >= 0 && k < cap - 1; i--) {
        out[k++] = d[i];
        if (i && i % 3 == 0 && k < cap - 1) out[k++] = n2_lang_thou();
    }
    out[k] = 0;
}

void hud_num(char *out, int cap, int v) { dw_num(out, cap, v); }

static int dw_ftol(float f) { return (int)f; }   /* 0x75bed4 truncates */

static void dw_show(struct HudDriftW *w, int o, int show) { if (o >= 0) feng_anim_hide(&w->anim, o, !show); }

/* the widget constructors (0x4f6c00, 0x4f7ad0) */
static void drift_init(Hud *h, const char *dataroot, int turbo) {
    struct HudDriftW *w = h->dw;
    feng_anim_init(&w->anim, h->pk);
    dw_load_strings(w, dataroot);
    w->o_slow = dw_obj(h, "SlowMessage");
    w->o_wwc = dw_obj(h, "WrongWay_CountDown");
    w->o_comment = dw_obj(h, "Comment_DATA");
    w->o_angle = dw_obj(h, "Angle_Indicator_DATA");
    w->o_angle_deg = dw_obj(h, "Angle_degrees");
    w->o_back1 = dw_obj(h, "Drift_Angle_Backing1");
    w->o_back3 = dw_obj(h, "Drift_Angle_Backing3");
    w->o_point = dw_obj(h, "SP_Drift_Point");
    w->o_title = dw_obj(h, "SP_Drift_Title");
    w->o_back03 = dw_obj(h, "Style_Point_Backing_03");
    w->o_total = dw_obj(h, "SP_DRIFT_TOTAL");
    w->o_total_title = dw_obj(h, "Style_TotalDrift_Title");
    w->o_box01 = dw_obj(h, "CurrentSpeed_Trap_Backing_01");
    w->o_next_ind = dw_obj(h, "NextSpeeed_Trap_Indicator");
    w->o_mult = dw_obj(h, "drift_multiplier_text");
    w->o_next_pts = dw_obj(h, "SpeedTrap_Speed_DATA");
    w->o_best_back = dw_obj(h, "BestDriftBacking");
    w->o_best_text = dw_obj(h, "BestLapTimeText");
    w->o_best = dw_obj(h, "BestDrift");
    /* 0x4f6c00 */
    dw_label(w, w->o_slow, 0xb8387a0bu);                  /* "Too Slow!" */
    dw_show(w, w->o_point, 0);
    dw_show(w, w->o_title, 0);
    uint32_t sc = feng_hash_upper(turbo ? "Turbo" : "NoTurbo");
    feng_anim_set(&w->anim, w->o_angle, sc, 1);
    feng_anim_set(&w->anim, w->o_angle_deg, sc, 1);
    /* only the backing the widget lists is shown with it */
    dw_show(w, turbo ? w->o_back3 : w->o_back1, 0);
    /* 0x4f7ad0 */
    w->c60 = 0xffcaf48au; w->c64 = 0xffcaf48au; w->c68 = 0xffcf3030u;
    w->f70 = 0.05f; w->t74 = 0; w->b78 = 0; w->b79 = 1; w->lvl7c = 0;
    { char b[16]; snprintf(b, sizeof b, "X%d", 0); dw_text(w, w->o_mult, b); }
    printf("drift HUD: %s, %d scripts, %d tracks; %d strings; %s backing\n", h->pk->name,
           h->pk->nscr, h->pk->ntrk, w->nstr, turbo ? "turbo" : "no-turbo");
}

/* 0x4ab780: the running drift's points (pt) and title (ti) */
static void dw_popup(struct HudDriftW *w, int ti, int pt, int clock) {
    int nx = w->node.label != 0xa46aaea8u;                /* not "DRIFT" */
    int pts = dw_ftol(nx && w->node.chain ? w->node.pts / (float)w->node.chain : w->node.pts);
    char b[48], n1[24], n2[24];
    dw_num(n1, sizeof n1, pts); dw_num(n2, sizeof n2, w->node.chain);
    if (!w->node.failed && !(w->node.pts < 0.0f)) {
        if (!nx) {                                         /* drift race, DRIFT */
            feng_anim_color(&w->anim, pt, w->c64); feng_anim_color(&w->anim, ti, w->c64);
            dw_text(w, pt, n1);
        } else {
            feng_anim_color(&w->anim, pt, w->c60); feng_anim_color(&w->anim, ti, w->c60);
            if (w->node.chain > 1) snprintf(b, sizeof b, "+%sx%s", n1, n2);
            else snprintf(b, sizeof b, "+%s", n1);
            dw_text(w, pt, b);
        }
    } else {
        feng_anim_color(&w->anim, pt, w->c68); feng_anim_color(&w->anim, ti, w->c68);
        if (w->node.chain > 1 && nx) snprintf(b, sizeof b, "%sx%s", n1, n2);
        else snprintf(b, sizeof b, "%s", n1);
        dw_text(w, pt, b);
    }
    dw_label(w, ti, w->node.label);
    const uint32_t ANIMATE = feng_hash_upper("animate"), FADEOUT = feng_hash_upper("fadeout");
    if (pt >= 0 && w->node.fade != 1 && !feng_anim_running(&w->anim, pt, FADEOUT)) {
        feng_anim_set(&w->anim, pt, ANIMATE, 1);
        dw_show(w, pt, w->b79);
    }
    if (w->node.fade != 1 && !feng_anim_running(&w->anim, ti, FADEOUT)) {
        feng_anim_set(&w->anim, ti, ANIMATE, 1);
        dw_show(w, ti, 1);
    }
    if (w->node.fade && (w->node.state == 3 || (!w->node.cont && w->node.state != 5))) {
        feng_anim_set(&w->anim, ti, FADEOUT, 1);
        if (w->b79)
            feng_anim_set(&w->anim, pt, feng_hash_upper(w->node.pts < 0.0f ? "fadeoutCrash" : "fadeout"), 1);
        w->node.state = 5;
    }
    if (w->node.time == 0 || w->node.time == 0x7fffffff) { w->node.fade = 1; w->node.time = clock; }
}

/* The style moment popups outside a drift race (CunningStuntSpread::Update
   0x4da900): the shown moments into the node list (0x4c88e0, run while the
   HUD feature 0x20000 is on), then UpdateStunts 0x4da710: the first
   instant node (else the first) drawn by 0x4ab780, removed 0.5 s after it
   was drawn (1 s when it is alone). The Tach_NOS_Gamebreaker GLOW tests of
   0x4da710 never hold on PC (no code starts those scripts). */
static void st_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_stp < 0 && w->o_stt < 0) return;
    const HudStyle *y = s->style;
    if (!y) { w->nsn = 0; dw_show(w, w->o_stp, 0); dw_show(w, w->o_stt, 0); return; }
    for (int i = 0; i < y->n; i++) {                       /* 0x4c88e0 */
        const float cur = y->m[i].cur;
        int j = 0;
        for (; j < w->nsn; j++) if (w->sn[j].index == y->m[i].index && cur != 0.0f) break;
        if (j < w->nsn) {
            w->sn[j].failed = y->m[i].state == 4;
            w->sn[j].state = y->m[i].state;
            w->sn[j].pts = cur;
            if (y->m[i].state != 3) w->sn[j].chain = dw_ftol(y->m[i].mult + 0.5f);
            w->sn[j].time = 0;
            continue;
        }
        if (cur == 0.0f || w->nsn >= 16) continue;
        int at = cur < 0.0f ? 0 : w->nsn;                  /* negative: at the head */
        memmove(&w->sn[at + 1], &w->sn[at], (size_t)(w->nsn - at) * sizeof w->sn[0]);
        w->nsn++;
        w->sn[at].cont = y->m[i].cont != 0;                /* 0x4c7e20 */
        w->sn[at].pts = cur;
        w->sn[at].chain = dw_ftol(y->m[i].mult + 0.5f);
        w->sn[at].index = y->m[i].index;
        w->sn[at].label = y->m[i].label;
        w->sn[at].time = 0; w->sn[at].fade = 0;
        w->sn[at].failed = y->m[i].state == 4;
        w->sn[at].state = y->m[i].state;                   /* (+0x24 is not set by the ctor) */
    }
    const uint32_t ANIMATE = feng_hash_upper("animate"), FADEOUT = feng_hash_upper("fadeout");
    if (w->nsn == 0) {                                     /* 0x4da710 */
        if (!feng_anim_running(&w->anim, w->o_stt, ANIMATE) && !feng_anim_running(&w->anim, w->o_stt, FADEOUT)) {
            dw_show(w, w->o_stp, 0); dw_show(w, w->o_stt, 0);
        }
        return;
    }
    int k = 0;
    while (k < w->nsn && w->sn[k].cont) k++;
    if (k == w->nsn) k = 0;
    if (!feng_anim_running(&w->anim, w->o_stt, FADEOUT)) {
        w->c60 = 0xffcaf48au; w->c64 = 0xffcaf48au; w->c68 = 0xffcf3030u;   /* 0x4f7ad0 */
        w->b79 = y->show_pts != 0;
        w->node.cont = w->sn[k].cont; w->node.pts = w->sn[k].pts; w->node.chain = w->sn[k].chain;
        w->node.time = w->sn[k].time; w->node.label = w->sn[k].label; w->node.fade = w->sn[k].fade;
        w->node.state = w->sn[k].state; w->node.failed = w->sn[k].failed;
        dw_popup(w, w->o_stt, w->o_stp, y->clock);
        w->sn[k].time = w->node.time; w->sn[k].fade = w->node.fade; w->sn[k].state = w->node.state;
    }
    float life = w->nsn > 1 ? 0.5f : 1.0f;                /* 0x78e074 / 0x78e078 */
    if (life < (float)(y->clock - w->sn[k].time) * 0.00025f) {
        memmove(&w->sn[k], &w->sn[k + 1], (size_t)(w->nsn - k - 1) * sizeof w->sn[0]);
        w->nsn--;
    }
}

/* one frame of the drift widgets, in the retail order: the car's style
   tick (title 0x4ac120, popup copy 0x4c88e0), then 0x4da590 and 0x4da900 */
static void drift_update(Hud *h, const HudDrift *d) {
    struct HudDriftW *w = h->dw;
    const int now = d->clock;
    char b[48];
    if (d->title) {                                        /* 0x4ac120 */
        w->comment_on = 1; w->comment_t = now;
        dw_label(w, w->o_comment, d->title);
        dw_show(w, w->o_comment, 1);
    }
    if (d->m0_shown) {                                     /* 0x4c88e0, moment 0 in a drift race */
        float keep = w->node.pts;
        w->node.cont = d->m0_cont;
        w->node.pts = d->m0_cur;
        w->node.chain = dw_ftol(d->m0_mult + 0.5f);
        w->node.label = d->m0_label;
        w->node.time = 0; w->node.fade = 0;
        w->node.failed = d->m0_state == 4;
        w->node.state = d->m0_state;
        w->node_on = 1;
        if (d->m0_state == 4) { w->node.pts = keep; dw_show(w, w->o_comment, 0); }
    }
    /* 0x4da590 */
    float a = d->slip_deg;                                 /* 0x4ab390 */
    if (d->gear == 0 || d->fwd_speed < 0.0f) a = 0.0f;
    a = fabsf(a);
    if (a > 90.0f) a = 90.0f;
    snprintf(b, sizeof b, "%d", dw_ftol(a));
    dw_text(w, w->o_angle, b);
    if (d->losing_mult) {                                  /* 0x4ab420 */
        dw_show(w, w->o_slow, 1);
        if (!feng_anim_running(&w->anim, w->o_slow, feng_hash_upper("LosingMult")))
            feng_anim_set(&w->anim, w->o_slow, feng_hash_upper("LosingMult"), 1);
    } else if (d->too_slow) {
        dw_show(w, w->o_slow, 1);
        if (!feng_anim_running(&w->anim, w->o_slow, feng_hash_upper("TooSlowToGain")))
            feng_anim_set(&w->anim, w->o_slow, feng_hash_upper("TooSlowToGain"), 1);
    } else dw_show(w, w->o_slow, 0);
    w->c64 = d->wheelf > 1.0f || d->prox > 1.0f ? 0xffff8020u : 0xffcaf48au;   /* 0x4c7ea0 */
    dw_show(w, w->o_wwc, 0);                               /* 0x4ab2b0: no countdown running */
    if (!w->comment_on || (float)(now - w->comment_t) * 0.00025f > 2.0f)   /* 0x4ab340 */
        dw_show(w, w->o_comment, 0);
    /* 0x4da900 -> 0x4c85d0 */
    dw_num(b, sizeof b, dw_ftol(d->m0_best));
    dw_text(w, w->o_best, b);
    dw_show(w, w->o_title, 0);
    dw_show(w, w->o_point, 0);
    if (w->node_on) {
        dw_show(w, w->o_title, 1);
        dw_show(w, w->o_point, 1);
        dw_popup(w, w->o_title, w->o_point, now);
        if (!w->node.failed) { w->node_on = 0; w->node.time = 0; }
        else if ((float)(now - w->node.time) * 0.00025f > 1.0f) {
            w->node_on = 0; w->node.time = 0;
            dw_show(w, w->o_title, 0);
            dw_show(w, w->o_point, 0);
        }
    }
    const uint32_t HL = feng_hash_upper("Highlight");
    if (d->m0_state == 3) {
        dw_num(b, sizeof b, dw_ftol(d->m0_banked));
        dw_text(w, w->o_total, b);
        feng_anim_set(&w->anim, w->o_total, HL, 1);
        feng_anim_set(&w->anim, w->o_total_title, HL, 1);
    }
    int next = d->next, cur = dw_ftol(d->m0_cur), lvl = d->level;
    char n1[24];
    if (w->lvl7c < lvl) feng_anim_set(&w->anim, w->o_mult, HL, 1);
    dw_num(n1, sizeof n1, lvl);
    snprintf(b, sizeof b, "X%s", n1);
    dw_text(w, w->o_mult, b);
    w->lvl7c = lvl;
    if (next < 1) {
        dw_text(w, w->o_next_pts, "----");
        dw_label(w, w->o_next_ind, 0xcc62c8beu);           /* "MAX" */
        feng_anim_color(&w->anim, w->o_next_pts, 0xffaa8000u);
        return;
    }
    dw_num(b, sizeof b, next);
    dw_text(w, w->o_next_pts, b);
    snprintf(b, sizeof b, dw_str(w, 0xe03ef7a4u), lvl + 1);   /* "Gain %dX with:" */
    dw_text(w, w->o_next_ind, b);
    if (next <= cur) { feng_anim_color(&w->anim, w->o_next_pts, 0xffaa8000u); return; }
    float per = 0.5f;
    if (cur > 0) { per = (float)next / (float)cur * w->f70; if (per > 0.5f) per = 0.5f; }
    if (per < (float)(now - w->t74) * 0.00025f) {
        w->b78 = !w->b78; w->t74 = now;
        feng_anim_color(&w->anim, w->o_next_pts, w->b78 ? 0xffcaf48au : 0xffaa8000u);
    }
}

/* ------------------------------------------------------------- drag HUD */

/* the objects the drag HUD's widgets list (their constructors look them up
   by name; the ones HUD_Drag lacks are skipped): 0x4f6500, 0x4f69e0,
   0x4f6a90, 0x4f5a70, 0x4f4ac0 (PositionBacking is a group) */
static const char *const DRAG_TACH[] = {
    "RPM_fill", "3rdPersonTachRPMNumbers", "RPM_Redline", "3rdPersonNeedle", "3rdPersonGear",
    "ShiftMessage", "3rdPersonSpeedUnits", "3rdPersonSpeed", "Drag_Position_Backing_03", "Shift_light" };
static const char *const DRAG_NOS[] = { "NOS_Line", "NOS_GameBreaker", "NOS_Fill" };
static const char *const DRAG_TURBO[] = { "3rdperson_Turbofill", "3rdperson_Turbolines",
    "3rdperson_TurboDial", "3rdperson_TurboRed_1", "3rdperson_TurboRed_2" };
static const char *const DRAG_POS[] = { "RacePositionNumber", "RacePositionNth", "NumberRacers",
    "RaceTime", "PositionBacking", "Drag_Position_Backing_01", "Drag_Position_Backing_02" };
#define DRAG_N(a) ((int)(sizeof a / sizeof a[0]))

static void drag_mark_list(Hud *h, const char *const *names, int n) {
    for (int k = 0; k < n; k++) {
        int o = dw_obj(h, names[k]);
        if (o >= 0) h->dw->wl[o] = 1;
        if (o >= 0 && h->pk->obj[o].type == FENG_GROUP)        /* PositionBacking's art */
            for (int i = 0; i < h->pk->nobj; i++) if (feng_inside(h->pk, i, o)) h->dw->wl[i] = 1;
    }
}
static void drag_mark(Hud *h) {
    drag_mark_list(h, DRAG_TACH, DRAG_N(DRAG_TACH));
    drag_mark_list(h, DRAG_NOS, DRAG_N(DRAG_NOS));
    drag_mark_list(h, DRAG_TURBO, DRAG_N(DRAG_TURBO));
    drag_mark_list(h, DRAG_POS, DRAG_N(DRAG_POS));
    int o = dw_obj(h, "HEAT_Bar");
    if (o >= 0) h->dw->wl[o] = 1;
}
/* 0x4c6690: a widget's listed objects shown / hidden with its feature */
static void drag_show_list(Hud *h, const char *const *names, int n, int show) {
    for (int k = 0; k < n; k++) dw_show(h->dw, dw_obj(h, names[k]), show);
}

static uint32_t drag_argb(const FengObj *o) {                /* 0x50ccc0 -> 0x5054d0 */
    uint32_t c = 0;
    for (int k = 0; k < 4; k++) {
        int v = (int)(o->rgba[k == 0 ? 3 : k - 1] * 255.0f + 0.5f);
        c = c << 8 | (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    return c;
}

/* 0x60fe10: the tach's rev scale class by the engine's redline / limiter */
static int drag_tach_class(float red, float max) {
    if (9000.0f <= max && 8500.0f <= red) return 9000.0f <= red ? 0 : 1;
    if (max < 8000.0f || red < 7500.0f) {
        if (max < 7000.0f || red < 6500.0f) return red < 6000.0f ? 7 : 6;
        return red < 7000.0f ? 5 : 4;
    }
    return red < 8000.0f ? 3 : 2;
}

static void drag_init(Hud *h, const char *dataroot, float redline, float maxrpm) {
    struct HudDriftW *w = h->dw;
    w->drag = 1;
    feng_anim_init(&w->anim, h->pk);
    dw_load_strings(w, dataroot);
    w->o_rpmfill = dw_obj(h, "RPM_fill");
    w->o_rpmnum = dw_obj(h, "3rdPersonTachRPMNumbers");
    w->o_redline = dw_obj(h, "RPM_Redline");
    w->o_needle = dw_obj(h, "3rdPersonNeedle");
    w->o_gear = dw_obj(h, "3rdPersonGear");
    w->o_shmsg = dw_obj(h, "ShiftMessage");
    w->o_units = dw_obj(h, "3rdPersonSpeedUnits");
    w->o_speed = dw_obj(h, "3rdPersonSpeed");
    w->o_pb03 = dw_obj(h, "Drag_Position_Backing_03");
    w->o_shlight = dw_obj(h, "Shift_light");
    w->o_heat = dw_obj(h, "HEAT_Bar");
    w->o_nosline = dw_obj(h, "NOS_Line");
    w->o_nosgb = dw_obj(h, "NOS_GameBreaker");
    w->o_nosfill = dw_obj(h, "NOS_Fill");
    for (int k = 0; k < 5; k++) w->o_tb[k] = dw_obj(h, DRAG_TURBO[k]);
    w->o_pnum = dw_obj(h, "RacePositionNumber");
    w->o_pnth = dw_obj(h, "RacePositionNth");
    w->o_pof = dw_obj(h, "NumberRacers");
    w->o_ptime = dw_obj(h, "RaceTime");
    w->o_pback = dw_obj(h, "PositionBacking");
    w->o_pb01 = dw_obj(h, "Drag_Position_Backing_01");
    w->o_pb02 = dw_obj(h, "Drag_Position_Backing_02");
    /* 0x4f6500 -> 0x5ed9b0 (one human in a drag race): DRAG_RPM_<n>_LINES
       on the rev scale (missing: it and the redline stay hidden, +0xa4) and
       DRAG_RPM_REDLINES_<r> on the redline */
    static const char *const LINES[8] = { "10000", "10000", "9000", "9000", "8000", "8000", "7000", "7000" };
    static const int RED[8] = { 9000, 8500, 8000, 7500, 7000, 6500, 6000, 5500 };
    int cls = drag_tach_class(redline, maxrpm);
    char nl[48], nr[48], path[1024];
    snprintf(nl, sizeof nl, "DRAG_RPM_%s_LINES", LINES[cls]);
    snprintf(nr, sizeof nr, "DRAG_RPM_REDLINES_%d", RED[cls]);
    uint32_t keys[2] = { feng_tex_key(nl), feng_tex_key(nr) };
    FengImage img[2]; memset(img, 0, sizeof img);
    snprintf(path, sizeof path, "%s/GLOBAL/InGameDrag.bun", dataroot);
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (d) { feng_tpk_load(d, len, keys, 2, img); free(d); }
    if (img[0].rgba) { w->ov_num.tex = hud_upload(&img[0]); w->ov_num.w = img[0].w; w->ov_num.h = img[0].h;
                       w->ov_num.additive = img[0].blend == 6; }
    else w->fa4 = 1;
    if (img[1].rgba) { w->ov_red.tex = hud_upload(&img[1]); w->ov_red.w = img[1].w; w->ov_red.h = img[1].h;
                       w->ov_red.additive = img[1].blend == 6; }
    feng_image_free(&img[0]); feng_image_free(&img[1]);
    w->g84 = 1; w->g80 = 1;
    w->f88 = w->f89 = w->f8a = w->f8b = w->f8c = w->f8d = w->f8e = 0;
    w->c98 = w->o_gear >= 0 ? drag_argb(&h->pk->obj[w->o_gear]) : 0xffffffffu;
    w->c9c = w->o_needle >= 0 ? drag_argb(&h->pk->obj[w->o_needle]) : 0xffffffffu;
    w->p4c = w->p50 = 0;
    /* every widget starts with its feature off (0x4c6690(0, 0)) */
    drag_show_list(h, DRAG_TACH, DRAG_N(DRAG_TACH), 0);
    drag_show_list(h, DRAG_NOS, DRAG_N(DRAG_NOS), 0);
    drag_show_list(h, DRAG_TURBO, DRAG_N(DRAG_TURBO), 0);
    drag_show_list(h, DRAG_POS, DRAG_N(DRAG_POS), 0);
    dw_show(w, w->o_heat, 0);
    dw_show(w, dw_obj(h, "WrongWay_CountDown"), 0);     /* 0x4ab2b0: offline, no countdown */
    w->drag_on = -1;                                    /* the features' state, set on the first frame */
    printf("drag HUD: %s, %d scripts; rev scale %s / %s (class %d, redline %.0f, limiter %.0f)%s\n",
           h->pk->name, h->pk->nscr, nl, nr, cls, (double)redline, (double)maxrpm,
           w->fa4 ? " -- scale art missing" : "");
}

/* 0x4a9d60: v over (lo, hi) mapped onto (a, b), clamped */
static float drag_map(float lo, float hi, float a, float b, float v) {
    if (lo < v && v < hi) return (b - a) * ((v - lo) / (hi - lo)) + a;
    if (!(v < lo) && hi <= v) return b;
    return a;
}
/* 0x4c66f0: a bottom-anchored vertical fill -- the art's v0 (data +0x48)
   from the value, the height (1 - v0) x the texture's, the centre moved so
   the bottom edge stays */
static void drag_fill(Hud *h, int o, float v, float hi, float a, float b) {
    struct HudDriftW *w = h->dw;
    if (o < 0) return;
    const FengObj *ob = &h->pk->obj[o];
    if (ob->res < 0 || ob->res >= h->pk->nres || h->rt[ob->res].h <= 0) return;
    float th = (float)h->rt[ob->res].h, k = 1.0f / th;
    float f = drag_map(0.0f, hi, (th - a) * k, b * k, v);
    int32_t *d = w->anim.data[o];
    float ox, oy, sx, oh, nh = (1.0f - f) * th;
    memcpy(&ox, &d[7], 4); memcpy(&oy, &d[8], 4); memcpy(&sx, &d[14], 4); memcpy(&oh, &d[15], 4);
    memcpy(&d[18], &f, 4);
    feng_anim_size(&w->anim, o, sx, nh);
    feng_anim_pos(&w->anim, o, ox, (oh - nh) * 0.5f + oy);
}

/* 0x4ffaa0: the place's ordinal suffix label */
static uint32_t drag_nth(int n) {
    static const uint32_t T[10] = { 0x7447c0e3u, 0xbc4ce12fu, 0x63eb6a03u, 0xbd498562u, 0x46573b3fu,
                                    0xbc4cae38u, 0xbd383957u, 0xe2ab0284u, 0x438da7c0u, 0xbcdd9448u };
    if (n < 0) n = 0;
    return n == 10 ? 0xbcdd9448u : T[n % 10];
}

static void drag_msg(struct HudDriftW *w, uint32_t label) {
    const uint32_t ZOOMIN = feng_hash_upper("ZoomIn");
    dw_label(w, w->o_shmsg, label);
    feng_anim_set(&w->anim, w->o_shmsg, ZOOMIN, 1);
    dw_show(w, w->o_shmsg, 1);
}
/* the message's zoom ran out: hidden, the latch released (player +0x58 is
   never 6 / 7 here) */
static void drag_msg_done(struct HudDriftW *w, unsigned char *latch) {
    if (feng_anim_running(&w->anim, w->o_shmsg, feng_hash_upper("ZoomIn"))) return;
    *latch = 0;
    dw_show(w, w->o_shmsg, 0);
}

/* 0x4c7090, one human: the drag tach */
static void drag_tach(Hud *h, const HudState *s, const HudDrag *g, int on) {
    struct HudDriftW *w = h->dw;
    w->g84 = g->gear;
    if (!g->shifting) w->f8c = 0;
    else if (!w->f8c) w->f8c = 1;                       /* +0x90 = drivetrain +0x6c (unread) */
    const float rpm = g->rpm;
    if (on) {
        char b[24];
        /* speed: car +0x430 x 2.237 mph (0x78426c) or x 3.6 km/h (0x7904fc);
           a Test Track run (0x89e7e6, set only by StartCarTuningRace) leaves
           both texts as they are -- DynoTuningStats shows the speed */
        if (!g->tuning) {
            dw_label(w, w->o_units, g->mph ? 0x8569ab44u : 0x8569a25fu);
            snprintf(b, sizeof b, "%3d", dw_ftol(g->speed * (g->mph ? 2.237f : 3.6f)));
            dw_text(w, w->o_speed, b);
        }
        /* 0x4ab210: rpm / the limiter over the car's sweep, wrapped to (-360, 360] */
        float f = g->maxrpm > 0.0f ? rpm / g->maxrpm : 0.0f;
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        float a0 = g->ang0, span = a0 < g->ang1 ? g->ang1 - a0 : 360.0f - a0 + g->ang1;
        float ang = a0 + span * f;
        if (360.0f < ang) ang -= 360.0f;
        if (ang < -360.0f) ang += 360.0f;
        feng_anim_rot(&w->anim, w->o_needle, ang);
        /* the redline blinks while over it after an over-revved shift */
        const float gr = g->grade;
        if (!(gr < 0.0f) || w->f8b || rpm <= g->redline) {
            if (w->f8d) { feng_anim_set(&w->anim, w->o_redline, feng_hash_upper("Init"), 1); w->f8d = 0; w->f8e = 0; }
        } else {
            if (!w->f8e) w->f8e = 1;                    /* +0x94 = the clock (unread) */
            if (!w->f8d) { feng_anim_set(&w->anim, w->o_redline, feng_hash_upper("Blink"), 1); w->f8d = 1; }
        }
        /* the gear "%c" (table 0x7f66a4): orange 0xffff8000 while the gearbox
           is mid-shift (car +0x448 == 0), else its authored colour */
        if (w->o_gear >= 0) {
            static const char GC[10] = { 'A', 'R', 'N', '1', '2', '3', '4', '5', '6', '7' };
            int gi = w->g84 + 1;
            snprintf(b, sizeof b, "%c", gi >= 0 && gi < 10 ? GC[gi] : 'X');
            dw_text(w, w->o_gear, b);
            feng_anim_color(&w->anim, w->o_gear, g->gear_out ? 0xffff8000u : w->c98);
        }
        /* the shift messages (ShiftMessage, ZoomIn) */
        if (gr <= 0.0f || 1.0f < gr) {
            if (w->f88) drag_msg_done(w, &w->f88);
        } else if (w->f88) drag_msg_done(w, &w->f88);
        else if (g->gear != w->g80) {
            drag_msg(w, 0.5f < gr ? 0x0598b065u : 0x27d2dd45u);   /* "Perfect Shift!" / "Good Shift!" */
            w->f88 = 1;
        }
        if (g->cut) {                                   /* a shift with the clutch out (+0x60 3) */
            if (w->f89) drag_msg_done(w, &w->f89);
            else { drag_msg(w, 0x75941dc1u); w->f89 = 1; }   /* "Missed Shift!" */
        } else if (w->f89) drag_msg_done(w, &w->f89);
        if (g->race_state != 4 || w->g84 <= w->g80 || w->g80 == 0 || w->f88 || w->f89) {
            if (w->f8a) drag_msg_done(w, &w->f8a);      /* 0x4c76ff */
            if (w->f8b) drag_msg_done(w, &w->f8b);
        } else if (!(gr != 0.0f || w->f8a)) { drag_msg(w, 0x2303d76cu); w->f8a = 1; }   /* "Short Shift!" */
        else if (gr < 0.0f && !w->f8b) { drag_msg(w, 0x27d75cd8u); w->f8b = 1; }        /* "Over Rev!" */
        /* the shift light and the needle's colour: gear +0x80's shift
           window (0x7fbf60 x drivetrain +0xe4, at least 500 rpm) below its
           shift point, the shift's grade so far (0x594f20, unrounded) */
        int pg = w->g80 < 0 ? 0 : w->g80 > 9 ? 9 : w->g80;
        float q = g->phys ? g->q[pg] : 0.0f, win = g->win[pg];
        float ww = 500.0f <= win ? win : 500.0f, start = g->up[pg] - ww;
        if ((q != 0.0f || !(rpm < start)) && !g->shifting) {
            if (q != -1.0f) {
                if (q <= 0.5f) {
                    if (g->gear != 0) {
                        float cur[4] = { (float)(w->c9c >> 24), (float)(w->c9c >> 16 & 0xff),
                                         (float)(w->c9c >> 8 & 0xff), (float)(w->c9c & 0xff) };
                        static const float TGT[4] = { 255.0f, 0.0f, 128.0f, 192.0f };
                        if (!feng_anim_running(&w->anim, w->o_shlight, feng_hash_upper("PulseBlue")))
                            feng_anim_set(&w->anim, w->o_shlight, feng_hash_upper("PulseBlue"), 1);
                        float t = (rpm - start) / (ww * 0.5f);              /* 0x4ab0b0 */
                        uint32_t c = 0;
                        for (int k = 0; k < 4; k++) {
                            float v = (TGT[k] - cur[k]) * t + cur[k];
                            v = v > 0.0f ? (v < 255.0f ? v : 255.0f) : 0.0f;
                            c = c << 8 | (uint32_t)(int)v;                    /* 0x4a9e30 */
                        }
                        feng_anim_color(&w->anim, w->o_needle, c);
                    }
                } else {
                    feng_anim_color(&w->anim, w->o_needle, 0xff00ff00u);
                    if (!feng_anim_running(&w->anim, w->o_shlight, feng_hash_upper("PulseGreen")))
                        feng_anim_set(&w->anim, w->o_shlight, feng_hash_upper("PulseGreen"), 1);
                }
            } else {
                feng_anim_color(&w->anim, w->o_needle, 0xff800000u);
                if (!feng_anim_running(&w->anim, w->o_shlight, feng_hash_upper("PulseRed")))
                    feng_anim_set(&w->anim, w->o_shlight, feng_hash_upper("PulseRed"), 1);
            }
            dw_show(w, w->o_shlight, 1);
        } else {
            dw_show(w, w->o_shlight, 0);
            feng_anim_color(&w->anim, w->o_needle, w->c9c);
        }
    }
    w->g80 = w->g84;
    if (w->fa4) { dw_show(w, w->o_rpmnum, 0); dw_show(w, w->o_redline, 0); }
    (void)s;
}

/* one frame of the drag HUD's widgets: the features on while the race runs
   (0x4c6690), then 0x4c7090, 0x4c7aa0, 0x4c7b20, 0x4c6b80, 0x4ebb10 */
static void drag_update(Hud *h, const HudState *s, const HudDrag *g) {
    struct HudDriftW *w = h->dw;
    int on = s->racing && !s->race_over;
    int nos_on = on && g->nos;
    int pos_on = on && !s->no_pos;                       /* RacePosition, feature 0x200 */
    int st = on | nos_on << 1 | pos_on << 2 | (s->dyno != NULL) << 3;
    if (st != w->drag_on) {
        drag_show_list(h, DRAG_TACH, DRAG_N(DRAG_TACH), on);
        if (s->dyno) {                                  /* ctor 8010c080: a tuning race (0x803fcaa4) hides the speed */
            static const char *const spd[] = { "3rdPersonSpeedUnits", "3rdPersonSpeed" };
            drag_show_list(h, spd, 2, 0);
        }
        drag_show_list(h, DRAG_TURBO, DRAG_N(DRAG_TURBO), on);
        drag_show_list(h, DRAG_POS, DRAG_N(DRAG_POS), pos_on);
        drag_show_list(h, DRAG_NOS, DRAG_N(DRAG_NOS), nos_on);
        dw_show(w, w->o_heat, on);
        w->drag_on = st;
    }
    drag_tach(h, s, g, on);
    if (on) {
        /* 0x4c7aa0: the heat 0..1 (0x5eccd0) */
        float ht = g->heat <= 0.0f ? 0.0f : g->heat > 1.0f ? 1.0f : g->heat;
        drag_fill(h, w->o_heat, ht, 1.0f, 105.0f, 5.0f);
        /* 0x4c6b80: the turbo dial, 96..158 degrees in a drag race */
        for (int k = 0; k < 5; k++) dw_show(w, w->o_tb[k], g->turbo);
        float b = g->boost, a = (96.0f - 158.0f) * (b + 1.0f) * 0.5f + 158.0f;   /* 0x4aa160 */
        if (360.0f < a) a -= 360.0f;
        if (a < 0.0f) a = 360.0f - a;
        feng_anim_rot(&w->anim, w->o_tb[2], a);
        /* 0x4ebb10: place "%d", its suffix, "/%d" the field; the race clock,
           held 3 s after a lap event */
        char t[24];
        snprintf(t, sizeof t, "%d", s->position);
        dw_text(w, w->o_pnum, t);
        dw_label(w, w->o_pnth, drag_nth(s->position));
        snprintf(t, sizeof t, "/%d", s->cars);
        dw_text(w, w->o_pof, t);
        if (s->lap_new) w->p4c = s->rt_now;
        if (3.0f <= (float)(s->rt_now - w->p4c) * 0.00025f) w->p50 = s->rt_now;
        hud_time_fmt(t, w->p50, 0);
        dw_text(w, w->o_ptime, t);
    }
    if (nos_on) {                                       /* 0x4c7b20, a drag race: 45 / -9 */
        float v = g->nos_v;
        if (v <= 0.0f) { dw_show(w, w->o_nosline, 0); dw_show(w, w->o_nosgb, 0); }
        else if (1.0f < v) {
            dw_show(w, w->o_nosline, 1); dw_show(w, w->o_nosgb, 1);
            drag_fill(h, w->o_nosline, 1.0f, 1.0f, 45.0f, -9.0f);
            drag_fill(h, w->o_nosgb, v - 1.0f, 1.0f, 45.0f, -9.0f);
        } else {
            dw_show(w, w->o_nosline, 1); dw_show(w, w->o_nosgb, 0);
            drag_fill(h, w->o_nosline, v, 1.0f, 45.0f, -9.0f);
        }
    }
}

/* a ported widget's object, from its live FEng state */
static void fe_anim_draw(Hud *h, const HudDC *dc, int i) {
    struct HudDriftW *w = h->dw;
    const FengObj *o = &h->pk->obj[i];
    FengPlace pl;
    feng_anim_place(&w->anim, i, &pl);
    if (pl.hidden || pl.rgba[3] <= 0.0f) return;
    if (o->type == FENG_STRING) {
        fe_text_at(h, dc, i, w->has_txt[i] ? w->txt[i] : o->text, pl.x, pl.y, pl.sx, pl.rgba);
        return;
    }
    int res = w->res_ov[i] ? w->res_ov[i] - 1 : o->res;
    if (res < 0 || res >= h->pk->nres) return;
    /* White16x16.bmp ships only in GLOBALB.BUN: a white texel times the
       colour is the flat colour, which the untextured quad draws exactly */
    static const HudTex white = { 0, 16, 16, 0 };
    const HudTex *t = &h->rt[res];
    /* 0x50c990: the drag tach's rev scale / redline art set by the engine */
    if (w->drag && i == w->o_rpmnum && w->ov_num.tex) t = &w->ov_num;
    if (w->drag && i == w->o_redline && w->ov_red.tex) t = &w->ov_red;
    if (w->gw_ok && i == w->o_ei[1] && w->ei_tex[w->ei_lit ? 0 : 1].tex) t = &w->ei_tex[w->ei_lit ? 0 : 1];
    if (w->gw_ok && i == w->o_ei[4] && w->ei_icon > 0 && w->ei_icon < 20) {
        int a = w->ei_icon == 6 ? 5 : w->ei_icon == 10 ? 9 : w->ei_icon;   /* the same key */
        if (w->ei_tex[2 + a].tex) t = &w->ei_tex[2 + a];
    }
    if (w->gw_ok && i == w->o_sms[0] && w->ei_tex[EI_ENV + w->sms_img].tex) t = &w->ei_tex[EI_ENV + w->sms_img];
    if (w->sk_o[i] && w->sk_tex[w->sk_o[i] - 1].tex) t = &w->sk_tex[w->sk_o[i] - 1];
    for (int k = 0; k < 2; k++) if (w->tov[k].tex && i == w->tov_obj[k] - 1) t = &w->tov[k];
    HudTex xk = { w->xt[i] == 1 ? h->cross_tex : w->xt[i] == 2 ? h->tick_tex : 0, 0, 0, 0 };
    if (xk.tex) t = &xk;
    if (!t->tex) {
        if (strncmp(h->pk->res[res].name, "White16x16", 10)) return;
        t = &white;
    }
    /* PC_Loading's backdrop: the widescreen art over the whole screen */
    if (w->screen && hud_is_wide_loading_res(h->pk->res[res].name) && hud_wide_loading_tex() &&
        fabsf(o->size[0]) >= 600.0f && fabsf(o->size[1]) >= 440.0f) {
        float v[20];
        put_vert(v,      -1.0f,  1.0f, 0.0f, 0.0f);
        put_vert(v + 5,   1.0f,  1.0f, 1.0f, 0.0f);
        put_vert(v + 10,  1.0f, -1.0f, 1.0f, 1.0f);
        put_vert(v + 15, -1.0f, -1.0f, 0.0f, 1.0f);
        hud_batch(h, dc, hud_wide_loading_tex(), v, 1, pl.rgba);
        return;
    }
    int pin = w->screen ? screen_pin(w) : hud_pin_of(h->pk->obj[o->root].ax);
    /* a whole screen's full-screen art (PC_Loading's backdrop) covers the
       width as the front end draws it (shop.c to_ndc cover), instead of a
       4:3 picture between black bars */
    if (w->screen && fabsf(o->size[0]) >= 600.0f && fabsf(o->size[1]) >= 440.0f && dc->asp > 4.0f / 3.0f) {
        float k = dc->asp * 0.75f;
        pl.x *= k; pl.y *= k; pl.sx *= k; pl.sy *= k;
        pin = 0;
    }
    if (t->additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    /* the image's uv rectangle (object data words 17..20) when a widget
       crops it (the drag HUD's fill bars, 0x4c66f0) */
    const int32_t *d = w->anim.data[i];
    float uv[4];
    for (int k = 0; k < 4; k++) memcpy(&uv[k], &d[17 + k], 4);
    if ((w->drag || w->uvo[i] || (w->gw_ok && i == w->o_gw[1])) && t->tex && (uv[0] != 0.0f || uv[1] != 0.0f || uv[2] != 1.0f || uv[3] != 1.0f)) {
        float hw = 0.5f * fabsf(pl.sx), hh = 0.5f * fabsf(pl.sy), c = cosf(pl.angle), s = sinf(pl.angle);
        static const float cx[4] = { -1, 1, 1, -1 }, cy[4] = { -1, -1, 1, 1 };
        float v[20];
        for (int k = 0; k < 4; k++) {
            float lx = cx[k] * hw, ly = cy[k] * hh, nx, ny;
            fe_ndc(dc->asp, pin, pl.x + lx * c - ly * s, pl.y + lx * s + ly * c, &nx, &ny);
            put_vert(v + k * 5, nx, ny, cx[k] < 0 ? uv[0] : uv[2], cy[k] < 0 ? uv[1] : uv[3]);
        }
        hud_batch(h, dc, t->tex, v, 1, pl.rgba);
    } else {
        float nx, ny;
        fe_ndc(dc->asp, pin, pl.x, pl.y, &nx, &ny);
        hud_blit(dc, t->tex, fabsf(pl.sx) / 240.0f, fabsf(pl.sy) / 240.0f, nx, ny, -pl.angle,
                 pl.rgba[0], pl.rgba[1], pl.rgba[2], pl.rgba[3]);
    }
    if (t->additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* 0x577240 -> 0x576d30 (English, no flags): a time in 1/4000 s as
   "M:SS.CC" (minutes under 10 with a leading space), "SS.CC" (" S.CC"),
   "0.CC", or "Hh:MM:SS" from an hour */
/* 0x577240 -> 0x576d30 (English): flags 1 negative (set for t < 0), 2 a
   '+' when positive, 4 zero templates (the same digits), 8 keep the last
   digit when a sign shifts the string right. A sign replaces the leading
   space of a one-digit first field, else the string moves right by one
   and (without 8) loses its last digit. 0 and 0x7fffffff read as 0. */
void hud_time_fmt(char *b, int t, int flags) {
    if (t == 0x7fffffff) t = 0;
    if (t < 0) { t = -t; flags |= 1; }
    int sec = t / 4000, h = sec / 3600, m = sec % 3600 / 60, ss = sec % 60;
    int cs = (t % 4000 * 1000 / 4000) / 10, first;
    if (h) {
        strcpy(b, (flags & 4) ? "00h:00:00" : "--h:--:--");
        b[1] = (char)('0' + h % 10); b[0] = h < 10 ? ' ' : (char)('0' + h / 10 % 10);
        b[5] = (char)('0' + m % 10); b[4] = (char)('0' + m / 10 % 10);
        b[8] = (char)('0' + ss % 10); b[7] = (char)('0' + ss / 10 % 10);
        first = h;
    } else if (m) {
        strcpy(b, (flags & 4) ? "00:00.00" : "--:--.--");
        b[1] = (char)('0' + m % 10); b[0] = m < 10 ? ' ' : (char)('0' + m / 10 % 10);
        b[4] = (char)('0' + ss % 10); b[3] = (char)('0' + ss / 10 % 10);
        b[7] = (char)('0' + cs % 10); b[6] = (char)('0' + cs / 10 % 10);
        first = m;
    } else if (ss) {
        strcpy(b, (flags & 4) ? "00.00" : "--.--");
        b[1] = (char)('0' + ss % 10); b[0] = ss < 10 ? ' ' : (char)('0' + ss / 10 % 10);
        b[4] = (char)('0' + cs % 10); b[3] = (char)('0' + cs / 10 % 10);
        first = ss;
    } else {
        strcpy(b, (flags & 4) ? "0.00" : "0.--");
        b[3] = (char)('0' + cs % 10); b[2] = (char)('0' + cs / 10 % 10);
        first = 10;                                      /* always shifts when signed */
    }
    if (!(flags & 3)) return;
    if (first >= 10) {
        char c[16]; strcpy(c, b);
        if (!(flags & 8)) c[strlen(c) - 1] = 0;
        strcpy(b + 1, c);
    }
    b[0] = (flags & 1) ? '-' : '+';
}
void hud_time_str(char *b, int t) { hud_time_fmt(b, t, 0); }
/* 0x4ab4b0: the label of the player's place -- the last of the field
   "You were last!", else the table 0x7f66b4 by place */
static uint32_t ro_place_label(const HudState *s) {
    static const uint32_t T[7] = { 0x37083607u, 0xe815bc6du, 0xc9a64efbu, 0x443fab5au,
                                   0xac122037u, 0x4342d430u, 0xeac720d3u };
    if (s->ro_racers == s->ro_place) return 0xeac720d3u;
    return s->ro_place >= 0 && s->ro_place < 7 ? T[s->ro_place] : 0x37083607u;
}
/* ---- free roam's Outrun widgets (HUD_SingleRace.fng) ----------------------
   GetAwayMeter (ctor 0x4f87e0, update 0x4dabd0): group 8ca76279 (+0x38) with
   Getaway_Distance_Bar acbaf9ef (+0x28), Distance_Number 92cdc512 (+0x2c),
   Distance_Text bcea10ce (+0x34) and the backing / lines; Out_Run_Message
   407919af (+0x30) at top level.
   EventIndicator (ctor 0x4f83d0, update 0x4da9a0): group 3345911d -- [0] the
   D-pad icon 50910822 (texture 0x6fff0767 lit / 0xea570722 unlit), [2] / [3]
   the highlights a206a0b4 / 0a729b1b, [5] the mode icon 3b1b624a (0x4abe50:
   event 9 -> 0x33b5791a); [1] [4] null (other platforms); [6] the SMS icon
   Engage_SMS_mail 4b9b8f5a and [7] its count string 668c9cd6 ("x3"), both
   outside the group, hidden every frame and shown only by the SMS events
   (0x13 / 0x14, career; SetResetState hides them, UpdateSMS shows them);
   the backings 99f665f4 / b996a84c are only in the widget's list (shown
   with it). So event 9 draws the group alone. */
static const uint32_t GW_OBJ[7] = { 0x8ca76279u, 0xacbaf9efu, 0x92cdc512u, 0xbcea10ceu,
                                    0x238f4b29u, 0xc81a81f5u, 0x407919afu };
static const uint32_t EI_OBJ[6] = { 0x3345911du, 0x50910822u, 0xa206a0b4u, 0x0a729b1bu,
                                    0x3b1b624au, 0x99f665f4u };
static const uint32_t EI_SMS[2] = { 0x4b9b8f5au, 0x668c9cd6u };   /* [6], [7] */
/* ShowcaseCountdown (GC ctor 80111d20): the group, then what it registers --
   Show_case_backing, CurrentLapTimeText_2 ("Time:"), CurrentLapTime_2 (the
   time, +0x20); the group's placeholder f7cfc1fc is not one of them */
static const uint32_t SC_OBJ[4] = { 0x30d59b88u, 0x9a4e84a9u, 0x6bf40d64u, 0xcd3a501fu };
/* ShowcaseRewardIndicator (GC ctor 80108b1c / PC 4f5060): its one
   registered object, the string FEHashUpper("Cash_Reward") (HUD_SingleRace,
   InGameRace.bun) */
#define CR_OBJ 0x49106ce2u
/* MinimapWrapper ctor 0x4f73e0: GPS_Search_Group, GPS_SEARCH_Text, GPS_SEARCH_ERROR,
   the GPS target group (two Minimap_GPS_Selector images) */
static const uint32_t GPS_OBJ[4] = { 0x06254d8au, 0x0fba78c2u, 0x06012887u, 0x697d2bcau };
static void gw_mark(Hud *h) {
    struct HudDriftW *w = h->dw;
    for (int k = 1; k < 7; k++) { int o = feng_find(h->pk, GW_OBJ[k]); if (o >= 0) w->wl[o] = 1; }
    int g = feng_find(h->pk, EI_OBJ[0]);                 /* the indicator's group, every piece */
    for (int i = 0; g >= 0 && i < h->pk->nobj; i++)
        if (i != g && feng_inside(h->pk, i, g)) w->wl[i] = 1;
    if (g >= 0)                                          /* [6] [7]: the indicator's, not G_SMS */
        for (int k = 0; k < 2; k++) { int o = feng_find(h->pk, EI_SMS[k]); if (o >= 0) w->wl[o] = 1; }
    for (int k = 1; k < 4; k++) { int o = feng_find(h->pk, SC_OBJ[k]); if (o >= 0) w->wl[o] = 1; }
    { int o = feng_find(h->pk, CR_OBJ); if (o >= 0) w->wl[o] = 1; }
    { int o = feng_find(h->pk, feng_hash_upper("Cash_Bank")); if (o >= 0) w->wl[o] = 1; }
    int sg = feng_find(h->pk, GPS_OBJ[0]);               /* GPS_Search_Group, every piece */
    for (int i = 0; sg >= 0 && i < h->pk->nobj; i++)
        if (i != sg && feng_inside(h->pk, i, sg)) w->wl[i] = 1;
}
/* EngageEvent::GetEventTypeTexHash 0x4abe50: each EventIndicatorType's
   icon (1..19; 0 none). 0x13 (SMS) draws UpdateSMS's art instead. */
static const uint32_t EI_TYPE_TEX[20] = {
    0, 0xe9638d3eu, 0xaaab31e9u, 0x00119144u, 0x2521e5ebu, 0x91703aefu, 0x91703aefu, 0x3f6495c5u, 0xbf5ac5a9u,
    0x33b5791au, 0x33b5791au, 0x8244f164u, 0xb8038ac0u, 0xb802f10eu, 0xc56d3032u, 0xdc99aafeu, 0x3f40e75fu,
    0xd1d9d8a4u, 0xb0c0dcb5u, 0xf95dd41eu };
/* SMSGetEnvelopeImageHashAndColour 0x4974a0 (GC 800dc308) by
   SMSMessageCategories: 1 ENGAGE_SMS_RACHEL_MESSAGE, 2 _OPPONENT_MESSAGE,
   3 _SPECIAL_EVENT_MESSAGE, 4 _OUTRUN_MESSAGE, 5 _SHOP_MESSAGE,
   6 _TIPS_MESSAGE, 7 _RACE_MESSAGE, else ENGAGE_SMS_MAIL (the TPK keeps 23
   characters of the names); its FEColor as stored, b g r a */
static const uint32_t SMS_ENV_TEX[8] = { 0x4b9b8f5au, 0x88c9990au, 0x8b2f36eeu, 0x470222fdu,
                                         0xa9dfe648u, 0x5d9b6555u, 0x756b5c1bu, 0x55476a96u };
static const unsigned char SMS_ENV_COL[8][4] = {
    { 0x0e, 0x9c, 0x0e, 0xff }, { 0x2d, 0xc8, 0xc8, 0xff }, { 0x2b, 0x80, 0xd5, 0xff },
    { 0x8a, 0xf4, 0xca, 0xff }, { 0x30, 0x30, 0xcf, 0xff }, { 0xff, 0x80, 0x80, 0xff },
    { 0xc8, 0x82, 0x32, 0xff }, { 0xb5, 0x4a, 0x80, 0xff } };
/* the indicator's swapped art (0x50c990 by texture key): the D-pad icon's
   two states and the SMS envelopes (InGameFreeRoam.bun; InGameRace /
   InGameDrift carry the same) and the types' icons (GLOBALB.BUN) */
static void gw_load(Hud *h, const char *dataroot) {
    struct HudDriftW *w = h->dw;
    static const char *const from[2] = { "GLOBAL/InGameFreeRoam.bun", "GLOBAL/GLOBALB.BUN" };
    enum { NK = EI_NTEX };
    uint32_t keys[NK] = { 0x6fff0767u, 0xea570722u };
    for (int t = 0; t < 20; t++)           /* 6 and 10 draw 5's and 9's art (ei_art) */
        keys[2 + t] = EI_TYPE_TEX[t] && t != 6 && t != 10 ? EI_TYPE_TEX[t] : 0xffffffffu;
    for (int c = 0; c < 8; c++) keys[EI_ENV + c] = SMS_ENV_TEX[c];
    FengImage img[NK]; memset(img, 0, sizeof img);
    for (int f = 0; f < 2; f++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dataroot, from[f]);
        if (feng_tpk_load_file(path, keys, NK, img) >= 0) continue;
        long len = 0;
        unsigned char *d = hud_read_bundle(path, &len);
        if (d) { feng_tpk_load(d, len, keys, NK, img); free(d); }
    }
    for (int k = 0; k < NK; k++) {
        if (keys[k] == 0xffffffffu) continue;
        if (img[k].rgba) {
            w->ei_tex[k].tex = hud_upload(&img[k]); w->ei_tex[k].w = img[k].w; w->ei_tex[k].h = img[k].h;
            w->ei_tex[k].additive = img[k].blend == 6;
        } else if (k != 2 + 0x13) printf("player HUD: engage art %08x not found\n", keys[k]);
        feng_image_free(&img[k]);
    }
    w->gw_ok = 1;
}
static void gw_reset(Hud *h) {
    struct HudDriftW *w = h->dw;
    for (int k = 0; k < 7; k++) w->o_gw[k] = feng_find(h->pk, GW_OBJ[k]);
    for (int k = 0; k < 6; k++) w->o_ei[k] = feng_find(h->pk, EI_OBJ[k]);
    dw_show(w, w->o_gw[6], 0);
    /* the getaway meter group (8ca76279, flags c0000000): bit 31 does not
       hide it, the FEngHud ctor 0x4f8bf0's hide-all does (GC 801139f8 ->
       FEngSetAllObjectsInPackageVisibility 801502d0) until 0x4dabd0 shows it */
    dw_show(w, w->o_gw[0], 0);
    w->gw44 = 0; w->gw24 = 0; w->gw_was = 0;
    for (int k = 0; k < 4; k++) w->o_sc[k] = feng_find(h->pk, SC_OBJ[k]);
    dw_show(w, w->o_sc[0], 0);
    w->o_cr = feng_find(h->pk, CR_OBJ);
    dw_show(w, w->o_cr, 0);
    w->o_cb = feng_find(h->pk, feng_hash_upper("Cash_Bank"));
    dw_show(w, w->o_cb, 0);
    for (int k = 0; k < 3; k++) w->o_gps[k] = feng_find(h->pk, GPS_OBJ[k]);
    w->o_gpst[0] = w->o_gpst[1] = -1;
    {   int g = feng_find(h->pk, GPS_OBJ[3]), n = 0;
        for (int i = 0; g >= 0 && i < h->pk->nobj && n < 2; i++)
            if (i != g && feng_inside(h->pk, i, g) && h->pk->obj[i].type == FENG_IMAGE) w->o_gpst[n++] = i; }
    dw_show(w, w->o_gps[0], 0);
    w->gps_edge = 0;
    w->sc_on = 0;
    memset(w->ei, 0, sizeof w->ei); w->ei_ms = 0; w->ei_eng9 = 0; w->ei_icon = 0; w->ei_lit = 0;
    for (int k = 0; k < 2; k++) w->o_sms[k] = feng_find(h->pk, EI_SMS[k]);
    w->sms_img = 0; w->sms_last = 0; w->sms_env = 0; w->sms_s0 = 0;
}
/* 0x4c67e0(bar, v, 1, 99, 1, 1): a left-anchored horizontal fill -- the
   art's u1 (data +0x4c) from the value, the width (1 - u) x the texture's,
   the centre moved so the left edge stays */
static void gw_fill(Hud *h, int o, float v) {
    struct HudDriftW *w = h->dw;
    if (o < 0) return;
    const FengObj *ob = &h->pk->obj[o];
    if (ob->res < 0 || ob->res >= h->pk->nres || h->rt[ob->res].w <= 0) return;
    float tw = (float)h->rt[ob->res].w, k = 1.0f / tw;
    float f = drag_map(0.0f, 1.0f, (tw - 99.0f) * k, 1.0f * k, v);
    int32_t *d = w->anim.data[o];
    float ox, oy, ow, sy, u = 1.0f - f, nw = (1.0f - f) * tw;
    memcpy(&ox, &d[7], 4); memcpy(&oy, &d[8], 4); memcpy(&ow, &d[14], 4); memcpy(&sy, &d[15], 4);
    memcpy(&d[19], &u, 4);
    feng_anim_size(&w->anim, o, nw, sy);
    feng_anim_pos(&w->anim, o, ox - (ow - nw) * 0.5f, oy);
}
static void gw_msg(struct HudDriftW *w, uint32_t label) {
    dw_label(w, w->o_gw[6], label);
    dw_show(w, w->o_gw[6], 1);
    feng_anim_set(&w->anim, w->o_gw[6], feng_hash_upper("ZoomIn"), 1);
}
/* 0x4dabd0 every frame: off -> the group's InActive (the frame count reset
   out of mode 7); on -> 0x4c9780: the third frame the Outrun's opening line
   (0x783202b0 the player leads, else 0x18b97380), the fourth the group's
   Active; then the distance from the leader (GetDistanceFromLeader, > 0
   behind): Ahead green / Behind red, a change of sign while racing (stage 4)
   its line, the bar |d| / the limit, the number in feet (x 3.28, units
   0x1578c3) or metres (0x6bb2d6af) by 0x83aa1a */
/* ShowcaseCountdown::Update (PC 0x4c9210): with its feature (bit 4) and
   time left, CurrentLapTime_2 = "%s" of Timer::PrintToString(4), then its
   "Animate" from 10 s up (0x784560), "Animate_RED" below, each started
   only when not already running */
static void sc_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_sc[0] < 0) return;
    int on = s->wc_on != 0;
    if (on != w->sc_on) { dw_show(w, w->o_sc[0], on); w->sc_on = on; }
    if (!on || !(0.0f < s->wc_left)) return;
    char b[16];
    hud_time_fmt(b, (int)(s->wc_left * 4000.0f), 4);
    dw_text(w, w->o_sc[3], b);
    uint32_t sc = feng_hash_upper(s->wc_left < 10.0f ? "Animate_RED" : "Animate");
    if (!feng_anim_running(&w->anim, w->o_sc[3], sc)) feng_anim_set(&w->anim, w->o_sc[3], sc, 1);
}
/* ShowcaseRewardIndicator's class statics (GC 8046db8c..9c; PC 0x83649c
   messageQueued, 0x8364a0 baseReward, 0x8364a4 starRatingReward, 0x8364a8
   mMagType, 0x8364ac mDelayFrames). They outlive any one HUD, as the
   original's do. */
static int g_cr_queued, g_cr_base, g_cr_star, g_cr_kind, g_cr_delay;
void hud_showcase_reward(int kind, int base, int star) {
    /* FEngHud::ShowShowcaseReward queues only when the HUD has the widget
       (FEngHud +0x78 PC / +0x70 GC); here every in-game HUD reads the same
       statics and only one with Cash_Reward shows them */
    g_cr_kind = kind; g_cr_delay = 4; g_cr_base = base; g_cr_star = star; g_cr_queued = 1;
}
/* ShowcaseRewardIndicator::Update (GC 80108bb0 / PC 4a9ff0): its feature
   is hi 0x80 (mask +0x10/+0x14 = 0x80_00000000), which
   Player::DetermineHudFeatures (GC 802158f8 / PC 600090) sets in the normal
   driving HUD -- races and free roam alike -- and leaves clear when it
   returns 0 (magazine / preview mode, ...), on the race over path and in
   the stage-0 intro's HasControl branch. Off: Cash_Reward invisible, the
   delay held. On: the delay counts down a frame at a time; at 0 with a
   message queued the string becomes "%s%s: %$d" -- the label (0xaba1e56f
   "Bank Reward for Magazine" for kinds 1/3, 0x2ab10d61 "Bank Reward for DVD"
   for 2/4; other kinds keep the old text), the separator (" " when
   GetCurrentLanguage() == 1: PC 0x7909a0 / GC 80324004; else "": PC 0x783fe5
   / GC 80322504), base + star with '$' digit
   grouping -- then made visible with "ZoomIn" and the queue cleared. Once
   shown it is not hidden by the widget itself while the feature stays on:
   the ZoomIn script's own end state decides (FEng data). */
static void cr_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    /* the DetermineHudFeatures cases the port has state for (guess: the
       other zero-returning ones -- race coordinator states, dyno, 0x82a3a4
       > 0 -- have no HudState counterpart and are not modelled) */
    int on = !s->magazine && !s->race_over && !s->sms_stage0;
    if (w->o_cr < 0) return;
    if (!on) { dw_show(w, w->o_cr, 0); return; }
    if (0 < g_cr_delay) g_cr_delay--;
    if (!g_cr_queued || g_cr_delay != 0) return;
    const char *sep = n2_lang() == 1 ? " " : "";         /* French: " " */
    uint32_t label = (g_cr_kind == 3 || g_cr_kind == 1) ? 0xaba1e56fu
                   : (g_cr_kind == 4 || g_cr_kind == 2) ? 0x2ab10d61u : 0;
    if (label) {
        char num[24], b[160];
        dw_num(num, sizeof num, g_cr_base + g_cr_star);
        snprintf(b, sizeof b, "%s%s: %s", dw_str(w, label), sep, num);
        dw_text(w, w->o_cr, b);
    }
    dw_show(w, w->o_cr, 1);
    feng_anim_set(&w->anim, w->o_cr, feng_hash_upper("ZoomIn"), 1);
    g_cr_queued = 0;
}
/* FEngHud::ShowPickedUpBank (GC 80114e48 / PC 4ac1b0): the indicator's +0x28
   = the cash, then PlayUISoundFX(0x11) -- the sound whether or not the HUD
   has the widget. One static, as the indicator outlives no HUD here. */
static int g_cb_cash;
void hud_bank_pickup(int cash) {
    g_cb_cash = cash;
    audio_fe_play(0x11);
}
/* PickedUpBankIndicator::Update (GC 801089c0 / PC 4a9ef0): feature off or
   the world challenge countdown up = Cash_Bank, Cash_data, Cash_header
   invisible (hud_shows_money gates the bank group's own pieces); on =
   visible, and a pending pickup prints "%s %$d" ("Bank Reward:" 0x5d10f6af,
   the cash) into Cash_Bank, starts its Cash_Animate script and clears it.
   Cash_Bank's authored alpha is 0: only the script shows it. */
static void cb_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_cb < 0) return;
    if (!hud_shows_money(s)) { dw_show(w, w->o_cb, 0); return; }
    dw_show(w, w->o_cb, 1);
    if (!g_cb_cash) return;
    char num[24], b[160];
    dw_num(num, sizeof num, g_cb_cash);
    snprintf(b, sizeof b, "%s %s", dw_str(w, 0x5d10f6afu), num);
    dw_text(w, w->o_cb, b);
    feng_anim_set(&w->anim, w->o_cb, feng_hash_upper("Cash_Animate"), 1);
    g_cb_cash = 0;
}
static void gw_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_gw[0] < 0) return;
    if (!s->outrun) {
        if (w->gw_was) feng_anim_set(&w->anim, w->o_gw[0], feng_hash_upper("InActive"), 0);
        w->gw_was = 0; w->gw44 = 0;
        return;
    }
    w->gw_was = 1;
    if (w->gw44 == 3) gw_msg(w, s->outrun_rank == 1 ? 0x783202b0u : 0x18b97380u);
    if (w->gw44 == 4) {
        feng_anim_set(&w->anim, w->o_gw[0], feng_hash_upper("Active"), 0);
        dw_show(w, w->o_gw[0], 1);
    }
    if (w->gw44 < 4) w->gw44++;
    float d = s->outrun_d;
    if (d <= 0.0f) {
        if (!feng_anim_running(&w->anim, w->o_gw[2], feng_hash_upper("Ahead")))
            feng_anim_set(&w->anim, w->o_gw[2], feng_hash_upper("Ahead"), 0);
        feng_anim_color(&w->anim, w->o_gw[1], 0xff30cf30u);
        if (0.0f < w->gw24 && w->gw44 >= 4) gw_msg(w, 0xbfd1936fu);
    } else {
        if (!feng_anim_running(&w->anim, w->o_gw[2], feng_hash_upper("Behind")))
            feng_anim_set(&w->anim, w->o_gw[2], feng_hash_upper("Behind"), 0);
        feng_anim_color(&w->anim, w->o_gw[1], 0xff950000u);
        if (w->gw24 <= 0.0f && w->gw44 >= 4) gw_msg(w, 0x9b2af3c7u);
    }
    w->gw24 = d;
    float lim = s->outrun_limit > 0.0f ? s->outrun_limit : 300.0f;
    gw_fill(h, w->o_gw[1], fabsf(d) / lim);
    char num[24];
    if (!s->kmh) { dw_label(w, w->o_gw[3], 0x001578c3u); dw_num(num, sizeof num, dw_ftol(-d * 3.28f)); }
    else { dw_label(w, w->o_gw[3], 0x6bb2d6afu); dw_num(num, sizeof num, dw_ftol(-d)); }
    dw_text(w, w->o_gw[2], num);
}
/* The EventIndicator (0x4da9a0 Update, GC 801119e8): 8 EngageEvents, the
   highest priority one shown. Events come from AddEvent / RemoveEvent
   (0x4c9170 / 0x4dab80): the random encounter's challenger (type 9,
   FEngHud::NotifyRandomEncounterCar from s->engage), the career's trigger
   zones (FEngHud::Enter/ExitTriggerZone) and the stage-0 intro
   (TriggerStage0Intro: 0x14) through hud_event_add / hud_event_remove. */
static struct { int add, type, param; } g_eiq[16];
static int g_neiq, g_eiq_reset;
void hud_event_add(int type, int param) {
    if (g_neiq < 16) { g_eiq[g_neiq].add = 1; g_eiq[g_neiq].type = type; g_eiq[g_neiq].param = param; g_neiq++; }
}
void hud_event_remove(int type) {
    if (g_neiq < 16) { g_eiq[g_neiq].add = 0; g_eiq[g_neiq].type = type; g_eiq[g_neiq].param = 0; g_neiq++; }
}
void hud_event_reset(void) { g_neiq = 0; g_eiq_reset = 1; }
/* EngageEvent::GetPriority 0x4abf20 (an empty slot 0) */
static int ee_prio(int type) {
    if ((type >= 1 && type <= 8) || (type >= 11 && type <= 18)) return 4;
    if (type == 9 || type == 10) return 3;
    return type == 0x13 ? 2 : type == 0x14 ? 1 : 0;
}
/* EngageEvent::SetScript: start the script unless it runs (0x51d040 / 0x51cf70(.., 1)) */
static void ee_script(struct HudDriftW *w, int o, const char *name) {
    if (o < 0) return;
    uint32_t hs = feng_hash_upper(name);
    if (!feng_anim_running(&w->anim, o, hs)) feng_anim_set(&w->anim, o, hs, 1);
}
/* the widget's objects as the PC holds them: [0] the D-pad icon, [3], [5] the type's icon */
#define EI0 (w->o_ei[1])
#define EI3 (w->o_ei[3])
#define EI5 (w->o_ei[4])
/* EngageEvent::UpdateGeneric 0x4c8b80 (showcases 0x11 alike: UpdateShowcase 0x4c8dd0 adds the
   preview mode, C9) */
static void ee_generic(struct HudDriftW *w, int e) {
    for (;;) switch (w->ei[e].state) {
    case 2:
        w->ei_lit = 1;
        w->ei_icon = w->ei[e].type;                          /* [5] = GetEventTypeTexHash */
        /* fall through */
    case 3:
        w->ei_lit = 1;
        dw_show(w, EI5, 1); dw_show(w, EI3, 1);
        ee_script(w, EI3, "pulse"); ee_script(w, EI5, "Blink"); ee_script(w, EI0, "Pulse");
        return;
    case 4: w->ei[e].state = 3; continue;                   /* shown on */
    case 6:
        dw_show(w, EI5, 0); dw_show(w, EI3, 0);
        w->ei_lit = 0;
        ee_script(w, EI0, "Init"); ee_script(w, EI3, "Init"); ee_script(w, EI5, "Init");
        return;
    default: return;
    }
}
/* EngageEvent::UpdateCareerStage0 0x4aba30: the D-pad icon alone */
static void ee_stage0(struct HudDriftW *w, int e) {
    switch (w->ei[e].state) {
    case 2: w->ei_lit = 1; ee_script(w, EI0, "Pulse"); break;
    case 3: w->ei_lit = 1; ee_script(w, EI0, "Init"); break;
    case 4: case 6: ee_script(w, EI0, "Init"); break;
    default: break;
    }
}
/* 0x5046b0 FindScript + 0x504a00(0) its track at offset 0 (0x7992f4[0]) +
   0x535570(0, c): the base key moved by c - the value at t 0, so the track
   starts at c (the keys keep their deltas); bit 7 cleared */
static void ee_script_col0(struct HudDriftW *w, int o, uint32_t hs, const unsigned char c[4]) {
    const FengPackage *p = w->anim.p;
    if (o < 0 || o >= p->nobj) return;
    const FengObj *ob = &p->obj[o];
    const FengScript *sc = NULL;
    for (int s = 0; s < ob->nscr && !sc; s++) if (p->scr[ob->scr0 + s].hash == hs) sc = &p->scr[ob->scr0 + s];
    if (!sc) return;
    for (int k = 0; k < sc->ntrk; k++) {
        const FengTrack *tr = &p->trk[sc->trk0 + k];
        if (tr->off != 0) continue;
        if (tr->key0 < 0 || tr->size / 4 < 4) return;
        const int kw = 1 + tr->size / 4;
        int32_t *base = w->anim.keyw + tr->key0 + 1, v[4];
        const int32_t *k0 = w->anim.keyw + tr->key0 + kw;     /* track_eval at t 0 */
        for (int i = 0; i < 4; i++) v[i] = base[i];
        if (tr->nkey > 0) {
            const int32_t *kl = k0 + (tr->nkey - 1) * kw;
            if ((tr->flags & 0x7f) == 1 && tr->interp && k0[0] > 0) {   /* wrap: from the last key */
                float span = (float)(tr->len - kl[0] + k0[0]);
                float f = span <= 1e-5f ? 0.0f : (float)(tr->len - kl[0]) / span;
                for (int i = 0; i < 4; i++) v[i] += kl[1 + i] + (int32_t)((float)(k0[1 + i] - kl[1 + i]) * f);
            } else for (int i = 0; i < 4; i++) v[i] += k0[1 + i];
        }
        for (int i = 0; i < 4; i++) base[i] += (int32_t)c[i] - v[i];
        w->anim.trk_done[sc->trk0 + k] = 0;
        return;
    }
}
/* EngageEvent::UpdateSMS 0x4abaf0 (GC 80111014): [0] the phone / Cingular
   icon, [2] the highlight, [5] off, [6] the envelope, [7] the count */
static void ee_sms(struct HudDriftW *w, int e) {
    int o6 = w->o_sms[0], o7 = w->o_sms[1], o2 = w->o_ei[2];
    switch (w->ei[e].state) {
    case 2: case 3: {
        w->ei_lit = 1;                                       /* [0] = 0x6fff0767 ENGAGE_PHONE_ICON */
        int c = w->sms_env >= 1 && w->sms_env <= 7 ? w->sms_env : 0;
        w->sms_img = c;                                      /* [6] = SMSGetEnvelopeImageHashAndColour */
        dw_show(w, o6, 1); dw_show(w, o7, 0); dw_show(w, EI5, 0); dw_show(w, o2, 1);
        uint32_t ha = feng_hash_upper("animation");
        if (o6 >= 0 && !feng_anim_running(&w->anim, o6, ha)) {   /* its colour into "animation" */
            ee_script_col0(w, o6, ha, SMS_ENV_COL[c]);
            ee_script(w, o6, "animation");
        }
        ee_script(w, o7, "Hide"); ee_script(w, o2, "pulse"); ee_script(w, EI0, "Pulse");
        break; }
    case 4:
        w->sms_img = 0;                                      /* [6] = 0x4b9b8f5a ENGAGE_SMS_MAIL */
        w->ei_lit = 0;                                       /* [0] = 0xea570722 ENGAGE_CINGULAR_ICON */
        dw_show(w, o6, 1); dw_show(w, o7, 1); dw_show(w, o2, 1);
        ee_script(w, o6, "static"); ee_script(w, o2, "static"); ee_script(w, o7, "static");
        ee_script(w, EI0, "Init");
        if (o7 >= 0) dw_text(w, o7, "");                     /* FEPrintf(0x783fe5 "", n): blank */
        break;
    case 6:
        w->ei_lit = 0;                                       /* [0] = 0xea570722 */
        dw_show(w, o2, 0);
        ee_script(w, o6, "Hide"); ee_script(w, o7, "Hide"); ee_script(w, EI0, "Init");
        break;
    default: break;
    }
}
/* EngageEvent::updateState 0x4c8ac0: shown -> on after 1 s (0x14: its
   param in s; FOCUS on ENGAGE_DPAD, 15), on -> 4 after 4 s */
static void ee_state(struct HudDriftW *w, int e) {
    float el = (float)(w->ei_ms - w->ei[e].t0) * 0.001f;
    int t = w->ei[e].type;
    if (w->ei[e].state == 2) {
        float lim = t == 0x14 ? (float)w->ei[e].param : 1.0f;
        if ((t != 0x13 || !w->sms_s0) && el > lim) {         /* 0x13 holds while Stage0 HasControl */
            if (t != 0x14) audio_fe_play(15);                /* playSound(3): event 0xb trigger 0xe */
            w->ei[e].state = 3;
        }
    } else if (w->ei[e].state == 3 && el > 4.0f) w->ei[e].state = 4;
}
/* EngageEvent::Update 0x4c9030 */
static void ee_update(struct HudDriftW *w, int e) {
    if (!w->ei[e].type) { w->ei[e].state = 0; return; }
    if (w->ei[e].state == 0 || w->ei[e].state == 1) return;
    ee_state(w, e);
    if (w->ei[e].type == 0x14) ee_stage0(w, e);
    else if (w->ei[e].type == 0x13) ee_sms(w, e);            /* UpdateSMS */
    else ee_generic(w, e);
}
/* Start 0x4c9...: shown now; FOCUS on ENGAGE_PHONE (14), 0x13's 16, 0x14 silent */
static void ee_start(struct HudDriftW *w, int e) {
    w->ei[e].state = 2; w->ei[e].t0 = w->ei_ms;
    if (w->ei[e].type == 0x13) audio_fe_play(16);
    else if (w->ei[e].type != 0x14) audio_fe_play(14);
}
static void ee_finish(struct HudDriftW *w, int e) {          /* Finish */
    w->ei[e].state = 6; ee_update(w, e);
    w->ei[e].type = 0; w->ei[e].state = 0;
}
static void ei_add(struct HudDriftW *w, int type, int param) { /* AddEvent 0x4c9170 */
    for (int e = 0; e < 8; e++)                              /* FindSameEvent */
        if (w->ei[e].type == type && w->ei[e].state) {
            if (w->ei[e].state == 4) { ee_start(w, e); w->ei[e].param = param; }   /* Restart */
            return;
        }
    for (int e = 0; e < 8; e++)
        if (!w->ei[e].state) {                               /* Fill */
            w->ei[e].state = 1; w->ei[e].type = (signed char)type; w->ei[e].t0 = 0; w->ei[e].param = param;
            return;
        }
}
static void ei_remove(struct HudDriftW *w, int type) {       /* RemoveEvent 0x4dab80 */
    for (int e = 0; e < 8; e++) if (w->ei[e].type == type) { ee_finish(w, e); return; }
}
static void ei_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_ei[0] < 0) return;
    dw_show(w, w->o_ei[0], s->roam);
    w->ei_lit = 0;                                           /* SetResetState: [5] [6] [7] [3] [2] */
    dw_show(w, EI5, 0); dw_show(w, w->o_sms[0], 0); dw_show(w, w->o_sms[1], 0);
    dw_show(w, EI3, 0); dw_show(w, w->o_ei[2], 0);
    if (g_eiq_reset) { g_eiq_reset = 0; memset(w->ei, 0, sizeof w->ei); w->ei_eng9 = 0; w->sms_last = 0; }
    w->sms_env = s->sms_env; w->sms_s0 = s->sms_stage0;
    if (!s->roam) {                                          /* the feature is off */
        memset(w->ei, 0, sizeof w->ei); w->ei_eng9 = 0; g_neiq = 0; w->sms_last = 0;
        return;
    }
    w->ei_ms += s->dt_ms;
    if (s->engage != w->ei_eng9) {                           /* NotifyRandomEncounterCar */
        if (s->engage) ei_add(w, 9, 0); else ei_remove(w, 9);
        w->ei_eng9 = s->engage;
    }
    for (int q = 0; q < g_neiq; q++)
        if (g_eiq[q].add) ei_add(w, g_eiq[q].type, g_eiq[q].param); else ei_remove(w, g_eiq[q].type);
    g_neiq = 0;
    for (int e = 0; e < 8; e++) ee_update(w, e);
    int ip = -1, hi = -1, hp = 0;
    for (int e = 0; e < 8 && ip < 0; e++) if (w->ei[e].state == 2 || w->ei[e].state == 3) ip = e;   /* FindAnyEventInProgress */
    for (int e = 0; e < 8; e++)                              /* FindHighPriorityEvent */
        if (w->ei[e].state && ee_prio(w->ei[e].type) > hp) { hp = ee_prio(w->ei[e].type); hi = e; }
    if (ip < 0) {
        if (hi < 0) return;                                  /* SetResetState again */
        if (w->ei[hi].state != 4) ee_start(w, hi);
        ee_update(w, hi);
    } else if (hi >= 0 && ee_prio(w->ei[ip].type) < hp) {
        if (w->ei[ip].type == 0x13) { w->ei[ip].state = 4; ee_update(w, ip); }   /* Overide */
        else ee_finish(w, ip);
        ee_start(w, hi);
    } else ee_update(w, ip);
}
/* SMSMessageManager::Update 0x4db8c0 (GC 80113978), after the indicator
   in FEngHud::Update: none unread -> RemoveEvent(0x13), last 0; more than
   last time -> AddEvent(0x13, unread); last = unread */
static void sms_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_ei[0] < 0 || !s->roam) return;
    int n = s->sms_unread;
    if (n == 0) { ei_remove(w, 0x13); w->sms_last = 0; return; }
    if (w->sms_last < n) { ei_add(w, 0x13, n); w->sms_last = n; }
}
#undef EI0
#undef EI3
#undef EI5

int hud_screen_tex_file(Hud *h, uint32_t obj, const char *path, const char *texname, int opaque) {
    if (!texname) return 0;
    uint32_t key = 0xffffffffu;                              /* bin hash of the name */
    for (const char *c = texname; *c; c++) key = key * 33u + (unsigned char)*c;
    return hud_screen_tex_key(h, obj, path, key, opaque);
}
int hud_screen_tex_key(Hud *h, uint32_t obj, const char *path, uint32_t key, int opaque) {
    if (!h || !h->dw || !h->pk) return 0;
    struct HudDriftW *w = h->dw;
    int o = feng_find(h->pk, obj);
    if (o < 0) return 0;
    int k = 0;
    while (k < 2 && w->tov_obj[k] && w->tov_obj[k] - 1 != o) k++;
    if (k == 2) return 0;
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d) { printf("screen: %s missing\n", path); return 0; }
    FengImage img; memset(&img, 0, sizeof img);
    int ok = feng_tpk_load(d, len, &key, 1, &img) && img.rgba;
    free(d);
    if (!ok) { printf("screen: %s holds no %08x\n", path, (unsigned)key); return 0; }
    if (opaque) for (int i = 0; i < img.w * img.h; i++) img.rgba[i * 4 + 3] = 255;
    if (w->tov[k].tex && !w->tov_borrow[k]) glDeleteTextures(1, &w->tov[k].tex);
    w->tov[k].tex = hud_upload(&img); w->tov[k].w = img.w; w->tov[k].h = img.h; w->tov[k].additive = 0;
    w->tov_obj[k] = o + 1; w->tov_borrow[k] = 0;
    feng_image_free(&img);
    return 1;
}
/* hud_screen_tex_key with the picture given as RGBA (port: the World Expansion's track maps) */
int hud_screen_tex_rgba(Hud *h, uint32_t obj, int iw, int ih, const unsigned char *rgba) {
    if (!h || !h->dw || !h->pk || !rgba || iw <= 0 || ih <= 0) return 0;
    struct HudDriftW *w = h->dw;
    int o = feng_find(h->pk, obj);
    if (o < 0) return 0;
    int k = 0;
    while (k < 2 && w->tov_obj[k] && w->tov_obj[k] - 1 != o) k++;
    if (k == 2) return 0;
    FengImage img; memset(&img, 0, sizeof img);
    img.w = iw; img.h = ih; img.rgba = (unsigned char *)rgba;
    if (w->tov[k].tex && !w->tov_borrow[k]) glDeleteTextures(1, &w->tov[k].tex);
    w->tov[k].tex = hud_upload(&img); w->tov[k].w = iw; w->tov[k].h = ih; w->tov[k].additive = 0;
    w->tov_obj[k] = o + 1; w->tov_borrow[k] = 0;
    return 1;
}
/* 0x50c990 with the IconScroller's icon i (hud_screen_strip): the
   sponsors screen's big decal (uiSponsorsMain::RefreshHeader) */
int hud_screen_strip_tex(Hud *h, uint32_t obj, int i) {
    if (!h || !h->dw || !h->pk || i < 0 || i >= 16 || !h->dw->strip_tex[i]) return 0;
    struct HudDriftW *w = h->dw;
    int o = feng_find(h->pk, obj);
    if (o < 0) return 0;
    int k = 0;
    while (k < 2 && w->tov_obj[k] && w->tov_obj[k] - 1 != o) k++;
    if (k == 2) return 0;
    if (w->tov[k].tex && !w->tov_borrow[k]) glDeleteTextures(1, &w->tov[k].tex);
    w->tov[k].tex = w->strip_tex[i]; w->tov[k].w = w->tov[k].h = 0; w->tov[k].additive = w->strip_add[i];
    w->tov_obj[k] = o + 1; w->tov_borrow[k] = 1;
    return 1;
}
/* MinimapWrapper::Update 0x4da640 / UpdateGPS 0x4c8410: the search box
   while GetGPSStatus is 0..2 (its error line for 1 / 2), hidden once a route
   is followed (status 3, or 4, or the controller on a route -- a re-route
   never shows it); UI sound 0x12 on each entry into status 3 */
static void gps_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_gps[0] < 0) return;
    int st = s->gps_status, search = 0;
    if (s->gps_on) {
        if (st == 1 || st == 2) { dw_show(w, w->o_gps[2], 1); dw_label(w, w->o_gps[2], st == 1 ? 0x155409e8u : 0x31139b65u); }
        else dw_show(w, w->o_gps[2], 0);
        search = st >= 0 && st <= 2;
        if (st == 4 || s->gps_following) search = 0;
        if (!w->gps_edge && st == 3) audio_fe_play(0x12);
        w->gps_edge = st == 3;
    } else w->gps_edge = 0;
    dw_show(w, w->o_gps[0], search);
    if (search && st != 1 && st != 2) dw_show(w, w->o_gps[2], 0);
}
/* ---- DynoTuningStats (GC ctor 80112aec / Setup 80112c30 / Update 80113208,
   PC 4c9ce0; the rows' Display 8030b9ec..8030c088) ---------------------------
   One HudElement on every race HUD (HUD_SingleRace / _Short_Track / _Drift /
   _Drag carry group d292009b), shown by feature 0x800, which only
   DetermineTuningHUDFeatures (GC 80216658) sets: a test run, until the car
   finishes (a drag run keeps it). Setup picks the rows by the race: drag
   (burnout / 0-60 / quarter mile, the speed), drift (best lap score / best
   drift / longest drift), the rest (best lap / current G's / max G's / Time).
   The group is authored c0000000 and nothing names it in the code; Toggle
   (GC 80107e98) sets only the registered objects. CONFIRMED (G3 11-01) that
   every unregistered child stays hidden, as dy_update does: the FEngHud ctor
   (GC 801139f8, PC 0x4f8c2a..0x4f8c50) hides every object of the package
   (FEngSetAllObjectsInPackageVisibility 0x52cfc0 (pkg, 0)) unless drag
   (0x89e7d8 / 0x890108, g_tweakIsDragRace) or car show (0x89e7e5 / 0x836474,
   g_tweakIsCarShow); in HUD_Drag the group's unregistered children are only
   32eee5fc / 32eee5fd (Setup 80112c30 hides them) and the empty group
   29f0a455. (Bit 31 of the group's flags gates RenderGroup 80147728 only
   while the FEngine is not executing, SetExecution 80145958 -- the cFEng
   ctor sets it executing for good, so bit 31 never hides: feng.c
   feng_anim_init, stream F 11-01.) */
#define DY_GROUP 0xd292009bu
#define DY_MSG   0xd1b15e68u
static void dy_mark(Hud *h) {
    struct HudDriftW *w = h->dw;
    int g = feng_find(h->pk, DY_GROUP);
    for (int i = 0; g >= 0 && i < h->pk->nobj; i++)
        if (i != g && feng_inside(h->pk, i, g)) w->wl[i] = 1;
    int m = feng_find(h->pk, DY_MSG);
    if (m >= 0) w->wl[m] = 1;
}
static void dy_init(Hud *h) {
    struct HudDriftW *w = h->dw;
    w->o_dy = feng_find(h->pk, DY_GROUP);
    w->o_dy_msg = feng_find(h->pk, DY_MSG);
    if (w->o_dy >= 0) feng_anim_hide(&w->anim, w->o_dy, 1);
    if (w->o_dy_msg >= 0) feng_anim_hide(&w->anim, w->o_dy_msg, 1);
    w->dy_on = 0; w->dy_kind = -1;
    w->dy_m0 = w->dy_m2 = w->dy_m3 = 0;
    w->dy_t34 = w->dy_t38 = 0;
}
/* bPrintf of the game's strings: %d %$d %s %f %N.Nf %$N.Nf, the numbers from
   n[] and the strings from t[] in order ('$' groups by 3, as dw_num) */
static void dy_printf(char *out, int cap, const char *fmt, const double *n, const char *const *t) {
    int o = 0, kn = 0, kt = 0;
    for (const char *p = fmt; *p && o < cap - 1; p++) {
        if (*p != '%') { out[o++] = *p; continue; }
        p++;
        if (*p == '%') { out[o++] = '%'; continue; }
        int grp = 0, prec = -1;
        if (*p == '$') { grp = 1; p++; }
        while (*p >= '0' && *p <= '9') p++;                 /* the width: no padding in these */
        if (*p == '.') { prec = 0; p++; while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0'); }
        if (!*p) break;
        char b[64];
        if (*p == 's') { snprintf(b, sizeof b, "%s", t && t[kt] ? t[kt] : ""); kt++; }
        else if (*p == 'f') {
            double v = n[kn++];
            snprintf(b, sizeof b, "%.*f", prec < 0 ? 6 : prec, v);
            if (grp) {   /* the integer part grouped, the language's decimal character */
                char g[64]; const char *dot = strchr(b, '.');
                int neg = b[0] == '-', ni = (dot ? (int)(dot - b) : (int)strlen(b)) - neg, q = 0;
                if (neg) g[q++] = '-';
                for (int i = 0; i < ni; i++) { g[q++] = b[neg + i]; if ((ni - 1 - i) % 3 == 0 && i < ni - 1) g[q++] = n2_lang_thou(); }
                if (dot) { g[q++] = n2_lang_dec(); snprintf(g + q, sizeof g - q, "%s", dot + 1); } else g[q] = 0;
                snprintf(b, sizeof b, "%s", g);
            }
        } else {
            int v = (int)n[kn++];
            if (grp) dw_num(b, sizeof b, v); else snprintf(b, sizeof b, "%d", v);
        }
        for (const char *q = b; *q && o < cap - 1; q++) out[o++] = *q;
    }
    out[o] = 0;
}
static void dy_set(Hud *h, uint32_t hash, const char *txt) { dw_text(h->dw, feng_find(h->pk, hash), txt); }
static void dy_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_dy < 0) return;
    const HudDyno *d = s->dyno;
    int on = d && (!s->race_over || d->kind == 0);       /* IsFinishedRacing: 0 unless drag */
    if (on && d->kind != w->dy_kind) {                    /* Setup's registrations by the race */
        static const uint32_t common[] = { 0xb2d2e745u, 0xf11affd1u, 0xf11affd2u, 0xf11affd3u, 0xf11affd4u,
            0x9c355b23u, 0x9c355b24u, 0x9c355b25u, 0x9c355b26u, 0x0b470b80u, 0x7121f270u,
            0xc8d50791u, 0xc8d50792u, 0xc8d50793u, 0xf3828848u, 0xf3828849u, 0xf382884au };
        static const uint32_t drag[] = { 0xb2751131u, 0x72f68cc9u, 0x29f91c7cu, 0x32eee5fbu };
        static const uint32_t rest[] = { 0x4635ca2cu, 0xe1fa0da7u };
        memset(w->dy_reg, 0, sizeof w->dy_reg);
        for (size_t k = 0; k < sizeof common / sizeof common[0]; k++) { int o = feng_find(h->pk, common[k]); if (o >= 0) w->dy_reg[o] = 1; }
        if (d->kind == 0) for (size_t k = 0; k < 4; k++) { int o = feng_find(h->pk, drag[k]); if (o >= 0) w->dy_reg[o] = 1; }
        if (d->kind == 2) for (size_t k = 0; k < 2; k++) { int o = feng_find(h->pk, rest[k]); if (o >= 0) w->dy_reg[o] = 1; }
        for (int i = 0; i < h->pk->nobj; i++)             /* a registered group: its pieces */
            if (w->dy_reg[i] && h->pk->obj[i].type == FENG_GROUP)
                for (int j = 0; j < h->pk->nobj; j++) if (feng_inside(h->pk, j, i)) w->dy_reg[j] = 1;
        w->dy_kind = d->kind; w->dy_on = 0;
    }
    if (on != w->dy_on) {
        feng_anim_hide(&w->anim, w->o_dy, !on);
        if (on) for (int i = 0; i < h->pk->nobj; i++)
            if (i != w->o_dy && feng_inside(h->pk, i, w->o_dy) && h->pk->obj[i].type != FENG_GROUP)
                feng_anim_hide(&w->anim, i, !w->dy_reg[i]);
        if (w->o_dy_msg >= 0) feng_anim_hide(&w->anim, w->o_dy_msg, !(on && d->kind == 0));
        w->dy_on = on;
    }
    if (!on) return;
    int kmh = s->kmh;
    char b[160], t[24];
    const char *ts[2];
    double n[2];
    if (d->kind == 0) {
        dy_set(h, 0x7121f270u, dw_str(w, 0x35102edfu));                            /* "Drag" */
        /* BurnoutDistanceDisplay: "%$2.1f" of stats +0x28 (at least 0); "m" or x 3.3 "ft." */
        float bd = d->burnout < 0.0f ? 0.0f : d->burnout;
        dy_set(h, 0xc8d50791u, dw_str(w, 0x823b1fd8u));
        n[0] = kmh ? bd : bd * 3.3f; dy_printf(b, sizeof b, "%$2.1f", n, NULL); dy_set(h, 0xf3828848u, b);
        dy_set(h, 0x32eee5fbu, kmh ? "m" : dw_str(w, 0xce8edab8u));
        /* ZeroSixtyTimeDisplay: "0 to %d Time:" 60 / 100, Timer(stats +8, 4) */
        n[0] = kmh ? 100 : 60; dy_printf(b, sizeof b, dw_str(w, 0xc2be294du), n, NULL); dy_set(h, 0xc8d50792u, b);
        hud_time_fmt(t, d->t60, 4); dy_set(h, 0xf3828849u, t);
        /* QuarterMileTimeDisplay: Timer(stats +0x14, 4) */
        dy_set(h, 0xc8d50793u, dw_str(w, kmh ? 0xe591119du : 0xc93f1717u));
        hud_time_fmt(t, d->qm_time, 4); dy_set(h, 0xf382884au, t);
        snprintf(b, sizeof b, "%3d", (int)(fabsf(d->speed) * (kmh ? 3.6f : 2.237f)));
        dy_set(h, 0x72f68cc9u, b);
        dy_set(h, 0x29f91c7cu, dw_str(w, kmh ? 0x8569a25fu : 0x8569ab44u));
        /* ShowDragTuningMessage (GC 801134b8): each message once, its text then ZoomIn */
        int m = w->o_dy_msg;
        if (m >= 0) {
            uint32_t zoom = feng_hash_upper("ZoomIn");
            if (!w->dy_m0 && d->qm_speed >= 0.0f) {           /* the quarter mile: CompareDistances >= 400 */
                w->dy_m0 = 1;
                hud_time_fmt(t, d->qm_time, 0);
                ts[0] = t; n[0] = d->qm_speed;                   /* stats +0x10 as stored (m/s) */
                dy_printf(b, sizeof b, dw_str(w, kmh ? 0xc76c65b4u : 0x761e08ceu), n, ts);
                dw_text(w, m, b);
                feng_anim_set(&w->anim, m, zoom, 1);
            }
            if (!w->dy_m2 && d->t60 != 0) {
                w->dy_m2 = 1;
                hud_time_fmt(t, d->t60, 4);
                ts[0] = t; n[0] = kmh ? 100 : 60;
                dy_printf(b, sizeof b, dw_str(w, 0xb6bb5e46u), n, ts);
                dw_text(w, m, b);
                feng_anim_set(&w->anim, m, zoom, 1);
            }
            if (!w->dy_m3 && d->burnout > 0.0f) {             /* 0x4018f0 (0x89e7d8 || *0x890108, the drag flags) CONFIRMED set: DynoTuningStats::Update exits at 0x4c9d5d unless the same test passes */
                w->dy_m3 = 1;
                n[0] = kmh ? d->burnout : d->burnout * 3.3f;
                ts[0] = kmh ? "m" : dw_str(w, 0xce8edab8u);
                dy_printf(b, sizeof b, dw_str(w, 0xc35e2357u), n, ts);
                dw_text(w, m, b);
                feng_anim_set(&w->anim, m, zoom, 1);
            }
        }
    } else if (d->kind == 1) {
        dy_set(h, 0x7121f270u, dw_str(w, 0xd7162cfau));                            /* "Drift" */
        dy_set(h, 0xc8d50791u, dw_str(w, 0x7a04c109u));                            /* LapHighScore "%$d" */
        n[0] = d->best_score; dy_printf(b, sizeof b, "%$d", n, NULL); dy_set(h, 0xf3828848u, b);
        dy_set(h, 0xc8d50792u, dw_str(w, 0x0db1a145u));                            /* HighestScoringDrift */
        n[0] = d->best_drift; dy_printf(b, sizeof b, "%$d", n, NULL); dy_set(h, 0xf3828849u, b);
        dy_set(h, 0xc8d50793u, dw_str(w, 0x58b8df93u));                            /* LongestDriftScore */
        hud_time_fmt(t, (int)(d->best_drift_s * 4000.0f), 0); dy_set(h, 0xf382884au, t);
    } else {
        uint32_t title = d->setting == 0 ? 0x93fdee74u : d->setting == 3 ? 0x30da61e1u : d->setting == 4 ? 0x75f903f4u : 0;
        dy_set(h, 0x7121f270u, title ? dw_str(w, title) : "");
        dy_set(h, 0xc8d50791u, dw_str(w, 0x7a04c109u));                            /* BestLapTime, Timer 4 */
        hud_time_fmt(t, d->best_lap, 4); dy_set(h, 0xf3828848u, t);
        dy_set(h, 0xc8d50792u, dw_str(w, 0x59bfb401u));                            /* CurrentLateralGForce */
        n[0] = d->g_cur; dy_printf(b, sizeof b, "%$2.2f", n, NULL); dy_set(h, 0xf3828849u, b);
        dy_set(h, 0xc8d50793u, dw_str(w, 0x2dab24e4u));                            /* MaxLateralGForce */
        n[0] = d->g_max; dy_printf(b, sizeof b, "%$2.2f", n, NULL); dy_set(h, 0xf382884au, b);
        /* CurrentTimeDisplay: a new lap stamps the clock; 3 s on, the time since
           the last start line crossing is taken; "--:--.--" before any crossing */
        dy_set(h, 0x4635ca2cu, dw_str(w, 0x32a2cd8eu));
        if (d->new_lap) w->dy_t34 = d->now;
        if (d->last_cross != 0 && d->last_cross != 0x7fffffff) {
            if ((float)(d->now - w->dy_t34) * 0.00025f >= 3.0f) w->dy_t38 = d->now - d->last_cross;
            hud_time_fmt(t, w->dy_t38, 4); dy_set(h, 0xe1fa0da7u, t);
        } else dy_set(h, 0xe1fa0da7u, dw_str(w, 0x61c9b7a2u));
    }
}

/* the race over widget 0x4c7f00, every frame: its objects shown while the
   feature is on (0x4c6690); RaceOverMessage's script 0x657a31f6 started
   once (with RaceOverBacking's, 0x36819d93 in a drag race), then the text
   by the player's race entry state */
static void ro_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_ro_msg < 0) return;
    int on = s->race_over != 0 && !s->dyno;               /* a test run: no 4 in DetermineTuningHUDFeatures */
    if (on != w->ro_on) {
        feng_anim_hide(&w->anim, w->o_ro_msg, !on);
        if (w->o_ro_back >= 0) feng_anim_hide(&w->anim, w->o_ro_back, !on);
        w->ro_on = on;
    }
    if (!on) return;
    if (!feng_anim_running(&w->anim, w->o_ro_msg, 0x657a31f6u) && !w->ro_started) {
        feng_anim_set(&w->anim, w->o_ro_msg, 0x657a31f6u, 1);
        if (w->o_ro_back >= 0) feng_anim_set(&w->anim, w->o_ro_back, s->ro_drag ? 0x36819d93u : 0x657a31f6u, 1);
        w->ro_started = 1;
    }
    char t[16], b[160], n[24];
    hud_time_str(t, s->ro_time);
    switch (s->ro_state) {
    case 1:
        if (s->ro_score_race) {
            dw_num(n, sizeof n, s->ro_score);
            if (s->ro_alone) snprintf(b, sizeof b, "%s %s", dw_str(w, 0x13640de9u), n);
            else snprintf(b, sizeof b, "%s\n%s %s", dw_str(w, ro_place_label(s)), dw_str(w, 0x13640de9u), n);
        } else if (s->ro_alone) snprintf(b, sizeof b, "%s %s", dw_str(w, 0xc2878ebcu), t);
        else snprintf(b, sizeof b, "%s\n%s %s", dw_str(w, ro_place_label(s)), dw_str(w, 0xc2878ebcu), t);
        dw_text(w, w->o_ro_msg, b);
        break;
    case 3: dw_label(w, w->o_ro_msg, 0x1b59940cu); break;   /* knocked out */
    case 0xb: dw_label(w, w->o_ro_msg, 0x668c7453u); break; /* time's up */
    case 8: dw_label(w, w->o_ro_msg, 0x7449d26du); break;   /* blown engine */
    case 9: dw_label(w, w->o_ro_msg, 0x4ba0d22fu); break;   /* totaled */
    case 10: break;
    /* the Outrun: 6 won "%s\n%s %s" ("You Won the Outrun!", "Your time:",
       the time; career adds "\n%s %$d" "Bank Reward:" and the stage's
       purse 0x500c60 -- no career yet), 7 lost the same with 0x5d1621b2 */
    case 6: case 7:
        snprintf(b, sizeof b, "%s\n%s %s", dw_str(w, s->ro_state == 6 ? 0xdc089c64u : 0x5d1621b2u),
                 dw_str(w, 0xc2878ebcu), t);
        dw_text(w, w->o_ro_msg, b);
        break;
    /* 0xc (online) does not apply */
    default: dw_label(w, w->o_ro_msg, 0xa074449fu); break;
    }
}

/* the wrong way widget 0x4aafc0 (feature 0x4000, on in a race): offline the
   countdown stays hidden; WrongWayImage fades in (FadeIn) once the player is
   going the wrong way (player +0x248) and out (FadeOut) once not */
static void ww_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_ww < 0) return;
    int on = s->racing && !s->race_over;                 /* 0x4c6690 */
    feng_anim_hide(&w->anim, w->o_ww, !on);
    if (w->o_wwc2 >= 0) feng_anim_hide(&w->anim, w->o_wwc2, 1);
    if (!on) return;
    if (s->wrong_way) {
        if (!w->ww_in) { w->ww_in = 1; feng_anim_set(&w->anim, w->o_ww, feng_hash_upper("FadeIn"), 1); }
    } else if (w->ww_in && !feng_anim_running(&w->anim, w->o_ww, feng_hash_upper("FadeOut"))) {
        feng_anim_set(&w->anim, w->o_ww, feng_hash_upper("FadeOut"), 1);
        w->ww_in = 0;
    }
}

/* the lap widget's time part (0x4aa1b0): at a new lap the lap's time
   holds for 3 s (then the running lap again) and Lap_Time_Title / Text
   show it 2 s ("BEST LAP TIME" when it is the best and not the first);
   no lap yet: "--:--.--" */
static void lt_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_lt < 0) return;
    int on = s->rt_on && s->racing && !s->race_over && !s->no_laps;   /* 0x4c6690: the widget's objects (LapCounter 0x400) */
    feng_anim_hide(&w->anim, w->o_lt, !on);
    if (!on) {
        if (w->o_ltt >= 0) feng_anim_hide(&w->anim, w->o_ltt, 1);
        if (w->o_ltx >= 0) feng_anim_hide(&w->anim, w->o_ltx, 1);
        return;
    }
    char b[24];
    if (s->lap_start != 0 && s->lap_start != 0x7fffffff) {
        if (s->lap_new) {
            w->lt54 = s->rt_now; w->lt58 = s->lap_last; w->lt5c = s->rt_now;
            w->lt64 = !(s->lap_best < w->lt58 || !w->lt65);
            w->lt65 = 1;
        }
        if (3.0f < (float)(s->rt_now - w->lt54) * 0.00025f) w->lt58 = s->rt_now - s->lap_start;
        hud_time_fmt(b, w->lt58, 4);
        dw_text(w, w->o_lt, b);
        int show = w->lt65 && (float)(s->rt_now - w->lt5c) * 0.00025f < 2.0f;   /* 0x7844c0; only after a lap */
        if (w->o_ltt >= 0) feng_anim_hide(&w->anim, w->o_ltt, !show);
        if (w->o_ltx >= 0) feng_anim_hide(&w->anim, w->o_ltx, !show);
        if (show) {
            if (w->o_ltx >= 0) dw_text(w, w->o_ltx, b);
            if (w->o_ltt >= 0) dw_label(w, w->o_ltt, w->lt64 ? 0xa8247197u : 0xfc1b498au);
        }
    } else {
        dw_label(w, w->o_lt, 0x61c9b7a2u);                                 /* "--:--.--" */
        /* no lap yet: the package's placeholders ("LAPY TIMER" / "TIME") stay hidden */
        if (w->o_ltt >= 0) feng_anim_hide(&w->anim, w->o_ltt, 1);
        if (w->o_ltx >= 0) feng_anim_hide(&w->anim, w->o_ltx, 1);
    }
}
/* the split time (0x4da090 -> 0x4c6cb0 / 0x4aa740): at a new lap or
   checkpoint the gap to the leader (to the second when leading), signed,
   zooming in green when ahead or level, red behind; gone 3 s after */
static void sp_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_sp < 0) return;
    int on = s->split_on && s->racing && !s->race_over;
    if (!on) { feng_anim_hide(&w->anim, w->o_sp, 1); w->sp28 = 0; return; }
    if (!s->split_new) {
#ifdef OPENUG2_SIM_PRE_H
        if (w->sp28 && getenv("OPENUG2_SIM_SPLITSHOT")) {   /* test aid: mid-zoom shots at 0.15/0.6/1.5 s */
            static int stage, seq; static const float at[3] = { 0.15f, 0.6f, 1.5f };
            float el = (float)(s->rt_now - w->sp2c) * 0.00025f;
            if (el < 0.1f) stage = 0;
            if (stage < 3 && el >= at[stage]) {
                char nm[32]; void sim_shot_named(const char *);
                snprintf(nm, sizeof nm, "splitz%d_%02d_%c", s->split_mode, seq, 'a' + stage);
                sim_shot_named(nm);
                if (++stage == 3) seq++;
            }
        }
#endif
        if (3.0f < (float)(s->rt_now - w->sp2c) * 0.00025f) { feng_anim_hide(&w->anim, w->o_sp, 1); w->sp28 = 0; }
        return;
    }
    if (s->split_mode == 0 && (s->lb_n < 2 || s->split_gap == 0)) return;   /* ShowRaceLeader only */
    char b[24];
    hud_time_fmt(b, s->split_gap, 2);
    dw_text(w, w->o_sp, b);
    if (!w->sp28) {
        w->sp2c = s->rt_now;
        feng_anim_set(&w->anim, w->o_sp, feng_hash_upper(s->split_gap <= 0 ? "ZoominGreen" : "ZoominRed"), 1);
        feng_anim_hide(&w->anim, w->o_sp, 0);
        w->sp28 = 1;
        printf("hud: split time %s (mode %d)\n", b, s->split_mode);
#ifdef OPENUG2_SIM_PRE_H
        if (getenv("OPENUG2_SIM_SPLITSHOT")) {   /* test aid: a shot of each split */
            static int n; char nm[32]; void sim_shot_named(const char *);
            snprintf(nm, sizeof nm, "split%d_%02d", s->split_mode, n++); sim_shot_named(nm);
        }
#endif
    }
}
/* the leaderboard (one human, 0x4da190): 4 rows -- the leader, the cars
   just ahead, the player, the cars behind (0x4aaed0); for 10 s the others'
   gaps to the player ("+%s", 0x577240 flags 10) and 2 s everyone's names
   (0x4aa890); "%d :" positions; the player's row SetPlayerColor, the
   others Init; rows past the field hidden */
static void lb_update(Hud *h, const HudState *s) {
    struct HudDriftW *w = h->dw;
    if (w->o_lbd[0] < 0) return;
    int on = s->lb_on && s->racing && !s->race_over && s->rt_on;
    for (int r = 0; r < 4; r++) {
        int o4[4] = { w->o_lbd[r], w->o_lbt[r], w->o_lbtb[r], w->o_lbb[r] };
        for (int q = 0; q < 4; q++) if (o4[q] >= 0) feng_anim_hide(&w->anim, o4[q], !on);
    }
    if (!on) return;
    int names = w->lb1c0;                                /* the view: drawn before a flip shows */
    if (!w->lb1c0) {
        if (10.0f < (float)(s->rt_now - w->lb1bc) * 0.00025f) { w->lb1bc = s->rt_now; w->lb1c0 = 1; }   /* 0x784560 */
    } else if (2.0f < (float)(s->rt_now - w->lb1bc) * 0.00025f) { w->lb1bc = s->rt_now; w->lb1c0 = 0; }
    if (s->lb_drift) {
        /* a drift race (0x4da190 with 0x89e7d9 / 0x890104): DrawDriftScores
           0x4aac50 for 10 s, DrawDriftPlayerNames 0x4aadc0 for 2 s -- rows
           1..min(list, 4) straight from the sorted score list, no window
           around the player; LBBacking SetPlayerColor for a real car, else
           Init (LBData's colour 0x4aa7a0 is online only); "%d :" the row,
           LBData "%d" ftol(score) or the name "%s"; the 4 rows always shown
           offline (0x5f00f0 == 0 -> count 4), then rows past the list hidden */
        int n = s->lb_n < 4 ? s->lb_n : 4;
        for (int q = 0; q < n; q++) {
            char b[32];
            if (w->o_lbb[q] >= 0)
                feng_anim_set(&w->anim, w->o_lbb[q], feng_hash_upper(s->lb[q].hl ? "SetPlayerColor" : "Init"), 1);
            if (names) dw_text(w, w->o_lbd[q], s->lb[q].name);
            else { snprintf(b, sizeof b, "%d", dw_ftol(s->lb[q].score)); dw_text(w, w->o_lbd[q], b); }
            snprintf(b, sizeof b, "%d :", q + 1);
            if (w->o_lbt[q] >= 0) dw_text(w, w->o_lbt[q], b);
        }
        for (int q = n; q < 4; q++) {
            if (w->o_lbd[q] >= 0) feng_anim_hide(&w->anim, w->o_lbd[q], 1);
            if (w->o_lbt[q] >= 0) feng_anim_hide(&w->anim, w->o_lbt[q], 1);
        }
        return;
    }
    int n = s->lb_n < 8 ? s->lb_n : 8, me = 0;
    for (int k = 0; k < n; k++) if (s->lb[k].me) me = k;
    int row[4] = { 0, 0, 0, 0 }, before[7], after[7], nb = 0, na = 0, slots = 4 - 1;
    int pos = me + 1, i8 = pos - 2, i5 = pos;
    for (int it = 0; it < n && slots > 0; it++) {
        if (0 < i8 && 1 < slots) { before[nb++] = i8--; slots--; }
        if (i5 < n && 0 < slots) { after[na++] = i5++; slots--; }
    }
    int r = 1;
    for (int k = nb - 1; k >= 0; k--) row[r++] = before[k];
    if (me != 0) row[r++] = me;
    for (int k = 0; k < na; k++) row[r++] = after[k];
    for (int q = 0; q < 4 && q < n; q++) {
        int k = row[q];
        char b[32];
        if (k == me) {
            if (w->o_lbb[q] >= 0) feng_anim_set(&w->anim, w->o_lbb[q], feng_hash_upper("SetPlayerColor"), 1);
            feng_anim_set(&w->anim, w->o_lbd[q], feng_hash_upper("SetPlayerColor"), 1);
            dw_text(w, w->o_lbd[q], s->lb[k].name);
        } else {
            if (w->o_lbb[q] >= 0) feng_anim_set(&w->anim, w->o_lbb[q], feng_hash_upper("Init"), 1);
            feng_anim_set(&w->anim, w->o_lbd[q], feng_hash_upper("Init"), 1);
            if (names) dw_text(w, w->o_lbd[q], s->lb[k].name);
            else { hud_time_fmt(b, s->lb[k].gap, 10); dw_text(w, w->o_lbd[q], b); }
        }
        snprintf(b, sizeof b, "%d :", k + 1);
        if (w->o_lbt[q] >= 0) dw_text(w, w->o_lbt[q], b);
    }
    for (int q = n; q < 4; q++) {                        /* past the field */
        if (w->o_lbd[q] >= 0) feng_anim_hide(&w->anim, w->o_lbd[q], 1);
        if (w->o_lbt[q] >= 0) feng_anim_hide(&w->anim, w->o_lbt[q], 1);
    }
}

static const char *ordinal_suffix(int n) {
    int t = n % 100;
    if (t >= 11 && t <= 13) return "th";
    switch (n % 10) { case 1: return "st"; case 2: return "nd"; case 3: return "rd"; }
    return "th";
}

/* Every drawn object of HUD_SingleRace, far to near, with the live values the
   game itself would write into the dynamic ones. */
static void strip_draw(Hud *h, const HudDC *dc) {
    struct HudDriftW *w = h->dw;
    float cx = 0.0f, cy = -150.0f, sw = 118.0f, sh = 59.0f;
    if (w->strip_master >= 0) {
        const FengObj *m = &h->pk->obj[w->strip_master];
        cx = m->ax; cy = m->ay; sw = fabsf(m->size[0]); sh = fabsf(m->size[1]);
    }
    float step = sw + 40.0f;
    for (int i = 0; i < w->strip_n; i++) {
        if (!w->strip_tex[i]) continue;
        float x = cx + ((float)i - w->strip_scroll) * step;
        float k = 0.8f;
        if (i == w->strip_sel) {   /* 0x538760: alpha ramp -> scale 0.97..1.0 */
            float tri = w->strip_pulse < 1.0f ? w->strip_pulse : 2.0f - w->strip_pulse;
            float a = tri * 255.0f * 1.1f + 127.5f; if (a > 255.0f) a = 255.0f;
            k = 0.97f + a * 0.000117647f;
        }
        if (fabsf(x) + 0.5f * sw * k > 282.0f) continue;   /* outside the strip box */
        float nx, ny;
        fe_ndc(dc->asp, 0, x, cy, &nx, &ny);
        if (w->strip_add[i]) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        hud_blit(dc, w->strip_tex[i], sw * k / 240.0f, sh * k / 240.0f, nx, ny, 0.0f, 1.0f, 1.0f, 1.0f,
                 w->strip_lock[i] ? 150.0f / 255.0f : 1.0f);
        if (w->strip_add[i]) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}
/* GitHub #15: the regular tach's dial by the player's engine. 0x5ed9b0's
   non-drag branch (GC Tachometer ctor 80108d1c): the 0x60fe10 class picks
   3RDPERSON_<10500|9500|8500|7500>LINES_CUSTOM_<style> for the rev scale and
   3RDPERSON_REDLINE_<9000..5500> for the red zone, and the needle sweeps
   limiter / scale * 235 degrees (+0xc4) over rpm / limiter clamped to 0..1
   (CalcAngleForRPM 801094b0 / 0x4aa0e0). The package's default art is the
   10500 dial; swapped on the GL thread once the Hud's own uploads are done. */
static void tach_dial(Hud *h, const HudState *s) {
    if (h->defer || !h->pk || !(s->tach_lim > 0.0f && s->tach_red > 0.0f)) return;
    static const float SCALE[8] = { 10500, 10500, 9500, 9500, 8500, 8500, 7500, 7500 };
    static const int RED[8] = { 9000, 8500, 8000, 7500, 7000, 6500, 6000, 5500 };
    int cls = drag_tach_class(s->tach_red, s->tach_lim);
    h->tach_lim = s->tach_lim;
    h->tach_sweep = s->tach_lim / SCALE[cls] * 235.0f;
    if (h->tach_cls == cls + 1) return;
    h->tach_cls = cls + 1;
    int on = feng_find(h->pk, feng_hash_upper("3rdPersonTachRPMNumbers"));
    int orl = feng_find(h->pk, feng_hash_upper("RPM_Redline"));
    int rn = on >= 0 ? h->pk->obj[on].res : -1, rr = orl >= 0 ? h->pk->obj[orl].res : -1;
    char nl[64], nlp[48], nr[48];
    snprintf(nl, sizeof nl, "3RDPERSON_%.0fLINES_CUSTOM_%02d", (double)SCALE[cls], g_hud_style);
    snprintf(nlp, sizeof nlp, "3RDPERSON_%.0fLINES", (double)SCALE[cls]);
    snprintf(nr, sizeof nr, "3RDPERSON_REDLINE_%d", RED[cls]);
    uint32_t keys[3] = { feng_tex_key(nl), feng_tex_key(nlp), feng_tex_key(nr) };
    FengImage img[3]; memset(img, 0, sizeof img);
    for (int f = 0; f < HUD_NPACK; f++) {
        char path[1024], pack[64];
        snprintf(pack, sizeof pack, HUD_PACKS[f], g_hud_style);
        snprintf(path, sizeof path, "%s/%s", h->dataroot, pack);
        if (feng_tpk_load_file(path, keys, 3, img) >= 0) continue;
        long len = 0;
        unsigned char *d = hud_read_bundle(path, &len);
        if (!d) continue;
        feng_tpk_load(d, len, keys, 3, img);
        free(d);
    }
    FengImage *ln = img[0].rgba ? &img[0] : img[1].rgba ? &img[1] : NULL;
    if (ln && rn >= 0 && rn < h->pk->nres) {
        if (h->rt[rn].tex) glDeleteTextures(1, &h->rt[rn].tex);
        h->rt[rn].tex = hud_upload(ln); h->rt[rn].w = ln->w; h->rt[rn].h = ln->h;
        h->rt[rn].additive = ln->blend == 6;
    }
    if (img[2].rgba && rr >= 0 && rr < h->pk->nres) {
        if (h->rt[rr].tex) glDeleteTextures(1, &h->rt[rr].tex);
        h->rt[rr].tex = hud_upload(&img[2]); h->rt[rr].w = img[2].w; h->rt[rr].h = img[2].h;
        h->rt[rr].additive = img[2].blend == 6;
    }
    printf("player HUD: tach dial %s / %s (class %d, redline %.0f, limiter %.0f, sweep %.1f deg)%s%s\n",
           ln == &img[0] ? nl : nlp, nr, cls, (double)s->tach_red, (double)s->tach_lim, (double)h->tach_sweep,
           ln ? "" : " -- scale art missing", img[2].rgba ? "" : " -- redline art missing");
    for (int i = 0; i < 3; i++) feng_image_free(&img[i]);
}
/* the needle's angle in degrees: the engine's sweep, else the default dial */
static float tach_angle(const Hud *h) {
    if (h->tach_cls && h->tach_lim > 0.0f) {
        float f = h->needle_rpm / h->tach_lim;
        if (f < 0.0f) f = 0.0f; else if (f > 1.0f) f = 1.0f;
        return f * h->tach_sweep;
    }
    return h->needle_rpm / 1000.0f * RPM_DEG_PER_1000;
}
static void hud_draw_screen(Hud *h, const HudDC *dc, const HudState *s) {
    const FengPackage *p = h->pk;
    unsigned char gate_on[G_COUNT];
    gate_on[G_ALWAYS] = 1;
    gate_on[G_TURBO]  = s->have_turbo ? 1 : 0;   /* unfitted = removed */
    gate_on[G_NOS]    = (unsigned char)hud_shows_nos(s);
    gate_on[G_RACE]   = s->racing ? 1 : 0;
    gate_on[G_POS]    = s->racing && s->cars > 1 && s->position > 0;
    gate_on[G_LAPS]   = s->racing ? 1 : 0;
    gate_on[G_BANK]   = (unsigned char)hud_shows_money(s);
    gate_on[G_SMS]    = !s->racing && s->message_waiting;

    tach_dial(h, s);
    float rpm = s->have_rpm ? s->rpm : 0.0f;
    if (rpm < 0.0f) rpm = 0.0f;
    if (rpm > RPM_SCALE_MAX) rpm = RPM_SCALE_MAX;
    h->needle_rpm += (rpm - h->needle_rpm) * 0.35f;
    float kmh = fabsf(s->speed_kmh);
    h->needle_speed += (kmh - h->needle_speed) * 0.40f;
    h->nos_glow += ((s->nitro_active ? 1.0f : 0.0f) - h->nos_glow) * 0.25f;
    float rot = hud_map_rot(s);
    char buf[32];

    int strip_done = !(h->dw && h->dw->strip_n);
    for (int k = 0; k < h->ndraw; k++) {
        int i = h->draw[k];
        const FengObj *o = &p->obj[i];
        if (!strip_done && (h->dw->strip_master < 0 || o->az < p->obj[h->dw->strip_master].az)) {
            strip_draw(h, dc);              /* the icons sit at OPTION_MASTER's depth */
            strip_done = 1;
        }
        if (!gate_on[h->gate[i]]) continue;
        if (h->tach[i] && s->no_gauges) continue;
        if (h->mmap[i] && s->map_off) continue;
        if ((h->gate[i] == G_POS && s->no_pos) || (h->gate[i] == G_LAPS && s->no_laps)) continue;
        /* the race over mask (0x600090): only that widget and the
           countdown's (hi 0x20000) keep their objects */
        if (s->race_over && h->role[i] != R_COUNTDOWN &&
            !(h->dw && (i == h->dw->o_ro_msg || i == h->dw->o_ro_back))) continue;
        if (h->dw && i == h->dw->o_ww && !(s->racing && !s->race_over)) continue;
        switch (h->role[i]) {
        case R_RPM_NEEDLE:
            if (s->have_rpm)
                fe_sprite_at(h, dc, i, o->ax, o->ay,
                             tach_angle(h) * (float)M_PI / 180.0f,
                             o->rgba);
            break;
        case R_TURBO_NEEDLE: {
            float b = s->have_boost ? s->boost : 0.0f;
            if (b < -1.0f) b = -1.0f;
            if (b > 1.0f) b = 1.0f;
            fe_sprite_at(h, dc, i, o->ax, o->ay,
                         (BOOST_DEG_ZERO + b * BOOST_DEG_SPAN) * (float)M_PI / 180.0f, o->rgba);
            break; }
        case R_SPEED:   /* 0x4c68d0: m/s x 2.237 mph or x 3.6 km/h */
            snprintf(buf, sizeof buf, "%d", (int)(h->needle_speed * (s->kmh ? 1.0f : 2.237f / 3.6f) + 0.5f));
            fe_text(h, dc, i, buf, o->rgba);
            break;
        case R_UNITS:
            fe_text(h, dc, i, hud_str(h->dataroot, s->kmh ? 0x8569a25fu : 0x8569ab44u), o->rgba);
            break;
        case R_GEAR:
            if (!s->have_rpm) break;
            if (s->gear < 0) snprintf(buf, sizeof buf, "R");
            else if (!s->gear) snprintf(buf, sizeof buf, "N");
            else snprintf(buf, sizeof buf, "%d", s->gear);
            {   /* 0x4c6aa6: 0xffc3c3c3 in gear, 0xffff8040 mid-shift (car +0x448 == 0) */
                float c[4] = { 0xc3 / 255.0f, 0xc3 / 255.0f, 0xc3 / 255.0f, o->rgba[3] };
                if (s->gear_out) { c[0] = 1.0f; c[1] = 0x80 / 255.0f; c[2] = 0x40 / 255.0f; }
                fe_text(h, dc, i, buf, c);
            }
            break;
        case R_SHIFT:
            if (s->have_rpm && s->gear > 0 && s->rpm_redline > 0.0f &&
                s->rpm >= s->rpm_redline * 0.93f)
                fe_sprite(h, dc, i);
            break;
        case R_NOS_FILL: {
            /* NitrousGauge::Update 0x4c7b20 (GC 8010d324), single tach (0x89e7c0 == 1, not drag):
               f <= 0 hides it; f <= 1 rotates NOS_Mask to f and colours it TweakRegulalNOSColour
               0x96005a7d (+0x2c); above one bottle NOS_Mask goes to f - 1 and the colour is
               TweakGBNOSColour 0x9600b4fa (+0x30). FEngSetColor replaces the authored colour
               every frame (no boost glow: the glow scripts never start on PC, 0x4da710) */
            float f = s->have_nitro_tank ? s->nitro_tank : 1.0f;
            if (f > 2.0f) f = 2.0f;
            if (!(f > 0.0f)) break;
            float fill = f > 1.0f ? f - 1.0f : f;
            unsigned argb = f > 1.0f ? 0x9600b4fau : 0x96005a7du;
            float c[4] = { (argb >> 16 & 255) / 255.0f, (argb >> 8 & 255) / 255.0f,
                           (argb & 255) / 255.0f, (argb >> 24) / 255.0f };
            if (fill >= 0.999f) fe_sprite_at(h, dc, i, o->ax, o->ay, o->aangle, c);
            else if (fill > 0.001f) fe_nos_sector(h, dc, i, fill, c);
            break; }
        case R_NOS_LINE:   /* 0x4c7cb1: NOS_Line visible only above one bottle (0x4c7be2 hides it) */
            if (s->have_nitro_tank && s->nitro_tank > 1.0f) fe_sprite(h, dc, i);
            break;
        case R_MAP_SLOT:
            hud_draw_map(h, dc, s);
            break;
        case R_MAP_NORTH:
            /* turns about its authored pivot (the panel centre) to where
               world north currently lies */
            fe_sprite_at(h, dc, i, o->ax, o->ay, -rot, o->rgba);
            break;
        case R_POS_NUM:
            snprintf(buf, sizeof buf, "%d", s->position);
            fe_text(h, dc, i, buf, o->rgba);
            break;
        case R_POS_SUFFIX: {   /* 0x4ffaa0: 10 "th", else the table 0x7f70d8 by n % 10 */
            static const uint32_t T[10] = { 0x7447c0e3u, 0xbc4ce12fu, 0x63eb6a03u, 0xbd498562u, 0x46573b3fu,
                                            0xbc4cae38u, 0xbd383957u, 0xe2ab0284u, 0x438da7c0u, 0xbcdd9448u };
            int n = s->position < 0 ? 0 : s->position;
            const char *t = hud_str(h->dataroot, n == 10 ? 0xbcdd9448u : T[n % 10]);
            fe_text(h, dc, i, t[0] ? t : hud_str(h->dataroot, 0xc01a6f63u), o->rgba);
            break; }
        case R_POS_OF:
            snprintf(buf, sizeof buf, "/%d", s->cars);
            fe_text(h, dc, i, buf, o->rgba);
            break;
        /* the lap box as the game's lap widget writes it (0x4aa1b0, English
           layout): laps = LapNumber "%2d" + NumberLaps "/%2d"; progress =
           LapText "Completed", NumberLaps "%3d%%", LapNumber hidden; no box
           data (free roam, no race manager) = nothing drawn */
        case R_LAP_LABEL:
            if (s->lapbox == HUD_LAPBOX_LAPS) fe_text(h, dc, i, o->text, o->rgba);
            else if (s->lapbox == HUD_LAPBOX_PERCENT)
                fe_text(h, dc, i, hud_str(h->dataroot, 0xb1ee4ca9u), o->rgba);
            break;
        case R_LAP_NUM:
            if (s->lapbox != HUD_LAPBOX_LAPS) break;
            snprintf(buf, sizeof buf, "%2d", s->lapbox_num);
            fe_text(h, dc, i, buf, o->rgba);
            break;
        case R_LAP_OF:
            if (s->lapbox == HUD_LAPBOX_LAPS) snprintf(buf, sizeof buf, "/%2d", s->lapbox_of);
            else if (s->lapbox == HUD_LAPBOX_PERCENT) snprintf(buf, sizeof buf, "%3d%%", s->lapbox_pct);
            else break;
            fe_text(h, dc, i, buf, o->rgba);
            break;
        case R_BANK_VALUE:   /* Cash_data: "%$d" of CareerWallet::GetBalance (PC 0x78f744) */
            dw_num(buf, sizeof buf, (int)s->money);
            fe_text(h, dc, i, buf, o->rgba);
            break;
        case R_COUNTDOWN: {
            /* 0x4ab5b0: hidden at stage 0; "GO!" (label 0x535) or "%d";
               alpha ftol(255 cos(ftol(frac 5898240) / 360 in 16-bit
               angle units)), scale 2 frac + 1 about its own position */
            if (s->countdown <= 0) break;
            const char *t = buf;
            if (s->countdown == 4) t = hud_str(h->dataroot, 0x535u);
            else snprintf(buf, sizeof buf, "%d", s->countdown);
            float f = s->countdown_frac;
            int ang = (int)(f * 5898240.0f) / 360;                  /* 0x790a70 */
            float c[4] = { o->rgba[0], o->rgba[1], o->rgba[2], 0 };
            c[3] = (float)(int)(cosf((float)(unsigned short)ang * (6.28318531f / 65536.0f)) * 255.0f) / 255.0f;
            fe_text_at(h, dc, i, t, o->ax, o->ay, 2.0f * f + 1.0f, c);   /* 0x78e070 */
            break; }
        case R_ANIM:
            if (h->dw) fe_anim_draw(h, dc, i);
            break;
        case R_CORNER: {
            /* 3rdperson_BoxCorner is the race box: its art (256x64, the shade
               in texels ~8..160) covers the position readout, but the retail
               layout starts "Bank:" (left-justified, x 119) ~20 units left of
               the shade (x 137). Port-side (user 11-13): behind the bank in
               free roam, stretch it from the shade's right edge so it starts
               6 units before "Bank:". Races keep the authored box. */
            int bl = h->o_bank_label;
            if (s->racing || !gate_on[G_BANK] || bl < 0 || o->size[0] <= 0.0f) { fe_sprite(h, dc, i); break; }
            float w = o->size[0], left = o->ax - 0.5f * w;
            float vis_l = left + w * (8.0f / 256.0f), vis_r = left + w * (160.0f / 256.0f);
            float want_l = p->obj[bl].ax - 6.0f;
            if (want_l >= vis_l) { fe_sprite(h, dc, i); break; }
            float sx = (vis_r - want_l) / (vis_r - vis_l);
            float w2 = w * sx, left2 = vis_r - w2 * (160.0f / 256.0f);
            const HudTex *t = (o->res >= 0 && o->res < p->nres) ? &h->rt[o->res] : NULL;
            if (!t || !t->tex) break;
            float nx, ny;
            fe_ndc(dc->asp, hud_pin_of(p->obj[o->root].ax), left2 + 0.5f * w2, o->ay, &nx, &ny);
            hud_blit(dc, t->tex, w2 / 240.0f, fabsf(o->size[1]) / 240.0f, nx, ny, 0.0f,
                     o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3]);
            break; }
        default:
            if (o->type == FENG_STRING) fe_text(h, dc, i, o->text, o->rgba);
            else fe_sprite(h, dc, i);
            break;
        }
    }
}

/* ------------------------------------------------------- navigation arrow */

static void hud_draw_nav_arrow(Hud *h, const HudDC *dc, const HudState *s) {
    if (!s->nav_active) { h->arrow_primed = 0; return; }
    /* Relative bearing: where the next reachable route node lies compared to
       where the car points. Guidance never points at the destination through
       a building — nav_bearing comes from the solved route, not from the
       straight line to the target. */
    float want = hud_angle_delta(s->car_heading, s->nav_bearing);
    if (!h->arrow_primed) { h->arrow_angle = want; h->arrow_primed = 1; }
    else {
        /* ease along the SHORT way round, so a turn through +/-pi does not
           whip the arrow the wrong direction */
        h->arrow_angle += hud_angle_delta(h->arrow_angle, want) * 0.20f;
        h->arrow_angle = hud_angle_delta(0.0f, h->arrow_angle);
    }
    float ax, ay;
    hud_place(dc->asp, 0.0f, 1.0f, 0.0f, -0.20f, &ax, &ay);   /* exact top centre */
    /* Both angles are world bearings (CCW from +X), so a positive delta means
       the target is CCW of the nose = to the LEFT. hud_xform's rot is also
       CCW-positive, so the arrow angle passes straight through: negating it
       here points the arrow at the mirror image of every turn. */
    float rot = h->arrow_angle;
    if (h->arrow_tex) {
        /* the sprite points up at rot 0; a right turn must lean right */
        hud_blit(dc, h->arrow_tex, 0.13f, 0.26f, ax, ay, rot,
                 0.30f, 0.62f, 1.0f, 0.95f);
    } else {
        /* No usable arrow art ships in these packs, so this chevron is drawn,
           not decoded. Two swept bars meeting at the tip -- deliberately plain,
           so it is never mistaken for the original sprite. */
        const float L = 0.115f, T = 0.032f, SPREAD = 0.70f;
        /* forward is "up" at rot 0; screen y is up, so dir = (-sin, cos) */
        float fx = -sinf(rot), fy = cosf(rot);
        float tipx = ax + fx * L * 0.62f / dc->asp, tipy = ay + fy * L * 0.62f;
        for (int side = -1; side <= 1; side += 2) {
            /* each leg runs BACK from the tip, swept +/-SPREAD off the axis */
            float c = cosf((float)side * SPREAD), sn = sinf((float)side * SPREAD);
            float lx = -fx * c - -fy * sn, ly = -fx * sn + -fy * c;
            float mx = tipx + lx * L * 0.5f / dc->asp, my = tipy + ly * L * 0.5f;
            hud_blit(dc, 0, L, T, mx, my, atan2f(ly, lx),
                     0.30f, 0.62f, 1.0f, 0.95f);
        }
        /* shaft, so the glyph reads as an arrow rather than a bare V */
        hud_blit(dc, 0, L * 0.95f, T * 0.82f,
                 ax - fx * L * 0.10f / dc->asp, ay - fy * L * 0.10f,
                 atan2f(fy, fx), 0.30f, 0.62f, 1.0f, 0.95f);
    }
    if (s->nav_dist_m > 0.5f) {
        char buf[32];
        if (s->nav_dist_m >= 1000.0f)
            snprintf(buf, sizeof buf, "%d KM", (int)(s->nav_dist_m / 1000.0f + 0.5f));
        else
            snprintf(buf, sizeof buf, "%d M", (int)(s->nav_dist_m + 0.5f));
        hud_label(dc, buf, ax, ay - 0.175f, 0.040f, 0.55f, 0.80f, 1.0f, 0.95f);
    }
}

/* ------------------------------------------------------------------ frame */

void hud_draw(Hud *h, const RProg *rp, GpuMesh *quad,
              const HudState *st, int width, int height) {
    if (!h || !rp || !quad || !st || width <= 0 || height <= 0) return;

    HudState s = *st;
    if (h->preview) {           /* clearly-labelled sample values only */
        s.have_rpm = 1; s.have_nitro_tank = 1; s.have_boost = 1; s.have_money = 1;
        s.have_nos = 1; s.have_turbo = 1;   /* show the full cluster */
        if (s.rpm_redline < 1.0f) s.rpm_redline = 8000.0f;
        if (s.rpm <= 0.0f) s.rpm = 5200.0f;
        if (s.speed_kmh <= 0.0f) s.speed_kmh = 137.0f;
        if (s.gear == 0) s.gear = 4;
        s.nitro_tank = 0.62f; s.boost = 0.4f; s.money = 250000;
    }

    HudDC dc; dc.rp = rp; dc.quad = quad; dc.asp = (float)width / (float)height;

    /* --- enter 2D: save what we change so the world pass is untouched --- */
    GLboolean depth_on = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend_on = glIsEnabled(GL_BLEND);
    GLint blend_src = GL_SRC_ALPHA, blend_dst = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);
    GLboolean depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(rp->uUnlit, 1.0f);
    glUniform1f(rp->uVColor, 0.0f);
    /* both unlit branches mix toward uFogColor; a HUD quad's vPos is meaningless
       so the only safe fog for it is none. Restored below. */
    GLfloat fog_density = 0.0f;
    glGetUniformfv(rp->prog, rp->uFogDensity, &fog_density);
    glUniform1f(rp->uFogDensity, 0.0f);
    glUniform1f(rp->uAlpha, 1.0f);
    glUniform1f(rp->uDecal, 0.0f);
    glUniform1f(rp->uAlphaTest, 0.0f);
    glUniform1f(rp->uTextureAlpha, 1.0f);   /* HUD art is alpha-blended sprites */
    glUniform1f(rp->uEmissiveTex, 0.0f);
    glUniform1f(rp->uVista, 0.0f);
    glUniform3f(rp->uEmissive, 0.0f, 0.0f, 0.0f);

    #ifdef __vita__
    vita_lap(10);
    #endif
    /* magazine mode: DetermineHudFeatures 0 -- every widget off, nothing
       of the driving HUD drawn; only the reward indicator's Update runs
       (it hides Cash_Reward and holds its delay). (guess) The widgets'
       feature-on scripts are not replayed when the mode ends. */
    if (s.magazine) {
        if (h->pk && h->dw && !h->dw->screen) cr_update(h, &s);
        goto hud_restore;
    }
    if (h->pk && h->dw && !h->dw->screen) { ro_update(h, &s); dy_update(h, &s); ww_update(h, &s); lt_update(h, &s); sp_update(h, &s); lb_update(h, &s);
                                            gw_update(h, &s); ei_update(h, &s); sc_update(h, &s); gps_update(h, &s);
                                            sms_update(h, &s); cr_update(h, &s); cb_update(h, &s); st_update(h, &s); }
    if (h->pk && h->dw && h->dw->drag && s.drag) drag_update(h, &s, s.drag);
    if (h->pk && h->dw && s.drift) {
        drift_update(h, s.drift);
        feng_anim_tick(&h->dw->anim, s.drift->dt_ms);
    } else if (h->pk && h->dw && !h->dw->screen) feng_anim_tick(&h->dw->anim, s.dt_ms);
    if (h->pk) hud_draw_screen(h, &dc, &s);
    #ifdef __vita__
    vita_lap(11);
    #endif
    if (!s.race_over) hud_draw_nav_arrow(h, &dc, &s);
    #ifdef __vita__
    vita_lap(15);
    #endif

    if (h->preview) {
        float px, py;
        hud_place(dc.asp, 0.0f, -1.0f, 0.0f, 0.045f, &px, &py);
        hud_label(&dc, "PREVIEW SAMPLE VALUES", px, py, 0.040f,
                  1.0f, 0.55f, 0.25f, 1.0f);
    }

    /* --- restore --- */
hud_restore:
    glUniform1f(rp->uUseTex, 0.0f);
    glUniform1f(rp->uUnlit, 0.0f);
    glUniform1f(rp->uEmissiveTex, 0.0f);
    glUniform1f(rp->uAlpha, 1.0f);
    glUniform1f(rp->uTextureAlpha, 0.0f);
    glUniform1f(rp->uFogDensity, fog_density);
    glUniform3f(rp->uColor, 1.0f, 1.0f, 1.0f);
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc((GLenum)blend_src, (GLenum)blend_dst);
}

/* ------------------------------------------------------------ banner text */

/* The tallest loaded HUD font (the game's own), or -1. */
static int hud_menu_font(const Hud *h) {
    int best = -1;
    for (int f = 0; f < HUD_NFONT; f++)
        if (h->font[f].ok && (best < 0 || h->font[f].f.line > h->font[best].f.line))
            best = f;
    return best;
}

/* `txt` in font `fi` at FEng point (fx, fy) (640x480 units, screen centre
 * origin, y down), `scale` x the font's size; just 0 left, 1 centre, 2 right.
 * Falls back to the procedural label font when no game font loaded. */
static void hud_menu_text(Hud *h, const HudDC *dc, int fi, const char *txt,
                          float fx, float fy, float scale, int just, const float *rgba) {
    if (!txt || !*txt) return;
    if (fi < 0) {
        float nx, ny; fe_ndc(dc->asp, 0, fx, fy, &nx, &ny);
        hud_label(dc, txt, nx, ny, 24.0f * scale / 240.0f, rgba[0], rgba[1], rgba[2], rgba[3]);
        return;
    }
    const HudFont *F = &h->font[fi];
    float w = feng_text_width(&F->f, txt) * scale;
    float x = just == 1 ? fx - 0.5f * w : just == 2 ? fx - w : fx;
    float y = fy - 0.5f * (float)F->f.line * scale;
    int nq = 0;
    float v[HUD_QMAX * 4 * 5];
    for (const char *c = txt; *c && nq < HUD_QMAX; c++) {
        const FengGlyph *g = &F->f.g[(unsigned char)*c];
        if (!g->ok) continue;
        if (g->w && g->h) {
            float x0 = x + g->xo * scale, y0 = y + g->yo * scale;
            float x1 = x0 + g->w * scale, y1 = y0 + g->h * scale;
            float ax, ay, bx, by;
            fe_ndc(dc->asp, 0, x0, y0, &ax, &ay);
            fe_ndc(dc->asp, 0, x1, y1, &bx, &by);
            float u0 = (float)g->u / F->aw, v0 = (float)g->v / F->ah;
            float u1 = (float)(g->u + g->w) / F->aw, v1 = (float)(g->v + g->h) / F->ah;
            float *q = v + nq * 20;
            put_vert(q,      ax, ay, u0, v0);
            put_vert(q + 5,  bx, ay, u1, v0);
            put_vert(q + 10, bx, by, u1, v1);
            put_vert(q + 15, ax, by, u0, v1);
            nq++;
        }
        x += g->adv * scale;
    }
    hud_batch(h, dc, F->tex, v, nq, rgba);
}

/* A movie full screen (IG_PlayMovie): black, the picture letterboxed */
void hud_draw_movie(const RProg *rp, GpuMesh *quad, GLuint tex, int tw, int th, int width, int height) {
    if (!rp || !quad || width <= 0 || height <= 0) return;
    HudDC dc; dc.rp = rp; dc.quad = quad; dc.asp = (float)width / (float)height;
    GLboolean depth_on = glIsEnabled(GL_DEPTH_TEST), blend_on = glIsEnabled(GL_BLEND);
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    GLfloat fog_density = 0.0f; glGetUniformfv(rp->prog, rp->uFogDensity, &fog_density);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
    glUniform1f(rp->uVColor, 0.0f); glUniform1f(rp->uFogDensity, 0.0f);
    glUniform1f(rp->uDecal, 0.0f); glUniform1f(rp->uAlphaTest, 0.0f);
    glUniform1f(rp->uTextureAlpha, 1.0f); glUniform1f(rp->uVista, 0.0f);
    glUniform3f(rp->uEmissive, 0.0f, 0.0f, 0.0f);
    hud_blit(&dc, 0, 2.0f * dc.asp, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    if (tex && tw > 0 && th > 0) {
        float ma = (float)tw / (float)th, w = 2.0f * dc.asp, h = 2.0f;
        if (dc.asp > ma) w = 2.0f * ma; else h = 2.0f * dc.asp / ma;
        hud_blit(&dc, tex, w, h, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    glUniform1f(rp->uUseTex, 0.0f); glUniform1f(rp->uUnlit, 0.0f);
    glUniform1f(rp->uEmissiveTex, 0.0f); glUniform1f(rp->uAlpha, 1.0f);
    glUniform1f(rp->uTextureAlpha, 0.0f); glUniform1f(rp->uFogDensity, fog_density);
    glUniform3f(rp->uColor, 1.0f, 1.0f, 1.0f);
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}
void hud_draw_banner(Hud *h, const RProg *rp, GpuMesh *quad, const char *text,
                     const char *sub, int width, int height) {
    if (!h || !rp || !quad || !text || width <= 0 || height <= 0) return;
    HudDC dc; dc.rp = rp; dc.quad = quad; dc.asp = (float)width / (float)height;
    GLboolean depth_on = glIsEnabled(GL_DEPTH_TEST), blend_on = glIsEnabled(GL_BLEND);
    GLint blend_src = GL_SRC_ALPHA, blend_dst = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    GLfloat fog_density = 0.0f; glGetUniformfv(rp->prog, rp->uFogDensity, &fog_density);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(rp->uVColor, 0.0f); glUniform1f(rp->uFogDensity, 0.0f);
    glUniform1f(rp->uDecal, 0.0f); glUniform1f(rp->uAlphaTest, 0.0f);
    glUniform1f(rp->uTextureAlpha, 1.0f); glUniform1f(rp->uVista, 0.0f);
    glUniform3f(rp->uEmissive, 0.0f, 0.0f, 0.0f);
    static const float gold[4] = {1.00f, 0.80f, 0.20f, 1.0f};
    static const float white[4] = {0.95f, 0.95f, 0.97f, 1.0f};
    int fi = hud_menu_font(h);
    hud_menu_text(h, &dc, fi, text, 0.0f, -60.0f, 2.6f, 1, gold);
    if (sub) hud_menu_text(h, &dc, fi, sub, 0.0f, -10.0f, 0.9f, 1, white);
    glUniform1f(rp->uUseTex, 0.0f); glUniform1f(rp->uUnlit, 0.0f);
    glUniform1f(rp->uEmissiveTex, 0.0f); glUniform1f(rp->uAlpha, 1.0f);
    glUniform1f(rp->uTextureAlpha, 0.0f); glUniform1f(rp->uFogDensity, fog_density);
    glUniform3f(rp->uColor, 1.0f, 1.0f, 1.0f);
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc((GLenum)blend_src, (GLenum)blend_dst);
}

/* Lines of text in the game's HUD font (the banner's): FEng point (x, y), scale, justification
   (0 left, 1 centre, 2 right), colour 0xAARRGGBB; an optional dark plate behind (plate[4]: centre
   x, y, w, h in FEng units, w <= 0 none) -- the canyon duel's score panel (port) */
void hud_draw_lines(Hud *h, const RProg *rp, GpuMesh *quad, int width, int height,
                    const HudLine *ln, int n, const float *plate) {
    if (!h || !rp || !quad || !ln || n <= 0 || width <= 0 || height <= 0) return;
    HudDC dc; dc.rp = rp; dc.quad = quad; dc.asp = (float)width / (float)height;
    GLboolean depth_on = glIsEnabled(GL_DEPTH_TEST), blend_on = glIsEnabled(GL_BLEND);
    GLint blend_src = GL_SRC_ALPHA, blend_dst = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    GLfloat fog_density = 0.0f; glGetUniformfv(rp->prog, rp->uFogDensity, &fog_density);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(rp->uVColor, 0.0f); glUniform1f(rp->uFogDensity, 0.0f);
    glUniform1f(rp->uDecal, 0.0f); glUniform1f(rp->uAlphaTest, 0.0f);
    glUniform1f(rp->uTextureAlpha, 1.0f); glUniform1f(rp->uVista, 0.0f);
    glUniform3f(rp->uEmissive, 0.0f, 0.0f, 0.0f);
    if (plate && plate[2] > 0.0f && plate[3] > 0.0f) {
        float nx, ny; fe_ndc(dc.asp, 0, plate[0], plate[1], &nx, &ny);
        hud_blit(&dc, 0, plate[2] / 240.0f, plate[3] / 240.0f, nx, ny, 0.0f, 0.0f, 0.0f, 0.0f, 0.55f);
    }
    int fi = hud_menu_font(h);
    for (int i = 0; i < n; i++) {
        uint32_t c = ln[i].argb;
        float rgba[4] = { (float)((c >> 16) & 255) / 255.0f, (float)((c >> 8) & 255) / 255.0f,
                          (float)(c & 255) / 255.0f, (float)(c >> 24) / 255.0f };
        hud_menu_text(h, &dc, fi, ln[i].txt, ln[i].x, ln[i].y, ln[i].scale, ln[i].just, rgba);
    }
    glUniform1f(rp->uUseTex, 0.0f); glUniform1f(rp->uUnlit, 0.0f);
    glUniform1f(rp->uEmissiveTex, 0.0f); glUniform1f(rp->uAlpha, 1.0f);
    glUniform1f(rp->uTextureAlpha, 0.0f); glUniform1f(rp->uFogDensity, fog_density);
    glUniform3f(rp->uColor, 1.0f, 1.0f, 1.0f);
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc((GLenum)blend_src, (GLenum)blend_dst);
}

/* A polyline in FEng screen space (640x480, origin at the centre, y down),
   n points xy[2n], colour 0xAARRGGBB, `width` FEng units: the FEGraph lines
   (DynoRunChartScreen::SetupGraph PC 0x53d0a0) */
void hud_fe_lines(const RProg *rp, GpuMesh *quad, int width, int height,
                  const float *xy, int n, uint32_t argb, float thick) {
    if (!rp || !quad || !xy || n < 2 || width <= 0 || height <= 0) return;
    HudDC dc; dc.rp = rp; dc.quad = quad; dc.asp = (float)width / (float)height;
    GLboolean depth_on = glIsEnabled(GL_DEPTH_TEST), blend_on = glIsEnabled(GL_BLEND);
    GLint blend_src = GL_SRC_ALPHA, blend_dst = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    GLfloat fog_density = 0.0f; glGetUniformfv(rp->prog, rp->uFogDensity, &fog_density);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(rp->uVColor, 0.0f); glUniform1f(rp->uFogDensity, 0.0f);
    glUniform1f(rp->uDecal, 0.0f); glUniform1f(rp->uAlphaTest, 0.0f);
    glUniform1f(rp->uTextureAlpha, 1.0f); glUniform1f(rp->uVista, 0.0f);
    glUniform3f(rp->uEmissive, 0.0f, 0.0f, 0.0f);
    float r = (float)((argb >> 16) & 255) / 255.0f, g = (float)((argb >> 8) & 255) / 255.0f;
    float b = (float)(argb & 255) / 255.0f, a = (float)(argb >> 24) / 255.0f;
    for (int i = 0; i + 1 < n; i++) {
        float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * i + 2], by = xy[2 * i + 3];
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        float nx, ny; fe_ndc(dc.asp, 0, 0.5f * (ax + bx), 0.5f * (ay + by), &nx, &ny);
        hud_blit(&dc, 0, (len + thick * 0.5f) / 240.0f, thick / 240.0f, nx, ny, atan2f(-dy, dx), r, g, b, a);
    }
    glUniform1f(rp->uUseTex, 0.0f); glUniform1f(rp->uUnlit, 0.0f);
    glUniform1f(rp->uEmissiveTex, 0.0f); glUniform1f(rp->uAlpha, 1.0f);
    glUniform1f(rp->uTextureAlpha, 0.0f); glUniform1f(rp->uFogDensity, fog_density);
    glUniform3f(rp->uColor, 1.0f, 1.0f, 1.0f);
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc((GLenum)blend_src, (GLenum)blend_dst);
}
