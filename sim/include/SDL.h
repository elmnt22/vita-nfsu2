/* SDL.h -- OpenUG2 Windows simulator of the Vita build.
 *
 * The game code is compiled exactly as for the Vita (__vita__, N2_GLES), but
 * on real SDL2 for Windows. The SDL calls whose Vita behaviour the game
 * depends on (the 720x408 framebuffer, the pad surfaced as keys, the swap
 * that drives the test script) are routed to sim/sim_vita.c; the rest is the
 * real library. The Vita extras declared by vita/include/SDL.h follow.
 */
#ifndef OPENUG2_SIM_SDL_H
#define OPENUG2_SIM_SDL_H

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED 1
#endif
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

int  sim_Init(Uint32 flags);
SDL_Window *sim_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
int  sim_GL_SetAttribute(SDL_GLattr attr, int value);
SDL_GLContext sim_GL_CreateContext(SDL_Window *w);
void sim_GL_GetDrawableSize(SDL_Window *w, int *dw, int *dh);
void sim_GetWindowSize(SDL_Window *w, int *ww, int *wh);
void sim_GL_SwapWindow(SDL_Window *w);
int  sim_GL_SetSwapInterval(int interval);
int  sim_PollEvent(SDL_Event *e);
const Uint8 *sim_GetKeyboardState(int *numkeys);
int  sim_SetRelativeMouseMode(SDL_bool on);
int  sim_NumJoysticks(void);

#ifndef SIM_NO_SDL_REDIRECT
#define SDL_Init                sim_Init
#define SDL_CreateWindow        sim_CreateWindow
#define SDL_GL_SetAttribute     sim_GL_SetAttribute
#define SDL_GL_CreateContext    sim_GL_CreateContext
#define SDL_GL_GetDrawableSize  sim_GL_GetDrawableSize
#define SDL_GetWindowSize       sim_GetWindowSize
#define SDL_GL_SwapWindow       sim_GL_SwapWindow
#define SDL_GL_SetSwapInterval  sim_GL_SetSwapInterval
#define SDL_PollEvent           sim_PollEvent
#define SDL_GetKeyboardState    sim_GetKeyboardState
#define SDL_SetRelativeMouseMode sim_SetRelativeMouseMode
#define SDL_NumJoysticks        sim_NumJoysticks
#endif

/* ---- the Vita extras (vita/include/SDL.h) ---- */
float vita_analog_steer(void);
void vita_early_init(void);
void vita_mark(const char *fmt, ...);
void vita_lap(int slot);
extern int vita_gfx_mode;
extern float vita_rstick_x, vita_rstick_y;
extern float vita_lstick_y;
extern int vita_probe_skip;
void vita_shader_failed(unsigned type, const char *src, const char *log);
void vita_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
#endif
