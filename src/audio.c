/* audio.c — OpenUG2 Audio module implementation. */
#include "audio.h"
#include "music.h"
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* band center RPMs (idle / powerband / scream) */
static const float ENG_CENTER[3] = { 1500.0f, 4000.0f, 7000.0f };
/* 4-cyl 4-stroke: 2 firing events per rev -> fundamental = rpm/60*2 */
#define ENG_FIRE_HZ(rpm) ((rpm) / 60.0f * 2.0f)

EngineSynthState g_engine = {
    .current_rpm = ENG_IDLE_RPM, .target_rpm = ENG_IDLE_RPM,
    .load = 0.0f, .current_load = 0.0f, .master_volume = 0.0f,
    .bands = { { ENG_FIRE_HZ(1500.0f), 0, 0 },
               { ENG_FIRE_HZ(4000.0f), 0, 0 },
               { ENG_FIRE_HZ(7000.0f), 0, 0 } },
};
volatile float g_road_vol = 0.0f;
volatile float g_hit = 0.0f;
volatile float g_skid = 0.0f;
volatile float g_skid3[3];
volatile int g_skid3_on;
volatile int g_turbo_fitted = 1;
volatile float g_turbo_boost = 0.0f;
volatile float g_speed_frac = 0.0f;
volatile int g_nitro = 0;
volatile int g_purge = 0;
static volatile int g_shift_event;   /* gearbox: +1 upshift, -1 downshift */
static volatile int g_audio_hold, g_audio_held, g_audio_live;

/* piecewise-linear crossfade: full L at/below C_L, full H at/above C_H */
void eng_band_weights(float rpm, float w[3]) {
    w[0] = w[1] = w[2] = 0.0f;
    if (rpm <= ENG_CENTER[0])      w[0] = 1.0f;
    else if (rpm < ENG_CENTER[1]) {
        float t = (rpm - ENG_CENTER[0]) / (ENG_CENTER[1] - ENG_CENTER[0]);
        w[0] = 1.0f - t; w[1] = t;
    } else if (rpm < ENG_CENTER[2]) {
        float t = (rpm - ENG_CENTER[1]) / (ENG_CENTER[2] - ENG_CENTER[1]);
        w[1] = 1.0f - t; w[2] = t;
    } else                         w[2] = 1.0f;
}

/* 6-speed gearbox: rpm-per-speed ratios (index 1..6; [0] unused). Scaled so
 * 6th at full speed_frac sits just under redline (1000 + 25*272 = 7800). */
static const float ENG_GEAR_RATIO[7] = { 0, 130, 82, 56, 41, 32, 25 };
#define ENG_RATIO_SCALE (6800.0f / 25.0f)

void eng_gearbox_step(float speed_frac, float throttle, float dt,
                      int *gear, float *shift_t) {
    if (*shift_t > 0.0f) {           /* clutch window: cut load, revs sag */
        *shift_t -= dt;
        g_engine.load = 0.0f;
        g_engine.target_rpm += (ENG_IDLE_RPM - g_engine.target_rpm) * 0.18f;
        return;
    }
    float rpm = ENG_IDLE_RPM + speed_frac * ENG_GEAR_RATIO[*gear] * ENG_RATIO_SCALE;
    if (rpm > ENG_UPSHIFT_RPM && *gear < ENG_GEARS) {
        (*gear)++; *shift_t = ENG_SHIFT_S; g_shift_event = 1; return;
    }
    if (rpm < ENG_DOWNSHIFT_RPM && *gear > 1) {
        (*gear)--; *shift_t = ENG_SHIFT_S; g_shift_event = -1; return;
    }
    if (rpm > ENG_REDLINE_RPM) rpm = ENG_REDLINE_RPM;
    g_engine.target_rpm = rpm;
    g_engine.load = throttle > 0.0f ? 1.0f : 0.0f;
}

void eng_shift_event(int dir) { g_shift_event = dir; }

float eng_gear_rpm(float speed_frac, int gear) {
    if (gear < 1) gear = 1;
    if (gear > ENG_GEARS) gear = ENG_GEARS;
    return ENG_IDLE_RPM + speed_frac * ENG_GEAR_RATIO[gear] * ENG_RATIO_SCALE;
}

float eng_manual_thrust(float speed_frac, int gear, float shift_t) {
    if (shift_t > 0.0f) return 0.0f;                 /* clutch in */
    float rpm = eng_gear_rpm(speed_frac, gear);
    if (gear < ENG_GEARS && rpm >= ENG_REDLINE_RPM) return 0.0f;   /* limiter */
    if (gear == 1 && rpm < 4000.0f) rpm = 4000.0f;   /* clutch slip launch */
    if (rpm < 2000.0f) return 0.45f;
    if (rpm < 4000.0f) return 0.45f + 0.55f * (rpm - 2000.0f) / 2000.0f;
    if (rpm < 7500.0f) return 1.0f;
    float t = 1.0f - 0.4f * (rpm - 7500.0f) / 500.0f;  /* top end fades */
    return t < 0.6f ? 0.6f : t;                        /* 6th: no cut */
}

void eng_gearbox_manual(float speed_frac, float throttle, float dt,
                        int *gear, float *shift_t, int shift_req) {
    static int limiter_t;
    if (*gear < -1) *gear = -1;
    if (shift_req > 0 && *gear < ENG_GEARS) {
        (*gear)++; *shift_t = ENG_SHIFT_S; g_shift_event = 1;
    } else if (shift_req < 0 && *gear > -1 &&
               (*gear > 1 ? eng_gear_rpm(speed_frac, *gear - 1) < ENG_REDLINE_RPM + 300.0f
                : *gear == 1 ? 1                            /* 1 -> N: always */
                : speed_frac < 0.03f)) {                    /* N -> R: ~7 km/h */
        (*gear)--; *shift_t = ENG_SHIFT_S; g_shift_event = -1;
    }
    if (*shift_t > 0.0f) {           /* clutch window: cut load, revs sag */
        *shift_t -= dt;
        g_engine.load = 0.0f;
        g_engine.target_rpm += (ENG_IDLE_RPM - g_engine.target_rpm) * 0.18f;
        return;
    }
    float rpm;
    if (*gear == 0)            /* neutral: free-revving on the throttle */
        rpm = throttle > 0.0f ? ENG_REDLINE_RPM : ENG_IDLE_RPM;
    else {                     /* R runs on 1st's ratio */
        rpm = eng_gear_rpm(speed_frac, *gear < 0 ? 1 : *gear);
        /* 1st / R off the line: the clutch slips, revs rise with the throttle */
        if (*gear <= 1 && throttle > 0.0f && rpm < 4000.0f) rpm = 4000.0f;
    }
    if (rpm >= ENG_REDLINE_RPM - 50.0f && throttle > 0.0f)
        rpm = ENG_REDLINE_RPM - ((limiter_t++ / 4) & 1) * 400.0f;   /* bounce */
    if (rpm > ENG_REDLINE_RPM) rpm = ENG_REDLINE_RPM;
    g_engine.target_rpm = rpm;
    g_engine.load = throttle > 0.0f ? 1.0f : 0.0f;
}

/* per-band harmonic recipe: {multiple of the fundamental, amplitude} pairs.
 * low = lumpy idle (sub + fundamental), mid = growl, high = scream. */
typedef struct { float mult, amp; } Harm;
static const Harm ENG_HARM[3][4] = {
    { {0.5f,0.35f}, {1,0.80f}, {2,0.25f}, {3,0.10f} },
    { {1,0.45f},    {2,0.65f}, {3,0.40f}, {4,0.20f} },
    { {2,0.35f},    {3,0.55f}, {4,0.45f}, {6,0.30f} },
};

static unsigned g_rng = 22222;
static float frand(void) { g_rng = g_rng*1664525u + 1013904223u; return (int)(g_rng>>9)/4194304.0f - 1.0f; }

/* auxiliary acoustics (audio-thread state): turbo whine, blow-off valve,
 * overrun exhaust pops */
typedef struct {
    float turbo_phase;
    float hiload;            /* recent-max load, ~150ms decay */
    float bov_env, bov_hp;   /* blow-off hiss envelope + highpass state */
    float pop_env, pop_lp;   /* backfire envelope + lowpass (thump) state */
    float pop_timer;
    float boost, hiboost, whine_pos;   /* bank whine: smoothed pressure, recent peak */
    float road_pos, wind_pos[3];
    float nos_vol, nos_pos; int nos_was;
    float purge_vol, purge_pos; int purge_was;
} AuxAudio;
static AuxAudio g_aux;

/* ---- Ginsu sample bank (.abk) loading ----
 * ABKC header: bank offset u32 @+0x18, bank size u32 @+0x24. The bank ("BNKl")
 * holds PT ("platform") headers: TLV of (tag u8, len u8, value big-endian) —
 * 0x84 sample rate, 0x85 sample count, 0x88 data offset (bank-relative).
 * Stream data = EA-XA mono ADPCM: 15-byte frames (header: coef index high
 * nibble, shift low nibble; 28 nibbles) mixed with 0xEE-marked frames =
 * predictor reseed (2x s16be @+1) + 28 uncompressed s16be samples @+5.
 * All of this was derived from the data and cross-validated: decode is
 * exact at every resync frame and loops come out ~perfectly periodic. */

static const int EAXA_C1[4] = { 0, 240, 460, 392 };
static const int EAXA_C2[4] = { 0, 0, -208, -220 };
static int s16be(const unsigned char *p) { int v = (p[0] << 8) | p[1]; return v > 32767 ? v - 65536 : v; }

static void eaxa_decode(const unsigned char *d, int16_t *out, uint32_t nsamp) {
    int prev = 0, prev2 = 0; uint32_t n = 0;
    while (n < nsamp) {
        if (d[0] == 0xEE) {                       /* uncompressed + reseed */
            uint32_t take = nsamp - n < 28 ? nsamp - n : 28;
            for (uint32_t k = 0; k < take; k++) out[n+k] = (int16_t)s16be(d + 5 + 2*k);
            n += take;
            prev = s16be(d + 1); prev2 = s16be(d + 3);
            d += 61;
        } else {
            int ci = d[0] >> 4, shift = 20 - (d[0] & 15);
            for (int b = 1; b < 15 && n < nsamp; b++) {
                for (int hi = 1; hi >= 0 && n < nsamp; hi--) {
                    int nib = hi ? d[b] >> 4 : d[b] & 15;
                    if (nib > 7) nib -= 16;
                    /* same signed-nibble shift UB as xas_decode: multiply by
                       2^shift instead. shift is 5..20 here, so 1<<shift and
                       the product (|nib| <= 8) both stay well inside int. */
                    int s = ((nib * (1 << shift)) + prev*EAXA_C1[ci] + prev2*EAXA_C2[ci] + 0x80) >> 8;
                    if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                    prev2 = prev; prev = s;
                    out[n++] = (int16_t)s;
                }
            }
            d += 15;
        }
    }
}

