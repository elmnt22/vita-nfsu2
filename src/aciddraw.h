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

/* V: view matrix, PV: projection x view (column-major GL). proj: the
   projection's pixel scale for the screen-size clamp (half viewport height /
   tan(fovy / 2)). incar: the viewing car in a bumper/hood camera (its engine,
   exhaust, mirror and front-wheel effects are not drawn), else NULL. Leaves
   blending off, depth writes on and the program unbound. Returns quads drawn. */
int  aciddraw_draw(const float V[16], const float PV[16], float proj, const AcidOwner *incar);

#ifdef __cplusplus
}
#endif
#endif
