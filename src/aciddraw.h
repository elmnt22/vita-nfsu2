/* aciddraw.h -- draws the Acid particles (acid.h) as SPEED2.EXE 0x48bdf0
 * does: camera billboards first (0x5c0e10 -> 0x5bb430), then plane quads
 * (0x5c7e80), worldnodepth.fx, textures by hash from InGameCommon.bun and
 * GLOBALB.BUN with the TPK record's blend (additive or source alpha). */
#ifndef OPENUG2_ACIDDRAW_H
#define OPENUG2_ACIDDRAW_H

#include "acid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GL context needed; call after acid_load (the textures of every loaded
   definition are fetched here). dataroot: the folder holding GLOBAL/
   (InGameCommon.bun); glob: GLOBALB.BUN's bytes, not kept. */
int  aciddraw_init(const char *dataroot, const unsigned char *glob, long globlen);
void aciddraw_free(void);
/* the GL texture of the cracked window sheet for a window level 1..3
   (WINDOW_DAMAGE0..2), 0 if not loaded */
unsigned aciddraw_window_tex(int level);

/* V: view matrix, PV: projection x view (column-major GL). proj: the
   projection's pixel scale for the screen-size clamp (half viewport height /
   tan(fovy / 2)). incar: the viewing car in a bumper/hood camera (its engine,
   exhaust, mirror and front-wheel effects are not drawn), else NULL. Leaves
   blending off, depth writes on and the program unbound. Returns quads drawn. */
int  aciddraw_draw(const float V[16], const float PV[16], float proj, const AcidOwner *incar);

/* eLightFlare pool (0x48ab20 add, 0x494280 draw, eRenderLightFlare 0x490920,
   flare type 3/4 = the normal view): a light flare of type 0..14 at pos for
   this frame, drawn and cleared by the next aciddraw_draw. */
void aciddraw_light_flare(const float pos[3], int type);

/* A car light flare for this frame (CarRenderInfo::RenderFlaresOnCar
   0x615630 -> eRenderLightFlare, flareType 0, flags 4): world position and
   direction, the car's position (its 4-pixel size test), type (0 headlight,
   1 brake light), intensity (clamped to 1). */
void aciddraw_car_flare(const float pos[3], const float dir[3], const float car[3], int type, float intensity);
/* the last queued car flare's mirror height for the road reflection view
   (0x6159a4: the car matrix's z row applied to the light's local (x, y, -z)) */
void aciddraw_car_flare_mirror(float ez);
/* The road reflection view's light flares (flareType 2): the world's flares
   (mirror height from `ground`, looked up once each: 1 = found, *z the
   topology elevation under p) and the last main pass's car flares, into the
   bound reflection target with the normal camera's V / PV. wet = the rain's
   wetness (rain_wetness). Returns the quads drawn. */
int  aciddraw_reflect_flares(const float V[16], const float PV[16], float proj, float wet,
                             int (*ground)(const float p[3], float *z));
/* The car environment cube face's light flares (flareType 1: the world's
   flares within 63 m, then the pool's), into the bound face with its V / PV;
   proj = the face view's focal length in pixels. Returns the quads drawn. */
int  aciddraw_env_flares(const float V[16], const float PV[16], float proj);

/* The world's light flares (0x80135100 packs, eRenderWorldLightFlares
   0x491860): reset, then add every pack of a track bundle (returns the
   flares added); enable while in the game (retail: game state 6). */
void aciddraw_world_flares_reset(void);
int  aciddraw_world_flares_add(const unsigned char *bundle, long len);
void aciddraw_world_flares_enable(int on);
/* only flares on tiles whose buildings are placed: build focus (cx, cy), radius r (<= 0 off);
   gen = the build that placed them (a new gen fades its new tiles' flares in over 0.9 s), now_ms = the clock */
void aciddraw_world_flares_area(float cx, float cy, float r, unsigned long gen, unsigned now_ms);
/* the walk's section source (FindDrivableSection 0x5e00d0): the visible
   sections of the drivable section under eye, their count (-1 none, -2 no
   section data: the port's old 200 m box pick) */
void aciddraw_world_flares_sections(int (*vis)(const float eye[3], const short **list));
int  aciddraw_flares_pending(void);

#ifdef __cplusplus
}
#endif
#endif