/* fundamental of a loop by normalized autocorrelation (halving refinement
 * avoids locking onto a multiple of the true period) */
static float loop_base_freq(const int16_t *s, uint32_t n, float rate) {
    double mean = 0; for (uint32_t i = 0; i < n; i++) mean += s[i]; mean /= n;
    int best_lag = 0; double best_r = 0;
    for (int lag = 40; lag < 1500 && (uint32_t)lag < n/2; lag += 2) {
        double num = 0, den = 0;
        for (uint32_t k = 0; k + lag < n; k += 8) {
            double a = s[k] - mean, b = s[k+lag] - mean;
            num += a * b; den += a * a;
        }
        double r = den > 0 ? num / den : 0;
        if (r > best_r) { best_r = r; best_lag = lag; }
    }
    if (!best_lag) return 0;
    for (;;) {   /* prefer the fundamental over its multiples */
        int half = best_lag / 2;
        if (half < 40) break;
        double num = 0, den = 0;
        for (uint32_t k = 0; k + half < n; k += 8) {
            double a = s[k] - mean, b = s[k+half] - mean;
            num += a * b; den += a * a;
        }
        if (den <= 0 || num/den < 0.9 * best_r) break;
        best_lag = half;
    }
    return rate / (float)best_lag;
}

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24; }

/* ---- Gnsu20 native sweep loading (SOUND/ENGINE/GIN_*.gin) ----
 * Header: "Gnsu20", f32 rpm_min/rpm_max @+0x08, u32 n1/n2/total/rate @+0x10.
 * Then (n1+1) u32 sample positions at evenly spaced RPMs (the rpm->position
 * curve; descending for decel recordings), (n2+1) u32 cycle-aligned grain
 * points, then EA-XAS v0 audio: 0x13-byte frames of 32 samples. Frame
 * header u32 LE packs coef index (bits 0-3), hist2 (bits 4-15, as
 * s16 & 0xFFF0), shift (16-19), hist1 (20-31, as s16 & 0xFFF0); both
 * history samples are OUTPUT, so every frame decodes independently.
 * Validated: local fundamental at each rpm marker == rpm/60 Hz.
 * (Frame layout confirmed against vgmstream's ea_xas_decoder.c, used as a
 * format reference like Nikki/OpenNFSTools — no code copied.) */

static void xas_decode(const unsigned char *d, int16_t *out, uint32_t nsamp) {
    uint32_t n = 0;
    while (n < nsamp) {
        uint32_t h = rd32(d);
        int ci = h & 15, shift = (h >> 16) & 15;
        float p2 = (int16_t)(h & 0xFFF0), p1 = (int16_t)((h >> 16) & 0xFFF0);
        out[n++] = (int16_t)p2;
        if (n < nsamp) out[n++] = (int16_t)p1;
        static const float XC1[4] = { 0.0f, 0.9375f, 1.796875f, 1.53125f };
        static const float XC2[4] = { 0.0f, 0.0f, -0.8125f, -0.859375f };
        for (int b = 4; b < 19 && n < nsamp; b++) {
            for (int hi = 1; hi >= 0 && n < nsamp; hi--) {
                int nib = hi ? d[b] >> 4 : d[b] & 15;
                if (nib > 7) nib -= 16;
                /* nib is signed (-8..7): left-shifting it is UB in C even
                   though every real target just sign-extends. Multiplying
                   by 2^12 first gives the identical bit pattern without the
                   UB; the right shift that follows stays on an already-
                   nonnegative-shifted value's implementation-defined (not
                   undefined) arithmetic shift, same as before. */
                float s = (float)((nib * 4096) >> shift) + p1*XC1[ci] + p2*XC2[ci];
                if (s > 32767.0f) s = 32767.0f; if (s < -32768.0f) s = -32768.0f;
                p2 = p1; p1 = s;
                out[n++] = (int16_t)s;
            }
        }
        d += 0x13;
    }
}

static void free_sweep(GinsuSweep *s) {
    free(s->samples); free(s->rpm_pos); free(s->grain);
    memset(s, 0, sizeof *s);
}

static int gin_load(const char *path, GinsuSweep *s) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return 0; }
    fclose(f);
    if (len < 0x20 || memcmp(d, "Gnsu20", 6) != 0) { free(d); return 0; }
    memcpy(&s->rpm_min, d + 8, 4); memcpy(&s->rpm_max, d + 12, 4);
    s->n_rpm = (int)rd32(d + 0x10); s->n_grain = (int)rd32(d + 0x14);
    s->total_samples = rd32(d + 0x18); s->sample_rate = rd32(d + 0x1c);
    long data0 = 0x20 + 4L*(s->n_rpm + 1) + 4L*(s->n_grain + 1);
    long nfr = (long)((s->total_samples + 31) / 32);
    if (s->n_rpm < 2 || s->n_grain < 2 || s->total_samples < 4096 ||
        data0 + nfr*0x13 > len) { free(d); return 0; }
    s->rpm_pos = (uint32_t *)malloc(4u*(s->n_rpm + 1));
    s->grain   = (uint32_t *)malloc(4u*(s->n_grain + 1));
    s->samples = (int16_t *)malloc((size_t)s->total_samples * 2);
    if (!s->rpm_pos || !s->grain || !s->samples) { free_sweep(s); free(d); return 0; }
    for (int i = 0; i <= s->n_rpm; i++)   s->rpm_pos[i] = rd32(d + 0x20 + 4*i);
    for (int i = 0; i <= s->n_grain; i++) s->grain[i]   = rd32(d + 0x20 + 4*(s->n_rpm+1) + 4*i);
    xas_decode(d + data0, s->samples, s->total_samples);
    free(d);
    return 1;
}

/* rpm at a sample position, from the sweep's marker curve (works for the
 * descending decel curves too) */
static float gin_rpm_at(const GinsuSweep *s, float pos) {
    float step = (s->rpm_max - s->rpm_min) / (float)s->n_rpm;
    for (int i = 0; i < s->n_rpm; i++) {
        float a = (float)s->rpm_pos[i], b = (float)s->rpm_pos[i+1];
        if ((pos >= a && pos <= b) || (pos >= b && pos <= a)) {
            float t = (b != a) ? (pos - a) / (b - a) : 0.0f;
            return s->rpm_min + ((float)i + t) * step;
        }
    }
    return pos <= (float)s->rpm_pos[0] ? s->rpm_min : s->rpm_max;
}

/* sample position for an rpm (clamped to the sweep's range) */
static float gin_pos_at(const GinsuSweep *s, float rpm) {
    float t = (rpm - s->rpm_min) / (s->rpm_max - s->rpm_min);
    if (t < 0) t = 0; if (t > 1) t = 1;
    float fi = t * (float)s->n_rpm;
    int i = (int)fi; if (i >= s->n_rpm) i = s->n_rpm - 1;
    return (float)s->rpm_pos[i] + (fi - (float)i) *
           ((float)s->rpm_pos[i+1] - (float)s->rpm_pos[i]);
}

/* Per-car audio rows, from SPEED2.EXE: fn 0x459240 maps the car type (GLOBALB
 * car-table order) to a row; the table at 0x7f2230 holds 12-byte records
 * {engine slot, ?, turbo bank} at row*4 + upgrade level. Cars outside the
 * switch (LANCEREVO8, and the folders UG2 has no car type for) take its
 * default row 18. Per row at stock (level 0): engine slot, shift bank
 * (record +4, indexes the gear-bank table at 0x7f2860) and turbo bank
 * (k_row_turbo, further down). */
static const struct { const char *car; unsigned char row; } k_audio_row[] = {
    { "PEUGOT", 22 }, { "FOCUS", 10 }, { "COROLLA", 6 }, { "240SX", 0 },
    { "MIATA", 19 }, { "CIVIC", 5 }, { "PEUGOT106", 23 }, { "CORSA", 7 },
    { "HUMMER", 14 }, { "NAVIGATOR", 21 }, { "ESCALADE", 9 }, { "TIBURON", 31 },
    { "SENTRA", 28 }, { "CELICA", 4 }, { "IS300", 17 }, { "SUPRA", 30 },
    { "GOLF", 12 }, { "A3", 3 }, { "RSX", 24 }, { "ECLIPSE", 8 }, { "TT", 32 },
    { "RX8", 26 }, { "350Z", 2 }, { "G35", 11 }, { "3000GT", 1 }, { "GTO", 13 },
    { "MUSTANGGT", 20 }, { "SKYLINE", 29 }, { "RX7", 25 }, { "IMPREZAWRX", 15 },
};
static int car_audio_row(const char *carname) {
    for (int i = 0; i < (int)(sizeof k_audio_row / sizeof *k_audio_row); i++)
        if (!strcmp(carname, k_audio_row[i].car)) return k_audio_row[i].row;
    return 18;
}
static const unsigned char k_row_engine[33] = {
    18, 6, 25, 29, 22, 22, 18, 31, 6, 39, 31, 25, 29, 14, 38, 15, 4, 26, 22, 23, 14, 39, 23, 23, 4, 10, 10, 4, 33, 9, 26, 28, 28 };
static const unsigned char k_row_gear[33] = {
    0, 4, 5, 0, 0, 0, 0, 0, 4, 12, 0, 4, 0, 8, 12, 1, 0, 4, 0, 0, 8, 12, 0, 0, 0, 1, 1, 1, 0, 5, 5, 4, 5 };
/* Engine slots (0x7f0de0, 0x4c each): the Gnsu20 sweep and the decel sweep
 * (record +0x10 flag, +0x14 index into the decel table at 0x7f1aa8; flag 0
 * = no decel recording, the accel sweep plays on the overrun too). */
