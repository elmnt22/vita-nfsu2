/* wcoll.h — the retail NFSU2 collision world, ported from SPEED2.EXE (v1.2).
 *
 * The game does not collide against the render meshes. Every drivable track
 * section has its own collision pack (chunk 0x80034130 in STREAM<track>.BUN):
 * a 32 m grid of cells, each cell listing compact 3/4-vertex polygons with a
 * 4x4 sub-cell mask, and every polygon carries a surface id into the retail
 * surface table (0x800ee0: ROAD, DIRT, GRASS, SOLID_WALL, ...). Walls are
 * vertical polygons in the same packs; race barriers are polygons that only
 * become solid when their scenery group (BARRIERS_<race>, PLAYER_BARRIERS_<race>,
 * BARRIERS_CAREER<stage>) is switched on.
 *
 * Which pack is live follows the track-section boundaries (chunk 0x34152 in
 * <track>.BUN) through the section tracker 0x5dfeb0 / 0x5e68d0.
 * The ground query is 0x5e72e0. */
#ifndef OPENUG2_WCOLL_H
#define OPENUG2_WCOLL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WColl WColl;

/* Per-collider state: the section tracker and the four most recently used
 * ground polygons (world object +0x0c..+0x18 and +0x2bc..+0x2cc). */
typedef struct {
    float tp[2], tr;      /* tracker: last re-check point and its safe radius */
    int   bnd;            /* boundary the point was last found in, -1 none */
    int   pack;           /* live collision pack, -1 none */
    int   ncache;
    int   cache[4];       /* MRU polygons of the live pack */
    int   nogroups;       /* the collider's barrier group list (+0x2f0) is empty: 0x5f54a0
                             fills it only for human (1) and racer (2) owned cars, so
                             traffic never meets BARRIERS_ / PLAYER_BARRIERS_ polygons */
} WCollCtx;

/* Loads the collision packs of TRACKS/STREAM<stem>.BUN and the boundaries and
 * barrier groups (chunk 0x3412f) of TRACKS/<stem>.BUN. NULL when the track
 * has none. */
WColl *wcoll_load(const char *stream_bun, const char *track_bun);
void   wcoll_free(WColl *w);
void   wcoll_cache_thread(void);   /* the thread whose gathers use the polygon cache (main) */
int    wcoll_pack_count(const WColl *w);

void wcoll_ctx_init(WCollCtx *c);

/* Barrier groups (0x5d80f0): turn a scenery group's polygons solid by name;
 * clear switches every group off again (free roam). */
int  wcoll_enable_group(WColl *w, const char *name);
void wcoll_clear_groups(WColl *w);
const char *wcoll_poly_group(const WColl *w, int id);   /* diagnostics */
/* (port) the xy footprints (x0 y0 x1 y1 per wall) of a group's polygons, for
 * the GPS route search to keep out of the lock walls. Returns the count. */
int  wcoll_group_walls(const WColl *w, const char *name, float *seg, int max);

/* VisibleSectionManager::FindDrivableSection 0x5e00d0: the visible-section
 * list (chunk 0x34153) of the drivable section under p (xy). Returns its
 * count with *list set, -1 when p is in none, -2 when the track has no
 * section data. Main thread. */
int wcoll_visible_sections(WColl *w, const float p[2], const short **list);

/* 0x5e72e0: height of the collision surface under p (p[2] is the reference
 * height; a layer up to 1 m above it still counts). found = 0 when the point
 * has no polygon (or no live pack): then the result is p[2], surface 1 and
 * normal (0,0,-1), as the original returns. */
float wcoll_ground(WColl *w, WCollCtx *c, const float p[3],
                   int *surface, float normal[3], int *found);

/* One collision polygon in world space (the 0x80-byte record 0x5d7f10
 * expands): 3 or 4 counter-clockwise vertices, unit normal n and plane
 * distance d (n.p = d), flags (0x2/0x8 walls, 0x4 vertical +-2048 m) and the
 * surface id. */
typedef struct {
    float v[4][3];
    float n[3], d;
    int nv, flags, surface;
    int id;               /* pack << 16 | polygon */
} WCollPoly;

/* 0x5e0d30: polygons near a body whose xy box is [mn, mx] (pos selects the
 * live pack, as the body's own collider does). Returns the count. */
int wcoll_gather(WColl *w, WCollCtx *c, const float pos[3], const float mn[2],
                 const float mx[2], WCollPoly *out, int max);

/* Retail surface table 0x800ee0 (29 entries, stride 0x98). */
#define WCOLL_NSURF 29
typedef struct {
    const char *name;
    float skid_width;     /* +0x14 skid-mark width scale */
    float bump_freq;      /* +0x18 road-noise lattice frequency, 1/m */
    float bump_amp;       /* +0x1c road-noise amplitude, m */
    float grip;           /* +0x24 */
    float slide;          /* +0x28 */
    float rolling;        /* +0x2c rolling resistance scale */
    float drag;           /* +0x30 */
    float priority;       /* +0x34 */
} WCollSurface;
extern const WCollSurface wcoll_surfaces[WCOLL_NSURF];

/* 0x5d3ca0: per-surface road noise at (x, y), metres, added to the ground
 * height under each wheel (wheel +0x168). */
float wcoll_bump(int surface, float x, float y);

#ifdef __cplusplus
}
#endif
#endif
