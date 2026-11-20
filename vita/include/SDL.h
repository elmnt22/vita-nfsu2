/* SDL.h — OpenUG2 PS Vita platform shim.
 *
 * The engine only touches a small slice of SDL2 (window/GL context, keyboard
 * state, a handful of events, one audio callback, one worker thread, timers).
 * On the Vita that slice is implemented directly on vitaGL + SceCtrl +
 * SceAudio + SceKernel in vita/sdl_vita.c, so no SDL port is needed.
 *
 * Vita buttons are surfaced as the desktop keys the engine already reads
 * (see the mapping table in sdl_vita.c), plus analog steering through
 * vita_analog_steer().
 */
#ifndef OPENUG2_VITA_SDL_H
#define OPENUG2_VITA_SDL_H

#include <stdint.h>
#include <string.h>

typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_GAMECONTROLLER 0x00002000u

#define SDL_PRESSED  1
#define SDL_RELEASED 0

/* ---- keyboard ----------------------------------------------------------- */
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4, SDL_SCANCODE_B = 5, SDL_SCANCODE_C = 6, SDL_SCANCODE_D = 7, SDL_SCANCODE_E = 8, SDL_SCANCODE_F = 9, SDL_SCANCODE_G = 10,
    SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_L = 15, SDL_SCANCODE_N = 17,
    SDL_SCANCODE_Q = 20, SDL_SCANCODE_S = 22, SDL_SCANCODE_W = 26,
    SDL_SCANCODE_1 = 30,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_LEFTBRACKET = 47, SDL_SCANCODE_RIGHTBRACKET = 48,
    SDL_SCANCODE_F3 = 60, SDL_SCANCODE_F6 = 63, SDL_SCANCODE_PAUSE = 72, SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_KP_ENTER = 88, SDL_SCANCODE_KP_1 = 89, SDL_SCANCODE_KP_7 = 95, SDL_SCANCODE_KP_8 = 96, SDL_SCANCODE_KP_9 = 97, SDL_SCANCODE_KP_6 = 94, SDL_SCANCODE_KP_3 = 91, SDL_SCANCODE_KP_2 = 90,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;

typedef Sint32 SDL_Keycode;
#define SDLK_SCANCODE_MASK (1 << 30)
#define SDL_SCANCODE_TO_KEYCODE(x) ((SDL_Keycode)((x) | SDLK_SCANCODE_MASK))
#define SDLK_RETURN       '\r'
#define SDLK_ESCAPE       '\x1B'
#define SDLK_SPACE        ' '
#define SDLK_1            '1'
#define SDLK_LEFTBRACKET  '['
#define SDLK_RIGHTBRACKET ']'
#define SDLK_e            'e'
#define SDLK_f            'f'
#define SDLK_g            'g'
#define SDLK_k            'k'
#define SDLK_w            'w'
#define SDLK_c            'c'
#define SDLK_n            'n'
#define SDLK_BACKSPACE    '\b'
#define SDLK_DELETE       '\x7F'
#define SDLK_PAUSE        SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_PAUSE)
#define SDLK_KP_ENTER     SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_ENTER)
#define SDLK_F3           SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F3)
#define SDLK_F6           SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F6)
#define SDLK_RIGHT        SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RIGHT)
#define SDLK_LEFT         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LEFT)
#define SDLK_DOWN         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_DOWN)
#define SDLK_UP           SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_UP)
#define SDLK_KP_9         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_9)
#define SDLK_KP_6         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_6)
#define SDLK_KP_3         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_3)
#define SDLK_KP_2         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_2)
#define SDLK_KP_1         SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_1)

typedef struct { SDL_Scancode scancode; SDL_Keycode sym; Uint16 mod; } SDL_Keysym;

/* ---- events ------------------------------------------------------------- */
enum {
    SDL_QUIT = 0x100,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300, SDL_KEYUP,
    SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP,
    SDL_CONTROLLERBUTTONDOWN = 0x651, SDL_CONTROLLERBUTTONUP
};
enum { SDL_WINDOWEVENT_FOCUS_LOST = 13 };
#define SDL_BUTTON_LEFT  1
#define SDL_BUTTON_RIGHT 3

typedef enum {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT
} SDL_GameControllerButton;

typedef struct { Uint32 type; Uint32 timestamp; Uint8 state; Uint8 repeat; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint8 button; Uint8 state; Sint32 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; Uint32 timestamp; Sint32 x, y, xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint8 event; } SDL_WindowEvent;
typedef struct { Uint32 type; Uint32 timestamp; Uint8 button; Uint8 state; } SDL_ControllerButtonEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
    SDL_WindowEvent window;
    SDL_ControllerButtonEvent cbutton;
    Uint8 padding[56];
} SDL_Event;

/* ---- opaque handles ----------------------------------------------------- */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Thread SDL_Thread;
typedef void *SDL_GLContext;