static const struct { const char *gin, *dcl; } k_engine_slot[41] = {
    { "GIN_HondaS2000_Church.gin", "GIN_S2000_Church_DCL.gin" },       /* 0 */
    { "GIN_Ferrari_360.gin", "GIN_Ferrari_360_DCL.gin" },              /* 1 */
    { "GIN_Trueno_AE86.gin", "GIN_Trueno_DCL.gin" },                   /* 2 */
    { "GIN_Honda_S2000_5zigen.gin", "GIN_Honda_S2000_5zigen_DCL.gin" }, /* 3 */
    { "GIN_Honda_S2000_Spugen.gin", "GIN_Honda_S2000_Spugen_DCL.gin" }, /* 4 */
    { "GIN_Honda_S2000_spliced.gin", "GIN_Honda_S2000_2f2f_DCL.gin" }, /* 5 */
    { "GIN_MitsEclipse_2000.gin", "GIN_Eclipse_2000_DCL.gin" },        /* 6 */
    { "GIN_Nissan_300ZX_Street.gin", "GIN_300ZX_DCL.gin" },            /* 7 */
    { "GIN_Eclipse_5zigen.gin", "GIN_Mits_5zigen_DCL.gin" },           /* 8 */
    { "GIN_Nissan_Skyline.gin", NULL },                                /* 9 */
    { "GIN_Mazda_RX7.gin", "GIN_Mazda_RX7_DCL.gin" },                  /* 10 */
    { "GIN_RX8_Ramp_v1_CD.gin", "GIN_Mazda_RX8_DCL.gin" },             /* 11 */
    { "GIN_Mustang_Drag.gin", "GIN_Mustang_Drag_DCL.gin" },            /* 12 */
    { "GIN_Bronco_Comp.gin", "GIN_Bronco_DCL.gin" },                   /* 13 */
    { "GIN_MustSal_Ramp_V2_CD.gin", "GIN_Mustang_Saleen_DCL.gin" },    /* 14 */
    { "GIN_WRXSTi_Pull_09.gin", "GIN_WRX_STi_DCL.gin" },               /* 15 */
    { "GIN_WRX_Street.gin", "GIN_WRX_Street_DCL.gin" },                /* 16 */
    { "GIN_WRX_2F2F.gin", "GIN_WRX2F2F_DCL.gin" },                     /* 17 */
    { "GIN_Nissan_240SX.gin", "GIN_240SX_Decel.gin" },                 /* 18 */
    { "GIN_Toyota_GTS.gin", "GIN_Toyota_GTS_DCL.gin" },                /* 19 */
    { "GIN_Honda_Prelude.gin", "GIN_Honda_Prelude_DCL.gin" },          /* 20 */
    { "GIN_VW_Scirocco.gin", "GIN_VW_Scirocco_DCL.gin" },              /* 21 */
    { "GIN_Honda_S2000_APEX.gin", "GIN_HONDA_S2000_Apex_DCL.gin" },    /* 22 */
    { "GIN_Mazda_Miata.gin", "GIN_Mazda_Miata_DCL.gin" },              /* 23 */
    { "GIN_VW_Drag_GTi.gin", "GIN_VW_Drag_GTi_DCL.gin" },              /* 24 */
    { "GIN_G35_350ZX_Comp_v2_AB.gin", "GIN_Infinit_G35_DCL.gin" },     /* 25 */
    { "GIN_350Z_01d_shrt2.gin", "GIN_Infinit_G35_DCL.gin" },           /* 26 */
    { "GIN_350Z_Jason_shrt2.gin", "GIN_Nissan_350ZX_Jason_DCL.gin" },  /* 27 */
    { "GIN_VR6_Jetta_M3.gin", "GIN_VW_Jetta_M3_DCL.gin" },             /* 28 */
    { "GIN_VW_Cabriolet.gin", "GIN_VW_Cab_DCL.gin" },                  /* 29 */
    { "GIN_VR6_Jetta_M4.gin", "GIN_VW_Jetta_M4_DCL.gin" },             /* 30 */
    { "GIN_FordFocus_Road.gin", "GIN_Ford_Focus2_DCL.gin" },           /* 31 */
    { "GIN_FordFocus_comp2.gin", "GIN_Ford_Focus2_DCL.gin" },          /* 32 */
    { "GIN_Protege_Ramp_V2_CD.gin", "GIN_Protege_Lev2_DCL.gin" },      /* 33 */
    { "GIN_Protege_Ramp_Lev2_CD.gin", "GIN_Protege_Lev2_DCL.gin" },    /* 34 */
    { "GIN_Mustang_Nscr_COMP.gin", "GIN_Nascar_DCL.gin" },             /* 35 */
    { "GIN_Cavel_v1_Base.gin", "GIN_Cavel_DCL.gin" },                  /* 36 */
    { "GIN_Cavel_v1_Lev1.gin", "GIN_Cavel_DCL.gin" },                  /* 37 */
    { "GIN_Hummer.gin", NULL },                                        /* 38 */
    { "GIN_Porsche_Cayenne.gin", "GIN_Porsche_Cayenne_DCL.gin" },      /* 39 */
    { "GIN_Acura_ITR.gin", "GIN_Acura_ITR_DCL.gin" },                  /* 40 */
};
static const char *const k_gear_bank[16] = {
    "GEAR_SML_Base", "GEAR_SML_Lev1", "GEAR_SML_Lev2", "GEAR_SML_Lev3",
    "GEAR_MED_Base", "GEAR_MED_Lev1", "GEAR_MED_Lev2", "GEAR_MED_Lev3",
    "GEAR_LRG_Base", "GEAR_LRG_Lev1", "GEAR_LRG_Lev2", "GEAR_LRG_Lev3",
    "GEAR_TK_Base",  "GEAR_TK_Lev1",  "GEAR_TK_Lev2",  "GEAR_TK_Lev3" };

int audio_load_ginsu_sweeps(const char *dataroot, const char *carname) {
    Ginsu20State *g = &g_engine.gin;
    if (g->has_gin_data) {
        free_sweep(&g->accel_sweep);
        if (!g->decel_is_alias) free_sweep(&g->decel_sweep);
        memset(g, 0, sizeof *g);
    }
    char dirp[512], up_car[64], up_f[256];
    snprintf(dirp, sizeof dirp, "%s/SOUND/ENGINE", dataroot);
    {   /* the game's own choice: car row -> stock engine slot */
        int slot = k_row_engine[car_audio_row(carname)];
        char path[768];
        snprintf(path, sizeof path, "%s/%s", dirp, k_engine_slot[slot].gin);
        if (gin_load(path, &g->accel_sweep)) {
            int ok = 0;
            if (k_engine_slot[slot].dcl) {
                snprintf(path, sizeof path, "%s/%s", dirp, k_engine_slot[slot].dcl);
                ok = gin_load(path, &g->decel_sweep);
            }
            if (!ok) { g->decel_sweep = g->accel_sweep; g->decel_is_alias = 1; }
            printf("engine sounds: slot %d %s + %s\n", slot, k_engine_slot[slot].gin,
                   ok ? k_engine_slot[slot].dcl : "(no decel)");
            goto loaded;
        }
    }
    size_t cl = strlen(carname);
    if (cl >= sizeof up_car) return 0;
    for (size_t i = 0; i <= cl; i++) up_car[i] = (char)toupper((unsigned char)carname[i]);
    DIR *dir = opendir(dirp);
    if (!dir) return 0;
    char best[2][256] = { "", "" };            /* [0]=accel, [1]=decel */
    struct dirent *e;
    while ((e = readdir(dir))) {
        size_t fl = strlen(e->d_name);
        if (fl < 5 || fl >= sizeof up_f) continue;
        for (size_t i = 0; i <= fl; i++) up_f[i] = (char)toupper((unsigned char)e->d_name[i]);
        if (strncmp(up_f, "GIN_", 4) != 0 || strcmp(up_f + fl - 4, ".GIN") != 0) continue;
        if (!strstr(up_f, up_car)) continue;
        int dcl = strstr(up_f, "DCL") || strstr(up_f, "DECEL") ? 1 : 0;
        if (!best[dcl][0] || strlen(e->d_name) < strlen(best[dcl]))
            snprintf(best[dcl], sizeof best[dcl], "%s", e->d_name);
    }
    closedir(dir);
    if (!best[0][0]) return 0;
    char path[768];
    snprintf(path, sizeof path, "%s/%s", dirp, best[0]);
    if (!gin_load(path, &g->accel_sweep)) return 0;
    int n = 1;
    if (best[1][0]) {
        snprintf(path, sizeof path, "%s/%s", dirp, best[1]);
        if (gin_load(path, &g->decel_sweep)) n = 2;
    }
    if (n == 1) { g->decel_sweep = g->accel_sweep; g->decel_is_alias = 1; }
loaded:
    for (int a = 0; a < 2; a++) {   /* same indexing as the callback: 1=accel */
        GinsuSweep *sw = a ? &g->accel_sweep : &g->decel_sweep;
        g->play_cursor[a] = (float)sw->grain[0];
        g->grain_rpm[a] = gin_rpm_at(sw,
            0.5f*((float)sw->grain[0] + (float)sw->grain[1]));
        if (g->grain_rpm[a] < 1.0f) g->grain_rpm[a] = sw->rpm_min;
    }
    g->has_gin_data = 1;
    return g->decel_is_alias ? 1 : 2;
}

#define ENG_NBANKS 41   /* SOUND/ENGINE/CAR_00..CAR_40 in the retail data */

int audio_bank_for_car(const char *carname) {
    unsigned h = 5381;   /* djb2 of the car folder name -> stable per-car voice */
    for (const char *c = carname; *c; c++) h = h*33u + (unsigned char)*c;
    return (int)(h % ENG_NBANKS);
}

static void free_band(AudioSampleBand *sb) {
    free(sb->samples); memset(sb, 0, sizeof *sb);
}

int audio_load_engine_bank(const char *dataroot, int carbank) {
    for (int b = 0; b < 3; b++) { free_band(&g_engine.accel_bands[b]); free_band(&g_engine.decel_bands[b]); }
    free_band(&g_engine.idle_band);
    g_engine.sample_mode = 0;
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/ENGINE/CAR_%02d_ENG_MB_SPU.abk", dataroot, carbank);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return 0; }
    fclose(f);
    if (len < 0x40 || memcmp(d, "ABKC", 4) != 0) { free(d); return 0; }
    long boff = rd32(d + 0x18), bsize = rd32(d + 0x24);
    if (boff + bsize > len || memcmp(d + boff, "BNKl", 4) != 0) { free(d); return 0; }
    /* collect the PT headers from the bank TOC */
    struct { uint32_t off, count, rate; } st[16]; int nst = 0;
    for (long p = boff; p < boff + 0x800 - 4 && nst < 16; p++) {
        if (memcmp(d + p, "PT\0\0", 4) != 0) continue;
        uint32_t off = 0, count = 0, rate = 22050;
        long q = p + 4;
        while (q < p + 0x30) {
            int tag = d[q++];
            if (tag == 0xFF || tag == 0xFE) break;
            if (tag == 0xFD) continue;
            int nlen = d[q++]; uint32_t v = 0;
            for (int k = 0; k < nlen; k++) v = v << 8 | d[q++];
            if (tag == 0x84) rate = v;
            else if (tag == 0x85) count = v;
            else if (tag == 0x88) off = v;
        }
        if (count && off) { st[nst].off = off; st[nst].count = count; st[nst].rate = rate; nst++; }
        p += 0x20;
    }
    /* Ginsu engine bank layout: [0]=idle, [1..3]=accel L/M/H, [4..6]=decel
       L/M/H, [7]=high/whine. ponytail: idle+whine unused — fold them in when
       the 3+3 blend feels thin at the extremes. */
    if (nst < 7) { free(d); return 0; }
    int loaded = 0;
    for (int b = 0; b < 7; b++) {
        /* stream 0 = idle, 1..3 = accel L/M/H, 4..6 = decel L/M/H */
        AudioSampleBand *sb = b == 0 ? &g_engine.idle_band
                            : b < 4 ? &g_engine.accel_bands[b-1]
                                    : &g_engine.decel_bands[b-4];
        uint32_t si = (uint32_t)b;
        if (boff + st[si].off >= len) continue;
        sb->samples = (int16_t *)malloc(st[si].count * sizeof(int16_t));
        if (!sb->samples) continue;
        eaxa_decode(d + boff + st[si].off, sb->samples, st[si].count);
        sb->sample_count = st[si].count;
        sb->srate = (float)st[si].rate;
        sb->base_freq = loop_base_freq(sb->samples, sb->sample_count, sb->srate);
        sb->playback_index = 0; sb->volume = 0;
        loaded++;
    }
    free(d);
    g_engine.sample_mode = (loaded == 7);
    return g_engine.sample_mode ? loaded : 0;
}

