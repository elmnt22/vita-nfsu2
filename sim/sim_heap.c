/* sim_heap.c -- the Vita's memory layout, modelled in the simulator.
 *
 * Every game malloc / calloc / realloc / free (macros in sim_pre.h) still gets
 * PC memory, and is also placed in a model of the console:
 *   - the newlib heap: 115 MB, 8-byte chunks with a 4-byte header, best fit,
 *     immediate coalescing, the top chunk trimmed back (brk = end of the
 *     highest live chunk), so holes and brk creep behave as on the Vita;
 *   - kernel big blocks: page-rounded, refused when the kernel would keep less
 *     than the 12 MB reserve, with the same routing and fallbacks as the
 *     __wrap_* functions in vita/sdl_vita.c (BIGBLK_MIN, HEAP_FALLBACK_MIN).
 * vita_heap_report() in sim_vita.c prints the model, with the call sites that
 * hold the most heap and the one holding brk up. A long drive in the sim then
 * shows fragmentation without a run on the console.
 *
 *   OPENUG2_SIM_HEAP=1           model on (default off: plain malloc)
 *   OPENUG2_SIM_KERNEL_MB=134    kernel memory big blocks can use (+ reserve)
 *   OPENUG2_SIM_BIGMIN_KB=64     try other big-block thresholds (default = BIGBLK_MIN)
 *   OPENUG2_SIM_HEAP_FAIL=1      a request the model cannot place returns NULL
 */
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SH_HEAP_BYTES   (115u * 1024u * 1024u)
#define SH_UNITS        (SH_HEAP_BYTES / 8u)
#define SH_RESERVE      (12u * 1024u * 1024u)
#define SH_FALLBACK_MIN (16u * 1024u)
#define T_INUSE 1u
#define T_PREVFREE 2u
#define T_SIZE(t) ((t) >> 2)

static int sh_on = -1, sh_fail;
static size_t sh_bigmin = 64u * 1024u, sh_kernel = 134u * 1024u * 1024u;
/* a blocking lock: a spin with Sleep(0) livelocked at boot (threads pinned
   to one core at different priorities) */
static SRWLOCK sh_srw = SRWLOCK_INIT;
static void sh_lock(void) { AcquireSRWLockExclusive(&sh_srw); }
static void sh_unlock(void) { ReleaseSRWLockExclusive(&sh_srw); }

/* ---- heap model ------------------------------------------------------------ */
static uint32_t *tag;            /* per 8-byte unit: header at a chunk's start; a free chunk's last unit = its node */
static uint32_t top, top_peak;   /* units */
static size_t live_heap, live_heap_peak;
typedef struct { uint32_t start, size, prev, next; } FNode;
static FNode *nodes; static uint32_t nnodes, capnodes, freenode = 0;
/* exact bins below 512 B, then 32 per power of two (sizes within ~3%), with
   a bitmap of non-empty bins: a request looks at its own bin's first few
   chunks, then takes any chunk of the next non-empty bin */
#define NB (64 + 19 * 32)
static uint32_t head[NB];        /* node index + 1, 0 = empty */
static uint32_t nonempty[(NB + 31) / 32];

