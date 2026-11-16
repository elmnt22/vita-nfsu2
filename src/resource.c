/* resource.c — OpenUG2 ResourceManager module implementation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef __vita__
#include <sys/mman.h>
#else
#include <psp2/io/fcntl.h>
#endif

#include "resource.h"

void (*g_load_pump)(void);   /* resource.h: the loading screen's frame pump */

#ifdef __vita__
/* No mmap on the Vita: read the whole file into the heap instead. Callers only
   read the mapping and release it through res_unmap_file, so this is
   behaviourally identical apart from the up-front read. */
unsigned char *res_map_file(const char *path, long *len) {
    int fd = open(path, O_RDONLY);       /* read(): see n2_read_file on fread speed */
    if (fd < 0) return NULL;
    long n = (long)lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    unsigned char *p = n > 0 ? malloc((size_t)n) : NULL;
    long got = 0;
    while (p && got < n) {
        long want = n - got > (4L << 20) ? (4L << 20) : n - got;
        long k = (long)read(fd, p + got, (size_t)want);
        if (k <= 0) break;
        got += k;
    }
    close(fd);
    if (!p || got != n) { free(p); return NULL; }
    *len = n;
    return p;
}

void res_unmap_file(unsigned char *data, long len) {
    (void)len;
    free(data);
}
#else
unsigned char *res_map_file(const char *path, long *len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); return NULL; }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *len = (long)st.st_size;
    return (unsigned char *)p;
}

void res_unmap_file(unsigned char *data, long len) {
    if (data && len > 0) munmap(data, (size_t)len);
}
#endif

int res_list_tracks(const char *troot, char (*list)[64], int max,
                    const char *cur, int *sel) {
    int n = 0;
    DIR *td = opendir(troot); struct dirent *de;
    while (td && n < max && (de = readdir(td))) {
        char *dot = strstr(de->d_name, ".BUN");
        if (strncmp(de->d_name, "STREAM", 6) || !dot) continue;
        int L = (int)(dot - de->d_name); if (L > 63) L = 63;
        memcpy(list[n], de->d_name, L); list[n][L] = 0;
        if (!strcmp(list[n], cur)) *sel = n;
        n++;
    }
    if (td) closedir(td);
    return n;
}

int res_list_cars(const char *dataroot, char (*list)[64], int max,
                  const char *cur, int *sel) {
    static const char *parts[] = { "AUDIO","BRAKES","EXHAUST","PLATES","ROOF",
        "WHEELS","SPOILER","SPOILER_HATCH","SPOILER_SUV","MIRRORS_BODY",
        "MIRRORS_HUMMER","MIRRORS_POST","MIRRORS_SUV" };
    int n = 0;
    char croot[1024]; snprintf(croot, sizeof croot, "%s/CARS", dataroot);
    DIR *cd = opendir(croot); struct dirent *de;
    while (cd && n < max && (de = readdir(cd))) {
        if (de->d_name[0] == '.') continue;
        int isp = 0;
        for (unsigned p = 0; p < sizeof parts/sizeof parts[0]; p++)
            if (!strcmp(de->d_name, parts[p])) isp = 1;
        if (isp) continue;
        char gp[1200]; snprintf(gp, sizeof gp, "%s/%s/GEOMETRY.BIN", croot, de->d_name);
        FILE *f = fopen(gp, "rb"); if (!f) continue; fclose(f);
        int L = (int)strlen(de->d_name); if (L > 63) L = 63;
        memcpy(list[n], de->d_name, L); list[n][L] = 0;
        if (!strcmp(list[n], cur)) *sel = n;
        n++;
    }
    if (cd) closedir(cd);
    return n;
}

