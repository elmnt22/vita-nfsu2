/* sdl_vita.c — OpenUG2 PS Vita platform layer: the SDL2 subset declared in
 * vita/include/SDL.h, implemented on vitaGL, SceCtrl, SceAudio and SceKernel.
 *
 * Driving goes through the controller setup's bindings (options.h g_opt.ctl,
 * defaults options_ctl_preset: the original's PS-style pad layout, its L2 R2
 * L3 R3 on the rear touchpad corners) to the desktop keys the engine reads.
 * The left stick always steers (analog). Menus use fixed buttons: Cross OK,
 * Circle back, Square, Triangle, the D-pad, L / R ([ / ]), Start (Enter).
 *   Select          developer overlay     (1)
 *   Select + Start  quit                  (Esc)
 */
#include "options.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <malloc.h>
#include <unistd.h>
#include <vitaGL.h>
#include <vitashark.h>
#include <psp2/gxm.h>
#include <psp2/apputil.h>
#include <psp2/appmgr.h>
#include <psp2/system_param.h>
#include <psp2/display.h>

#include "SDL.h"

#ifndef GL_DITHER
#define GL_DITHER 0x0BD0
#endif

/* newlib heap for the engine's own allocations (track bundles are read whole
   into RAM). Whatever user RAM is left after this goes to vitaGL's pools,
   alongside the 128 MB of CDRAM that holds textures and vertex buffers. */
/* 115 MB (was 150): the heap holds the small allocations, whose live peak
   over a long race was 85 MB (10-11, heap live minus big-size chunks).
   Big blocks belong in kernel blocks, which never fragment; the 35 MB moved
   to BIGBLK_POOL, so vitaGL's share is unchanged. */
int _newlib_heap_size_user = 115 * 1024 * 1024;   /* + up to BIGBLK_POOL in kernel blocks */
static int g_gl_ready;
static SceUID g_main_thid = -1;   /* render thread (vita_early_init) */   /* vitaGL initialised (its pools can be queried) */
unsigned int sceUserMainThreadStackSize = 8 * 1024 * 1024;
static SceUID g_boot_fd=-1;
static SceKernelLwMutexWork g_boot_lock;
static int g_boot_lock_ready;
static unsigned long long g_boot_mark_us, g_boot_write_us;
static unsigned g_boot_marks;
/* Producers never take the disk-write lock once gameplay logging is queued. */
#define BOOT_QUEUE 128
static struct { char line[320]; int len; } g_boot_queue[BOOT_QUEUE];
static SceKernelLwMutexWork g_boot_queue_lock;
static SceUID g_boot_thread=-1;
static unsigned g_boot_head,g_boot_tail,g_boot_dropped;
static int g_boot_stop;
/* log.txt background writer (see async_put) */
static SceUID g_async_thread = -1;
static unsigned g_async_waits, g_async_writes;

