/* mplay.h - a movie on screen: movie.c's decoder turned into a GL texture
 * (BT.601) with its sound on the mixer (audio_set_movie). Used by the front
 * end's IG_PlayMovie (shop.c) and the post-race movies (main.c). */
#ifndef MPLAY_H
#define MPLAY_H

#include "render.h"

/* the movies are stored 512x512 and shown full screen at 4:3 */
#define MPLAY_ASPECT (4.0f / 3.0f)

typedef struct MPlay MPlay;

/* Opens MOVIES/<file>; NULL when it is missing (the caller skips it). The
   sound starts with it. */
MPlay *mplay_open(const char *path);
/* Advances the picture by dt seconds; 1 once the pictures ran out and the
   sound played out. */
int mplay_update(MPlay *p, float dt);
GLuint mplay_tex(const MPlay *p, int *w, int *h);
void mplay_close(MPlay *p);

#endif
