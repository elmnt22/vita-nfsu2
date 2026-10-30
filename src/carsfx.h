/* carsfx.h — the opponents' and traffic's engine sounds, ported from
 * SPEED2.EXE's car audio (RE notes: re-tools/out/AUDIO_AI.md).
 *
 * Main thread only: per drawn frame carsfx_update() runs each AI racer's
 * audio controllers (SFXCTL_AIPhysics 0x47c490 -> AIShifting 0x483130 ->
 * AccelTrans 0x4831a0 -> SFXCTL_Engine 0x47c010) and CARSFX_AIEngine's
 * parameter block (0x4610f0), the traffic engines (CARSFX_TrafficEngine
 * 0x462240, slots 0x463e30), the 3D position / Doppler controller
 * (0x45df00, 0x45e1f0) and the mixmap's distance rolloff (NFS3DMixCtl
 * 0x468580 over SOUND/MIXMAPS/MAPOUTPUT*.mxb). The results go to the
 * audio thread's car voices (audio.h g_carvoice).
 *
 * The effects: the AI racers' turbo (CARSFX_Turbo 0x47cad0 / 0x472770 /
 * 0x45ece0), nitrous (0x484f20), skids (0x460a70 / 0x460c70) and stereo
 * (0x4635b0); the traffic's woosh (0x475e10 / 0x47e8d0), horn (0x47e6e0 /
 * 0x462550), stereo and bottom-out (0x47ed80 / 0x47f100). Woosh and
 * bottom-out are stitches: InGameCommon.bun chunk 0x8003b500 (loader
 * 0x47bc30), layered samples each with its own delay and volume. They
 * play through audio.h g_fxvoice.
 *
 * The collisions of every car, the player's included (the world collision
 * sounds, class 5: CSTATE_Collision / SFXObj_Collision): each resolved
 * contact (carcoll.c) becomes a hit stitch picked by type and strength
 * plus a FX_Scrape loop (carsfx_coll_hit / carsfx_coll_pairs). */
#ifndef OPENUG2_CARSFX_H
#define OPENUG2_CARSFX_H

#include "nis.h"

#define CARSFX_AI_MAX 8     /* AI car manager max (0x4853c0 init(2, 8)) */
#define CARSFX_TR_MAX 6     /* traffic car manager max (init(3, 6)) */
#define CARSFX_TR_REG 16    /* traffic cars the manager tracks */

/* one car, as the sound reads it from the simulation */
typedef struct {
    int   on;               /* the car exists and is simulated */
    float pos[2];           /* car +0x60/+0x64 (world x, y) */
    float fwd[2];           /* car +0x90/+0x94 (heading, not normalised) */
    float speed;            /* car +0x430: |velocity|, m/s */
    float vtop;             /* 0x5b5930 top speed, m/s */
    int   gear, gears;      /* car +0x434 (0 R, 1 N, 2 first ..), forward gears */
    int   nos;              /* nitrous burning (0x5ecdf0) */
    unsigned short steer;   /* driver +0x1e0, 16-bit angle */
    int   nis;              /* a pre-race scene has the engine (0x47c490 -> 0x46fa30) */
    float nis_rpm, nis_thr; /* its rev pattern's rpm / throttle 0..100 (nis_rev_step) */
    float up[10];           /* each gear's upshift rpm (0x5ecd30, n2sim_gear_up) */
    /* for the effects */
    float vel[2];           /* car +0x70/+0x74, m/s */
    float upz;              /* car +0xb8: the body's up axis, z */
    int   contact[4];       /* car +0x390..+0x393 wheel on the ground (FL FR RR RL) */
    int   surface[4];       /* SFXCTL_Wheel +0xb0..: retail surface id per wheel */
    float skid[3];          /* SFXCTL_Skid +0x194..: the skid controller's channels 0..1 */
    int   turbo;            /* record +0x224 boost curve not all 0 (0x457b00) */
    int   cartype;          /* car type (GLOBALB car table order) */
} CarSfxCar;

