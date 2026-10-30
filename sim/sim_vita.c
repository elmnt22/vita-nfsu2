/* sim_vita.c -- OpenUG2 Windows simulator of the Vita build: the platform
 * layer vita/sdl_vita.c provides on the console, on real SDL2 + an OpenGL ES
 * 2.0 context. The game sources are compiled unchanged with __vita__ and
 * N2_GLES (see sim/build_sim.sh), so the simulator runs the same front end,
 * shaders, uniform/variant shims (vita/vu_shim.inc) and 720x408 framebuffer.
 *
 * Folders: ux0:/app0: map to <root>/ux0 and <root>/app0, root = env
 * OPENUG2_SIM_ROOT (default: the simroot folder next to the exe's parent).
 *
 * Input: the PC keyboard stands in for the Vita pad, and goes through the
 * same button -> key translation as the console (controller bindings too):
 *   arrows = D-pad, Z = Cross, X = Circle, A = Square, S = Triangle,
 *   Q = L, W = R, Enter = Start, Right Shift = Select, F12 = screenshot.
 *
 * Test script: env OPENUG2_SIM_SCRIPT = a text file, one command a line:
 *   frames N          let N frames run
 *   wait SECONDS      let real time pass (loading runs on threads)
 *   press BTN [N]     hold a button for N frames (default 3), then release
 *   hold BTN / release BTN
 *   stick X Y         left stick, -1..1 (X: +1 right)
 *   rtouch U V        hold a finger on the rear touchpad at U, V in 0..1 (V down); rtouch off lifts it
 *   shot NAME         save <root>/shots/NAME.png at the next frame
 *   quit
 * Buttons: CROSS CIRCLE SQUARE TRIANGLE L R UP DOWN LEFT RIGHT START SELECT.
 */
#define SIM_NO_SDL_REDIRECT 1
#define SIM_NO_VU_SHIM 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <dirent.h>
#include <direct.h>
#include <process.h>
#include <windows.h>
#include "SDL.h"
#include "SDL_opengles2.h"
#include "psp2/io/fcntl.h"
#include "../src/options.h"

void write_png(const char *path, int w, int h, const unsigned char *rgb);   /* render.c */

#define SIM_W 720
#define SIM_H 408

/* ---- folders ------------------------------------------------------------- */
static char g_root[MAX_PATH];
static void root_init(void) {
    if (g_root[0]) return;
    const char *e = getenv("OPENUG2_SIM_ROOT");
    if (e && *e) snprintf(g_root, sizeof g_root, "%s", e);
    else {
        char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, sizeof exe);
        char *s = strrchr(exe, '\\'); if (s) *s = 0;          /* .../OpenUG2/sim/build */
        snprintf(g_root, sizeof g_root, "%s\\..\\..\\..\\simroot", exe);
    }
}
const char *sim_path(const char *p, char *buf, size_t cap) {
    root_init();
    const char *dev = NULL, *rest = NULL;
    if (!strncmp(p, "ux0:", 4)) { dev = "ux0"; rest = p + 4; }
    else if (!strncmp(p, "app0:", 5)) { dev = "app0"; rest = p + 5; }
    if (!dev) return p;
    while (*rest == '/') rest++;
    snprintf(buf, cap, "%s/%s/%s", g_root, dev, rest);
    return buf;
}
FILE *sim_fopen(const char *p, const char *m) { char b[1024]; return fopen(sim_path(p, b, sizeof b), m); }
int sim_open(const char *p, int flags, ...) {
    char b[1024]; int mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return _open(sim_path(p, b, sizeof b), flags | O_BINARY, mode ? mode : (_S_IREAD | _S_IWRITE));
}
DIR *sim_opendir(const char *p) { char b[1024]; return opendir(sim_path(p, b, sizeof b)); }
int sim_mkdir(const char *p, int mode) { (void)mode; char b[1024]; return _mkdir(sim_path(p, b, sizeof b)); }
int sim_remove(const char *p) { char b[1024]; return remove(sim_path(p, b, sizeof b)); }
int sim_rmdir(const char *p) { char b[1024]; return _rmdir(sim_path(p, b, sizeof b)); }
int sim_rename(const char *a, const char *bb) {
    char b1[1024], b2[1024];
    const char *x = sim_path(a, b1, sizeof b1), *y = sim_path(bb, b2, sizeof b2);
    remove(y);                                    /* POSIX rename replaces */
    return rename(x, y);
}
int sim_stat(const char *p, struct stat *st) { char b[1024]; return stat(sim_path(p, b, sizeof b), st); }

/* ---- psp2 stand-ins -------------------------------------------------------- */
SceUInt64 sceKernelGetProcessTimeWide(void) {
    static Uint64 f0, t0;
    if (!f0) { f0 = SDL_GetPerformanceFrequency(); t0 = SDL_GetPerformanceCounter(); }
    return (SceUInt64)((SDL_GetPerformanceCounter() - t0) * 1000000ull / f0);
}
int sceClibPrintf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vprintf(fmt, ap); va_end(ap); return n; }
SceUID sceIoOpen(const char *path, int flags, SceMode mode) {
    int f = (flags & 3) == 1 ? O_RDONLY : (flags & 3) == 2 ? O_WRONLY : O_RDWR;
    if (flags & 0x0200) f |= O_CREAT;
    if (flags & 0x0400) f |= O_TRUNC;
    int fd = sim_open(path, f, mode ? mode : (_S_IREAD | _S_IWRITE));
    return fd < 0 ? (SceUID)0x80010002 : fd;
}
int sceIoClose(SceUID fd) { return _close(fd); }
int sceIoRead(SceUID fd, void *buf, SceSize n) { return _read(fd, buf, n); }
static SDL_mutex *g_pread_lock;
int sceIoPread(SceUID fd, void *buf, SceSize n, SceOff off) {
    if (!g_pread_lock) g_pread_lock = SDL_CreateMutex();
    SDL_LockMutex(g_pread_lock);
    int r = _lseeki64(fd, off, SEEK_SET) < 0 ? -1 : _read(fd, buf, n);
    SDL_UnlockMutex(g_pread_lock);
    return r;
}
int sceIoMkdir(const char *path, SceMode mode) { return sim_mkdir(path, mode); }

