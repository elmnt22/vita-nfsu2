/* xpcity.h -- expansion cities (Rockport, Palmont): one-time translation on the console. */
#ifndef XPCITY_H
#define XPCITY_H
/* track is an expansion city (STREAML2RA / STREAML5RA) */
int xpcity_is_city(const char *track);
/* Makes sure the city's translated stream is in troot (from the PC tool, or
   translated here from ux0:data/NFSU2/expansion/<game>/TRACKS behind a progress
   screen). 1 = ready (or not a city), 0 = not installed / failed. */
int xpcity_prepare(const char *troot, const char *track);
/* xpcity_prepare opened the window already (the boot must not open another) */
int xpcity_window_made(void);
#endif
