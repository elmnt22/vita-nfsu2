/* airacer.h — the retail NFSU2 AI racer, ported from SPEED2.EXE (v1.2).
 *
 * The original's own opponent: a controller that thinks every 0.1 s
 * (0x42abd0) and plans a path ahead of the car on the race's road network
 * (Routes<event><F|B>.bin: sections of points, links between them), a
 * FuturePoint list (ctrl +0x370) with per-lane lateral shifts and speeds,
 * and a race driver (0x425ab0, vtable 0x784eb0) that every frame turns the
 * current plan into steering, throttle and brake through adaptive PIDs
 * (0x421030 steering, 0x41d8d0 speed). The car itself is an N2Sim on the
 * original's mover rules: the Mellovator by default, the full simulation
 * for 5 s after touching a human player's car.
 *
 * Function names in airacer.c comments are the original addresses; see
 * E:/re-tools/out/AI_P1.md for the decode notes. */
#ifndef OPENUG2_AIRACER_H
#define OPENUG2_AIRACER_H

#include "n2sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- road network --------------------------------------------------------
   Routes<ev><F|B>.bin (loader 0x5deb80): chunk 0x34121 sections (0x8c-byte
   header + n x 0x38-byte points, indexed by the u16 id at +10), 0x34122
   links (32 bytes), 0x34123 sectors (0x114 bytes), 0x34124 (0x108 bytes). */
typedef struct AiNet AiNet;
AiNet *ainet_load(const char *path);
void   ainet_free(AiNet *n);
int    ainet_sections(const AiNet *n);
float  ainet_route_length(const AiNet *n);
/* The race-load pass over the route (0x5ddfc0 + 0x5e8780): racing-line
   corner speeds for the fastest AI car's class, braking / acceleration
   along the line, and (in a race) the side links' kinds by what they gain. */
typedef struct {
    int   klass;                  /* the fastest AI car's performance class 0..3 */
    int   suv;                    /* an AI car is an SUV */
    int   route_type;             /* ainet_route_type() of the race record */
    int   race_route;             /* 0x88f2be: a race route */
    unsigned race_flags;          /* race record +0x94 */
    int   circuit;                /* lapped */
    float lap_len, lap_half;      /* 0x88f2b0 / 0x88f2b4 */
    const unsigned char *class_curves;   /* GLOBALB chunk 0x34a33 (4 x 0x10c) */
} AiRoutePrep;
int    ainet_route_type(unsigned race_flags, int event);
void   ainet_prepare(AiNet *n, const AiRoutePrep *rp);

/* ---- the AI's world: the road net, the AI tuning blob (GLOBALB chunk
   0x34b00, 0x81cb60), the race's flags and every car the AI can see ---- */
typedef struct AiWorld AiWorld;
typedef struct AiRacer AiRacer;
typedef struct {
    int   drag, drift, streetx;   /* 0x89e7d8 0x89e7d9 0x89e7e3 */
    int   x89010c, x890104, x890108, x890114;   /* other race-type flags */
    int   mode;                   /* 0x89e7b0: 0 free roam, 7 outrun, else racing */
    int   circuit;                /* a lapped race */
    float track_len;              /* the lap's route length (0x88f298 +0x18) */
    int   difficulty;             /* 0x89e804: race setup +0x7d (0..3) */
    float shortcut_time;          /* race record +0xe4, -1 none */
    /* the catch-up's inputs (0x4172d0) */
    int   catchup_opt;            /* 0x89e7f8: the race's CATCH UP option */
    int   traffic_level;          /* 0x89e7a8 */
    int   laps;                   /* 0x89e7bc */
    int   sprint;                 /* 0x89e7b4: point to point */
    int   humans;                 /* 0x89e7c0 */
    float catchup_weight;         /* 0x89e7fc[target]: blob[0x94c + (class + difficulty x 4) x 4] */
    int   event;                  /* 0x89e7a0 the event id */
    int   reversed;               /* 0x89e7a4: the race runs its route backward */
} AiRaceSetup;
AiWorld *aiworld_new(const AiNet *net, const void *blob, unsigned seed);
void     aiworld_free(AiWorld *w);
void     aiworld_setup(AiWorld *w, const AiRaceSetup *s);
/* every frame before the cars: game time (s), the race state (*0x890118: 3
   the countdown), whether the race runs (0x5eeef0), its start time (s, -1
   none yet) */
void     aiworld_frame(AiWorld *w, float time, int race_state, int race_active, float race_start);
/* the views' eyes (up to 4, 0x5f1160): a stalled racer is put back on the
   road only out of their sight (50 m) */
void     aiworld_set_views(AiWorld *w, const float *eyes, int n);