/* ---- vitaGL stand-ins ------------------------------------------------------- */
void *vglGetTexDataPointer(GLenum target) { (void)target; return (void *)1; }
GLboolean vglCompressedTexImageChainUG2(GLenum fmt, GLsizei w, GLsizei h, GLsizei levels, const void *data) {
    const unsigned char *p = (const unsigned char *)data;
    int bs = (fmt == 0x83F0 || fmt == 0x83F1) ? 8 : 16;      /* DXT1 : DXT3/5 */
    for (int l = 0; l < levels; l++) {
        int sz = ((w + 3) / 4) * ((h + 3) / 4) * bs;
        glCompressedTexImage2D(GL_TEXTURE_2D, l, fmt, w, h, 0, sz, p);
        p += sz;
        w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
    }
    /* The game's chains stop at 4x4; vitaGL samples them as they are, desktop GL
       calls the texture incomplete (black) unless the level range says so. */
    glTexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, levels - 1);
    while (glGetError() != GL_NO_ERROR) {}
    return GL_TRUE;
}

/* No program binaries in the simulator: every program compiles from source
   (a fresh program's link status stays false, so render.c compiles it). */
void glProgramBinary(GLuint program, GLenum format, const void *binary, GLsizei length) {
    (void)program; (void)format; (void)binary; (void)length;
}
void glGetProgramBinary(GLuint program, GLsizei cap, GLsizei *length, GLenum *format, void *binary) {
    (void)program; (void)cap; (void)binary;
    if (length) *length = 0;
    if (format) *format = 0;
}

/* ---- scandir (not in MinGW) ------------------------------------------------- */
int alphasort(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }
int sim_scandir(const char *p, struct dirent ***out, int (*filt)(const struct dirent *),
                int (*cmp)(const struct dirent **, const struct dirent **)) {
    DIR *d = sim_opendir(p);
    if (!d) return -1;
    struct dirent **v = NULL, *e; int n = 0, cap = 0;
    while ((e = readdir(d))) {
        if (filt && !filt(e)) continue;
        if (n == cap) { cap = cap ? cap * 2 : 32; v = realloc(v, (size_t)cap * sizeof *v); }
        v[n] = malloc(sizeof *e); memcpy(v[n], e, sizeof *e); n++;
    }
    closedir(d);
    if (cmp && n > 1) qsort(v, (size_t)n, sizeof *v, (int (*)(const void *, const void *))cmp);
    *out = v;
    return n;
}

/* ---- the GL shims, shared with the console build ---------------------------- */
int vita_gfx_mode;
#include "../vita/vu_shim.inc"

/* ---- crash report: the faulting function/line and the code addresses on
   the stack, named through DbgHelp from OpenUG2sim.pdb ------------------------ */
#include <dbghelp.h>
static void sim_sym(DWORD a, const char *tag) {
    char buf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    memset(buf, 0, sizeof buf); si->SizeOfStruct = sizeof *si; si->MaxNameLen = 255;
    DWORD64 off = 0; DWORD loff = 0; IMAGEHLP_LINE64 ln; memset(&ln, 0, sizeof ln); ln.SizeOfStruct = sizeof ln;
    if (!SymFromAddr(GetCurrentProcess(), a, &off, si)) return;
    if (SymGetLineFromAddr64(GetCurrentProcess(), a, &loff, &ln))
        fprintf(stdout, "sim: %s %s+0x%llx  %s:%lu\n", tag, si->Name, off, ln.FileName, ln.LineNumber);
    else fprintf(stdout, "sim: %s %s+0x%llx\n", tag, si->Name, off);
}
static LONG WINAPI sim_crash(EXCEPTION_POINTERS *x) {
    fprintf(stdout, "\nsim: CRASH code %08lx at %p\n", x->ExceptionRecord->ExceptionCode,
            x->ExceptionRecord->ExceptionAddress);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    sim_sym((DWORD)x->ExceptionRecord->ExceptionAddress, "at");
    {   HMODULE fm = NULL; char mn[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)x->ExceptionRecord->ExceptionAddress, &fm))
            GetModuleFileNameA(fm, mn, sizeof mn);
        fprintf(stdout, "sim: in module %s (base %p); access %s %p\n", mn, (void *)fm,
                x->ExceptionRecord->ExceptionInformation[0] == 8 ? "execute" :
                x->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                (void *)x->ExceptionRecord->ExceptionInformation[1]); }
    CONTEXT *c = x->ContextRecord;
    DWORD *sp = (DWORD *)c->Esp;
    fprintf(stdout, "sim: esp %08lx ebp %08lx eax %08lx ecx %08lx edx %08lx; [esp] %08lx %08lx %08lx %08lx\n", c->Esp, c->Ebp, c->Eax, c->Ecx, c->Edx, sp[0], sp[1], sp[2], sp[3]);
    sim_sym(sp[0], "ret");
    {   /* which exe section the fault and the return address are in */
        unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
        IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
        DWORD q[2] = { (DWORD)x->ExceptionRecord->ExceptionAddress, sp[0] };
        for (int k = 0; k < 2; k++) {
            DWORD rva = q[k] - (DWORD)base;
            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
                if (rva >= sh[i].VirtualAddress && rva < sh[i].VirtualAddress + sh[i].Misc.VirtualSize)
                    fprintf(stdout, "sim: %08lx = exe rva %08lx in %.8s\n", q[k], rva, (const char *)sh[i].Name);
        }
        fprintf(stdout, "sim: exe base %p\n", (void *)base);
        for (int k = 0; k < 2; k++) {
            MEMORY_BASIC_INFORMATION mi;
            if (VirtualQuery((void *)q[k], &mi, sizeof mi))
                fprintf(stdout, "sim: %08lx: alloc base %p region %p+%lx protect %lx type %lx\n", q[k],
                        mi.AllocationBase, mi.BaseAddress, (unsigned long)mi.RegionSize, mi.Protect, mi.Type);
        }
        DWORD *fp = (DWORD *)c->Ebp;   /* the frame-pointer chain */
        for (int i = 0; i < 32 && fp; i++) {
            MEMORY_BASIC_INFORMATION mi;
            if (!VirtualQuery(fp, &mi, sizeof mi) || mi.State != MEM_COMMIT) break;
            fprintf(stdout, "sim: frame %d ret %08lx\n", i, fp[1]);
            sim_sym(fp[1], "  frame");
            if ((DWORD *)fp[0] <= fp) break;
            fp = (DWORD *)fp[0];
        }
    }
    HMODULE m = GetModuleHandleA(NULL);
    int shown = 0;
    for (int i = 0; i < 8192 && shown < 24; i++) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(sp + i, &mi, sizeof mi) || mi.State != MEM_COMMIT) break;
        DWORD v = sp[i];
        if (v > (DWORD)m && v < (DWORD)m + 0x400000) { sim_sym(v, "  from"); shown++; }
    }
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* First chance, before any frame handler: a stack overflow or a fault the
   C runtime swallows never reaches sim_crash. Raw write, little stack. */
