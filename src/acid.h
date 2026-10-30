/* acid.h -- the original's particle system ("Acid", SPEED2.EXE 0x486000..0x494000)
 * and the car effects that drive it (0x5f5730 -> 0x61fb00 -> 0x615cc0).
 *
 * Data: emitter definitions (bundle chunk 0x35021, 0x220 bytes each) and
 * emitter templates (chunk 0x35020, 0xd0 bytes each) from GLOBALB.BUN and the
 * track bundle. A car's effect lists are clones of the templates whose
 * definition belongs to the effect's group (bStringHash of "CARFX_NITRO" ...,
 * 0x48b8b0); the ground effects (drive-over, skid smoke, spray, bottom-out)
 * come from the surface table 0x800ee0 by surface (0x883e68).
 *
 * Simulation is exact to the asm (0x491e50 spawn, 0x492e10 tick): 32-byte
 * particles with life in 1/1024 s, speed * 128 and direction * 127 packed,
 * quadratic drag and gravity, the game's xorshift random. Drawing (0x48bdf0)
 * is in aciddraw.c. Notes: E:/re-tools/out/PARTICLES.md. */
#ifndef OPENUG2_ACID_H
#define OPENUG2_ACID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- data --------------------------------------------------------------- */
/* An emitter definition (chunk 0x35021), decoded. */
typedef struct AcidDef {
    uint32_t hash, tex, group;           /* +0x08 +0x0c +0x10 */
    signed char dirmode;                 /* +0x18 0 cone around row 2, else ring */
    unsigned char cols_log2, rows_log2;  /* +0x19 +0x1a flipbook grid */
    signed char fps;                     /* +0x1b flipbook rate, 0 = still */
    unsigned char rand_frame;            /* +0x1c random first frame */
    signed char flip;                    /* +0x1d random mirror bit */
    unsigned char inherit;               /* +0x1e drift with the owner's velocity */
    signed char aspect;                  /* +0x1f size aspect type 0..3 */
    unsigned char orient;                /* +0x20 0 camera billboard, 1..3 plane quad */
    unsigned char always;                /* +0x21 spawns with particles disabled */
    float on_m, on_v, off_m, off_v;      /* +0x24 .. +0x30 */
    float rate, rate_v, life, life_v;    /* +0x34 .. +0x40 */
    float speed, speed_v, rot, cone;     /* +0x44 .. +0x50 (degrees) */
    float k, k_v;                        /* +0x54 +0x58 owner velocity share */
    int   slot;                          /* +0x5c car attach slot */
    float box_c[3], box_s[3];            /* +0x60 +0x70 spawn box, emitter space */
    float vis, grav, drag, maxsize;      /* +0x80 .. +0x8c */
    float ctime[4];                      /* +0x90 colour key times (x3) */
    float ckey[16];                      /* +0xa0 colour keys, RGBA 0..255 */
    float col[16];                       /* +0x100 colour cubic (rows t^3 t^2 t 1) */
    float size[16];                      /* +0x140 size cubic (x size, y spin) */
    char  name[48];                      /* +0x1f0 */
    unsigned int gltex;                  /* draw side (aciddraw.c), 0 = none */
    int   blend;                         /* the texture's TPK blend: 0 none 1 alpha 2 add */
} AcidDef;

/* Register every emitter definition (0x35021) and, when `templates` is set,
   every emitter template (0x35020) found at the top level of a bundle. The
   bytes are copied. Returns the number of records taken. */
int  acid_load(const unsigned char *bundle, long len, int templates);
int  acid_ndefs(void);
AcidDef *acid_def_at(int i);
/* free every definition, template, clone, group and particle */
void acid_shutdown(void);

/* ---- runtime ------------------------------------------------------------ */
typedef struct AcidGroup AcidGroup;

/* Anything particles can drift with (0x492e10 reads owner +0x70). */
typedef struct AcidOwner { float vel[3]; } AcidOwner;

