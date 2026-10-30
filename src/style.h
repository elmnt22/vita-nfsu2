/* style.h — the retail NFSU2 style points ("style moments"), ported from
 * SPEED2.EXE (v1.2), as far as a drift race uses them.
 *
 * Every racer of a race has a StylePointsAccum (0xa0 bytes, race entry
 * +0x1d4): one StyleMoment (0x58) per row of the moment table (GLOBALB
 * chunk 0x34a07, 39 rows of 0x30 bytes) whose race-type mask (+7) holds the
 * race's bit (0x56ebe0: drift 4, drag 2, ...). A moment is fed values
 * (0x56eab0); an instant moment banks at once, a continuous one (+6) arms,
 * runs while it keeps being fed and banks after its timeout (0x56e920).
 * In a drift race the drift scorer (0x48 bytes, entry +0x1d8, 0x56ed40)
 * feeds moment 0 (STYLE_DRIFT) every frame; the drift score is moment 0's
 * banked total (+0x2c). Each racer, the AI too, has its own scorer
 * (0x802228 = 1 in retail); the AI's points are scaled by the difficulty
 * (0x79dae8: 1, 1.25, 1.5).
 *
 * The other style moments (clean laps, power slides, near misses, ...) come
 * from MiscStyleManager (0x56ff30, race entry +0x1dc) and the racer stats
 * (burnout, 0x5f7410); every human racer has them in every race type and in
 * free roam (RacingCar init 0x5fc2a0), and their banked total refills the
 * nitrous bottle (0x6016a0: 1500 points a bottle). */
#ifndef OPENUG2_STYLE_H
#define OPENUG2_STYLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STYLE_NMOM 39

/* one row of GLOBALB chunk 0x34a07 */
typedef struct {
    uint32_t hash;          /* +0x00 label (STYLE_DRIFT, ...)               */
    int8_t   idx;           /* +0x04                                        */
    uint8_t  b5;            /* +0x05                                        */
    uint8_t  cont;          /* +0x06 continuous (arms, runs, banks)         */
    uint8_t  mask;          /* +0x07 race types (1 normal, 2 drag, 4 drift, */
                            /*       8 sprint, 0x10 street X, 0x20 ...,     */
                            /*       0x40 lap races)                        */
    float    arm;           /* +0x08 arm time / title threshold             */
    float    timeout;       /* +0x0c no value for this long ends it         */
    float    thresh;        /* +0x10 raw total needed (sign of base)        */
    float    cap;           /* +0x14 points cap (>0) / floor (<0)           */
    float    maxmult;       /* +0x18                                        */
    float    multstep;      /* +0x1c                                        */
    float    f20, f24;      /* +0x20 +0x24                                  */
    float    base;          /* +0x28 points per value second                */
    int8_t   maxchain;      /* +0x2c (-1 any)                               */
} StyleRec;

/* parse chunk 0x34a07 out of GLOBALB; returns the row count (39) */
int style_init(const unsigned char *globalb, long len);
const StyleRec *style_rec(int i);
/* 0x7fa520: per-moment value cap in replays (unused here) */

/* the game clock (0x86518c, 4000 ticks a second) */
void style_set_clock(int ticks);
int  style_clock(void);

typedef struct {                /* StyleMoment, 0x58 bytes */
    const StyleRec *rec;        /* +0x08 */
    int     index;              /* +0x0c */
    uint8_t active;             /* +0x10 */
    int     state;              /* +0x14 0 running, 1 armed, 2 idle,
                                         3 banked, 4 failed */
    uint8_t counts;             /* +0x18 counts into the total */
    float   mult;               /* +0x1c */
    int     chain;              /* +0x20 */
    float   cur;                /* +0x24 points of the running moment */
    float   val;                /* +0x28 last value */
    float   banked;             /* +0x2c */
    uint8_t hold;               /* +0x30 keep a finished state one frame */
    int     t_add;              /* +0x34 last value at */
    int     t_arm;              /* +0x38 armed at */
    float   raw;                /* +0x3c */
    float   best_time;          /* +0x40 */
    float   best_pts;           /* +0x44 */
    int     p48, p4c;           /* +0x48 +0x4c */
    float   best_span;          /* +0x50 */
    float   best_val;           /* +0x54 */
} StyleMoment;

typedef struct {                /* StylePointsAccum, 0xa0 bytes */
    StyleMoment *m[STYLE_NMOM]; /* +0x00 (NULL: not in this race type) */
    StyleMoment  store[STYLE_NMOM];
    float        total;         /* +0x9c */
} StyleAccum;