static LONG CALLBACK sim_first_chance(EXCEPTION_POINTERS *e) {
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c == 0xC00000FD || c == 0xC0000005 || c == 0xC0000094 || c == 0xC000001D) {
        char m[96]; int n = wsprintfA(m, "sim: first-chance exception %08lx at %p (thread %lu)\r\n",
                                      c, e->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
        DWORD w; WriteFile(GetStdHandle(STD_ERROR_HANDLE), m, (DWORD)n, &w, NULL);
        fwrite(m, 1, (size_t)n, stdout); fflush(stdout);
        if (c != 0xC00000FD) sim_sym((DWORD)e->ExceptionRecord->ExceptionAddress, "  in");
        fflush(stdout);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- log / boot milestones ----------------------------------------------- */
static Uint64 g_t0;
static FILE *g_boot;
void vita_early_init(void) {
    char b[1024];
    SetUnhandledExceptionFilter(sim_crash);
    AddVectoredExceptionHandler(1, sim_first_chance);
    root_init();
    sim_mkdir("ux0:data", 0); sim_mkdir("ux0:data/OpenUG2", 0); sim_mkdir("ux0:shots", 0);
    if (!getenv("OPENUG2_SIM_CONSOLE")) {
        freopen(sim_path("ux0:data/OpenUG2/log.txt", b, sizeof b), "w", stdout);
        setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    }
    g_boot = fopen(sim_path("ux0:data/OpenUG2/boot.txt", b, sizeof b), "w");
    g_t0 = sceKernelGetProcessTimeWide();
}
void vita_mark(const char *fmt, ...) {
    char line[512]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    double t = (sceKernelGetProcessTimeWide() - g_t0) / 1e6;
    printf("[%7.2fs] %s\n", t, line);
    if (g_boot) { fprintf(g_boot, "[%7.2fs] %s\n", t, line); fflush(g_boot); }
}
void vita_crumb(const char *fmt, ...) { (void)fmt; }
volatile int vita_wd_armed, vita_wd_sec;   /* the race-hang watchdog (Vita only) */
volatile unsigned vita_wd_beat;
void vita_mark_stats(void) {}
void vita_log_flush(void) { fflush(stdout); }
void vita_lap(int slot) { (void)slot; }
/* the heap model (sim_heap.c) stands in for the console's numbers, so the
   read-ahead's room gate and the reports behave as on the Vita */
int sim_heap_on(void); unsigned sim_heap_brk_mb(void), sim_heap_room_mb(void);
size_t sim_heap_big_live(void); void sim_heap_big_report(char *out, size_t cap);
void sim_heap_report(const char *label, int detail);
void vita_heap_report(const char *label) {
    static Uint32 last_detail;
    Uint32 now = SDL_GetTicks();
    int detail = 1;
    if (!last_detail || now - last_detail >= 60000) { detail = 2; last_detail = now ? now : 1; }
    sim_heap_report(label, detail);
}
unsigned vita_heap_top_mb(void) { return sim_heap_on() > 0 ? sim_heap_brk_mb() : 0; }
unsigned vita_bigblk_room_mb(void) { return sim_heap_on() > 0 ? sim_heap_room_mb() : 512; }
size_t vita_bigblk_live(void) { return sim_heap_on() > 0 ? sim_heap_big_live() : 0; }
void vita_bigblk_report(char *out, size_t cap) {
    if (sim_heap_on() > 0) sim_heap_big_report(out, cap); else if (cap) out[0] = 0;
}
void vita_async_file(FILE *f) { (void)f; }
void vita_gx_start(void) {}
const char *vita_gx_header(void) { return ""; }
void vita_gx_row(FILE *f) { (void)f; }
void vita_fast_clocks(void) {}
int vita_probe_skip;
float vita_audio_pct;
int vita_lock30 = 1;
void vita_shader_failed(unsigned type, const char *src, const char *log) {
    printf("SHADER FAILED (type %u): %s\n--- source ---\n%s\n", type, log ? log : "", src ? src : "");
    fflush(stdout);
}
const char *vita_user_name(void) {
    const char *u = getenv("USERNAME");
    return u ? u : "Player";
}

/* ---- threads ---------------------------------------------------------------- */
void vita_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx) { for (int i = 0; i < n; i++) fn(i, ctx); }
void vita_parallel_async(int n, void (*fn)(int i, void *ctx), void *ctx) { for (int i = 0; i < n; i++) fn(i, ctx); }
void vita_parallel_join(void) {}

/* ---- progress screen (the tile conversion before the renderer exists) ----- */
static SDL_Window *g_win;
static void ps_rect(int x, int y, int w, int h, float r, float g, float b) {
    glScissor(x, SIM_H - y - h, w, h); glClearColor(r, g, b, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
}
void vita_progress_screen_done(void) {}
void vita_progress_screen(const char *title, const char *line1, const char *line2, float frac) {
    static Uint32 last;
    Uint32 now = SDL_GetTicks();
    if (now - last < 100 && frac < 1.0f) return;
    last = now;
    printf("progress: %s | %s | %s | %.0f%%\n", title ? title : "", line1 ? line1 : "", line2 ? line2 : "", frac * 100);
    glDisable(GL_DEPTH_TEST); glEnable(GL_SCISSOR_TEST);
    ps_rect(0, 0, SIM_W, SIM_H, 0.02f, 0.03f, 0.06f);
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    int bw = SIM_W * 2 / 3, bx = (SIM_W - bw) / 2, by = SIM_H * 290 / 544, bh = SIM_H * 14 / 544;
    ps_rect(bx - 2, by - 2, bw + 4, bh + 4, 0.55f, 0.85f, 0.95f);
    ps_rect(bx, by, bw, bh, 0.02f, 0.05f, 0.08f);
    ps_rect(bx, by, (int)(bw * frac), bh, 0.25f, 0.85f, 1.0f);
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(g_win);
    SDL_PumpEvents();
}

/* ---- input: the pad, from the keyboard or the script ------------------------- */
enum { P_CROSS = 1 << 0, P_CIRCLE = 1 << 1, P_SQUARE = 1 << 2, P_TRIANGLE = 1 << 3, P_L = 1 << 4,
       P_R = 1 << 5, P_UP = 1 << 6, P_DOWN = 1 << 7, P_LEFT = 1 << 8, P_RIGHT = 1 << 9,
       P_START = 1 << 10, P_SELECT = 1 << 11,
       P_LS_LEFT = 1 << 24, P_LS_RIGHT = 1 << 25, P_RT_UL = 1 << 26, P_RT_UR = 1 << 27,
       P_RT_LL = 1 << 28, P_RT_LR = 1 << 29 };
static const struct { const char *name; uint32_t bit; SDL_Scancode key; } g_btn[] = {
    { "CROSS", P_CROSS, SDL_SCANCODE_Z }, { "CIRCLE", P_CIRCLE, SDL_SCANCODE_X },
    { "SQUARE", P_SQUARE, SDL_SCANCODE_A }, { "TRIANGLE", P_TRIANGLE, SDL_SCANCODE_S },
    { "L", P_L, SDL_SCANCODE_Q }, { "R", P_R, SDL_SCANCODE_W },
    { "UP", P_UP, SDL_SCANCODE_UP }, { "DOWN", P_DOWN, SDL_SCANCODE_DOWN },
    { "LEFT", P_LEFT, SDL_SCANCODE_LEFT }, { "RIGHT", P_RIGHT, SDL_SCANCODE_RIGHT },
    { "START", P_START, SDL_SCANCODE_RETURN }, { "SELECT", P_SELECT, SDL_SCANCODE_RSHIFT } };
#define NBTN ((int)(sizeof g_btn / sizeof g_btn[0]))
static uint32_t g_kbd_buttons, g_script_buttons;
static float g_script_lx, g_script_ly;
static float g_script_tu = -1.0f, g_script_tv = -1.0f;   /* script: the rear touch finger, -1 none */

#define EVQ 128
static SDL_Event evq[EVQ];
static int evq_head, evq_tail;
static Uint8 g_keys[SDL_NUM_SCANCODES];
static uint32_t g_prev_buttons, g_new_buttons;
static float g_steer;
float vita_rstick_x, vita_rstick_y, vita_lstick_y;
static float g_script_rx, g_script_ry; static int g_script_hyd;   /* script: rstick, hyd (pressure 1 / bounce 2) */
int vita_ctrl_scheme = 0;

static uint32_t vb_mask(int vb) {
    switch (vb) {
    case VB_CROSS: return P_CROSS;     case VB_CIRCLE: return P_CIRCLE;
    case VB_SQUARE: return P_SQUARE;   case VB_TRIANGLE: return P_TRIANGLE;
    case VB_L: return P_L;             case VB_R: return P_R;
    case VB_UP: return P_UP;           case VB_DOWN: return P_DOWN;
    case VB_LEFT: return P_LEFT;       case VB_RIGHT: return P_RIGHT;
    case VB_START: return P_START;     case VB_SELECT: return P_SELECT;
    case VB_LS_LEFT: return P_LS_LEFT; case VB_LS_RIGHT: return P_LS_RIGHT;
    case VB_RT_UL: return P_RT_UL;     case VB_RT_UR: return P_RT_UR;
    case VB_RT_LL: return P_RT_LL;     case VB_RT_LR: return P_RT_LR;
    default: return 0;
    }
}
static int act_down(uint32_t b, int act) {
    return (b & vb_mask(g_opt.ctl[act][0])) || (b & vb_mask(g_opt.ctl[act][1]));
}
int vita_take_new_button(void) {
    uint32_t n = g_new_buttons;
    g_new_buttons = 0;
    for (int vb = 1; vb < VB_COUNT; vb++) if (n & vb_mask(vb)) return vb;
    return 0;
}
static int push_event(SDL_Event *e) {
    int next = (evq_tail + 1) % EVQ;
    if (next == evq_head) return 0;
    evq[evq_tail] = *e; evq_tail = next;
    return 1;
}
static SDL_Keycode key_sym(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_RETURN: return SDLK_RETURN;
    case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE;
    case SDL_SCANCODE_SPACE:  return SDLK_SPACE;
    case SDL_SCANCODE_1:      return SDLK_1;
    case SDL_SCANCODE_LEFTBRACKET:  return SDLK_LEFTBRACKET;
    case SDL_SCANCODE_RIGHTBRACKET: return SDLK_RIGHTBRACKET;
    case SDL_SCANCODE_A: return 'a';
    case SDL_SCANCODE_D: return 'd';
    case SDL_SCANCODE_F: return SDLK_f;
    case SDL_SCANCODE_S: return 's';
    case SDL_SCANCODE_W: return 'w';
    case SDL_SCANCODE_E: return 'e';
    case SDL_SCANCODE_Q: return 'q';
    case SDL_SCANCODE_C: return 'c';
    case SDL_SCANCODE_N: return 'n';
    case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
    case SDL_SCANCODE_DELETE: return SDLK_DELETE;
    default: return SDL_SCANCODE_TO_KEYCODE(sc);
    }
}
static void set_key(SDL_Scancode sc, int down) {
    if (g_keys[sc] == (down ? 1 : 0)) return;
    g_keys[sc] = down ? 1 : 0;
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = key_sym(sc);
    push_event(&e);
}
static void pad_button(uint32_t now, uint32_t bit, SDL_GameControllerButton b) {
    if ((now & bit) && !(g_prev_buttons & bit)) {
        SDL_Event e; memset(&e, 0, sizeof e);
        e.type = SDL_CONTROLLERBUTTONDOWN; e.cbutton.button = (Uint8)b; e.cbutton.state = SDL_PRESSED;
        push_event(&e);
    }
}
static int g_shot_req;
/* vita/sdl_vita.c poll_pad(), on the simulated pad */
static void poll_pad(void) {
    uint32_t b = g_kbd_buttons | g_script_buttons;
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    vita_rstick_x = g_script_rx; vita_rstick_y = g_script_ry;
    vita_lstick_y = g_script_ly;
    float lxf = g_script_lx;
    if (k[SDL_SCANCODE_J]) lxf = -1.0f;
    if (k[SDL_SCANCODE_L]) lxf = 1.0f;
    if (k[SDL_SCANCODE_U]) vita_rstick_x = -1.0f;   /* showroom turntable */
    if (k[SDL_SCANCODE_O]) vita_rstick_x = 1.0f;
    int lx = (int)(lxf * 127.0f);
    g_steer = lx > 24 ? -(float)(lx - 24) / 103.0f : lx < -24 ? -(float)(lx + 24) / 103.0f : 0.0f;
    if (g_steer > 1.0f) g_steer = 1.0f;
    if (g_steer < -1.0f) g_steer = -1.0f;
    if (lx < -64) b |= P_LS_LEFT;
    if (lx > 64) b |= P_LS_RIGHT;
    {   /* the rear touchpad, held two samples like sdl_vita.c */
        static uint32_t last;
        uint32_t now = g_script_tu >= 0.0f ? vb_mask(options_touch_zone(&g_opt, g_script_tu, g_script_tv)) : 0;
        b |= now & last;
        last = now;
    }

    int quit = (b & P_SELECT) && (b & P_START);
    g_new_buttons |= b & ~g_prev_buttons;
    set_key(SDL_SCANCODE_W, act_down(b, OPT_ACT_THROTTLE));
    set_key(SDL_SCANCODE_S, act_down(b, OPT_ACT_BACK));
    set_key(SDL_SCANCODE_E, act_down(b, OPT_ACT_SHIFT_UP));
    set_key(SDL_SCANCODE_Q, act_down(b, OPT_ACT_SHIFT_DOWN));
    set_key(SDL_SCANCODE_A, act_down(b, OPT_ACT_STEER_L) || lx < -64);
    set_key(SDL_SCANCODE_D, act_down(b, OPT_ACT_STEER_R) || lx > 64);
    set_key(SDL_SCANCODE_SPACE, act_down(b, OPT_ACT_HANDBRAKE));
    set_key(SDL_SCANCODE_N, act_down(b, OPT_ACT_NITROUS) && !(b & P_SELECT));
    set_key(SDL_SCANCODE_C, act_down(b, OPT_ACT_CAMERA));
    set_key(SDL_SCANCODE_KP_9, act_down(b, OPT_ACT_ENGAGE));   /* free roam's challenge */
    set_key(SDL_SCANCODE_KP_6, act_down(b, OPT_ACT_WORLD_MAP));   /* joy event 0xc0: the career world map */
    set_key(SDL_SCANCODE_KP_3, act_down(b, OPT_ACT_READ_SMS));   /* joy event 0xc2: the SMS mailbox */
    set_key(SDL_SCANCODE_KP_2, act_down(b, OPT_ACT_CAREER_STATS));   /* RC message 0x13 (0x6068c5): UI_Status_Master */
    set_key(SDL_SCANCODE_B, act_down(b, OPT_ACT_LOOK_BACK));
    set_key(SDL_SCANCODE_KP_7, act_down(b, OPT_ACT_HYD_PRESSURE) || (g_script_hyd & 1));   /* hydraulics (hyd_player) */
    set_key(SDL_SCANCODE_KP_8, act_down(b, OPT_ACT_HYD_BOUNCE) || (g_script_hyd & 2));
    set_key(SDL_SCANCODE_PAUSE, act_down(b, OPT_ACT_PAUSE) && !quit);
    set_key(SDL_SCANCODE_KP_ENTER, (b & P_CROSS) != 0);
    set_key(SDL_SCANCODE_BACKSPACE, (b & P_CIRCLE) != 0);
    set_key(SDL_SCANCODE_DELETE, (b & P_SQUARE) != 0);   /* menus: Square */
    set_key(SDL_SCANCODE_LEFT, (b & P_LEFT) != 0);
    set_key(SDL_SCANCODE_RIGHT, (b & P_RIGHT) != 0);
    set_key(SDL_SCANCODE_UP, (b & P_UP) != 0);
    set_key(SDL_SCANCODE_DOWN, (b & P_DOWN) != 0);
    set_key(SDL_SCANCODE_LEFTBRACKET, (b & P_L) != 0);
    set_key(SDL_SCANCODE_RIGHTBRACKET, (b & P_R) != 0);
    set_key(SDL_SCANCODE_F, (b & P_TRIANGLE) && !(b & P_SELECT));
    set_key(SDL_SCANCODE_RETURN, (b & P_START) && !quit);
    set_key(SDL_SCANCODE_1, (b & P_SELECT) && !quit);
    {   /* Reset: on a clean release of its button (vita/sdl_vita.c) */
        static int rheld, rclean;
        int rd = act_down(b, OPT_ACT_RESET);
        if (rd && !rheld) rclean = 1;
        else if (rd && (b & ~g_prev_buttons)) rclean = 0;
        set_key(SDL_SCANCODE_KP_1, !rd && rheld && rclean);
        rheld = rd;
    }
    set_key(SDL_SCANCODE_ESCAPE, quit);
    pad_button(b, P_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
    pad_button(b, P_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad_button(b, P_CROSS, SDL_CONTROLLER_BUTTON_A);
    pad_button(b, P_CIRCLE, SDL_CONTROLLER_BUTTON_B);
    pad_button(b, P_START, SDL_CONTROLLER_BUTTON_START);
    g_prev_buttons = b;
}
static int g_quit;
static void pump_real(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = 1;
        else if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
            int down = e.type == SDL_KEYDOWN;
            if (e.key.keysym.scancode == SDL_SCANCODE_F12 && down) g_shot_req = 1;
            for (int i = 0; i < NBTN; i++)
                if (e.key.keysym.scancode == g_btn[i].key) {
                    if (down) g_kbd_buttons |= g_btn[i].bit; else g_kbd_buttons &= ~g_btn[i].bit;
                }
        }
    }
}
int sim_PollEvent(SDL_Event *e) {
    if (evq_head == evq_tail) {
        pump_real();
        if (g_quit) { memset(e, 0, sizeof *e); e->type = SDL_QUIT; g_quit = 0; return 1; }
        poll_pad();
    }
    if (evq_head == evq_tail) return 0;
    *e = evq[evq_head]; evq_head = (evq_head + 1) % EVQ;
    return 1;
}
const Uint8 *sim_GetKeyboardState(int *numkeys) { if (numkeys) *numkeys = SDL_NUM_SCANCODES; return g_keys; }
int sim_SetRelativeMouseMode(SDL_bool on) { (void)on; return 0; }
int sim_NumJoysticks(void) { return 0; }
int sim_PushEvent(SDL_Event *e) { return push_event(e); }   /* the game's own pushes (test aids) join the simulated queue */
float vita_analog_steer(void) { return g_steer; }

/* ---- the test script ---------------------------------------------------------- */
static char **g_lines; static int g_nlines, g_line;
static int g_wait_frames; static Uint32 g_wait_until;
static char g_shot_name[128];
static struct { uint32_t bit; int frames; } g_tap;
static unsigned g_frame;
static void script_load(void) {
    const char *p = getenv("OPENUG2_SIM_SCRIPT");
    if (!p || !*p) return;
    FILE *f = fopen(p, "r");
    if (!f) { printf("sim: script %s not found\n", p); return; }
    char buf[512];
    while (fgets(buf, sizeof buf, f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        g_lines = realloc(g_lines, (size_t)(g_nlines + 1) * sizeof *g_lines);
        g_lines[g_nlines++] = _strdup(buf);
    }
    fclose(f);
    const char *r = getenv("OPENUG2_SIM_LINE");
    if (r) g_line = atoi(r);
    printf("sim: script %s, %d lines, from line %d\n", p, g_nlines, g_line);
}
static uint32_t btn_bit(const char *n) {
    for (int i = 0; i < NBTN; i++) if (!_stricmp(n, g_btn[i].name)) return g_btn[i].bit;
    printf("sim: unknown button %s\n", n);
    return 0;
}
static void save_shot(const char *name) {
    unsigned char *rgba = malloc(SIM_W * SIM_H * 4), *rgb = malloc(SIM_W * SIM_H * 3);
    if (!rgba || !rgb) { free(rgba); free(rgb); return; }
    glReadPixels(0, 0, SIM_W, SIM_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int y = 0; y < SIM_H; y++)
        for (int x = 0; x < SIM_W; x++) {
            const unsigned char *s = rgba + ((SIM_H - 1 - y) * SIM_W + x) * 4;
            unsigned char *d = rgb + (y * SIM_W + x) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    char p[256], b[1024];
    snprintf(p, sizeof p, "ux0:shots/%s.png", name);
    write_png(sim_path(p, b, sizeof b), SIM_W, SIM_H, rgb);
    printf("sim: screenshot %s (frame %u)\n", b, g_frame);
    free(rgba); free(rgb);
}
/* "probe X Y" (screenshot pixel coords): for the next frame, log every draw
   that changes that pixel -- which program/texture/count painted it. */
int g_sim_probe; static int g_probe_x, g_probe_y, g_probe_arm, g_probe_n;
static unsigned char g_probe_last[4];
void sim_probe_draw(int count) {
    unsigned char px[4]; GLint prog = 0, tex = 0, unit = 0;
    glReadPixels(g_probe_x, SIM_H - 1 - g_probe_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    g_probe_n++;
    if (memcmp(px, g_probe_last, 4)) {
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &unit); glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex); glActiveTexture((GLenum)unit);
        printf("probe: draw %d count %d prog %d tex %d -> %d %d %d %d\n", g_probe_n, count, prog, tex, px[0], px[1], px[2], px[3]);
        memcpy(g_probe_last, px, 4);
        static const char *names[] = { "uVColor", "uUseTex", "uDecal", "uColor", "uAlpha", "uCDiffMin",
            "uCDiffRange", "uCPow", "uSH0", "uFlipN", "uRimTint", "uSpec", "uAmbient", "uDiffuse", "uEmissive" };
        for (int k = 0; k < (int)(sizeof names / sizeof *names); k++) {
            GLint l = glGetUniformLocation((GLuint)prog, names[k]);
            if (l < 0) continue;
            GLfloat v[4] = {0};
            glGetUniformfv((GLuint)prog, l, v);
            printf("probe:   %s = %.3f %.3f %.3f %.3f\n", names[k], v[0], v[1], v[2], v[3]);
        }
        GLint en = 0; glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &en);
        GLint cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
        printf("probe:   attr3 %d cull %d blend %d\n", en, cull, blend);
    }
}
int g_sim_post;
static Uint32 g_waitpost_until;
static void script_step(void) {
    if (g_tap.frames > 0 && --g_tap.frames == 0) g_script_buttons &= ~g_tap.bit;
    if (g_tap.frames > 0) return;
    if (g_wait_frames > 0) { g_wait_frames--; return; }
    if (g_wait_until && SDL_GetTicks() < g_wait_until) return;
    if (g_waitpost_until) {   /* waitpost: until a post-race screen is drawn, or the timeout */
        if (!g_sim_post && SDL_GetTicks() < g_waitpost_until) return;
        g_waitpost_until = 0;
    }
    g_wait_until = 0;
    while (g_line < g_nlines) {
        char cmd[32] = "", a1[128] = "", a2[64] = "";
        int n = sscanf(g_lines[g_line++], "%31s %127s %63s", cmd, a1, a2);
        if (n < 1 || cmd[0] == '#') continue;
        printf("sim: [%d] %s %s %s\n", g_line, cmd, a1, a2);
        if (!_stricmp(cmd, "frames")) { g_wait_frames = atoi(a1); return; }
        if (!_stricmp(cmd, "waitpost")) { g_sim_post = 0; g_waitpost_until = SDL_GetTicks() + (Uint32)(atof(a1) * 1000); return; }
        if (!_stricmp(cmd, "wait")) { g_wait_until = SDL_GetTicks() + (Uint32)(atof(a1) * 1000); return; }
        if (!_stricmp(cmd, "press")) {
            g_tap.bit = btn_bit(a1); g_tap.frames = n > 2 ? atoi(a2) : 3;
            g_script_buttons |= g_tap.bit; return;
        }
        if (!_stricmp(cmd, "hold")) { g_script_buttons |= btn_bit(a1); continue; }
        if (!_stricmp(cmd, "release")) { g_script_buttons &= ~btn_bit(a1); continue; }
        if (!_stricmp(cmd, "stick")) { g_script_lx = (float)atof(a1); g_script_ly = (float)atof(a2); continue; }
        if (!_stricmp(cmd, "rtouch")) {
            if (!_stricmp(a1, "off")) g_script_tu = g_script_tv = -1.0f;
            else { g_script_tu = (float)atof(a1); g_script_tv = (float)atof(a2); }
            continue;
        }
        if (!_stricmp(cmd, "rstick")) { g_script_rx = (float)atof(a1); g_script_ry = (float)atof(a2); continue; }
        if (!_stricmp(cmd, "hyd")) { g_script_hyd = atoi(a1); continue; }
        if (!_stricmp(cmd, "shot")) { snprintf(g_shot_name, sizeof g_shot_name, "%s", a1); return; }
        if (!_stricmp(cmd, "probe")) { g_probe_x = atoi(a1); g_probe_y = atoi(a2); g_probe_arm = 1; return; }
        if (!_stricmp(cmd, "quit")) { fflush(stdout); exit(0); }
        printf("sim: unknown command %s\n", cmd);
    }
}

/* ---- window / GL ------------------------------------------------------------ */
static int g_inited;
int sim_Init(Uint32 flags) {
    if (g_inited) return 0;
    g_inited = 1;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
    int r = SDL_Init(flags | SDL_INIT_VIDEO | SDL_INIT_TIMER);
    vita_mark("boot logging: Windows simulator (custom gauges build, OpenGL ES 2.0 on %s)", SDL_GetPlatform());
    script_load();
    return r;
}
int sim_GL_SetAttribute(SDL_GLattr attr, int value) { (void)attr; (void)value; return 0; }
SDL_Window *sim_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    (void)title; (void)w; (void)h; (void)flags;
    if (g_win) return g_win;                      /* the Vita has one screen */
    if (!g_inited) sim_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_win = SDL_CreateWindow("OpenUG2 (Vita simulator)", x, y, SIM_W, SIM_H, SDL_WINDOW_OPENGL);
    return g_win;
}
static SDL_GLContext g_ctx;
static PFNGLENABLEPROC g_real_enable;
/* The Vita's depth buffer is DF32M (32-bit float, vitaGL gxm.c); a window's is
   24-bit fixed. Framebuffer 0 is redirected to an offscreen buffer with float
   depth, copied to the window at each swap. */
