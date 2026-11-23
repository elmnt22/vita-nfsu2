/* carcoll.h — the retail NFSU2 car-vs-world collision manager, ported from
 * SPEED2.EXE (v1.2) for the player's car against the track's collision
 * polygons (wcoll.c).
 *
 * The original runs one manager step (0x5ffa50) after every body has moved:
 * each pair of overlapping bodies is tested with a GJK distance (0x5b3390)
 * between the car's convex collision hull (<CAR>_CV in GEOMETRY.BIN, chunk
 * 0x80034020) and the polygon as a thin face; touching pairs (distance
 * < 0.001) become contacts, timed by bisecting back between the previous and
 * the current pose (0x5b4be0). The earliest contact is resolved first
 * (0x5b2610): the car is put back at the contact time, one impulse with
 * restitution and friction is applied (0x5af0e0), and the car is simulated
 * again for the time it lost; then its pairs are tested again, up to 20
 * resolutions a frame and 2 per pair. */
#ifndef OPENUG2_CARCOLL_H
#define OPENUG2_CARCOLL_H

#include "n2sim.h"
#include "wcoll.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CarColl CarColl;

/* One resolved contact, for effects (0x5a9620: sparks from the surface
 * table, damage, the collision sound). */
typedef struct {
    float point[3];       /* contact point, world */
    float impulse[3];     /* impulse on the car */
    float slide;          /* tangential speed at the contact, m/s */
    float normal[3];      /* from the car into the polygon */
    int   surface;        /* the polygon's surface id */
    int   wall;           /* polygon flag 0x2 (counts as a hit for steering) */
    int   car, other;     /* carcoll_run_all: the car hit, and the car it hit
                             (-1: a polygon) */
    int   poly;           /* the polygon's id (WCollPoly.id), -1 for a car */
    float poly_nz;        /* the polygon's normal z (0x5a0160: a wall below 0.1) */
    int   wild;           /* CheckForWildCollision 0x59fb70: bit 0 the car,
                             bit 1 the other went into crash mode */
} CarCollHit;

/* Loads <car>_CV from the car's GEOMETRY.BIN. NULL if the car has none (the
 * original then uses a 4.33 x 1.94 x 0.82 m box, 0x5aec20; so does this). */
CarColl *carcoll_load(const char *geometry_bin, const char *car);
CarColl *carcoll_load_mem(const unsigned char *geometry_bin, long len, const char *car);
/* The same for a car with a wide body kit fitted (N2CarConfig parts[N2_PART_WIDE]
 * = 1 + n, KITW0n): <car>_W0n_CV, else <car>_CV. */
CarColl *carcoll_load_kit(const char *geometry_bin, const char *car, int wide);
CarColl *carcoll_load_mem_kit(const unsigned char *geometry_bin, long len, const char *car, int wide);
void     carcoll_free(CarColl *c);
/* A copy with its own step-start pose (one per car). */
CarColl *carcoll_clone(const CarColl *c);

/* 0x593210: remember the pose the frame starts from (before n2sim_step). */
void carcoll_begin(CarColl *c, const N2Sim *s, float dt);

/* 0x5ffa50 for the car after n2sim_step: finds and resolves contacts with
 * the collision polygons around it. Returns the number resolved; up to
 * maxhits of them are reported. */
int  carcoll_run(CarColl *c, N2Sim *s, WColl *w, WCollCtx *wc,
                 CarCollHit *hits, int maxhits);

int  carcoll_vertex_count(const CarColl *c);
/* the hull's bounding radius round the centre of mass (after a begin) */
float carcoll_radius(const CarColl *c);

/* The whole manager step for several cars at once: every car against its
   polygons and against every other car, the earliest contact of all pairs
   resolved first. Car against car (0x5af0e0 with two bodies): restitution
   0.1, friction 0.3, inertia x 0.4, each car taken as at least 1.85 t,
   impulse +I on B and -I on A; both cars are put back to the contact time
   and run again. Each car needs its own CarColl (the step-start pose lives
   there) and its own ground context. */
#define CARCOLL_MAXCARS 64
/* A body that is not a car (a smokeable prop, smokeable.c): the collidable's
   virtual calls the manager makes on it. */
typedef struct {
    void  (*body)(void *o, N2Body *b);
    void  (*pose)(void *o, const float pos[3], const float R[16], const float P[3]);   /* vt+0x30 */
    void  (*advance)(void *o, float t);                                                 /* vt+0x2c */
    /* vt+0x4c ConsiderImpulse: the scale for the normal impulse J at arm r
       (1 = taken whole; an anchored prop breaking loose returns less) */
    float (*consider)(void *o, const float J[3], const float r[3]);
    void  (*impulse)(void *o, const float J[3], const float r[3]);                      /* vt+0x50 */
    /* BeginCollision / EndCollision bookkeeping: touched by a car (1) or not */
    void  (*touch)(void *o, int car);
    float bounce, friction;   /* vt+0x70 / vt+0x74 */
    int   asleep;             /* not moving: no polygon pairs, no pairs with another sleeper */
} CarCollBodyOps;
typedef struct {
    CarColl  *c;
    N2Sim    *s;
    WCollCtx *wc;         /* NULL: no polygons for this car */
    int       nocollide;  /* car +0x17: no contacts at all */
    int       human;      /* car +0x18, for the caller's wake rule */
    int       noprops;    /* no pairs with the props (ops) bodies: they met this car in a run of its own */
    CarCollBodyOps *ops;  /* non-NULL: not a car (s unused), obj is the body */
    void     *obj;
} CarCollCar;
/* carcoll_begin for a body that is not a car */
void carcoll_begin_body(CarColl *c, const N2Body *b, float dt);
/* most prop-against-world resolves in one contacts pass (default: no limit) */
void carcoll_set_prop_world_cap(int cap);
/* a hull by its exact name from a 0x80034020 container in memory (the
   smokeables' hulls in <track>.BUN); NULL when there is none */
CarColl *carcoll_load_named(const unsigned char *bun, long len, const char *name);
/* collidable vt+0x48 (0x593940), called for both cars of every resolved
   contact, before it is solved: other = -1 for a polygon. The original
   switches an AI car to the full simulation for 5 s (ctrl +0x734) here
   when the other car is a human player's. */
typedef void (*CarCollWake)(void *user, int car, int other);
int  carcoll_run_all(CarCollCar *cars, int n, WColl *w, CarCollHit *hits, int maxhits,
                     CarCollWake wake, void *user);
/* The pairs the last run had (its broadphase, 0x5b23b0): {car, other car
   or -1, polygon id or -1} in the run's car numbering. A pair lives while
   its shapes stay near (the collision sound keys its contacts on it). */
int  carcoll_pairs(int (*out)[3], int max);
/* The car-against-car pairs of the last run with their pair state (the
   collision pair list 0x86b490 that MiscStyleManager's near-miss test reads):
   the GJK distance of the last test (+0x10) and whether the pair touched
   (+0x244). In the run's car numbering. */
int  carcoll_car_pairs(int (*ab)[2], float *dist, int *touch, int max);
/* CollisionBody +0x18 (car +0x3c -> +0x18): the body's live Collision
   objects -- one per pair that took an impulse in the last 0.3 s of world
   time (Collision::Update 0x592bd0). body = the CarCollCar's s (a car) or
   obj (a prop). carcoll_world_time advances that clock once per world tick,
   before the tick's runs. */
void carcoll_world_time(float dt);
int  carcoll_body_collisions(const void *body);
int  carcoll_body_world(const void *body);   /* with the world only (#35) */

#ifdef __cplusplus
}
#endif
#endif