static void boot_write_line(const char *line,int len) {
    if (g_boot_lock_ready) sceKernelLockLwMutex(&g_boot_lock,1,NULL);
    unsigned long long begin=sceKernelGetProcessTimeWide();
    SceUID fd=g_boot_fd;
    if(fd<0) {
        fd=sceIoOpen("ux0:data/NFSU2/boot.txt",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
        if(g_boot_lock_ready)g_boot_fd=fd;
    }
    if(fd>=0) {
        int done=0;
        while(done<len) { int n=sceIoWrite(fd,line+done,(SceSize)(len-done));
            if(n<=0)break;done+=n; }
        if(!g_boot_lock_ready)sceIoClose(fd);
    }
    g_boot_write_us+=sceKernelGetProcessTimeWide()-begin;
    if(g_boot_lock_ready)sceKernelUnlockLwMutex(&g_boot_lock,1);
}
static int boot_writer(unsigned args,void *argp) {
    (void)args;(void)argp;
    for(;;) {
        char line[320];int len=0,stop;
        sceKernelLockLwMutex(&g_boot_queue_lock,1,NULL);
        if(g_boot_tail!=g_boot_head) {
            unsigned i=g_boot_tail++%BOOT_QUEUE;
            len=g_boot_queue[i].len;memcpy(line,g_boot_queue[i].line,(size_t)len);
        }
        stop=g_boot_stop;
        sceKernelUnlockLwMutex(&g_boot_queue_lock,1);
        if(len)boot_write_line(line,len);
        else if(stop)break;
        else sceKernelDelayThread(10000);
    }
    return 0;
}
static void boot_queue_start(void) {
    if(sceKernelCreateLwMutex(&g_boot_queue_lock,"boot-queue",0,0,NULL)<0)return;
    SceUID uid=sceKernelCreateThread("boot-writer",boot_writer,0x10000100,0x10000,0,SCE_KERNEL_CPU_MASK_USER_1|SCE_KERNEL_CPU_MASK_USER_2,NULL);   /* off the frame thread's core 0 */
    if(uid>=0 && sceKernelStartThread(uid,0,NULL)>=0) {g_boot_thread=uid;return;}
    if(uid>=0)sceKernelDeleteThread(uid);
    sceKernelDeleteLwMutex(&g_boot_queue_lock);
}

/* Heap break above its start: the newlib heap's high-water mark. O(1). */
static char *g_heap_base;
unsigned vita_heap_top_mb(void) {
    char *top = (char *)sbrk(0);
    return g_heap_base && top != (char *)-1 ? (unsigned)((top - g_heap_base) >> 20) : 0;
}

/* Heap picture for the streaming loader: live bytes, free bytes inside the
   heap, how much of that free space is the untouched top (the rest is holes
   between live blocks), and the break. mallinfo walks every free chunk and
   holds the malloc lock while it does (~50 ms), so call it only from the
   loader thread, never from the render thread. */
void vita_mark(const char *fmt, ...);
void vita_bigblk_report(char *out, size_t cap);
void vita_heap_report(const char *label) {
    struct mallinfo mi = mallinfo();
    unsigned brk_mb = vita_heap_top_mb();
    unsigned reserved = (unsigned)(_newlib_heap_size_user >> 20);
    vita_mark("heap %s: live %u MB, free in heap %u MB (top %u MB, holes %u MB), brk %u of %u MB",
              label, (unsigned)(mi.uordblks >> 20), (unsigned)(mi.fordblks >> 20),
              (unsigned)(mi.keepcost >> 20), (unsigned)((mi.fordblks - mi.keepcost) >> 20),
              brk_mb, reserved);
    char big[128]; vita_bigblk_report(big, sizeof big);
    SceKernelFreeMemorySizeInfo fi; memset(&fi, 0, sizeof fi); fi.size = sizeof fi;
    if (sceKernelGetFreeMemorySize(&fi) < 0) fi.size_user = 0;
    vita_mark("  %s; kernel user free %u MB", big, (unsigned)(fi.size_user >> 20));
}

/* Earliest possible sign of life: runs after the C runtime (and its heap
   reservation) but before main(). If boot.txt is missing, the app died in
   the loader or while reserving the heap. */
__attribute__((constructor(101))) static void vita_boot_probe(void) {
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    sceKernelGetFreeMemorySize(&info);
    char line[192];
    int len = snprintf(line, sizeof line,
        "boot ok: newlib heap %u MB reserved; still free: user %u MB, cdram %u MB, phycont %u MB\n",
        (unsigned)(_newlib_heap_size_user >> 20), (unsigned)(info.size_user >> 20),
        (unsigned)(info.size_cdram >> 20), (unsigned)(info.size_phycont >> 20));
    g_heap_base = (char *)sbrk(0);
    sceIoMkdir("ux0:data/NFSU2", 0777);
    /* the last run's logs survive one relaunch as *_prev.txt (a crash's log was lost to the relaunch) */
    sceIoRemove("ux0:data/NFSU2/log_prev.txt");    sceIoRename("ux0:data/NFSU2/log.txt", "ux0:data/NFSU2/log_prev.txt");
    sceIoRemove("ux0:data/NFSU2/boot_prev.txt");   sceIoRename("ux0:data/NFSU2/boot.txt", "ux0:data/NFSU2/boot_prev.txt");
    sceIoRemove("ux0:data/NFSU2/crumbs_prev.txt"); sceIoRename("ux0:data/NFSU2/crumbs.txt", "ux0:data/NFSU2/crumbs_prev.txt");
    SceUID fd = sceIoOpen("ux0:data/NFSU2/boot.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, (SceSize)len); sceIoClose(fd); }
    g_boot_lock_ready=sceKernelCreateLwMutex(&g_boot_lock,"openug-boot",0,0,NULL)>=0;
    if (g_boot_lock_ready)
        g_boot_fd=sceIoOpen("ux0:data/NFSU2/boot.txt",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
}

/* Startup milestones appended straight to boot.txt, independent of stdio, so
   a hang or crash anywhere during boot shows the last step reached. */
void vita_mark(const char *fmt, ...) {
    unsigned long long begin=sceKernelGetProcessTimeWide();
    char line[320];
    /* mallinfo() walks the heap under the malloc lock (~40 ms in a streamed
       world); gameplay markers report the break high-water mark instead. */
    int queued = g_boot_thread >= 0;
    int len = snprintf(line, sizeof line, queued ? "[%7.2fs brk %3u MB" : "[%7.2fs heap %3u MB",
                       (double)sceKernelGetProcessTimeWide() / 1e6,
                       queued ? vita_heap_top_mb() : (unsigned)(mallinfo().uordblks >> 20));
    /* vitaGL pool headroom: video RAM and its own RAM pool. Startup only:
       vglMemFree walks the pool and reached ~45 ms per call as VRAM
       fragmented over a streamed session (hitch profiler, 2026-10-05). */
    if (g_gl_ready && !queued)
        len += snprintf(line + len, sizeof line - (size_t)len, ", gpu free vram %u ram %u MB",
                        (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 20),
                        (unsigned)(vglMemFree(VGL_MEM_RAM) >> 20));
    len += snprintf(line + len, sizeof line - (size_t)len, "] ");
    va_list ap;
    va_start(ap, fmt);
    len += vsnprintf(line + len, sizeof line - (size_t)len - 1, fmt, ap);
    va_end(ap);
    if (len > (int)sizeof line - 2) len = (int)sizeof line - 2;
    line[len++] = '\n';
    if(g_boot_thread>=0) {
        sceKernelLockLwMutex(&g_boot_queue_lock,1,NULL);
        if(g_boot_head-g_boot_tail<BOOT_QUEUE) {
            unsigned i=g_boot_head++%BOOT_QUEUE;
            memcpy(g_boot_queue[i].line,line,(size_t)len);g_boot_queue[i].len=len;
        } else g_boot_dropped++;
        g_boot_mark_us+=sceKernelGetProcessTimeWide()-begin;g_boot_marks++;
        sceKernelUnlockLwMutex(&g_boot_queue_lock,1);
    } else {
        boot_write_line(line,len);
        if(g_boot_lock_ready)sceKernelLockLwMutex(&g_boot_lock,1,NULL);
        g_boot_mark_us+=sceKernelGetProcessTimeWide()-begin;g_boot_marks++;
        if(g_boot_lock_ready)sceKernelUnlockLwMutex(&g_boot_lock,1);
    }
}

/* Snapshot only. Print after unlocking to avoid coupling the two log locks. */
void vita_mark_stats(void) {
    if(g_boot_thread>=0)sceKernelLockLwMutex(&g_boot_queue_lock,1,NULL);
    else if(g_boot_lock_ready)sceKernelLockLwMutex(&g_boot_lock,1,NULL);
    unsigned queued=g_boot_head-g_boot_tail,dropped=g_boot_dropped;
    unsigned count=g_boot_marks;
    unsigned total=(unsigned)(g_boot_mark_us/1000);
    if(g_boot_thread>=0)sceKernelUnlockLwMutex(&g_boot_queue_lock,1);
    else if(g_boot_lock_ready)sceKernelUnlockLwMutex(&g_boot_lock,1);
    if (g_boot_lock_ready) sceKernelLockLwMutex(&g_boot_lock,1,NULL);
    unsigned writes=(unsigned)(g_boot_write_us/1000);
    if (g_boot_lock_ready) sceKernelUnlockLwMutex(&g_boot_lock,1);
    printf("boot diagnostics: %u markers, %u ms total, %u ms disk writes (cumulative)\n",count,total,writes);
    printf("boot queue: %u pending, %u dropped\n",queued,dropped);
    printf("log queue: %s, %u card writes, %u producer waits\n",
           g_async_thread>=0?"background":"synchronous",g_async_writes,g_async_waits);
}

#define VITA_W 960
#define VITA_H 544
#define VITA_DATA "ux0:data/NFSU2"

/* ---- logging: stdout/stderr -> ux0:data/NFSU2/log.txt ------------------
   freopen() on the Vita opens the file but newlib keeps writing fds 1/2 to
   tty0, and swapping the stdout FILE pointer does not stick either. So the
   final syscall is intercepted instead (linked with -Wl,--wrap=_write_r):
   writes to fds 1/2 go straight to sceIoWrite, without a stdio buffer. Keep
   one append handle: streamed builds emit many lines, so opening/closing the
   file for every fragment adds memory-card work to the loading path. */
#define LOG_PATH  VITA_DATA "/log.txt"
static int g_log_ready;
static int g_logs_off;   /* nolog.txt, see vlog() */
static SceUID g_log_fd = -1;
static SceKernelLwMutexWork g_log_lock;
static int g_log_lock_ready;

static int log_write_sync(const char *buf, int n) {
    static int reported;
    if (g_log_lock_ready) sceKernelLockLwMutex(&g_log_lock, 1, NULL);
    SceUID fd = g_log_fd;
    if (fd < 0) {
        fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
        if (g_log_lock_ready) g_log_fd = fd;
    }
    int w = -1;
    if (fd >= 0) {
        w = sceIoWrite(fd, buf, (SceSize)n);
        if (!g_log_lock_ready) sceIoClose(fd);
    }
    int report = (fd < 0 || w != n) && !reported;
    if (report) reported = 1;
    if (g_log_lock_ready) sceKernelUnlockLwMutex(&g_log_lock, 1);
    if (report) {
        vita_mark("log write failed: open 0x%08X, wrote %d of %d", (unsigned)fd, w, n);
    }
    return n;
}

/* Gameplay log writes are queued: one sceIoWrite to the memory card costs
   ~20 ms, and a stream activation prints several lines in one frame. Producers
   copy into a ring under a short lock; a background thread writes contiguous
   runs (many lines per write). A full ring makes the producer wait rather than
   drop or reorder lines. An abrupt crash can lose the queued tail. The same
   path carries one extra newlib fd (perf.csv), see vita_async_file(). */
#define ASYNC_RING (64 * 1024)
typedef struct { char data[ASYNC_RING]; unsigned head, tail; int fd; } AsyncRing;
static AsyncRing g_ring_log = { .fd = -1 }, g_ring_file = { .fd = -1 };
static SceKernelLwMutexWork g_async_lock;
static int g_async_stop;
_ssize_t __real__write_r(struct _reent *r, int fd, const void *buf, size_t n);

/* 1: queued. 0: not queued, the caller writes synchronously (no thread, or a
   write bigger than the ring once everything before it is on the card). */
static int async_put(AsyncRing *q, const char *buf, unsigned n) {
    if (!n) return 1;
    for (;;) {
        if (g_async_thread < 0) return 0;
        sceKernelLockLwMutex(&g_async_lock, 1, NULL);
        unsigned used = q->head - q->tail;
        if (ASYNC_RING - used >= n) {
            unsigned off = q->head % ASYNC_RING, first = ASYNC_RING - off;
            if (first > n) first = n;
            memcpy(q->data + off, buf, first);
            memcpy(q->data, buf + first, n - first);
            q->head += n;
            sceKernelUnlockLwMutex(&g_async_lock, 1);
            return 1;
        }
        int empty = used == 0;
        g_async_waits++;
        sceKernelUnlockLwMutex(&g_async_lock, 1);
        if (empty) return 0;
        sceKernelDelayThread(1000);
    }
}

static void async_ring_write(AsyncRing *q, const char *p, unsigned n) {
    g_async_writes++;
    if (q == &g_ring_log) { log_write_sync(p, (int)n); return; }
    while (n) {
        _ssize_t w = __real__write_r(_REENT, q->fd, p, n);
        if (w <= 0) break;
        p += w; n -= (unsigned)w;
    }
}

static int async_writer(unsigned args, void *argp) {
    (void)args; (void)argp;
    AsyncRing *rings[2] = { &g_ring_log, &g_ring_file };
    for (;;) {
        int busy = 0, stop;
        for (int i = 0; i < 2; i++) {
            AsyncRing *q = rings[i];
            sceKernelLockLwMutex(&g_async_lock, 1, NULL);
            unsigned used = q->head - q->tail, off = q->tail % ASYNC_RING;
            unsigned n = used < ASYNC_RING - off ? used : ASYNC_RING - off;
            sceKernelUnlockLwMutex(&g_async_lock, 1);
            if (!n) continue;
            async_ring_write(q, q->data + off, n);   /* producers never touch [tail, head) */
            sceKernelLockLwMutex(&g_async_lock, 1, NULL);
            q->tail += n;
            sceKernelUnlockLwMutex(&g_async_lock, 1);
            busy = 1;
        }
        if (busy) continue;
        sceKernelLockLwMutex(&g_async_lock, 1, NULL);
        stop = g_async_stop;
        sceKernelUnlockLwMutex(&g_async_lock, 1);
        if (stop) break;
        sceKernelDelayThread(5000);
    }
    return 0;
}

static void async_log_start(void) {
    if (g_async_thread >= 0) return;
    if (sceKernelCreateLwMutex(&g_async_lock, "log-queue", 0, 0, NULL) < 0) return;
    SceUID uid = sceKernelCreateThread("log-writer", async_writer, 0x10000100, 0x10000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);   /* off core 0 */
    if (uid >= 0 && sceKernelStartThread(uid, 0, NULL) >= 0) { g_async_thread = uid; return; }
    if (uid >= 0) sceKernelDeleteThread(uid);
    sceKernelDeleteLwMutex(&g_async_lock);
}

static void async_log_stop(void) {
    if (g_async_thread < 0) return;
    sceKernelLockLwMutex(&g_async_lock, 1, NULL);
    g_async_stop = 1;
    sceKernelUnlockLwMutex(&g_async_lock, 1);
    sceKernelWaitThreadEnd(g_async_thread, NULL, NULL);
    sceKernelDeleteThread(g_async_thread);
    g_async_thread = -1;
    sceKernelDeleteLwMutex(&g_async_lock);
}

/* Route a stdio FILE's writes (newlib flushes) through the background writer. */
void vita_async_file(FILE *f) {
    if (!f) return;
    setvbuf(f, NULL, _IOFBF, 16384);
    g_ring_file.fd = fileno(f);
}

static int log_write(const char *buf, int n) {
    if (g_logs_off) return n;
    if (n > 0 && async_put(&g_ring_log, buf, (unsigned)n)) return n;
    return log_write_sync(buf, n);
}

/* stdio print functions, wrapped (-Wl,--wrap=...) so that anything the
   engine prints to stdout/stderr reaches log.txt immediately. newlib keeps
   stdout block-buffered whatever setvbuf asks, which loses the last ~1 KB,
   i.e. exactly the error printed right before a crash. GCC also rewrites
   printf into puts/putchar and fprintf into fputs/fwrite, so those are
   wrapped too. Other streams pass through to the real functions. */
static int is_std(FILE *f) { return f == stdout || f == stderr; }

/* nolog.txt (the Settings screen's Logs: off): nothing reaches log.txt
   after its one "logging off" line. */
int vita_logs_off(void) { return g_logs_off; }

static int vlog(const char *fmt, va_list ap) {
    if (g_logs_off) return 0;
    char small[1024];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(small, sizeof small, fmt, ap);
    if (n < 0) { va_end(ap2); return n; }
    if ((size_t)n < sizeof small) log_write(small, n);
    else {
        char *big = (char *)malloc((size_t)n + 1);
        if (big) { vsnprintf(big, (size_t)n + 1, fmt, ap2); log_write(big, n); free(big); }
    }
    va_end(ap2);
    return n;
}

int __real_vfprintf(FILE *f, const char *fmt, va_list ap);
int __real_fputs(const char *s, FILE *f);
int __real_fputc(int c, FILE *f);
size_t __real_fwrite(const void *p, size_t size, size_t count, FILE *f);

int __wrap_vprintf(const char *fmt, va_list ap) { return vlog(fmt, ap); }
int __wrap_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vlog(fmt, ap); va_end(ap); return n;
}
int __wrap_vfprintf(FILE *f, const char *fmt, va_list ap) {
    return is_std(f) ? vlog(fmt, ap) : __real_vfprintf(f, fmt, ap);
}
int __wrap_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = is_std(f) ? vlog(fmt, ap) : __real_vfprintf(f, fmt, ap);
    va_end(ap); return n;
}
int __wrap_puts(const char *s) {
    int n = (int)strlen(s);
    log_write(s, n); log_write("\n", 1);
    return n + 1;
}
int __wrap_fputs(const char *s, FILE *f) {
    if (!is_std(f)) return __real_fputs(s, f);
    log_write(s, (int)strlen(s));
    return 0;
}
int __wrap_putchar(int c) { char ch = (char)c; log_write(&ch, 1); return c & 0xFF; }
int __wrap_fputc(int c, FILE *f) {
    if (!is_std(f)) return __real_fputc(c, f);
    char ch = (char)c; log_write(&ch, 1); return c & 0xFF;
}
size_t __wrap_fwrite(const void *p, size_t size, size_t count, FILE *f) {
    if (!is_std(f)) return __real_fwrite(p, size, count, f);
    log_write((const char *)p, (int)(size * count));
    return count;
}

/* Messages from the Cg compiler behind vitaGL (vitashark). vitaGL only keeps
   them when built with HAVE_SHARK_LOG, so collect them here instead. */
static char g_shark_msgs[4096];
static int g_shark_len;

static void shark_cb(const char *msg, shark_log_level level, int line) {
    const char *tag = level == SHARK_LOG_ERROR ? "error" :
                      level == SHARK_LOG_WARNING ? "warning" : "info";
    int room = (int)sizeof g_shark_msgs - g_shark_len;
    if (room <= 1) return;
    int n = snprintf(g_shark_msgs + g_shark_len, (size_t)room, "%s (line %d): %s\n",
                     tag, line, msg ? msg : "");
    g_shark_len += n < room ? n : room - 1;
}

/* A GLSL shader vitaGL could not compile. vitaGL crashes inside glLinkProgram
   when handed one, so the renderer substitutes a fallback; this records the
   compiler's message in boot.txt and the full source in shader_fail_N.txt. */
void vita_shader_failed(unsigned type, const char *src, const char *log) {
    static int count;
    count++;
    if ((!log || !log[0]) && g_shark_len) log = g_shark_msgs;
    vita_mark("SHADER COMPILE FAILED #%d (%s): %.150s", count,
              type == 0x8B31 ? "vertex" : "fragment", log ? log : "");
    char path[128];
    snprintf(path, sizeof path, VITA_DATA "/shader_fail_%d.txt", count);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return;
    const char *h1 = "=== compiler log ===\n", *h2 = "\n=== source ===\n";
    sceIoWrite(fd, h1, (SceSize)strlen(h1));
    if (log) sceIoWrite(fd, log, (SceSize)strlen(log));
    sceIoWrite(fd, h2, (SceSize)strlen(h2));
    if (src) sceIoWrite(fd, src, (SceSize)strlen(src));
    if (g_shark_len) {
        const char *h3 = "\n=== all compiler messages since last report ===\n";
        sceIoWrite(fd, h3, (SceSize)strlen(h3));
        sceIoWrite(fd, g_shark_msgs, (SceSize)g_shark_len);
    }
    sceIoClose(fd);
    g_shark_len = 0;
    g_shark_msgs[0] = 0;
}

struct _reent;
_ssize_t __wrap__write_r(struct _reent *r, int fd, const void *buf, size_t n) {
    if (g_log_ready && (fd == 1 || fd == 2)) return log_write((const char *)buf, (int)n);
    if (fd >= 0 && fd == g_ring_file.fd && async_put(&g_ring_file, (const char *)buf, (unsigned)n))
        return (_ssize_t)n;
    return __real__write_r(r, fd, buf, n);
}

void vita_log_flush(void) {
    fflush(stdout);
}

/* Race-start breadcrumbs: one synchronous append to crumbs.txt per call
   (open, write, close), so the last step before a crash survives where the
   queued log loses its tail. Truncated at boot. */
void vita_crumb(const char *fmt, ...) {
    char line[256];
    int len = snprintf(line, sizeof line, "[%8.3fs brk %3u MB] ",
                       (double)sceKernelGetProcessTimeWide() / 1e6, vita_heap_top_mb());
    va_list ap;
    va_start(ap, fmt);
    len += vsnprintf(line + len, sizeof line - (size_t)len - 1, fmt, ap);
    va_end(ap);
    if (len > (int)sizeof line - 2) len = (int)sizeof line - 2;
    line[len++] = '\n';
    SceUID fd = sceIoOpen(VITA_DATA "/crumbs.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, (SceSize)len); sceIoClose(fd); }
}

/* Race-hang watchdog: the main loop bumps vita_wd_beat at every perf mark
   (vita_wd_sec = the section just closed) and arms vita_wd_armed each frame
   while a race clock runs (no loading in a race). Armed and no beat for
   WD_SECONDS: a synchronous crumb names the section, then this thread
   faults on purpose so the Vita writes a core dump with every thread's
   registers and stack -- the main thread's PC is where it hangs. */
volatile int vita_wd_armed, vita_wd_sec;
volatile unsigned vita_wd_beat;
/* The boot world load (main.c sets it from the smokeables to the end of the
   world load, crumbs.txt names each step): a load stuck WD_LOAD_SECONDS
   faults the same way, so a hung load leaves a core dump (10-31 HW: a venue
   boot hung after the smokeables with no dump). A normal load is ~16 s. */
volatile int vita_wd_load;
#define WD_SECONDS 15
#define WD_LOAD_SECONDS 120
/* Everywhere else (front end, shops, crib, free roam, loads) once the main
   loop runs: a player-reported hard freeze (11-14, a shop after skipping the
   speech) left nothing behind. The longest normal main-thread stall seen on
   HW is ~20 s (a free-roam start), so 90 s is only ever a real hang -- the
   game then ends with a crumb + core dump instead of freezing for good. */
#define WD_IDLE_SECONDS 90
static int wd_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    unsigned last = vita_wd_beat;
    int last_load = vita_wd_load;
    Uint64 since = sceKernelGetProcessTimeWide(), prev = since;
    for (;;) {
        sceKernelDelayThread(500 * 1000);
        Uint64 now = sceKernelGetProcessTimeWide();
        unsigned b = vita_wd_beat;
        int load = vita_wd_load, armed = vita_wd_armed;
        /* a long gap in this thread's own sleep = the Vita was suspended
           (standby / PS button): the main loop gets its time to beat again */
        int resumed = now - prev > 5000000u;
        prev = now;
        if (b != last || load != last_load || resumed || b == 0) { last = b; last_load = load; since = now; continue; }
        unsigned lim = load ? WD_LOAD_SECONDS : armed ? WD_SECONDS : WD_IDLE_SECONDS;
        if (now - since < (Uint64)lim * 1000000u) continue;
        vita_crumb("WATCHDOG: main loop stuck %u s after perf mark %d (beat %u, %s) -- "
                   "faulting for a core dump", (unsigned)((now - since) / 1000000u), vita_wd_sec, b,
                   load ? "in the boot world load, step in the crumbs above"
                   : armed ? "armed in a race" : "outside a race (front end / shop / free roam / load)");
        vita_log_flush();
        sceKernelDelayThread(1000 * 1000);          /* let the log writer drain */
        *(volatile int *)0 = 0x57d;                 /* the core dump */
        return 0;
    }
}
static void wd_start(void) {
    SceUID uid = sceKernelCreateThread("watchdog", wd_thread, 0x10000100, 0x4000, 0,
                                 SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);   /* off core 0 */
    if (uid >= 0) sceKernelStartThread(uid, 0, NULL);
    vita_mark("race watchdog thread: 0x%08X (%d s)", (unsigned)uid, WD_SECONDS);
}

void vita_early_init(void) {
    vita_mark("early init");
    /* The render thread owns core 0; workers (streaming, AI) use cores 1-2. */
    g_main_thid = sceKernelGetThreadId();
    vita_mark("main thread pinned to core 0: 0x%08X", (unsigned)
              sceKernelChangeThreadCpuAffinityMask(g_main_thid, SCE_KERNEL_CPU_MASK_USER_0));
    sceIoMkdir(VITA_DATA, 0777);
    {   SceUID cf = sceIoOpen(VITA_DATA "/crumbs.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (cf >= 0) sceIoClose(cf); }
    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    vita_mark("log.txt truncate: 0x%08X", (unsigned)fd);
    g_log_lock_ready = sceKernelCreateLwMutex(&g_log_lock, "openug-log", 0, 0, NULL) >= 0;
    /* Reopen with append semantics; serialize render/loader writes. */
    if (fd >= 0) sceIoClose(fd);
    if (g_log_lock_ready)
        g_log_fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    g_log_ready = 1;
    {   SceUID nf = sceIoOpen(VITA_DATA "/nolog.txt", SCE_O_RDONLY, 0);
        if (nf >= 0) {
            sceIoClose(nf);
            static const char off[] = "logging off (nolog.txt, LiveArea Settings); boot.txt and crumbs.txt are still written\n";
            log_write_sync(off, (int)sizeof off - 1);
            g_logs_off = 1;
            vita_mark("log.txt off (nolog.txt)");
        } }
    /* tty fds are not "interactive" to newlib, so stdout would otherwise be
       fully buffered and lose its tail on a crash. */
    setvbuf(stdout, NULL, _IONBF, 0);   /* one write per printf call */
    setvbuf(stderr, NULL, _IONBF, 0);
    wd_start();
    struct mallinfo mi = mallinfo();
    printf("OpenUG2 Vita: heap %u MB reserved, %u KB in use at start\n",
           (unsigned)(_newlib_heap_size_user >> 20), (unsigned)(mi.uordblks >> 10));
}

__attribute__((destructor)) static void vita_log_close(void) {
    async_log_stop();   /* drains queued log/perf bytes first */
    if(g_boot_thread>=0) {
        sceKernelLockLwMutex(&g_boot_queue_lock,1,NULL);g_boot_stop=1;
        sceKernelUnlockLwMutex(&g_boot_queue_lock,1);
        sceKernelWaitThreadEnd(g_boot_thread,NULL,NULL);sceKernelDeleteThread(g_boot_thread);
        g_boot_thread=-1;sceKernelDeleteLwMutex(&g_boot_queue_lock);
    }
    if (g_log_fd >= 0) { sceIoClose(g_log_fd); g_log_fd = -1; }
    if (g_log_lock_ready) { sceKernelDeleteLwMutex(&g_log_lock); g_log_lock_ready = 0; }
    if (g_boot_fd>=0) { sceIoClose(g_boot_fd); g_boot_fd=-1; }
    if (g_boot_lock_ready) { sceKernelDeleteLwMutex(&g_boot_lock); g_boot_lock_ready=0; }
}

/* ---- allocation failure reporting (linked with -Wl,--wrap=malloc,...) -----
   The engine does not check every allocation, so an out-of-memory turns into
   a NULL write somewhere later. Log the failing request first, straight
   through sceIo so it lands even if stdio is wedged, then let it continue. */
static void vita_oom(const char *fn, size_t n, void *caller) {
    struct mallinfo mi = mallinfo();
    char line[256];
    int len = snprintf(line, sizeof line,
        "OUT OF MEMORY: %s(%u KB) failed, heap in use %u KB of %u KB, caller %p (vita_oom at %p)\n",
        fn, (unsigned)(n >> 10), (unsigned)(mi.uordblks >> 10),
        (unsigned)(_newlib_heap_size_user >> 10), caller, (void *)vita_oom);
    SceUID fd = sceIoOpen(VITA_DATA "/oom.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, (SceSize)len); sceIoClose(fd); }
    fputs(line, stderr);
    vita_log_flush();
}

void *__real_malloc(size_t n);
void *__real_calloc(size_t c, size_t n);
void *__real_realloc(void *p, size_t n);
/* Big allocations are what decide whether a track fits; log each one. */
#define BIG_ALLOC (4u * 1024 * 1024)
static void vita_big(const char *fn, size_t n, void *caller) {
    sceClibPrintf("ALLOC %s %u KB by %p, heap break %u MB\n", fn, (unsigned)(n >> 10), caller,
                  vita_heap_top_mb());
}

/* ---- large blocks outside the newlib heap ---------------------------------
   Requests of BIGBLK_MIN and up get their own kernel memory block instead of
   heap space. A long drive streams ~55 MB of build data in and out every few
   seconds; inside one heap that left 70+ MB free but only in holes smaller
   than the 2-3 MB arrays a build needs, so builds failed (10-08). Kernel
   blocks are mapped page by page, so they never need a contiguous hole. The
   heap keeps the small, numerous allocations. Either side falls back to the
   other when it runs dry.
   64 KB (was 512, 10-22): tile geometry blocks, mesh arrays and build arrays
   of 64-512 KB came and went with streaming inside the heap, and the holes
   they left pushed brk up through a long session (hwlog22: 95 -> 104 of
   115 MB from 8 to 16 min, live flat at ~85). Sim heap model (sim/sim_heap.c),
   4 chained races: brk max 89 -> 74 MB, largest hole 1.2 -> 3.6 MB, ~200
   kernel blocks live, read-ahead hits unchanged (72%). */
#define BIGBLK_MIN   (64u * 1024u)
#define BIGBLK_POOL  (125u * 1024u * 1024u)  /* = what the heap gave up (240 -> 115 MB); kept out of vitaGL's reach */
#define BIGBLK_SLOTS 4096
#define BIGBLK_RESERVE (12u * 1024u * 1024u)   /* kernel memory left for thread stacks, modules */                   /* open addressing, power of two */
typedef struct { void *p; SceUID uid; size_t size, req; } BigBlk;
static BigBlk g_bigblk[BIGBLK_SLOTS];
static volatile int g_bigblk_lock;
static size_t g_bigblk_live, g_bigblk_peak; static int g_bigblk_count, g_bigblk_fail;
static void bigblk_lock(void) { while (__sync_lock_test_and_set(&g_bigblk_lock, 1)) sceKernelDelayThread(10); }
static void bigblk_unlock(void) { __sync_lock_release(&g_bigblk_lock); }
static unsigned bigblk_hash(const void *p) { return ((uintptr_t)p >> 12) * 2654435761u >> 20 & (BIGBLK_SLOTS - 1); }
/* caller holds the lock */
static BigBlk *bigblk_find(const void *p) {
    if (!p || ((uintptr_t)p & 0xFFF)) return NULL;   /* blocks are page aligned */
    for (unsigned i = 0, h = bigblk_hash(p); i < BIGBLK_SLOTS; i++, h = (h + 1) & (BIGBLK_SLOTS - 1)) {
        if (g_bigblk[h].p == p) return &g_bigblk[h];
        if (!g_bigblk[h].p && !g_bigblk[h].uid) return NULL;   /* never used: end of chain */
    }
    return NULL;
}
static void *bigblk_alloc(size_t n) {
    size_t size = (n + 0xFFF) & ~(size_t)0xFFF;
    if (size < n) return NULL;
    /* Stay inside the pool: the rest of the kernel's free memory is what
       thread stacks and system modules need. Without this cap decoded world
       tiles took all of it, and the audio thread, the tile read-ahead and
       every streaming build thread failed to start (10-09). The heap takes
       the request instead. */
    {   /* (a fixed 90 MB pool pushed tile data into the heap, which then
           overflowed: the kernel had more to give.) Keep a reserve free. */
        SceKernelFreeMemorySizeInfo fi; memset(&fi, 0, sizeof fi); fi.size = sizeof fi;
        if (sceKernelGetFreeMemorySize(&fi) < 0 || (size_t)fi.size_user < size + BIGBLK_RESERVE) {
            g_bigblk_fail++; return NULL;
        }
    }
    SceUID uid = sceKernelAllocMemBlock("ug2big", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
    if (uid < 0) { g_bigblk_fail++; return NULL; }
    void *p = NULL;
    if (sceKernelGetMemBlockBase(uid, &p) < 0 || !p) { sceKernelFreeMemBlock(uid); return NULL; }
    bigblk_lock();
    unsigned h = bigblk_hash(p), i = 0;
    /* a freed slot keeps uid -1 as a tombstone so chains stay intact */
    while (i < BIGBLK_SLOTS && g_bigblk[h].p) { h = (h + 1) & (BIGBLK_SLOTS - 1); i++; }
    if (i == BIGBLK_SLOTS) { bigblk_unlock(); sceKernelFreeMemBlock(uid); return NULL; }
    g_bigblk[h].p = p; g_bigblk[h].uid = uid; g_bigblk[h].size = size; g_bigblk[h].req = n;
    g_bigblk_live += size; g_bigblk_count++;
    if (g_bigblk_live > g_bigblk_peak) g_bigblk_peak = g_bigblk_live;
    bigblk_unlock();
    return p;
}
/* 1 if p was a big block (now freed) */
static int bigblk_free(void *p) {
    if (!p || ((uintptr_t)p & 0xFFF)) return 0;
    bigblk_lock();
    BigBlk *b = bigblk_find(p);
    if (!b) { bigblk_unlock(); return 0; }
    SceUID uid = b->uid;
    g_bigblk_live -= b->size; g_bigblk_count--;
    b->p = NULL; b->uid = -1; b->size = b->req = 0;
    bigblk_unlock();
    sceKernelFreeMemBlock(uid);
    return 1;
}
/* requested size of a big block, 0 if p is not one */
static size_t bigblk_req(void *p, size_t *cap) {
    if (!p || ((uintptr_t)p & 0xFFF)) return 0;
    bigblk_lock();
    BigBlk *b = bigblk_find(p);
    size_t r = b ? b->req : 0; if (b && cap) *cap = b->size;
    bigblk_unlock();
    return r;
}
size_t vita_bigblk_live(void) { return g_bigblk_live; }
/* kernel memory a big block could still take (free user memory beyond the
   reserve), MB */
unsigned vita_bigblk_room_mb(void) {
    SceKernelFreeMemorySizeInfo fi; memset(&fi, 0, sizeof fi); fi.size = sizeof fi;
    if (sceKernelGetFreeMemorySize(&fi) < 0 || (size_t)fi.size_user <= BIGBLK_RESERVE) return 0;
    return (unsigned)(((size_t)fi.size_user - BIGBLK_RESERVE) >> 20);
}
/* Heap chunks of big-block size: what overflowed into the heap when the
   kernel refused, plus anything else that large there. Each one is a hole
   when freed. Counted by usable size on both sides, so the sum balances. */
static volatile long g_spill_live, g_spill_peak; static volatile int g_spill_count;
static void spill_add(void *p) {
    size_t u = p ? malloc_usable_size(p) : 0;
    if (u < BIGBLK_MIN) return;
    long v = __sync_add_and_fetch(&g_spill_live, (long)u);
    __sync_fetch_and_add(&g_spill_count, 1);
    if (v > g_spill_peak) g_spill_peak = v;
}
static void spill_sub(void *p) {
    size_t u = p ? malloc_usable_size(p) : 0;
    if (u < BIGBLK_MIN) return;
    __sync_fetch_and_sub(&g_spill_live, (long)u);
    __sync_fetch_and_sub(&g_spill_count, 1);
}
void vita_bigblk_report(char *out, size_t cap) {
    snprintf(out, cap, "big blocks %d live, %u MB (peak %u MB), %d kernel refusals; in heap %d, %u MB (peak %u MB)",
             g_bigblk_count, (unsigned)(g_bigblk_live >> 20), (unsigned)(g_bigblk_peak >> 20), g_bigblk_fail,
             g_spill_count, (unsigned)((g_spill_live > 0 ? g_spill_live : 0) >> 20), (unsigned)(g_spill_peak >> 20));
}
static void bigblk_set_req(void *p, size_t n) {
    bigblk_lock();
    BigBlk *b = bigblk_find(p);
    if (b) b->req = n;
    bigblk_unlock();
}
void __real_free(void *p);
#define HEAP_FALLBACK_MIN (16u * 1024u)   /* heap full: a kernel block for this */

/* the frame thread's time in malloc / calloc / realloc / free (waiting on
   the heap lock while a worker holds it, or walking a fragmented heap):
   world_resident.c's hitch report takes it each frame */
static volatile Uint64 g_heap_main_us, g_heap_main_max; static volatile int g_heap_main_calls;
#define HEAP_T0 Uint64 ht0 = sceKernelGetThreadId() == g_main_thid ? sceKernelGetProcessTimeWide() : 0
#define HEAP_T1 if (ht0) { Uint64 d = sceKernelGetProcessTimeWide() - ht0; g_heap_main_us += d; \
                           if (d > g_heap_main_max) g_heap_main_max = d; g_heap_main_calls++; }
void vita_heap_main_take(double *sum_ms, double *max_ms, int *calls) {
    *sum_ms = (double)g_heap_main_us / 1000.0; *max_ms = (double)g_heap_main_max / 1000.0;
    *calls = g_heap_main_calls;
    g_heap_main_us = 0; g_heap_main_max = 0; g_heap_main_calls = 0;
}

void *__wrap_malloc(size_t n) {
    HEAP_T0;
    void *p = n >= BIGBLK_MIN ? bigblk_alloc(n) : NULL;
    if (!p && (p = __real_malloc(n)) && n >= BIGBLK_MIN) spill_add(p);
    if (!p && n >= HEAP_FALLBACK_MIN) p = bigblk_alloc(n);
    HEAP_T1
    if (!p && n) vita_oom("malloc", n, __builtin_return_address(0));
    else if (n >= BIG_ALLOC) vita_big("malloc", n, __builtin_return_address(0));
    return p;
}
void *__wrap_calloc(size_t c, size_t n) {
    if (c && n > (size_t)-1 / c) return NULL;
    size_t t = c * n;
    void *p = t >= BIGBLK_MIN ? bigblk_alloc(t) : NULL;
    if (p) memset(p, 0, t);
    else {
        p = __real_calloc(c, n);
        if (p && t >= BIGBLK_MIN) spill_add(p);
        if (!p && t >= HEAP_FALLBACK_MIN && (p = bigblk_alloc(t))) memset(p, 0, t);
    }
    if (!p && t) vita_oom("calloc", t, __builtin_return_address(0));
    else if (t >= BIG_ALLOC) vita_big("calloc", t, __builtin_return_address(0));
    return p;
}
void __wrap_free(void *p) {
    if (!p) return;
    HEAP_T0;
    if (!bigblk_free(p)) { spill_sub(p); __real_free(p); }
    HEAP_T1
}
void *__wrap_realloc(void *q, size_t n) {
    if (!q) return __wrap_malloc(n);
    if (!n) { __wrap_free(q); return NULL; }
    size_t cap = 0, have = bigblk_req(q, &cap);
    void *p;
    if (have) {                                   /* q is a kernel block */
        if (n <= cap) { bigblk_set_req(q, n); return q; }
        p = __wrap_malloc(n);
        if (p) { memcpy(p, q, have < n ? have : n); bigblk_free(q); }
        return p;
    }
    size_t usable = malloc_usable_size(q);
    if (n > usable && n >= BIGBLK_MIN && (p = bigblk_alloc(n))) {   /* growing out of the heap */
        size_t old = usable;
        memcpy(p, q, old < n ? old : n);
        spill_sub(q); __real_free(q);
        return p;
    }
    spill_sub(q);
    p = __real_realloc(q, n);
    if (p) spill_add(p);
    else {
        spill_add(q);                             /* q still stands */
        if (n >= HEAP_FALLBACK_MIN && (p = bigblk_alloc(n))) {
            size_t old = malloc_usable_size(q);
            memcpy(p, q, old < n ? old : n);
            spill_sub(q); __real_free(q);
        }
    }
    if (!p) vita_oom("realloc", n, __builtin_return_address(0));
    else if (n >= BIG_ALLOC) vita_big("realloc", n, __builtin_return_address(0));
    return p;
}

const char *SDL_GetError(void) { return "vita"; }

int SDL_Init(Uint32 flags) {
    (void)flags;
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    vita_mark("clocks: cpu %d MHz, bus %d, gpu %d, xbar %d",
              scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
              scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    boot_queue_start();
    async_log_start();
    extern int vgl_ug2_build __attribute__((weak));
    vita_mark("boot logging: %s; log.txt %s (player-merge+finshed build, vitaGL %s)",
              g_boot_thread>=0?"background queue":"synchronous fallback",
              g_async_thread>=0?"background queue":"synchronous",
              &vgl_ug2_build ? "patched layout-cache" : "stock");
    return 0;
}

void SDL_Quit(void) { fflush(stdout); }

/* ---- time ----------------------------------------------------------------- */
Uint32 SDL_GetTicks(void) { return (Uint32)(sceKernelGetProcessTimeWide() / 1000); }
Uint64 SDL_GetPerformanceCounter(void) { return (Uint64)sceKernelGetProcessTimeWide(); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000ull; }
void SDL_Delay(Uint32 ms) { sceKernelDelayThread(ms * 1000); }

/* ---- window / GL ---------------------------------------------------------- */
struct SDL_Window { int w, h; };
static struct SDL_Window g_window = { VITA_W, VITA_H };
/* Display framebuffer size. 720x408 is a native PS Vita display mode: the
   display hardware upscales it to the panel for free, so the 3D renders
   straight into it with no offscreen pass and no full-screen upscale draw.
   ux0:data/NFSU2/display960.txt keeps the old 960x544 framebuffer (3D in
   a 720x408 texture blitted up, HUD at 960x544). */
static int g_disp_w = VITA_W, g_disp_h = VITA_H;

int SDL_GL_SetAttribute(SDL_GLattr attr, int value) { (void)attr; (void)value; return 0; }

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)flags;
    return &g_window;               /* the Vita screen is always 960x544 */
}
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }

/* ---- Trophies (src/trophy.c decides, this reports) ----------------------
   The homebrew route (Rinnegatamante's NoTrpDrm + an unsigned trophy pack,
   as in his ports): sce_sys/trophy/NFSU20001_00/TROPHY.TRP built by
   tools/trophy_trp.py, communication id = the title id, signature checks
   switched off by the NoTrpDrm plugin. Without the plugin the context is
   refused and trophies stay log-only. Unlocks block, so a thread does them
   from a small queue. Trophy pack ids: 0 = platinum, our i = i + 1. */
#include <psp2/common_dialog.h>
#include <psp2/sysmodule.h>
#include "../src/trophy.h"
typedef struct { int sdkVersion; SceCommonDialogParam commonParam; int context; int options; uint8_t reserved[128]; } VtSetupParam;
typedef struct { uint32_t bit[4]; } VtUnlockState;
int sceNpTrophyInit(void *unk);
int sceNpTrophyCreateContext(int *context, char *commId, char *commSign, uint64_t options);
int sceNpTrophyCreateHandle(int *handle);
int sceNpTrophyDestroyHandle(int handle);
int sceNpTrophyUnlockTrophy(int ctx, int handle, int id, int *plat_id);
int sceNpTrophyGetTrophyUnlockState(int ctx, int handle, VtUnlockState *state, uint32_t *count);
int sceNpTrophySetupDialogInit(VtSetupParam *param);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
int sceNpTrophySetupDialogTerm(void);
static int vt_ctx;
static int vt_skip;                   /* the Settings screen's own context: no trophies there */
static VtUnlockState vt_state;          /* what the system has; main thread marks queued ones */
static SceUID vt_sema = -1, vt_lock = -1;
static int vt_q[128];
static unsigned vt_qh, vt_qt;          /* head (thread) / tail (main) */
static int vt_thread(SceSize a, void *p) {
    (void)a; (void)p;
    for (;;) {
        sceKernelWaitSema(vt_sema, 1, NULL);
        sceKernelLockMutex(vt_lock, 1, NULL);
        int id = vt_q[vt_qh++ & 127];
        sceKernelUnlockMutex(vt_lock, 1);
        int h = -1, plat = -1;
        int r = sceNpTrophyCreateHandle(&h);
        if (r >= 0) { r = sceNpTrophyUnlockTrophy(vt_ctx, h, id, &plat); sceNpTrophyDestroyHandle(h); }
        printf("trophy: system unlock %d -> 0x%08X%s\n", id, (unsigned)r, plat >= 0 ? " (platinum)" : "");
    }
    return 0;
}
static void vt_unlock(int i) {
    int id = i + 1;
    if (id <= 0 || id >= 128 || (vt_state.bit[id >> 5] >> (id & 31) & 1)) return;   /* already on the system */
    vt_state.bit[id >> 5] |= 1u << (id & 31);
    sceKernelLockMutex(vt_lock, 1, NULL);
    if (vt_qt - vt_qh < 128) { vt_q[vt_qt++ & 127] = id; sceKernelUnlockMutex(vt_lock, 1); sceKernelSignalSema(vt_sema, 1); }
    else sceKernelUnlockMutex(vt_lock, 1);
}
/* after vglInit: the setup dialog (installs the pack the first time) needs swaps */
static void vita_trophy_setup(void) {
    if (vt_skip) return;
    SceUID fd = sceIoOpen(TROPHY_ON_FILE, SCE_O_RDONLY, 0);   /* opt-in: off by default */
    if (fd < 0) { printf("trophy: system off (no trophies.txt)\n"); return; }
    sceIoClose(fd);
    char comm_id[12] = {0};
    static char sign[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};
    sceAppMgrAppParamGetString(0, 12, comm_id, sizeof comm_id);   /* the title id */
    int r = sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    if (r >= 0) r = sceNpTrophyInit(NULL);
    if (r >= 0) r = sceNpTrophyCreateContext(&vt_ctx, comm_id, sign, 0);
    if (r < 0) { printf("trophy: system unavailable (0x%08X; is NoTrpDrm in ux0:tai/config.txt?), log only\n", (unsigned)r); return; }
    VtSetupParam sp;
    memset(&sp, 0, sizeof sp);
    _sceCommonDialogSetMagicNumber(&sp.commonParam);
    sp.sdkVersion = PSP2_SDK_VERSION;
    sp.context = vt_ctx;
    r = sceNpTrophySetupDialogInit(&sp);
    if (r >= 0) {
        while (sceNpTrophySetupDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_TRUE);
        }
        sceNpTrophySetupDialogTerm();
    }
    int h = -1; uint32_t cnt = 0;
    if (sceNpTrophyCreateHandle(&h) >= 0) { sceNpTrophyGetTrophyUnlockState(vt_ctx, h, &vt_state, &cnt); sceNpTrophyDestroyHandle(h); }
    vt_sema = sceKernelCreateSema("trophy_q", 0, 0, 128, NULL);
    vt_lock = sceKernelCreateMutex("trophy_m", 0, 0, NULL);
    SceUID th = sceKernelCreateThread("trophy_unlock", vt_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (vt_sema < 0 || vt_lock < 0 || th < 0) { printf("trophy: thread setup failed, log only\n"); return; }
    sceKernelStartThread(th, 0, NULL);
    trophy_backend = vt_unlock;
    printf("trophy: system ready (%s, setup 0x%08X, %u on the system)\n", comm_id, (unsigned)r, (unsigned)cnt);
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *w) {
    (void)w;
    if (!g_gl_ready) {
        vglSetupRuntimeShaderCompiler(SHARK_OPT_FAST, GL_TRUE, GL_FALSE, GL_TRUE);
        /* The default POSTPONED mode reports every GLSL shader as compiled and
           only compiles inside glLinkProgram, where a failure crashes vitaGL.
           The engine always compiles VS then FS then links, which is exactly
           SHADER_PAIR's premise, and it surfaces errors at glCompileShader. */
        vglSetSemanticBindingMode(VGL_MODE_SHADER_PAIR);
        /* Small legacy pool (the engine is shader-only); keep 16 MB of RAM free.
           vglInit* does not report success: GL_TRUE only means the display
           fell back to a smaller resolution than requested. */
        {   SceUID fd = sceIoOpen("ux0:data/NFSU2/display960.txt", SCE_O_RDONLY, 0);
            if (fd >= 0) sceIoClose(fd);
            else { g_disp_w = 720; g_disp_h = 408; } }
        /* leave the big-block pool (less what is already taken) to the kernel */
        /* lowlat.txt: double buffering. Triple buffering lets one finished
           frame wait in the display queue behind the one on screen (the CPU
           blocks ~10 ms a frame in the queue), adding up to a frame of input
           lag; with two buffers the GPU has to finish inside each frame. */
        {   SceUID fd = sceIoOpen("ux0:data/NFSU2/lowlat.txt", SCE_O_RDONLY, 0);
            if (fd >= 0) { sceIoClose(fd); vglUseTripleBuffering(GL_FALSE); }
            printf("display: %s" "%c", fd >= 0 ? "double buffered, low latency (lowlat.txt)"
                                                    : "triple buffered", 10); }
        size_t bigleft = vita_bigblk_live() < BIGBLK_POOL ? BIGBLK_POOL - vita_bigblk_live() : 0;
        if (vglInitExtended(0x100000, g_disp_w, g_disp_h, 16 * 1024 * 1024 + (int)bigleft, SCE_GXM_MULTISAMPLE_NONE))
            printf("vitaGL: display fell back from %dx%d\n", g_disp_w, g_disp_h);
        printf("display: %dx%d framebuffer%s\n", g_disp_w, g_disp_h,
               g_disp_w < VITA_W ? " (hardware upscaled, no blit pass)" : " (3D blitted up from 720x408)");
        /* No vblank wait: at 10-20 fps a missed refresh costs a whole extra
           16.7 ms, which measured frames were paying (83/100/117 ms steps). */
        vglWaitVblankStart(GL_FALSE);
        shark_install_log_cb(shark_cb);
        shark_set_warnings_level(SHARK_WARN_HIGH);
        g_gl_ready = 1;
        vita_trophy_setup();
        printf("vitaGL: ram %u KB free, cdram %u KB free\n",
               (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024));
    }
    return &g_window;
}
void SDL_GL_DeleteContext(SDL_GLContext c) { (void)c; }
void SDL_GL_GetDrawableSize(SDL_Window *w, int *dw, int *dh) { (void)w; *dw = g_disp_w; *dh = g_disp_h; }
void SDL_GetWindowSize(SDL_Window *w, int *ww, int *wh) { (void)w; *ww = g_disp_w; *wh = g_disp_h; }
/* Locked 30 (default; fps60.txt in ux0:data/NFSU2 turns it off): the display
   thread holds each frame until two vblanks have passed since the last one
   went out, so every frame stays on screen for exactly 2 refreshes. vitaGL
   runs this callback on its display-queue thread just before the flip; the
   GPU and CPU queue up behind it. */
int vita_lock30 = -1;
static unsigned lock30_vc;
static void lock30_cb(void *fb) {
    (void)fb;
    while ((int)(sceDisplayGetVcount() - lock30_vc) < 2) sceDisplayWaitVblankStart();
    unsigned vc = sceDisplayGetVcount();
    /* proof: how many refreshes the previous frame stayed up (2 = on time) */
    static unsigned held[4], n;
    unsigned d = vc - lock30_vc;
    held[d >= 5 ? 3 : d < 2 ? 0 : d - 2]++;
    lock30_vc = vc;
    if (++n == 300) {
        printf("frame pacing over 300 frames: 2 refreshes %u, 3 %u, 4 %u, 5+ %u\n",
               held[0], held[1], held[2], held[3]);
        memset(held, 0, sizeof held); n = 0;
    }
}
int SDL_GL_SetSwapInterval(int interval) {
    if (vita_lock30 < 0) {
        SceUID fd = sceIoOpen("ux0:data/NFSU2/fps60.txt", SCE_O_RDONLY, 0);
        vita_lock30 = fd < 0;
        if (fd >= 0) sceIoClose(fd);
        printf("frame rate: %s\n", vita_lock30 ? "locked 30 (fps60.txt unlocks)" : "unlocked (fps60.txt)");
    }
    vglSetDisplayCallback(interval && vita_lock30 ? lock30_cb : NULL);
    vglWaitVblankStart(interval ? GL_TRUE : GL_FALSE);
    return 0;
}
/* Lap timer for profiling one stretch of the frame: vita_lap(0) starts it,
   vita_lap(n) adds the time since the previous lap to slot n. Averages are
   logged every 60 frames from SDL_GL_SwapWindow. */
/* vitaGL internals: the per-frame circular pool for draw-time data. Draws
   that overrun it fall back to one mapped allocation each (slow). */
extern uint8_t *circular_data_pool[], *circular_data_pool_ptr[], *circular_data_pool_limit[];
extern int vgl_circular_idx;
static unsigned g_pool_peak_kb, g_pool_size_kb;

int vita_gfx_mode;
static Uint64 g_mode_t0;
static unsigned g_mode_frames;
#define LAP_SLOTS 16
static Uint64 g_lap_last, g_lap_acc[LAP_SLOTS];

void vita_lap(int slot) {
    if (g_main_thid >= 0 && sceKernelGetThreadId() != g_main_thid) return;   /* AI workers */
    Uint64 now = sceKernelGetProcessTimeWide();
    if (slot > 0 && slot < LAP_SLOTS) g_lap_acc[slot] += now - g_lap_last;
    g_lap_last = now;
}

enum { GXW_END, GXW_BEGIN, GXW_QUEUE, GXW_NOTIFY, GXW_FINISH, GXW_SWAP, GXW_N };
typedef struct { uint32_t t, busy, lat, scenes, wait[GXW_N], busy_s[2]; } GxSnap;
static int gx_on;
static volatile uint32_t gx_wait_us[GXW_N];
extern int vita_gfx_mode;
static void gx_delta(GxSnap *s, double *busy, double *idle, double *lat, unsigned *scenes,
                     double wait[GXW_N], double split[2]);
float vita_audio_pct;   /* mixer share of a core, refreshed every lap report */
static void lap_report(unsigned frames) {
    char line[512];
    int len = snprintf(line, sizeof line, "vita: lap ms/frame over %u frames:", frames);
    for (int i = 1; i < LAP_SLOTS && len < (int)sizeof line - 16; i++)
        if (g_lap_acc[i])
            len += snprintf(line + len, sizeof line - (size_t)len, " %d=%.1f", i,
                            (double)g_lap_acc[i] / 1000.0 / frames);
    { extern unsigned vita_variant_switches(void);
      len += snprintf(line + len, sizeof line - (size_t)len, " | shader switches %.0f/frame",
                      (double)vita_variant_switches() / frames); }
    printf("%s | draw pool peak %u of %u KB\n", line, g_pool_peak_kb, g_pool_size_kb);
    if (gx_on) {
        static GxSnap snap;
        double busy, idle, lat, wt[GXW_N], sp[2]; unsigned n;
        gx_delta(&snap, &busy, &idle, &lat, &n, wt, sp);
        printf("gx ms/frame: gpu busy %.1f [3d %.1f, display+hud %.1f] idle %.1f (%.1f scenes, done %.1f ms after submit) |"
               " cpu blocked: endscene %.1f beginscene %.1f queue %.1f notify %.1f finish %.1f swap-other %.1f\n",
               busy / frames, sp[0] / frames, sp[1] / frames, idle / frames, (double)n / frames, lat, wt[GXW_END] / frames,
               wt[GXW_BEGIN] / frames, wt[GXW_QUEUE] / frames, wt[GXW_NOTIFY] / frames,
               wt[GXW_FINISH] / frames, wt[GXW_SWAP] / frames);
    }
    {   extern float audio_cost_pct(void);
        vita_audio_pct = audio_cost_pct();
        printf("audio mixer: %.1f%% of a core\n", vita_audio_pct);
    }
    g_pool_peak_kb = 0;
    memset(g_lap_acc, 0, sizeof g_lap_acc);
}

/* ---- GPU timeline for perf.csv (vita_gx_*) ---------------------------------
   vitaGL has no timer queries, but it ends every scene with a fragment
   notification (its query fence, value +1 per scene). The GXM calls are
   --wrap'd at link time: sceGxmEndScene records the fence value and the time
   the scene was submitted; a watcher thread polls the fence and timestamps the
   moment the GPU finished it. Scenes run in order on the GPU, so a scene is
   busy from max(submit, previous scene done) to its own done time; the rest of
   the wall time is GPU idle. The other wraps time how long the CPU blocks in
   each call. Counters are 32-bit microseconds and only ever differenced. */
typedef struct { volatile uint32_t *addr; uint32_t value, t_submit, display; } GxScene;
#define GX_RING 64
static GxScene gx_ring[GX_RING];
static volatile unsigned gx_head, gx_tail;
static volatile uint32_t gx_busy_us, gx_lat_us, gx_scenes, gx_busy_s[2];
static int gx_in_swap;   /* the scene ended inside vglSwapBuffers is the display one */

static int gx_watch(SceSize args, void *argp) {
    (void)args; (void)argp;
    uint32_t prev_done = sceKernelGetProcessTimeLow();
    for (;;) {
        if (gx_tail == gx_head) { sceKernelDelayThread(250); continue; }
        GxScene s = gx_ring[gx_tail % GX_RING];
        while ((int32_t)(*s.addr - s.value) < 0) sceKernelDelayThread(100);
        uint32_t done = sceKernelGetProcessTimeLow();
        uint32_t start = (int32_t)(s.t_submit - prev_done) > 0 ? s.t_submit : prev_done;
        gx_busy_us += done - start;
        gx_busy_s[s.display] += done - start;
        gx_lat_us += done - s.t_submit;
        gx_scenes++;
        prev_done = done;
        __sync_synchronize();
        gx_tail++;
    }
    return 0;
}

/* the GPU's busy microseconds so far (32-bit, differenced by the caller), 0 while the watcher is off */
unsigned vita_gx_busy_total(void) { return gx_on ? gx_busy_us : 0; }

void vita_gx_start(void) {
    if (gx_on) return;
    {   /* unattended runs: ux0:data/NFSU2/gfxmode.txt "<n>" starts in A/B mode n
           (1 = the GPU probe), as Select+Triangle would step to it */
        FILE *gm = fopen("ux0:data/NFSU2/gfxmode.txt", "r");
        int m = 0;
        if (gm) {
            if (fscanf(gm, "%d", &m) == 1 && m > 0 && m < 4) {   /* GFX_MODES (defined below) */
                vita_gfx_mode = m;
                printf("vita: gfx mode %d from gfxmode.txt\n", m);
            }
            fclose(gm);
        }
    }
    /* High priority so completion timestamps are not delayed behind the
       streaming worker; it sleeps between 100-250 us polls. */
    SceUID uid = sceKernelCreateThread("gx-watch", gx_watch, 100, 0x4000, 0,
                                       SCE_KERNEL_CPU_MASK_USER_2, NULL);
    if (uid < 0) { printf("gx: watcher thread failed 0x%08X\n", uid); return; }
    sceKernelStartThread(uid, 0, NULL);
    gx_on = 1;
}

/* Per-interval figures since the previous call with the same snapshot. */
static void gx_delta(GxSnap *s, double *busy, double *idle, double *lat, unsigned *scenes,
                     double wait[GXW_N], double split[2]) {
    for (int i = 0; i < 2; i++) { uint32_t b = gx_busy_s[i]; split[i] = (b - s->busy_s[i]) / 1000.0; s->busy_s[i] = b; }
    uint32_t t = sceKernelGetProcessTimeLow(), b = gx_busy_us, l = gx_lat_us, n = gx_scenes;
    double wall = (t - s->t) / 1000.0;
    *busy = (b - s->busy) / 1000.0;
    *idle = wall > *busy ? wall - *busy : 0.0;
    *scenes = n - s->scenes;
    *lat = *scenes ? (l - s->lat) / 1000.0 / *scenes : 0.0;
    for (int i = 0; i < GXW_N; i++) { uint32_t w = gx_wait_us[i]; wait[i] = (w - s->wait[i]) / 1000.0; s->wait[i] = w; }
    s->t = t; s->busy = b; s->lat = l; s->scenes = n;
}

/* ---- GPU probe (gfx mode 1) ------------------------------------------------
   Switches one 3D pass off at a time (main.c tests vita_probe_skip) and
   measures the 3D scene's GPU time with and without it. Phases of
   PROBE_PHASE frames alternate baseline / skip k, so every skip is compared
   with the baselines either side of it (the view keeps changing while
   driving). The first PROBE_SETTLE frames of each phase are not measured:
   the GPU finishes a frame ~2-3 frames after it is submitted. */
int vita_probe_skip;
#define PROBE_PASSES 12
#define PROBE_PHASE 30
#define PROBE_SETTLE 8
static const char *const g_probe_name[PROBE_PASSES] = {
    "base", "sky", "world opaque", "world cutout", "world blend+lights",
    "all cars", "AI cars", "shadow+skid+smoke", "road reflection",
    "bloom chain", "HUD", "car env map" };
static double g_probe_ms[PROBE_PASSES], g_probe_base_ms[PROBE_PASSES];
static unsigned g_probe_n[PROBE_PASSES], g_probe_base_n[PROBE_PASSES];
static void vita_probe_frame(void) {
    static unsigned phase, frame, cycles;
    static uint32_t mark;
    static double last_base = -1;   /* ms/frame of the previous baseline phase */
    if (vita_gfx_mode != 1 || !gx_on) {
        if (vita_probe_skip || phase || frame) {
            vita_probe_skip = 0; phase = frame = cycles = 0; last_base = -1;
            memset(g_probe_ms, 0, sizeof g_probe_ms); memset(g_probe_n, 0, sizeof g_probe_n);
            memset(g_probe_base_ms, 0, sizeof g_probe_base_ms);
            memset(g_probe_base_n, 0, sizeof g_probe_base_n);
        }
        return;
    }
    /* phase 2j = baseline, 2j+1 = skip pass j+1 */
    int k = (phase & 1) ? (int)(phase / 2) + 1 : 0;
    if (frame == PROBE_SETTLE) mark = gx_busy_us;   /* whole frame: one GPU scene since the 720x408 display */
    if (++frame == PROBE_PHASE) {
        double ms = (gx_busy_us - mark) / 1000.0 / (PROBE_PHASE - PROBE_SETTLE);
        if (k == 0) {
            /* this baseline pairs with the skip before it and the one after */
            if (phase > 0) { g_probe_base_ms[phase / 2] += ms; g_probe_base_n[phase / 2]++; }
            last_base = ms;
            g_probe_ms[0] += ms; g_probe_n[0]++;
        } else {
            g_probe_ms[k] += ms; g_probe_n[k]++;
            if (last_base >= 0) { g_probe_base_ms[k] += last_base; g_probe_base_n[k]++; }
        }
        frame = 0;
        if (++phase == 2 * (PROBE_PASSES - 1) + 1) {   /* ends on a baseline */
            phase = 0; cycles++;
            char line[640];
            int len = snprintf(line, sizeof line, "gpu probe (%u cycles, gpu ms/frame): base %.1f",
                               cycles, g_probe_n[0] ? g_probe_ms[0] / g_probe_n[0] : 0.0);
            for (int j = 1; j < PROBE_PASSES && len < (int)sizeof line - 48; j++) {
                if (!g_probe_n[j] || !g_probe_base_n[j]) continue;
                double save = g_probe_base_ms[j] / g_probe_base_n[j] - g_probe_ms[j] / g_probe_n[j];
                len += snprintf(line + len, sizeof line - (size_t)len, " | %s %.1f",
                                g_probe_name[j], save);
            }
            printf("%s\n", line);
        }
        k = (phase & 1) ? (int)(phase / 2) + 1 : 0;
    }
    vita_probe_skip = k;
}

/* perf.csv columns, one row per frame (see pf_open in main.c). */
const char *vita_gx_header(void) {
    return ",gx_busy,gx_idle,gx_scenes,gx_latency,w_endscene,w_beginscene,w_queue,"
           "w_notifywait,w_finish,w_swap_other,gx_3d,gx_display,gfx_mode";
}
void vita_gx_row(FILE *f) {
    static GxSnap snap;
    double busy, idle, lat, w[GXW_N], sp[2]; unsigned n;
    gx_delta(&snap, &busy, &idle, &lat, &n, w, sp);
    if (!gx_on) { fprintf(f, ",,,,,,,,,,,,%d", vita_gfx_mode); return; }
    fprintf(f, ",%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d", busy, idle, n, lat,
            w[GXW_END], w[GXW_BEGIN], w[GXW_QUEUE], w[GXW_NOTIFY], w[GXW_FINISH], w[GXW_SWAP],
            sp[0], sp[1], vita_gfx_mode);
}

#define GX_TIMED(slot, call) \
    uint32_t t0 = sceKernelGetProcessTimeLow(); int r = call; \
    if (gx_on) gx_wait_us[slot] += sceKernelGetProcessTimeLow() - t0;

int __real_sceGxmEndScene(SceGxmContext *, const SceGxmNotification *, const SceGxmNotification *);
int __wrap_sceGxmEndScene(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) {
    GX_TIMED(GXW_END, __real_sceGxmEndScene(c, v, f))
    if (gx_on && f && f->address) {
        unsigned h = gx_head;
        if (h - gx_tail < GX_RING) {   /* full: drop it, the next scene's done time still bounds it */
            gx_ring[h % GX_RING] = (GxScene){ f->address, f->value, sceKernelGetProcessTimeLow(), gx_in_swap ? 1u : 0u };
            __sync_synchronize();
            gx_head = h + 1;
        }
    }
    return r;
}
int __real_sceGxmBeginScene(SceGxmContext *, unsigned int, const SceGxmRenderTarget *,
                            const SceGxmValidRegion *, SceGxmSyncObject *, SceGxmSyncObject *,
                            const SceGxmColorSurface *, const SceGxmDepthStencilSurface *);
int __wrap_sceGxmBeginScene(SceGxmContext *c, unsigned int fl, const SceGxmRenderTarget *rt,
                            const SceGxmValidRegion *vr, SceGxmSyncObject *vs, SceGxmSyncObject *fs,
                            const SceGxmColorSurface *cs, const SceGxmDepthStencilSurface *ds) {
    GX_TIMED(GXW_BEGIN, __real_sceGxmBeginScene(c, fl, rt, vr, vs, fs, cs, ds))
    return r;
}
int __real_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *, SceGxmSyncObject *, const void *);
int __wrap_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *o, SceGxmSyncObject *n, const void *d) {
    GX_TIMED(GXW_QUEUE, __real_sceGxmDisplayQueueAddEntry(o, n, d))
    return r;
}
int __real_sceGxmNotificationWait(const SceGxmNotification *);
int __wrap_sceGxmNotificationWait(const SceGxmNotification *n) {
    GX_TIMED(GXW_NOTIFY, __real_sceGxmNotificationWait(n))
    return r;
}
int __real_sceGxmFinish(SceGxmContext *);
int __wrap_sceGxmFinish(SceGxmContext *c) {
    GX_TIMED(GXW_FINISH, __real_sceGxmFinish(c))
    return r;
}

