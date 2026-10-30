/* tuning.c -- the crib's Test Track: Performance Tuning / Dyno (TEST_TRACK.md).
 *
 * 1. CustomPhysicsSpec (PC SPEED2.EXE): the availability table (GLOBALB
 *    0x34a1a, GetInstalledParams 0x59ae40), GetRealValue 0x5a7da0,
 *    GetRealMinMax 0x5aca50 (+ grip 0x5a7b80 / damping 0x5a7cb0 / gear map
 *    0x59b5c0 / AdjustForTuneableRange 0x59b160), the tuneable range
 *    0x5b2980 (+ the torque limit 0x5ace80), RemapValue 0x5b2800 /
 *    GetTotalRangeMinMax 0x5b09a0 / ApplyUnitRemapping 0x59b4a0,
 *    UpdateStoredTuning 0x5b3ee0, SetValue 0x5b4670, SetRealValue 0x5b3fa0,
 *    SetTorqueValue 0x5b2c00, FixTorqueCurve 0x59b3c0 and
 *    RebuildCustomTunings 0x5b4780.
 * 2. CarTuneManager + the screens of GLOBAL/InGamePerformanceTuning.bun.
 *
 * GUESSES (GAPS.md "Test Track"): the per-channel split of a shared shock /
 * grip value (SetRealValue cases 1/5/19/20: a lerp in each channel's own
 * range), the nitrous coupling (0x5b3100: the other type at the mirrored
 * place of its range), no gear spacing (EnforceGearSpacingConstraint), the
 * torque limit's peak power (0x5ac980: the best of the 9 points), the
 * unique-upgrade undo/redo around the edits (0x599d90 / 0x5997e0, not
 * ported), the slider rows' layout and the dyno run (analytic). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tuning.h"
#include "n2sim.h"
#include "hud.h"
#include "options.h"
#include "audio.h"
#include "shop.h"
#include "career.h"

#define PO 0x110   /* P in the GLOBALB record */
#define BUN "GLOBAL/InGamePerformanceTuning.bun"

typedef struct { uint32_t part; int8_t level, type; uint8_t mode; float a, b, dmin, dmax; } TuneRec;
static TuneRec g_trec[96];
static int g_ntrec;

static float rf(const unsigned char *p, int o) { float f; memcpy(&f, p + o, 4); return f; }
static void  wf(unsigned char *p, int o, float f) { memcpy(p + o, &f, 4); }
static float fmin2(float a, float b) { return a < b ? a : b; }
static float fmax2(float a, float b) { return a > b ? a : b; }
static int   is_gear(int t) { return t >= 10 && t <= 15; }
static int   is_trend(int t) { return t == 3 || t == 7 || t == 9; }   /* GetTuningTrend 0x59b6c0: -1 */
static int   no_disp(float v) { return fabsf(v + 99.99f) < 0.001f; }

int tune_init(const unsigned char *g, long len)
{
    g_ntrec = 0;
    long stack_end[8]; int depth = 0;
    long o = 0, end = len;
    stack_end[0] = end;
    while (o + 8 <= end) {
        uint32_t id, sz; memcpy(&id, g + o, 4); memcpy(&sz, g + o + 4, 4);
        long body = o + 8, next = body + (long)sz;
        if (next > end || next < body) break;
        if ((id & 0x80000000u) && sz > 8 && depth < 7) { stack_end[++depth] = end; end = next; o = body; continue; }
        if (id == 0x34a1a && !g_ntrec && sz % 0x18 == 0) {
            for (long i = 0; i < (long)sz / 0x18 && g_ntrec < 96; i++) {
                const unsigned char *r = g + body + i * 0x18;
                TuneRec *t = &g_trec[g_ntrec++];
                memcpy(&t->part, r, 4);
                t->level = (int8_t)r[4]; t->type = (int8_t)r[5]; t->mode = r[6];
                t->a = rf(r, 8); t->b = rf(r, 12); t->dmin = rf(r, 16); t->dmax = rf(r, 20);
            }
        }
        o = next;
        while (o >= end && depth > 0) { o = end; end = stack_end[depth--]; }
    }
    printf("tuning: %d CustomPhysicsSpec availability records (GLOBALB 0x34a1a)\n", g_ntrec);
    return g_ntrec;
}

/* ---- one CustomPhysicsSpec: the record it edits ---- */
typedef struct {
    unsigned char *P;            /* the working record's P */
    const unsigned char *R;      /* the raw record's P (GetRealMinMax's ranges) */
    const PerfParts *pp;
    float base[25][9];           /* spec +0x40 (types 0-22), +0x9c ECU, +0xc0 turbo */
    int metric;
} Spec;

/* IsTuningActive: the highest-level record of the type whose part is fitted */
static const TuneRec *t_rec(const Spec *s, int t)
{
    const TuneRec *best = NULL;
    for (int i = 0; i < g_ntrec; i++) {
        const TuneRec *r = &g_trec[i];
        if (r->type != t || r->part >= PERF_NPARTS || !s->pp || !s->pp->fit[r->part]) continue;
        if (!best || r->level > best->level) best = r;
    }
    return best;
}
static int t_gears(const Spec *s) { int n; memcpy(&n, s->P + 0x1c8, 4); return n; }
/* RebuildCustomTunings / IsTuningTypeAvailable: active, a gear only up to the car's count */
static int t_avail(const Spec *s, int t)
{
    if (!t_rec(s, t)) return 0;
    return !is_gear(t) || t - 8 < t_gears(s) + 2;
}
/* GetRealValuePtr 0x59b200 (P offsets) */
static int vptr(int t, int i)
{
    switch (t) {
    case 0: return 0x174;  case 1: return 0x178 + 4 * i; case 2: return 0x180;
    case 3: return 0x18 + 0x30 * i; case 4: return 0x194; case 5: return 0x198 + 4 * i;
    case 6: return 0x1a0;  case 7: return 0x78 + 0x30 * i; case 8: return 0x270;
    case 9: return 0x1b8 + 4 * i;
    case 16: return 0x2a0; case 17: return 0x2ac; case 18: return 0x27c;
    case 19: return i ? 0xe0 : 0xdc; case 20: return i ? 0x100 : 0xfc;
    case 21: return 0x264; case 22: return 0x268;
    case 23: return 0x200 + 4 * i; case 24: return 0x224 + 4 * i;
    default: return is_gear(t) ? 0x1b0 + t * 4 : 0x174;
    }
}
/* GetRealValue 0x5a7da0 */
static float real_value(const Spec *s, int t, int i)
{
    switch (t) {
    case 1: case 5: case 19: case 20: return (rf(s->P, vptr(t, 0)) + rf(s->P, vptr(t, 1))) * 0.5f;
    case 3: case 7: return rf(s->R, vptr(t, 0)) - rf(s->P, vptr(t, 0));
    case 23: return rf(s->P, 0x200 + 4 * i) - rf(s->P, 0x224 + 4 * i);
    default: return rf(s->P, vptr(t, i));
    }
}
/* MapGearRatioToTopSpeed 0x59b5c0: speed (m/s) <-> ratio */
static float gear_map(const unsigned char *P, float v)
{
    float sp = rf(P, 0x1c0), R = sp < 0.2f ? rf(P, 0x50) : sp > 0.8f ? rf(P, 0x80) : (1.0f - sp) * rf(P, 0x50) + sp * rf(P, 0x80);
    float A = rf(P, 0x1f4) * 0.10471967f, F = rf(P, 0x1b8);
    return F * v != 0.0f ? R * A / (F * v) : 0.0f;
}
/* AdjustForTuneableRange 0x59b160 */
static void adjust(const Spec *s, int t, float *mn, float *mx)
{
    const TuneRec *r = t_rec(s, t);
    if (!r) return;
    float a = *mn, b = *mx;
    if (r->mode == 2 || r->mode == 3) { *mn = (b - a) * r->a + a; *mx = (b - a) * r->b + b; }
    else if (r->mode == 0 || r->mode == 1) { *mn = a + r->a; *mx = b + r->b; }
    else if (r->mode == 4) { *mn = fmin2(a, r->a); *mx = fmax2(b, r->b); }
}
/* GetGripMinMax 0x5a7b80 / GetDampingMinMax 0x5a7cb0: per channel stock ->
   upgrade target, each adjusted, averaged over both when avg */
static void chan_minmax(const Spec *s, int t, int ch, int avg, float *mn, float *mx)
{
    static const int lo19[2] = { 0xdc, 0xe0 }, hi19[2] = { 0x54c, 0x550 };
    static const int lo20[2] = { 0xfc, 0x100 }, hi20[2] = { 0x56c, 0x570 };
    float a[2], b[2];
    for (int c = 0; c < 2; c++) {
        if (t == 19) { a[c] = rf(s->R, lo19[c]); b[c] = rf(s->R, hi19[c]); }
        else if (t == 20) { a[c] = rf(s->R, lo20[c]); b[c] = rf(s->R, hi20[c]); }
        else if (t == 1) { a[c] = rf(s->R, 0x178 + 4 * c); b[c] = rf(s->R, 0x5c8 + 4 * c); }
        else { a[c] = rf(s->R, 0x198 + 4 * c); b[c] = rf(s->R, 0x5e8 + 4 * c); }
        adjust(s, t, &a[c], &b[c]);
    }
    if (avg) { *mn = (a[0] + a[1]) * 0.5f; *mx = (b[0] + b[1]) * 0.5f; }
    else { *mn = a[ch & 1]; *mx = b[ch & 1]; }
}
/* GetRealMinMax 0x5aca50 */
static void real_minmax(const Spec *s, int t, float *omn, float *omx, int ch, int avg)
{
    float mn = 100000.0f, mx = -100000.0f;
    const unsigned char *R = s->R;
#define EXT(v) do { float v_ = (v); if (v_ < mn) mn = v_; if (v_ > mx) mx = v_; } while (0)
    switch (t) {
    case 0: case 4: EXT(rf(R, 0x174)); EXT(rf(R, 0x194)); EXT(rf(R, 0x5c4)); EXT(rf(R, 0x5e4)); break;
    case 1: case 5: case 19: case 20: chan_minmax(s, t, ch, avg, omn, omx); return;
    case 2: case 6: EXT(rf(R, 0x180)); EXT(rf(R, 0x1a0)); EXT(rf(R, 0x5d0)); EXT(rf(R, 0x5f0)); break;
    case 3: case 7: EXT(-rf(R, 0x604)); EXT(-rf(R, 0x608)); EXT(0.0f); break;
    case 8: EXT(rf(R, 0x270)); EXT(rf(R, 0x5bc)); break;
    case 9: EXT(rf(R, 0x1b8)); EXT(rf(R, 0x3d8)); break;
    case 16: *omn = 1.0f; *omx = 11.0f; return;                       /* 0x7a2380 */
    case 17: mn = 0.1f; mx = 0.9f; break;
    case 18: mn = 0.0f; mx = 1.0f; break;
    case 21: case 22:
        EXT(rf(R, 0x264)); EXT(rf(R, 0x5a0) + rf(R, 0x590) + rf(R, 0x264));
        EXT(rf(R, 0x268)); EXT(rf(R, 0x5a4) + rf(R, 0x594) + rf(R, 0x268));
        break;
    case 23: case 24:
        for (int i = 0; i < 9; i++) {
            if (t == 23) { EXT(rf(R, 0x200 + 4 * i)); EXT(rf(R, 0x4e0 + 4 * i) + rf(R, 0x420 + 4 * i) + rf(R, 0x200 + 4 * i)); }
            else { EXT(rf(R, 0x224 + 4 * i)); EXT(rf(R, 0x510 + 4 * i)); }
        }
        adjust(s, t, &mn, &mx);
        *omn = fmax2(mn, 0.0f); *omx = fmax2(mx, 0.0f);
        return;
    default:
        if (is_gear(t)) { *omn = gear_map(s->P, 111.75682f); *omx = gear_map(s->P, 1.1175681f); return; }
        break;
    }
#undef EXT
    adjust(s, t, &mn, &mx);
    *omn = mn; *omx = mx;
}
static float base_of(const Spec *s, int t, int i) { return t < 23 ? s->base[t][0] : s->base[t][i]; }
/* 0x5a79f0: the default the tuneable range hangs on -- over the pair's bases,
   the smallest for the lowering / final drive, else the largest */