static int bin_of(uint32_t units) {
    uint32_t bytes = units * 8u;
    if (bytes < 512) return (int)units;                 /* exact */
    int fl = 31 - __builtin_clz(bytes);
    int b = 64 + (fl - 9) * 32 + (int)((bytes >> (fl - 5)) & 31);
    return b < NB ? b : NB - 1;
}
static int next_bin(int b) {   /* first non-empty bin >= b, or -1 */
    if (b >= NB) return -1;
    int w = b >> 5; uint32_t m = nonempty[w] & (~0u << (b & 31));
    while (!m) { if (++w >= (NB + 31) / 32) return -1; m = nonempty[w]; }
    return w * 32 + __builtin_ctz(m);
}
static uint32_t node_new(void) {
    if (freenode) { uint32_t i = freenode - 1; freenode = nodes[i].next; return i; }
    if (nnodes == capnodes) {
        capnodes = capnodes ? capnodes * 2 : 65536;
        nodes = (FNode *)realloc(nodes, capnodes * sizeof *nodes);
    }
    return nnodes++;
}
static void bin_insert(uint32_t start, uint32_t size) {
    uint32_t i = node_new(); int b = bin_of(size);
    nodes[i].start = start; nodes[i].size = size; nodes[i].prev = 0; nodes[i].next = head[b];
    if (head[b]) nodes[head[b] - 1].prev = i + 1;
    head[b] = i + 1; nonempty[b >> 5] |= 1u << (b & 31);
    tag[start] = size << 2;   /* a free chunk's predecessor is always in use (coalesced); the unit may hold stale bits */
    tag[start + size - 1] = i;
    if (start + size < top) tag[start + size] |= T_PREVFREE;
}
static void bin_remove(uint32_t i) {
    FNode *n = &nodes[i]; int b = bin_of(n->size);
    if (n->prev) nodes[n->prev - 1].next = n->next;
    else if (!(head[b] = n->next)) nonempty[b >> 5] &= ~(1u << (b & 31));
    if (n->next) nodes[n->next - 1].prev = n->prev;
    if (n->start + n->size < top) tag[n->start + n->size] &= ~T_PREVFREE;
    n->next = freenode; freenode = i + 1;
}
static uint32_t req_units(size_t n) {   /* newlib request2size: +4 header, 8-aligned, min 16 */
    size_t b = (n + 4 + 7) & ~(size_t)7;
    if (b < 16) b = 16;
    return (uint32_t)(b / 8);
}
/* place r units: best fit, else the top; returns start or UINT32_MAX */
static uint32_t heap_alloc(uint32_t r) {
    uint32_t best = 0; int found = 0;
    int b = bin_of(r);
    if (head[b]) {   /* own bin: the smallest fit among its first chunks */
        uint32_t bs = 0xFFFFFFFFu; int seen = 0;
        for (uint32_t k = head[b]; k && seen < 32; k = nodes[k - 1].next, seen++) {
            FNode *n = &nodes[k - 1];
            if (n->size >= r && n->size < bs) { bs = n->size; best = k - 1; if (bs == r) break; }
        }
        found = bs != 0xFFFFFFFFu;
    }
    if (!found && (b = next_bin(b + 1)) >= 0) { best = head[b] - 1; found = 1; }   /* every chunk there fits */
    uint32_t s;
    if (found) {
        FNode n = nodes[best];
        bin_remove(best);
        s = n.start;
        uint32_t keep = tag[s] & T_PREVFREE;
        if (n.size - r >= 2) {
            tag[s] = (r << 2) | T_INUSE | keep;
            bin_insert(s + r, n.size - r);
        } else {
            r = n.size;
            tag[s] = (r << 2) | T_INUSE | keep;
        }
    } else {
        if ((uint64_t)top + r > SH_UNITS) return 0xFFFFFFFFu;
        s = top; top += r;
        tag[s] = (r << 2) | T_INUSE;
        if (top > top_peak) top_peak = top;
    }
    live_heap += (size_t)T_SIZE(tag[s]) * 8;
    if (live_heap > live_heap_peak) live_heap_peak = live_heap;
    return s;
}
static void heap_free(uint32_t s) {
    uint32_t n = T_SIZE(tag[s]);
    live_heap -= (size_t)n * 8;
    if (tag[s] & T_PREVFREE) {
        uint32_t pi = tag[s - 1]; uint32_t ps = nodes[pi].start, pn = nodes[pi].size;
        bin_remove(pi);
        s = ps; n += pn;
    }
    uint32_t e = s + n;
    if (e < top && !(tag[e] & T_INUSE)) {
        uint32_t en = T_SIZE(tag[e]);
        uint32_t ni = tag[e + en - 1];
        bin_remove(ni);
        n += en;
    }
    if (s + n == top) { top = s; tag[s] = 0; return; }   /* back into the top chunk (trimmed) */
    tag[s] &= T_PREVFREE;   /* keep only "prev free" (prev is in use here, so 0) */
    bin_insert(s, n);
}
/* in place: shrink, or grow into a free neighbour above / the top */
static int heap_resize(uint32_t s, uint32_t r) {
    uint32_t n = T_SIZE(tag[s]), keep = tag[s] & T_PREVFREE;
    if (r <= n) {
        if (n - r >= 2) {
            tag[s] = (r << 2) | T_INUSE | keep;
            live_heap -= (size_t)(n - r) * 8;
            uint32_t rs = s + r, rn = n - r;
            /* the remainder joins whatever is free above it */
            tag[rs] = (rn << 2) | T_INUSE; live_heap += (size_t)rn * 8;
            heap_free(rs);
        }
        return 1;
    }
    uint32_t e = s + n;
    if (e == top) {
        if ((uint64_t)s + r > SH_UNITS) return 0;
        live_heap += (size_t)(r - n) * 8; top = s + r;
        if (top > top_peak) top_peak = top;
        tag[s] = (r << 2) | T_INUSE | keep;
        return 1;
    }
    if (e < top && !(tag[e] & T_INUSE)) {
        uint32_t en = T_SIZE(tag[e]);
        if (n + en >= r) {
            bin_remove(tag[e + en - 1]);
            uint32_t tot = n + en;
            if (tot - r >= 2) {
                tag[s] = (r << 2) | T_INUSE | keep;
                bin_insert(s + r, tot - r);
            } else {
                r = tot; tag[s] = (r << 2) | T_INUSE | keep;
                if (s + r < top) tag[s + r] &= ~T_PREVFREE;
            }
            live_heap += (size_t)(r - n) * 8;
            if (live_heap > live_heap_peak) live_heap_peak = live_heap;
            return 1;
        }
    }
    return 0;
}