/* One decoded bank sample (EA-XA, see eaxa_decode). loop_end > 0 marks an
 * authored loop (PT tags 0x86/0x87). */
typedef struct { int16_t *s; uint32_t n, loop_start, loop_end; float rate; } Clip;
static Clip g_shift_clip[2], g_skid_clip, g_skidk[4];   /* g_skidk = FX_SKID clips 1..4 */
static float g_skidk_pos[4];
static float g_shift_pos = -1.0f, g_skid_pos;
static int g_shift_which;

static void clip_free(Clip *c) { free(c->s); memset(c, 0, sizeof *c); }

/* Decode samples want[0..nwant) of an .abk bank into out[]. Same PT tag
 * reader as audio_load_engine_bank; entries without a data offset are false
 * "PT" matches inside audio data and are skipped. The TOC scan stops at the
 * first sample's data (MB_Stich_Collision's 94-entry TOC runs to +0xeb0). */
static int abk_clips(const char *path, const int *want, int nwant, Clip *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return 0; }
    fclose(f);
    int loaded = 0, found = 0;
    long boff = len >= 0x40 && !memcmp(d, "ABKC", 4) ? (long)rd32(d + 0x18) : -1;
    long bsize = boff >= 0 ? (long)rd32(d + 0x24) : 0;
    if (boff < 0 || boff + bsize > len || memcmp(d + boff, "BNKl", 4)) { free(d); return 0; }
    long data0 = bsize;
    for (long p = boff; p < boff + bsize - 4 && p - boff < data0; p++) {
        if (memcmp(d + p, "PT\0\0", 4) != 0) continue;
        uint32_t off = 0, count = 0, rate = 22050, ls = 0, le = 0;
        long q = p + 4;
        while (q < p + 0x40) {
            int tag = d[q++];
            if (tag == 0xFF || tag == 0xFE) break;
            if (tag == 0xFD) continue;
            int nlen = d[q++]; uint32_t v = 0;
            for (int k = 0; k < nlen && k < 4; k++) v = v << 8 | d[q + k];
            q += nlen;
            if (tag == 0x84) rate = v; else if (tag == 0x85) count = v;
            else if (tag == 0x88) off = v; else if (tag == 0x86) ls = v;
            else if (tag == 0x87) le = v;
        }
        p += 3;
        if (!count || !off || boff + (long)off >= len) continue;
        if ((long)off < data0) data0 = off;
        int idx = found++;
        for (int w = 0; w < nwant; w++) {
            if (want[w] != idx || out[w].s) continue;
            out[w].s = (int16_t *)malloc(count * sizeof(int16_t));
            if (!out[w].s) continue;
            eaxa_decode(d + boff + off, out[w].s, count);
            out[w].n = count; out[w].rate = (float)rate;
            out[w].loop_start = ls < count ? ls : 0;
            out[w].loop_end = le > ls && le < count ? le : 0;
            loaded++;
        }
    }
    free(d);
    return loaded;
}

static int abk_clip(const char *path, int want, Clip *out) {
    return abk_clips(path, &want, 1, out);
}

int audio_load_shift_bank(const char *dataroot, const char *carname) {
    const char *bank = k_gear_bank[k_row_gear[car_audio_row(carname)]];
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/SHIFTING/%s.abk", dataroot, bank);
    clip_free(&g_shift_clip[0]); clip_free(&g_shift_clip[1]);
    g_shift_pos = -1.0f;
    int n = abk_clip(path, 0, &g_shift_clip[0]) + abk_clip(path, 1, &g_shift_clip[1]);
    printf("shift sounds: %s, %d clips (%.2f / %.2f s)\n", bank, n,
           g_shift_clip[0].n / (g_shift_clip[0].rate > 0 ? g_shift_clip[0].rate : 1.0f),
           g_shift_clip[1].n / (g_shift_clip[1].rate > 0 ? g_shift_clip[1].rate : 1.0f));
    return n > 0;
}

int audio_load_skid_bank(const char *dataroot) {
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/SKIDS/SKID_PAV_MB.abk", dataroot);
    clip_free(&g_skid_clip);
    int ok = abk_clip(path, 0, &g_skid_clip);
    /* FX_SKID's sample players use clips 4,3,2,4,1,4: three tonal squeals
       (1-3) each with the broadband rubber clip 4 (clip 0 is not used) */
    {   static const int want[4] = { 1, 2, 3, 4 };
        for (int k = 0; k < 4; k++) clip_free(&g_skidk[k]);
        int nk = abk_clips(path, want, 4, g_skidk);
        printf("skid sound: FX_SKID clips 1-4 %s\n", nk == 4 ? "loaded" : "missing"); }
    printf("skid sound: %s (%.2f s, loop %u-%u)\n", ok ? "SKID_PAV_MB" : "synth",
           ok ? g_skid_clip.n / g_skid_clip.rate : 0.0f, g_skid_clip.loop_start, g_skid_clip.loop_end);
    return ok;
}

/* Collision clips: [0..5] heavy crunches (dull, 0.4-0.7 s, peak at onset),
 * [6..11] light knocks (0.1-0.3 s). Bank indices from decoding all 94 and
 * measuring length, zero-crossing rate and attack; the bright ones (5-7 kHz
 * crossings) are glass/hiss and are left out. */
#define N_COLL 12
static const int k_coll_ids[N_COLL] = { 4, 31, 47, 55, 74, 85,  0, 5, 34, 48, 63, 64 };
static Clip g_coll[N_COLL];
static volatile float g_impact_req;      /* main thread: strongest pending hit */
static int g_impact_used;                /* audio_impact ever called */
static int g_coll_cur = -1;
static float g_coll_pos, g_coll_vol, g_coll_age;

int audio_load_collision_bank(const char *dataroot) {
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/MB_Stich_Collision.abk", dataroot);
    for (int i = 0; i < N_COLL; i++) clip_free(&g_coll[i]);
    int n = abk_clips(path, k_coll_ids, N_COLL, g_coll);
    printf("collision sounds: %d of %d clips\n", n, N_COLL);
    return n;
}

static const unsigned char k_row_turbo[33] = {
    0, 6, 6, 3, 3, 0, 0, 0, 6, 12, 0, 6, 0, 12, 12, 6, 3,
    6, 3, 0, 12, 12, 3, 0, 0, 6, 6, 3, 0, 6, 6, 6, 6 };
/* SPEED2.EXE 0x7f3338: 24-byte entries {bank, whine rate at no boost, at
 * full boost, third rate (unused here), 40.0f, 0.5f}. Read here as
 * playback Hz for the 22050 Hz whine loop (our reading; not traced in code). */
/* 0x7f3338 records: {bank name, whine volume +4, blow-off volume +8}
   (carsfx.c k_turbo_rec has the volumes it plays with) */
static const struct { const char *bank; float whine_vol, bov_vol; } k_turbo_bank[15] = {
    { "TURBO_SML1_0", 6500, 24500 }, { "TURBO_SML1_1", 6500, 24000 }, { "TURBO_SML1_2", 8500, 22500 },
    { "TURBO_SML2_0", 6500, 22000 }, { "TURBO_SML2_1", 7500, 22000 }, { "TURBO_SML2_2", 6000, 18500 },
    { "TURBO_MED_0",  6500, 24000 }, { "TURBO_MED_1",  9500, 24500 }, { "TURBO_MED_2",  8000, 24500 },
    { "TURBO_BIG_0",  6000, 20000 }, { "TURBO_BIG_1",  6000, 18000 }, { "TURBO_BIG_2",  9500, 18000 },
    { "TURBO_TRUCK_0",8500, 16500 }, { "TURBO_TRUCK_1",10500,24000 }, { "TURBO_TRUCK_2",7250, 18000 },
};
static float clip_at(const Clip *c, float pos);

/* a looping clip: authored loop points when present, else the whole clip */
static float loop_next(const Clip *c, float *pos, float step) {
    if (!c->n) return 0.0f;
    uint32_t ls = c->loop_end ? c->loop_start : 0, le = c->loop_end ? c->loop_end : c->n - 1;
    if (*pos >= (float)le || *pos < 0.0f) *pos = (float)ls;
    float v = clip_at(c, *pos);
    *pos += step;
    if (*pos >= (float)le) *pos -= (float)(le - ls);
    return v;
}

/* a one-shot voice; c = NULL when idle */
typedef struct { const Clip *c; float pos, vol; } Voice;
static float voice_next(Voice *v) {
    if (!v->c) return 0.0f;
    float s = clip_at(v->c, v->pos) * v->vol;
    v->pos += v->c->rate / 44100.0f;
    if (v->pos >= (float)v->c->n - 1.0f) v->c = NULL;
    return s;
}
static Voice g_bov, g_nos_voice, g_purge_voice;

static Clip g_turbo_whine, g_bov_clip[3];
static int g_pturbo_rec = -1;
int audio_player_turbo_rec(void) { return g_pturbo_rec; }

int audio_load_turbo_bank(const char *dataroot, const char *carname) {
    int row = car_audio_row(carname);
    int t = k_row_turbo[row];
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/TURBO/%s_MB.abk", dataroot, k_turbo_bank[t].bank);
    g_bov.c = NULL;   /* under audio_hold: nothing is mixing */
    clip_free(&g_turbo_whine);
    for (int i = 0; i < 3; i++) clip_free(&g_bov_clip[i]);
    static const int want[4] = { 0, 1, 2, 3 };
    Clip c[4]; memset(c, 0, sizeof c);
    int n = abk_clips(path, want, 4, c);
    g_turbo_whine = c[0];
    for (int i = 0; i < 3; i++) g_bov_clip[i] = c[i + 1];
    g_pturbo_rec = n ? t : -1;
    printf("turbo sounds: %s (row %d), %d clips\n", k_turbo_bank[t].bank, row, n);
    return n;
}