int res_list_circuits(const char *troot, const char *trackname,
                      char (*list)[256], int max) {
    /* ALL is a blind union of incompatible bundles, not a race world. */
    if (!trackname || !strcmp(trackname, "ALL")) return 0;
    /* the shipped 1:1 mapping: STREAM<stem>.BUN <-> ROUTES<stem>/ */
    const char *stem = strncmp(trackname, "STREAM", 6) ? trackname : trackname + 6;
    char dname[256]; snprintf(dname, sizeof dname, "ROUTES%s", stem);
    char rdir[1200]; snprintf(rdir, sizeof rdir, "%s/%s", troot, dname);

    int n = 0;
    DIR *rd = opendir(rdir); struct dirent *re;
    while (rd && n < max && (re = readdir(rd))) {
        if (strncmp(re->d_name, "Paths", 5)) continue;
        char rel[256]; snprintf(rel, sizeof rel, "%s/%s", dname, re->d_name);
        int dup=0; for (int i=0;i<n;i++) if(!strcmp(list[i],rel)) dup=1;
        if (dup) continue;
        char pp[1500]; snprintf(pp, sizeof pp, "%s/%s", troot, rel);
        long pl; unsigned char *pd = n2_read_file(pp, &pl); N2Path tp = {0};
        if (pd && n2_load_path(pd, pl, &tp) > 4) {
            /* closed loop: first waypoint ~= last (sprints are open routes) */
            float dx=tp.xy[0]-tp.xy[(tp.n-1)*2], dy=tp.xy[1]-tp.xy[(tp.n-1)*2+1];
            if (dx*dx+dy*dy < 900.0f) strncpy(list[n++], rel, 255);
        }
        free(tp.xy); free(pd);
    }
    if (rd) closedir(rd);
    return n;
}

#ifdef __vita__
/* ---- pinned compact bundle --------------------------------------------- */
typedef struct { long coff, foff, flen; } ResPixStub;
static struct {
    char path[1024];
    unsigned char *data; long len;
    ResPixStub *stub; int nstub;
} g_pin;
static long g_pin_flen;
static unsigned g_pin_gen = 1;   /* bumped by every pin and unpin */

static int pin_pread(int fd, long off, void *dst, long n) {
    if (lseek(fd, off, SEEK_SET) != off) return 0;
    long got = 0;
    while (got < n) {
        long want = n - got > (4L << 20) ? (4L << 20) : n - got;
        long k = (long)read(fd, (unsigned char *)dst + got, (size_t)want);
        if (k <= 0) return 0;
        got += k;
    }
    return 1;
}
static uint32_t pin_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void pin_w32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* Copy one TPK container (0xb3300000) at file offset `off` (header
 * included) into out+cur, emptying its 0x33320002 pixel payload. Returns the
 * bytes written, or -1. `out` NULL = size only (stubs still counted). */
static long pin_tpk(int fd, long off, uint32_t size, unsigned char *out, long cur,
                    ResPixStub *stub, int *nstub) {
    long end = off + 8 + (long)size, o = off + 8, w = 8;
    long outer_hdr = cur;
    unsigned char h[8];
    while (o + 8 <= end) {
        if (!pin_pread(fd, o, h, 8)) return -1;
        uint32_t cid = pin_u32(h), cs = pin_u32(h + 4);
        if (cid != 0xb3320000u) {                 /* copied verbatim */
            if (out && !pin_pread(fd, o, out + cur + w, 8 + (long)cs)) return -1;
            w += 8 + (long)cs;
        } else {
            long inner_hdr = cur + w, iw = 8, q = o + 8, qe = o + 8 + (long)cs;
            while (q + 8 <= qe) {
                unsigned char g[8];
                if (!pin_pread(fd, q, g, 8)) return -1;
                uint32_t gid = pin_u32(g), gs = pin_u32(g + 4);
                if (gid == 0x33320002u) {
                    if (out) { pin_w32(out + inner_hdr + iw, gid); pin_w32(out + inner_hdr + iw + 4, 0); }
                    if (stub) {
                        /* payload starts after a run of 0x11 filler bytes */
                        unsigned char head[256]; long hn = gs < 256 ? (long)gs : 256, skip = 0;
                        if (hn > 0 && pin_pread(fd, q + 8, head, hn))
                            while (skip < hn && head[skip] == 0x11) skip++;
                        skip = n2_tpk_pixel_skip(skip);   /* 0x80-aligned payload */
                        stub[*nstub].coff = inner_hdr + iw;
                        stub[*nstub].foff = q + 8 + skip; stub[*nstub].flen = (long)gs - skip;
                    }
                    (*nstub)++;
                    iw += 8;
                } else {
                    if (out && !pin_pread(fd, q, out + inner_hdr + iw, 8 + (long)gs)) return -1;
                    iw += 8 + (long)gs;
                }
                q += 8 + (long)gs;
            }
            if (out) { pin_w32(out + inner_hdr, cid); pin_w32(out + inner_hdr + 4, (uint32_t)(iw - 8)); }
            w += iw;
        }
        o += 8 + (long)cs;
    }
    if (out) { pin_w32(out + outer_hdr, 0xb3300000u); pin_w32(out + outer_hdr + 4, (uint32_t)(w - 8)); }
    return w;
}