void SDL_GL_SwapWindow(SDL_Window *w) {
    (void)w;
    {   int ci = vgl_circular_idx;
        unsigned used = (unsigned)(circular_data_pool_ptr[ci] - circular_data_pool[ci]) >> 10;
        g_pool_size_kb = (unsigned)(circular_data_pool_limit[ci] - circular_data_pool[ci]) >> 10;
        if (used > g_pool_peak_kb) g_pool_peak_kb = used; }
    {   uint32_t t0 = sceKernelGetProcessTimeLow(), e0 = gx_wait_us[GXW_END], q0 = gx_wait_us[GXW_QUEUE];
        gx_in_swap = 1;
        vglSwapBuffers(GL_FALSE);
        gx_in_swap = 0;
        if (gx_on) gx_wait_us[GXW_SWAP] += (sceKernelGetProcessTimeLow() - t0)
                                         - (gx_wait_us[GXW_END] - e0) - (gx_wait_us[GXW_QUEUE] - q0); }
    if (!g_mode_t0) g_mode_t0 = sceKernelGetProcessTimeWide();
    g_mode_frames++;
    vita_probe_frame();
    static unsigned lap_frames;
    if (++lap_frames == 60) { lap_report(lap_frames); lap_frames = 0; }
    static unsigned frames;
    static Uint64 t0;
    if (!frames) t0 = sceKernelGetProcessTimeWide();
    frames++;
    if (frames == 1 || frames == 10 || frames == 60 || frames % 600 == 0) {
        double secs = (double)(sceKernelGetProcessTimeWide() - t0) / 1e6;
        if (frames <= 60)   /* vglMemFree is slow once VRAM fragments: startup only */
            printf("vita: frame %u (%.1f fps avg), heap break %u MB, vram free %u KB\n", frames,
                   secs > 0 ? frames / secs : 0.0, vita_heap_top_mb(),
                   (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 10));
        else
            printf("vita: frame %u (%.1f fps avg), heap break %u MB\n", frames,
                   secs > 0 ? frames / secs : 0.0, vita_heap_top_mb());
        vita_log_flush();
    }
}
void *SDL_GL_GetProcAddress(const char *name) { return vglGetProcAddress(name); }

