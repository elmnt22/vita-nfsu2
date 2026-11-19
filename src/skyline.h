#ifndef OPENUG2_SKYLINE_H
#define OPENUG2_SKYLINE_H
/* Always-resident skyline (Vita pop-in work, 11-14).
 *
 * The streamed world ends about 400 m out, and its tall far-LOD stand-ins
 * reach 650 m (main.c standin_k). Past that the city stopped. The skyline is
 * every stand-in of the track at least SKYLINE_MIN_H tall, in 128 m cells,
 * flat-coloured (vertex prelight x its texture's average colour), so a cell
 * is one untextured draw. It is built once from the tile pack into
 * cache/tiles/<track>_sky_v*.bin by a background thread, loaded at each world
 * start, and drawn only for cells wholly past SKYLINE_INNER_M (closer ones
 * are the streamed stand-ins'), with a polygon offset so a streamed stand-in
 * of the same building wins where both draw.
 *
 * ux0:data/NFSU2/noskyline.txt switches it off. */
#include "nfsu2.h"
#include "render.h"

/* Main thread, at a world start: load (or begin building) the track's skyline. */
void skyline_start(const char *track);
/* Main thread, once a frame: uploads a few cells once the loader is done. */
void skyline_pump(void);
/* The uploaded cells (empty until ready). */
const N2Batch *skyline_batches(int *n);
/* Main thread, inside the world pass: draws the cells wholly past the inner
   edge (a whole 256 m block in one draw when it can); begin() runs before the
   first draw (state setup). Returns the draws. */
int skyline_draw(const float cam[3], int (*in_view)(const float lo[3], const float hi[3], void *ctx),
                 void *ctx, void (*begin)(void *ctx));
float skyline_inner(void);
float skyline_outer(void);
/* Main thread: forget the GL buffers (a track switch / GL teardown). */
void skyline_reset(void);
void skyline_cache_file(const char *track, char *out, size_t n);   /* its cache file path */
int  skyline_build_file(const char *track, char *out, size_t n);   /* Expansion Prep Tool */
void skyline_forget(const char *track);   /* drop its stand-in cache */
/* Main thread, from the world texture binder: note a decoded texture's mean colour. */
void skyline_texavg_note(uint32_t key, const N2Tex *t);
#endif
