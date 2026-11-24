/* rstream.h -- retail TrackStreamer spike (11-23, branch rstream-spike).
 *
 * Streams STREAML4RA sections straight from the shipped bundle the way the
 * PC game does (TrackStreamer + VisibleSectionManager, spec in
 * E:/re-tools/out/rstream/RETAIL_STREAMER_SPEC.md) and draws their scenery
 * with the retail cull and LOD rules (TreeCull / DrawAScenery). Off unless
 * ux0:data/NFSU2/retailstream.txt exists (or OPENUG2_RSTREAM is set); the
 * world tiles stay the default and still supply the ground queries. */
#ifndef OPENUG2_RSTREAM_H
#define OPENUG2_RSTREAM_H
#include "render.h"

/* the switch file / env (read once) */
int  rstream_enabled(void);
/* this track runs on the streamer (enabled and the base city) */
int  rstream_track_ok(const char *trackname);
/* 1 once rstream_start succeeded for the current track */
int  rstream_active(void);
/* main thread: start (or keep) streaming for a track; stop on another */
int  rstream_start(const char *track_root, const char *trackname);
void rstream_stop(void);
/* the scenery-group set: resident_args.scenery_event (cheap when unchanged) */
void rstream_set_event(int event);

typedef struct {
    const RProg *rp;
    const float *MVP;           /* view-projection (column major) */
    const float *cam;           /* eye */
    const float *look;          /* view direction (any length) */
    float hfov43;               /* the retail horizontal FOV (4:3), radians */
    const float (*planes)[4];   /* world_view_planes(MVP) */
    int refl_ready;             /* the wet-road reflection is rendered this frame */
} RsDrawCtx;

/* main thread, once per frame before drawing: zones, wanted list, uploads */
void rstream_update(const float cam[3]);
/* 1 once the area round the last rstream_update position is drawable (or
   nothing streams); *left = sections/texture work still to come */
int  rstream_area_ready(int *left);
/* 1 from a fresh start until its first area is in (20 s at most): the
   loading screen stays over the world meanwhile */
int  rstream_cold(void);
/* a jump (fast travel, a replay's exit): cold again until the new area is in */
void rstream_rewarm(void);
/* main view scenery; returns draws issued. Leaves uMVP = MVP, uModel = I,
   alpha test off, blend off, depth write on. */
int  rstream_draw(const RsDrawCtx *c);
#endif