/* ---- kernel big blocks ----------------------------------------------------- */
static size_t big_live, big_peak, spill_live, spill_peak; static int big_count, big_fail, heap_oom;
static long big_calls, big_calls_main;   /* kernel block allocations: all threads / the first (frame) thread */
static DWORD main_tid;
static int big_take(size_t n, size_t *got) {
    size_t size = (n + 0xFFF) & ~(size_t)0xFFF;
    if (big_live + size + SH_RESERVE > sh_kernel) { big_fail++; return 0; }
    big_live += size; big_count++; big_calls++;
    if (GetCurrentThreadId() == main_tid) big_calls_main++;
    if (big_live > big_peak) big_peak = big_live;
    *got = size;
    return 1;
}

/* ---- pointer map ----------------------------------------------------------- */
typedef struct { void *p; uint32_t start; uint32_t big; size_t req; void *caller; } Rec;   /* big = pages*4K or 0 */
static Rec *map; static uint32_t mapcap, mapused, maptomb;
#define TOMB ((void *)1)
static uint32_t hidx(void *p) { return (uint32_t)(((uintptr_t)p >> 3) * 2654435761u); }
static Rec *map_find(void *p) {
    if (!map) return NULL;
    for (uint32_t i = hidx(p) & (mapcap - 1);; i = (i + 1) & (mapcap - 1)) {
        if (!map[i].p) return NULL;
        if (map[i].p == p) return &map[i];
    }
}
static void map_put(const Rec *r);
static void map_grow(void) {
    Rec *old = map; uint32_t oc = mapcap;
    mapcap = mapcap ? (mapused * 4 > mapcap ? mapcap * 2 : mapcap) : (1u << 20);
    map = (Rec *)calloc(mapcap, sizeof *map); mapused = maptomb = 0;
    for (uint32_t i = 0; i < oc; i++) if (old[i].p && old[i].p != TOMB) map_put(&old[i]);
    free(old);
}
static void map_put(const Rec *r) {
    if (!map || (mapused + maptomb + 1) * 2 > mapcap) map_grow();
    for (uint32_t i = hidx(r->p) & (mapcap - 1);; i = (i + 1) & (mapcap - 1))
        if (!map[i].p || map[i].p == TOMB) { if (map[i].p == TOMB) maptomb--; map[i] = *r; mapused++; return; }
}
static void map_del(Rec *r) { r->p = TOMB; mapused--; maptomb++; }

static void sh_init(void) {
    const char *e = getenv("OPENUG2_SIM_HEAP");
    sh_on = e && e[0] == '1';   /* opt-in: plain malloc unless asked */
    if ((e = getenv("OPENUG2_SIM_KERNEL_MB"))) sh_kernel = (size_t)atoi(e) << 20;
    if ((e = getenv("OPENUG2_SIM_BIGMIN_KB"))) sh_bigmin = (size_t)atoi(e) << 10;
    sh_fail = getenv("OPENUG2_SIM_HEAP_FAIL") != NULL;
    main_tid = GetCurrentThreadId();
    if (sh_on) {
        tag = (uint32_t *)VirtualAlloc(NULL, (size_t)SH_UNITS * 4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!tag) sh_on = 0;
    }
}

