/* n2sim.h — the retail NFSU2 car simulation, ported from SPEED2.EXE (v1.2).
 *
 * This is the game's own handling model, not an approximation: a rigid body,
 * four wheels on four suspensions, engine, drivetrain with clutch state
 * machine and automatic gearbox, and aerodynamics, stepped together with the
 * same explicit-Euler substeps (h <= 0.0044 s) the original uses. Every
 * coefficient comes from the car's GLOBALB.BUN physics record or from tables
 * copied out of the executable; function names in comments are the original
 * addresses (see memory/openug2-physics-re.md for the decode notes).
 *
 * Units are the game's: metres, seconds, tonnes, kN, kN*m. World +Z is up,
 * body rows are x forward, y left, z up (row-vector matrices, world = v*R).
 *
 * Not part of this file: walls and car-to-car contacts (the caller pushes the
 * body, calls n2sim_set_velocity and reports the hit with n2sim_collision),
 * and the ground (the caller supplies a probe). The player controller (pedal
 * layer 0x407a30, pad steering 0x416bb0) is ported in n2sim_input. */
#ifndef OPENUG2_N2SIM_H
#define OPENUG2_N2SIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Ground under a wheel. Return 1 and fill z (support height at x,y), the unit
 * normal and a surface index into the retail surface table (0 = road), or 0
 * when there is no ground. zref is the wheel mount height, so a probe can pick
 * the right layer of a stacked road. */
typedef int (*N2GroundFn)(void *user, float x, float y, float zref,
                          float *z, float normal[3], int *surface);

typedef struct N2Sim N2Sim;

/* One car's GLOBALB 0x34600 record: header, stock physics (P = rec+0x110,
 * 0x2b0 bytes) and the performance-upgrade targets after it. */
#define N2_REC_SIZE 0x890

/* rec points at the car's GLOBALB 0x34600 record (the 0x40-byte header that
 * starts with the CARS\<NAME>\GEOMETRY.BIN path); the record is copied. */
N2Sim *n2sim_create(const unsigned char *rec, N2GroundFn ground, void *user);
void   n2sim_destroy(N2Sim *s);

/* Place the car: pos is the ground point under the car's origin, heading is
 * the yaw (radians, 0 = +X). The suspension starts at its static ride height,
 * all velocities are zero and the gearbox is in first. */
void n2sim_reset(N2Sim *s, const float pos[3], float heading);

/* The player's raw controls, what the retail input handlers store in the
 * player controller: throttle/brake/handbrake pedals 0..1, steer the stick
 * -1..1 (+ = left). manual selects the manual transmission (mode 1 of
 * 0x407280); shift is a shift-button press this frame (+1 up, -1 down).
 * n2sim_input runs the controller (0x41e2c0): brake held at a standstill
 * selects reverse in either mode and gas leaves it, the shift buttons never
 * leave reverse and never go below neutral. hold is the start-line hold
 * (controller +0x2b4): neutral and brake on, the gas revs the engine, and
 * releasing it drops into gear. Call once per frame, before n2sim_step. */
typedef struct {
    float throttle, brake, handbrake, steer;
    int   manual, shift, hold;
    int   nitrous;          /* nitrous button held */
    /* the DragDriver (0x4220a0): the race driver steers (its 16-bit wheel
       angle in steer_u16), the pad gives the pedals and the gears */
    int   steer_ai;
    unsigned short steer_u16;
} N2Input;
void n2sim_input(N2Sim *s, const N2Input *in);
/* Nitrous: fit performance package level 0..3 (none, 1/3, 2/3, full of the
 * record's upgrade targets; refills the bottle), and refill (0x591cd0). */
void n2sim_set_nitrous(N2Sim *s, int level);
void n2sim_nitrous_refill(N2Sim *s);
void n2sim_nitrous_add(N2Sim *s, float amount);   /* amount of a bottle */
/* nitrous left as a fraction of the bottle (0x5925b0) */
/* A wall or car hit this frame: the steering limit eases back over 1 s. */
void n2sim_collision(N2Sim *s);
/* The ground probe is cheap (the retail collision packs): query it every
   substep as the original does instead of once per frame. */