/* Two passes over the top-level chunk list: size, then copy. */
static long pin_build(int fd, long flen, unsigned char *out, ResPixStub *stub, int *nstub) {
    long off = 0, cur = 0;
    unsigned char h[8];
    *nstub = 0;
    while (off + 8 <= flen) {
        if (!pin_pread(fd, off, h, 8)) return -1;
        uint32_t cid = pin_u32(h), cs = pin_u32(h + 4);
        long next = off + 8 + (long)cs;
        if (next > flen) return -1;
        if (cid == 0) {
            /* padding: shrink to the size that keeps the next chunk at its
               original offset modulo 128 */
            long pad = ((next - cur - 8) % 128 + 128) % 128;
            if (out) { pin_w32(out + cur, 0); pin_w32(out + cur + 4, (uint32_t)pad);
                       memset(out + cur + 8, 0, (size_t)pad); }
            cur += 8 + pad;
        } else if (cid == 0xb3300000u) {
            long w = pin_tpk(fd, off, cs, out, cur, stub, nstub);
            if (w < 0) return -1;
            cur += w;
            /* realign whatever follows with an explicit padding chunk */
            long mis = ((next - cur) % 128 + 128) % 128;
            if (mis) {
                long pad = ((mis - 8) % 128 + 128) % 128;
                if (out) { pin_w32(out + cur, 0); pin_w32(out + cur + 4, (uint32_t)pad);
                           memset(out + cur + 8, 0, (size_t)pad); }
                cur += 8 + pad;
            }
        } else {
            if (out && !pin_pread(fd, off, out + cur, 8 + (long)cs)) return -1;
            cur += 8 + (long)cs;
        }
        off = next;
    }
    return cur;
}

/* On-card cache of the compact copy, so only the first boot pays for the
   ~12k small reads of building it. Keyed by the bundle's file size. */
#define PIN_MAGIC "OUG2CB02"
#define PIN_STRIP_MAGIC "OUG2CS01"   /* the compact copy less what world tiles replace */
static void pin_cache_path(const char *path, char *out, size_t cap) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, cap, "ux0:data/NFSU2/cache/%s.compact", base);
}
static int pin_cache_load(const char *cpath, const char *path, long flen, const char *want) {
    int fd = open(cpath, O_RDONLY);
    if (fd < 0) return 0;
    char magic[8]; long hdr[3];   /* source length, compact length, stub count */
    int ok = read(fd, magic, 8) == 8 && !memcmp(magic, want, 8) &&
             read(fd, hdr, sizeof hdr) == (int)sizeof hdr && hdr[0] == flen &&
             hdr[1] > 0 && hdr[2] >= 0;
    unsigned char *data = ok ? (unsigned char *)malloc((size_t)hdr[1]) : NULL;
    ResPixStub *stub = ok && hdr[2] ? (ResPixStub *)malloc((size_t)hdr[2] * sizeof *stub) : NULL;
    ok = ok && data && (!hdr[2] || stub) &&
         (!hdr[2] || pin_pread(fd, 8 + (long)sizeof hdr, stub, hdr[2] * (long)sizeof *stub)) &&
         pin_pread(fd, 8 + (long)sizeof hdr + hdr[2] * (long)sizeof *stub, data, hdr[1]);
    close(fd);
    if (!ok) { free(data); free(stub); return 0; }
    snprintf(g_pin.path, sizeof g_pin.path, "%s", path);
    g_pin.data = data; g_pin.len = hdr[1]; g_pin.stub = stub; g_pin.nstub = (int)hdr[2];
    g_pin_flen = flen; g_pin_gen++;
    printf("bundle pinned from cache: %s (%ld MB)\n", cpath, hdr[1] >> 20);
    return 1;
}
static void pin_cache_save(const char *cpath, long flen, const char *magic) {
    mkdir("ux0:data/NFSU2/cache", 0777);
    char tmp[1110]; snprintf(tmp, sizeof tmp, "%s.tmp", cpath);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    long hdr[3] = { flen, g_pin.len, g_pin.nstub };
    long sn = g_pin.nstub * (long)sizeof *g_pin.stub;
    int ok = write(fd, magic, 8) == 8 && write(fd, hdr, sizeof hdr) == (int)sizeof hdr &&
             (!sn || write(fd, g_pin.stub, (size_t)sn) == sn);
    for (long got = 0; ok && got < g_pin.len; ) {
        long want = g_pin.len - got > (4L << 20) ? (4L << 20) : g_pin.len - got;
        long k = (long)write(fd, g_pin.data + got, (size_t)want);
        if (k <= 0) ok = 0; else got += k;
    }
    close(fd);
    if (ok) { remove(cpath); ok = rename(tmp, cpath) == 0; }
    if (!ok) remove(tmp);
    printf("bundle cache %s: %s\n", ok ? "written" : "NOT written", cpath);
}