/* the race type bit of 0x56ebe0 / 0x56c500 */
enum {
    STYLE_RT_NORMAL = 1,    /* circuit, lap KO, free roam, outrun */
    STYLE_RT_DRAG   = 2,    /* 0x89e7d8 / 0x890108 */
    STYLE_RT_DRIFT  = 4,    /* 0x89e7d9 / 0x890104 */
    STYLE_RT_SPRINT = 8,    /* 0x89e7b4 */
    STYLE_RT_STREETX = 0x10,/* 0x89e7e3 / 0x89010c */
    STYLE_RT_URL    = 0x40  /* 0x89e7e1 */
};
/* 0x56ebe0's choice, in its order (0x89e7e5 / 0x20 is never set in retail) */
int  style_race_mask(int drift, int drag, int sprint, int streetx, int url);
void style_accum_init(StyleAccum *a, int race_mask);          /* 0x56ebe0 */
void style_moment_add(StyleMoment *m, float v, int p2);       /* 0x56eab0 */
/* 0x56fd10: once a frame -- clears finished moments, ticks every moment,
   sums the total. Moments that just finished (or are running) are listed
   in shown[] for the HUD (0x4da6f0); returns their count. */
int  style_accum_tick(StyleAccum *a, float dt, StyleMoment **shown, int max);
/* the same tick with the shown moments copied as the player's HUD reads
   them inside it (0x4da6f0 runs between the list and the reset of the
   finished moments) */
typedef struct { int index, state, cont; float cur, mult; uint32_t label; } StyleShown;
int  style_accum_tick_snap(StyleAccum *a, float dt, StyleShown *snap, int max);
void style_accum_finish(StyleAccum *a);                       /* 0x56d710 */

/* what the drift scorer reads from its car each frame */
typedef struct {
    float speed;            /* car +0x430 |v|                                 */
    float fwd_speed;        /* car +0x42c                                     */
    float slip;             /* 0x59fa10: heading - velocity heading, rad;     */
                            /*           0 below 1 m/s forward speed          */
    float yaw_rate;         /* car +0x118 (angular velocity z)                */
    int   surface[4];       /* car +0x394..: the wheels' surface ids          */
    float top_speed;        /* 0x5b5930                                       */
    int   wall_contacts;    /* collision info +0x120: wall pairs alive        */
                            /*   (a pair lives 0.3 s after its last contact)  */
    float race_dist;        /* race entry +0x14: laps raced                   */
    int   human;            /* racer kind 1                                   */
    int   ctrl_flag;        /* human controller +0x248                        */
} StyleCar;

typedef struct {            /* MiscStyleManager (0x3c) */
    uint8_t clean[3];       /* +0x08 +0x09 +0x0a */
    uint8_t latch_cp;       /* +0x0b RacingCar +0xe edge */
    uint8_t latch_lap;      /* +0x0c RacingCar +0xd edge */
    int     best_rank;      /* +0x10 */
    int8_t  s360;           /* +0x14 */
    float   h360;           /* +0x18 heading in degrees */
    int8_t  jturn;          /* +0x1c */
    int8_t  nm_state;       /* +0x1d near miss: 0 look, 1 passing, 2 bank */
    uint8_t nm_cleared;     /* +0x1e over the other car (CLEARED ACCIDENT) */
    int     nm_id;          /* +0x20..+0x28 the pair: the other car's id, -1 none */
    uint8_t finished;       /* 0x5ee250 this frame (racer done in a race) */
    int     t_coll;         /* +0x2c last hit at */
    float   coll_imp;       /* +0x30 its impulse sum */
    int     coll_kind;      /* +0x34 0 car, 3 wall */
    uint8_t coll_traffic;   /* +0x38 */
} StyleMisc;
void style_misc_init(StyleMisc *s);                           /* 0x56cdf0 */
/* 0x56ed10 from outside the managers: feed moment i (e.g. 15 SPRAY SHOW,
   0x60351d) when this race type has it */
void style_trigger(StyleAccum *a, int i, float v, int p2);
/* one car-against-car pair of the racer's car in the collision pair list
   (0x86b490); the caller lists every pair of the car, ok = both cars are
   in the race's play (not blinking 0x5ebfa0, not poofed 0x5ec060, +0x550
   set) and both cars collide (0x592e60: always so in the port); the scan
   also skips pairs within 0.001 (0x598730 = GJKWitness +0x14, 0x598390) */
typedef struct {
    int   id;               /* the other car */
    float dist;             /* +0x10 the hulls' distance */
    int   touch;            /* +0x244 a contact */
    int   ok;
    float other_z;          /* other car +0x68 */
    float center_dist;      /* 0x401150 between the cars' +0x60 */
    float relv;             /* |velocity difference| (0x4011b0, 0x440f70) */
} StyleNear;
/* what MiscStyleManager::Update (0x56ff30) reads from its racer each frame */
typedef struct {
    int   racing;           /* race object stage 4 (*0x890118 == 4) */
    int   race_mode;        /* 0x89e7b0 (0: free roam) */
    int   nracers;          /* 0x89e7c0..0x89e7cc */
    float race_time;        /* race object +0x14 x 0.00025 */
    int   rank;             /* RacingCar +0xa */
    int   leader;           /* LEAD LAP: rank 1 (drift: 0x56dd90 standings) */
    int   newcp, newlap;    /* RacingCar +0xe, +0xd */
    int   finished;         /* 0x5ee250 */
    int   wheels;           /* car +0x424 */
    float speed;            /* car +0x430 */
    float fwd_speed;        /* car +0x42c */
    float yaw_rate;         /* car +0x118 */
    float slip;             /* 0x59fa10 */
    float up_z;             /* car +0xb8 */
    uint16_t heading;       /* car +0x40c */
    float air;              /* car +0x68 - 0x5ed400()+8 - ground (0x5e72e0) */
    float z;                /* car +0x68 */
    int   hyd_on;           /* 0x5ed290: a hydraulic bounce / extend is on */
    int   body_flag;        /* car +0x3c -> +0x18 */
    float draft;            /* car +0x4dc (0x5ebd80) */
    int   blink;            /* 0x5ebfa0 */
    int   dist_m;           /* car +0x410 (passed as the slide's p2) */
    int   shortcut;         /* 0x5d9bf0 found a shortcut zone (type 0xb)
                               whose +0x1a was 0 (the caller sets it) */
    int   bounce_n;         /* car +0x408[4] set */
    int   hydros_n;         /* 0x5ed470 in {2, 3} */
    const StyleNear *near; int nnear;
} StyleMiscIn;
void style_misc_update(StyleMisc *s, StyleAccum *a, const StyleMiscIn *in);   /* 0x56ff30 */
/* RacingCarStats +0x28 burnout (0x5f7410, call 0x5f781f); *state starts
   at -1. avg_slip = the driven wheels' car +0x3b4 average. */