#include "vu_shim.inc"   /* GL shims (see SDL_opengles2.h) */

/* ---- input ---------------------------------------------------------------- */
#define EVQ 64
static SDL_Event evq[EVQ];
static int evq_head, evq_tail;
static Uint8 g_keys[SDL_NUM_SCANCODES];
static uint32_t g_prev_buttons;
#define GFX_MODES 4
static const char *const g_mode_name[GFX_MODES] = {
    "normal", "GPU probe (passes switched off in turn, flickers)",
    "old world cutout shader (A/B against normal)",
    "show the road reflection target (top left)" };
static float g_steer;
/* 0: the original's pad layout, 1: R gas / L brake (options_ctl_preset).
   The garage CONTROLS row applies it as a preset to the controller setup's
   bindings (options.h), which are what the pad reads. */
int vita_ctrl_scheme = 0;
/* bits the pad never reports, for the left stick past half and the rear
   touchpad's corner zones (options_touch_zone) */
#define PB_LS_LEFT  0x01000000u
#define PB_LS_RIGHT 0x02000000u
#define PB_RT_UL    0x04000000u
#define PB_RT_UR    0x08000000u
#define PB_RT_LL    0x10000000u
#define PB_RT_LR    0x20000000u
/* VB_* -> pad bits */
static uint32_t vb_mask(int vb) {
    switch (vb) {
    case VB_CROSS: return SCE_CTRL_CROSS;     case VB_CIRCLE: return SCE_CTRL_CIRCLE;
    case VB_SQUARE: return SCE_CTRL_SQUARE;   case VB_TRIANGLE: return SCE_CTRL_TRIANGLE;
    case VB_L: return SCE_CTRL_LTRIGGER | SCE_CTRL_L1;
    case VB_R: return SCE_CTRL_RTRIGGER | SCE_CTRL_R1;
    case VB_UP: return SCE_CTRL_UP;           case VB_DOWN: return SCE_CTRL_DOWN;
    case VB_LEFT: return SCE_CTRL_LEFT;       case VB_RIGHT: return SCE_CTRL_RIGHT;
    case VB_START: return SCE_CTRL_START;     case VB_SELECT: return SCE_CTRL_SELECT;
    case VB_LS_LEFT: return PB_LS_LEFT;       case VB_LS_RIGHT: return PB_LS_RIGHT;
    case VB_RT_UL: return PB_RT_UL;           case VB_RT_UR: return PB_RT_UR;
    case VB_RT_LL: return PB_RT_LL;           case VB_RT_LR: return PB_RT_LR;
    default: return 0;
    }
}
static int act_down(uint32_t b, int act) {
    return (b & vb_mask(g_opt.ctl[act][0])) || (b & vb_mask(g_opt.ctl[act][1]));
}
/* buttons newly pressed since the last take (the controller screen's capture) */
static uint32_t g_new_buttons;
int vita_take_new_button(void) {
    uint32_t n = g_new_buttons;
    g_new_buttons = 0;
    for (int vb = 1; vb < VB_COUNT; vb++) if (n & vb_mask(vb)) return vb;
    return 0;
}