static float def_value(const Spec *s, int t, int i)
{
    int pr[2] = { t, -1 };
    if (t == 0 || t == 4) { pr[0] = 0; pr[1] = 4; }
    else if (t == 2 || t == 6) { pr[0] = 2; pr[1] = 6; }
    else if (t == 3 || t == 7) { pr[0] = 3; pr[1] = 7; }
    else if (t == 21 || t == 22) { pr[0] = 21; pr[1] = 22; }
    float v = is_trend(t) ? 1e7f : -1e7f;
    for (int k = 0; k < 2; k++) {
        if (pr[k] < 0) continue;
        float b = base_of(s, pr[k], i);
        v = is_trend(t) ? fmin2(v, b) : fmax2(v, b);
    }
    return v;
}
/* 0x5ace80: a torque curve's max at least its highest base point, and no
   more power at this point's rpm than the base curve's peak */
static void torque_limit(const Spec *s, int t, float *mn, float *mx, int i)
{
    float top = 0.0f, peak = 0.0f, r0 = rf(s->P, 0x1f0), r1 = rf(s->P, 0x1f8);
    for (int k = 0; k < 9; k++) {
        float b = base_of(s, t, k), rpm = (r1 - r0) * (float)k * 0.125f + r0;
        top = fmax2(top, b);
        peak = fmax2(peak, b * 1000.0f * rpm * 0.000104712f);   /* GUESS: 0x5ac980 = the best point */
    }
    if (*mx < top) *mx = top;
    float rpm = (r1 - r0) * (float)i * 0.125f + r0, pw = *mx * 1000.0f * rpm * 0.000104712f;
    if (peak < pw && rpm > 0.0f) *mx = fmax2(0.0f, (peak - pw) / (rpm * 0.104712f) + *mx);
    *mn = fmax2(*mn, 0.0f);
    if (*mx < 0.0f) *mx = 0.0f;
}
/* GetTotalRangeMinMax 0x5b09a0 (the display range) */
static float unit_fwd(const Spec *s, int t, float v)
{
    if (t == 3 || t == 7) return v * (s->metric ? 100.0f : 39.37f);
    if (is_gear(t)) return v * (s->metric ? 3.6f : 2.237f);
    if (t == 23 || t == 24) return v * 1000.0f * (s->metric ? 1.0f : 0.737562f);
    return v;
}
static void total_range(const Spec *s, int t, float *lo, float *hi)
{
    real_minmax(s, t, lo, hi, 0, 1);
    const TuneRec *r = t_rec(s, t);
    if (!r) return;
    float a = *lo, b = *hi;
    if (no_disp(r->dmin)) { if (is_gear(t)) a = gear_map(s->P, a); a = unit_fwd(s, t, a); } else a = r->dmin;
    if (no_disp(r->dmax)) { if (is_gear(t)) b = gear_map(s->P, b); b = unit_fwd(s, t, b); } else b = r->dmax;
    if (b < a) { float x = a; a = b; b = x; }
    if (a == b) b += 0.01f;
    *lo = a; *hi = b;
}
static float lerp_map(float x, float a, float b, float c, float d) { return b != a ? c + (x - a) / (b - a) * (d - c) : c; }
/* RemapValue 0x5b2800: real -> display (fwd) or back */
static float remap(const Spec *s, int t, float v, int fwd)
{
    float rmn, rmx, dlo, dhi;
    real_minmax(s, t, &rmn, &rmx, 0, 1);
    if (t_rec(s, t) && rmn == rmx) rmx += 0.01f;
    total_range(s, t, &dlo, &dhi);
    if (is_gear(t)) {
        if (!fwd) return gear_map(s->P, v / (s->metric ? 3.6f : 2.237f));   /* ApplyUnitRemapping(.., 0) */
        float sv = gear_map(s->P, v), a = gear_map(s->P, rmn), b = gear_map(s->P, rmx);
        if (b < a) { float x = a; a = b; b = x; }
        return lerp_map(sv, a, b, dlo, dhi);
    }
    return fwd ? lerp_map(v, rmn, rmx, dlo, dhi) : lerp_map(v, dlo, dhi, rmn, rmx);
}
/* GetTuneableRangeMinMax 0x5b2980 */
static int tune_range(const Spec *s, int t, float *lo, float *hi, int i, int disp)
{
    const TuneRec *r = t_rec(s, t);
    if (!r) return 0;
    float rmn, rmx;
    real_minmax(s, t, &rmn, &rmx, i, 1);
    float def = def_value(s, t, i);
    if (t == 23 || t == 24) torque_limit(s, t, &rmn, &rmx, i);
    float R = rmx - rmn, a, b;
    switch (r->mode) {
    case 0:
        if (is_trend(t)) { a = fmax2(rmn, def - r->b); b = rmx; }
        else { a = rmn; b = fmin2(rmx, def + r->b); }
        break;
    case 1: a = fmax2(rmn, def + r->a); b = fmin2(rmx, def + r->b); break;
    case 2:
        if (is_trend(t)) { a = fmax2(rmn, def - R * r->b); b = rmx; }
        else { a = rmn; b = fmin2(rmx, def + R * r->b); }
        break;
    case 3: a = fmax2(rmn, def + R * r->a); b = fmin2(rmx, def + R * r->b); break;
    case 4: a = r->a; b = r->b; break;
    default: a = rmn; b = rmx; break;
    }
    if (disp) {
        a = remap(s, t, a, 1); b = remap(s, t, b, 1);
        if (b < a) { float x = a; a = b; b = x; }
    }
    if (a == b) b += 0.01f;
    *lo = a; *hi = b;
    return 1;
}
/* CustomPhysicsSpec GetValue 0x59aee0 / SetValue 0x59af80 (the byte) */
static float byte_t(uint8_t b) { float t = ((float)b - 100.0f) / 100.0f; return t < -1.0f ? -1.0f : t > 1.0f ? 1.0f : t; }
static uint8_t t_byte(float t)
{
    int b = (int)(t * 100.0f + 100.5f);
    return (uint8_t)(b < 0 ? 0 : b > 200 ? 200 : b);
}
static int tidx(int t, int i) { return t < 23 ? t : t == 23 ? 0x17 + i : 0x20 + i; }
/* UpdateStoredTuning 0x5b3ee0: the real value into the tuneable range, its byte stored */
static float store(const Spec *s, uint8_t *tune, int t, float v, int i)
{
    float lo, hi;
    if (!tune_range(s, t, &lo, &hi, i, 0)) return v;
    v = fmax2(lo, fmin2(hi, v));
    if (tune) tune[tidx(t, i)] = t_byte((v - base_of(s, t, i)) / (hi - lo));
    return v;
}
/* SetTorqueValue 0x5b2c00 (spread: the UI's edit moves the neighbours,
   weights 0x7fc674, the sum kept) */