static GLuint g_main_fbo;
static PFNGLBINDFRAMEBUFFERPROC g_real_bind_fb;
typedef void (__stdcall *BlitFn)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static BlitFn g_blit;
static void __stdcall main_bind_fb(GLenum target, GLuint fb) { g_real_bind_fb(target, fb ? fb : g_main_fbo); }
/* vitaGL gives every depth renderbuffer DF32M whatever format is asked for
   (framebuffers.c glRenderbufferStorage): the game's DEPTH_COMPONENT16 scene
   targets have float depth on the Vita, 16-bit here unless remapped. */
static PFNGLRENDERBUFFERSTORAGEPROC g_real_rb_storage;
static void __stdcall vita_rb_storage(GLenum target, GLenum fmt, GLsizei w, GLsizei h) {
    if (fmt == 0x81A5 /* DEPTH_COMPONENT16 */ || fmt == 0x81A6 /* 24 */ || fmt == 0x1902 /* DEPTH_COMPONENT */)
        fmt = 0x8CAC;   /* GL_DEPTH_COMPONENT32F */
    else if (fmt == 0x88F0 /* DEPTH24_STENCIL8 */)
        fmt = 0x8CAD;   /* GL_DEPTH32F_STENCIL8 */
    g_real_rb_storage(target, fmt, w, h);
}
static void main_fbo_init(void) {
    vu_skip_off = getenv("OPENUG2_SIM_NOUSKIP") != NULL;
    if (!getenv("OPENUG2_SIM_D24")) { g_real_rb_storage = sim_glRenderbufferStorage; sim_glRenderbufferStorage = vita_rb_storage; }
    g_blit = (BlitFn)SDL_GL_GetProcAddress("glBlitFramebuffer");
    if (!g_blit || getenv("OPENUG2_SIM_D24")) { printf("sim: depth 24-bit fixed (window)\n"); return; }
    GLuint rb[2];
    glGenRenderbuffers(2, rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb[0]);
    glRenderbufferStorage(GL_RENDERBUFFER, 0x8058 /* GL_RGBA8 */, SIM_W, SIM_H);
    glBindRenderbuffer(GL_RENDERBUFFER, rb[1]);
    glRenderbufferStorage(GL_RENDERBUFFER, 0x8CAD /* GL_DEPTH32F_STENCIL8 */, SIM_W, SIM_H);
    glGenFramebuffers(1, &g_main_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_main_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, 0x821A /* GL_DEPTH_STENCIL_ATTACHMENT */, GL_RENDERBUFFER, rb[1]);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        printf("sim: float depth buffer incomplete (0x%x), window depth\n", st);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); g_main_fbo = 0; return;
    }
    g_real_bind_fb = sim_glBindFramebuffer; sim_glBindFramebuffer = main_bind_fb;
    printf("sim: depth 32-bit float (like the Vita's DF32M)\n");
}
static void main_fbo_present(void) {
    if (!g_main_fbo) return;
    GLint cur = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur);
    g_real_bind_fb(0x8CA8 /* READ */, g_main_fbo);
    g_real_bind_fb(0x8CA9 /* DRAW */, 0);
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST); if (sc) sim_glDisable(GL_SCISSOR_TEST);
    g_blit(0, 0, SIM_W, SIM_H, 0, 0, SIM_W, SIM_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (sc) sim_glEnable(GL_SCISSOR_TEST);
    g_real_bind_fb(GL_FRAMEBUFFER, (GLuint)cur);
}
static void __stdcall nocull_enable(GLenum cap) { if (cap != GL_CULL_FACE) g_real_enable(cap); }
SDL_GLContext sim_GL_CreateContext(SDL_Window *w) {
    if (g_ctx) return g_ctx;                      /* one context, as on the Vita */
    SDL_GLContext c = g_ctx = SDL_GL_CreateContext(w);
    if (!c) { printf("sim: no OpenGL ES 2.0 context: %s\n", SDL_GetError()); fflush(stdout); return NULL; }
    int missing = sim_gl_load();
    main_fbo_init();
    if (getenv("OPENUG2_SIM_NOCULL")) { g_real_enable = sim_glEnable; sim_glEnable = nocull_enable; }
    printf("sim: GL %s | %s | %s (%d entry points missing)\n", (const char *)glGetString(GL_VERSION),
           (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION), missing);
    printf("display: %dx%d framebuffer (hardware upscaled, no blit pass)\n", SIM_W, SIM_H);
    return c;
}
void sim_GL_GetDrawableSize(SDL_Window *w, int *dw, int *dh) { (void)w; *dw = SIM_W; *dh = SIM_H; }
void sim_GetWindowSize(SDL_Window *w, int *ww, int *wh) { (void)w; *ww = SIM_W; *wh = SIM_H; }
/* The Vita's display is 60 Hz and the game locks 30 (vita_lock30); a PC
   monitor's vsync is whatever it is (120+ Hz here), which ran every
   per-frame timer at the wrong speed. So: no vsync, the frames paced to the
   Vita's rate in sim_GL_SwapWindow. */