void n2sim_ground_exact(N2Sim *s, int on);

/* One frame (0x5b72d0): pre-step, int(dt/0.0044)+1 substeps, publish.
   In the Mellovator (0x5aad90) int(dt/(1/70))+1 substeps. */
void n2sim_step(N2Sim *s, float dt);

/* Which car model runs the car (car +0x28, switched by 0x5f46a0): the full
   simulation, or the Mellovator -- the original's cheaper model for AI
   racers (one spring, a linear tyre and a spin per wheel, engine speed from
   the driven wheels). Switching carries the body, wheel spins and gear
   over, as the new mover's constructor reads them from the car state. */
#define N2_MOVER_PHYSICS 1
#define N2_MOVER_TRAFFIC 2     /* TrafficMover 0x408820: kinematic bicycle on the ground */
/* (n2sim_traffic_place below puts a traffic car down on it) */
#define N2_MOVER_MELLO   3
/* NISMover (ctor 0x5b6fc0, "NISMvr"): the pre-race scene drives the car.
   A Mellovator body and chassis whose tyres give no grip (0x5ab540: the
   wheels only hold the car up); each frame the driver 0x5a44a0 sets the
   body's velocity toward the scene's target (n2sim_nis_target). */
#define N2_MOVER_NIS     5
void n2sim_set_mover(N2Sim *s, int mover);
/* 0x5a40a0: the scene's car target this frame -- the point (x, y) the
   car's centre of mass is to reach, the heading (16-bit angle), how many
   targets came before this one (slot place count, 0 on the first) and the
   frame time. More than 1.1 m or 15 degrees off puts the car down there
   (0x5fbd60); otherwise the target is stored (blended 3:1 with the last
   one after the 4th) and the front wheels steer toward the heading. */
void n2sim_nis_target(N2Sim *s, float x, float y, unsigned short heading, int count, float dt);
/* the scene's event track on a car on the NIS mover (0x4392e0 types 4..6,
   only while the car's mover is 5): N2_NISEV_BRAKE_LOCK a = front-wheel
   spin scale (0x598ed0, chassis +0x250), N2_NISEV_BURNOUT a = extra turns a
   second on the driven wheels (0x598ee0, +0x254), N2_NISEV_TORQUE a / b =
   torque along the car's forward / left axis (0x5a4080, +0x258 / +0x25c,
   applied by 0x5a46a0). Returns 0 when the car is not on the NIS mover.
   Types 2 / 3 work on any mover: N2_NISEV_HYD_BOUNCE / _EXTEND a = wheel
   (0 FL, 1 FR, 2 RR, 3 RL), b = the control's bounce / extend value
   (hydraulics 0x5a2fd0). */
enum { N2_NISEV_HYD_BOUNCE = 2, N2_NISEV_HYD_EXTEND = 3,
       N2_NISEV_BRAKE_LOCK = 4, N2_NISEV_BURNOUT = 5, N2_NISEV_TORQUE = 6 };
int  n2sim_nis_event(N2Sim *s, int what, float a, float b);
int  n2sim_mover(const N2Sim *s);
/* the current gear's down- and up-shift rpm (AI nitrous gate 0x402590) */
void n2sim_gear_band(const N2Sim *s, float *lo, float *hi);
/* gear g's upshift rpm, without the shift randomness (0x5ecd30) */
float n2sim_gear_up(const N2Sim *s, int g);

/* An AI driver's outputs, what it writes into the car's driver object
   (0x5eb910 throttle +0x20c, 0x5eb8f0 brake +0x214, 0x5eb900 handbrake
   +0x210, steering +0x1e0 as a 16-bit angle, gear request vt+0x20: -1
   automatic, 0 reverse). Replaces n2sim_input for cars the AI drives. */
typedef struct {
    unsigned short steer;
    float throttle, brake, handbrake;
    int   gear_req;
    int   nitrous;
    int   clutch_kick;      /* drivetrain +0x74 (drift driver 0x416120) */
} N2Controls;
void n2sim_controls(N2Sim *s, const N2Controls *c);
/* The car's performance level 0..10 (0x5b6990(1,-1)), which sets the
   Mellovator's tyre stiffness (+0x144 = 0.25 .. 0.5). */