static void set_torque(Spec *s, int t, float v, int i, int spread)
{
    float cur[9];
    for (int k = 0; k < 9; k++) cur[k] = real_value(s, t, k);
    float old = cur[i], d = v - old, lo = 0, hi = 0;
    tune_range(s, t, &lo, &hi, i, 0);
    cur[i] = fmax2(lo, fmin2(hi, v));
    if (d != 0.0f && fabsf(d) > 1e-7f && spread) {
        static const float w[9] = { 0.25f, 0.25f, -0.25f, -0.25f, -0.25f, -0.25f, -0.25f, -0.25f, 0.0f };
        int wi = 0, off = 0, guard = 0;
        float rem = d;
        while (wi < 20 && fabsf(rem) > 1e-7f && guard++ < 64) {
            float m = -rem;
            if (wi < 9) {
                m = d * w[wi];
                if (rem <= 0.0f) { if (m <= rem) m = rem; }
                else if (m >= rem) m = rem;
            }
            int n = -off;
            if (off < 1 && ++n > 8) n = 1;
            int j = i + n;
            if (j >= 0 && j < 9) {
                float l2 = 0, h2 = 0;
                tune_range(s, t, &l2, &h2, j, 0);
                float nv = fmax2(l2, fmin2(h2, m + cur[j]));
                wi++;
                rem += nv - cur[j];
                cur[j] = nv;
            }
            off = n;
        }
    }
    for (int k = 0; k < 9; k++) {
        if (t == 23) wf(s->P, 0x200 + 4 * k, cur[k] + rf(s->P, 0x224 + 4 * k));
        else {
            wf(s->P, 0x200 + 4 * k, rf(s->P, 0x200 + 4 * k) - rf(s->P, 0x224 + 4 * k) + cur[k]);
            wf(s->P, 0x224 + 4 * k, cur[k]);
        }
    }
}
/* SetRealValue 0x5b3fa0 */
static void set_real(Spec *s, int t, float v, int i, int spread)
{
    switch (t) {
    case 1: case 5: case 19: case 20: {
        float amn, amx;
        real_minmax(s, t, &amn, &amx, 0, 1);
        float f = amx != amn ? (v - amn) / (amx - amn) : 0.0f;   /* GUESS: each channel at the same place of its range */
        for (int c = 0; c < 2; c++) {
            float cmn, cmx;
            real_minmax(s, t, &cmn, &cmx, c, 0);
            wf(s->P, vptr(t, c), cmn + f * (cmx - cmn));
        }
        break; }
    case 3: case 7:
        for (int c = 0; c < 2; c++) wf(s->P, vptr(t, c), rf(s->R, vptr(t, c)) - v);
        break;
    case 9: wf(s->P, 0x1b8, v); wf(s->P, 0x1bc, v); break;
    case 16: case 17: {   /* 0x5b3100 SetNitrousValue */
        int o = t == 17 ? 16 : 17;
        float l1, h1, l2, h2;
        wf(s->P, vptr(t, 0), v);
        if (tune_range(s, t, &l1, &h1, 0, 0) && tune_range(s, o, &l2, &h2, 0, 0) && h1 != l1)
            wf(s->P, vptr(o, 0), h2 - (v - l1) / (h1 - l1) * (h2 - l2));   /* GUESS: the mirror */
        break; }
    case 23: case 24: set_torque(s, t, v, i, spread); break;
    default:
        if (is_gear(t)) {   /* 0x5b3060: SetGearRatio (spacing GUESS: none), gears past the count 0 */
            wf(s->P, 0x1b0 + t * 4, v);
            int n = t_gears(s) + 2;
            for (int g = 10; g < 16; g++) if (g - 8 >= n) wf(s->P, 0x1b0 + g * 4, 0.0f);
        } else wf(s->P, vptr(t, i), v);
        break;
    }
}
/* SetValue 0x5b4670 (remap: v is a display value) */
static void set_value(Spec *s, uint8_t *tune, int t, float v, int i, int remap_in)
{
    if (remap_in) v = remap(s, t, v, 0);
    v = store(s, tune, t, v, i);
    set_real(s, t, v, i, 1);
    if (t == 23 || t == 24) for (int k = 0; k < 9; k++) store(s, tune, t, real_value(s, t, k), k);
    else if (is_gear(t)) { for (int g = 10; g < 16; g++) if (t_avail(s, g)) store(s, tune, g, real_value(s, g, 0), 0); }
    else if (t == 16 || t == 17) { int o = t == 17 ? 16 : 17; store(s, tune, o, real_value(s, o, 0), 0); }
}
/* RebuildCustomTunings 0x5b4780 on s->P from the bytes */
static void rebuild(Spec *s, const uint8_t *tune)
{
    static const uint8_t stock[TUNE_BYTES] = { [0 ... TUNE_BYTES - 1] = 100 };
    if (!tune) tune = stock;
    memset(s->base, 0, sizeof s->base);
    for (int t = 0; t < 23; t++) if (t_avail(s, t)) s->base[t][0] = real_value(s, t, 0);
    for (int t = 0; t < 23; t++) {
        if (!t_avail(s, t)) continue;
        float lo, hi;
        if (!tune_range(s, t, &lo, &hi, 0, 0)) continue;
        float v = (hi - lo) * byte_t(tune[t]) + s->base[t][0];
        if (is_gear(t)) set_real(s, t, v, 0, 0);
        else { v = store(s, NULL, t, v, 0); set_real(s, t, v, 0, 0); }
    }
    for (int t = 10; t < 16; t++) {
        if (!t_avail(s, t)) continue;
        float lo, hi;
        if (!tune_range(s, t, &lo, &hi, 0, 0)) continue;
        float v = store(s, NULL, t, (hi - lo) * byte_t(tune[t]) + s->base[t][0], 0);
        set_real(s, t, v, 0, 0);
    }
    float sum0 = 0.0f;
    for (int k = 0; k < 9; k++) sum0 += rf(s->P, 0x200 + 4 * k);
    for (int t = 23; t < 25; t++) {
        if (!t_avail(s, t)) continue;
        for (int k = 0; k < 9; k++) s->base[t][k] = real_value(s, t, k);
        for (int k = 0; k < 9; k++) {
            float lo, hi;
            if (!tune_range(s, t, &lo, &hi, k, 0)) continue;
            set_torque(s, t, (hi - lo) * byte_t(tune[tidx(t, k)]) + s->base[t][k], k, 0);
        }
    }
    float sum = 0.0f;
    for (int k = 0; k < 9; k++) sum += rf(s->P, 0x200 + 4 * k);
    if (fabsf(sum0 - sum) > 0.001f) {   /* FixTorqueCurve 0x59b3c0 */
        float d = (sum0 - sum) * 0.111111f;
        for (int k = 0; k < 9; k++) wf(s->P, 0x200 + 4 * k, rf(s->P, 0x200 + 4 * k) + d);
    }
}
static void spec_init(Spec *s, unsigned char *built, const unsigned char *raw, const PerfParts *pp)
{
    memset(s, 0, sizeof *s);
    s->P = built + PO; s->R = raw + PO; s->pp = pp;
    s->metric = g_opt.kmh == 1;
}
void tune_apply(unsigned char *built, const unsigned char *raw, const PerfParts *pp, const uint8_t *tune)
{
    if (!built || !raw || !pp || !g_ntrec) return;
    Spec s; spec_init(&s, built, raw, pp);
    int n = 0;
    for (int t = 0; t < 25; t++) n += t_avail(&s, t);
    if (!n) return;                                   /* GetNumActiveTunings == 0 */
    unsigned char keep[0x2b0];
    memcpy(keep, built + PO, sizeof keep);
    rebuild(&s, tune);
    for (int o = 0; o < 0x2b0; o += 4) {               /* a broken record never reaches the sim */
        float f = rf(built + PO, o);
        if (f != f) { memcpy(built + PO, keep, sizeof keep); printf("tuning: NaN at P+0x%x, tunings dropped\n", o); return; }
    }
}
int tune_setting_for_race(int qr)
{
    /* SetupPlayerCarsAndStuff 80119c88's eTuningSetting, the r4 of each caller:
       SetupCircuit 0, SetupShortTrack 3, SetupFreeRun 0, SetupDragRace 1,
       SetupDriftRace 2, SetupBurnout 2, SetupURL 4, SetupOutrun 0 */
    return qr == QR_DRAG ? 1 : qr == QR_DRIFT ? 2 : qr == QR_STREETX ? 3 : qr == QR_URL ? 4 : 0;
}

/* ================================================================== */
/* CarTuneManager + the screens                                        */
/* ================================================================== */
enum { SCR_NONE, SCR_SETTING, SCR_MAIN, SCR_MASTER, SCR_SLIDERS, SCR_GRAPH, SCR_NOS, SCR_CHART, SCR_RESULTS };
/* DynoRunStats (0x1b dwords): top speed, 0-60, 0-100, max power (kW) + rpm, max
   torque (kNm) + rpm, the redline P+0x1f8 and the torque curve P+0x200..0x220 */
typedef struct { float top, t60, t100, hp, hp_rpm, tq, tq_rpm, red, curve[9]; int ok; } DynoStats;

static struct {
    int active, entry, setting, on_dyno;
    unsigned char raw[N2_REC_SIZE], work[N2_REC_SIZE];
    PerfParts pp;
    uint8_t stored[TUNE_NSET][TUNE_BYTES];      /* +0x8e0 */
    uint8_t db[TUNE_NSET][TUNE_BYTES];          /* the car's (HaveAnySettingsChangedFromDatabase) */
    uint8_t temp[TUNE_BYTES];                   /* +0x9ad */
    int set_chosen[TUNE_NSET];
    /* screens */
    const char *root;
    int scr, sel, cat, row, top, pt;
    Hud *h, *dlg;
    int dlg_kind, dsel, quit_save;
    int types[16], ntypes;
    /* dyno */
    float dyno_t; int dyno_run, nruns, chart_nser;
    DynoStats cur, prev, first;
    /* DynoManager (PC 0x56d1a0..0x56e6e0): state (+4) 0 off / 1 shifting up
       to 3rd / 2 full throttle to the redline / 3 stopping / 4 end, the gear
       it wants (+8), the rpm Average (+0xc, 20 samples, mean +0x18), the
       StartDynoRun request (0x86462c), the run clock and the RacingCarStats
       0-60 / 0-100 timers (+8 / +0xc), the theoretical top speed */
    int dm_state, dm_target, dm_start, dm_n, dm_i;
    float dm_buf[20], dm_sum, dm_mean, dm_red, dm_t, dm_t60, dm_t100, dm_top;
} g_tt;

int tun_active(void) { return g_tt.active; }
int tun_setting(void) { return g_tt.active ? g_tt.setting : -1; }
const uint8_t *tun_temp(void) { return g_tt.temp; }
int tun_on_dyno(void) { return g_tt.on_dyno; }
void tun_set_on_dyno(int on) { g_tt.on_dyno = on; }
int tun_screen_up(void) { return g_tt.scr != SCR_NONE; }

static void tun_rebuild_work(void)
{
    perf_build(g_tt.raw, &g_tt.pp, g_tt.work);
    tune_apply(g_tt.work, g_tt.raw, &g_tt.pp, g_tt.temp);
}
static void tun_spec(Spec *s)
{
    /* the spec the screens edit: the parts-built record (the bases) with
       the temp tunings applied */
    perf_build(g_tt.raw, &g_tt.pp, g_tt.work);
    spec_init(s, g_tt.work, g_tt.raw, &g_tt.pp);
    rebuild(s, g_tt.temp);
}
void tun_begin(int entry, const unsigned char *raw, const PerfParts *pp, const uint8_t (*car_tune)[TUNE_BYTES])
{
    memset(&g_tt, 0, sizeof g_tt);
    g_tt.active = 1; g_tt.entry = entry; g_tt.setting = -1; g_tt.on_dyno = 1;   /* 803da110 = 1 */
    memcpy(g_tt.raw, raw, N2_REC_SIZE);
    g_tt.pp = *pp;
    for (int i = 0; i < TUNE_NSET; i++) {
        memcpy(g_tt.stored[i], car_tune ? car_tune[i] : g_tt.stored[i], TUNE_BYTES);
        if (!car_tune) memset(g_tt.stored[i], 100, TUNE_BYTES);
        memcpy(g_tt.db[i], g_tt.stored[i], TUNE_BYTES);
    }
    memset(g_tt.temp, 100, sizeof g_tt.temp);
    tun_rebuild_work();
    int n = 0; Spec s; spec_init(&s, g_tt.work, g_tt.raw, &g_tt.pp);
    for (int t = 0; t < 25; t++) n += t_avail(&s, t);
    int tuned = 0;
    for (int i = 0; i < TUNE_NSET; i++) for (int k = 0; k < TUNE_BYTES; k++) tuned += g_tt.stored[i][k] != 100;
    printf("tuning: BeginCarTuning (entry %d), %d tuning types available, %d stored values off stock\n", entry, n, tuned);
}
int tun_quit_save(void) { return g_tt.quit_save; }
int tun_changed(void)
{
    if (g_tt.setting >= 0) memcpy(g_tt.stored[g_tt.setting], g_tt.temp, TUNE_BYTES);
    return memcmp(g_tt.stored, g_tt.db, sizeof g_tt.db) != 0;
}
void tun_commit(uint8_t (*car_tune)[TUNE_BYTES])
{
    if (g_tt.setting >= 0) memcpy(g_tt.stored[g_tt.setting], g_tt.temp, TUNE_BYTES);   /* StoreTempSettings */
    if (car_tune) memcpy(car_tune, g_tt.stored, sizeof g_tt.stored);
    memcpy(g_tt.db, g_tt.stored, sizeof g_tt.db);
    printf("tuning: CommitCustomTuningsToFEDatabase\n");
}
void tun_close_screens(void) { hud_free(&g_tt.h); hud_free(&g_tt.dlg); g_tt.scr = SCR_NONE; }
void tun_end(void) { tun_close_screens(); g_tt.active = 0; g_tt.setting = -1; }
void tun_test_event(int *event, int *qr_type, int *laps)
{
    /* StartCarTuningRace 8011c904 */
    static const int ev[5] = { 4705, 4709, 4708, 4708, 4705 };
    static const int ty[5] = { QR_CIRCUIT, QR_DRAG, QR_DRIFT, QR_STREETX, QR_URL };
    int s = g_tt.setting < 0 ? 0 : g_tt.setting;
    *event = ev[s]; *qr_type = ty[s];
    *laps = s == 1 ? 1 : 10;          /* RaceParameters +0x60 (0x803fca60): SetupTuningDrag 1, the others 10 */
}