/* the model's placement for a new block of n bytes: 0 = could not place */
static int place(void *p, size_t n, void *caller) {
    Rec r; memset(&r, 0, sizeof r); r.p = p; r.req = n; r.caller = caller;
    size_t got = 0; int ok = 0;
    if (n >= sh_bigmin && big_take(n, &got)) { r.big = (uint32_t)got; ok = 1; }
    if (!ok) {
        uint32_t s = heap_alloc(req_units(n));
        if (s != 0xFFFFFFFFu) {
            r.start = s; ok = 1;
            if (n >= sh_bigmin) { spill_live += n; if (spill_live > spill_peak) spill_peak = spill_live; }
        }
    }
    if (!ok && n >= SH_FALLBACK_MIN && big_take(n, &got)) { r.big = (uint32_t)got; ok = 1; }
    if (!ok) { heap_oom++; return 0; }
    map_put(&r);
    return 1;
}
static void release(const Rec *r) {
    if (r->big) { big_live -= r->big; big_count--; }
    else { if (r->req >= sh_bigmin) spill_live -= r->req; heap_free(r->start); }
}
static void unplace(Rec *r) { release(r); map_del(r); }

static void oom_line(const char *fn, size_t n, void *caller) {
    printf("simheap: OUT OF MEMORY %s(%u KB) by %p, brk %u MB, live %u MB, big %u MB\n", fn, (unsigned)(n >> 10),
           caller, (unsigned)((size_t)top * 8 >> 20), (unsigned)(live_heap >> 20), (unsigned)(big_live >> 20));
}

void *sim_malloc_at(size_t n, void *caller) {
    if (sh_on < 0) sh_init();
    void *p = malloc(n ? n : 1);
    if (!sh_on || !p) return p;
    sh_lock();
    int ok = place(p, n, caller);
    sh_unlock();
    if (!ok) { oom_line("malloc", n, caller); if (sh_fail) { free(p); return NULL; } }
    return p;
}
void *sim_calloc_at(size_t c, size_t n, void *caller) {
    if (sh_on < 0) sh_init();
    if (c && n > (size_t)-1 / c) return NULL;
    void *p = calloc(c ? c : 1, n ? n : 1);
    if (!sh_on || !p) return p;
    sh_lock();
    int ok = place(p, c * n, caller);
    sh_unlock();
    if (!ok) { oom_line("calloc", c * n, caller); if (sh_fail) { free(p); return NULL; } }
    return p;
}
void sim_free(void *p) {
    if (!p) return;
    if (sh_on > 0) {
        sh_lock();
        Rec *r = map_find(p);
        if (r) unplace(r);
        sh_unlock();
    }
    free(p);
}
/* the routing of __wrap_realloc: a kernel block stays while it is big enough,
   a heap chunk grows in place when it can, out to a kernel block at BIGBLK_MIN */
void *sim_realloc_at(void *q, size_t n, void *caller) {
    if (sh_on < 0) sh_init();
    if (!q) return sim_malloc_at(n, caller);
    if (!n) { sim_free(q); return NULL; }
    if (!sh_on) return realloc(q, n);
    sh_lock();
    Rec *r = map_find(q), old;
    if (!r) { sh_unlock(); return realloc(q, n); }
    old = *r;
    int moved_model = 0, ok = 1;
    if (old.big) {
        if (n <= old.big) r->req = n;
        else moved_model = 1;
    } else {
        size_t usable = (size_t)T_SIZE(tag[old.start]) * 8 - 4;
        if (n > usable && n >= sh_bigmin) moved_model = 1;
        else if (heap_resize(old.start, req_units(n))) {
            if (old.req >= sh_bigmin) spill_live -= old.req;
            if (n >= sh_bigmin) { spill_live += n; if (spill_live > spill_peak) spill_peak = spill_live; }
            r->req = n;
        } else moved_model = 1;
    }
    sh_unlock();
    void *p = realloc(q, n);
    if (!p) return NULL;
    sh_lock();
    if (moved_model) {   /* the new block is placed before the old one goes, as on the Vita */
        Rec *o = map_find(q), oc = *o;
        map_del(o);
        ok = place(p, n, caller);
        release(&oc);
    } else if (p != q) {
        Rec *o = map_find(q); Rec nr = *o; map_del(o); nr.p = p; map_put(&nr);
    }
    sh_unlock();
    if (!ok) oom_line("realloc", n, caller);
    return p;
}
char *sim_strdup_at(const char *s, void *caller) {
    size_t n = strlen(s) + 1;
    char *d = (char *)sim_malloc_at(n, caller);
    if (d) memcpy(d, s, n);
    return d;
}
/* what sim_pre.h's macros call: the return address is the game's call site */
__attribute__((noinline)) void *sim_malloc(size_t n) { return sim_malloc_at(n, __builtin_return_address(0)); }
__attribute__((noinline)) void *sim_calloc(size_t c, size_t n) { return sim_calloc_at(c, n, __builtin_return_address(0)); }
__attribute__((noinline)) void *sim_realloc(void *q, size_t n) { return sim_realloc_at(q, n, __builtin_return_address(0)); }
__attribute__((noinline)) char *sim_strdup(const char *s) { return sim_strdup_at(s, __builtin_return_address(0)); }