static void *g_tpk_memo; static int g_tpk_memo_n; static size_t g_tpk_memo_elem;
/* World tiles carry every placed mesh, so with them the pinned bundle only
   has to answer texture (TPK), light and grass lookups. Its model geometry
   (0x80134000), scenery sections (0x80034100) and their extras (0x80034130)
   were ~57 MB nothing reads; drop them, keeping the pixel stubs valid. */
static int g_pin_strip_req;
static void pin_strip(void) {
    if (!g_pin.data || g_pin.len <= 0) return;
    const unsigned char *d = g_pin.data;
    long len = g_pin.len, keep = 0;
    for (long p = 0; p + 8 <= len; ) {
        uint32_t m = pin_u32(d + p), sz = pin_u32(d + p + 4);
        if (p + 8 + (long)sz > len) { keep += len - p; break; }
        if (m != 0x80134000u && m != 0x80034100u && m != 0x80034130u) keep += 8 + (long)sz;
        p += 8 + (long)sz;
    }
    if (keep >= len) return;
    unsigned char *out = (unsigned char *)malloc((size_t)(keep ? keep : 1));
    if (!out) { printf("bundle strip: no memory, keeping the full copy\n"); return; }
    long q = 0; int si = 0, kept_stubs = 0;
    for (long p = 0; p + 8 <= len; ) {
        uint32_t m = pin_u32(d + p), sz = pin_u32(d + p + 4);
        long end = p + 8 + (long)sz;
        if (end > len) end = len;
        int drop = m == 0x80134000u || m == 0x80034100u || m == 0x80034130u;
        /* stubs are in buffer order: move the ones inside this chunk along */
        while (si < g_pin.nstub && g_pin.stub[si].coff < end) {
            if (!drop && g_pin.stub[si].coff >= p) {
                g_pin.stub[kept_stubs] = g_pin.stub[si];
                g_pin.stub[kept_stubs].coff += q - p;
                kept_stubs++;
            }
            si++;
        }
        if (!drop) { memcpy(out + q, d + p, (size_t)(end - p)); q += end - p; }
        p = end;
    }
    printf("bundle strip (world tiles): %ld MB -> %ld MB in memory, %d of %d pixel stubs kept\n",
           len >> 20, q >> 20, kept_stubs, g_pin.nstub);
    free(g_pin.data);
    g_pin.data = out; g_pin.len = q; g_pin.nstub = kept_stubs;
    free(g_tpk_memo); g_tpk_memo = NULL; g_tpk_memo_n = 0;   /* offsets into the old copy */
    {   /* the stripped copy on the card too: later loads read 1 MB, not 57 */
        char spath[1100];
        pin_cache_path(g_pin.path, spath, sizeof spath);
        snprintf(spath + strlen(spath), sizeof spath - strlen(spath), ".strip");
        pin_cache_save(spath, g_pin_flen, PIN_STRIP_MAGIC);
    }
}
void res_pin_strip(void) { g_pin_strip_req = 1; pin_strip(); }