/* ---- the strips (Setting, MAIN, Master) ---- */
static const struct { uint32_t icon, label, header; } k_set[5] = {
    { 0xe9638d3eu, 0x93fdee74u, 0x5ae1d9e5u }, { 0xaaab31e9u, 0x35102edfu, 0x5b4967f0u },
    { 0x00119144u, 0xd7162cfau, 0xc476882bu }, { 0x13a4ceebu, 0x30da61e1u, 0xca169c60u },
    { 0x3f6495c5u, 0x75f903f4u, 0xcc76dee5u },
};
enum { MO_TUNE, MO_TEST, MO_DYNO, MO_MODE, MO_SAVE, MO_QUIT, MO_RESUME, MO_RESTART, MO_RETURN, MO_OPTIONS };
static const struct { uint32_t icon, label; int act; } k_main_dyno[] = {   /* SetupDynoPause 8017e0cc */
    { 0xd4060fc4u, 0xa34e13bdu, MO_TUNE }, { 0xf188098cu, 0xf62234efu, MO_TEST }, { 0xfe1e723eu, 0x436c91d4u, MO_DYNO },
    { 0xc2b2829cu, 0xffe08459u, MO_MODE }, { 0x73fe99b7u, 0x410258c3u, MO_SAVE }, { 0x4c9e34e6u, 0xde5cdfb2u, MO_QUIT },
}, k_main_run[] = {                                                           /* SetupTuningPause 8017e324 */
    { 0x12bb5ea2u, 0x105149cbu, MO_RESUME }, { 0xb295a6b6u, 0x80833fc2u, MO_RESTART }, { 0xd4060fc4u, 0xa34e13bdu, MO_TUNE },
    { 0xfe1e723eu, 0x18d5a136u, MO_RETURN }, { 0x520de4e3u, 0xb34993c9u, MO_OPTIONS }, { 0x4c9e34e6u, 0xde5cdfb2u, MO_QUIT },
};
static const struct { uint32_t icon, label; int t0, t1; } k_cat[7] = {   /* AddTuningCategory */
    { 0x178475e7u, 0xef3772c1u, 0, 8 }, { 0x01a29ffau, 0xe8da9e62u, 9, 15 }, { 0x214db7cfu, 0x7ed554d1u, 21, 22 },
    { 0xd6dc23eeu, 0x6741bf48u, 18, 20 }, { 0x502abeedu, 0x135bdd07u, 23, 23 }, { 0x06ef789cu, 0x5ae08536u, 24, 24 },
    { 0x9701bde4u, 0x135c04dau, 16, 17 },
};
static int g_main_n, g_main_run;
static int main_act(int i) { return g_main_run ? k_main_run[i].act : k_main_dyno[i].act; }

/* CareerSpeech::HandleTuningCategory 80189f3c: the highlighted category's
   FE_Dynoshop line (ChooseTuningCategory 0x0c407210, unlocked only) */
static int cat_avail(int c);
static void cat_speech(int c)
{
    static const int ev[7] = { 0x13, 0x14, 0x18, 0x19, 0x15, 0x16, 0x17 };
    if (c >= 0 && c < 7 && cat_avail(c)) career_speech_fe(ev[c]);
}
static void strip_title(void)
{
    if (!g_tt.h) return;
    uint32_t lab = 0;
    if (g_tt.scr == SCR_SETTING) {
        lab = k_set[g_tt.sel].label;
        hud_screen_label_h(g_tt.h, 0x95ad096bu, k_set[g_tt.sel].header);   /* RefreshHeader 8017e574 */
    } else if (g_tt.scr == SCR_MAIN) lab = g_main_run ? k_main_run[g_tt.sel].label : k_main_dyno[g_tt.sel].label;
    else if (g_tt.scr == SCR_MASTER) lab = k_cat[g_tt.sel].label;
    hud_screen_strip_select(g_tt.h, g_tt.sel);
    hud_screen_label(g_tt.h, "ICON_TITLE", lab);
}
static int cat_avail(int c)   /* IsTuningCategoryAvailable 8017d2d8 */
{
    Spec s; spec_init(&s, g_tt.work, g_tt.raw, &g_tt.pp);
    for (int t = k_cat[c].t0; t <= k_cat[c].t1; t++) if (t_avail(&s, t)) return 1;
    return 0;
}
static int open_pkg(const char *fng)
{
    tun_close_screens();
    g_tt.h = hud_init_screen(g_tt.root, BUN, fng);
    if (!g_tt.h) { printf("tuning: %s not loaded\n", fng); return 0; }
    hud_screen_hide(g_tt.h, "EVENT HANDLER", 1);
    hud_screen_hide(g_tt.h, "Event Handler", 1);
    return 1;
}
static void open_setting(void)
{
    if (!open_pkg("UI_PerformanceTuning_Setting.fng")) return;
    uint32_t keys[5];
    for (int i = 0; i < 5; i++) keys[i] = k_set[i].icon;
    int got = hud_screen_strip(g_tt.h, g_tt.root, "GLOBAL/GLOBALB.BUN", keys, 5);
    g_tt.scr = SCR_SETTING; g_tt.sel = g_tt.setting < 0 ? 0 : g_tt.setting;
    strip_title();
    hud_screen_script_all(g_tt.h, "FadeIn");
    career_speech_fe(0x1b);   /* ChooseTuningSetting's first tick (+0x170, GC 8017e898) */
    printf("tuning: UI_PerformanceTuning_Setting (%d of 5 icons)\n", got);
}
static void open_main(void)
{
    if (!open_pkg("UI_PerformanceDyno_MAIN.fng")) return;
    g_main_run = !g_tt.on_dyno;
    g_main_n = 6;
    uint32_t keys[6];
    for (int i = 0; i < 6; i++) keys[i] = g_main_run ? k_main_run[i].icon : k_main_dyno[i].icon;
    int got = hud_screen_strip(g_tt.h, g_tt.root, "GLOBAL/GLOBALB.BUN", keys, 6);
    /* the test-run icons (Resume / Restart / Options / Quit) are InGameCommon's */
    got += hud_screen_strip_more(g_tt.h, g_tt.root, "GLOBAL/InGameCommon.bun", keys, 6);
    for (int i = 0; i < 6; i++)   /* Save Profile only when something changed (8017e0cc) */
        hud_screen_strip_lock(g_tt.h, i, main_act(i) == MO_SAVE && !tun_changed());
    if (g_tt.setting >= 0) hud_screen_label_h(g_tt.h, 0x242217c1u, k_set[g_tt.setting].header);
    hud_screen_label_h(g_tt.h, 0x42adb44cu, 0x8a24d87au);   /* "Dyno Shop" */
    g_tt.scr = SCR_MAIN; g_tt.sel = 0;
    strip_title();
    hud_screen_script_all(g_tt.h, "FadeIn");
    printf("tuning: UI_PerformanceDyno_MAIN (%s, %d icons)\n", g_main_run ? "test run" : "dyno", got);
}
static void open_master(void)
{
    if (!open_pkg("UI_PerformanceTuning_Master.fng")) return;
    uint32_t keys[7];
    for (int i = 0; i < 7; i++) keys[i] = k_cat[i].icon;
    int got = hud_screen_strip(g_tt.h, g_tt.root, "GLOBAL/GLOBALB.BUN", keys, 7);
    int first = -1;
    for (int i = 0; i < 7; i++) { int a = cat_avail(i); hud_screen_strip_lock(g_tt.h, i, !a); if (a && first < 0) first = i; }
    if (g_tt.setting >= 0) hud_screen_label_h(g_tt.h, 0x95ad096bu, k_set[g_tt.setting].header);
    g_tt.scr = SCR_MASTER; g_tt.sel = g_tt.cat >= 0 && g_tt.cat < 7 && cat_avail(g_tt.cat) ? g_tt.cat : first < 0 ? 0 : first;
    strip_title();
    cat_speech(g_tt.sel);
    hud_screen_script_all(g_tt.h, "FadeIn");
    printf("tuning: UI_PerformanceTuning_Master (%d icons)\n", got);
}