/* walks every chunk below brk: headers, free-chunk footers, coalescing and
   the live total must agree (the model's own test, sim/test bench) */
int sim_heap_check(void) {
    sh_lock();
    size_t live = 0; uint32_t nfree = 0, binned = 0; int prev_free = 0, bad = 0;
    for (uint32_t s = 0; s < top && !bad; ) {
        uint32_t t = tag[s], n = T_SIZE(t);
        if (n < 2 || s + n > top) { printf("check: bad size %u at %u\n", n, s); bad = 1; break; }
        if (!!(t & T_PREVFREE) != prev_free) { printf("check: prevfree bit wrong at %u\n", s); bad = 1; }
        if (t & T_INUSE) { live += (size_t)n * 8; prev_free = 0; }
        else {
            uint32_t i = tag[s + n - 1];
            if (prev_free) { printf("check: two free chunks in a row at %u\n", s); bad = 1; }
            if (i >= nnodes || nodes[i].start != s || nodes[i].size != n) { printf("check: footer node wrong at %u\n", s); bad = 1; }
            nfree++; prev_free = 1;
        }
        s += n;
    }
    if (prev_free) { printf("check: free chunk at the top\n"); bad = 1; }
    for (int b = 0; b < NB; b++) {
        if (!!head[b] != !!(nonempty[b >> 5] & (1u << (b & 31)))) { printf("check: bitmap wrong for bin %d\n", b); bad = 1; }
        for (uint32_t k = head[b]; k; k = nodes[k - 1].next) {
            binned++;
            if (bin_of(nodes[k - 1].size) != b) { printf("check: chunk in wrong bin %d\n", b); bad = 1; }
        }
    }
    if (!bad && live != live_heap) { printf("check: live %zu vs counted %zu\n", live, live_heap); bad = 1; }
    if (!bad && binned != nfree) { printf("check: %u binned vs %u free\n", binned, nfree); bad = 1; }
    sh_unlock();
    return !bad;
}

/* ---- report ------------------------------------------------------------------ */
static void sh_name(void *a, char *out, size_t cap) {
    static int sym;
    if (!sym) { SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS); SymInitialize(GetCurrentProcess(), NULL, TRUE); sym = 1; }
    char buf[sizeof(SYMBOL_INFO) + 128]; SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    memset(buf, 0, sizeof buf); si->SizeOfStruct = sizeof *si; si->MaxNameLen = 127;
    DWORD64 off = 0; DWORD loff = 0; IMAGEHLP_LINE64 ln; memset(&ln, 0, sizeof ln); ln.SizeOfStruct = sizeof ln;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)a, &off, si)) { snprintf(out, cap, "%p", a); return; }
    if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)(uintptr_t)a, &loff, &ln)) {
        const char *f = strrchr(ln.FileName, '\\'); f = f ? f + 1 : ln.FileName;
        snprintf(out, cap, "%s %s:%lu", si->Name, f, ln.LineNumber);
    } else snprintf(out, cap, "%s+0x%llx", si->Name, off);
}

