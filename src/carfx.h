/* carfx.h -- the original NFSU2 car shading (car.fx in SPEED2.EXE), decoded
 * 10-09. Material parameters come from GLOBALB's light-material records, the
 * lighting is a 10-term quadratic-form spherical harmonic built per car from
 * a fixed three-light rig plus the nearest track lights. */
#ifndef N2_CARFX_H
#define N2_CARFX_H
#include <stdint.h>

typedef struct {
    float diff_min[4], diff_range[4];   /* rgb + w (w scales the env term) */
    float spec_min[3], spec_range[3];
    float env_min[3], env_range[3];
    float spec_pow, env_pow;
} CarFxMat;

typedef struct { float pos[3], r_in, r_out; uint32_t rgba; } CarFxLight;
/* the world's light list was rebuilt or grew in place (carfx_sh's light grid) */
void carfx_lights_changed(void);
/* Give each completed light-list update its own identity, including when
   an allocator reuses the address of an earlier list. Safe on loader threads. */
uint32_t carfx_new_light_revision(void);
/* Renderer-only counters since the previous call. Elapsed time uses SDL ticks. */
void carfx_light_grid_stats(unsigned *builds, uint64_t *ticks);

/* GLOBALB.BUN light materials (0x00135200 records). Returns how many loaded. */
int carfx_load_materials(const unsigned char *d, long len);
int carfx_loaded(void);
/* NULL when the hash names no light material (or PLAINNOTHING, the paint slot). */
const CarFxMat *carfx_material(uint32_t hash);
/* What the paint slot (PLAINNOTHING) becomes: the car's base paint part's
   light material (attribute 0x6ba02c05, read at 0x638d8e) when one was set
   with carfx_use_paint, else the generic metallic paint. */
const CarFxMat *carfx_paint(void);
/* the light material of the car about to be drawn (0 = the generic one) */
void carfx_use_paint(uint32_t material);
/* Fallback for parts with no usable material: "DEFAULT". */
const CarFxMat *carfx_default(void);

/* sh[10][4]: c0 + c1 x2 + c2 y2 + c3 z2 + c4 xy + c5 xz + c6 yz + c7 x + c8 y + c9 z,
 * in world space (z up). camfwd: camera look direction (the rig turns with it).
 * lights/stride: the district light array (pos[3], r_in, r_out, rgba at the
 * start of each element). Returns the number of track lights used. */
int carfx_sh(float sh[10][4], const float pos[3], const float camfwd[3],
             const void *lights, int nlights, int stride);
/* A nonzero revision belongs to this list. Zero uses legacy invalidation. */
int carfx_sh_revision(float sh[10][4], const float pos[3], const float camfwd[3],
                     const void *lights, int nlights, int stride, uint32_t revision);

/* Shaper light rigs (SetShaperLights): load the 0x0003B700 chunk payload,
 * then pick one by name hash (bStringHash of the uppercase name, see
 * carfx_rig_hash); 0 / unknown = the in-game rig 0x7f3e10. */
typedef struct { uint32_t hash; float l[3][7]; } CarFxRig;
int carfx_load_rigs(const unsigned char *d, long len);
int carfx_use_rig(uint32_t hash);
uint32_t carfx_rig_hash(const char *name);

#endif
