/* smokeable.h — the retail NFSU2 smackable roadside props ("Smokeables"),
 * ported from SPEED2.EXE (v1.2) with the GameCube Speed.elf symbols.
 * Facts: E:/re-tools/out/SMOKEABLE.md.
 *
 * A prop lives in three forms. Its scenery instance is drawn by the world
 * until a spawner for it is spawned; a spawned, untouched prop is a
 * VirginSmokeable (only an axis-aligned box in the collision world) and the
 * prop draws at its authored place; the moment any moving body's xy box
 * overlaps that box (MaybeCreateWitnesses 0x5b23b0) the virgin becomes a live
 * Smokeable (ConvertToSmokeable 0x5b1390): a rigid body with the info's mass,
 * inertia and GJK hull, asleep until the first impulse wakes it. Anchored
 * props (street signs) stay a wall until one impulse beats their anchor.
 *
 * In the port the world tiles mark every mesh of a smokeable's scenery
 * instance with world_role 3 (smk_is_prop): those meshes keep their
 * textures but stay out of the world batches, and this module draws them
 * with the prop's transform. A spawner is "loaded" while the active resident
 * holds its meshes (the original spawns a section's pack when the section
 * loads). */
#ifndef OPENUG2_SMOKEABLE_H
#define OPENUG2_SMOKEABLE_H
#include <stdint.h>
#include "nfsu2.h"
#include "render.h"
#include "carcoll.h"
#include "wcoll.h"
#ifdef __cplusplus
extern "C" {
#endif

/* LoaderSmokeables 0x5b0e30: the infos (0x34026) and hulls (0x80034020) of
   <root>/<stem>.BUN, the scenery overrides (0x34107) and the spawner packs
   (0x34027) of STREAM<stem>.BUN. Replaces what was loaded. 0 = none. */
int  smk_load(const char *track_root, const char *stem);
void smk_free(void);
/* Is this scenery instance (section, index) a smokeable's? Read-only after
   smk_load: safe from the world build worker. */
int  smk_is_prop(unsigned section, unsigned instance);

/* World::World -> SpawnSmokeables 0x5b4220: every spawner enabled again,
   every live prop gone (a new world: a race start or end, a track load). */
void smk_world_reset(void);

/* The active resident (once per frame; acts when the generation changes):
   which spawners are loaded, their meshes and textures. */
void smk_resident(const N2Scene *scene, const GLuint *mesh_tex,
                  const unsigned char *mesh_mode, unsigned long generation);

/* ---- the frame, from the player's 60 Hz tick ---------------------------- */
/* Smokeable::Move for every live prop over dt: step-start poses for the
   collision manager, Euler substeps of 1/60 s, damping, sleep. */
void smk_begin(float dt, WColl *w);
/* MaybeCreateWitnesses for a moving body with an xy box of +-rad round pos:
   virgins it touches become live props. */
void smk_touch(const float pos[3], float rad);
/* The live props as collision bodies (cars[] entries with ops set); with
   polygons (wc) for the run that owns them. Returns the count added. */
int  smk_bodies(CarCollCar *out, int max, int with_polygons);
/* After the collision runs: CheckTimers (2 s revert, lifetime) and
   SmokeableThink (DeleteFarAwaySmokeableCollisionBodies) round eye. */
void smk_end(const float eye[3]);
/* Is cars[i] of a run one of ours? */
int  smk_is_body(const CarCollCar *k);
/* One of ours: its sound id (> 0, stable while it lives) and category
   (SmokeableCategory, the collision sound's material); else 0. */
int  smk_body_sound(const CarCollCar *k, int *cat);
/* One of ours: its collidable surface (vt+0x58 0x5b1ca0 = info +0xd0, the
   terrain type) and velocity, for the contact effect 0x5a9620; else 0. */
int  smk_body_contact(const CarCollCar *k, int *surface, float v[3]);
/* stSmokableCollision::BeginImpact (0x46ae00): a WATER_DRUM prop (sound id
   serial) whose motion emitters have started sloshes once in its life;
   1 = start the slosh now (marks it played). */
int  smk_slosh_claim(int serial);

/* Draw the loaded and live props (opaque, cutout, then blended meshes) after
   the world's opaque and cutout batches, under the world program. planes:
   the view frustum (NULL: no test). */
void smk_draw(const RProg *rp, const float MVP[16], const float cam[3], float view_dist,
              const float (*planes)[4]);

/* Per-run counters for the log line. */
void smk_report(void);

#ifdef __cplusplus
}
#endif
#endif