typedef struct { void *caller; size_t bytes; uint32_t count; } Agg;
static int agg_cmp(const void *a, const void *b) {
    size_t x = ((const Agg *)a)->bytes, y = ((const Agg *)b)->bytes;
    return x < y ? 1 : x > y ? -1 : 0;
}
int sim_heap_on(void) { if (sh_on < 0) sh_init(); return sh_on; }
unsigned sim_heap_brk_mb(void) { return (unsigned)((size_t)top * 8 >> 20); }
unsigned sim_heap_room_mb(void) {
    size_t used = big_live + SH_RESERVE;
    return used >= sh_kernel ? 0 : (unsigned)((sh_kernel - used) >> 20);
}
size_t sim_heap_big_live(void) { return big_live; }
void sim_heap_big_report(char *out, size_t cap) {
    snprintf(out, cap, "big blocks %d live, %u MB (peak %u MB), %d kernel refusals; in heap %u MB (peak %u MB)",
             big_count, (unsigned)(big_live >> 20), (unsigned)(big_peak >> 20), big_fail,
             (unsigned)(spill_live >> 20), (unsigned)(spill_peak >> 20));
}
/* detail: 0 = one line, 1 = + holes by size, 2 = + top call sites and brk holder */
void sim_heap_report(const char *label, int detail) {
    if (sim_heap_on() <= 0) return;
    sh_lock();
    size_t holes = (size_t)top * 8 - live_heap, largest = 0;
    size_t hb[6] = { 0 }; uint32_t hc[6] = { 0 };   /* <1K <16K <64K <256K <1M >=1M */
    for (int b = 0; b < NB; b++)
        for (uint32_t k = head[b]; k; k = nodes[k - 1].next) {
            size_t sz = (size_t)nodes[k - 1].size * 8;
            if (sz > largest) largest = sz;
            int c = sz < 1024 ? 0 : sz < 16384 ? 1 : sz < 65536 ? 2 : sz < 262144 ? 3 : sz < 1048576 ? 4 : 5;
            hb[c] += sz; hc[c]++;
        }
    printf("simheap %s: live %u MB (peak %u), holes %u MB (largest %u KB), brk %u MB (peak %u) of 115 | "
           "big %d live, %u MB (peak %u), %d refusals, %ld kernel allocs (%ld main); spill %u MB (peak %u); model OOM %d\n",
           label, (unsigned)(live_heap >> 20), (unsigned)(live_heap_peak >> 20), (unsigned)(holes >> 20),
           (unsigned)(largest >> 10), (unsigned)((size_t)top * 8 >> 20), (unsigned)((size_t)top_peak * 8 >> 20),
           big_count, (unsigned)(big_live >> 20), (unsigned)(big_peak >> 20), big_fail, big_calls, big_calls_main,
           (unsigned)(spill_live >> 20), (unsigned)(spill_peak >> 20), heap_oom);
    if (detail >= 1)
        printf("simheap   holes by size: <1K %u MB (%u) | <16K %u MB (%u) | <64K %u MB (%u) | <256K %u MB (%u) | "
               "<1M %u MB (%u) | >=1M %u MB (%u)\n",
               (unsigned)(hb[0] >> 20), hc[0], (unsigned)(hb[1] >> 20), hc[1], (unsigned)(hb[2] >> 20), hc[2],
               (unsigned)(hb[3] >> 20), hc[3], (unsigned)(hb[4] >> 20), hc[4], (unsigned)(hb[5] >> 20), hc[5]);
    Agg *ag = NULL; uint32_t nag = 0; void *top_holder = NULL; size_t top_req = 0; uint32_t best_end = 0;
    if (detail >= 2 && map) {
        uint32_t cap = 8192; ag = (Agg *)calloc(cap, sizeof *ag);
        for (uint32_t i = 0; i < mapcap; i++) {
            Rec *r = &map[i];
            if (!r->p || r->p == TOMB || r->big) continue;
            uint32_t end = r->start + T_SIZE(tag[r->start]);
            if (end > best_end) { best_end = end; top_holder = r->caller; top_req = r->req; }
            uint32_t h = (uint32_t)(((uintptr_t)r->caller >> 2) * 2654435761u) & (cap - 1);
            while (ag[h].caller && ag[h].caller != r->caller) h = (h + 1) & (cap - 1);
            ag[h].caller = r->caller; ag[h].bytes += (size_t)T_SIZE(tag[r->start]) * 8; ag[h].count++;
        }
        for (uint32_t i = 0; i < cap; i++) if (ag[i].caller) ag[nag++] = ag[i];
    }
    sh_unlock();
    if (ag) {
        qsort(ag, nag, sizeof *ag, agg_cmp);
        char nm[256];
        for (uint32_t i = 0; i < nag && i < 14; i++) {
            sh_name(ag[i].caller, nm, sizeof nm);
            printf("simheap   %6u KB in %6u: %s\n", (unsigned)(ag[i].bytes >> 10), ag[i].count, nm);
        }
        sh_name(top_holder, nm, sizeof nm);
        printf("simheap   brk held by a %u B block from %s\n", (unsigned)top_req, nm);
        free(ag);
    }
}