/* Road noise / wind / nitrous: the clips each game event references. An
 * ABK carries its events as compiled EA patch modules (export table at
 * header +0x38 -> descriptor +8/+0xc = state template); sample-player nodes
 * in a template hold "00 00 03 00 ff 00 00 00" then the clip index.
 *   FX_ROADNOISE -> clip 0; FX_WIND -> clips 0, 1, 2 (light .. strong);
 *   FX_NITROUS -> clips 0, 1, 2 (by envelope: 1 engage, 0 burn loop,
 *   2 release); FX_PURGE ("SND: NitrousPurge" 0x45f2d0, the nitrous
 *   held at a standstill, 0x5ecdb0) the same way: 4 engage, 3 loop,
 *   5 release. */
static Clip g_road_clip[1], g_wind_clip[3], g_nos_clip[3], g_purge_clip[3];

/* ---- front-end sounds (SoundMgr 0x458520): an id < 0x17 plays that
   sample of GLOBAL/FE_COMMON_MB.abk (the FE_Common object, PlayCommonSample),
   0x17..0x5d plays FE/FE_MB.abk sample id - 0x17 (PlayFrontEndSample).
   FE_COMMON is loaded whole; FE_MB samples on first use. */
#define FE_COMMON_N 0x17
#define FE_MB_N (0x5e - 0x17)
static Clip g_fecom[FE_COMMON_N], g_femb[FE_MB_N];
static char g_femb_path[512];
#define FE_VOICES 4
static struct { const Clip *volatile c; float pos; } g_fev[FE_VOICES];
static volatile int g_game_mute;

int audio_load_fe_banks(const char *dataroot) {
    char path[512];
    snprintf(path, sizeof path, "%s/SOUND/GLOBAL/FE_COMMON_MB.abk", dataroot);
    int want[FE_COMMON_N];
    for (int i = 0; i < FE_COMMON_N; i++) want[i] = i;
    int n = abk_clips(path, want, FE_COMMON_N, g_fecom);
    snprintf(g_femb_path, sizeof g_femb_path, "%s/SOUND/FE/FE_MB.abk", dataroot);
    printf("audio: FE_COMMON_MB %d samples\n", n);
    return n;
}

void audio_fe_play(int id) {
    const Clip *c = NULL;
    if (id < 0) return;
    if (id < FE_COMMON_N) c = &g_fecom[id];
    else if (id < 0x5e) {
        Clip *k = &g_femb[id - FE_COMMON_N];
        if (!k->s && g_femb_path[0]) abk_clip(g_femb_path, id - FE_COMMON_N, k);
        c = k;
    }
    if (!c || !c->s || !c->n) return;
    /* a free voice, else the oldest (furthest in) */
    int v = 0;
    for (int i = 0; i < FE_VOICES; i++) {
        if (!g_fev[i].c) { v = i; break; }
        if (g_fev[i].pos > g_fev[v].pos) v = i;
    }
    g_fev[v].c = NULL;
    __sync_synchronize();
    g_fev[v].pos = 0.0f;
    __sync_synchronize();
    g_fev[v].c = c;
}

void audio_pause_game(int on) { g_game_mute = on; }
static volatile float g_lv_master = 1.0f, g_lv_sfx = 1.0f, g_lv_engine = 1.0f, g_lv_tire = 1.0f,
                      g_lv_music = 1.0f;
static volatile int g_lv_stereo = 1;
void audio_set_levels(float master, float sfx, float engine, float tire, float music, int stereo) {
    g_lv_master = master; g_lv_sfx = sfx; g_lv_engine = engine; g_lv_tire = tire;
    g_lv_music = music; g_lv_stereo = stereo;
}

static void fe_voices_mix(float *acc, int n) {
    for (int v = 0; v < FE_VOICES; v++) {
        const Clip *c = g_fev[v].c;
        if (!c) continue;
        float pos = g_fev[v].pos, step = c->rate / 44100.0f;
        for (int i = 0; i < n; i++) {
            if (pos >= (float)c->n - 1.0f) { c = NULL; break; }
            acc[i] += clip_at(c, pos);
            pos += step;
        }
        g_fev[v].pos = pos;
        if (!c) g_fev[v].c = NULL;
    }
}

int audio_load_env_banks(const char *dataroot) {
    char path[512]; int n = 0;
    g_nos_voice.c = NULL; g_purge_voice.c = NULL;
    clip_free(&g_road_clip[0]);
    for (int i = 0; i < 3; i++) { clip_free(&g_wind_clip[i]); clip_free(&g_nos_clip[i]); clip_free(&g_purge_clip[i]); }
    static const int road_ids[1] = { 0 }, wind_ids[3] = { 0, 1, 2 }, nos_ids[3] = { 1, 0, 2 };
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/ROADNOISE_00_MB.abk", dataroot);
    n += abk_clips(path, road_ids, 1, g_road_clip);
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/WIND_00_MB.abk", dataroot);
    n += abk_clips(path, wind_ids, 3, g_wind_clip);
    snprintf(path, sizeof path, "%s/SOUND/NOS/Nitrous_00_MB.abk", dataroot);
    n += abk_clips(path, nos_ids, 3, g_nos_clip);
    {   static const int purge_ids[3] = { 4, 3, 5 };   /* FX_PURGE: 4 engage, 3 loop, 5 release */
        abk_clips(path, purge_ids, 3, g_purge_clip); }
    printf("environment sounds: %d of 7 clips (road %u, wind %u/%u/%u, nitrous %u/%u/%u samples)\n", n,
           g_road_clip[0].n, g_wind_clip[0].n, g_wind_clip[1].n, g_wind_clip[2].n,
           g_nos_clip[0].n, g_nos_clip[1].n, g_nos_clip[2].n);
    return n;
}

/* ---- the other cars' engines (g_carvoice, set by carsfx.c) ---- */
AudioCarVoice g_carvoice[AUDIO_CARVOICES];
int audio_car_row(const char *carname) { return car_audio_row(carname); }
/* an SPU engine bank: [0] idle, [1..3] accel L/M/H, [4..6] decel L/M/H
   (the layout audio_load_engine_bank reads) */
typedef struct { Clip c[7]; int ok; } AiBank;
static AiBank g_aibank[3];
static Clip g_traffic_clip;
/* audio thread: each voice's loop cursors, smoothed gain, filter state */
typedef struct { float pos[7], gain, lp; } CarVoiceState;
static CarVoiceState g_cvs[AUDIO_CARVOICES];

int audio_load_ai_banks(const char *dataroot, const int *slot, int n) {
    static const int want[7] = { 0, 1, 2, 3, 4, 5, 6 };
    int ok = 0;
    for (int b = 0; b < 3; b++) {
        for (int k = 0; k < 7; k++) clip_free(&g_aibank[b].c[k]);
        g_aibank[b].ok = 0;
        if (b >= n || slot[b] < 0) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/SOUND/ENGINE/CAR_%02d_ENG_MB_SPU.abk", dataroot, slot[b]);
        int got = abk_clips(path, want, 7, g_aibank[b].c);
        g_aibank[b].ok = got == 7;
        ok += g_aibank[b].ok;
        printf("car sounds: AI engine bank %d = CAR_%02d_ENG_MB_SPU (%d of 7 loops)\n", b, slot[b], got);
    }
    memset(g_cvs, 0, sizeof g_cvs);
    return ok;
}

int audio_load_traffic_clip(const char *dataroot) {
    char path[512];
    clip_free(&g_traffic_clip);
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/TRAFFIC_MB.abk", dataroot);
    int n = abk_clip(path, 0, &g_traffic_clip);
    printf("car sounds: traffic loop %u samples at %.0f Hz (loop %u..%u)\n", g_traffic_clip.n,
           g_traffic_clip.rate, g_traffic_clip.loop_start, g_traffic_clip.loop_end);
    return n;
}

/* ---- the other cars' sample effects (g_fxvoice, set by carsfx.c) ---- */
AudioFxVoice g_fxvoice[AUDIO_FXVOICES];
static Clip g_env_clip[8], g_woosh_clip[16], g_collall[94], g_scrape_clip;
typedef struct { Clip c[4]; } AiTurboBank;
static AiTurboBank g_aiturbo[8];
/* audio thread: cursor (negative = delay still running), last trig seen,
   smoothed gain */
typedef struct { float pos, gain; unsigned trig; int live; } FxVoiceState;
static FxVoiceState g_fvs[AUDIO_FXVOICES];

int audio_row_turbo(int row) { return row >= 0 && row < 33 ? k_row_turbo[row] : 0; }

int audio_load_carfx_banks(const char *dataroot, const int *coll, int ncoll) {
    char path[512]; int n = 0;
    static const int ids[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int i = 0; i < 8; i++) clip_free(&g_env_clip[i]);
    for (int i = 0; i < 16; i++) clip_free(&g_woosh_clip[i]);
    for (int i = 0; i < 94; i++) clip_free(&g_collall[i]);
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/ENV_COMMON_MB.abK", dataroot);
    int ne = abk_clips(path, ids, 8, g_env_clip);
    if (!ne) {   /* the EXE spells it .abK; case-sensitive file systems */
        snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/ENV_COMMON_MB.abk", dataroot);
        ne = abk_clips(path, ids, 8, g_env_clip);
    }
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/MB_Stitch_Whoosh.abk", dataroot);
    int nw = abk_clips(path, ids, 16, g_woosh_clip);
    int want[94], nwant = 0;
    for (int i = 0; i < ncoll; i++) {
        int dup = 0;
        for (int j = 0; j < nwant; j++) dup |= want[j] == coll[i];
        if (!dup && coll[i] >= 0 && coll[i] < 94) want[nwant++] = coll[i];
    }
    Clip tmp[94]; memset(tmp, 0, sizeof tmp);
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/MB_Stich_Collision.abk", dataroot);
    int nc = abk_clips(path, want, nwant, tmp);
    for (int j = 0; j < nwant; j++) g_collall[want[j]] = tmp[j];
    clip_free(&g_scrape_clip);
    snprintf(path, sizeof path, "%s/SOUND/IG_GLOBAL/FX_Misc_MB.abk", dataroot);
    int ns = abk_clip(path, 1, &g_scrape_clip);           /* the FX_Scrape patch's variant table: bank entry 2 */
    n = ne + nw + nc + ns;
    printf("car effects: ENV_COMMON %d of 8, woosh %d of 16, collision %d of %d clips, scrape %d\n", ne, nw, nc, nwant, ns);
    memset(g_fvs, 0, sizeof g_fvs);
    return n;
}

int audio_load_ai_turbo_banks(const char *dataroot, const int *tbank, int n) {
    static const int want[4] = { 0, 1, 2, 3 };
    int ok = 0;
    for (int b = 0; b < 8; b++) {
        for (int k = 0; k < 4; k++) clip_free(&g_aiturbo[b].c[k]);
        if (b >= n || tbank[b] < 0 || tbank[b] >= 15) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/SOUND/TURBO/%s_MB.abk", dataroot, k_turbo_bank[tbank[b]].bank);
        int got = abk_clips(path, want, 4, g_aiturbo[b].c);
        ok += got == 4;
        printf("car sounds: AI turbo bank %d = %s (%d of 4 clips)\n", b, k_turbo_bank[tbank[b]].bank, got);
    }
    for (int v = 0; v < AUDIO_FXVOICES; v++) g_fvs[v].live = 0;
    return ok;
}