int SDL_PushEvent(SDL_Event *e) {
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
    SDL_PushEvent(&e);
}

static void pad_button(uint32_t now, uint32_t vita, SDL_GameControllerButton b) {
    if ((now & vita) && !(g_prev_buttons & vita)) {
        SDL_Event e; memset(&e, 0, sizeof e);
        e.type = SDL_CONTROLLERBUTTONDOWN; e.cbutton.button = (Uint8)b; e.cbutton.state = SDL_PRESSED;
        SDL_PushEvent(&e);
    }
}

float vita_rstick_x, vita_rstick_y, vita_lstick_y;
/* the rear touchpad's zone bits. A finger counts once it has stayed in its
   zone for two samples, so a brush while holding the Vita does nothing */
static uint32_t rear_touch_bits(void) {
    static int init;
    static float x0, y0, xw = 1.0f, yh = 1.0f;
    static uint32_t last;
    if (!g_opt.rear_touch) { last = 0; return 0; }
    if (!init) {
        init = 1;
        sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
        SceTouchPanelInfo pi; memset(&pi, 0, sizeof pi);
        if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &pi) >= 0 && pi.maxAaX > pi.minAaX && pi.maxAaY > pi.minAaY) {
            x0 = pi.minAaX; y0 = pi.minAaY; xw = (float)(pi.maxAaX - pi.minAaX); yh = (float)(pi.maxAaY - pi.minAaY);
        } else { x0 = 0; y0 = 108; xw = 1919; yh = 781; }   /* the rear panel's usual active area */
    }
    SceTouchData td; memset(&td, 0, sizeof td);
    uint32_t now = 0;
    if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &td, 1) >= 0)
        for (uint32_t i = 0; i < td.reportNum && i < SCE_TOUCH_MAX_REPORT; i++)
            now |= vb_mask(options_touch_zone(&g_opt, (td.report[i].x - x0) / xw, (td.report[i].y - y0) / yh));
    uint32_t held = now & last;
    last = now;
    return held;
}