void n2sim_set_perf(N2Sim *s, float level);
/* Raw rating that level is normalised from (PerfConfig +8, 0x5b4dc0):
   level = (rating - min) / (max - min) * 10 with GLOBALB 0x34a32 [10],[11]. */
float n2sim_accel_rating(const N2Sim *s);
/* The PerfConfig acceleration curve (+0x55c, 0x5b5380): 64 points of
   drive force / mass over [0, top speed]; returns the top speed. */
float n2sim_accel_curve(const N2Sim *s, float out[64]);
/* A float of the car's physics record (P + off). */
float n2sim_param(const N2Sim *s, int off);
/* AI catch-up multiplier (car +0x468), 1 = none. */
void n2sim_set_catchup(N2Sim *s, float mult);
/* The race type the car's physics and the player's controller branch on:
   driftlike = a drift-type race (0x89e7d9 and kin): aero downforce by the
   4th power (0x59e620), no grip x0.85 / rear assist (0x5a4d80), the drift
   yaw assist 0x5ac260 (ai picks its graph; difficulty 0x89e7f4 0..2), the
   player's drift steering and handbrake clutch kick (0x41e2c0). */
void n2sim_set_race(N2Sim *s, int driftlike, int ai, int difficulty);
/* A drag race (0x89e7d8 || 0x890108): the upshift cap at the limiter
   (0x594670), every manual upshift graded (0x594f20 / 0x5a1700: the grade
   speeds the clutch and adds throttle for 0.5 s), the engine's over-rev heat
   while the race runs (0x5a11e0; blown at 1.2). klass: the performance class
   (DriveTrain +0xf0); shift_win: +0xe4 (a human's by difficulty 0x7a19e8);
   clutch_rate: +0xe8 (an AI's 0x7a19f4); rnd: the shift-point random ranges
   +0xcc..+0xe0 (NULL keeps them). */
void  n2sim_set_drag(N2Sim *s, int drag, int klass, float shift_win, float clutch_rate, const float rnd[6]);
/* the race state (*0x890118): 4 = racing */
void  n2sim_set_race_state(N2Sim *s, int state);
/* a street X race (0x89e7e3): the Mellovator's tyre cap 2 (0x5a38e0), ROAD
   surface in the car state (0x59cb90) */
void  n2sim_set_streetx(N2Sim *s, int on);
float n2sim_engine_heat(const N2Sim *s);   /* -1 off, 0..1.2, 2 blown */
int   n2sim_engine_blown(const N2Sim *s);
/* 0x594f20 unrounded for the current gear at `rpm` (the HUD's shift light);
   the last upshift's grade (1 perfect, 0.85 / 0.5 good, 0 early, -1 late) */
float n2sim_shift_grade(const N2Sim *s, float rpm);
float n2sim_last_shift(const N2Sim *s);
/* what the drag tach widget (0x4c7090) reads off the drivetrain: gear g's
   0x594f20(rpm, g, 0) and shift window 0x594f00(g); the shift in progress
   (+0x48, 0x5ecd70) and the shift cut running (+0x5c > 0, 0x5ece20) */
float n2sim_shift_q(const N2Sim *s, float rpm, int gear);
float n2sim_shift_win(const N2Sim *s, int gear);
int   n2sim_shifting(const N2Sim *s);
int   n2sim_shift_cut(const N2Sim *s);

/* What the rest of the game needs. pos is the car origin (record frame), on
 * the same ground-reference as n2sim_reset. */