unsigned char *res_bundle(const char *path, long *len) {
    if (!path || !len) return NULL;
    if (g_pin.data) {
        if (strcasecmp(path, g_pin.path)) return NULL;   /* one pinned bundle */
        *len = g_pin.len;
        return g_pin.data;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    long flen = (long)lseek(fd, 0, SEEK_END);
    char cpath[1100];
    pin_cache_path(path, cpath, sizeof cpath);
    if (g_pin_strip_req) {   /* world tiles: the stripped copy is all that is wanted */
        char spath[1110]; snprintf(spath, sizeof spath, "%s.strip", cpath);
        if (pin_cache_load(spath, path, flen, PIN_STRIP_MAGIC)) {
            close(fd);
            *len = g_pin.len; return g_pin.data;
        }
    }
    if (pin_cache_load(cpath, path, flen, PIN_MAGIC)) {
        close(fd);
        if (g_pin_strip_req) pin_strip();
        *len = g_pin.len; return g_pin.data;
    }
    int nstub = 0;
    long size = flen > 0 ? pin_build(fd, flen, NULL, NULL, &nstub) : -1;
    unsigned char *out = size > 0 ? (unsigned char *)malloc((size_t)size) : NULL;
    ResPixStub *stub = nstub ? (ResPixStub *)malloc((size_t)nstub * sizeof *stub) : NULL;
    int n2 = 0;
    if (!out || (nstub && !stub) || pin_build(fd, flen, out, stub, &n2) != size || n2 != nstub) {
        close(fd); free(out); free(stub);
        return NULL;
    }
    close(fd);
    snprintf(g_pin.path, sizeof g_pin.path, "%s", path);
    g_pin.data = out; g_pin.len = size; g_pin.stub = stub; g_pin.nstub = nstub;
    g_pin_flen = flen; g_pin_gen++;
    printf("bundle pinned: %s %ld MB -> %ld MB in memory (%d pixel blocks on demand)\n",
           path, flen >> 20, size >> 20, nstub);
    pin_cache_save(cpath, flen, PIN_MAGIC);   /* the full copy: a later conversion needs it */
    if (g_pin_strip_req) pin_strip();
    *len = g_pin.len;
    return g_pin.data;
}

long res_bundle_file_len(void) { return g_pin_flen; }

#ifdef __vita__
static void pix_fd_close(void);
#endif
void res_unpin(void) {
    if (!g_pin.data && !g_pin.stub) return;
#ifdef __vita__
    pix_fd_close();   /* the pixel reads' handle is on the old bundle's file */
#endif
    printf("bundle unpinned: %s%c", g_pin.path, 10);
    free(g_pin.data); free(g_pin.stub);
    memset(&g_pin, 0, sizeof g_pin);
    g_pin_flen = 0;
    g_pin_strip_req = 0;
    free(g_tpk_memo); g_tpk_memo = NULL; g_tpk_memo_n = 0;   /* offsets into the old copy */
    g_pin_gen++;
}
unsigned res_pin_gen(void) { return g_pin_gen; }

int res_is_pinned(const void *data) {
    return data && data == g_pin.data;
}

/* nfsu2.h hooks: an emptied pixel block inside the pinned buffer, and a
   read of its original bytes from the file (thread-safe: own descriptor). */
int n2_pix_stub(const unsigned char *d, long hdr, long *foff, long *flen) {
    if (!g_pin.data || d != g_pin.data) return 0;
    int lo = 0, hi = g_pin.nstub - 1;
    while (lo <= hi) {                       /* stubs are in buffer order */
        int mid = (lo + hi) / 2;
        if (g_pin.stub[mid].coff == hdr) {
            *foff = g_pin.stub[mid].foff; *flen = g_pin.stub[mid].flen;
            return 1;
        }
        if (g_pin.stub[mid].coff < hdr) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* The pinned bundle never changes, so its TPK block list is computed once
   (first world load, main thread, before any worker exists) and handed out
   as fresh copies that callers free as usual. */
/* (declared above pin_strip) */
int n2_tpk_memo_get(const unsigned char *d, void **blk, int *nblk) {
    if (!g_tpk_memo || !g_pin.data || d != g_pin.data) return 0;
    size_t sz = (size_t)g_tpk_memo_n * g_tpk_memo_elem;
    void *copy = malloc(sz ? sz : 1);
    if (!copy) return 0;
    memcpy(copy, g_tpk_memo, sz);
    *blk = copy; *nblk = g_tpk_memo_n;
    return 1;
}
void n2_tpk_memo_put(const unsigned char *d, const void *blk, int nblk, size_t elem) {
    if (g_tpk_memo || !g_pin.data || d != g_pin.data || nblk <= 0) return;
    void *copy = malloc((size_t)nblk * elem);
    if (!copy) return;
    memcpy(copy, blk, (size_t)nblk * elem);
    g_tpk_memo_elem = elem; g_tpk_memo_n = nblk; g_tpk_memo = copy;
}

/* One bundle handle for every pixel read. Opening a file on the card costs
   ~10 ms, which used to be paid for each texture. sceIoPread carries its own
   offset, so the worker and the main thread can share the handle. */
static volatile SceUID g_pix_fd = -1;
static void pix_fd_close(void) {
    SceUID fd = g_pix_fd;
    if (fd >= 0 && __sync_bool_compare_and_swap(&g_pix_fd, fd, -1)) sceIoClose(fd);
}
/* read statistics for the boot.txt texture line (worker reads dominate) */
volatile long n2_pix_stat_reads, n2_pix_stat_bytes;
volatile unsigned long long n2_pix_stat_us;
unsigned long long sceKernelGetProcessTimeWide(void);
static int n2_pix_read_impl(long foff, void *dst, long n);

int n2_pix_read(long foff, void *dst, long n) {
    unsigned long long t0 = sceKernelGetProcessTimeWide();
    int ok = n2_pix_read_impl(foff, dst, n);
    __sync_fetch_and_add(&n2_pix_stat_reads, 1);
    __sync_fetch_and_add(&n2_pix_stat_bytes, n);
    n2_pix_stat_us += sceKernelGetProcessTimeWide() - t0;
    return ok;
}

static int n2_pix_read_impl(long foff, void *dst, long n) {
    if (!g_pin.data || n < 0) return 0;
    SceUID pfd = g_pix_fd;
    if (pfd < 0) {
        SceUID nfd = sceIoOpen(g_pin.path, SCE_O_RDONLY, 0);
        if (nfd >= 0) {
            if (__sync_bool_compare_and_swap(&g_pix_fd, -1, nfd)) pfd = nfd;
            else { sceIoClose(nfd); pfd = g_pix_fd; }
        }
    }
    if (pfd >= 0) {
        long got = 0;
        while (got < n) {
            long want = n - got > (4L << 20) ? (4L << 20) : n - got;
            int k = sceIoPread(pfd, (unsigned char *)dst + got, (SceSize)want, (SceOff)(foff + got));
            if (k <= 0) break;
            got += k;
        }
        if (got == n) return 1;
    }
    int fd = open(g_pin.path, O_RDONLY);   /* fallback: the old per-read path */
    if (fd < 0) return 0;
    int ok = pin_pread(fd, foff, dst, n);
    close(fd);
    return ok;
}
#endif

static char res_kept_path[1024];
static unsigned char *res_kept_data;
static long res_kept_len;

void res_drop(void) {
    free(res_kept_data);
    res_kept_data = NULL;
    res_kept_len = 0;
    res_kept_path[0] = 0;
}

void res_keep(const char *path, unsigned char *data, long len) {
    res_drop();
    if (!path || !data) { free(data); return; }
    snprintf(res_kept_path, sizeof res_kept_path, "%s", path);
    res_kept_data = data;
    res_kept_len = len;
}

unsigned char *res_take(const char *path, long *len) {
    if (!res_kept_data || !path || strcasecmp(path, res_kept_path)) return NULL;
    unsigned char *d = res_kept_data;
    *len = res_kept_len;
    res_kept_data = NULL;
    res_kept_len = 0;
    res_kept_path[0] = 0;
    return d;
}

/* ---- top-level chunk index (res_chunk_index) ---------------------------- */
#include <SDL.h>
#define CIX_MAGIC "OUG2CI01"
#define CIX_SLOTS 4
static struct { char path[512]; long flen; ResChunk *c; int n; unsigned use; } g_cix[CIX_SLOTS];
static unsigned g_cix_clock;
static SDL_atomic_t g_cix_lock;   /* spin lock (the Vita SDL shim has no SDL_SpinLock) */
static void cix_lock(void) { while (!SDL_AtomicCAS(&g_cix_lock, 0, 1)) SDL_Delay(0); }
static void cix_unlock(void) { SDL_AtomicSet(&g_cix_lock, 0); }

static void cix_cache_path(const char *path, char *out, size_t cap) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, cap, "ux0:data/NFSU2/cache/%s.idx", base);
}

static ResChunk *cix_build(const char *path, long flen, int *count) {
    char cpath[600];
    cix_cache_path(path, cpath, sizeof cpath);
    FILE *cf = fopen(cpath, "rb");
    if (cf) {   /* magic, source length, count, entries */
        char magic[8]; int32_t hdr[2];
        ResChunk *c = NULL;
        if (fread(magic, 1, 8, cf) == 8 && !memcmp(magic, CIX_MAGIC, 8) &&
            fread(hdr, sizeof hdr, 1, cf) == 1 && hdr[0] == (int32_t)flen && hdr[1] > 0 &&
            (c = (ResChunk *)malloc((size_t)hdr[1] * sizeof *c)) != NULL &&
            fread(c, sizeof *c, (size_t)hdr[1], cf) == (size_t)hdr[1]) {
            fclose(cf);
            *count = hdr[1];
            return c;
        }
        free(c);
        fclose(cf);
    }
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Uint32 t0 = SDL_GetTicks();
    int n = 0, cap = 0;
    ResChunk *c = NULL;
    long off = 0;
    unsigned char h[8];
    /* the loaders' walk: every header from 0 until one cannot be read */
    while (fseek(f, off, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8) {
        if (n == cap) {
            cap = cap ? cap * 2 : 4096;
            ResChunk *g = (ResChunk *)realloc(c, (size_t)cap * sizeof *c);
            if (!g) { free(c); fclose(f); return NULL; }
            c = g;
        }
        uint32_t sz = (uint32_t)h[4] | (uint32_t)h[5] << 8 | (uint32_t)h[6] << 16 | (uint32_t)h[7] << 24;
        c[n].id = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
        c[n].size = sz; c[n].off = (uint32_t)off;
        n++;
        off += 8 + (long)sz;
    }
    fclose(f);
    if (!n) { free(c); return NULL; }
    mkdir("ux0:data/NFSU2/cache", 0777);
    char tmp[620]; snprintf(tmp, sizeof tmp, "%s.tmp", cpath);
    FILE *wf = fopen(tmp, "wb");
    if (wf) {
        int32_t hdr[2] = { (int32_t)flen, n };
        int ok = fwrite(CIX_MAGIC, 1, 8, wf) == 8 && fwrite(hdr, sizeof hdr, 1, wf) == 1 &&
                 fwrite(c, sizeof *c, (size_t)n, wf) == (size_t)n;
        ok = fclose(wf) == 0 && ok;
        remove(cpath);
        if (!ok || rename(tmp, cpath)) remove(tmp);
    }
    printf("chunk index: %s, %d chunks in %u ms (cached)\n", path, n, SDL_GetTicks() - t0);
    *count = n;
    return c;
}

ResChunk *res_chunk_index(const char *path, int *count) {
    if (count) *count = 0;
    if (!path || !count || strlen(path) >= sizeof g_cix[0].path) return NULL;
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 8) return NULL;
    long flen = (long)st.st_size;
    cix_lock();
    int s = -1;
    for (int i = 0; i < CIX_SLOTS; i++)
        if (g_cix[i].c && g_cix[i].flen == flen && !strcmp(g_cix[i].path, path)) { s = i; break; }
    if (s < 0) {
        int n = 0;
        ResChunk *c = cix_build(path, flen, &n);   /* under the lock: one walk per file */
        if (!c) { cix_unlock(); return NULL; }
        s = 0;
        for (int i = 1; i < CIX_SLOTS; i++) if (g_cix[i].use < g_cix[s].use) s = i;
        free(g_cix[s].c);
        snprintf(g_cix[s].path, sizeof g_cix[s].path, "%s", path);
        g_cix[s].flen = flen; g_cix[s].c = c; g_cix[s].n = n;
    }
    g_cix[s].use = ++g_cix_clock;
    ResChunk *out = (ResChunk *)malloc((size_t)g_cix[s].n * sizeof *out);
    if (out) { memcpy(out, g_cix[s].c, (size_t)g_cix[s].n * sizeof *out); *count = g_cix[s].n; }
    cix_unlock();
    return out;
}

int res_chunk_find(const ResChunk *ix, int n, long off) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if ((long)ix[m].off == off) return m;
        if ((long)ix[m].off < off) lo = m + 1; else hi = m - 1;
    }
    return -1;
}