/* what the AI reads of a car */
typedef struct {
    float pos[3], vel[3], fwd[3], left[3], up[3], acc[3];
    float heading;                /* atan2(fwd.y, fwd.x) */
    float speed, absv;            /* forward speed, |v| */
    int   wheels;                 /* wheels down */
    unsigned char contact[4];
    int   mover, ctrl_type;       /* N2_MOVER_*, controller type (3 AI racer) */
    float rear_frac, steer_gain;  /* physics record +0x1c0, +0x270 */
    float slip[4][2];             /* each wheel's slip pair (car state +0x374) */
    int   nos_level;              /* nitrous burning */
    int   traffic;                /* a traffic car (racer info +4 == 3) */
    int   wrong_way;              /* a human's player +0x248: going the wrong way */
    float half_len, half_wid;
    float progress;               /* distance along the race route (car +0x8fc) */
    int   active;
    float race_dist;              /* race entry +0x14: laps + fraction past the line */
    int   race_pos;               /* race entry +0xa: standing, 1-based (0 none) */
    int   human;                  /* a human player's car (car +0x18) */
    int   in_race;                /* has a race entry (car +0x1c) */
    float rpm;                    /* engine (car +0x440) */
    int   gear;                   /* car +0x434: 0 R, 1 N, 2 first */
    float band_lo, band_hi;       /* the gear's down- and up-shift rpm (0x5946e0 / 0x594670) */
    float nos_frac, nos_cap;      /* nitrous left (of the bottle), the bottle in seconds */
} AiCarInfo;
void aicar_from_n2(AiCarInfo *c, const N2Sim *sim, const N2State *st, const float prev_vel[3], float dt);
/* slot 0..23; index is the game's car index */
void aiworld_set_car(AiWorld *w, int slot, const AiCarInfo *c, int index);

/* ---- an AI racer: controller, the JustDrive action and the race driver -- */
AiRacer *airacer_new(AiWorld *w, int slot, int skill, const N2Sim *sim, int suv, float half_wid);
void     airacer_free(AiRacer *r);
/* a human player's controller (type 1, 0x4266d0) on the car in `slot`: it
   tracks the car on the road net and predicts its path every frame (the
   plan the AI's collision prediction sees of the human), and keeps the
   player's pace ring (0x602650) the AI's catch-up reads. half_wid: half the
   car's collision box width. Call airacer_human_frame every frame after
   aiworld_set_car. */
AiRacer *airacer_new_human(AiWorld *w, int slot, const N2Sim *sim, int suv, float half_wid);
void     airacer_human_frame(AiRacer *r, float dt);
/* the racer's id (car +10: AI 100 + i) and the race table's block level
   (racer info +0x902) */
void     airacer_set_racer(AiRacer *r, int id, int block);
/* the car was put down (spawned or reset): car +0x9c0 / +0x974 */
void     airacer_placed(AiRacer *r);
/* where a reset puts the car (0x6012b0 -> 0x5fbed0): its route point,
   heading along the section, forward speed <= 8 m/s; 0 without a route
   point. The controller restarts; the caller places the car. */
int      airacer_reset_pose(AiRacer *r, float pos[3], float *heading, float *speed);
/* the current action's type (0 Pass, 1 AvoidAvoidable, 4 Block, 6
   JustDrive, 9 GetUnstuck, 11 IJustGotHit, 13 AccidentRecovery) */
int      airacer_action(const AiRacer *r);
/* the horn pattern the racer asked for is sounding now (car +0x92c..) */
int      airacer_horn(const AiRacer *r);   /* always 0: see 0x5f6190 in airacer.c */
/* a touch this frame: other = the caller's id for the other side */
/* a contact this frame (the pair list 0x86b490): other = the car's index
   or, for a polygon, any id of its own; j the impulse's size (t m/s, summed
   into pair +0x60); wall: 0x5a0160 (a polygon with |normal z| < 0.1) */
void     airacer_contact(AiRacer *r, int other, int other_is_car, float other_absv, const float pt[3], float j, int wall);
/* the car touched a human player's car: full simulation for 5 s */
void     airacer_touched_human(AiRacer *r);
/* the mover the car should run on now (N2_MOVER_PHYSICS / _MELLO) */
int      airacer_mover(const AiRacer *r);
/* the race manager finished the car (0x606cb0 -> 0x42b330): its goal
   becomes Park -- StopDriving brakes it to a stop on its lane (a traffic
   lane for an AI car, the racing line for a human-owned one) */
void     airacer_park(AiRacer *r);
/* a drag race (aidrag.c): the human's car gets a DragController (type 2,
   0x4299c0) -- the AI steers it along the lane picked with the lane-change
   buttons (airacer_drag_lane: right 0 / 1, pressed 0 / 1, 0x4102b0), the
   pad keeps pedals and gears (N2Input.steer_ai); airacer_frame gives the
   steering. aiworld_set_go_in: seconds to GO (0x5ef2e0) for the start. */
AiRacer *airacer_new_drag_player(AiWorld *w, int slot, const N2Sim *sim, int suv, float half_wid);
void     airacer_drag_lane(AiRacer *r, int right, int pressed);
int      airacer_drag_lane_now(const AiRacer *r);
/* diagnostics: the player's last lane move -- the lane the car was nearest,
   where the plan went, and why a step was refused */