int sim_GL_SetSwapInterval(int interval) {
    (void)interval;
    printf("frame rate: %s\n", getenv("OPENUG2_SIM_FAST") ? "unlocked (simulator)"
                                                         : "paced to the Vita (30 locked / 60)");
    return SDL_GL_SetSwapInterval(0);
}
static void sim_pace(void) {
    if (getenv("OPENUG2_SIM_FAST")) return;
    static Uint64 next;
    Uint64 f = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
    Uint64 period = f / (vita_lock30 > 0 ? 30 : 60);
    if (!next || now > next + period) next = now;          /* first frame / fell behind */
    while ((now = SDL_GetPerformanceCounter()) < next) {
        Uint64 left_ms = (next - now) * 1000 / f;
        if (left_ms > 2) SDL_Delay((Uint32)(left_ms - 1));
    }
    next += period;
}
void sim_GL_SwapWindow(SDL_Window *w) {
    g_frame++;
    if (g_sim_probe) { g_sim_probe = 0; printf("probe: end of frame %u\n", g_frame); }
    if (g_probe_arm) { g_probe_arm = 0; g_sim_probe = 1; g_probe_n = 0; memset(g_probe_last, 0xff, 4); }
    if (g_shot_name[0]) { save_shot(g_shot_name); g_shot_name[0] = 0; }
    if (g_shot_req) { char n[64]; snprintf(n, sizeof n, "f12_%u", g_frame); save_shot(n); g_shot_req = 0; }
    main_fbo_present();
    sim_pace();
    SDL_GL_SwapWindow(w);
    script_step();
    if (g_frame % 30 == 0) fflush(stdout);
}

/* Restart the app (a venue change reads ux0:data/OpenUG2/venue.txt): a new
   process, the script carrying on from the next line. */
void vita_relaunch(void) {
    char exe[MAX_PATH], line[32];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    snprintf(line, sizeof line, "%d", g_line);
    SetEnvironmentVariableA("OPENUG2_SIM_LINE", line);
    fflush(stdout);
    char quoted[MAX_PATH + 2]; snprintf(quoted, sizeof quoted, "\"%s\"", exe);
    /* Wait for the new process so a caller (timeout, a test loop) sees the
       whole run; the window goes first, the Vita has one screen. */
    SDL_Quit();
    intptr_t rc = _spawnl(_P_WAIT, exe, quoted, NULL);
    exit(rc < 0 ? 1 : (int)rc);
}