typedef struct AcidEmitter {             /* 0xd0 bytes in the original */
    struct AcidEmitter *next;
    uint32_t hash;                       /* +0x08 */
    int   state;                         /* +0x10 0 off 1 on 2 dead 3 driven */
    float timer, acc;                    /* +0x14 +0x18 */
    const AcidDef *def;                  /* +0x1c */
    AcidGroup *group;                    /* +0x20 */
    uint32_t seed;                       /* +0x24 */
    unsigned short nospawn;              /* +0x28 */
    const float *col;                    /* +0x30 colour cubic override (0x48b920), NULL = the def's */
    float m[16];                         /* +0x40 row vectors, row 3 = position */
    float vel[3];                        /* +0x80 */
    char  name[64];                      /* +0x90 */
} AcidEmitter;

/* Camera positions for the visibility test (0x48bd90: up to 3 views). */
void acid_set_cameras(const float (*pos)[3], int n);
/* g_ParticleSystemEnable (0x870cec); on by default */
void acid_set_enabled(int on);
/* 0x491e50: emit from e for dt seconds (dt < 0: driven, at least one particle) */
void acid_emit(AcidEmitter *e, float dt, const AcidOwner *owner);
/* 0x492e10: run the world emitters' on/off cycle and move every particle */
void acid_tick(float dt);
/* drop every particle and group (race restart, area change) */
void acid_clear(void);
int  acid_live_particles(void);
int  acid_live_groups(void);

/* Iteration for the renderer: groups in draw order, then a group's list
   (0 billboards, 1 plane quads). */
typedef struct AcidParticle {
    struct AcidParticle *next, *prev;
    short life;                          /* +0x08 1/1024 s left */
    unsigned short rot;                  /* +0x0a 65536 per turn, bit 0 mirror */
    unsigned short speed;                /* +0x0c m/s * 128 */
    unsigned char k, typ;                /* +0x0e k*255, +0x0f orient | aspect << 4 */
    float pos[3];                        /* +0x10 */
    signed char dir[3];                  /* +0x1c * 127 */
    unsigned char frame;                 /* +0x1f */
} AcidParticle;
AcidGroup *acid_group_first(void);
AcidGroup *acid_group_next(AcidGroup *g);
const AcidDef *acid_group_def(const AcidGroup *g);
const AcidOwner *acid_group_owner(const AcidGroup *g);
/* the colour cubic a group's particles take (its emitter's override, else the def's) */
const float *acid_group_col(const AcidGroup *g);
const AcidParticle *acid_group_list(const AcidGroup *g, int list, int *count);
const AcidParticle *acid_particle_next(const AcidGroup *g, int list, const AcidParticle *p);

/* ---- car effects (0x638190 car fx object, 0x615cc0, 0x5f5730) ------------ */
enum {
    CARFX_NONE, CARFX_DRIVE_OVER, CARFX_DRIVE_OVER2, CARFX_SKID_SMOKE, CARFX_TIRE_SPEW,
    CARFX_BOTTOM_OUT, CARFX_DAM_RADIATOR, CARFX_DAM_ENGINE, CARFX_BLOWN_TIRE,
    CARFX_DRIVE_ON_FLAT_TIRE, CARFX_NITRO, CARFX_EXHAUST_SMOKE, CARFX_EXHAUST_BLOWOFF,
    CARFX_NOS_BLOWOFF, CARFX_NOS_BLOWOFF_TIRES, CARFX_COUNT = 30
};
#define ACID_SLOTS 30

/* A model marker (0x13401a: bStringHash name, 4x4, row 3 = position). */
typedef struct { uint32_t hash; float pos[3]; } AcidMarker;

typedef struct AcidCar {
    AcidOwner own;                       /* car +0x70 velocity (drift) */
    AcidEmitter *fx[CARFX_COUNT];        /* car fx +0x116: clones per effect id */
    float attach[ACID_SLOTS][8][3];      /* car fx +0x152: attach points, car space */
    unsigned char aflip[ACID_SLOTS][8];  /* the points' x / y / z low bits (1 2 4): mirror that axis */
    int   nattach[ACID_SLOTS];
    float purge_col[2][16];              /* 0x8a3f60 colour slots of the purge's hood / tyre clones */
    float wheel_timer[4];                /* car +0x99c: time each tyre has slid */
    float smoke[4];                      /* car +0x3e4: last skid smoke strength */
    int   flame_armed;                   /* car +0x924: revs dropped, next rev-up flames */
    int   fxlog[CARFX_COUNT];            /* OPENUG2_FXLOG rate limit (frames) */
    /* CarDamage (car +0x958, 0x63a360): 32 sectors round the body's
       outline, each 0..128 (BodyDamage +8), their sum (+0x28, /4096 =
       the damage 0x610ad0), the breakable items' sectors (+0xe8) and
       levels (+0xe0, 2 bits each), the outline box (+0x58 / +0x60) */
    int   dmg_on;
    unsigned char dmg[32];
    int   dmg_sum;
    unsigned dmg_flags;
    signed char dmg_item[15];
    float dmg_min[2], dmg_max[2];
} AcidCar;