/* ---- the sliders (TuningSlidersMenu) ---- */
static const uint32_t k_name[24] = {
    0xfe22b6a2u, 0xefdb2567u, 0xb6d444b4u, 0x0b161ec3u, 0x93b7d1a3u, 0x87c86448u, 0xff0cbfd5u, 0x534e99e4u,
    0x2430e6beu, 0x6a71e798u, 0x7d822b64u, 0x7d822b65u, 0x7d822b66u, 0x7d822b67u, 0x7d822b68u, 0x7d822b69u,
    0x55c60293u, 0xfbbdd223u, 0x2308f06cu, 0x3988faefu, 0xe4eebd70u, 0xd99fc2e4u, 0x21d83e05u, 0xc8b8fbf1u,
};
static uint32_t name_hash(int t) { return t < 24 ? k_name[t] : 0xfe82fde0u; }
static void end_labels(int t, uint32_t *l, uint32_t *r)
{
    *l = *r = 0;
    if (t == 3 || t == 7) { *l = 0x26033125u; *r = 0xe66708f3u; }
    else if (t <= 6) { *l = 0xe66d2a8fu; *r = 0xe666e852u; }
    else if (t == 8) { *l = 0xe6692b0au; *r = 0xe669e1c6u; }
    else if (t == 9) { *l = 0xcebfaa36u; *r = 0x95a7073du; }
    else if (t == 18) { *l = 0x001c0cc9u; *r = 0x02cbdc28u; }
    else if (t >= 19 && t <= 22) { *l = 0xe66f30dbu; *r = 0x36c30e70u; }
}
static uint32_t unit_hash(const Spec *s, int t)
{
    if (t == 3 || t == 7) return s->metric ? 0x5e1f61eeu : 0x6382ece0u;
    if (is_gear(t)) return s->metric ? 0xcbbc8bf8u : 0x8569ab44u;
    if (t == 17 || t == 23 || t == 24) return s->metric ? 0x5b81d613u : 0xacb8dff3u;
    return 0;
}
#define ROWS 6
#define ROW_H 44.0f
static void sliders_list(int cat)
{
    static const int o_susp[] = { 3, 7, 0, 4, 1, 5, 2, 6, 8 }, o_drv[] = { 10, 11, 12, 13, 14, 15, 9 },
                     o_aero[] = { 21, 22 }, o_tire[] = { 19, 20, 18 };
    const int *o = cat == 0 ? o_susp : cat == 1 ? o_drv : cat == 2 ? o_aero : o_tire;
    int n = cat == 0 ? 9 : cat == 1 ? 7 : cat == 2 ? 2 : 3;
    Spec s; spec_init(&s, g_tt.work, g_tt.raw, &g_tt.pp);
    g_tt.ntypes = 0;
    for (int i = 0; i < n; i++) if (t_avail(&s, o[i])) g_tt.types[g_tt.ntypes++] = o[i];
}
static void sliders_refresh(void)
{
    Hud *h = g_tt.h;
    if (!h) return;
    Spec s; tun_spec(&s);
    float tx, ty, tw, th, dx, dy, dw, dh;
    if (!hud_screen_obj_box(h, hud_screen_find_h(h, 0xa753c46cu), &tx, &ty, &tw, &th)) { tx = -151; ty = -46; tw = 251; }
    if (!hud_screen_obj_box(h, hud_screen_find_h(h, 0xc128b184u), &dx, &dy, &dw, &dh)) { dx = 131; dy = -45; dw = 259; }
    (void)th; (void)dh;
    if (g_tt.row < g_tt.top) g_tt.top = g_tt.row;
    if (g_tt.row >= g_tt.top + ROWS) g_tt.top = g_tt.row - ROWS + 1;
    for (int k = 0; k < 16; k++) {
        int idx = g_tt.top + k, vis = k < ROWS && idx < g_tt.ntypes;
        int on = vis && idx == g_tt.row;
        float y = ty + (float)k * ROW_H;
        int ob[] = { hud_screen_find_h(h, 0xb2980ba8u + k), hud_screen_find_h(h, 0x9b483b01u + k),
                     hud_screen_find_h(h, 0x654668ecu + k), hud_screen_find_h(h, 0x7b2dd6dbu + k),
                     hud_screen_find_h(h, 0x7bcd6704u + k), hud_screen_find_h(h, 0x7c51b6d7u + k),
                     hud_screen_find_h(h, 0x8e435402u + k), hud_screen_find_h(h, 0x8e435843u + k),
                     hud_screen_find_h(h, 0x4e4963ddu + k), hud_screen_find_h(h, 0x289c21feu + k),
                     hud_screen_find_h(h, 0x66e29936u + k), hud_screen_find_h(h, 0x7770beffu + k),
                     hud_screen_find_h(h, 0x77abeb28u + k) };
        for (int j = 0; j < (int)(sizeof ob / sizeof ob[0]); j++) hud_screen_obj_hide(h, ob[j], !vis);
        if (!vis) continue;
        int t = g_tt.types[idx];
        float lo, hi, cur = remap(&s, t, real_value(&s, t, 0), 1);
        tune_range(&s, t, &lo, &hi, 0, 1);
        float f = hi > lo ? (cur - lo) / (hi - lo) : 0.5f;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        /* the row: the backing group, name at TITLE_MASTER, value and slider in DATA_MASTER (layout GUESS) */
        hud_screen_obj_pos(h, ob[10], -4.0f, y + 1.0f);
        hud_screen_obj_pos(h, ob[0], tx - tw * 0.5f + 8.0f, y);
        char txt[96];
        uint32_t u = unit_hash(&s, t);
        if (u) snprintf(txt, sizeof txt, "%s (%s)", hud_screen_string(h, name_hash(t)), hud_screen_string(h, u));
        else snprintf(txt, sizeof txt, "%s", hud_screen_string(h, name_hash(t)));
        hud_screen_text_h(h, 0xb2980ba8u + k, txt);
        snprintf(txt, sizeof txt, is_gear(t) ? "%2.1f" : "%2.2f", cur);
        float bx0 = dx - dw * 0.5f + 70.0f, bw = dw - 92.0f;
        hud_screen_obj_pos(h, ob[1], dx - dw * 0.5f + 10.0f, y);
        hud_screen_text_h(h, 0x9b483b01u + k, txt);
        hud_screen_obj_pos(h, ob[2], bx0 + bw * 0.5f, y + 6.0f);
        hud_screen_obj_size(h, ob[2], bw, 12.0f);
        hud_screen_obj_pos(h, ob[3], bx0 + bw * f, y + 6.0f);
        hud_screen_obj_pos(h, ob[8], bx0 + bw * f * 0.5f, y + 6.0f);
        hud_screen_obj_size(h, ob[8], bw * f, 5.75f);
        hud_screen_obj_hide(h, ob[9], 1); hud_screen_obj_hide(h, ob[11], 1); hud_screen_obj_hide(h, ob[12], 1);
        hud_screen_obj_pos(h, ob[4], bx0 - 12.0f, y + 6.0f);
        hud_screen_obj_pos(h, ob[5], bx0 + bw + 12.0f, y + 6.0f);
        hud_screen_obj_hide(h, ob[4], !on); hud_screen_obj_hide(h, ob[5], !on);
        uint32_t l, r; end_labels(t, &l, &r);
        hud_screen_obj_hide(h, ob[6], !l); hud_screen_obj_hide(h, ob[7], !r);
        if (l) { hud_screen_obj_pos(h, ob[6], bx0, y - 8.0f); hud_screen_label_h(h, 0x8e435402u + k, l); }
        if (r) { hud_screen_obj_pos(h, ob[7], bx0 + bw - 24.0f, y - 8.0f); hud_screen_label_h(h, 0x8e435843u + k, r); }
        uint32_t col = on ? 0xffcaf48au : 0xffffffffu;
        hud_screen_obj_color(h, ob[0], col); hud_screen_obj_color(h, ob[1], col);
    }
    hud_screen_hide_h(h, 0x444969feu, g_tt.top + ROWS >= g_tt.ntypes);
    hud_screen_hide_h(h, 0x444969fdu, g_tt.top == 0);
}
static void open_sliders(int cat)
{
    Spec s; spec_init(&s, g_tt.work, g_tt.raw, &g_tt.pp);
    int drive = cat == 1 && t_avail(&s, 10);   /* SetupTuningData 8017f1f4 */
    if (!open_pkg(drive ? "UI_PerformanceTuning_Drivetrain.fng" : "UI_PerformanceTuning_Sliders.fng")) return;
    sliders_list(cat);
    g_tt.scr = SCR_SLIDERS; g_tt.row = 0; g_tt.top = 0;
    for (uint32_t k = 0; k < 12; k++) hud_screen_hide_h(g_tt.h, 0x3409d780u + k, 1);   /* tip_text */
    for (uint32_t k = 0; k < 3; k++) hud_screen_hide_h(g_tt.h, 0xb544c7b0u + k, 1);
    hud_screen_label_h(g_tt.h, 0x42adb44cu, k_cat[cat].label);
    sliders_refresh();
    hud_screen_script_all(g_tt.h, "FadeIn");
    printf("tuning: %s, %d sliders\n", drive ? "UI_PerformanceTuning_Drivetrain" : "UI_PerformanceTuning_Sliders", g_tt.ntypes);
}
/* one slider step (GetIncValue 0x522560: a 30th of the tuneable range) */
static int slider_step(int t, int i, int dir)
{
    Spec s; tun_spec(&s);
    float lo, hi;
    if (!tune_range(&s, t, &lo, &hi, i, 1)) return 0;
    float cur = remap(&s, t, real_value(&s, t, i), 1), step = (hi - lo) * 0.0333333f;
    float nv = fmax2(lo, fmin2(hi, cur + step * (float)dir));
    if (fabsf(nv - cur) < 1e-6f) return 0;
    set_value(&s, g_tt.temp, t, nv, i, 1);
    return 1;
}

/* ---- the torque graph (TuningGraphMenu / TorqueGraph) ---- */
static void graph_refresh(void)
{
    Hud *h = g_tt.h;
    if (!h) return;
    Spec s; tun_spec(&s);
    int t = g_tt.cat == 4 ? 23 : 24;
    float r0 = rf(s.P, 0x1f0), r1 = rf(s.P, 0x1f8);
    for (int i = 0; i < 9; i++) {
        float lo = 0, hi = 0, v = remap(&s, t, real_value(&s, t, i), 1);
        tune_range(&s, t, &lo, &hi, i, 1);
        float rpm = (r1 - r0) * (float)i * 0.125f + r0, x = -231.0f + rpm * 0.0504f;   /* x axis 0..9 x1000 rpm */
        struct { uint32_t o; float val; } bars[3] = { { 0xa11a9316u + i, v }, { 0x520c0557u + i, lo }, { 0x520c0998u + i, hi } };
        for (int b = 0; b < 3; b++) {
            int o = hud_screen_find_h(h, bars[b].o);
            float ht = fmax2(1.0f, fminf(bars[b].val, 500.0f) / 500.0f * 239.0f);
            hud_screen_obj_pos(h, o, x, 158.0f - ht * 0.5f);
            hud_screen_obj_size(h, o, b ? 30.0f : 22.0f, ht);
        }
        int c = hud_screen_find_h(h, 0x57d3f016u + i);
        float ht = fmax2(1.0f, fminf(v, 500.0f) / 500.0f * 239.0f);
        hud_screen_obj_pos(h, c, x, 158.0f - ht);
        hud_screen_obj_hide(h, c, i != g_tt.pt);
    }
    for (uint32_t k = 0; k < 10; k++) {   /* the rpm labels under the axis */
        int o = hud_screen_find_h(h, 0x70f8df36u + k);
        hud_screen_obj_pos(h, o, -231.0f + 50.4f * (float)k, 170.0f);
    }
    for (uint32_t k = 0; k < 10; k++) { hud_screen_hide_h(h, 0x10da171eu + k, 1); hud_screen_hide_h(h, 0x2498423du + k, 1); }
    hud_screen_hide_h(h, 0x938d3cc8u, 1); hud_screen_hide_h(h, 0xb7a08a0du, 1);
    hud_screen_hide_h(h, 0x938cb067u, 1); hud_screen_hide_h(h, 0xc46cf606u, 1);
    char txt[96];
    snprintf(txt, sizeof txt, "%s (%s)", hud_screen_string(h, name_hash(t)), hud_screen_string(h, unit_hash(&s, t)));
    hud_screen_text_h(h, 0x42adb44cu, txt);
}
static void open_graph(int cat)
{
    if (!open_pkg("UI_PerformanceTuning_Graph.fng")) return;
    g_tt.scr = SCR_GRAPH; g_tt.pt = 4;
    for (uint32_t k = 0; k < 12; k++) hud_screen_hide_h(g_tt.h, 0x3409d780u + k, 1);
    for (uint32_t k = 0; k < 3; k++) hud_screen_hide_h(g_tt.h, 0xb544c7b0u + k, 1);
    hud_screen_label_h(g_tt.h, 0xd69c9b64u, 0xb672f15au);   /* "Maximum" */
    hud_screen_label_h(g_tt.h, 0x6b6d137cu, 0x0000c3e5u);   /* GUESS: the legend's min line */
    (void)cat;
    graph_refresh();
    hud_screen_script_all(g_tt.h, "FadeIn");
    printf("tuning: UI_PerformanceTuning_Graph (%s)\n", g_tt.cat == 4 ? "ECU" : "Turbo");
}
/* ---- nitrous (NOSGraph) ---- */
static void nos_refresh(void)
{
    Hud *h = g_tt.h;
    if (!h) return;
    Spec s; tun_spec(&s);
    char txt[96];
    float p = remap(&s, 17, real_value(&s, 17, 0), 1), d = remap(&s, 16, real_value(&s, 16, 0), 1), lo = 0, hi = 1;
    snprintf(txt, sizeof txt, "%s: %2.2f", hud_screen_string(h, 0x824bc854u), p);
    hud_screen_text_h(h, 0x9b483b01u, txt);
    snprintf(txt, sizeof txt, "%s: %2.2f", hud_screen_string(h, 0x67e53439u), d);
    hud_screen_text_h(h, 0x9b483b02u, txt);
    tune_range(&s, 17, &lo, &hi, 0, 1);
    float f = hi > lo ? (p - lo) / (hi - lo) : 0.5f;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    hud_screen_obj_pos(h, hud_screen_find_h(h, 0x7b2dd6dbu), 10.0f - 128.0f + 256.0f * f, 176.0f);
    int fill = hud_screen_find_h(h, 0x4e4963ddu);
    hud_screen_obj_pos(h, fill, 10.0f - 128.0f + 128.0f * f, 176.0f);
    hud_screen_obj_size(h, fill, 256.0f * f, 5.75f);
    hud_screen_hide_h(h, 0x289c21feu, 1); hud_screen_hide_h(h, 0x77abeb28u, 1); hud_screen_hide_h(h, 0x7770beffu, 1);
    /* the graph: duration (0..12 s across 320) by power (line height) */
    float w = fminf(d, 12.0f) / 12.0f * 320.0f, ht = f * 200.0f;
    int bar = hud_screen_find_h(h, 0x11495cd6u);
    hud_screen_obj_pos(h, bar, -149.0f + w * 0.5f, 120.0f - ht);
    hud_screen_obj_size(h, bar, w, 3.0f);
}
static void open_nos(void)
{
    if (!open_pkg("UI_PerformanceTuning_NOS.fng")) return;
    g_tt.scr = SCR_NOS;
    nos_refresh();
    hud_screen_script_all(g_tt.h, "FadeIn");
    printf("tuning: UI_PerformanceTuning_NOS\n");
}
static void open_category(int cat)
{
    g_tt.cat = cat;
    if (cat <= 3) open_sliders(cat);
    else if (cat <= 5) open_graph(cat);
    else open_nos();
}