/* the listener (0x46d7e0): the camera and the player's car, 2D */
typedef struct {
    float cam[2], cam_fwd[2];
    float car[2], car_fwd[2];
    int   mode;             /* mix mode 0x82bb48: 4 = intro scene, else 0 */
    int   racing;           /* race manager state not 0/2/3 (pre-race) */
    float car_vel[2];       /* the player's car +0x70/+0x74, m/s */
    float car_fspeed;       /* its +0x42c, forward speed m/s (signed) */
    int   drag;             /* 0x89e7d8 drag race */
    float cam_z;            /* the camera's height (0x5f1160 is 3D) */
    int   coll;             /* collision sounds on (in the game, not paused) */
    /* the player's turbo (CARSFX_Turbo on the player's car) */
    int   pturbo;           /* fitted */
    int   ptrec;            /* its 0x7f3338 record (audio_player_turbo_rec) */
    float pload, pthr;      /* engine load and throttle, 0..100 */
    float prpm;             /* engine rpm */
    /* the player's wheels, for CARSFX_RoadNoise (SFXCTL_Wheel 0x8008f9d4) */
    int   pwheels;          /* the fields below are filled */
    float pspeed;           /* car +0x430, m/s */
    int   pcontact[4];      /* car +0x390 (FL FR RR RL) */
    int   psurface[4];      /* car +0x394: retail surface id per wheel */
    float pslipv[4][2];     /* car +0x3b4 */
    float pfric[4];         /* car +0x3d4 = tire +0x170 friction use */
    float pupz;             /* car +0xb8: the body's up axis, z (CARSFX_BottomOut) */
    int   pbottom;          /* car sound event 5 (0x5f5d8f) param, 0..32767; 0 none */
    int   pgear;            /* car +0x434 (0 R, 1 N, 2 first ..) */
    int   pjumpcam;         /* the nearest view (0x459ac0) is in camera zone 6, the jump cam (+0x58) */
    int   pspark;           /* car sound event 9 (0x5f6049, the neutral rev flame) this frame */
} CarSfxListener;

/* a car in a collision, as the collision sound reads it */
typedef struct {
    int   id;               /* the caller's car number (0 = the player) */
    float pos[3];           /* car +0x60 */
    float fwd[3], up[3];    /* body rows +0x90, +0xb0 */
    float speed;            /* car +0x430, m/s */
    int   wheels;           /* wheels on the ground (0x5ec1f0 x 4) */
    int   human;            /* car +0x18: a human player's car */
} CarSfxCollCar;

/* Parse the four mixmaps (MAPOUTPUT, DFT, DRG, URL). Returns maps read. */
int  carsfx_init(const char *dataroot);
/* the player's car changed (audio row): its SWTN_CAR_xx_MB.abk -- the
   CAR_Sputter patch and its samples (inside audio_hold or before
   audio_init). Returns 1 when the patch loaded. */
int  carsfx_player_car(const char *dataroot, int row);
/* the SOUND/TURBO banks the AI racers need (0x7f3338 indexes, one per
   distinct bank); valid after carsfx_race_begin. Returns how many. */
int  carsfx_ai_turbo_banks(int tbank[CARSFX_AI_MAX]);
/* A new race field: the mixmap for the race type (0x4869a0: drag DRG,
   URL race URL, drift DFT, else the default) and the AI racers' audio rows
   (audio_car_row of each opponent's car). Picks the AI engine banks
   (0x463f60, at most 3 for the field); bank_slot[] receives the engine
   slots to load (CAR_xx_ENG_MB_SPU.abk), returns how many (0..3). */
int  carsfx_race_begin(int mixmap, int streetx, int drift, const int *rows, int nai,
                       int bank_slot[3]);
/* which loaded bank (0..2) AI car k plays, -1 = none */
int  carsfx_ai_bank(int k);
void carsfx_race_end(void);
/* one drawn frame; dt in seconds */
void carsfx_update(float dt, const CarSfxListener *L, const CarSfxCar *ai, int nai,
                   const CarSfxCar *tr, int ntr);

/* One resolved contact of a collision run src (0x5a9620 -> 0x4814e0 /
   0x4792d0): cars a and b (b < 0: car a against polygon poly), the point,
   the impulse on car a (t m/s) and the sliding speed (m/s). For car vs car
   pass a < b. Call from the physics tick, after carsfx_update has run once
   (it takes the camera from there). */
void carsfx_coll_hit(int src, int a, int b, int poly, const float point[3], const float impulse[3],
                     float slide, const CarSfxCollCar *ca, const CarSfxCollCar *cb);
/* A contact of a smokeable prop (kind 2, stSmokableCollision 0x47a960): a, b,
   poly as for carsfx_coll_hit in the caller's own numbering for src; car: the
   car in it (NULL: the prop against the world or another prop); prop: a
   stable id of the prop (one hit sound per prop at a time); cat: its
   SmokeableCategory (the sound material). */
void carsfx_coll_prop(int src, int a, int b, int poly, const float point[3], const float impulse[3],
                      const CarSfxCollCar *car, int prop, int cat);
/* The water drum's slosh (0x46ae00): claim(prop) answers whether a WATER_DRUM
   prop sloshes at this hit (once in its life; smk_slosh_claim). */
void carsfx_set_slosh_claim(int (*claim)(int prop));
/* After each run of src: the pairs {a, b, poly} it still has. A contact's
   sound state lives while its pair does (0x5ae470). */
void carsfx_coll_pairs(int src, const int (*pairs)[3], int n);

/* the original's integer mix math, exposed for the selftest/oracle */
int  carsfx_mb2lin(int mb);                 /* 0x467ff0 */
int  carsfx_lin2mb(unsigned lin);           /* 0x468050 */
int  carsfx_curve(unsigned type, unsigned x, int db);   /* 0x468160 */

#endif