/* 0x638190: wheels = the record's four wheel points (P+0x10+0x30i, car
   space); markers = every 0x13401a marker of the car's fitted parts. */
AcidCar *acid_car_new(const float wheel[4][3], const AcidMarker *mk, int nmk);
void acid_car_free(AcidCar *c);

/* The Nitrous Purge part's effects (0x634800, car config +0x818): its
   HOODEMITTER / WHEELEMITTER emitter groups replace the NOS_BLOWOFF (13) and
   NOS_BLOWOFF_TIRES (14) clones, recoloured by HOOD- / TIREHUE, SAT, LUM
   (0x48b920); HOODLEFT / HOODRIGHT put points on the hood markers
   0xe3de69db / 0x601a6cce (slot 28, the right one mirrored in y),
   WHEELLEFT / WHEELRIGHT on the record's first two wheel points pushed
   forward by their radius (slot 29, the left one mirrored). */
typedef struct {
    uint32_t hood_group, wheel_group;    /* HOODEMITTER / WHEELEMITTER, 0 none */
    float hood_hsl[3], tire_hsl[3];      /* HOODHUE SAT LUM / TIREHUE SAT LUM */
    int   hood_left, hood_right, wheel_left, wheel_right;
    int   has_hood[2]; float hood[2][3]; /* the hood markers (left, right), car space */
    float wheel[2][3], radius[2];        /* record P+0x10 / +0x40 and P+0x20 / +0x50 */
} AcidPurge;
void acid_car_purge(AcidCar *c, const AcidPurge *p);

/* 0x61fb00 -> 0x615cc0: fire effect `id` at `intensity`. param3 = (slot << 16)
   | surface: slots 5..8 (the wheels) for ids 3/4/8/9; ids 1..5 take their
   emitters from the surface table. pos/mat: where the car is (row-vector
   4x4, its row 3 replaced by pos); vel: what the particles inherit. */
void acid_car_fire(AcidCar *c, int id, float intensity, int param3,
                   const float pos[3], const float mat[16], const float vel[3]);

/* The per-frame car effects 0x5f5730, from the published car state. */
typedef struct {
    float camdist;            /* 0x5f1160: distance to the camera */
    float dt;                 /* frame time (game clock 0x86518c delta) */
    float speed;              /* car +0x430 forward speed, m/s */
    float pos[3], mat[16];    /* car position and render matrix (rows fwd/left/up) */
    float vel[3];             /* car +0x70 */
    int   contact[4];         /* car +0x390 */
    int   surface[4];         /* car +0x394 */
    float spin[4];            /* car +0x330 wheel spin, rad/s */
    float slipv[4][2];        /* car +0x3b4 */
    float wheel_mat[4][16];   /* car +0x130 wheel world matrices (spray) */
    int   centre_surface;     /* 0x5e72e0 at the car (drive-over), -1 unknown */
    int   nitrous;            /* 0x59fa30: nitrous burning */
    float throttle;           /* |car +0x30 +0x20c| */
    float bottom;             /* car +0x370..+0x37c summed: bump-stop impulses this frame */
    int   gear;               /* car +0x434 (1 = neutral) */
    float rpm;                /* car +0x440 tachometer rpm */
    float rpm_range;          /* engine +0x1f8 - +0x1f0: redline - idle */
    float flame;              /* 0x5995e0(4) turbo level + 0.5 with the junkman turbo: the shift flame */
} AcidCarFrame;
void acid_car_frame(AcidCar *c, const AcidCarFrame *f);

/* CarDamage: the body box (car space x/y min, max) and the model markers
   place the breakable items (InitSectorPositions 0x610e00); then
   acid_damage_hit takes a collision impulse (CarCollisionBody::ApplyImpulse
   0x5a0b00 -> AddDamage 0x593bf0: |J| at the car-space point). Damage
   only counts while TheRaceParameters' damage flag (0x89e7d2) is on. */