/* ---- the dyno run (DynoManager / DynoRunChartScreen) ---- */
static float torque_at(const unsigned char *P, float rpm)
{
    float r0 = rf(P, 0x1f0), r1 = rf(P, 0x1f8);
    if (r1 <= r0) return rf(P, 0x200);
    float x = (rpm - r0) / (r1 - r0) * 8.0f;
    if (x <= 0) return rf(P, 0x200);
    if (x >= 8) return rf(P, 0x220);
    int i = (int)x; float f = x - (float)i;
    return rf(P, 0x200 + 4 * i) * (1 - f) + rf(P, 0x204 + 4 * i) * f;
}
/* RecordDynoRun (PC 523330): the top speed, 0-60 / 0-100, max power and
   torque with their rpm */
static void dyno_measure(DynoStats *st)
{
    const unsigned char *P = g_tt.work + PO;
    memset(st, 0, sizeof *st);
    float r0 = rf(P, 0x1f0), r1 = rf(P, 0x1f8), red = rf(P, 0x1f8);
    for (float rpm = r0; rpm < r1; rpm += 50.0f) {   /* GetMaxHP / GetMaxTorque (PC 0x5b56e0 / 0x5b5810) */
        float tq = torque_at(P, rpm), kw = tq * 1000.0f * rpm * 0.000104712f;
        if (tq > st->tq) { st->tq = tq; st->tq_rpm = rpm; }
        if (kw > st->hp) { st->hp = kw; st->hp_rpm = rpm; }
    }
    st->red = red;                                 /* the curve SetupGraph draws for a previous run */
    memcpy(st->curve, P + 0x200, sizeof st->curve);
    /* the top speed: RidePhysicsInfo::GetTheoreticalTopSpeed (PC 0x5b5930, n2sim's
       vtop); 0-60 / 0-100: the RacingCarStats timers of the roller run (0 = not
       reached, the results hide it) */
    st->top = g_tt.dm_top; st->t60 = g_tt.dm_t60; st->t100 = g_tt.dm_t100;
    st->ok = 1;
}
/* ShowMaxStats (PC 0x54a620): "%$3.1f %s^@%$d rpm" (0x43aeef78 / 0x76f2bc60), power in
   kW (0x5b81d5ba) or x1.34102209 bhp (0xcbbc6512), torque Nm (0x5b81d613) or x0.737562
   ft-lbs (0xacb8dff3), the rpm + 0.5 truncated */
static void fmt_power(char *o, int cap, const Hud *h, float kw, float rpm)
{
    int metric = g_opt.kmh == 1;
    snprintf(o, cap, "%.1f %s\n@%d rpm", metric ? kw : kw * 1.34102209f,
             hud_screen_string(h, metric ? 0x5b81d5bau : 0xcbbc6512u), (int)(rpm + 0.5f));
}
static void fmt_torque(char *o, int cap, const Hud *h, float kNm, float rpm)
{
    int metric = g_opt.kmh == 1;
    float v = kNm * 1000.0f * (metric ? 1.0f : 0.737562f);
    snprintf(o, cap, "%.1f %s\n@%d rpm", v, hud_screen_string(h, metric ? 0x5b81d613u : 0xacb8dff3u), (int)(rpm + 0.5f));
}
static void chart_refresh(void)
{
    Hud *h = g_tt.h;
    if (!h) return;
    char a[96];
    hud_screen_label_h(h, 0xd69c9b64u, 0x0f87545du);   /* "Max Torque:" */
    hud_screen_label_h(h, 0x6b6d137cu, 0x52d837f5u);   /* "Max Power:" */
    if (!g_tt.dyno_run && g_tt.cur.ok) {
        fmt_torque(a, sizeof a, h, g_tt.cur.tq, g_tt.cur.tq_rpm); hud_screen_text_h(h, 0xd7d6db1du, a);
        fmt_power(a, sizeof a, h, g_tt.cur.hp, g_tt.cur.hp_rpm); hud_screen_text_h(h, 0x11ae2235u, a);
    } else {   /* ShowMaxStats runs at the run's end (progress 1) */
        hud_screen_text_h(h, 0xd7d6db1du, ""); hud_screen_text_h(h, 0x11ae2235u, "");
    }
    hud_screen_label_h(h, 0x42adb44cu, 0xac7bd52au);   /* "Dyno Chart" */
}
/* SetupGraph (PC 0x53d0a0): the axes. y max: power 600 bhp / 447.41992 kW, torque
   600 ft-lbs / 813.49077 Nm; six labels each side (objects "%d" hashed on 0xeec8134f /
   0x22b57eb7, 1..6) at k * (int)(max * 0.2); titles "Power ( %s )" 0x06285e80 and
   "Torque ( %s )" 0x38d548d2 */
static float chart_ymax(int torque) { int m = g_opt.kmh == 1; return torque ? (m ? 813.49077f : 600.0f) : (m ? 447.41992f : 600.0f); }
static void chart_axes(void)
{
    Hud *h = g_tt.h;
    if (!h) return;
    int m = g_opt.kmh == 1;
    int sp = (int)(chart_ymax(0) * 0.2f), st = (int)(chart_ymax(1) * 0.2f);
    char a[96];
    for (uint32_t k = 0; k < 6; k++) {
        snprintf(a, sizeof a, "%d", (int)k * sp); hud_screen_text_h(h, 0xc7ca7d60u + k, a);
        snprintf(a, sizeof a, "%d", (int)k * st); hud_screen_text_h(h, 0x796555c8u + k, a);
    }
    snprintf(a, sizeof a, hud_screen_string(h, 0x06285e80u), hud_screen_string(h, m ? 0x5b81d5bau : 0xcbbc6512u));
    hud_screen_text_h(h, 0xe2a8254cu, a);
    snprintf(a, sizeof a, hud_screen_string(h, 0x38d548d2u), hud_screen_string(h, m ? 0x5b81d613u : 0xacb8dff3u));
    hud_screen_text_h(h, 0x2345f69eu, a);
}
/* the FEGraph's series: per run shown (this one; the first run's too after the
   first run), torque (0xffaa8000) then power (0xff1a95af), the first run's at
   alpha 0x82; 32 points from idle P+0x1f0 by (redline - idle) / 31 up to the
   redline, x 0..10000 rpm, revealed by the progress. The FEGraph's rect is
   DynoChartTopLeft (79,73) + DynoChartSize (486,312) (PC 0x7fa3c0 / 0x7fa3c8, GC
   8046fdbc / 8046fdc4; Draw__7FEGraph 8015da1c: min at pos + (0, size.y), max at
   pos + (size.x, 0)) in 640x480 screen space = FEng x -241..245, y 145..-167 */
static float g_dm_prog;   /* GetDynoProgress, the FEGraph's reveal (*(0x838564)+0x34) */
static void chart_draw(const RProg *rp, GpuMesh *quad, int W, int H)
{
    const unsigned char *P = g_tt.work + PO;
    float idle = rf(P, 0x1f0);
    float prog = g_tt.dyno_run ? g_dm_prog : 1.0f;
    int m = g_opt.kmh == 1;
    int nser = g_tt.chart_nser;
    for (int s = nser - 1; s >= 0; s--) {
        const float *curve; float red;
        DynoStats live;
        if (s == 0) {
            memcpy(live.curve, P + 0x200, sizeof live.curve); live.red = rf(P, 0x1f8);
            curve = live.curve; red = live.red;
        } else { curve = g_tt.first.curve; red = g_tt.first.red; }   /* slot 2: the first run */
        if (red <= idle) continue;
        float step = (red - idle) * 0.032258064f, pt[2][2 * 40];
        int n = 0;
        for (float rpm = idle; rpm <= red + 0.01f && n < 40; rpm += step, n++) {
            float x = (rpm - idle) / (red - idle) * 8.0f, tq;
            int i = (int)x; if (i > 7) i = 7; if (i < 0) i = 0;
            tq = curve[i] + (curve[i + 1] - curve[i]) * (x - (float)i);
            float vt = tq * 1000.0f * (m ? 1.0f : 0.737562f);
            float vp = tq * 1000.0f * rpm * 0.000104712f * (m ? 1.0f : 1.34102209f);
            float fx = -241.0f + rpm / 10000.0f * 486.0f;
            pt[0][2 * n] = pt[1][2 * n] = fx;
            pt[0][2 * n + 1] = 145.0f - fminf(vt / chart_ymax(1), 1.0f) * 312.0f;
            pt[1][2 * n + 1] = 145.0f - fminf(vp / chart_ymax(0), 1.0f) * 312.0f;
        }
        int show = s == 0 ? (int)(prog * (float)n + 0.999f) : n;
        uint32_t al = s ? 0x82000000u : 0xff000000u;
        hud_fe_lines(rp, quad, W, H, pt[0], show, al | 0xaa8000u, 2.5f);
        hud_fe_lines(rp, quad, W, H, pt[1], show, al | 0x1a95afu, 2.5f);
    }
}
static void open_chart(void)
{
    if (!open_pkg("UI_PerformanceDyno_Chart.fng")) return;
    g_tt.scr = SCR_CHART;
    g_tt.dyno_t = 0; g_tt.dyno_run = 1;   /* StartDynoRun (done by tun_dyno_frame) */
    g_tt.dm_start = 1; g_tt.dm_state = 0; g_dm_prog = 0;
    g_tt.chart_nser = (g_tt.nruns > 0) + 1;   /* SetupGraph: the first run's curves too after one */
    tun_rebuild_work();
    chart_axes();
    chart_refresh();
    hud_screen_script_all(g_tt.h, "FadeIn");
    printf("tuning: UI_PerformanceDyno_Chart -- dyno run\n");
}
/* DynoRunResultsScreen::Init (PC 0x54a970): "%s DATA_1..5"; the 0-60 / 0-100
   timers "%s %s" of Timer::PrintToString(0) (hidden when 0 / 0x7fffffff), the
   top speed "%3.1f %s" x 2.237 mph / 3.6 km/h and torque / power hidden when <= 0
   (port: hidden = empty text) */