static const Clip *fx_clip(int set, int k) {
    if (k < 0) return NULL;
    switch (set) {
    case AFX_ENV:   return k < 8 ? &g_env_clip[k] : NULL;
    case AFX_WOOSH: return k < 16 ? &g_woosh_clip[k] : NULL;
    case AFX_COLL:  return k < 94 ? &g_collall[k] : NULL;
    case AFX_NOS:   /* g_nos_clip holds clips 1, 0, 2 */
        return k == 0 ? &g_nos_clip[1] : k == 1 ? &g_nos_clip[0] : k == 2 ? &g_nos_clip[2] : NULL;
    case AFX_SKID:  return k == 0 ? &g_skid_clip : k <= 4 ? &g_skidk[k - 1] : NULL;
    case AFX_SCRAPE: return k == 0 ? &g_scrape_clip : NULL;
    case AFX_PTURBO: return k == 0 ? &g_turbo_whine : k <= 3 ? &g_bov_clip[k - 1] : NULL;
    default:
        if (set >= AFX_TURBO && set < AFX_TURBO + 8 && k < 4) return &g_aiturbo[set - AFX_TURBO].c[k];
        return NULL;
    }
}

/* one buffer of an effect voice into acc[0..n): the clip at the voice's
   pitch, gain ramped across the buffer */
static void fx_voice_mix(int v, float *acc, int n) {
    AudioFxVoice *fv = &g_fxvoice[v];
    FxVoiceState *st = &g_fvs[v];
    unsigned trig = fv->trig;
    const Clip *c = fx_clip(fv->set, fv->clip);
    if (trig != st->trig) {
        st->trig = trig;
        st->pos = -fv->delay * 44100.0f;
        st->live = 1;
        st->gain = 0.0f;
    }
    if (!fv->on || !st->live || !c || !c->n) { st->gain = 0.0f; return; }
    float g1 = fv->gain;
    if (!(g1 > 0.0f)) g1 = 0.0f;
    if (g1 > 4.0f) g1 = 4.0f;
    float pitch = fv->pitch;
    if (!(pitch > 0.25f && pitch < 4.0f)) pitch = 1.0f;
    float step = c->rate / 44100.0f * pitch;
    int loop = fv->loop;
    float g0 = st->gain, dg = (g1 - g0) / (float)n;
    for (int i = 0; i < n; i++) {
        g0 += dg;
        if (st->pos < 0.0f) {                 /* the stitch delay */
            st->pos += 1.0f;
            if (st->pos > 0.0f) st->pos = 0.0f;
            continue;
        }
        float s;
        if (loop) s = loop_next(c, &st->pos, step);
        else {
            if (st->pos >= (float)c->n - 1.0f) { st->live = 0; fv->done = 1; break; }
            s = clip_at(c, st->pos);
            st->pos += step;
        }
        acc[i] += s * g0 * 0.6f;             /* the traffic engine's level */
    }
    st->gain = g1;
}

/* one buffer of a car voice, added into acc[0..n). The loops play like the
   player's .abk path (idle below ~2200 rpm, 3 rpm bands, accel vs decel by
   load), at the voice's pitch; then the 1-pole low-pass. */
static void car_voice_mix(int v, float *acc, int n) {
    AudioCarVoice *cv = &g_carvoice[v];
    CarVoiceState *st = &g_cvs[v];
    int on = cv->on, bank = cv->bank;
    float g1 = on ? cv->gain : 0.0f;
    if (g1 > 4.0f) g1 = 4.0f;
    if (g1 < 1e-4f && st->gain < 1e-4f) { st->gain = 0.0f; return; }
    float pitch = cv->pitch, rpm = cv->rpm, load = cv->load, fc = cv->cutoff;
    if (!(pitch > 0.25f && pitch < 4.0f)) pitch = 1.0f;
    if (load < 0.0f) load = 0.0f; if (load > 1.0f) load = 1.0f;
    const Clip *cl[7]; float step[7], w[7]; int nl = 0, idx[7];
    float lvl;
    if (bank < 0) {                       /* traffic: FX_TRAFFIC's loop */
        if (!g_traffic_clip.n) return;
        cl[0] = &g_traffic_clip; idx[0] = 0; w[0] = 1.0f;
        step[0] = g_traffic_clip.rate / 44100.0f * pitch; nl = 1;
        lvl = 0.6f;
    } else {
        if (bank > 2 || !g_aibank[bank].ok) return;
        const AiBank *b = &g_aibank[bank];
        float wb[3]; eng_band_weights(rpm, wb);
        float wi = (2200.0f - rpm) / 1200.0f;
        if (wi < 0) wi = 0; if (wi > 1) wi = 1;
        if (wi > 1e-3f) { cl[nl] = &b->c[0]; idx[nl] = 0; w[nl] = wi;
                          step[nl++] = rpm / ENG_IDLE_RPM * b->c[0].rate / 44100.0f * pitch; }
        for (int k = 0; k < 3; k++)
            for (int a = 0; a < 2; a++) {
                float ww = wb[k] * (1.0f - wi) * (a ? 1.0f - load : load);
                int j = a ? 4 + k : 1 + k;
                if (ww < 1e-3f) continue;
                cl[nl] = &b->c[j]; idx[nl] = j; w[nl] = ww;
                step[nl++] = rpm / ENG_CENTER[k] * b->c[j].rate / 44100.0f * pitch;
            }
        lvl = 1.6f * 0.6f;                /* the player's .abk gain at a mid master volume */
    }
    float al = fc >= 20000.0f ? 1.0f : 1.0f - expf(-6.2831853f * (fc > 50.0f ? fc : 50.0f) / 44100.0f);
    float g0 = st->gain, dg = (g1 - g0) / (float)n;
    for (int i = 0; i < n; i++) {
        float s = 0.0f;
        for (int l = 0; l < nl; l++) s += loop_next(cl[l], &st->pos[idx[l]], step[l]) * w[l];
        st->lp += (s - st->lp) * al;
        g0 += dg;
        acc[i] += st->lp * g0 * lvl;
    }
    st->gain = g1;
}

void audio_impact(float strength) {
    g_impact_used = 1;
    if (strength > g_impact_req) g_impact_req = strength;
}

void audio_hold(int on) {
    if (!g_audio_live) return;
    if (on) {
        g_audio_held = 0;
        __sync_synchronize();
        g_audio_hold = 1;
        for (int i = 0; i < 250 && !g_audio_held; i++) SDL_Delay(1);
    } else {
        g_audio_hold = 0;
        __sync_synchronize();
    }
}

static float clip_at(const Clip *c, float pos) {
    uint32_t i0 = (uint32_t)pos;
    if (i0 + 1 >= c->n) return 0.0f;
    float fr = pos - (float)i0;
    return (c->s[i0]*(1.0f-fr) + c->s[i0+1]*fr) / 32768.0f;
}

/* the songs' level under the effects: the original's mix level isn't
   decoded (the AO*MusicVol sliders scale it); full-scale music would sit
   on the knee */