void  acid_damage_setup(AcidCar *c, const float bbmin[2], const float bbmax[2],
                        const AcidMarker *mk, int nmk);
void  acid_damage_hit(AcidCar *c, const float local[3], float jmag);
float acid_damage(const AcidCar *c);                 /* 0x610ad0: 0..1 */
void  acid_damage_repair(AcidCar *c);                /* RepairAllDamage */
extern int acid_damage_enabled;                      /* 0x89e7d2 */
/* the collision code's hook: an impulse J (world) at r (world offset from
   the body's centre) on the car simulated by `sim`; main.c routes it */
extern void (*acid_impulse_cb)(const void *sim, const float J[3], const float r[3]);
static inline void acid_car_impulse(const void *sim, const float J[3], const float r[3])
{ if (acid_impulse_cb) acid_impulse_cb(sim, J, r); }

/* ---- emitters owned by others (Smokeable's motion / anchor lists) ------ */
/* afxGetEmitters: clones of every loaded template of definition group
   `group` (a list through ->next; free with acid_free_clones) */
AcidEmitter *acid_clones(uint32_t group);
void acid_free_clones(AcidEmitter *list);
/* afxCopyEmitter 0x48b830: a fresh emitter from t, state 1 (on) */
AcidEmitter *acid_copy(const AcidEmitter *t);
/* AddAutoEmitter 0x48bc50: e runs with the world emitters (acid_tick) from
   now on (its ->next is the manager's); acid_auto_remove takes it out and
   frees it */
void acid_auto_add(AcidEmitter *e);
void acid_auto_remove(AcidEmitter *e);

/* TriggerEffectEmitters (GC 8022cf54..8022d2dc): the clones of one
   definition group, placed at a point on demand */
typedef struct AcidTrigger {
    int on; float pos[3];               /* +0 / +4 */
    AcidEmitter *tmpl;                  /* +0x14 afxGetEmitters (Init) */
    AcidEmitter *live[8]; int nlive;    /* +0x1c the placed copies */
} AcidTrigger;
void acid_trigger_init(AcidTrigger *t, const char *group);
/* SetEffectLocation: 1 when placed (or already there); a z of 0 asks
   ground(x, y, &z) for the height, 0 when that fails */
int  acid_trigger_place(AcidTrigger *t, const float pos[3],
                        int (*ground)(float x, float y, float *z));
void acid_trigger_off(AcidTrigger *t);   /* DisableEffect: its particles go too */
void acid_trigger_free(AcidTrigger *t);  /* the dtor */

/* ---- collisions ---------------------------------------------------------- */
/* 0x592c10: the surface a contact between collidables of surfaces a and b
   counts as (table +0x34 priority; a car is surface 28, CAR, 0x5aeed0). */
int  acid_contact_surface(int a, int b);
/* 0x5dc4c0: fire the surface table's hit (type 5) or scrape (type 6)
   emitters at pt (identity rotation, no owner) at intensity k. */
void acid_world_fx(int type, int surface, float k, const float pt[3], const float vel[3]);
/* 0x5a9620's effects for one resolved contact, within 50 m (x, y) of a
   camera: scrape by the sliding speed (velocity: the faster side, va or
   vb), hit by the impulse's size (t m/s). vb NULL for a polygon. */
void acid_contact(int surface, const float pt[3], const float imp[3], float slide,
                  const float va[3], const float vb[3]);

/* 0x433090: one flag girl hand flare (XECH_FLAG_GIRL_FLARE) at pos, its
   particles thrown 2 m/s along dir. Its light (0x48ab20) is not ported. */
void acid_flare(const float pos[3], const float dir[3]);

/* surface table 0x800ee0: the seven ground effects' group names and ranges
   (drive-over, drive-over 2, skid, spray, bottom-out, hit, scrape) */
typedef struct {
    const char *name;
    const char *fx[7]; float lo[7], hi[7];   /* DO DO2 SK SP BO hit scrape */
} AcidSurface;
extern const AcidSurface acid_surfaces[29];

#ifdef __cplusplus
}
#endif
#endif