static void poll_pad(void) {
    SceCtrlData pad; memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t b = pad.buttons & 0x00ffffffu;
    const uint32_t L = SCE_CTRL_LTRIGGER | SCE_CTRL_L1, R = SCE_CTRL_RTRIGGER | SCE_CTRL_R1;

    {   /* right stick, -1..1 with a dead zone (showroom turntable / camera) */
        int rx = (int)pad.rx - 128, ry = (int)pad.ry - 128;
        vita_rstick_x = (rx > 24 || rx < -24) ? (float)rx / 127.0f : 0.0f;
        vita_rstick_y = (ry > 24 || ry < -24) ? (float)ry / 127.0f : 0.0f;
        int ly = (int)pad.ly - 128;
        vita_lstick_y = (ly > 24 || ly < -24) ? (float)ly / 127.0f : 0.0f;
    }
    int lx = (int)pad.lx - 128;
    /* +1 = left, like A; the dead zone is cut out of the range, so the
       wheel starts from zero at its edge instead of jumping to 0.19 */
    g_steer = lx > 24 ? -(float)(lx - 24) / 103.0f : lx < -24 ? -(float)(lx + 24) / 103.0f : 0.0f;
    if (g_steer > 1.0f) g_steer = 1.0f;
    if (g_steer < -1.0f) g_steer = -1.0f;
    if (lx < -64) b |= PB_LS_LEFT;
    if (lx > 64) b |= PB_LS_RIGHT;
    b |= rear_touch_bits();

    int quit = (b & SCE_CTRL_SELECT) && (b & SCE_CTRL_START);
    g_new_buttons |= b & ~g_prev_buttons;
    /* the game's actions through the controller setup's bindings: W gas,
       S brake/reverse, E / Q shift, A / D steer (the left stick always),
       Space handbrake, N nitrous, C camera, B look back, keypad 7 / 8 hydraulics, Pause. Engage,
       keypad 9 / 6 / 3 / 2 engage, world map, SMS, career stats, keypad 1
       reset (a pulse on the button's release, below). */
    set_key(SDL_SCANCODE_W, act_down(b, OPT_ACT_THROTTLE));
    set_key(SDL_SCANCODE_S, act_down(b, OPT_ACT_BACK));
    set_key(SDL_SCANCODE_E, act_down(b, OPT_ACT_SHIFT_UP));
    set_key(SDL_SCANCODE_Q, act_down(b, OPT_ACT_SHIFT_DOWN));
    /* the stick as left / right keys (also the drag lane change's analog
       events 0xba / 0xbb, handler 0x571d50 table 0x7fe238): retail presses
       left when the 0..255 axis byte goes below 0x40 (0x7fdd08) and right
       above 0xbf (0x7fdd09) -- raw < 64 / raw > 191, so lx < -64 / lx > 63 */
    set_key(SDL_SCANCODE_A, act_down(b, OPT_ACT_STEER_L) || lx < -64);
    set_key(SDL_SCANCODE_D, act_down(b, OPT_ACT_STEER_R) || lx > 63);
    set_key(SDL_SCANCODE_SPACE, act_down(b, OPT_ACT_HANDBRAKE));
    set_key(SDL_SCANCODE_N, act_down(b, OPT_ACT_NITROUS) && !(b & SCE_CTRL_SELECT));
    set_key(SDL_SCANCODE_C, act_down(b, OPT_ACT_CAMERA));
    set_key(SDL_SCANCODE_KP_9, act_down(b, OPT_ACT_ENGAGE));   /* free roam's challenge */
    set_key(SDL_SCANCODE_KP_6, act_down(b, OPT_ACT_WORLD_MAP));   /* joy event 0xc0: the career world map */
    set_key(SDL_SCANCODE_KP_3, act_down(b, OPT_ACT_READ_SMS));   /* joy event 0xc2: the SMS mailbox */
    set_key(SDL_SCANCODE_KP_2, act_down(b, OPT_ACT_CAREER_STATS));   /* RC message 0x13 (0x6068c5): UI_Status_Master */
    set_key(SDL_SCANCODE_B, act_down(b, OPT_ACT_LOOK_BACK));
    set_key(SDL_SCANCODE_KP_7, act_down(b, OPT_ACT_HYD_PRESSURE));   /* hydraulics (hyd_player) */
    set_key(SDL_SCANCODE_KP_8, act_down(b, OPT_ACT_HYD_BOUNCE));
    set_key(SDL_SCANCODE_PAUSE, act_down(b, OPT_ACT_PAUSE) && !quit);
    /* the menus' fixed buttons: Cross accepts, Circle backs out */
    set_key(SDL_SCANCODE_KP_ENTER, (b & SCE_CTRL_CROSS) != 0);
    set_key(SDL_SCANCODE_BACKSPACE, (b & SCE_CTRL_CIRCLE) != 0);
    set_key(SDL_SCANCODE_DELETE, (b & SCE_CTRL_SQUARE) != 0);   /* menus: Square */
    /* the d-pad, or the left stick past half (the menus' PAD_*_ALTERNATE;
       nothing reads the arrows while driving) */
    set_key(SDL_SCANCODE_LEFT, (b & SCE_CTRL_LEFT) || lx < -64);
    set_key(SDL_SCANCODE_RIGHT, (b & SCE_CTRL_RIGHT) || lx > 64);
    set_key(SDL_SCANCODE_UP, (b & SCE_CTRL_UP) || vita_lstick_y < -0.5f);
    set_key(SDL_SCANCODE_DOWN, (b & SCE_CTRL_DOWN) || vita_lstick_y > 0.5f);
    set_key(SDL_SCANCODE_LEFTBRACKET, (b & L) != 0);
    set_key(SDL_SCANCODE_RIGHTBRACKET, (b & R) != 0);
    set_key(SDL_SCANCODE_F, (b & SCE_CTRL_TRIANGLE) && !(b & SCE_CTRL_SELECT));   /* menus: Triangle */
    set_key(SDL_SCANCODE_RETURN, (b & SCE_CTRL_START) && !quit);
    /* Select+Triangle: next A/B test mode; logs how the one just left
       performed (Select+Up/Down are taken by the overclock overlay) */
    int mode_combo = (b & SCE_CTRL_SELECT) && (b & SCE_CTRL_TRIANGLE);
    if (mode_combo && !((g_prev_buttons & SCE_CTRL_SELECT) && (g_prev_buttons & SCE_CTRL_TRIANGLE))) {
        Uint64 now = sceKernelGetProcessTimeWide();
        if (g_mode_t0 && g_mode_frames)
            printf("vita: gfx mode %d (%s): %u frames, %.1f ms/frame\n", vita_gfx_mode,
                   g_mode_name[vita_gfx_mode], g_mode_frames,
                   (double)(now - g_mode_t0) / 1000.0 / g_mode_frames);
        vita_gfx_mode = (vita_gfx_mode + 1) % GFX_MODES;
        vita_mark("gfx mode %d: %s", vita_gfx_mode, g_mode_name[vita_gfx_mode]);
        g_mode_t0 = now; g_mode_frames = 0;
    }
    set_key(SDL_SCANCODE_1, (b & SCE_CTRL_SELECT) && !quit && !mode_combo);
    {   /* Reset (Player::HandleReset, main.c): on a clean release of its
           button -- nothing else pressed while it was held -- so the Select
           combos (quit, gfx mode, overclock) never reset the car */
        static int rheld, rclean;
        int rd = act_down(b, OPT_ACT_RESET);
        if (rd && !rheld) rclean = 1;
        else if (rd && (b & ~g_prev_buttons)) rclean = 0;
        set_key(SDL_SCANCODE_KP_1, !rd && rheld && rclean);
        rheld = rd;
    }
    set_key(SDL_SCANCODE_ESCAPE, quit);

    pad_button(b, SCE_CTRL_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
    pad_button(b, SCE_CTRL_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad_button(b, SCE_CTRL_CROSS, SDL_CONTROLLER_BUTTON_A);
    pad_button(b, SCE_CTRL_CIRCLE, SDL_CONTROLLER_BUTTON_B);
    pad_button(b, SCE_CTRL_START, SDL_CONTROLLER_BUTTON_START);
    g_prev_buttons = b;
}

int SDL_PollEvent(SDL_Event *e) {
    if (evq_head == evq_tail) poll_pad();        /* sample once per drained queue */
    if (evq_head == evq_tail) return 0;
    *e = evq[evq_head]; evq_head = (evq_head + 1) % EVQ;
    return 1;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys) {
    if (numkeys) *numkeys = SDL_NUM_SCANCODES;
    return g_keys;
}
int SDL_SetRelativeMouseMode(SDL_bool on) { (void)on; return 0; }

float vita_analog_steer(void) { return g_steer; }

int SDL_NumJoysticks(void) { return 0; }           /* the pad is surfaced as keys */
SDL_bool SDL_IsGameController(int index) { (void)index; return SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int index) { (void)index; return NULL; }
void SDL_GameControllerClose(SDL_GameController *c) { (void)c; }

/* ---- audio ---------------------------------------------------------------- */
static SDL_AudioSpec g_aspec;
static int g_aport = -1;
static SceUID g_athread = -1;
static volatile int g_apaused = 1, g_arun;

static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int bytes = g_aspec.samples * g_aspec.channels * 2;
    Uint8 *buf[2] = { calloc(1, (size_t)bytes), calloc(1, (size_t)bytes) };
    int cur = 0;
    while (g_arun) {
        if (g_apaused) memset(buf[cur], 0, (size_t)bytes);
        else g_aspec.callback(g_aspec.userdata, buf[cur], bytes);
        sceAudioOutOutput(g_aport, buf[cur]);     /* blocks until consumed */
        cur ^= 1;
    }
    free(buf[0]); free(buf[1]);
    return sceKernelExitDeleteThread(0);
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
        const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes) {
    (void)device; (void)iscapture; (void)allowed_changes;
    if (!desired || !desired->callback || desired->channels < 1 || desired->channels > 2) return 0;
    g_aspec = *desired;
    g_aport = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, g_aspec.samples, g_aspec.freq,
                                  g_aspec.channels == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
    if (g_aport < 0) { printf("audio: open port failed 0x%08X\n", g_aport); return 0; }
    if (obtained) *obtained = g_aspec;
    g_arun = 1;
    /* Above the streaming/read-ahead threads and off the render thread's
       core: with the render thread busy, sharing its core made the mix
       choppy (10-09). The mixer itself is ~7-11% of one core. */
    g_athread = sceKernelCreateThread("audio", audio_thread, 64, 0x10000, 0,
                                      SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);
    if (g_athread < 0) { printf("audio: thread could not start: 0x%08X\n", (unsigned)g_athread); sceAudioOutReleasePort(g_aport); g_aport = -1; return 0; }
    sceKernelStartThread(g_athread, 0, NULL);
    return 1;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { (void)dev; g_apaused = pause_on; }
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    if (g_aport < 0) return;
    g_arun = 0;
    sceKernelWaitThreadEnd(g_athread, NULL, NULL);
    sceAudioOutReleasePort(g_aport); g_aport = -1;
}

/* ---- threads -------------------------------------------------------------- */
struct SDL_Thread { SceUID uid; SDL_ThreadFunction fn; void *data; int status; };

static int thread_entry(SceSize args, void *argp) {
    (void)args;
    SDL_Thread *t = *(SDL_Thread **)argp;
    t->status = t->fn(t->data);
    return sceKernelExitThread(0);
}

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data) {
    SDL_Thread *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn; t->data = data;
    /* World preparation walks big bundles; give it a generous stack and let
       it run on any core other than the render thread's. */
    /* the boot world load ran on the 8 MB main stack before it moved here */
    int big_stack = name && !strcmp(name, "world-load");
    t->uid = sceKernelCreateThread(name ? name : "worker", thread_entry, 0x10000100,
                                   (big_stack ? 8 : 2) * 1024 * 1024, 0,
                                   SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);
    if (t->uid < 0) { printf("thread \"%s\" could not start: 0x%08X (out of kernel memory?)\n", name ? name : "worker", (unsigned)t->uid); free(t); return NULL; }
    sceKernelStartThread(t->uid, sizeof t, &t);
    return t;
}

