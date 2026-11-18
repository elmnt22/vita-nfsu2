/* xpcity.h -- expansion cities (Rockport, Palmont): one-time translation on the console. */
#ifndef XPCITY_H
#define XPCITY_H
/* track is an expansion city (STREAML2RA / STREAML5RA) */
int xpcity_is_city(const char *track);
const char *xpcity_region(const char *track);   /* its routes region (L2RA ...): TRACKS/ROUTES<region> */
/* Makes sure the city's translated stream is in troot (from the PC tool, or
   translated here from ux0:data/NFSU2/expansion/<game>/TRACKS behind a progress
   screen). 1 = ready (or not a city), 0 = not installed / failed. */
int xpcity_prepare(const char *troot, const char *track);
/* xpcity_prepare opened the window already (the boot must not open another) */
int xpcity_window_made(void);
/* the city's minimap file (BMAP) once xpcity_prepare found or made it, else NULL */
const char *xpcity_map(const char *track);
/* the Free Run world picker: 0 Bayview, 1 Rockport, 2 Palmont */
extern int g_xpcity_pick;
int xpcity_installed(int pick);          /* translated or raw files there (Bayview: 1) */
const char *xpcity_track(int pick);      /* its stream name (STREAML2RA ...), Bayview NULL */
/* race barriers (MW BARRIER_SPLINE groups): the city's per-event lists
   (TRACKS/ROUTES<region>/barriers.txt) for the scenery selection (world_scenery.h hook,
   cleared outside a city), and switching an event's groups solid. */
void xpcity_barriers_select(const char *troot, const char *track);
struct WColl;
int xpcity_barriers_enable(struct WColl *w, int event);   /* groups switched on */
#endif
