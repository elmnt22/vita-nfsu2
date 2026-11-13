/* nisdraw.h -- draws the pre-race scene's characters (nis.c v1.3): their
 * skinned solids from the scene bundle (0x80134000, 60-byte vertices with
 * three bone weights and indices into a 16-bone palette per slice), skinned
 * on the CPU as the game's worldbone.fx does it on the GPU (0x5c5930 ->
 * 0x5bdcd0 BlendMatrices), lit by the car.fx light rig at the pelvis
 * (0x61d150 -> 0x48f600), and the foot shadows (0x433220, CHARACTERSHADOW). */
#ifndef OPENUG2_NISDRAW_H
#define OPENUG2_NISDRAW_H

#include "nis.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NisDraw NisDraw;

/* The models and textures of every character of the scene (GL context
   needed). NULL when the scene has none or nothing could be loaded. */
NisDraw *nisdraw_load(const Nis *n);
void nisdraw_free(NisDraw *d);

/* V and PV: the frame's view and projection x view (column-major, as the
   world batches use them); lights/stride: the district light array for
   the light rig (carfx_sh). Leaves GL_DEPTH_TEST on, blending off, and the
   program unbound; the caller binds its own program again. Returns the
   characters drawn. */
int nisdraw_draw(NisDraw *d, const Nis *n, const float V[16], const float PV[16],
                 const void *lights, int nlights, int stride, uint32_t light_revision);

#ifdef __cplusplus
}
#endif
#endif