extern int vita_gfx_mode;
/* ---- parallel-for (vita_parallel_for) ---------------------------------------
   Two persistent workers, one per spare core, sleep on their own semaphore.
   A call publishes fn/ctx/n, wakes as many workers as there are spare items,
   runs items itself too (all three pull from one atomic counter) and waits
   for the woken workers. Priority 120 beats the streaming worker (160), which
   shares cores 1-2, so a frame's AI never queues behind a world build. */
#define PFOR_WORKERS 2
static SceUID pfor_start[PFOR_WORKERS], pfor_done = -1;
static void (*volatile pfor_fn)(int, void *);
static void *volatile pfor_ctx;
static volatile int pfor_next, pfor_n;

static void pfor_run(void) {
    for (;;) {
        int i = __sync_fetch_and_add(&pfor_next, 1);
        if (i >= pfor_n) break;
        pfor_fn(i, pfor_ctx);
    }
}

static int pfor_worker(SceSize args, void *argp) {
    int w = *(int *)argp; (void)args;
    for (;;) {
        sceKernelWaitSema(pfor_start[w], 1, NULL);
        __sync_synchronize();
        pfor_run();
        __sync_synchronize();
        sceKernelSignalSema(pfor_done, 1);
    }
    return 0;
}

static int pfor_init(void) {
    pfor_done = sceKernelCreateSema("pfor-done", 0, 0, PFOR_WORKERS, NULL);
    if (pfor_done < 0) return 0;
    for (int w = 0; w < PFOR_WORKERS; w++) {
        pfor_start[w] = sceKernelCreateSema("pfor-start", 0, 0, 1, NULL);
        SceUID uid = sceKernelCreateThread(w ? "pfor-2" : "pfor-1", pfor_worker, 120, 0x40000, 0,
                                           w ? SCE_KERNEL_CPU_MASK_USER_2 : SCE_KERNEL_CPU_MASK_USER_1, NULL);
        if (pfor_start[w] < 0 || uid < 0 || sceKernelStartThread(uid, sizeof w, &w) < 0) {
            /* a worker that never runs would never signal pfor_done */
            printf("parallel-for: worker %d failed, running serial\n", w + 1);
            pfor_done = -1; return 0;
        }
    }
    printf("parallel-for: %d workers on cores 1-2\n", PFOR_WORKERS);
    return 1;
}

static int pfor_state;   /* 0 untried, 1 ready, -1 unavailable */
static int pfor_async_used;
/* Workers only; the caller continues and collects them with
   vita_parallel_join(). */
void vita_parallel_async(int n, void (*fn)(int i, void *ctx), void *ctx) {
    if (n <= 0) return;
    if (pfor_async_used) { sceKernelWaitSema(pfor_done, pfor_async_used, NULL); pfor_async_used = 0; }
    if (!pfor_state) pfor_state = pfor_init() ? 1 : -1;
    if (pfor_state < 0) { for (int i = 0; i < n; i++) fn(i, ctx); return; }
    pfor_fn = fn; pfor_ctx = ctx; pfor_n = n; pfor_next = 0;
    __sync_synchronize();
    int use = n < PFOR_WORKERS ? n : PFOR_WORKERS;
    for (int w = 0; w < use; w++) sceKernelSignalSema(pfor_start[w], 1);
    pfor_async_used = use;
}
void vita_parallel_join(void) {
    if (!pfor_async_used) return;
    sceKernelWaitSema(pfor_done, pfor_async_used, NULL);
    __sync_synchronize();
    pfor_async_used = 0;
}

void vita_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx) {
    static int state;   /* 0 untried, 1 ready, -1 unavailable */
    if (n <= 0) return;
    vita_parallel_join();
    if (!pfor_state) pfor_state = pfor_init() ? 1 : -1;
    state = pfor_state;
    if (state < 0 || n == 1) {
        for (int i = 0; i < n; i++) fn(i, ctx);
        return;
    }
    pfor_fn = fn; pfor_ctx = ctx; pfor_n = n; pfor_next = 0;
    __sync_synchronize();
    int use = n - 1 < PFOR_WORKERS ? n - 1 : PFOR_WORKERS;
    for (int w = 0; w < use; w++) sceKernelSignalSema(pfor_start[w], 1);
    pfor_run();
    sceKernelWaitSema(pfor_done, use, NULL);
    __sync_synchronize();
}

void SDL_WaitThread(SDL_Thread *t, int *status) {
    if (!t) return;
    sceKernelWaitThreadEnd(t->uid, NULL, NULL);
    sceKernelDeleteThread(t->uid);
    if (status) *status = t->status;
    free(t);
}

/* ---- one-off progress screen (before the engine's renderer exists) --------
   Used by the world tile conversion, which runs before the world and its
   shaders load: rectangles and a 5x7 pixel font made of scissored clears. */
static void ps_rect(int x, int y, int w, int h, float r, float g, float b) {
    glScissor(x, g_disp_h - y - h, w, h);   /* top-left coordinates */
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}
#include "ps_font.inc"

/* Background art (app0:assets/optimizing.raw, 720x408 RGB565, made from
   the user's own artwork; its title is part of the picture). */
static GLuint g_ps_bg; static int g_ps_bg_tried;
static void ps_background(int W, int H) {
    if (!g_ps_bg_tried) {
        g_ps_bg_tried = 1;
        size_t n = 720u * 408u * 2u;
        unsigned char *px = malloc(n);
        SceUID fd = sceIoOpen("app0:assets/optimizing.raw", SCE_O_RDONLY, 0);
        if (px && fd >= 0 && sceIoRead(fd, px, (SceSize)n) == (int)n) {
            glGenTextures(1, &g_ps_bg);
            glBindTexture(GL_TEXTURE_2D, g_ps_bg);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 720, 408, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else printf("progress screen: no background art (%d)\n", fd);
        if (fd >= 0) sceIoClose(fd);
        free(px);
    }
    if (!g_ps_bg) { ps_rect(0, 0, W, H, 0.02f, 0.03f, 0.06f); return; }
    /* fixed-function quad: the engine's shaders do not exist yet */
    glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrthof(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_ps_bg);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0, 0); glVertex3f(0, 0, 0);
    glTexCoord2f(1, 0); glVertex3f(1, 0, 0);
    glTexCoord2f(0, 1); glVertex3f(0, 1, 0);
    glTexCoord2f(1, 1); glVertex3f(1, 1, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_SCISSOR_TEST);
}
static void ps_text_shadow(const char *s, int x, int y, int px, float r, float g, float b) {
    ps_text(s, x + px, y + px, px, 0.0f, 0.0f, 0.0f);
    ps_text(s, x, y, px, r, g, b);
}
/* Called once the conversion is over: the texture is not needed again. */
void vita_progress_screen_done(void) {
    if (g_ps_bg) { glDeleteTextures(1, &g_ps_bg); g_ps_bg = 0; }
}

/* Draws one frame: background, a progress bar at `frac` (0..1) and two lines.
   `title` is drawn only when there is no background art (the art has it). */