typedef struct {
    float pos[3];           /* car origin, world                              */
    float rot[9];           /* body rows: forward, left, up                   */
    float vel[3];           /* m/s, world                                     */
    float angvel[3];        /* rad/s, world                                   */
    float heading;          /* atan2(forward.y, forward.x)                    */
    float speed;            /* forward speed, m/s (carstate +0x3ec)           */
    float rpm, rpm_tach;    /* engine, and the smoothed tachometer (+0x400)   */
    float throttle;         /* engine throttle after shaping                  */
    float nos_frac, nos_cap; /* nitrous left (0..1 of the bottle), bottle s   */
    int   nos_on;           /* nitrous burning this frame                     */
    int   gear;             /* 0 = R, 1 = N, 2 = 1st ...                      */
    int   gears;            /* forward gear count                             */
    int   wheels_down;      /* carstate +0x3e4                                */
    float wheel_spin[4];    /* rad, for drawing                               */
    float wheel_w[4];       /* wheel spin rate, rad/s (carstate +0x2f0)       */
    float wheel_mat[4][16]; /* wheel world matrices, rows (car +0x130): the    */
                            /* Mellovator's are the body turned by the steer */
    float wheel_steer[4];   /* rad                                            */
    float wheel_comp[4];    /* suspension compression, m (+ = up)            */
    float wheel_slip[4];    /* friction use 0..1 (+0x170), skids/smoke       */
    float wheel_lslip[4];   /* longitudinal slip speed, m/s (+0x118)          */
    float wheel_load[4];    /* kN                                             */
    int   wheel_contact[4];
    float wheel_pos[4][3];  /* wheel centres, world                           */
    /* tyre marks, 0x59dbb0: a wheel marks the road while it slides or uses
       more than 8 kN of grip, in contact, its patch moving over 1 m/s.
       skid = mark strength 0.3..1 (0 = no mark), width in metres, at the
       contact point. */
    float wheel_skid[4], wheel_skid_w[4], wheel_cpt[4][3];
    float wheel_latv[4];    /* contact patch speed along the axle, m/s        */
    float wheel_slipv[4][2]; /* +0x100: longitudinal slip while sliding (else
                                0) and lateral speed, m/s -- what the skid
                                sound (0x46ea50/0x46dfe0) listens to         */
    int   wheel_sliding[4];
    int   wheel_surface[4]; /* retail surface id under each wheel (+0x114)    */
    float vtop;             /* 0x5b5930 top speed, m/s                        */
    int   mover;            /* N2_MOVER_PHYSICS / N2_MOVER_MELLO / _NIS       */
} N2State;
void n2sim_state(const N2Sim *s, N2State *out);

/* Collision hooks: move the car origin / replace the linear velocity after a
 * wall or car push (the original's own collision code does the same through
 * RigidBody::SetPosition 0x59b8b0 and the momentum fields). */
void n2sim_set_position(N2Sim *s, const float pos[3]);
void n2sim_set_velocity(N2Sim *s, const float vel[3]);
/* a traffic car put down on the TrafficMover at pos, facing heading
   (radians), moving at speed m/s along it */
void n2sim_traffic_place(N2Sim *s, const float pos[3], float heading, float speed);

/* The rigid body as the collision manager sees it (carcoll.c; the car's
 * collidable, vtable 0x7a2470, reads RigidBody +0x20..+0x1b0). */
typedef struct {
    float pos[3];          /* centre of mass, world (+0x20) */
    float R[16];           /* orientation, rows forward/left/up (+0x30) */
    float p[3], L[3];      /* linear / angular momentum (+0x70 +0x80) */
    float v[3], w[3];      /* velocity, angular velocity (+0xa0 +0xc0) */
    float mass, invmass;   /* +0x164 +0x168 */
    const float *Iinv;     /* inverse inertia in use (+0x1b0 grounded, +0x170) */
    float com[3];          /* centre of mass in model space */
    int   wheels_down;     /* car state +0x424 */
} N2Body;
void n2sim_body(const N2Sim *s, N2Body *o);
/* 0x593630: put the body back at a pose with a momentum (angular momentum
 * kept), then refresh v and w from p and L (vtable +0x38). */
void n2sim_body_pose(N2Sim *s, const float pos[3], const float R[16], const float p[3]);
/* 0x59bd80: impulse J at arm r from the centre of mass: p += J (|J| <= 1000),
 * L += (r x J) (|r x J| <= 10) * ang; then v and w are refreshed. */
void n2sim_body_impulse(N2Sim *s, const float J[3], const float r[3], float ang);

#ifdef __cplusplus
}
#endif
#endif