#define MUSIC_MIX 0.5f
static void audio_mix(void *ud, Uint8 *stream, int len) {
    (void)ud;
    if (g_audio_hold) {   /* a reload is swapping the data under us */
        memset(stream, 0, (size_t)len);
        __sync_synchronize();
        g_audio_held = 1;
        return;
    }
    {   float req = g_impact_req;
        if (req > 0.0f) {
            g_impact_req = 0.0f;
            /* a new hit replaces the playing one once it has sounded for
               80 ms, or at once when clearly harder */
            if (g_coll_cur < 0 || g_coll_age > 0.08f || req > g_coll_vol + 0.2f) {
                g_rng = g_rng*1664525u + 1013904223u;
                int k = (int)(g_rng >> 16) % 6 + (req < 0.35f ? 6 : 0);
                if (g_coll[k].n) {
                    g_coll_cur = k; g_coll_pos = 0.0f; g_coll_age = 0.0f;
                    g_coll_vol = 0.35f + 0.65f * (req > 1.0f ? 1.0f : req);
                }
            }
        }
    }
    {   int ev = g_shift_event;
        if (ev) {
            g_shift_event = 0;
            g_shift_which = ev > 0 ? 0 : 1;
            if (!g_shift_clip[g_shift_which].n) g_shift_which = 0;
            if (g_shift_clip[g_shift_which].n) g_shift_pos = 0.0f;
        }
    }
    static float hit = 0.0f, lp = 0.0f;
    int16_t *out = (int16_t *)stream;
    int n = len / 4;                 /* stereo S16: the effects centred, the music in stereo */
    static float mus[2 * 4096];
    if (n > 4096) n = 4096;
    memset(mus, 0, sizeof(float) * 2 * (size_t)n);
    music_mix(mus, n, 44100);
    static float hp = 0.0f; static double sq = 0.0;
    EngineSynthState *e = &g_engine;
    float target = e->target_rpm, load = e->load, master = e->master_volume;
    float road = g_road_vol, skid = g_skid;
    float spd = g_speed_frac;
    if (spd < 0.0f) spd = 0.0f; if (spd > 1.0f) spd = 1.0f;
    int nos_on = g_nitro && g_nos_clip[1].n;
    if (nos_on != g_aux.nos_was) {   /* engage / release one-shot */
        const Clip *k = nos_on ? &g_nos_clip[0] : &g_nos_clip[2];
        if (k->n && (nos_on || g_aux.nos_vol > 0.1f)) {
            g_nos_voice.c = k; g_nos_voice.pos = 0.0f;
            g_nos_voice.vol = nos_on ? 1.0f : g_aux.nos_vol;
        }
        g_aux.nos_was = nos_on;
    }
    int purge_on = g_purge && g_purge_clip[1].n;
    if (purge_on != g_aux.purge_was) {
        const Clip *k = purge_on ? &g_purge_clip[0] : &g_purge_clip[2];
        if (k->n && (purge_on || g_aux.purge_vol > 0.1f)) {
            g_purge_voice.c = k; g_purge_voice.pos = 0.0f;
            g_purge_voice.vol = purge_on ? 1.0f : g_aux.purge_vol;
        }
        g_aux.purge_was = purge_on;
    }
    if (g_hit > hit) hit = g_hit; g_hit = 0.0f;   /* latch a new collision */
    static float cars[4096], fe[4096];
    int ncar = n < 4096 ? n : 4096;
    memset(fe, 0, sizeof(float) * (size_t)ncar);
    fe_voices_mix(fe, ncar);
    const float lvm = g_lv_master, lvmus = MUSIC_MIX * g_lv_music * g_lv_master;
    if (!g_lv_stereo)   /* Mono: both channels the songs' mid */
        for (int i = 0; i < n; i++) mus[i * 2] = mus[i * 2 + 1] = 0.5f * (mus[i * 2] + mus[i * 2 + 1]);
    if (g_game_mute) {   /* paused: the front-end sounds and the music only */
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 2; c++) {
                float x = fe[i] * 0.62f * lvm + mus[i * 2 + c] * lvmus;
                if (x > 1.0f) x = 1.0f; if (x < -1.0f) x = -1.0f;
                out[i * 2 + c] = (int16_t)(x * 32767.0f);
            }
        return;
    }
    static float fxb[4096];
    memset(cars, 0, sizeof(float) * (size_t)ncar);
    memset(fxb, 0, sizeof(float) * (size_t)ncar);
    for (int v = 0; v < AUDIO_CARVOICES; v++) car_voice_mix(v, cars, ncar);
    for (int v = 0; v < AUDIO_FXVOICES; v++) fx_voice_mix(v, fxb, ncar);
    const float lve = g_lv_engine, lvt = g_lv_tire, lvs = g_lv_sfx;
    for (int i = 0; i < n; i++) {
        /* engine inertia: revs chase the target (~110ms), load a bit faster */
        e->current_rpm  += (target - e->current_rpm)  * 0.0002f;
        e->current_load += (load   - e->current_load) * 0.0005f;
        float w[3]; eng_band_weights(e->current_rpm, w);
        float eng = 0.0f;
        if (e->gin.has_gin_data) {
            /* native Gnsu20 path: loop the cycle-aligned grain matching the
               current rpm, pitch-corrected by the grain's recorded rpm.
               When rpm moves >6% off the playing grain, a secondary cursor
               sprouts at the target grain and crossfades in over ~12ms
               (dual-grain interpolation — no staircase on fast sweeps).
               Accel/decel sweeps crossfade by load. */
            Ginsu20State *g = &e->gin;
            /* The gearbox runs idle..redline (1000-8000); each car's accel
               recording covers its own engine's range (the Hummer 1267-4783).
               Map by fraction onto that range, so a truck never plays its
               sweep at 1.7x speed; that is the engine rpm both sweeps use.
               Several decel recordings only cover high revs (Bronco_DCL
               4629-5748): below its range the coast sound hands over to the
               accel sweep instead of playing a high-rev note at idle. */
            float rf = (e->current_rpm - ENG_IDLE_RPM) / (ENG_REDLINE_RPM - ENG_IDLE_RPM);
            if (rf < 0.0f) rf = 0.0f; if (rf > 1.0f) rf = 1.0f;
            float erpm = g->accel_sweep.rpm_min + rf * (g->accel_sweep.rpm_max - g->accel_sweep.rpm_min);
            float dmin = g->decel_sweep.rpm_min;
            float wdec = (erpm - 0.85f * dmin) / (0.15f * dmin);
            if (wdec < 0.0f) wdec = 0.0f; if (wdec > 1.0f) wdec = 1.0f;
            float wa = e->current_load + (1.0f - e->current_load) * (1.0f - wdec);
            float wd = (1.0f - e->current_load) * wdec;
            for (int a = 0; a < 2; a++) {
                GinsuSweep *sw = a ? &g->accel_sweep : &g->decel_sweep;
                float rate = (float)sw->sample_rate / 44100.0f;
                float srpm = erpm < sw->rpm_min ? sw->rpm_min : erpm > sw->rpm_max ? sw->rpm_max : erpm;
                if (!a && wd < 1e-4f) continue;   /* decel silent: skip its work */
                int j = g->grain_idx[a];
                if (g->play_cursor[a] >= (float)sw->grain[j+1])   /* loop own grain */
                    g->play_cursor[a] -= (float)(sw->grain[j+1] - sw->grain[j]);
                if (g->play_cursor[a] < (float)sw->grain[j])
                    g->play_cursor[a] = (float)sw->grain[j];
                if (!g->xf_on[a] &&
                    fabsf(srpm - g->grain_rpm[a]) > 0.06f * g->grain_rpm[a]) {
                    float pos = gin_pos_at(sw, srpm);
                    int lo = 0, hi = sw->n_grain - 1;
                    while (lo < hi) {
                        int mid = (lo + hi) / 2;
                        if (pos >= (float)sw->grain[mid+1]) lo = mid + 1; else hi = mid;
                    }
                    if (lo != j) {
                        g->xf_on[a] = 1; g->xf_grain[a] = lo; g->xf_t[a] = 0.0f;
                        g->xf_cursor[a] = (float)sw->grain[lo];
                        g->xf_rpm[a] = gin_rpm_at(sw,
                            0.5f*((float)sw->grain[lo] + (float)sw->grain[lo+1]));
                        if (g->xf_rpm[a] < 1.0f) g->xf_rpm[a] = sw->rpm_min;
                    }
                }
                uint32_t i0 = (uint32_t)g->play_cursor[a];
                float fr = g->play_cursor[a] - (float)i0;
                float s = (sw->samples[i0]*(1.0f-fr) + sw->samples[i0+1]*fr) / 32768.0f;
                g->play_cursor[a] += (srpm / g->grain_rpm[a]) * rate;
                if (g->xf_on[a]) {
                    int k = g->xf_grain[a];
                    if (g->xf_cursor[a] >= (float)sw->grain[k+1])
                        g->xf_cursor[a] -= (float)(sw->grain[k+1] - sw->grain[k]);
                    uint32_t i1 = (uint32_t)g->xf_cursor[a];
                    float f1 = g->xf_cursor[a] - (float)i1;
                    float s2 = (sw->samples[i1]*(1.0f-f1) + sw->samples[i1+1]*f1) / 32768.0f;
                    float t = g->xf_t[a];
                    s = s*(1.0f-t) + s2*t;
                    g->xf_cursor[a] += (srpm / g->xf_rpm[a]) * rate;
                    g->xf_t[a] += 1.0f / (0.012f * 44100.0f);
                    if (g->xf_t[a] >= 1.0f) {   /* promote secondary to primary */
                        g->grain_idx[a] = k; g->play_cursor[a] = g->xf_cursor[a];
                        g->grain_rpm[a] = g->xf_rpm[a]; g->xf_on[a] = 0;
                    }
                }
                eng += s * (a ? wa : wd) * master;
            }
            eng *= 0.55f;   /* sweeps are recorded near full scale */
        } else if (e->sample_mode) {
            /* Ginsu path: idle loop below ~2200rpm, crossfading into the 3
               RPM-band loop pairs; accel vs decel crossfaded by load */
            float wi = (2200.0f - e->current_rpm) / 1200.0f;   /* 1 at idle, 0 by 2200 */
            if (wi < 0) wi = 0; if (wi > 1) wi = 1;
            AudioSampleBand *ib = &e->idle_band;
            ib->playback_index += (e->current_rpm / ENG_IDLE_RPM) * (ib->srate / 44100.0f);
            while (ib->playback_index >= (float)ib->sample_count)
                ib->playback_index -= (float)ib->sample_count;
            ib->volume = wi * master;
            if (ib->volume > 1e-4f) {
                uint32_t i0 = (uint32_t)ib->playback_index;
                uint32_t i1 = i0 + 1 < ib->sample_count ? i0 + 1 : 0;
                float fr = ib->playback_index - (float)i0;
                eng += (ib->samples[i0]*(1.0f-fr) + ib->samples[i1]*fr) / 32768.0f * ib->volume;
            }
            for (int b = 0; b < 3; b++) {
                float ratio = e->current_rpm / ENG_CENTER[b];
                for (int a = 0; a < 2; a++) {
                    AudioSampleBand *sb = a ? &e->accel_bands[b] : &e->decel_bands[b];
                    sb->playback_index += ratio * (sb->srate / 44100.0f);
                    while (sb->playback_index >= (float)sb->sample_count)
                        sb->playback_index -= (float)sb->sample_count;
                    sb->volume = w[b] * (1.0f - wi)
                               * (a ? e->current_load : 1.0f - e->current_load) * master;
                    if (sb->volume < 1e-4f) continue;
                    uint32_t i0 = (uint32_t)sb->playback_index;
                    uint32_t i1 = i0 + 1 < sb->sample_count ? i0 + 1 : 0;
                    float fr = sb->playback_index - (float)i0;
                    float s = (sb->samples[i0]*(1.0f-fr) + sb->samples[i1]*fr) / 32768.0f;
                    eng += s * sb->volume;
                }
            }
            eng *= 1.6f;   /* the game loops are quieter than full scale */
        } else {
            float fund = ENG_FIRE_HZ(e->current_rpm);
            for (int b = 0; b < 3; b++) {
                AudioBand *bd = &e->bands[b];
                /* pitch = base_freq * (rpm / center) == the shared fundamental */
                bd->phase += fund / 44100.0f; if (bd->phase >= 1.0f) bd->phase -= 1.0f;
                bd->volume = w[b] * (0.30f + 0.70f*e->current_load) * master;
                if (bd->volume < 1e-4f) continue;
                float s = 0.0f;
                for (int h = 0; h < 4; h++)
                    s += ENG_HARM[b][h].amp *
                         sinf(6.2831853f * ENG_HARM[b][h].mult * bd->phase);
                eng += s * bd->volume;
            }
            eng *= 0.6f;   /* mix gain: matches the old synth's loudness */
        }

        /* --- auxiliary acoustics, layered over any engine mode --- */
        {
            AuxAudio *x = &g_aux;
            float rf = e->current_rpm / ENG_REDLINE_RPM;
            float anz = frand();
            x->boost += ((g_turbo_fitted ? g_turbo_boost : 0.0f) - x->boost) * 0.0005f;
            x->hiboost -= 1.5f / 44100.0f;
            if (x->boost > x->hiboost) x->hiboost = x->boost;
            /* the car's SOUND/TURBO bank plays through carsfx.c (CARSFX_Turbo) */
            if (!g_turbo_whine.n) {   /* no bank: synth whine, pitch and volume follow load * rpm */
                float tv = e->current_load * rf; if (tv > 1.0f) tv = 1.0f;
                if (!g_turbo_fitted) tv = 0.0f;
                x->turbo_phase += (1200.0f * (1.0f + 1.5f*rf)) / 44100.0f;
                if (x->turbo_phase >= 1.0f) x->turbo_phase -= 1.0f;
                eng += (sinf(6.2831853f*x->turbo_phase)*0.8f + anz*0.2f)
                     * tv * 0.045f * master;
            }
            /* blow-off valve: boost vented on a sharp lift-off (load falls
               from >0.7 to <0.15 within the ~150ms hiload memory) */
            x->hiload -= 4.5f / 44100.0f;
            if (e->current_load > x->hiload) x->hiload = e->current_load;
            if (g_turbo_fitted && x->hiload > 0.7f && e->current_load < 0.15f && rf > 0.3f &&
                x->bov_env < 0.01f && !g_bov.c) {
                x->hiload = 0.0f;
                if (!g_bov_clip[0].n) x->bov_env = 0.9f;   /* no bank; else carsfx.c */
            }
            eng += voice_next(&g_bov) * 0.5f * master;
            if (x->bov_env > 0.002f) {
                x->bov_hp += (anz - x->bov_hp) * 0.45f;   /* hissy top end */
                eng += (anz - x->bov_hp) * x->bov_env * 0.5f * master;
                x->bov_env *= 0.99975f;                   /* ~250ms tail */
            }
            /* overrun pops: high rpm + closed throttle, irregular intervals */
            if (e->current_rpm > 4000.0f && e->current_load < 0.05f) {
                x->pop_timer -= 1.0f / 44100.0f;
                if (x->pop_timer <= 0.0f) {
                    g_rng = g_rng*1664525u + 1013904223u;
                    x->pop_timer = 0.10f + (float)(g_rng >> 20 & 1023) / 1023.0f * 0.20f;
                    x->pop_env = 0.5f + (float)(g_rng >> 8 & 255) / 255.0f * 0.5f;
                }
            } else x->pop_timer = 0.12f;
            if (x->pop_env > 0.003f) {
                x->pop_lp += (anz - x->pop_lp) * 0.10f;   /* deep bang */
                eng += x->pop_lp * x->pop_env * 2.2f * master;
                x->pop_env *= 0.9992f;                    /* ~35ms transient */
            }
        }
        float nz = frand();
        float env;
        if (g_road_clip[0].n) {
            /* tyre roar rises with speed; wind with speed squared, stepping
               through FX_WIND's three loops (light, mid, strong) */
            AuxAudio *x = &g_aux;
            float rg = (0.08f + 0.35f * spd) * (spd < 0.1f ? spd * 10.0f : 1.0f);
            float rs = 0.85f + 0.3f * spd;
            env = 0.0f;
            if (rg > 0.001f)
                env += loop_next(&g_road_clip[0], &x->road_pos,
                                 rs * g_road_clip[0].rate / 44100.0f) * rg;
            float wg = spd * spd * 0.45f;
            if (wg > 0.001f) {
                float ws = 0.9f + 0.2f * spd;
                float t = spd * 2.0f;   /* 0..1: light -> mid, 1..2: mid -> strong */
                float w3[3] = { t < 1.0f ? 1.0f - t : 0.0f,
                                t < 1.0f ? t : 2.0f - t,
                                t > 1.0f ? t - 1.0f : 0.0f };
                for (int k = 0; k < 3; k++)
                    if (w3[k] > 0.001f)
                        env += loop_next(&g_wind_clip[k], &x->wind_pos[k],
                                         ws * g_wind_clip[k].rate / 44100.0f) * wg * w3[k];
            }
        } else {   /* road/wind: low-passed white noise scaled by speed */
            lp += (nz - lp) * 0.25f;
            env = lp * road;
        }
        /* nitrous: engage one-shot, burn loop fading in under it, release */
        {   AuxAudio *x = &g_aux;
            x->nos_vol += ((nos_on ? 1.0f : 0.0f) - x->nos_vol) * (nos_on ? 0.0001f : 0.0006f);
            if (x->nos_vol > 0.002f)
                env += loop_next(&g_nos_clip[1], &x->nos_pos, g_nos_clip[1].rate / 44100.0f)
                     * x->nos_vol * 0.35f;
            env += voice_next(&g_nos_voice) * 0.35f;
            x->purge_vol += ((purge_on ? 1.0f : 0.0f) - x->purge_vol) * (purge_on ? 0.0001f : 0.0006f);
            if (x->purge_vol > 0.002f)
                env += loop_next(&g_purge_clip[1], &x->purge_pos, g_purge_clip[1].rate / 44100.0f)
                     * x->purge_vol * 0.35f;
            env += voice_next(&g_purge_voice) * 0.35f;
        }
        /* tyre screech: high-passed noise + a squeal square wave while drifting */
        hp += (nz - hp) * 0.5f;
        sq += 920.0/44100.0; if (sq >= 1.0) sq -= 1.0;
        float scr;
        if (g_skid3_on && g_skidk[0].n && g_skidk[3].n) {
            /* retail FX_SKID: channel c plays squeal clip 3-c (wheelspin 3,
               cornering 2, slide/brake 1), each with the rubber clip 4 */
            float v[3] = { g_skid3[0], g_skid3[1], g_skid3[2] }, sum = 0.0f;
            scr = 0.0f;
            for (int c = 0; c < 4; c++) {
                const Clip *k = &g_skidk[c];
                float vol = c < 3 ? v[2 - c] : 0.0f;
                if (c == 3) { sum = v[0] + v[1] + v[2]; vol = sum > 1.0f ? 1.0f : sum; }
                if (!k->n) continue;
                uint32_t ls = k->loop_end ? k->loop_start : 0, le = k->loop_end ? k->loop_end : k->n - 1;
                if (vol > 0.001f) scr += clip_at(k, g_skidk_pos[c]) * vol * (c < 3 ? 0.45f : 0.35f);
                g_skidk_pos[c] += k->rate / 44100.0f;
                if (g_skidk_pos[c] >= (float)le) g_skidk_pos[c] -= (float)(le - ls);
            }
        } else
        if (g_skid_clip.n) {   /* the game's pavement skid loop */
            const Clip *k = &g_skid_clip;
            uint32_t ls = k->loop_end ? k->loop_start : 0, le = k->loop_end ? k->loop_end : k->n - 1;
            scr = skid > 0.001f ? clip_at(k, g_skid_pos) * skid * 2.2f : 0.0f;
            g_skid_pos += k->rate / 44100.0f;
            if (g_skid_pos >= (float)le) g_skid_pos -= (float)(le - ls);
        } else
            scr = ((nz - hp)*0.7f + (sq < 0.5 ? 0.15f : -0.15f)) * skid;
        if (g_shift_pos >= 0.0f) {   /* gear change one-shot */
            const Clip *k = &g_shift_clip[g_shift_which];
            scr += clip_at(k, g_shift_pos) * 0.3f;   /* under the engine, not over it */
            g_shift_pos += k->rate / 44100.0f;
            if (g_shift_pos >= (float)k->n - 1.0f) g_shift_pos = -1.0f;
        }
        /* collision: short noise burst that decays */
        float thud = frand() * hit; hit *= 0.9985f;
        if (g_impact_used && g_coll[0].n) thud = 0.0f;   /* real clips replace the noise */
        if (g_coll_cur >= 0) {
            const Clip *k = &g_coll[g_coll_cur];
            thud += clip_at(k, g_coll_pos) * g_coll_vol * 0.9f;
            g_coll_pos += k->rate / 44100.0f;
            g_coll_age += 1.0f / 44100.0f;
            if (g_coll_pos >= (float)k->n - 1.0f) g_coll_cur = -1;
        }
        float s = (eng + (i < ncar ? cars[i] : 0.0f)) * lve + scr * lvt +
                  (env + thud*0.8f + (i < ncar ? fxb[i] : 0.0f)) * lvs + (i < ncar ? fe[i] : 0.0f);
        /* master gain ~1.8x the old 11000/32768, with a soft knee above
           half scale instead of hard clipping when everything plays at once */
        for (int c = 0; c < 2; c++) {
            float x = s * 0.62f * lvm + mus[i * 2 + c] * lvmus, ax = x < 0.0f ? -x : x;
            if (ax > 0.5f) {
                float o = ax - 0.5f;
                ax = 0.5f + o / (1.0f + 2.0f * o);   /* -> 1.0 asymptotically */
                x = x < 0.0f ? -ax : ax;
            }
            int v = (int)(x * 32767.0f);
            if (v > 32767) v = 32767; if (v < -32768) v = -32768;
            out[i * 2 + c] = (int16_t)v;
        }
    }
}

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif
/* audio thread cost: CPU time spent mixing vs audio time produced */
static volatile uint64_t g_mix_us, g_mix_samples;
static void audio_cb(void *ud, Uint8 *stream, int len) {
#ifdef __vita__
    uint64_t t0 = sceKernelGetProcessTimeWide();
    audio_mix(ud, stream, len);
    g_mix_us += sceKernelGetProcessTimeWide() - t0;
    g_mix_samples += (uint64_t)(len / 4);
#else
    audio_mix(ud, stream, len);
#endif
}

