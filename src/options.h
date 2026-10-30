/* options.h -- the game's option settings (SPEED2.EXE 0x83aa18.., the
 * profile's options block), edited by the Options screens (shop.c, rows
 * of UI_Options 0x4b6e60 / 0x4b7310 / 0x4b7cd0) and read by the game.
 *
 * The retail defaults are OptionsSettings::Default 0x5025b0 (GC
 * Default__15OptionsSettings 80130c78), see options_defaults. The volume
 * levels reach the mixer linearly: GetMasterVolume (GC 8007b23c) =
 * master x level x 32767 (FUN_00469050 only stores the block and asks the
 * two sound objects to re-read it).
 *
 * Saved to ux0:data/OpenUG2/options.bin (cache dir relative on PC).
 */
#ifndef OPENUG2_OPTIONS_H
#define OPENUG2_OPTIONS_H

/* one player's block (0x83aa64 + p * 0x28) */
typedef struct {
    unsigned char gauges;        /* +0x00 */
    unsigned char race_pos;      /* +0x01 */
    unsigned char lap_info;      /* +0x02 */
    unsigned char score;         /* +0x03 */
    unsigned char feedback;      /* +0x04 force feedback (wheel only) */
    unsigned char leaderboard;   /* +0x05 */
    unsigned char ask_trans;     /* +0x06 Ask for Transmission */
    int  camera;                 /* +0x10 0 Bumper 1 Hood 2 Close 3 Far (4 Super Far: shown, not cycled) */
    unsigned char split;         /* +0x14 0 Leader 1 Lap Record 2 Best Lap 3 Last Lap 4 Off */
    unsigned char manual;        /* +0x15 Transmission: 0 Auto 1 Manual */
    unsigned char stability_off; /* +0x16 Stability Control: 0 On 1 Off */
} OptPlayer;

/* the game actions of the controller screen, in its row order (0x4dba50) */
enum { OPT_ACT_STEER_L, OPT_ACT_STEER_R, OPT_ACT_THROTTLE, OPT_ACT_BACK, OPT_ACT_SHIFT_UP,
       OPT_ACT_SHIFT_DOWN, OPT_ACT_HANDBRAKE, OPT_ACT_NITROUS, OPT_ACT_CAMERA, OPT_ACT_LOOK_BACK,
       OPT_ACT_ENGAGE, OPT_ACT_WORLD_MAP, OPT_ACT_READ_SMS, OPT_ACT_CAREER_STATS, OPT_ACT_RESET,
       OPT_ACT_HYD_PRESSURE, OPT_ACT_HYD_BOUNCE, OPT_ACT_PAUSE, OPT_ACT_COUNT };
/* Vita buttons a binding can name */
enum { VB_NONE, VB_CROSS, VB_CIRCLE, VB_SQUARE, VB_TRIANGLE, VB_L, VB_R, VB_UP, VB_DOWN,
       VB_LEFT, VB_RIGHT, VB_START, VB_SELECT,
       VB_LS_LEFT, VB_LS_RIGHT,                     /* the left stick past half */
       VB_RT_UL, VB_RT_UR, VB_RT_LL, VB_RT_LR,      /* rear touchpad corners (the pad's L2 R2 L3 R3) */
       VB_COUNT };
extern const char *const g_vb_name[VB_COUNT];

typedef struct {
    /* audio (0x83aa28.., applied by FUN_00469050) */
    float master, speech, fe_music, ig_music, sfx, engine, tire;
    int   stereo;                /* 0x83aa50: 0 Mono 1 Stereo */
    /* gameplay */
    int   jump_cam;              /* 0x83aa20 (0 Off / 127 On in the original) */
    unsigned char mirror;        /* 0x83aa18 Rearview Mirror */
    unsigned char kmh;           /* 0x83aa1a Speedometer Units: 0 MPH 1 KM/H */
    unsigned char race_map;      /* 0x83aa1b 0 Off 1 Static 2 Rotate */
    unsigned char explore_map;   /* 0x83aa1c */
    int   career_diff;           /* 0x83ac30 0 Easy 1 Medium 2 Hard */
    unsigned char save_on, autosave;   /* 0x844ad8 / 0x844ad9 */
    /* EA Trax (profile +0x2d8 / 0x83aa58) */
    unsigned char shuffle;       /* 0 Sequential 1 Random */
    /* the playlist (0x83acb8): 27 slots {song, mode 0 off 1 FE 2 IG 3 all},
       reordered and set in the jukebox; filled with music.c's default
       (FUN_00515aa0) while playlist_set is 0 */
    unsigned char pl_song[27], pl_mode[27];
    unsigned char playlist_set;
    OptPlayer p[1];
    /* controller setup (UI_Options_PC_Controller): per action (OPT_ACT_*) a
       Primary and a Secondary Vita button (VB_*, 0 = none) */
    unsigned char ctl[OPT_ACT_COUNT][2];
    /* the Vita's rear touchpad (Player options, in place of the wheel rows):
       off, or its four corner zones sized by the sensitivity 0 Low 1 Medium 2 High */
    unsigned char rear_touch, touch_sens;
} N2Options;

extern N2Options g_opt;

void options_defaults(N2Options *o);
/* the default bindings. Scheme 0 is the original's for a PS-style pad (the
   Logitech Dual Action / Rumblepad 2 rows of the known-pad table 0x7fe7f8,
   picked by 0x5b81b0 from 0x5c11a0), its L2 R2 L3 R3 on the rear touch
   corners; scheme 1 drives on the triggers (R gas, L brake). */
void options_ctl_preset(N2Options *o, int scheme);
/* load from disk (defaults when missing or from another build) */
void options_load(void);
void options_save(void);
/* the rear touch zone (VB_RT_*, 0 none) at u, v in 0..1 of the panel (v down) */
int options_touch_zone(const N2Options *o, float u, float v);
/* the port's own option labels (the game has no strings for them), NULL else */
const char *options_port_str(unsigned int hash);
#define STR_REAR_TOUCH 0x0a5e7001u   /* "Rear Touchpad" */
#define STR_TOUCH_SENS 0x0a5e7002u   /* "Rear Touch Sensitivity" */

#endif
