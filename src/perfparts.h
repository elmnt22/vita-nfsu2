/* perfparts.h — the retail NFSU2 performance parts, ported from SPEED2.EXE.
 *
 * A car's physics record (GLOBALB 0x34600, N2_REC_SIZE bytes) holds the
 * stock physics P (rec +0x110, 0x2b0 bytes) and, after it, the upgrade
 * targets. The game never drives the raw record: every car's working P is
 * rebuilt from it by 0x5b4dc0 -> 0x5ac4f0, category by category, from the
 * parts fitted (packages in GLOBALB chunk 0x34a15; each part adds a value
 * per component, a category's level is their average). With no parts the
 * rebuild still runs (the stock turbo boost curve P+0x224 joins the torque
 * curve P+0x200). AI racers are fitted from the per-car combination tables
 * (chunk 0x34a31, sorted by performance rating) to a target rating
 * (0x5b6ad0). */
#ifndef OPENUG2_PERFPARTS_H
#define OPENUG2_PERFPARTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PERF_NPARTS 128   /* part index space (fitted flags at PerfConfig +0x330) */
#define PERF_NCAT   10

/* parse GLOBALB: packages 0x34a15 (the first), the combination tables
   0x34a31 (one per car type, in car type order) and the rating limits
   0x34a32. Returns 1 when everything was found. */
int  perf_init(const unsigned char *globalb, long len);
int  perf_ready(void);

/* the fitted parts of one car (PerfConfig +0x2e0: the category levels,
   +0x330 the fitted flags) */
typedef struct {
    float level[PERF_NCAT];
    unsigned char fit[PERF_NPARTS];
    unsigned char junk[PERF_NCAT];     /* junkman parts per path (spec +0x28), ApplyJunkmanUpgrades */
} PerfParts;

void perf_clear(PerfParts *pp);
/* 0x5a64a0(level, cat): the category's parts off, then every package of
   levels 1..level fitted in order (each part takes its prerequisites'
   test and removes the parts it replaces, 0x5a6320); cat -1 (InstallAll-
   PerfParts' "all types"): every package off, then per level categories
   0..9 in turn */
void perf_fit_level(PerfParts *pp, int cat, int level);
/* the Performance Shop (UI_ChoosePerformancePackage): a package's parts,
   0x500100(cat, level) -- up to 4: part index, name (language hash, part
   +4) and price (part +8). Returns the count, 0 when there is none. */
int  perf_pkg_parts(int cat, int level, int idx[4], unsigned int name[4], int price[4]);
/* a part's brand hashes (up to max), and a brand's DECAL_ICON string (display 0)
   or display name (display 1) from GLOBALB 0x34a14 ("" when unknown) */
int  perf_part_brands(int idx, unsigned int *out, int max);
const char *perf_brand_str(unsigned int hash, int display);
/* is_brand_in_any_perf_package (GC 8012f894) */
int perf_brand_in_package(unsigned int brand);
/* 0x521340: the package's parts fitted on top of what is there (0x5a6320(on)) */
void perf_fit_pkg(PerfParts *pp, int cat, int level);
/* 0x5213d0: one part fitted (by its index; UI_BuyPerformanceParts) */
void perf_fit_part(PerfParts *pp, int idx);
/* 0x599680(idx, on, 0): one part fitted or removed by its index
   (PhysicsUpgradeSpecification::InstallPerfPart) */
void perf_set_part(PerfParts *pp, int idx, int on);
/* 0x5b5810 / 0x5b56e0 of a built record: the peak torque (the P+0x200
   curve, kN m, over P+0x1f0..P+0x1f8 rpm in 50 rpm steps) and the peak power
   (torque x 1000 x rpm x 0.000104712, kW) -- raw, and over GLOBALB 0x34a32
   [4],[5] / [6],[7] as 0..1 */
void perf_power(const unsigned char *built, float raw[2], float norm[2]);
/* 0x521a20: every part of the category's packages removed (Return To Stock) */
void perf_unfit_cat(PerfParts *pp, int cat);
/* the shop's confirmations: 0x599610 a part fitted, 0x521c10 a part worse
   than the fitted one it replaces, 0x521b90 a package's parts all fitted,
   0x5303c0 how many of a package's parts are worse, 0x5995e0 a category's
   level rounded (0: stock) */
int  perf_part_fitted(const PerfParts *pp, int idx);
int  perf_part_worse(const PerfParts *pp, int idx);
int  perf_pkg_fitted(const PerfParts *pp, int cat, int level);
int  perf_pkg_worse(const PerfParts *pp, int cat, int level);
int  perf_cat_level(const PerfParts *pp, int cat);
/* 0x5ac4f0 (recompute path): out = rec with P rebuilt from the parts.
   rec and out are N2_REC_SIZE bytes and may not overlap. */
void perf_build(const unsigned char *rec, PerfParts *pp, unsigned char *out);
/* the car's raw performance rating (PerfConfig +8, 0x5b4dc0) of a built
   record, its level 0..10 (0x5b6990(1,-1)) and its class 0..3 */
float perf_rating(const unsigned char *built);
float perf_level(float rating);
int   perf_class(float rating);
/* 0x5b4dc0's race-type pass, after the rating: mode 3 (drift-type races)
   = 0x5a7750: 0x5a7530 with the drive type's drift table (GLOBALB-free,
   0x7fc2e4 + 0x144 k; k 0 front, 1 four-wheel, 2 rear drive by P+0x1c0)
   scales aero, yaw inertia, mass, the tyres, the brake bias and the drive
   split by the fitted levels, then empties the nitrous bottle (P+0x2a0).
   Modes 2 (drag) and 10/5 (street X) are not ported yet. */
#define PERF_MODE_DRAG 2        /* 0x5b4dc0 race-type modes */
#define PERF_MODE_DRIFT 3
#define PERF_MODE_STREETX 5
#define PERF_MODE_STREETX2 10
void perf_race_mods(unsigned char *built, const PerfParts *pp, int mode);
/* 0x5b5490: the handling rating 0.03..1 of a built record */
float perf_handling(const unsigned char *built);
/* The car select's sliders, 0.03..1: [0] acceleration 0x5b53a0, [1] top
   speed 0x5b6a20, [2] handling 0x5b5490 */
void perf_car_stats(const unsigned char *built, float out[3]);
/* 0x5b6ad0(target, 0, handling): fit the car type's combination nearest
   the target rating; with handling >= 0 once more, the target moved by
   (handling - got) x 150. Leaves pp fitted and out built. */
void perf_fit_rating(int type, const unsigned char *rec, float target, float handling,
                     PerfParts *pp, unsigned char *out);
/* 0x34a32 [10] [11]: the rating range levels are measured over */
float perf_rating_min(void);
float perf_rating_max(void);
/* 0x34a32 [28] [29] (GetPhysicsExtrema +0x70 / +0x74): the handling range */
float perf_handling_min(void);
float perf_handling_max(void);
/* GetMinMaxPerformanceMeasure 0x59a450: the car type's lowest and highest
   rating of its part combinations; 0 when the type has none */
int perf_type_rating_range(int type, float *lo, float *hi);

#ifdef __cplusplus
}
#endif
#endif