static void results_col(Hud *h, uint32_t base, const DynoStats *st)
{
    char a[96], t[16];
    int metric = g_opt.kmh == 1;
    for (uint32_t k = 0; k < 5; k++) hud_screen_text_h(h, base + k, "");
    if (!st->ok) return;
    if (st->t60 > 0) { hud_time_fmt(t, (int)(st->t60 * 4000.0f), 0); snprintf(a, sizeof a, "%s %s", t, hud_screen_string(h, 0x29f4bf03u)); hud_screen_text_h(h, base, a); }
    if (st->t100 > 0) { hud_time_fmt(t, (int)(st->t100 * 4000.0f), 0); snprintf(a, sizeof a, "%s %s", t, hud_screen_string(h, 0x29f4bf03u)); hud_screen_text_h(h, base + 1, a); }
    if (st->top > 0) {
        snprintf(a, sizeof a, "%3.1f %s", st->top * (metric ? 3.6f : 2.237f), hud_screen_string(h, metric ? 0xe74b7e13u : 0xe74b86f8u));
        hud_screen_text_h(h, base + 2, a);
    }
    if (st->tq > 0) { fmt_torque(a, sizeof a, h, st->tq, st->tq_rpm); hud_screen_text_h(h, base + 3, a); }
    if (st->hp > 0) { fmt_power(a, sizeof a, h, st->hp, st->hp_rpm); hud_screen_text_h(h, base + 4, a); }
}
static void open_results(void)
{
    if (!open_pkg("UI_PerformanceDyno_Results.fng")) return;
    g_tt.scr = SCR_RESULTS;
    Hud *h = g_tt.h;
    hud_screen_label_h(h, 0x42adb44cu, 0x37b7246au);   /* "Dyno Results" */
    hud_screen_label_h(h, 0x199c9631u, 0x8e566831u);   /* Current */
    hud_screen_label_h(h, 0x199c9632u, 0xb39c1f0bu);   /* Previous */
    hud_screen_label_h(h, 0x199c9633u, 0xcb5e7e4au);   /* First Run */
    hud_screen_label_h(h, 0x566bf78bu, 0x3712cc32u);   /* Top Speed */
    hud_screen_label_h(h, 0x566bf78cu, 0x6887e3b4u);   /* Max Torque */
    hud_screen_label_h(h, 0x566bf78du, 0x26e0b3ccu);   /* Max Power */
    results_col(h, 0xed5bed0bu, &g_tt.cur);
    results_col(h, 0xbf527c45u, &g_tt.prev);
    results_col(h, 0xb7554520u, &g_tt.first);
    hud_screen_script_all(h, "FadeIn");
    printf("tuning: UI_PerformanceDyno_Results (run %d)\n", g_tt.nruns);
}
static void record_run(void)
{
    /* RecordDynoRun (PC 0x523330): slots 0x83a65c current / 0x83a6c8 previous /
       0x83a734 first run; the first run goes to "first", later ones shift the
       current one (when its power > 0) to "previous" (CarTuneManager::RecordDynoRun
       = a plain copy), then the run is the current one */
    DynoStats run;
    dyno_measure(&run);
    if (!g_tt.nruns) g_tt.first = run;
    else if (g_tt.cur.hp > 0.0f) g_tt.prev = g_tt.cur;
    g_tt.cur = run;
    g_tt.nruns++;
    printf("tuning: dyno run %d -- %.1f kW @%d, %.3f kNm @%d, 0-60 %.2f s, top %.1f m/s\n", g_tt.nruns,
           g_tt.cur.hp, (int)g_tt.cur.hp_rpm, g_tt.cur.tq, (int)g_tt.cur.tq_rpm, g_tt.cur.t60, g_tt.cur.top);
}

/* ---- DynoManager (PC 0x56d340 StartDynoRun / 0x56e6e0 Update / 0x56d440
   GetDynoProgress / 0x56e680 EndDynoRun; GC 80191bfc / 80191e2c / 801920fc) ---- */
static void dm_record(float x)   /* Average::Record (GC 801a571c): mean of the last 20 */
{
    if (g_tt.dm_n < 20) g_tt.dm_n++;
    g_tt.dm_sum += x - g_tt.dm_buf[g_tt.dm_i];
    g_tt.dm_buf[g_tt.dm_i] = x;
    g_tt.dm_mean = g_tt.dm_sum / (float)g_tt.dm_n;
    if (++g_tt.dm_i >= 20) g_tt.dm_i = 0;
}
static float dm_progress(int gear)   /* mean rpm / (redline - 200), [0, 1] */
{
    if (g_tt.dm_state == 2 && gear != 4) return 0.0f;
    float d = g_tt.dm_red - 200.0f, p = d > 0 ? g_tt.dm_mean / d : 0.0f;
    return p < 0 ? 0 : p > 1 ? 1 : p;
}
static void dm_end(int now)   /* EndDynoRun(now): 1 = the car let go, 0 = brake it to a stop first */
{
    if (now) g_tt.dm_state = 0; else if (g_tt.dm_state) g_tt.dm_state = 3;
}
int tun_dyno_frame(float dt, float rpm, int gear, float speed, float vtop, TunDynoCtl *o)
{
    int f = 0;
    memset(o, 0, sizeof *o);
    o->gear_req = -1;
    if (g_tt.dm_start) {   /* the chart's ctor (GC 801826e0) -> StartDynoRun */
        g_tt.dm_start = 0;
        g_tt.dm_state = 1; g_tt.dm_target = 2;   /* request -1, then 2 (1st) */
        memset(g_tt.dm_buf, 0, sizeof g_tt.dm_buf);
        g_tt.dm_n = g_tt.dm_i = 0; g_tt.dm_sum = g_tt.dm_mean = 0;
        g_tt.dm_red = rf(g_tt.work + PO, 0x1f8);
        g_tt.dm_t = g_tt.dm_t60 = g_tt.dm_t100 = 0;   /* RacingCarStats::Reset, race clock 0 */
        f |= TT_DY_START;
        printf("tuning: StartDynoRun (redline %.0f)\n", g_tt.dm_red);
    }
    if (!g_tt.dm_state) return f;
    o->on = 1;
    g_tt.dm_t += dt;
    float mph = speed * 2.237f;   /* RacingCarStats::Update (PC 0x5f7410): car +0x42c x 2.237 */
    if (!g_tt.dm_t60 && mph >= 60.0f) g_tt.dm_t60 = g_tt.dm_t;
    if (!g_tt.dm_t100 && mph >= 100.0f) g_tt.dm_t100 = g_tt.dm_t;
    if (vtop > 0) g_tt.dm_top = vtop;
    o->gear_req = g_tt.dm_target;
    switch (g_tt.dm_state) {
    case 1:   /* up to 3rd (gear 4): full throttle, the next gear at 3000 rpm */
        if (gear == 4) { g_tt.dm_state = 2; o->throttle = 1.0f; break; }
        if (gear == g_tt.dm_target) {
            o->throttle = 1.0f;
            if (rpm >= 3000.0f) g_tt.dm_target++;
            dm_record(0.0f);
        } else o->throttle = 0.0f;
        break;
    case 2:   /* full throttle, the rpm averaged, until the progress is 1 */
        o->throttle = 1.0f;
        dm_record(rpm);
        if (dm_progress(gear) == 1.0f) { g_tt.dm_state = 3; o->gear_req = -1; }
        break;
    case 3:   /* off the throttle, braking above 1st, until stopped */
        o->gear_req = -1;
        o->brake = gear > 2 ? 1.0f : 0.0f;
        if (fabsf(mph) < 0.001f) g_tt.dm_state = 4;
        break;
    case 4:   /* EndDynoRun(1) */
        o->gear_req = -1;
        g_tt.dm_state = 0; o->on = 0;
        f |= TT_DY_END;
        printf("tuning: EndDynoRun after %.1f s\n", g_tt.dm_t);
        break;
    }
    {   static int ls, lt;
        if (g_tt.dm_state != ls || g_tt.dm_target != lt)
            printf("tuning: dyno state %d target %d at %.2f s: rpm %.0f gear %d %.1f mph mean %.0f\n",
                   g_tt.dm_state, g_tt.dm_target, g_tt.dm_t, rpm, gear, mph, g_tt.dm_mean);
        ls = g_tt.dm_state; lt = g_tt.dm_target; }
    g_dm_prog = dm_progress(gear);
    return f;
}
int tun_dyno_live(void) { return g_tt.active && (g_tt.scr == SCR_CHART || g_tt.dm_state || g_tt.dm_start); }

