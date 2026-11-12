/* racepath.h — the race route and race progress, ported from SPEED2.EXE (v1.2).
 *
 * A race's progress (car +0x8fc, what positions, the catch-up gap and the
 * AI's race decisions are measured in) is the distance along the event's
 * race route in the Paths<id>.bin road graph (track object 0x88f298):
 * 0x5e5bb0 joins the event's waypoints with the shortest chains of road
 * sections (depth-first 0x5d53d0), gives each section a Catmull-Rom spline
 * through its nodes whose 4th coordinate is arc length (0x5d5090, 0x5778d0)
 * and its place along the route (0x5e48b0 -> 0x5d52c0), then adds the side
 * routes (shortcuts) that rejoin the race later, their progress scaled onto
 * the main route. A car's locator (car +0x8d8, 0x5d9e30) finds the nearest
 * route node and projects the car onto that section's spline. */
#ifndef OPENUG2_RACEPATH_H
#define OPENUG2_RACEPATH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RacePath RacePath;

/* Paths<id>.bin and the event record (chunk 0x3414c) to build; reversed =
   0x89e7a4. NULL when the file, the event or the route cannot be built. */
RacePath *racepath_load(const char *paths_bin, int event, int reversed);
void      racepath_free(RacePath *p);
/* free roam (race mode 0): the zones only, no route -- racepath_lap 0,
   locating does nothing; NULL without chunk 0x3414a */
RacePath *racepath_load_zones(const char *paths_bin);
/* the next zone of `type` holding pos after `after` (NULL: the first), in
   file order -- the same as walking racepath_zone_next with that test */
const unsigned char *racepath_zone_at(const RacePath *p, const float pos[2], int type, const unsigned char *after);
float     racepath_lap(const RacePath *p);       /* route length (track +0x18) */
int       racepath_circuit(const RacePath *p);   /* track +0x24 */
int       racepath_routes(const RacePath *p);    /* main route + shortcuts */
int       racepath_sections(const RacePath *p, int route);

/* a car's locator (0x3c bytes at car +0x8d8) */
typedef struct {
    float pos[2];          /* +0x08 where it was last placed */
    float spt[2];          /* +0x10 the projection on the route */
    int   node;            /* +0x18 route node (-1 none) */
    float t;               /* +0x1c spline parameter */
    float frac;            /* +0x20 progress / lap */
    float progress;        /* +0x24 metres along the race route */
    float dir[2];          /* +0x28 route direction at the car */
    float thresh;          /* +0x38 move before re-locating (0x5da330) */
} RpLoc;
void  rploc_init(RpLoc *l);
/* 0x5da330 -> 0x5d9e30 */
void  racepath_locate(const RacePath *p, RpLoc *l, const float pos[3]);
/* 0x5d5cd0: a - b, wrapped by half a lap on a circuit */
float racepath_diff(const RacePath *p, float a, float b);
/* 0x5eb5d0's grid when the event has too few starting-position markers:
   car i at 100 + 17 x (i / 2) m along the route, 3 m right (even i) or left
   (odd) of the line (0x5da370), facing along it. z is 0: the caller snaps
   it to the collision ground (0x5e72e0). Returns n, 0 without a route. */
int   racepath_grid(const RacePath *p, int n, float (*pos)[3], float *heading);


/* ---- the race manager's checkpoints and each racer's laps -----------------
   0x5ee620: checkpoints in percent of the route: the start line (marker
   Forward/BackwardStartLine or 5 %), split boundaries 1..3 (markers, else
   +33 / +66 %), the finish (= the start on a circuit; on a point-to-point
   race the Forward/BackwardFinishLine marker or +95 %, the splits then
   spread at 33 / 66 % between them). */
typedef struct {
    int   n;
    float cp[8];                  /* racemgr +0xbf8 */
    int   start, finish;          /* +0xbf6 +0xbf7 */
    int   sprint;                 /* 0x89e7b4: point to point */
    int   laps;                   /* +0xbe8 */
    int   circuit;                /* the track's wrap (track +0x24) */
    float start_pos[3];           /* +0xc10: the start line marker (0 without one) */
} RaceCps;
/* 0x5d9bf0: the first zone (Paths chunk 0x3414a, records sorted by type)
   of `type` whose box (+0x20 / +0x28) and polygon (+0x44, +0x40 points,
   0x43d890) hold the point; NULL none. A record: +0 type, +4 / +8 a
   position, +0xc / +0x10 a direction, +0x14 a height, +0x30 a name hash,
   +0x42 its size. */
const unsigned char *racepath_zone(const RacePath *p, const float pos[2], int type);
/* the zone records in file order: the first with prev NULL, NULL after the
   last; and racepath_zone's hold test for one record (box + polygon) */
const unsigned char *racepath_zone_next(const RacePath *p, const unsigned char *prev);
int racepath_zone_holds(const unsigned char *z, const float pos[2]);
/* 0x5ee9b0: a route percent as the fraction of the lap from the start line */
float racecps_lap_frac(const RaceCps *c, float pct);
/* the route section of a locator node (node +8), -1 none */
int   racepath_node_sec(const RacePath *p, int node);
void racecps_init(RaceCps *c, const RacePath *p, const char *trackpos_path, int event,
                  int reversed, int sprint, int laps);
/* a race entry (car +0x1c): +8 lap, +9 best lap, +4/+6 checkpoint count,
   +0x1c last percent, +0x20 race distance in laps, +0xc finish place */
typedef struct {
    int   lap, maxlap;
    int   cp, cpmax;
    float pct, dist;
    int   place;                  /* 0 racing */
    int   pos;                    /* +0xa standing, 1-based */
    /* per checkpoint (1/4000 s, to the hundredth): +0x30 its last crossing,
       +0x44 the time from checkpoint 0's crossing before it (the split;
       the lap at the start line), +0x58 the one before, +0x6c the best,
       +0x80 the best before (0x7fffffff none) */
    int   cpt[5], split[5], psplit[5], best[5], pbest[5];
    /* 0x5f7c30 at the finish checkpoint, laps 0..20: +0x94 the lap's time
       (its split), +0xe8 the race time at its end (the laps summed) */
    int   lapt[21], lapc[21];
    char  newlap, newcp;         /* +0xd a new best lap / +0xe a new checkpoint, this tick */
} RaceEntry;
void race_entry_reset(RaceEntry *e);                       /* 0x5fc2a0 */
/* 0x5fc900(4), the start: checkpoint 0's crossing = now (the race clock,
   1/4000 s, to the hundredth) */
void race_entry_go(RaceEntry *e, const RaceCps *c, float frac, int now);
/* 0x5f7c30: one racer's tick from its route fraction (locator frac) at the
   race clock `now`; returns 1 when its best lap just reached the lap count
   (finished) */
int  race_entry_update(RaceEntry *e, const RaceCps *c, float frac, int now);
/* 0x5eec80 (with 0x5eec00): standings by finish place, then distance */
void race_standings(RaceEntry **e, int n);

#ifdef __cplusplus
}
#endif
#endif