void vita_progress_screen(const char *title, const char *line1, const char *line2, float frac) {
    int W = g_disp_w, H = g_disp_h;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    ps_rect(0, 0, W, H, 0.0f, 0.0f, 0.0f);
    ps_background(W, H);
    int sp = W >= 900 ? 3 : 2;
    if (!g_ps_bg && title)
        ps_text(title, (W - ps_text_width(title, sp + 1)) / 2, H * 150 / 544, sp + 1, 0.95f, 0.75f, 0.20f);
    /* under the art's title (y 237 of 544) */
    int bw = W * 2 / 3, bx = (W - bw) / 2, by = H * 290 / 544, bh = H * 14 / 544;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    ps_rect(bx - 2, by - 2, bw + 4, bh + 4, 0.55f, 0.85f, 0.95f);
    ps_rect(bx, by, bw, bh, 0.02f, 0.05f, 0.08f);
    ps_rect(bx, by, (int)(bw * frac), bh, 0.25f, 0.85f, 1.0f);
    if (line1) ps_text_shadow(line1, (W - ps_text_width(line1, sp)) / 2, by + bh + H * 22 / 544, sp, 0.92f, 0.97f, 1.0f);
    if (line2) ps_text_shadow(line2, (W - ps_text_width(line2, sp)) / 2, by + bh + H * 50 / 544, sp, 0.70f, 0.82f, 0.90f);
    glDisable(GL_SCISSOR_TEST);
    vglSwapBuffers(GL_FALSE);
}
/* Full clocks before SDL_Init has run (the tile conversion starts earlier). */
void vita_fast_clocks(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
}

/* The console user name: the profile name the original's results and standings
   show for the player (the port has no profile of its own). */
static void apputil_init(void) {
    static int done;
    if (done) return;
    SceAppUtilInitParam ip; SceAppUtilBootParam bp;
    memset(&ip, 0, sizeof ip); memset(&bp, 0, sizeof bp);
    sceAppUtilInit(&ip, &bp);
    done = 1;
}
const char *vita_user_name(void) {
    static char name[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE];
    static int done;
    if (!done) {
        apputil_init();
        if (sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8 *)name, sizeof name) < 0)
            name[0] = 0;
        done = 1;
    }
    return name;
}

/* The system language (SCE_SYSTEM_PARAM_LANG_*): the game's language, as the
   consoles take theirs from the OS (nfsu2.h n2_lang). */
int vita_system_lang(void) {
    apputil_init();
    int v = -1;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &v) < 0) v = -1;
    return v;
}

/* Restart the app (a venue change: the boot reads ux0:data/NFSU2/venue.txt). */
#define RELAUNCH_MARK VITA_DATA "/relaunch.tmp"
void vita_relaunch(void) {
    /* the new process is ours: the Settings screen does not open on it */
    SceUID fd = sceIoOpen(RELAUNCH_MARK, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) sceIoClose(fd);
    sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
}

/* ---- Settings screen (LiveArea button / L+R at boot, boot_settings.inc) ---- */
#define BS_W g_disp_w
#define BS_H g_disp_h
static unsigned bs_pad(void) {
    SceCtrlData pad; memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned b = pad.buttons, o = 0;
    if (b & SCE_CTRL_UP) o |= 1;          if (b & SCE_CTRL_DOWN) o |= 2;
    if (b & SCE_CTRL_LEFT) o |= 4;        if (b & SCE_CTRL_RIGHT) o |= 8;
    if (b & SCE_CTRL_CROSS) o |= 16;      if (b & SCE_CTRL_CIRCLE) o |= 32;
    if (b & SCE_CTRL_START) o |= 64;
    if (b & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1)) o |= 128;
    if (b & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1)) o |= 256;
    return o;
}
static void bs_present(void) { vglSwapBuffers(GL_FALSE); sceKernelDelayThread(33000); }
static int bs_exists(const char *p) {
    SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd); return 1;
}
static void bs_touch(const char *p) {
    SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) sceIoClose(fd);
}
static void bs_remove(const char *p) { sceIoRemove(p); }
static int bs_rmtree(const char *dir) {
    int n = 0;
    SceUID d = sceIoDopen(dir);
    if (d < 0) return 0;
    SceIoDirent e;
    char path[512];
    for (;;) {
        memset(&e, 0, sizeof e);
        if (sceIoDread(d, &e) <= 0) break;
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e.d_name);
        if (SCE_S_ISDIR(e.d_stat.st_mode)) n += bs_rmtree(path);
        else if (sceIoRemove(path) >= 0 && (++n & 63) == 0)
            vita_progress_screen(NULL, "DELETING CACHES", NULL, 0.5f);
    }
    sceIoDclose(d);
    sceIoRmdir(dir);
    return n;
}
/* The Settings screen's text: the Vita's own system font (ScePvf, the
   sans-serif Latin face the system menus use), one font handle per face. */
#include <psp2/pvf.h>
#include <psp2/sysmodule.h>
typedef struct { int w, h, left, top; float adv; } BsGlyphRasterV;
static ScePvfLibId g_pvf_lib; static ScePvfFontId g_pvf[2]; static int g_pvf_px[2], g_pvf_tried;
static ScePvfPointer pvf_alloc(ScePvfPointer u, ScePvfU32 n) { (void)u; return malloc(n); }
static ScePvfPointer pvf_realloc(ScePvfPointer u, ScePvfPointer p, ScePvfU32 n) { (void)u; return realloc(p, n); }
static void pvf_free(ScePvfPointer u, ScePvfPointer p) { (void)u; free(p); }
static ScePvfFontId pvf_open(int bold)
{
    ScePvfError err = 0;
    ScePvfFontStyleInfo st; memset(&st, 0, sizeof st);
    st.familyCode = SCE_PVF_FAMILY_SANSERIF;
    st.style = bold ? SCE_PVF_STYLE_BOLD : SCE_PVF_STYLE_REGULAR;
    st.languageCode = SCE_PVF_LANGUAGE_LATIN;
    ScePvfFontIndex ix = scePvfFindOptimumFont(g_pvf_lib, &st, &err);
    ScePvfFontId f = err >= 0 && ix >= 0 ? scePvfOpen(g_pvf_lib, ix, 1, &err) : NULL;
    if (!f || err < 0) f = scePvfOpenDefaultLatinFontOnSharedMemory(g_pvf_lib, &err);
    vita_mark("settings: system font %s %s (index %d, 0x%08x)", bold ? "bold" : "regular", f ? "open" : "missing",
              (int)ix, (unsigned)err);
    return f;
}
/* em size in pixels: measured from a capital H so the face lands at the
   same visual size as any other font (cap height 0.7 em) */
/* No system font: the boot screens' 5x7 pixel font, scaled to the cap
   height (capitals only), so the screen always has readable text. */
static int ps_glyph_raster(int face, int px, unsigned cp, unsigned char *buf, int stride, int cap, BsGlyphRasterV *g)
{
    int k = (int)((0.70f * (float)px) / 7.0f + 0.5f); if (k < 1) k = 1;
    memset(g, 0, sizeof *g);
    g->adv = (float)(6 * k + (face ? k / 2 : 0));
    char c = cp >= 'a' && cp <= 'z' ? (char)(cp - 32) : (char)cp;
    if (c == ' ') return 1;
    for (unsigned i = 0; i < sizeof g_font5x7 / sizeof *g_font5x7; i++) {
        if (g_font5x7[i].c != c) continue;
        int w = 5 * k + (face ? 1 : 0), h = 7 * k;
        if (w > stride || h > cap) return 0;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g_font5x7[i].row[row] & (16 >> col))
                    for (int yy = 0; yy < k; yy++)
                        for (int xx = 0; xx < k + (face ? 1 : 0); xx++)
                            buf[(row * k + yy) * stride + col * k + xx] = 255;
        g->w = w; g->h = h; g->left = 0; g->top = h;
        return 1;
    }
    return 1;   /* unknown character: a blank advance */
}
static int pvf_size(int face, int px)
{
    ScePvfFontId f = g_pvf[face];
    if (g_pvf_px[face] == px) return 1;
    if (scePvfSetCharSize(f, 10.0f, 10.0f) < 0) return 0;
    ScePvfCharInfo ci; memset(&ci, 0, sizeof ci);
    if (scePvfGetCharInfo(f, 'H', &ci) < 0 || ci.glyphMetrics.horizontalBearingY64 <= 0) return 0;
    float sz = 10.0f * (0.70f * (float)px) * 64.0f / (float)ci.glyphMetrics.horizontalBearingY64;
    if (scePvfSetCharSize(f, sz, sz) < 0) return 0;
    g_pvf_px[face] = px;
    return 1;
}
static int bs_font_raster_v(int face, int px, unsigned cp, unsigned char *buf, int stride, int cap, void *out)
{
    BsGlyphRasterV *g = (BsGlyphRasterV *)out;
    if (!g_pvf_tried) {
        g_pvf_tried = 1;
        /* libpvf is a system module the app loads itself (the PGF id
           covers it); without it every scePvf call fails and the screen
           had no text */
        int lm = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
        vita_mark("settings: font module load 0x%08x", (unsigned)lm);
        ScePvfInitRec r; memset(&r, 0, sizeof r);
        r.maxNumFonts = 4; r.allocFunc = pvf_alloc; r.reallocFunc = pvf_realloc; r.freeFunc = pvf_free;
        ScePvfError err = 0;
        g_pvf_lib = scePvfNewLib(&r, &err);
        if (g_pvf_lib) {
            /* PVF needs an EM scale as well as a resolution (as in libvita2d). */
            scePvfSetEM(g_pvf_lib, 72.0f / (10.125f * 128.0f));
            scePvfSetResolution(g_pvf_lib, 128.0f, 128.0f);
            g_pvf[0] = pvf_open(0); g_pvf[1] = pvf_open(1);
            if (!g_pvf[1]) g_pvf[1] = g_pvf[0];
        } else vita_mark("settings: scePvfNewLib failed 0x%08x", (unsigned)err);
    }
    face = face ? 1 : 0;
    ScePvfFontId f = g_pvf[face];
    if (!f) return ps_glyph_raster(face, px, cp, buf, stride, cap, g);
    if (g_pvf[1] == g_pvf[0]) g_pvf_px[face ^ 1] = 0;   /* one handle for both faces: size each time */
    static int warned;
    if (!pvf_size(face, px)) {
        if (!warned++) vita_mark("settings: system font size failed, pixel font used");
        return ps_glyph_raster(face, px, cp, buf, stride, cap, g);
    }
    if (g_pvf[1] == g_pvf[0]) scePvfSetEmboldenRate(f, face ? 10.0f : 0.0f);   /* no bold face: embolden */
    ScePvfCharInfo ci; memset(&ci, 0, sizeof ci);
    if (scePvfGetCharInfo(f, (ScePvfCharCode)cp, &ci) < 0) return ps_glyph_raster(face, px, cp, buf, stride, cap, g);
    g->adv = (float)ci.glyphMetrics.horizontalAdvance64 / 64.0f;
    g->left = ci.glyphMetrics.horizontalBearingX64 >> 6;
    g->top = ci.glyphMetrics.horizontalBearingY64 >> 6;
    ScePvfIrect rect; memset(&rect, 0, sizeof rect);
    if (scePvfGetCharImageRect(f, (ScePvfCharCode)cp, &rect) < 0)
        return ps_glyph_raster(face, px, cp, buf, stride, cap, g);
    g->w = rect.width; g->h = rect.height;
    if (g->w > stride) g->w = stride;
    if (g->h > cap) g->h = cap;
    if (g->w <= 0 || g->h <= 0) { g->w = g->h = 0; return 1; }   /* a space */
    ScePvfUserImageBufferRec ib; memset(&ib, 0, sizeof ib);
    ib.pixelFormat = SCE_PVF_USERIMAGE_DIRECT8;
    ib.xPos64 = -ci.glyphMetrics.horizontalBearingX64; ib.yPos64 = ci.glyphMetrics.horizontalBearingY64;   /* pen origin: the bitmap lands at (0,0) */
    ib.rect.width = (ScePvfU16)stride; ib.rect.height = (ScePvfU16)cap;
    ib.bytesPerLine = (ScePvfU16)stride;
    ib.buffer = buf;
    int ge = scePvfGetCharGlyphImage(f, (ScePvfCharCode)cp, &ib);
    if (ge < 0) {
        if (!warned++) vita_mark("settings: system font glyph failed 0x%08x, pixel font used", (unsigned)ge);
        memset(buf, 0, (size_t)stride * cap);
        return ps_glyph_raster(face, px, cp, buf, stride, cap, g);
    }
    return 1;
}
#define bs_font_raster(face, px, cp, buf, stride, cap, g) bs_font_raster_v(face, px, cp, buf, stride, cap, (void *)(g))
/* a new press on the front touch screen, in screen pixels */
static int bs_tap(int *x, int *y)
{
    static int init, down;
    if (!init) { init = 1; sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START); }
    SceTouchData td; memset(&td, 0, sizeof td);
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) < 0) return 0;
    int was = down; down = td.reportNum > 0;
    if (!down || was) return 0;
    *x = td.report[0].x * BS_W / 1920; *y = td.report[0].y * BS_H / 1088;
    return 1;
}
#include "boot_settings.inc"

/* Called first thing in main(). Opens the Settings screen when the LiveArea
   button (psla:-settings) launched the app or L+R are held, then restarts
   the app; returns at once otherwise. */
void vita_settings_boot(void) {
    if (bs_exists(RELAUNCH_MARK)) { sceIoRemove(RELAUNCH_MARK); return; }   /* our own restart */
    int want = (bs_pad() & (128 | 256)) == (128 | 256);
    apputil_init();
    SceAppUtilAppEventParam ev; memset(&ev, 0, sizeof ev);
    if (sceAppUtilReceiveAppEvent(&ev) >= 0 && ev.type == 0x05) {   /* LiveArea liveitem */
        char arg[2048] = "";
        sceAppUtilAppEventParseLiveArea(&ev, arg);
        vita_mark("LiveArea launch: \"%s\"", arg);
        if (strstr(arg, "-settings")) want = 1;
    }
    if (!want) return;
    sceIoMkdir(VITA_DATA, 0777);
    vt_skip = 1;
    SDL_GL_CreateContext(&g_window);
    boot_settings_run();
    vita_relaunch();
}
