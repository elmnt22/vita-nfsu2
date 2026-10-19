/* options.c -- the option settings and their file (see options.h). */
#include "options.h"
#include <stdio.h>
#include <string.h>

#ifdef __vita__
#define OPT_FILE "ux0:data/OpenUG2/options.bin"
#else
#define OPT_FILE "cache/options.bin"
#endif
#define OPT_MAGIC "OUOPT004"

N2Options g_opt;
const char *const g_vb_name[VB_COUNT] = { "", "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L", "R",
                                          "UP", "DOWN", "LEFT", "RIGHT", "START", "SELECT",
                                          "L STICK LEFT", "L STICK RIGHT", "REAR TOP LEFT",
                                          "REAR TOP RIGHT", "REAR BOTTOM LEFT", "REAR BOTTOM RIGHT" };

void options_ctl_preset(N2Options *o, int scheme) {
    memset(o->ctl, 0, sizeof o->ctl);
    /* Logitech pad (0x7fe7f8 rows c216/c218/c21a): b0 Square brake, b1 Cross
       gas, b2 Circle look back, b3 Triangle camera, b4 L1 nitrous, b5 R1
       handbrake, b6 L2 / b7 R2 shift down / up, b8 Select reset, b9 Start
       pause, b10 L3 pressurize, b11 R3 bounce; POV up engage, down stats,
       left map, right SMS. Steering is the stick (the axes table). */
    o->ctl[OPT_ACT_STEER_L][0] = VB_LS_LEFT;    o->ctl[OPT_ACT_STEER_R][0] = VB_LS_RIGHT;
    o->ctl[OPT_ACT_THROTTLE][0] = scheme ? VB_R : VB_CROSS;
    o->ctl[OPT_ACT_BACK][0] = scheme ? VB_L : VB_SQUARE;
    o->ctl[OPT_ACT_LOOK_BACK][0] = VB_CIRCLE;   o->ctl[OPT_ACT_CAMERA][0] = VB_TRIANGLE;
    o->ctl[OPT_ACT_NITROUS][0] = scheme ? VB_CROSS : VB_L;
    o->ctl[OPT_ACT_HANDBRAKE][0] = scheme ? VB_SQUARE : VB_R;
    o->ctl[OPT_ACT_SHIFT_DOWN][0] = VB_RT_UL;   o->ctl[OPT_ACT_SHIFT_UP][0] = VB_RT_UR;
    o->ctl[OPT_ACT_HYD_PRESSURE][0] = VB_RT_LL; o->ctl[OPT_ACT_HYD_BOUNCE][0] = VB_RT_LR;
    o->ctl[OPT_ACT_RESET][0] = VB_SELECT;       o->ctl[OPT_ACT_PAUSE][0] = VB_START;
    o->ctl[OPT_ACT_ENGAGE][0] = VB_UP;          o->ctl[OPT_ACT_CAREER_STATS][0] = VB_DOWN;
    o->ctl[OPT_ACT_WORLD_MAP][0] = VB_LEFT;     o->ctl[OPT_ACT_READ_SMS][0] = VB_RIGHT;
}

/* the corners' width / height share of the panel by sensitivity: High
   splits it in quadrants, Low leaves a wide dead cross where the fingers
   hold the Vita */
int options_touch_zone(const N2Options *o, float u, float v) {
    static const float zw[3] = { 0.30f, 0.40f, 0.50f }, zh[3] = { 0.35f, 0.45f, 0.50f };
    if (!o->rear_touch || u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 0;
    int k = o->touch_sens > 2 ? 2 : o->touch_sens;
    int left = u < zw[k], right = u >= 1.0f - zw[k], top = v < zh[k], bottom = v >= 1.0f - zh[k];
    if (top && left) return VB_RT_UL;
    if (top && right) return VB_RT_UR;
    if (bottom && left) return VB_RT_LL;
    if (bottom && right) return VB_RT_LR;
    return 0;
}

const char *options_port_str(unsigned int hash) {
    switch (hash) {
    case STR_REAR_TOUCH: return "Rear Touchpad";
    case STR_TOUCH_SENS: return "Rear Touch Sensitivity";
    default: return NULL;
    }
}

void options_defaults(N2Options *o) {
    memset(o, 0, sizeof *o);
    o->master = o->speech = o->fe_music = o->ig_music = o->sfx = o->engine = o->tire = 1.0f;
    o->stereo = 1;                    /* 0x515aa0: +0x70 = FUN_0040cb20 (the device: stereo) */
    o->jump_cam = 1;
    o->kmh = 0;                       /* English: imperial */
    o->race_map = 2; o->explore_map = 2;
    o->career_diff = 1;
    o->save_on = 1; o->autosave = 1;
    o->shuffle = 0;                   /* bss 0x83aa58: Sequential */
    OptPlayer *p = &o->p[0];
    p->gauges = p->race_pos = p->lap_info = p->score = p->leaderboard = 1;
    p->camera = 3;                    /* the port's chase camera started at Far */
    p->split = 0;
    o->rear_touch = 1; o->touch_sens = 1;   /* Medium */
    options_ctl_preset(o, 0);   /* the original's pad layout */
}

void options_load(void) {
    options_defaults(&g_opt);
    FILE *f = fopen(OPT_FILE, "rb");
    if (!f) { printf("options: defaults (no %s)\n", OPT_FILE); return; }
    char m[8]; unsigned sz = 0;
    N2Options o;
    int ok = fread(m, 1, 8, f) == 8 && fread(&sz, 4, 1, f) == 1;
    if (ok && !memcmp(m, OPT_MAGIC, 8) && sz == sizeof o && fread(&o, sizeof o, 1, f) == 1) {
        g_opt = o;
        printf("options: loaded %s\n", OPT_FILE);
    } else if (ok && !memcmp(m, "OUOPT003", 8) && sz <= sizeof o && fread(&g_opt, sz, 1, f) == 1) {
        /* 003 ended at ctl, which held the port's own layout: keep the
           rest, take the original's bindings and the touchpad defaults */
        g_opt.rear_touch = 1; g_opt.touch_sens = 1;
        options_ctl_preset(&g_opt, 0);
        printf("options: migrated %s\n", OPT_FILE);
    } else {
        options_defaults(&g_opt);
        printf("options: %s is from another build, defaults\n", OPT_FILE);
    }
    fclose(f);
}

void options_save(void) {
    FILE *f = fopen(OPT_FILE, "wb");
    if (!f) { printf("options: cannot write %s\n", OPT_FILE); return; }
    unsigned sz = sizeof g_opt;
    fwrite(OPT_MAGIC, 1, 8, f); fwrite(&sz, 4, 1, f); fwrite(&g_opt, sizeof g_opt, 1, f);
    fclose(f);
    printf("options: saved\n");
}