#define SDL_WINDOWPOS_CENTERED 0x2FFF0000u
#define SDL_WINDOW_OPENGL      0x00000002u
#define SDL_WINDOW_HIDDEN      0x00000008u
#define SDL_WINDOW_RESIZABLE   0x00000020u

typedef enum {
    SDL_GL_DOUBLEBUFFER = 5, SDL_GL_DEPTH_SIZE = 6,
    SDL_GL_CONTEXT_MAJOR_VERSION = 17, SDL_GL_CONTEXT_PROFILE_MASK = 21
} SDL_GLattr;
#define SDL_GL_CONTEXT_PROFILE_ES 0x0004

/* ---- audio -------------------------------------------------------------- */
typedef Uint32 SDL_AudioDeviceID;
typedef Uint16 SDL_AudioFormat;
#define AUDIO_S16SYS 0x8010
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct {
    int freq; SDL_AudioFormat format; Uint8 channels; Uint8 silence;
    Uint16 samples; Uint32 size; SDL_AudioCallback callback; void *userdata;
} SDL_AudioSpec;

/* ---- atomics ------------------------------------------------------------ */
typedef struct { volatile int value; } SDL_atomic_t;
static inline int SDL_AtomicSet(SDL_atomic_t *a, int v) { return __atomic_exchange_n(&a->value, v, __ATOMIC_SEQ_CST); }
static inline int SDL_AtomicGet(SDL_atomic_t *a) { return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST); }
static inline SDL_bool SDL_AtomicCAS(SDL_atomic_t *a, int oldv, int newv) {
    return __atomic_compare_exchange_n(&a->value, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? SDL_TRUE : SDL_FALSE;
}

#define SDL_memset memset

#ifdef __cplusplus
extern "C" {
#endif

int  SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);

Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void   SDL_Delay(Uint32 ms);

int  SDL_GL_SetAttribute(SDL_GLattr attr, int value);
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *w);
SDL_GLContext SDL_GL_CreateContext(SDL_Window *w);
void SDL_GL_DeleteContext(SDL_GLContext c);
void SDL_GL_GetDrawableSize(SDL_Window *w, int *dw, int *dh);
void SDL_GetWindowSize(SDL_Window *w, int *ww, int *wh);
int  SDL_GL_SetSwapInterval(int interval);
void SDL_GL_SwapWindow(SDL_Window *w);
void *SDL_GL_GetProcAddress(const char *name);

int  SDL_PollEvent(SDL_Event *e);
int  SDL_PushEvent(SDL_Event *e);
const Uint8 *SDL_GetKeyboardState(int *numkeys);
int  SDL_SetRelativeMouseMode(SDL_bool on);

int  SDL_NumJoysticks(void);
SDL_bool SDL_IsGameController(int index);
SDL_GameController *SDL_GameControllerOpen(int index);
void SDL_GameControllerClose(SDL_GameController *c);

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);

typedef int (*SDL_ThreadFunction)(void *data);
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_WaitThread(SDL_Thread *t, int *status);
typedef unsigned long SDL_threadID;
SDL_threadID SDL_ThreadID(void);   /* the calling thread (sceKernelGetThreadId) */

/* Vita extra: left-stick steering in [-1,1] (+1 = left, like A); 0 when centred. */
float vita_analog_steer(void);
/* Vita extra: log redirection; call first thing in main(). */
void vita_early_init(void);
/* Vita extra: append a timestamped startup milestone to boot.txt. */
void vita_mark(const char *fmt, ...);
/* Vita extra: frame-section lap timer (0 = start; n adds elapsed to slot n). */
void vita_lap(int slot);
/* Vita extra: test mode, cycled with Select+Triangle. 0 normal, 1 GPU
   probe. Each mode's ms/frame is logged. */
extern int vita_gfx_mode;
/* Vita extra: right analog stick, -1..1, dead zone applied. */
extern float vita_rstick_x, vita_rstick_y;
extern float vita_lstick_y;   /* left stick Y, -1..1 with a dead zone (showroom camera) */
/* Vita extra: GPU probe -- the 3D pass to leave out this frame (0 = none,
   1 sky, 2 world opaque, 3 world cutout, 4 world blend+lights, 5 all cars,
   6 AI cars, 7 shadow+skid+smoke). */
extern int vita_probe_skip;
/* Vita extra: record a GLSL compile failure (boot.txt + shader_fail_N.txt). */
void vita_shader_failed(unsigned type, const char *src, const char *log);
/* Vita extra: run fn(0..n-1, ctx) across the calling thread and two workers
   pinned to cores 1 and 2; returns when every index is done. */
void vita_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx);
/* GPU-buffer copies on a worker (cores 1-2): push from the frame thread only;
   nothing may draw or free dst/src until vita_copy_idle() */
void vita_copy_push(void *dst, const void *src, unsigned n);
int  vita_copy_idle(void);
unsigned vita_copy_mark(void);
int  vita_copy_passed(unsigned mark);
void vita_copy_wait(void);
void vita_copy_stats(unsigned long long *us, unsigned long long *bytes);

#ifdef __cplusplus
}
#endif

#endif