void     airacer_drag_why(const AiRacer *r, char *buf, int cap);
/* diagnostics: the contacts the racer is in now -- walls, cars (and the
   first car's id, -1 none) */
void     airacer_contacts(const AiRacer *r, int *walls, int *cars, int *car_id);
/* diagnostics: the controller's road tracking -- its route point (section,
   index, position), direction, path list (count, first / last distance)
   and the driver's plan length, as text */
void     airacer_track_str(const AiRacer *r, char *buf, int cap);
void     aiworld_set_go_in(AiWorld *w, float secs);
/* 0x5eba30(3): a human player's finished car gets an AI racer controller in
   place of its human one (freed by the caller); call airacer_park on it,
   then drive the car with airacer_frame's controls */
AiRacer *airacer_take_over(AiWorld *w, AiRacer *human, const N2Sim *sim, int suv, float half_wid);
/* the car was put down elsewhere (0x5fbd60); call after aiworld_set_car
   gave the AI its new pose */
void     airacer_put_down(AiRacer *r);
/* the controller lost the road and asks to be put back on it */
int      airacer_want_reset(AiRacer *r);
/* the catch-up's multiplier on the car's engine and grip (car +0x468) */
float    airacer_car_catchup(const AiRacer *r);
/* the racer's frame: think when due, then drive; out = the car's controls */
void     airacer_frame(AiRacer *r, float dt, N2Controls *out);
float    airacer_target_speed(const AiRacer *r);
int      airacer_path(const AiRacer *r, float *xyz, int max);   /* xyz NULL: the count */
/* a human controller's pace ring average (player +0xa0) */
float    airacer_pace(const AiRacer *r);

/* ---- the race's traffic (aitraffic.c) ------------------------------------
   TrafficController cars (owner kind 3): placed at random over the route at
   the race start (0x42e2c0), moved near a human by the TrafficTeleporter
   (0x42f910) once out of every view's reach. Each runs airacer_frame like a
   racer; airacer_mover gives its mover (0: off the road net -- not drawn,
   not simulated; N2_MOVER_TRAFFIC; N2_MOVER_MELLO once touched or
   avoiding). Give the AI a traffic car's state with AiCarInfo.traffic = 1,
   ctrl_type 4, in_race 0, active = !aitraffic_dead(). */
typedef struct {
    const unsigned char *track_rec;   /* the race's track record (GLOBALB 0x34201, 0x128 bytes) */
    float start_prog;                 /* the start line's progress, m */
    /* the Paths zone of type 16 at pos (0x5d9bf0), NULL none */
    const unsigned char *(*zone)(void *user, const float pos[2]);
    void *zone_user;
    int   tdiff;                      /* 0x89e7f4: the traffic cars' skill 0 -> 9, 1 -> 4, 2 -> 0 */
} AiTrafficSetup;
void     aitraffic_setup(AiWorld *w, const AiTrafficSetup *s);
/* the track record's traffic count at the world's traffic level (0x4fe9f0) */
int      aitraffic_count(const AiWorld *w);
/* traffic racer k (id 0x32 + k) on the car in `slot`, placed at random
   (0x42e2c0); NULL when no place was found (the original makes no car) */
AiRacer *aitraffic_spawn(AiWorld *w, int slot, int k, const N2Sim *sim, float half_len, float half_wid);
/* a new pose the AI gave the car (its start, a teleport): put the car there
   facing `heading` (radians) at `speed` m/s; 1 when there is one */
int      aitraffic_pose(AiRacer *r, float pos[3], float *heading, float *speed);
int      aitraffic_dead(const AiRacer *r);
void     aitraffic_report(const AiWorld *w);
extern const char *(*airacer_poly_name)(int id);   /* diagnostics: a wall polygon's barrier group */   /* log: traffic standing still */
/* once a frame after the cars: the intersections and the teleporter */
void     aitraffic_frame(AiWorld *w, float dt);

/* ---- the race's reference times (0x5f6e70 / 0x5edd90) ------------------
   The race route located for the table: reset (0x5da2f0), locate a point
   (0x5da330) -> its race fraction (0x5ee9b0 of the locator's percent), the
   progress (m) and the route section of its node; diff = 0x5d5cd0 (a - b,
   wrapped); finish = the finish line's progress (m). */
typedef struct {
    void *user;
    void  (*reset)(void *user);
    float (*locate)(void *user, const float p[3], float *progress, int *sec);
    float (*diff)(void *user, float a, float b);
    float finish;
} AiRefLoc;
/* out[0] the race's reference time (s), out[1 + 2i] the rate (race
   fraction a second) of bin i of 256, out[2 + 2i] the time to reach it */
void  ainet_reftime(const AiNet *n, const float start[3], float route_len, const AiRefLoc *L, float out[513]);
float ainet_reftime_at(const float tab[513], float d);

#ifdef __cplusplus
}
#endif
#endif