void style_burnout_tick(float *state, StyleAccum *a, int racing, int race_mode,
                        float race_time, float avg_slip, float dist);

/* 0x56fb20: a collision (pair effect +0x60 impulse sum imp, created at
   t_made) of this racer; wall = not a car, other_traffic = the car hit is
   traffic. racing = car +0x424 >= 1, upright = car +0xb8 > 0.9. */
void style_misc_collision(StyleMisc *s, StyleAccum *a, float imp, int t_made,
                          int wall, int other_traffic, int racing, float up_z);

typedef struct {            /* the drift scorer, 0x48 bytes */
    StyleAccum *acc;        /* +0x00 */
    StyleMisc  *misc;       /* +0x04 (owner's misc manager) */
    int     klass;          /* +0x0c 0 fastest .. 3 */
    int     target;         /* +0x10 tandem car: never set in retail */
    uint8_t wall_latch;     /* +0x15 */
    uint8_t lvl_drop;       /* +0x16 */
    uint8_t lvl_hold;       /* +0x17 */
    float   wheelf;         /* +0x18 */
    int     level;          /* +0x1c 1..5 */
    int     next;           /* +0x20 points for the next level */
    int     t_lvl;          /* +0x24 */
    int     t_rev;          /* +0x28 */
    float   rev_left;       /* +0x2c */
    float   maxdist;        /* +0x30 */
    float   angle_pts;      /* +0x34 */
    float   diffmult;       /* +0x38 */
    float   prox;           /* +0x3c */
    int     titles;         /* +0x40 */
    float   maxslip;        /* +0x44 */
    int     human;          /* car +0x18: shows title popups */
} DriftScorer;
/* 0x56c8f0 + 0x56d7d0: ai = racer kind 2; difficulty 0..2 */
void drift_scorer_init(DriftScorer *d, StyleAccum *a, StyleMisc *m,
                       int klass, int ai, int difficulty, int human);
/* nearby cars for the proximity term (0x56e0a0: the cars of the world
   list after the racers, i.e. traffic) */
typedef struct { float pos[3]; } StyleOther;
/* 0x56ed40 once a frame; pos = the scorer's car. Returns the points fed to
   moment 0 this frame (0 none). title = popup label hash or 0. */
float drift_scorer_tick(DriftScorer *d, const StyleCar *c, const float pos[3],
                        const StyleOther *traffic, int ntraffic, float dt,
                        uint32_t *title);

/* a racer's collision pairs (0x5ffa50 keeps a pair 0.3 s after its last
   contact, 0x592bd0; its effect object 0x5a9430/0x5a9620 is made at the
   first contact and sums |impulse| into +0x60). other < 0: the world. */
typedef struct { int other, t_made, t_last; float imp; } StylePair;
typedef struct { StylePair p[8]; int n; } StylePairs;
StylePair *style_pairs_hit(StylePairs *s, int other, float impulse);
void style_pairs_tick(StylePairs *s);
int  style_pairs_world(const StylePairs *s);      /* collision info +0x120 */

/* the drift race score bookkeeping of a race entry (0x5f7a00) */
typedef struct {
    float lap[21];          /* +0x13c per-lap points */
    float at_lap;           /* +0x190 score when the lap began */
} DriftLaps;
float drift_score(const StyleAccum *a);                        /* moment 0 +0x2c */
void  drift_laps_tick(DriftLaps *l, float score, int maxlap, int laps,
                      int lap_done);

/* downhill drifts (events 4174..4179, 0x56d850): three opponents that do
   not race, with the score of table 0x7fa5c0 for the event, the player's
   class and the difficulty; racers 101 and 102 get x0.95 and x0.9.
   Returns 0 when the event has no table row. */
int   drift_downhill_event(int event);
float drift_downhill_score(int event, int klass, int difficulty, int racer_id);

#ifdef __cplusplus
}
#endif
#endif