float audio_cost_pct(void) {
    uint64_t us = g_mix_us, n = g_mix_samples;
    g_mix_us = 0; g_mix_samples = 0;
    return n ? (float)((double)us / ((double)n / 44100.0 * 1e6) * 100.0) : 0.0f;
}

/* asserts the crossfade math: pure bands at the centers, 50/50 at the
 * midpoints, and the weights always partition to 1. */
void audio_selftest(void) {
    float w[3];
    eng_band_weights(1000.0f, w); assert(w[0]==1.0f && w[1]==0.0f && w[2]==0.0f);
    eng_band_weights(1500.0f, w); assert(w[0]==1.0f);
    eng_band_weights(2750.0f, w); assert(fabsf(w[0]-0.5f)<1e-4f && fabsf(w[1]-0.5f)<1e-4f);
    eng_band_weights(4000.0f, w); assert(w[1]==1.0f);
    eng_band_weights(5500.0f, w); assert(fabsf(w[1]-0.5f)<1e-4f && fabsf(w[2]-0.5f)<1e-4f);
    eng_band_weights(8000.0f, w); assert(w[2]==1.0f && w[0]==0.0f);
    for (float r = 800.0f; r <= 8200.0f; r += 37.0f) {
        eng_band_weights(r, w);
        assert(fabsf(w[0]+w[1]+w[2]-1.0f) < 1e-4f);
    }
    /* gearbox: a 20s 0->top ramp must climb 1..6 with rpm in [idle, redline]
     * and no flapping; the way back down must land in 1st. */
    int gear = 1, maxg = 1; float st = 0.0f;
    for (int i = 0; i <= 1200; i++) {
        eng_gearbox_step(i/1200.0f, 1.0f, 1.0f/60.0f, &gear, &st);
        assert(gear >= 1 && gear <= ENG_GEARS);
        assert(g_engine.target_rpm >= ENG_IDLE_RPM - 1.0f &&
               g_engine.target_rpm <= ENG_REDLINE_RPM + 1.0f);
        if (gear > maxg) maxg = gear;
    }
    assert(maxg == ENG_GEARS);
    for (int i = 1200; i >= 0; i--)
        eng_gearbox_step(i/1200.0f, 0.0f, 1.0f/60.0f, &gear, &st);
    assert(gear == 1);
    g_engine.target_rpm = ENG_IDLE_RPM; g_engine.load = 0.0f;   /* reset */
}

SDL_AudioDeviceID audio_init(void) {
    SDL_AudioSpec want; SDL_memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2;
    want.samples = 1024; want.callback = audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);
    g_audio_live = adev != 0;
    g_shift_event = 0;   /* the selftest's gear changes are not real shifts */
    return adev;
}
