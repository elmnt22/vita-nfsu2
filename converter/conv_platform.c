/* conv_platform.c -- OpenUG2 PC converter: the platform layer the game's
 * world code needs to run the one-time Vita conversion on a PC (the parts of
 * sim/sim_vita.c the converter uses, without the window, GL or pad).
 *
 * Folders: ux0:data/OpenUG2/cache/... goes to <output>/cache/..., the rest
 * of ux0:data/OpenUG2/... reads from the game folder (TRACKS, GLOBAL, ...),
 * anything else on ux0: lands under <output>. The game sources are built
 * 32-bit (ILP32 like the Vita) with __vita__ on, so every file written here
 * is the one the console writes.
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
#include <windows.h>
#include "SDL.h"
#include "SDL_opengles2.h"
#include "psp2/io/fcntl.h"

/* ---- folders ------------------------------------------------------------- */
static char g_game[MAX_PATH], g_out[MAX_PATH];
void conv_set_folders(const char *game, const char *out) {
    snprintf(g_game, sizeof g_game, "%s", game);
    snprintf(g_out, sizeof g_out, "%s", out);
}
#define DATA_PREFIX "ux0:data/OpenUG2/"
const char *sim_path(const char *p, char *buf, size_t cap) {
    if (strncmp(p, "ux0:", 4)) return p;
    if (!strncmp(p, DATA_PREFIX, sizeof DATA_PREFIX - 1)) {
        const char *rest = p + sizeof DATA_PREFIX - 1;
        int cache = !strncmp(rest, "cache", 5) && (rest[5] == 0 || rest[5] == '/');
        snprintf(buf, cap, "%s/%s", cache ? g_out : g_game, rest);
    } else {
        const char *rest = p + 4;
        while (*rest == '/') rest++;
        snprintf(buf, cap, "%s/%s", g_out, rest);
    }
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
int sim_rename(const char *a, const char *bb) {
    char b1[1024], b2[1024];
    const char *x = sim_path(a, b1, sizeof b1), *y = sim_path(bb, b2, sizeof b2);
    remove(y);                                    /* POSIX rename replaces */
    return rename(x, y);
}
int sim_stat(const char *p, struct stat *st) { char b[1024]; return stat(sim_path(p, b, sizeof b), st); }

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

/* ---- psp2 stand-ins -------------------------------------------------------- */
SceUInt64 sceKernelGetProcessTimeWide(void) {
    static Uint64 f0, t0;
    if (!f0) { f0 = SDL_GetPerformanceFrequency(); t0 = SDL_GetPerformanceCounter(); }
    return (SceUInt64)((SDL_GetPerformanceCounter() - t0) * 1000000ull / f0);
}
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

/* ---- the console's logging and memory reports ----------------------------- */
void vita_mark(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}
void vita_mark_stats(void) {}
void vita_heap_report(const char *label) { (void)label; }
unsigned vita_bigblk_room_mb(void) { return 512; }
void vita_shader_failed(unsigned type, const char *src, const char *log) { (void)type; (void)src; (void)log; }
int vita_probe_skip;
int g_sim_probe;
void sim_probe_draw(int count) { (void)count; }

/* No GL here: nothing the conversion runs draws or uploads. */
void *vglGetTexDataPointer(GLenum target) { (void)target; return (void *)1; }
void glProgramBinary(GLuint program, GLenum format, const void *binary, GLsizei length) {
    (void)program; (void)format; (void)binary; (void)length;
}
void glGetProgramBinary(GLuint program, GLsizei cap, GLsizei *length, GLenum *format, void *binary) {
    (void)program; (void)cap; (void)binary;
    if (length) *length = 0;
    if (format) *format = 0;
}