/* ---- dialogs (UI_InGameDialog.fng, GLOBAL/InGameCommon.bun) ---- */
enum { DK_NONE, DK_SETTING, DK_MODE, DK_SAVE, DK_QUIT, DK_QUIT_SAVE, DK_RETURN, DK_RESTART };
static void dlg_focus(void)
{
    hud_screen_script(g_tt.dlg, "Button1", g_tt.dsel == 0 ? "Highlight" : "UnHighlight");
    hud_screen_script(g_tt.dlg, "BUTTON_TEXT_1", g_tt.dsel == 0 ? "Highlight" : "UnHighlight");
    hud_screen_script(g_tt.dlg, "Button2", g_tt.dsel == 1 ? "Highlight" : "UnHighlight");
    hud_screen_script(g_tt.dlg, "BUTTON_TEXT_2", g_tt.dsel == 1 ? "Highlight" : "UnHighlight");
}
static void dlg_open(int kind)
{
    static const uint32_t msg[] = { 0, 0x6bb7037eu, 0xa2323032u, 0x0d7de8abu, 0xe5a0d45eu, 0x71851654u, 0x455c3f80u, 0x0972a44bu };
    hud_free(&g_tt.dlg);
    g_tt.dlg = hud_init_screen(g_tt.root, "GLOBAL/InGameCommon.bun", "UI_InGameDialog.fng");
    if (!g_tt.dlg) { printf("tuning: UI_InGameDialog.fng not loaded\n"); return; }
    g_tt.dlg_kind = kind;
    hud_screen_text(g_tt.dlg, "message_blurb", hud_screen_string(g_tt.dlg, msg[kind]));
    int yn = kind != DK_RESTART;
    hud_screen_label(g_tt.dlg, "BUTTON_TEXT_1", yn ? 0xf770u : 0x639u);        /* Yes / OK */
    hud_screen_label(g_tt.dlg, "BUTTON_TEXT_2", yn ? 0x61cu : 0x541389e5u);    /* No / Cancel */
    hud_screen_text(g_tt.dlg, "BUTTON_TEXT_3", "");
    hud_screen_hide(g_tt.dlg, "Button3", 1);
    hud_screen_script_all(g_tt.dlg, "FadeIn");
    g_tt.dsel = 0;
    dlg_focus();
}
static void choose_setting(int s)
{
    /* 0xd05fc3a3: setting = option, RestoreCustomTunings(setting); the first
       time the baseline dyno run (UI_PerformanceDyno_Chart), else MAIN */
    if (g_tt.setting >= 0) memcpy(g_tt.stored[g_tt.setting], g_tt.temp, TUNE_BYTES);
    g_tt.setting = s;
    memcpy(g_tt.temp, g_tt.stored[s], TUNE_BYTES);
    tun_rebuild_work();
    printf("tuning: setting %d chosen\n", s);
    if (!g_tt.set_chosen[s]) { g_tt.set_chosen[s] = 1; open_chart(); }
    else open_main();
}
static int dlg_key(int key)
{
    if (key == TT_K_LEFT || key == TT_K_RIGHT) { g_tt.dsel = key == TT_K_RIGHT; dlg_focus(); audio_fe_play(key == TT_K_LEFT ? FE_SND_LEFT : FE_SND_RIGHT); return TT_ACT_NONE; }
    if (key == TT_K_BACK) { audio_fe_play(FE_SND_BACK); hud_free(&g_tt.dlg); return TT_ACT_NONE; }
    if (key != TT_K_OK) return TT_ACT_NONE;
    int k = g_tt.dlg_kind, yes = g_tt.dsel == 0;
    audio_fe_play(FE_SND_SELECT);
    hud_free(&g_tt.dlg);
    switch (k) {
    case DK_SETTING: if (yes) choose_setting(g_tt.sel); return TT_ACT_REBUILD;
    case DK_MODE:    /* Yes: store + switch, No: switch (the temp set dropped) */
        if (yes && g_tt.setting >= 0) memcpy(g_tt.stored[g_tt.setting], g_tt.temp, TUNE_BYTES);
        else if (g_tt.setting >= 0) memcpy(g_tt.temp, g_tt.stored[g_tt.setting], TUNE_BYTES);
        open_setting();
        return TT_ACT_REBUILD;
    case DK_SAVE: if (yes) { tun_commit(NULL); return TT_ACT_SAVE; } return TT_ACT_NONE;
    case DK_QUIT:
        if (!yes) return TT_ACT_NONE;
        if (tun_changed()) { dlg_open(DK_QUIT_SAVE); return TT_ACT_NONE; }
        return TT_ACT_QUIT;
    case DK_QUIT_SAVE: g_tt.quit_save = yes; return TT_ACT_QUIT;   /* Yes: commit + EnterMemcard(0x151), No: commit */
    case DK_RETURN: if (yes) { tun_close_screens(); return TT_ACT_RETURN_DYNO; } return TT_ACT_NONE;
    case DK_RESTART: if (yes) { tun_close_screens(); return TT_ACT_RESTART; } return TT_ACT_NONE;
    }
    return TT_ACT_NONE;
}
void tun_open_dyno(const char *dataroot)
{
    g_tt.root = dataroot;
    g_tt.on_dyno = 1;
    if (g_tt.setting < 0) open_setting(); else open_main();
}
void tun_open_pause(const char *dataroot)
{
    g_tt.root = dataroot;
    g_tt.on_dyno = 0;
    open_main();
}
static int back_to_main(void)
{
    tun_rebuild_work();
    open_main();
    return TT_ACT_REBUILD;   /* RebuildPlayersRideInfo */
}
int tun_key(int key)
{
    if (!g_tt.active || g_tt.scr == SCR_NONE) return TT_ACT_NONE;
    if (g_tt.dlg) return dlg_key(key);
    switch (g_tt.scr) {
    case SCR_SETTING:
        if (key == TT_K_LEFT && g_tt.sel > 0) { g_tt.sel--; strip_title(); audio_fe_play(FE_SND_LEFT); }
        else if (key == TT_K_RIGHT && g_tt.sel < 4) { g_tt.sel++; strip_title(); audio_fe_play(FE_SND_RIGHT); }
        else if (key == TT_K_OK) { audio_fe_play(FE_SND_DIALOG); dlg_open(DK_SETTING); }
        else if (key == TT_K_BACK) {
            audio_fe_play(FE_SND_BACK);
            if (g_tt.setting < 0) dlg_open(DK_QUIT); else open_main();
        }
        break;
    case SCR_MAIN:
        if (key == TT_K_LEFT && g_tt.sel > 0) { g_tt.sel--; strip_title(); audio_fe_play(FE_SND_LEFT); }
        else if (key == TT_K_RIGHT && g_tt.sel < g_main_n - 1) { g_tt.sel++; strip_title(); audio_fe_play(FE_SND_RIGHT); }
        else if ((key == TT_K_BACK || key == TT_K_START) && g_main_run) { audio_fe_play(FE_SND_BACK); tun_close_screens(); return TT_ACT_RESUME; }
        else if (key == TT_K_OK) {
            int a = main_act(g_tt.sel);
            if (a == MO_SAVE && !tun_changed()) { audio_fe_play(FE_SND_WRONG); break; }
            audio_fe_play(a == MO_QUIT || a == MO_MODE || a == MO_SAVE || a == MO_RETURN || a == MO_RESTART ? FE_SND_DIALOG : FE_SND_SELECT);
            switch (a) {
            case MO_TUNE: g_tt.cat = 0; open_master(); break;
            case MO_TEST:   /* TestTrack::React 8030fb74: StoreTempSettings, the test track */
                memcpy(g_tt.stored[g_tt.setting], g_tt.temp, TUNE_BYTES);
                tun_close_screens();   /* off the dyno once the DynoExit revs have played (main.c) */
                return TT_ACT_TEST_RUN;
            case MO_DYNO: open_chart(); break;
            case MO_MODE: if (tun_changed() && memcmp(g_tt.temp, g_tt.db[g_tt.setting], TUNE_BYTES)) dlg_open(DK_MODE); else open_setting(); break;
            case MO_SAVE: dlg_open(DK_SAVE); break;
            case MO_QUIT: dlg_open(DK_QUIT); break;
            case MO_RESUME: tun_close_screens(); return TT_ACT_RESUME;
            case MO_RESTART: dlg_open(DK_RESTART); break;
            case MO_RETURN: dlg_open(DK_RETURN); break;
            case MO_OPTIONS: audio_fe_play(FE_SND_WRONG); printf("tuning: Options (UI_PauseOptionsMain) not wired here yet\n"); break;
            }
        }
        break;
    case SCR_MASTER:
        if (key == TT_K_LEFT || key == TT_K_RIGHT) {
            int d = key == TT_K_LEFT ? -1 : 1, n = g_tt.sel + d;
            while (n >= 0 && n < 7 && !cat_avail(n)) n += d;
            if (n >= 0 && n < 7) { g_tt.sel = n; strip_title(); cat_speech(n); audio_fe_play(d < 0 ? FE_SND_LEFT : FE_SND_RIGHT); }
        } else if (key == TT_K_OK && cat_avail(g_tt.sel)) { audio_fe_play(FE_SND_SELECT); open_category(g_tt.sel); }
        else if (key == TT_K_BACK || key == TT_K_START) { audio_fe_play(FE_SND_BACK); return back_to_main(); }
        break;
    case SCR_SLIDERS:
        if (key == TT_K_UP && g_tt.row > 0) { g_tt.row--; sliders_refresh(); audio_fe_play(FE_SND_UP); }
        else if (key == TT_K_DOWN && g_tt.row < g_tt.ntypes - 1) { g_tt.row++; sliders_refresh(); audio_fe_play(FE_SND_DOWN); }
        else if ((key == TT_K_LEFT || key == TT_K_RIGHT) && g_tt.ntypes) {
            if (slider_step(g_tt.types[g_tt.row], 0, key == TT_K_LEFT ? -1 : 1)) {
                audio_fe_play(key == TT_K_LEFT ? FE_SND_SLIDER_LEFT : FE_SND_SLIDER_RIGHT);
                if (is_gear(g_tt.types[g_tt.row]) || g_tt.types[g_tt.row] == 9) { /* 0x5fe4bae4: all refreshed */ }
                sliders_refresh();
            }
        } else if (key == TT_K_RESET && g_tt.ntypes) {   /* 0xc519bfc0 ResetToDefault */
            int t = g_tt.types[g_tt.row];
            g_tt.temp[t] = 100;
            sliders_refresh();
        } else if (key == TT_K_BACK) { audio_fe_play(FE_SND_BACK); open_master(); return TT_ACT_REBUILD; }
        else if (key == TT_K_START) { audio_fe_play(FE_SND_BACK); return back_to_main(); }
        break;
    case SCR_GRAPH: {
        int t = g_tt.cat == 4 ? 23 : 24;
        if (key == TT_K_LEFT && g_tt.pt > 0) { g_tt.pt--; graph_refresh(); audio_fe_play(FE_SND_LEFT); }
        else if (key == TT_K_RIGHT && g_tt.pt < 8) { g_tt.pt++; graph_refresh(); audio_fe_play(FE_SND_RIGHT); }
        else if (key == TT_K_UP || key == TT_K_DOWN) {
            if (slider_step(t, g_tt.pt, key == TT_K_UP ? 1 : -1)) { audio_fe_play(key == TT_K_UP ? FE_SND_UP : FE_SND_DOWN); graph_refresh(); }
        } else if (key == TT_K_RESET) { for (int k = 0; k < 9; k++) g_tt.temp[tidx(t, k)] = 100; graph_refresh(); }
        else if (key == TT_K_BACK) { audio_fe_play(FE_SND_BACK); open_master(); return TT_ACT_REBUILD; }
        else if (key == TT_K_START) { audio_fe_play(FE_SND_BACK); return back_to_main(); }
        break; }
    case SCR_NOS:
        if (key == TT_K_LEFT || key == TT_K_RIGHT) {
            if (slider_step(17, 0, key == TT_K_LEFT ? -1 : 1)) { audio_fe_play(key == TT_K_LEFT ? FE_SND_SLIDER_LEFT : FE_SND_SLIDER_RIGHT); nos_refresh(); }
        } else if (key == TT_K_RESET) { g_tt.temp[16] = g_tt.temp[17] = 100; nos_refresh(); }   /* 17 resets 16 too */
        else if (key == TT_K_BACK) { audio_fe_play(FE_SND_BACK); open_master(); return TT_ACT_REBUILD; }
        else if (key == TT_K_START) { audio_fe_play(FE_SND_BACK); return back_to_main(); }
        break;
    case SCR_CHART:
        if (g_tt.dyno_run && (key == TT_K_BACK)) {   /* 0x406415e3: record + EndDynoRun */
            g_tt.dyno_run = 0; record_run(); dm_end(0); chart_refresh();
        } else if (!g_tt.dyno_run && key == TT_K_OK) { audio_fe_play(FE_SND_SELECT); open_results(); }
        else if (!g_tt.dyno_run && key == TT_K_BACK) { audio_fe_play(FE_SND_BACK); open_main(); }
        break;
    case SCR_RESULTS:
        if (key == TT_K_OK || key == TT_K_BACK) { audio_fe_play(key == TT_K_OK ? FE_SND_SELECT : FE_SND_BACK); open_main(); }
        break;
    }
    return TT_ACT_NONE;
}
void tun_tick(float dt)
{
    if (g_tt.scr == SCR_CHART && g_tt.dyno_run) {
        g_tt.dyno_t += dt;
        /* GetDynoProgress (PC 56d440): mean rpm of the last 20 ticks / (redline - 200) */
        if (g_dm_prog >= 1.0f) {   /* NotificationMessage (PC 0x54a800): progress 1 */
            g_tt.dyno_run = 0;
            hud_screen_script_h(g_tt.h, 0x11bf9934u, "FULL");
            record_run();
            hud_screen_script_h(g_tt.h, 0x25b79b9fu, "Show");   /* ShowMaxStats: CHART_LEGEND */
        }
        chart_refresh();
    }
}
void tun_draw(const RProg *rp, GpuMesh *quad, int W, int H, int dms)
{
    if (g_tt.h) hud_screen_draw(g_tt.h, rp, quad, W, H, dms);
    if (g_tt.h && g_tt.scr == SCR_CHART) chart_draw(rp, quad, W, H);
    if (g_tt.dlg) hud_screen_draw(g_tt.dlg, rp, quad, W, H, dms);
}
