/* speech.c — the game's speech: EA SPCH event queue + line picker and the
 * MicroTalk / EA-XA decoders, ported from SPEED2.EXE and GC Speed.elf.
 *
 * Reference notes and the Python reference implementations live in
 * E:/re-tools/out/speech: SPEECH_DATA.md (formats, decoder), SPCH_PICK.md
 * (picker, RNG), SPEECH_GAME.md (when lines play), spch.py / utk.py / eaxa.py.
 * Addresses: GC Speed.elf (SPCH lib 802ef63c.., SpeechModule 80089...),
 * PC SPEED2.EXE (MicroTalk 0x733a33..0x734798, stream feeder 0x732980).
 *
 * Flow (GC names): UG2_SpeechParser::Play* -> Csis call -> iSPCH_AddEvent
 * (16-slot event queue) + SpeechModule::QueStream (only when nothing is
 * already queued); GameSpeech::Update -> SPCH_Play -> SPCH_Choose ->
 * iSPCH_ChooseSentence -> iSPCH_MakeSampleRequests ->
 * SpeechModule::SampleRequestCallback (stream read of the clip). Here the
 * stream is a small FIFO of in-memory clips the audio thread decodes. */
#include "speech.h"
#include "career.h"     /* CSPK_* */
#include "audio.h"
#include "music.h"    /* music_set_io_hook: the clip reads run on its thread */
#include <SDL.h>      /* SDL_Delay: lock()'s back-off */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#ifdef SPEECH_STANDALONE
uint32_t speech_test_now_ms;            /* the test drives the clock */
#define NOW_MS() speech_test_now_ms
#else
#define NOW_MS() SDL_GetTicks()        /* GetTicker: RealTime frames * 1000/60 = ms */
#endif

/* ------------------------------------------------------------------ helpers */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t  s16be(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
#define A4(n) (((n) + 3) & ~3u)

static volatile int g_lock;            /* guards the clip FIFO + decoder (main <-> audio) */
/* The music thread (clip reads) shares cores 1-2 with the higher-priority
   audio thread: a holder it preempts on the same core could leave it
   spinning, so a long wait sleeps a moment to let the holder run. */
static void lock(void) {
    for (int n = 0; __sync_lock_test_and_set(&g_lock, 1); )
        if (++n >= 2000) { n = 0; SDL_Delay(1); }
}
static void unlock(void) { __sync_lock_release(&g_lock); }

/* ------------------------------------------------------------------ data */
#define MAX_BANKS 32
typedef struct {
    uint16_t id, sub;
    uint8_t  k, ns, qlen;
    uint32_t unit;                      /* (hdr+7 + 1) * 256 */
    uint32_t file_base;                 /* viv offset of the .big + idx big_off */
    const uint8_t *hdr;                 /* VOXBANKHDR in the db's .idx copy */
    uint32_t hdr_size;
    uint8_t  queue[1 + 32];             /* play queue: [write idx, qlen entries] (runtime RAM) */
    int      vidx;                      /* index in the sorted gVoxBanks (Rand queue key) */
    int      db;
} SBank;

#define MAX_MATCH 8
#define MAX_PHR   4
#define MAX_SENT  4
typedef struct { uint32_t mask; uint8_t src; } SMatch;
typedef struct { uint16_t bank; uint8_t parm, mode; int nm; SMatch m[MAX_MATCH]; } SPhrase;
typedef struct { uint8_t weight, freq; int nphr; SPhrase ph[MAX_PHR]; } SSent;
typedef struct {
    char name[24];
    uint16_t id, age, prio;
    uint8_t prob, flags, maxrep, nparm;
    int nsent;
    SSent s[MAX_SENT];
} SEvent;

#define MAX_EVENTS 16
typedef struct {
    int ok;
    uint8_t *idx, *evt;
    uint32_t big_off, big_size;
    int datid;                          /* channel */
    SEvent ev[MAX_EVENTS];
    int nev;
} SDb;

enum { DB_SPEECH, DB_NIS, NDB };
static SDb g_db[NDB];
static SBank g_bank[MAX_BANKS];
static int g_nbank;
static SBank *g_sorted[MAX_BANKS];
static FILE *g_f;
static char g_viv_path[600];   /* sdat.viv, for the clip reader's own handle */
static int g_ok;

static int name_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static uint8_t *read_at(uint32_t off, uint32_t size) {
    uint8_t *p = malloc(size ? size : 1);
    if (!p) return NULL;
    if (fseek(g_f, (long)off, SEEK_SET) || fread(p, 1, size, g_f) != size) { free(p); return NULL; }
    return p;
}

/* .evt (VoxDat) -> compact event table; names from the csis table inside the .evt */
static int parse_evt(SDb *db, uint32_t size) {
    const uint8_t *d = db->evt;
    if (size < 0x18 || d[0] != 3 || d[1] != 0x11) return 0;
    uint32_t csis = le32(d + 4);
    int nev = le16(d + 0x10);
    db->datid = d[9];
    if (nev > MAX_EVENTS) nev = MAX_EVENTS;
    db->nev = nev;
    for (int i = 0; i < nev; i++) {
        SEvent *E = &db->ev[i];
        uint32_t e = (uint32_t)le16(d + 0x18 + 2 * i) * 4;
        if (e + 12 > size) return 0;
        E->id = le16(d + e); E->age = le16(d + e + 2); E->prio = le16(d + e + 4);
        int nsent = d[e + 6];
        E->maxrep = d[e + 8] & 15; E->prob = d[e + 9]; E->flags = d[e + 10]; E->nparm = d[e + 11] & 0x7f;
        E->nsent = nsent > MAX_SENT ? MAX_SENT : nsent;
        for (int s = 0; s < E->nsent; s++) {
            uint32_t so = e + (uint32_t)le16(d + e + 12 + 2 * s) * 4;
            SSent *S = &E->s[s];
            S->weight = d[so]; S->freq = d[so + 1];
            int nphr = d[so + 2] >> 2;
            S->nphr = nphr > MAX_PHR ? MAX_PHR : nphr;
            for (int p = 0; p < S->nphr; p++) {
                uint32_t po = so + (uint32_t)d[so + 8 + p] * 4;
                SPhrase *P = &S->ph[p];
                P->bank = le16(d + po); P->parm = d[po + 2]; P->mode = d[po + 3];
                int nm = (int8_t)d[po + 4];
                P->nm = nm < 0 ? 0 : nm > MAX_MATCH ? MAX_MATCH : nm;
                for (int m = 0; m < P->nm; m++) {
                    P->m[m].mask = le32(d + po + 8 + 8 * m);
                    P->m[m].src  = d[po + 8 + 8 * m + 4];
                }
            }
        }
        /* csis entry i: u32 name offset (from the end of the table) */
        uint32_t no = le32(d + csis + 0x20 * i), nm = csis + 0x20 * (uint32_t)nev + no;
        if (nm < size) { snprintf(E->name, sizeof E->name, "%s", (const char *)d + nm); }
    }
    return 1;
}

/* .idx (CLUMP_IDX_FILE) -> banks */
static void parse_idx(int dbi, uint32_t size) {
    SDb *db = &g_db[dbi];
    const uint8_t *x = db->idx;
    if (size < 0x58) return;
    uint32_t n = le32(x + 0x54);
    for (uint32_t i = 0; i < n && g_nbank < MAX_BANKS && 0x58 + i * 16 + 16 <= size; i++) {
        uint32_t hsz = le32(x + 0x58 + i * 16 + 4), hoff = le32(x + 0x58 + i * 16 + 8),
                 boff = le32(x + 0x58 + i * 16 + 12);
        if (hoff + hsz > size || hsz < 12) continue;
        SBank *b = &g_bank[g_nbank++];
        memset(b, 0, sizeof *b);
        b->hdr = x + hoff; b->hdr_size = hsz;
        b->id = le16(b->hdr); b->sub = le16(b->hdr + 2);
        b->k = b->hdr[4] & 0x7f; b->ns = b->hdr[5]; b->qlen = b->hdr[6];
        if (b->qlen > 32) b->qlen = 32;
        b->unit = ((uint32_t)b->hdr[7] + 1) * 256;
        b->file_base = db->big_off + boff;
        b->db = dbi;
    }
}

static void sample_range(const SBank *b, int s, uint32_t *off, uint32_t *size) {
    const uint8_t *r = b->hdr + 0xc + s * (b->k + 2);
    uint32_t st = ((uint32_t)r[0] << 8 | r[1]) * b->unit, en;
    if (s + 1 < b->ns) { const uint8_t *r2 = r + b->k + 2; en = ((uint32_t)r2[0] << 8 | r2[1]) * b->unit; }
    else en = (uint32_t)le16(b->hdr + 8) * b->unit;
    *off = b->file_base + st;
    *size = en > st ? en - st : 0;
}
static const uint8_t *sample_parms(const SBank *b, int s) { return b->hdr + 0xc + s * (b->k + 2) + 2; }
static uint32_t bank_valid_mask(const SBank *b, int j) {
    return le32(b->hdr + A4(b->ns * (b->k + 2) + 0xc) + 4 * j);
}

/* ------------------------------------------------------------------ RNG */
/* iSPCH_EACseedrandom 802f1434 / iSPCH_EACrandom 802f132c / iSPCH_Rand 802f1620 */
static uint32_t g_rx, g_rw[5];          /* seedX, then words 450,454,458,45c,460 */
static struct { uint16_t v, key; } g_rq[32];
static int g_rqi;

static uint32_t eac_random(void) {
    uint32_t a = g_rw[0], b = g_rw[1], c = g_rw[2], d = g_rw[3], e = g_rw[4], x = g_rx, cy;
    uint32_t n1 = e + d; cy = (n1 < e || n1 < d);
    uint32_t n2 = n1 + c + cy; cy = n2 < c;
    uint32_t n3 = n2 + b + cy; cy = n3 < b;
    uint32_t n4 = n3 + a + cy; cy = n4 < a;
    uint32_t nx = n4 + x + cy;
    e = e + 1;
    d = n1; c = n2; b = n3; a = n4; x = nx;
    if (e == 0 && ++d == 0 && ++c == 0 && ++b == 0 && ++a == 0) ++x;
    g_rw[0] = a; g_rw[1] = b; g_rw[2] = c; g_rw[3] = d; g_rw[4] = e; g_rx = x;
    return x;
}
static int rand_find(int val, int depth, int key) {     /* iSPCH_FindRandInQueue 802f1488 */
    if (depth > 32) depth = 32;
    int cnt = 0, i = g_rqi;
    for (int t = 0; t < 33 && cnt < depth; t++) {
        if (g_rq[i].key == (uint16_t)key) {
            if (g_rq[i].v == (uint16_t)val) return cnt;
            cnt++;
        }
        i = i > 0 ? i - 1 : i + 31;
    }
    return -1;
}
static int spch_rand(int n, int key) {
    int depth = n / 2 < 10 ? n / 2 : 10;
    int cand = (int)(((eac_random() >> 16) * (uint32_t)n) >> 16);
    if (key == -1) return cand;
    key &= 0xffff;
    int keep = cand, best = -1, tries = 0;
    for (;;) {
        int pos = rand_find(cand, depth, key), done = 0;
        if (pos == -1) { done = 1; keep = cand; }
        else if (pos > best) { best = pos; keep = cand; }
        if (++tries > 31) done = 1;
        if (done) break;
        cand = (int)(((eac_random() >> 16) * (uint32_t)n) >> 16);
    }
    g_rqi = (g_rqi + 1) & 31;
    g_rq[g_rqi].v = (uint16_t)keep; g_rq[g_rqi].key = (uint16_t)key;
    return cand;                         /* the last candidate (the queue gets 'keep') */
}
static void spch_init_random(uint32_t game_num) {       /* iSPCH_InitRandom 802f1570 */
    uint32_t s = game_num * 0x10001u;
    g_rx = s + 0xf22d0e56u;
    g_rw[0] = s + 0x883126e9u; g_rw[1] = s + 0xc624dd2fu; g_rw[2] = s + 0x0702c49cu;
    g_rw[3] = s + 0x9e353f7du; g_rw[4] = s + 0x6fdf3b64u;
    for (int i = 0; i < 32; i++) { g_rq[i].v = 0xffff; g_rq[i].key = 0xffff; }
    g_rqi = 0;
    int32_t sv = (int32_t)s;
    int warm = 32 - (int)(sv % 32);      /* C signed % */
    for (int i = 0; i < warm; i++) spch_rand(10, -1);
}

/* ------------------------------------------------------------------ event queue */
#define NSLOT 16
#define MAX_PRM 8
typedef struct {
    uint8_t used, ch;
    uint16_t subtick;
    uint32_t tick;
    const SEvent *ev;
    uint32_t prm[1 + MAX_PRM];          /* [0] EventSpec (unused here), [1..] the call args */
} EvSlot;
static EvSlot g_slot[NSLOT];
static int g_nev_ch[8];                 /* gVoxEvents */
static int g_last_slot[8];              /* DAT_8043e5ec */
static uint32_t g_last_tick; static uint16_t g_last_sub;
static struct { int id, count; } g_ingame[8];           /* gVoxInGame: last event + repeats */

static void clear_event(int i) {                         /* iSPCH_ClearEvent 802f0c58 */
    if (i < 0 || i >= NSLOT) return;
    int ch = g_slot[i].ch;
    g_slot[i].used = 0;
    if (g_last_slot[ch] == i) g_last_slot[ch] = -1;
    if (--g_nev_ch[ch] < 0) g_nev_ch[ch] = 0;
}
static void init_ingame(void) { for (int c = 0; c < 8; c++) { g_ingame[c].id = -1; g_ingame[c].count = 0; } }
static void clear_event_queue(void) {                    /* SPCH_ClearEventQueue 802f0b90 */
    for (int i = 0; i < NSLOT; i++) g_slot[i].used = 0;
    for (int c = 0; c < 8; c++) { g_last_slot[c] = -1; g_nev_ch[c] = 0; }
    init_ingame();
}
static int find_event_slot(unsigned prio, int ch) {      /* iSPCH_FindEventSlot 802f03f8 */
    for (int i = 0; i < NSLOT; i++) if (!g_slot[i].used) return i;
    uint32_t now = NOW_MS();
    for (int i = 0; i < NSLOT; i++) {
        unsigned age = g_slot[i].ev->age;
        if (age != 0 && now - g_slot[i].tick > age) { clear_event(i); return i; }
    }
    for (int i = 0; i < NSLOT; i++)
        if (g_slot[i].ev->prio <= prio && g_slot[i].ch == ch) {
            g_slot[i].used = 0; g_nev_ch[ch]--; return i;
        }
    return -1;
}
static int add_event(const SEvent *ev, int ch, const uint32_t *args, int nargs) {   /* iSPCH_AddEvent 802f0544 */
    int r = spch_rand(100, -1);
    if (r > ev->prob) return 0;
    int i = find_event_slot(ev->prio, ch);
    if (i < 0) return 0;
    uint32_t now = NOW_MS();
    g_last_sub = now == g_last_tick ? (uint16_t)(g_last_sub + 1) : 0;
    g_last_tick = now;
    EvSlot *s = &g_slot[i];
    s->ev = ev; s->ch = (uint8_t)ch; s->subtick = g_last_sub; s->tick = now;
    memset(s->prm, 0, sizeof s->prm);
    s->prm[0] = ev->id;
    for (int k = 0; k < nargs && k < MAX_PRM; k++) s->prm[1 + k] = args[k];
    g_nev_ch[ch]++;
    s->used = 1;
    g_last_slot[ch] = i;
    return 1;                            /* (flag 0x20 PlayLastEvent: not used by this data) */
}
static int choose_event(int ch) {                        /* iSPCH_ChooseEventSearch 802f0870 (preload 0) */
    uint32_t now = NOW_MS();
    int best = -1; unsigned bprio = 0; uint32_t bu = 0xffffffffu; unsigned bsub = 0;
    for (int i = 0; i < NSLOT; i++) {
        EvSlot *s = &g_slot[i];
        if (!s->used || s->ch != ch) continue;
        uint32_t u = now - s->tick;
        if (s->ev->age != 0 && s->ev->age < u) { clear_event(i); continue; }
        unsigned p = s->ev->prio;
        if (bprio < p || (p == bprio && (u < bu || (u == bu && bsub < s->subtick)))) {
            best = i; bprio = p; bu = u; bsub = s->subtick;
        }
    }
    return best;
}
static void clear_old_events(int k) {                    /* iSPCH_ClearOldEvents 802f0cf8 */
    for (int i = 0; i < NSLOT; i++) {
        EvSlot *s = &g_slot[i];
        if (i == k || !s->used || s->ch != g_slot[k].ch) continue;
        if (g_slot[k].tick <= s->tick && (s->tick != g_slot[k].tick || g_slot[k].subtick <= s->subtick))
            continue;                    /* newer than the chosen one */
        if (!(s->ev->flags & 4)) clear_event(i);
    }
}

/* ------------------------------------------------------------------ picker */
typedef struct { SBank *b; int s; } Pick;
static Pick g_req[MAX_PHR]; static int g_nreq;
static int g_last_bank = -1, g_last_sub_id = -1, g_last_sample = -1;

static SBank *find_bank(int id, int sub) {
    for (int i = 0; i < g_nbank; i++)
        if (g_sorted[i]->id == id && (sub < 0 || g_sorted[i]->sub == sub)) return g_sorted[i];
    return NULL;
}
static int bank_time_in_queue(const SBank *b, int s) {   /* iSPCHBank_GetSampleTimeInQueue */
    if (!b->qlen) return 0;
    int w = b->queue[0];
    for (int i = 0; i < b->qlen; i++) {
        if (--w < 0) w = b->qlen - 1;
        if (b->queue[1 + w] == s) return i + 1;
    }
    return 0;
}
static void bank_add_to_queue(SBank *b, int s) {         /* iSPCHBank_AddToQueue */
    if (!b->qlen) return;
    int w = b->queue[0];
    if (w < b->qlen) b->queue[1 + w] = (uint8_t)s;
    w++;
    b->queue[0] = (uint8_t)(w >= b->qlen ? 0 : w);
}
static int match_sample(const SPhrase *ph, const uint32_t *prm, const uint8_t *sp) {   /* 802f1828 */
    for (int i = 0; i < ph->nm; i++) {
        unsigned v = sp[i];
        if (v > 31 || !((ph->m[i].mask >> v) & 1)) return 0;
        unsigned src = ph->m[i].src;
        if (src == 0 || src == 0xfe || src == 0xff) continue;   /* 0xff global/IO: unused here */
        if (src > MAX_PRM || !((prm[src] >> v) & 1)) return 0;
    }
    return 1;
}
static unsigned decode_weight(uint8_t w) {
    static const unsigned mul[8] = { 1, 4, 16, 64, 256, 1024, 4096, 16384 };
    return (w & 31) * mul[w >> 5];
}

/* iSPCH_ChooseSentence 802f31b0 .. iSPCH_SaveChosenSentence: fills g_req */
static int choose_sentence(EvSlot *slot) {
    const SEvent *ev = slot->ev;
    int ch = slot->ch;
    const uint32_t *prm = slot->prm;
    /* iSPCH_RepeatEvent */
    if (g_ingame[ch].id == ev->id && ev->maxrep && !(g_ingame[ch].count < ev->maxrep)) return 0;
    /* (filter, rule settings, context: unused by this data) */
    unsigned w[MAX_SENT], tot = 0; int order[MAX_SENT], no = 0;   /* iSPCH_OrderSentences 802f1f58 */
    for (int i = 0; i < ev->nsent; i++) { w[i] = decode_weight(ev->s[i].weight); tot += w[i]; }
    unsigned w0[MAX_SENT]; memcpy(w0, w, sizeof w);
    while (tot > 0) {
        int x = spch_rand((int)tot, -1), j = 0;
        x -= (int)w[0];
        while (x >= 0 && j + 1 < ev->nsent) { j++; x -= (int)w[j]; }
        order[no++] = j; tot -= w[j]; w[j] = 0;
    }
    for (int i = 0; i < ev->nsent; i++) if (!w0[i]) order[no++] = i;
    for (int oi = 0; oi < no; oi++) {
        const SSent *S = &ev->s[order[oi]];
        if (!(spch_rand(100, -1) < S->freq)) continue;   /* iSPCH_CheckFrequency */
        SBank *pb[MAX_PHR]; int valid[MAX_PHR][256], nvalid[MAX_PHR]; int ok = 1;
        for (int p = 0; p < S->nphr && ok; p++) {        /* iSPCH_SentenceGetChoices */
            const SPhrase *ph = &S->ph[p];
            SBank *b = NULL;
            if (ph->mode == 0) b = find_bank(ph->bank, -1);
            else if (ph->mode == 1 && ph->parm <= MAX_PRM) b = find_bank(ph->bank, (int)(prm[ph->parm] & 0xffff));
            if (!b) { ok = 0; break; }
            for (int i = 0; i < ph->nm && i < b->k; i++) {   /* BankHasValidSamples */
                unsigned src = ph->m[i].src;
                if (src == 0 || src == 0xff || src == 0xfe || src > MAX_PRM) continue;
                if (!(bank_valid_mask(b, i) & prm[src])) { ok = 0; break; }
            }
            if (!ok) break;
            int nv = 0, oldest = -1, oldest_t = 0;       /* iSPCH_ChooseSamples */
            for (int s = 0; s < b->ns; s++) {
                if (!match_sample(ph, prm, sample_parms(b, s))) continue;
                int t = bank_time_in_queue(b, s);
                if (t < 1) valid[p][nv++] = s;
                else if (t > oldest_t) { oldest_t = t; oldest = s; }
            }
            if (!nv && oldest != -1) valid[p][nv++] = oldest;
            if (!nv) { ok = 0; break; }
            pb[p] = b; nvalid[p] = nv;
        }
        if (!ok) continue;
        g_nreq = 0;
        for (int p = 0; p < S->nphr; p++) {              /* SentenceMakeChoice / PickPhraseSample */
            int j = spch_rand(nvalid[p], pb[p]->vidx);
            g_req[g_nreq].b = pb[p]; g_req[g_nreq].s = valid[p][j]; g_nreq++;
        }
        for (int p = 0; p < g_nreq; p++) bank_add_to_queue(g_req[p].b, g_req[p].s);   /* SaveChosenSentence */
        return 1;
    }
    return 0;
}
/* SPCH_Play 802f0e9c -> SPCH_Choose 802f0f18 -> iSPCH_PlayChosen 802f2f40 */
static int spch_play(int ch) {
    g_nreq = 0;
    if (ch < 0 || ch >= 8 || !g_nev_ch[ch]) return 0;
    int i, ok = 0;
    for (;;) {
        i = choose_event(ch);
        if (i < 0) return 0;
        ok = choose_sentence(&g_slot[i]);               /* (EventRule callback: always 1) */
        if (ok) break;
        clear_event(i);
    }
    int id = g_slot[i].ev->id;
    clear_old_events(i);
    clear_event(i);
    if (g_ingame[ch].id == id) g_ingame[ch].count++;
    else { g_ingame[ch].id = id; g_ingame[ch].count = 1; }
    return g_nreq;
}

/* ------------------------------------------------------------------ MicroTalk */
/* tables from SPEED2.EXE (utk_tables.py): RC 0x819208, codebook 0x819308,
   commands 0x819508 {next_model, code_size, pulse} */
/* SPEED2.EXE 0x819208: reflection coefficients (64 f32) */
static const float k_rc[64] = {
    0.000000000e+00f, -9.967759848e-01f, -9.903270006e-01f, -9.838790298e-01f,
    -9.774309993e-01f, -9.709820151e-01f, -9.645339847e-01f, -9.580850005e-01f,
    -9.516370296e-01f, -9.307540059e-01f, -9.049599767e-01f, -8.791670203e-01f,
    -8.533729911e-01f, -8.275790215e-01f, -8.017860055e-01f, -7.759919763e-01f,
    -7.501980066e-01f, -7.244049907e-01f, -6.986110210e-01f, -6.706349850e-01f,
    -6.190479994e-01f, -5.674600005e-01f, -5.158730149e-01f, -4.642859995e-01f,
    -4.126980007e-01f, -3.611109853e-01f, -3.095239997e-01f, -2.579370141e-01f,
    -2.063490003e-01f, -1.547619998e-01f, -1.031749994e-01f, -5.158700049e-02f,
    0.000000000e+00f, 5.158700049e-02f, 1.031749994e-01f, 1.547619998e-01f,
    2.063490003e-01f, 2.579370141e-01f, 3.095239997e-01f, 3.611109853e-01f,
    4.126980007e-01f, 4.642859995e-01f, 5.158730149e-01f, 5.674600005e-01f,
    6.190479994e-01f, 6.706349850e-01f, 6.986110210e-01f, 7.244049907e-01f,
    7.501980066e-01f, 7.759919763e-01f, 8.017860055e-01f, 8.275790215e-01f,
    8.533729911e-01f, 8.791670203e-01f, 9.049599767e-01f, 9.307540059e-01f,
    9.516370296e-01f, 9.580850005e-01f, 9.645339847e-01f, 9.709820151e-01f,
    9.774309993e-01f, 9.838790298e-01f, 9.903270006e-01f, 9.967759848e-01f,
};
/* 0x819308: the two Huffman codebooks (model 0 normal, 1 after a large pulse) */
static const uint8_t k_codebook[2][256] = {
  {
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 17,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5, 21,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 18,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5, 25,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 17,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5, 22,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 18,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5,  0,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 17,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5, 21,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 18,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5, 26,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 17,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5, 22,
     4,  6,  5,  9,  4,  6,  5, 13,  4,  6,  5, 10,  4,  6,  5, 18,
     4,  6,  5,  9,  4,  6,  5, 14,  4,  6,  5, 10,  4,  6,  5,  2,
  },
  {
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 23,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8, 27,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 24,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8,  1,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 23,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8, 28,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 24,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8,  3,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 23,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8, 27,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 24,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8,  1,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 23,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8, 28,
     4, 11,  7, 15,  4, 12,  8, 19,  4, 11,  7, 16,  4, 12,  8, 24,
     4, 11,  7, 15,  4, 12,  8, 20,  4, 11,  7, 16,  4, 12,  8,  3,
  },
};
/* 0x819508: {next_model, code_size, pulse} per command */
static const struct { int next_model, code_size; float pulse; } k_cmd[29] = {
    { 1, 8, 0.0f },
    { 1, 7, 0.0f },
    { 0, 8, 0.0f },
    { 0, 7, 0.0f },
    { 0, 2, 0.0f },
    { 0, 2, -1.0f },
    { 0, 2, 1.0f },
    { 0, 3, -1.0f },
    { 0, 3, 1.0f },
    { 1, 4, -2.0f },
    { 1, 4, 2.0f },
    { 1, 3, -2.0f },
    { 1, 3, 2.0f },
    { 1, 5, -3.0f },
    { 1, 5, 3.0f },
    { 1, 4, -3.0f },
    { 1, 4, 3.0f },
    { 1, 6, -4.0f },
    { 1, 6, 4.0f },
    { 1, 5, -4.0f },
    { 1, 5, 4.0f },
    { 1, 7, -5.0f },
    { 1, 7, 5.0f },
    { 1, 6, -5.0f },
    { 1, 6, 5.0f },
    { 1, 8, -6.0f },
    { 1, 8, 6.0f },
    { 1, 7, -6.0f },
    { 1, 7, 6.0f },
};
static const float k_interp[3] = { 0.5973859429359436f, 0.1145915612578392f, 0.018032679334282875f };  /* 0x7d1f00 */

struct SpeechUTK {
    /* the clip (SCHl .. SCEl) */
    const uint8_t *clip; uint32_t size, pos;
    int first, block_mode, block_left, done;
    /* CMTBLKDecf state (0xd68 bytes in the exe) */
    const uint8_t *d; uint32_t dn, p;   /* block data, byte pointer */
    uint32_t v; int c;                  /* bit buffer, bit count */
    int reduced_bw, threshold;
    float gains[64], rc[12], hist[12], sub[756];
    int left, pcm_flag;
};

static uint32_t utk_byte(SpeechUTK *u) { uint32_t b = u->p < u->dn ? u->d[u->p] : 0; u->p++; return b; }
static uint32_t utk_bits(SpeechUTK *u, int n) {         /* Snd::getbits 0x733a33 */
    uint32_t r = u->v & ((1u << n) - 1);
    u->v >>= n; u->c -= n;
    if (u->c < 8) { u->v |= utk_byte(u) << u->c; u->c += 8; }
    return r;
}
static void utk_reset_reader(SpeechUTK *u, uint32_t pos) { u->p = pos; u->c = 8; u->v = utk_byte(u); }

/* CMTBLKDecf::Feed 0x7346db + initmut 0x73432c */
static void utk_feed(SpeechUTK *u, const uint8_t *data, uint32_t n, int parse_header) {
    u->d = data; u->dn = n;
    uint32_t p = 0;
    if (u->block_mode) { u->pcm_flag = n > 0 && data[0] == 0xEE; p = 1; }
    utk_reset_reader(u, p);
    if (parse_header) {
        u->reduced_bw = (int)utk_bits(u, 1);
        u->threshold = 32 - (int)utk_bits(u, 4);
        u->gains[0] = (float)((utk_bits(u, 4) + 1) * 8);
        float mul = (float)utk_bits(u, 6) * 0.001f + 1.04f;
        for (int i = 1; i < 64; i++) u->gains[i] = mul * u->gains[i - 1];
        memset(u->rc, 0, sizeof u->rc); memset(u->hist, 0, sizeof u->hist); memset(u->sub, 0, sizeof u->sub);
        u->left = 0;
    }
}
/* Snd::readsamples 0x733a9e */
static void utk_excitation(SpeechUTK *u, int multipulse, float *out, int stride) {
    int i = 0;
    if (!multipulse) {
        while (i < 108) {
            uint32_t k = u->v & 3;
            if (k == 1)      { out[i] = -2.0f; utk_bits(u, 2); }
            else if (k == 3) { out[i] =  2.0f; utk_bits(u, 2); }
            else             { out[i] =  0.0f; utk_bits(u, 1); }
            i += stride;
        }
        return;
    }
    int model = 0;
    while (i < 108) {
        int cmd = k_codebook[model][u->v & 0xff];
        model = k_cmd[cmd].next_model;
        utk_bits(u, k_cmd[cmd].code_size);
        if (cmd >= 4) { out[i] = k_cmd[cmd].pulse; i += stride; }
        else if (cmd >= 2) {
            int n = (int)utk_bits(u, 6) + 7;
            if (n * stride + i > 108) n = (108 - i) / stride;
            while (n-- > 0) { out[i] = 0.0f; i += stride; }
        } else {
            int x = 7;
            while (utk_bits(u, 1) == 1) x++;
            out[i] = (float)(utk_bits(u, 1) == 1 ? x : -x);
            i += stride;
        }
    }
}
/* Snd::reftolpc 0x733bc8 (x kept in a wide register, stores are f32) */
static void utk_reftolpc(const float *rc, float *lpc) {
    float t2[12], t1[12];
    for (int i = 10; i >= 0; i--) t2[i + 1] = rc[i];
    t2[0] = 1.0f;
    for (int i = 0; i < 12; i++) {
        double x = -((double)t2[11] * rc[11]);
        for (int j = 10; j >= 0; j--) {
            x -= (double)t2[j] * rc[j];
            t2[j + 1] = (float)(x * rc[j] + t2[j]);
        }
        t2[0] = (float)x; t1[i] = (float)x;
        for (int j = 0; j < i; j++) x -= (double)lpc[j] * t1[i - 1 - j];
        lpc[i] = (float)x;
    }
}
/* Snd::filter 0x733c54: 12-tap synthesis over nblk blocks of 12 at buf[324+off] */
static void utk_filter(SpeechUTK *u, int off, int nblk) {
    float lpc[12];
    utk_reftolpc(u->rc, lpc);
    float *p = &u->sub[324 + off], *h = u->hist;
    for (int b = 0; b < nblk; b++)
        for (int j = 0; j < 12; j++) {
            double x = *p;
            for (int k = 0; k < 12; k++) x += (double)lpc[k] * h[k >= j ? k - j : k - j + 12];
            h[11 - j] = (float)x;
            *p++ = (float)x;
        }
}
/* Snd::interpolate 0x7343f3: one 432-sample frame */
static void utk_frame(SpeechUTK *u) {
    float delta[12];
    int idx = (int)utk_bits(u, 6);
    int multipulse = idx < u->threshold;
    delta[0] = (k_rc[idx] - u->rc[0]) * 0.25f;
    for (int i = 1; i < 4; i++) delta[i] = (k_rc[utk_bits(u, 6)] - u->rc[i]) * 0.25f;
    for (int i = 4; i < 12; i++) delta[i] = (k_rc[16 + utk_bits(u, 5)] - u->rc[i]) * 0.25f;
    float exc[118];                     /* exc[5 + n], 5 guard samples each side */
    memset(exc, 0, sizeof exc);
    for (int s = 0; s < 4; s++) {
        int lag = (int)utk_bits(u, 8);
        float pgain = (float)utk_bits(u, 4) * (1.0f / 15.0f);
        float fgain = u->gains[utk_bits(u, 6)];
        if (!u->reduced_bw) utk_excitation(u, multipulse, exc + 5, 1);
        else {
            int align = (int)utk_bits(u, 1), zero = (int)utk_bits(u, 1);
            utk_excitation(u, multipulse, exc + 5 + align, 2);
            if (zero) for (int j = 0; j < 54; j++) exc[5 + 1 - align + 2 * j] = 0.0f;
            else {
                for (int j = 0; j < 5; j++) { exc[j] = 0.0f; exc[113 + j] = 0.0f; }
                for (int j = 0; j < 54; j++) {
                    int c = 5 + 1 - align + 2 * j;
                    exc[c] = (float)(((double)exc[c - 1] + exc[c + 1]) * k_interp[0] +
                                     (((double)exc[c - 5] + exc[c + 5]) * k_interp[2] -
                                      ((double)exc[c - 3] + exc[c + 3]) * k_interp[1]));
                }
                fgain *= 0.5f;
            }
        }
        float *dst = &u->sub[324 + 108 * s];
        const float *src = &u->sub[216 + 108 * s - lag];
        for (int j = 0; j < 108; j++) dst[j] = (float)((double)pgain * src[j] + (double)fgain * exc[5 + j]);
    }
    memmove(u->sub, u->sub + 432, 324 * sizeof(float));
    static const int off[4] = { 0, 12, 24, 36 }, nb[4] = { 1, 1, 1, 33 };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 12; i++) u->rc[i] += delta[i];
        utk_filter(u, off[k], nb[k]);
    }
}
static int16_t to_s16(float x) {
    double r = nearbyint((double)x);   /* the mixer's conversion is not decoded: round + clip */
    return (int16_t)(r < -32768.0 ? -32768 : r > 32767.0 ? 32767 : r);
}
/* Snd::decodemut 0x734798: n samples of the current block */
static void utk_decode(SpeechUTK *u, int16_t *out, int n) {
    int got = 0;
    for (;;) {
        int take = u->left < n - got ? u->left : n - got;
        const float *s = &u->sub[756 - u->left];
        for (int i = 0; i < take; i++) out[got + i] = to_s16(s[i]);
        got += take; u->left -= take;
        if (got >= n) return;
        utk_frame(u);
        u->left = 432;
        if (!u->block_mode) continue;
        /* block tail: raw PCM patch, the next frame's 0xEE flag, byte realign */
        uint32_t q = u->p - 1;
        if (u->pcm_flag) {
            if (q + 4 <= u->dn) {
                int o = s16be(u->d + q), cnt = s16be(u->d + q + 2);
                for (int i = 0; i < cnt && q + 6 + 2 * (uint32_t)i <= u->dn; i++)
                    if (o + i >= 0 && o + i < 432) u->sub[324 + o + i] = (float)s16be(u->d + q + 4 + 2 * i);
                q += 4 + 2 * (uint32_t)(cnt > 0 ? cnt : 0);
            }
            u->pcm_flag = 0;
        }
        u->pcm_flag = q < u->dn && u->d[q] == 0xEE;
        utk_reset_reader(u, q + 1);
    }
}

SpeechUTK *speech_utk_new(void) { return calloc(1, sizeof(SpeechUTK)); }
void speech_utk_free(SpeechUTK *u) { free(u); }
void speech_utk_start(SpeechUTK *u, const uint8_t *clip, uint32_t size) {
    memset(u, 0, sizeof *u);
    u->clip = clip; u->size = size; u->first = 1; u->block_mode = 1;
}
/* walks SCHl/SCCl/SCDl/SCEl; the stream feeder 0x732980 feeds each SCDl's
   data at +8+chan_off+1 and parses the coded header on the first block */
int speech_utk_read(SpeechUTK *u, int16_t *out, int n) {
    int got = 0;
    while (got < n && !u->done) {
        if (u->block_left == 0) {
            for (;;) {
                if (u->pos + 8 > u->size) { u->done = 1; break; }
                const uint8_t *c = u->clip + u->pos;
                uint32_t sz = le32(c + 4);
                if (sz < 8 || u->pos + sz > u->size) { u->done = 1; break; }
                u->pos += sz;
                if (!memcmp(c, "SCHl", 4)) {
                    uint32_t p = 12;    /* 'PT\0\0' then tags */
                    while (p + 2 <= sz && c[p] != 0xff) {
                        uint8_t t = c[p];
                        if (t == 0xfc || t == 0xfd || t == 0xfe) { p++; continue; }
                        uint32_t ln = c[p + 1], v = 0;
                        for (uint32_t k = 0; k < ln && p + 2 + k < sz; k++) v = v << 8 | c[p + 2 + k];
                        if (t == 0x80) u->block_mode = v >= 2;   /* 0x732980: block mode from version 2 */
                        p += 2 + ln;
                    }
                } else if (!memcmp(c, "SCDl", 4) && sz >= 8 + 9) {
                    uint32_t ns = le32(c + 8), co = le32(c + 12);
                    uint32_t start = 8 + 8 + co + 1;
                    if (start >= sz) continue;
                    utk_feed(u, c + start, sz - start, u->first);
                    u->first = 0;
                    u->block_left = (int)ns;
                    if (u->block_left > 0) break;
                } else if (!memcmp(c, "SCEl", 4)) { u->done = 1; break; }
            }
            if (u->done) break;
        }
        int m = u->block_left < n - got ? u->block_left : n - got;
        utk_decode(u, out + got, m);
        got += m; u->block_left -= m;
    }
    return got;
}

/* ------------------------------------------------------------------ clip FIFO (the stream) */
#define NFIFO 4
/* pending: queued, its bytes (off, size) not read yet -- the music thread's
   io hook reads them (speech_io_service), so a 300 KB clip read is not a
   frame hitch (944 ms on HW 10-31); the audio thread waits on it */
typedef struct { uint8_t *data; uint32_t size; volatile int finished; int bank, sub, sample, xa;
                 volatile int pending; uint32_t off; } Clip;
static Clip g_fifo[NFIFO];
static volatile int g_head, g_tail;              /* head: main pushes; tail: audio plays (under g_lock) */
static SpeechUTK g_dec;
static int g_dec_started;
static volatile int g_paused;
/* SpeechModule queued object (m+0x44): QueStream .. PlayStream; ch = the
   SPCH channel its events went to (0 UG2_Speech, 1 nisaudio) */
static struct { int on, played, ch; uint32_t t; } g_queued;
/* a nisaudio clip on the stream (EA-XA, below the NIS section) */
static int xa_stream_read(const Clip *c, int start, int16_t *out, int n, int *done);

static void fifo_reap(void) {           /* ClearCompletedRequests: free played clips (main) */
    uint8_t *d[NFIFO]; int n = 0, any = 0;
    for (int i = 0; i < NFIFO; i++) any |= g_fifo[i].finished;
    if (!any) return;
    lock();
    for (int i = 0; i < NFIFO; i++)   /* a purged clip still being read: its reader drops the bytes */
        if (g_fifo[i].finished && !g_fifo[i].pending) { d[n++] = g_fifo[i].data; g_fifo[i].data = NULL; g_fifo[i].finished = 0; }
    unlock();
    while (n > 0) free(d[--n]);
}
static int fifo_empty(void) { return g_head == g_tail; }
static void stream_purge(void) {        /* SndStrmWrapper::PurgeStream */
    lock();
    while (g_tail != g_head) { g_fifo[g_tail].finished = 1; g_tail = (g_tail + 1) % NFIFO; }
    g_dec_started = 0;
    unlock();
    fifo_reap();
}
static void module_purge(void) {        /* SpeechModule::PurgeSpeech 80089d94 */
    stream_purge();
    g_queued.on = 0;
}
static void manager_purge(void) {       /* SpeechManager::PurgeSpeech(0) 80089584 */
    if (!g_ok) return;
    clear_event_queue();
    module_purge();
}

int speech_pull(void *uu, int16_t *mono, int n) {
    (void)uu;
    if (g_paused || !g_ok) return 0;
    int got = 0;
    lock();
    while (got < n && g_tail != g_head) {
        Clip *c = &g_fifo[g_tail];
        if (c->pending) break;            /* still being read: silence until it is in */
        if (!c->data) {                   /* the read failed: the next request */
            c->finished = 1; g_tail = (g_tail + 1) % NFIFO; g_dec_started = 0;
            continue;
        }
        if (c->xa) {
            int done = 0;
            got += xa_stream_read(c, !g_dec_started, mono + got, n - got, &done);
            g_dec_started = 1;
            if (done) { c->finished = 1; g_tail = (g_tail + 1) % NFIFO; g_dec_started = 0; }
            continue;
        }
        if (!g_dec_started) { speech_utk_start(&g_dec, c->data, c->size); g_dec_started = 1; }
        int r = speech_utk_read(&g_dec, mono + got, n - got);
        got += r;
        if (g_dec.done) {               /* SCEl reached: the next request */
            c->finished = 1; g_tail = (g_tail + 1) % NFIFO; g_dec_started = 0;
        }
    }
    unlock();
    if (got < n) memset(mono + got, 0, sizeof(int16_t) * (size_t)(n - got));
    return got;
}

/* ------------------------------------------------------------------ init */
static int load_db(int dbi, const char *nm, const uint8_t *dir, uint32_t hl, uint32_t n) {
    SDb *db = &g_db[dbi];
    char want[4][64];
    static const char *ext[4] = { "idx", "evt", "csi", "big" };
    uint32_t off[4] = { 0 }, sz[4] = { 0 };
    for (int k = 0; k < 4; k++) snprintf(want[k], sizeof want[k], "speech/%s.%s", nm, ext[k]);
    uint32_t p = 0;
    for (uint32_t i = 0; i < n && p + 9 <= hl - 16; i++) {
        uint32_t o = be32(dir + p), s = be32(dir + p + 4); p += 8;
        const char *name = (const char *)dir + p;
        size_t ln = strnlen(name, hl - 16 - p); p += (uint32_t)ln + 1;
        for (int k = 0; k < 4; k++) if (name_eq(name, want[k])) { off[k] = o; sz[k] = s; }
    }
    if (!sz[0] || !sz[1] || !sz[3] || sz[0] > 0x10000 || sz[1] > 0x10000) return 0;
    db->idx = read_at(off[0], sz[0]);
    db->evt = read_at(off[1], sz[1]);
    if (!db->idx || !db->evt) return 0;
    db->big_off = off[3]; db->big_size = sz[3];
    if (!parse_evt(db, sz[1])) return 0;
    parse_idx(dbi, sz[0]);
    db->ok = 1;
    return 1;
}

void speech_seed(int32_t seed) {        /* gGameNum = |gSpeechSeed| */
    uint32_t g = (uint32_t)(seed < 0 ? -seed : seed);
    spch_init_random(g);
}

int speech_init(const char *dataroot) {
    if (g_ok) return 1;
    char path[600];
    snprintf(path, sizeof path, "%s/SDATA/sdat.viv", dataroot);
    g_f = fopen(path, "rb");
    if (!g_f) { printf("speech: no %s\n", path); return 0; }
    snprintf(g_viv_path, sizeof g_viv_path, "%s", path);
    uint8_t h[16];
    if (fread(h, 1, 16, g_f) != 16 || memcmp(h, "BIG", 3)) { fclose(g_f); g_f = NULL; return 0; }
    uint32_t n = be32(h + 8), hl = be32(h + 12);
    uint8_t *dir = hl > 16 && hl < (16u << 20) ? malloc(hl) : NULL;
    if (!dir || fread(dir, 1, hl - 16, g_f) != hl - 16) { free(dir); fclose(g_f); g_f = NULL; return 0; }
    g_nbank = 0;
    int ok = load_db(DB_SPEECH, "UG2_Speech", dir, hl, n);
    load_db(DB_NIS, "nisaudio", dir, hl, n);
    free(dir);
    if (!ok) { printf("speech: no UG2_Speech in sdat.viv\n"); fclose(g_f); g_f = NULL; return 0; }
    /* gVoxBanks: sorted by (id, sub) (iSPCH_FindInsertPosition) */
    for (int i = 0; i < g_nbank; i++) g_sorted[i] = &g_bank[i];
    for (int i = 1; i < g_nbank; i++)
        for (int j = i; j > 0; j--) {
            SBank *a = g_sorted[j - 1], *b = g_sorted[j];
            if (a->id > b->id || (a->id == b->id && a->sub > b->sub)) { g_sorted[j - 1] = b; g_sorted[j] = a; }
            else break;
        }
    for (int i = 0; i < g_nbank; i++) g_sorted[i]->vidx = i;
    clear_event_queue();
    /* gSpeechSeed = EAXSound::Random(-1) on the first SpeechManager::Init */
    speech_seed((int32_t)((uint32_t)time(NULL) * 2654435761u));
    g_ok = 1;
    audio_set_speech(speech_pull, NULL, 22050);
    printf("speech: %d banks, %d events (UG2_Speech%s)\n", g_nbank, g_db[DB_SPEECH].nev,
           g_db[DB_NIS].ok ? " + nisaudio" : "");
    return 1;
}

/* ------------------------------------------------------------------ commands */
static const SEvent *func(const char *name) {
    SDb *db = &g_db[DB_SPEECH];
    for (int i = 0; i < db->nev; i++) if (!strcmp(db->ev[i].name, name)) return &db->ev[i];
    return NULL;
}
static uint32_t g_brand = 0x2545f491u;  /* bRandom stand-in for SpeechCmdIE's A/B pick */

/* UG2_SpeechParser::Play*: the Csis call (AddEvent), then QueStream when free */
static void parser_play(const char *fn, const uint32_t *args, int nargs) {
    const SEvent *ev = func(fn);
    if (!ev) return;
    add_event(ev, g_db[DB_SPEECH].datid, args, nargs);
    if (!g_queued.on) {                 /* SpeechModule::QueStream(3, SpeechStreamQueued) */
        g_queued.on = 1; g_queued.played = 0; g_queued.t = NOW_MS(); g_queued.ch = g_db[DB_SPEECH].datid;
    }
}

/* CARSFX_Rain::QueueWeatherStream 0x4852a0 -> NIS_AudioParser::QueStream
   0x484510 (THUNDERSTRM branch 0x4846df): only while the speech module is
   done, the Csis call SoundFX_Select(4, variant) (0x45a770, interface
   0x7ed034) into the nisaudio channel, then SpeechModule::QueStream. */
int speech_stream_queued(void) { return g_ok && g_queued.on; }
/* NIS_AudioParser::QueStream 0x484510's stream branches: SoundFX_Select(a, b)
   into the nisaudio channel, then SpeechModule::QueStream */
static int fx_stream(uint32_t a, uint32_t b) {
    if (!g_ok || !g_db[DB_NIS].ok || !speech_done() || g_queued.on) return 0;
    SDb *db = &g_db[DB_NIS];
    const SEvent *ev = NULL;
    for (int i = 0; i < db->nev; i++) if (!strcmp(db->ev[i].name, "SoundFX_Select")) { ev = &db->ev[i]; break; }
    if (!ev) return 0;
    uint32_t p[2] = { a, b };
    add_event(ev, db->datid, p, 2);
    g_queued.on = 1; g_queued.played = 0; g_queued.t = NOW_MS(); g_queued.ch = db->datid;
    return 1;
}
int speech_weather_stream(int variant) { return fx_stream(4, (uint32_t)variant); }
/* EAXCollision::QueueCollisionStream 0x486130 (the crash, 0x5a0107):
   QueStream(5, "CollisionStream") -> the 0x78c17c branch 0x484699:
   SoundFX_Select(2, 8). GUESS: gated like the thunder (speech done, nothing
   queued); retail stops a queued stream of type 1 / 4 / 8 for it (0x48457e) */
int speech_collision_stream(void) { return fx_stream(2, 8); }

void speech_cmd(int kind, uint32_t a, uint32_t b) {
    if (!g_ok) return;
    uint32_t p[4];
    switch (kind) {
    case CSPK_CAREER:  p[0] = 1; p[1] = a; p[2] = b; parser_play("Career", p, 3); break;
    case CSPK_IE: {
        uint32_t ca = b & 0xffff, cb = b >> 16, ch = ca;
        if (cb) { g_brand = g_brand * 1103515245u + 12345u; ch = (g_brand >> 16) & 1 ? cb : ca; }
        p[0] = 0x10; p[1] = a; p[2] = ch; parser_play("Intro_Events", p, 3); break; }
    case CSPK_IA:      p[0] = 0x20; p[1] = a; p[2] = b; parser_play("Intro_Area", p, 3); break;
    case CSPK_TIMED:   p[0] = 4; p[1] = a; parser_play("Time", p, 2); break;
    case CSPK_DVD:     p[0] = 1; p[1] = 0x400; p[2] = a; p[3] = b; parser_play("Career_Dvd", p, 4); break;
    case CSPK_MAG:     p[0] = 1; p[1] = 0x200; p[2] = a; p[3] = b; parser_play("Career_Magazine", p, 4); break;
    case CSPK_OUTRUN:  /* PlayOutrun: sub-bank from the speaker */
        p[0] = 8; p[1] = a; p[2] = b;
        p[3] = b == 4 ? 2 : b == 1 ? 3 : b == 8 ? 4 : b == 0x200 ? 5 : 1;
        parser_play("Outrun", p, 4); break;
    case CSPK_FES:     p[0] = 2; p[1] = 1; p[2] = a; p[3] = b; parser_play("FE_Screen", p, 4); break;
    case CSPK_FESDYNO: p[0] = 2; p[1] = 0x20; p[2] = a; p[3] = b; parser_play("FE_Dynoshop", p, 4); break;
    case CSPK_PURGE:   manager_purge(); break;
    default: break;                     /* CSPK_SMS: not speech */
    }
}

/* Perf (port): the pending clips' bytes, read on the music decoder thread
   (music_set_io_hook) with its own handle on sdat.viv; the main thread only
   queues them. A clip purged while it was read is dropped here. */
static FILE *g_iof;   /* the reader thread's handle */
static int g_io_queued;   /* a clip read was handed to the music thread */
static void speech_io_service(void) {
    for (;;) {
        int slot = -1; uint32_t off = 0, size = 0;
        lock();
        for (int i = g_tail; i != g_head; i = (i + 1) % NFIFO)
            if (g_fifo[i].pending) { slot = i; off = g_fifo[i].off; size = g_fifo[i].size; break; }
        if (slot < 0)   /* purged (tail moved past it) but still marked pending */
            for (int i = 0; i < NFIFO; i++)
                if (g_fifo[i].pending && g_fifo[i].finished) { g_fifo[i].pending = 0; }
        unlock();
        if (slot < 0) return;
        if (!g_iof && g_viv_path[0]) g_iof = fopen(g_viv_path, "rb");
        uint8_t *d = g_iof ? (uint8_t *)malloc(size ? size : 1) : NULL;
        if (d && (fseek(g_iof, (long)off, SEEK_SET) || fread(d, 1, size, g_iof) != size)) { free(d); d = NULL; }
        lock();
        Clip *c = &g_fifo[slot];
        if (c->finished) { c->pending = 0; unlock(); free(d); continue; }   /* purged meanwhile */
        c->data = d;          /* NULL: the read failed, speech_pull skips it */
        __sync_synchronize();
        c->pending = 0;
        unlock();
#ifdef OPENUG2_SIM_PRE_H
        printf("speech: clip read on the music thread (%u bytes%s)\n", (unsigned)size, d ? "" : ", FAILED");
#endif
    }
}

/* the clip bytes for one sample request (SampleRequestCallback 80089b40) */
static void sample_request(SBank *b, int s) {
    uint32_t off, size;
    sample_range(b, s, &off, &size);
    g_last_bank = b->id; g_last_sub_id = b->sub == 0xffff ? -1 : b->sub; g_last_sample = s;
    if (!size || size > (4u << 20) || !g_f) return;
    fifo_reap();
    if (g_fifo[g_head].data || g_fifo[g_head].pending || g_fifo[g_head].finished ||
        (g_head + 1) % NFIFO == g_tail) return;   /* stream full: dropped */
    int async = 1;
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_SIM_SPEECHDUMP")) async = 0;   /* the test aid decodes the bytes below */
#endif
    if (async && music_set_io_hook(speech_io_service)) {   /* perf: read on the music thread */
        lock();
        Clip *c = &g_fifo[g_head];
        c->data = NULL; c->size = size; c->off = off; c->finished = 0;
        c->bank = b->id; c->sub = b->sub; c->sample = s; c->xa = b->db == DB_NIS;
        c->pending = 1;
        g_head = (g_head + 1) % NFIFO;
        unlock();
        g_io_queued = 1;
        printf("speech: clip bank %d sub %d sample %d (%u bytes, read queued)\n",
               b->id, b->sub == 0xffff ? -1 : (int)b->sub, s, (unsigned)size);
        return;
    }
    uint8_t *d = read_at(off, size);
    if (!d) return;
    lock();
    g_fifo[g_head].data = d; g_fifo[g_head].size = size; g_fifo[g_head].finished = 0;
    g_fifo[g_head].bank = b->id; g_fifo[g_head].sub = b->sub; g_fifo[g_head].sample = s;
    g_fifo[g_head].xa = b->db == DB_NIS;
    g_head = (g_head + 1) % NFIFO;
    unlock();
    printf("speech: clip bank %d sub %d sample %d (%u bytes)\n", b->id, b->sub == 0xffff ? -1 : (int)b->sub, s, (unsigned)size);
#ifdef OPENUG2_SIM_PRE_H
    {   /* sim (no audio device): OPENUG2_SIM_SPEECHDUMP = the decoded clips, raw s16 22050 Hz */
        static FILE *df; static int dfo;
        if (!dfo) { const char *e = getenv("OPENUG2_SIM_SPEECHDUMP"); dfo = 1; if (e) df = fopen(e, "wb"); }
        if (df) {
            SpeechUTK *u = speech_utk_new(); int16_t pcm[1024]; int r, tot = 0, pk = 0;
            speech_utk_start(u, d, size);
            while ((r = speech_utk_read(u, pcm, 1024)) > 0) {
                fwrite(pcm, 2, (size_t)r, df); tot += r;
                for (int i = 0; i < r; i++) if (abs(pcm[i]) > pk) pk = abs(pcm[i]);
            }
            fflush(df); speech_utk_free(u);
            printf("speech: sim decode %d samples (%.2f s), peak %d\n", tot, tot / 22050.0, pk);
        }
    }
#endif
}

void speech_update(void) {              /* GameSpeech::Update 8008a6f0 */
    if (!g_ok) return;
    fifo_reap();
    if (g_io_queued && !music_set_io_hook(speech_io_service))
        speech_io_service();   /* the music thread is gone: read the queued clips here */
    /* SpeechModule::Update: a queued stream not started within 1.0 s is dropped */
    if (g_queued.on && NOW_MS() - g_queued.t > 1000) module_purge();
    /* speech volume 0 purges (GetMasterVolume(1) == 0) */
    if (audio_speech_gain() <= 0.0f && (!fifo_empty() || g_queued.on)) module_purge();
    if (g_paused) return;
    if (g_queued.on && !g_queued.played) {
        g_last_bank = g_last_sub_id = g_last_sample = -1;
        int n = spch_play(g_queued.ch);
        for (int i = 0; i < n; i++) sample_request(g_req[i].b, g_req[i].s);
        g_queued.played = 1;
    }
    /* SpeechStreamQueued -> PlayStream: once the stream has data */
    if (g_queued.on && !fifo_empty()) g_queued.on = 0;
}

int speech_done(void) {
    if (!g_ok) return 1;
    return fifo_empty() && !(g_queued.on && !g_queued.played);
}
void speech_pause(int on) { g_paused = on ? 1 : 0; }
void speech_stop(void) { manager_purge(); }
void speech_last_pick(int *bank_id, int *sub, int *sample) {
    if (bank_id) *bank_id = g_last_bank;
    if (sub) *sub = g_last_sub_id;
    if (sample) *sample = g_last_sample;
}

/* ------------------------------------------------------------------ NIS: EA-XA R3 */
static struct {
    uint8_t *data; uint32_t size, pos;
    int ch, rate;
    int16_t *pcm; int pcm_n, pcm_pos, pcm_cap;  /* one decoded block, interleaved */
    int hist[2][2];
} g_nis;

static void nis_chan(const uint8_t *d, uint32_t dn, uint32_t pos, int n, int c, int16_t *out, int stride, int (*hist)[2]) {
    static const int C1[4] = { 0, 240, 460, 392 }, C2[4] = { 0, 0, -208, -220 };
    int c1 = hist[c][0], c2 = hist[c][1], got = 0;
    while (got < n && pos < dn) {
        uint8_t hdr = d[pos];
        if (hdr == 0xEE) {
            if (pos + 61 > dn) break;
            c1 = s16be(d + pos + 1); c2 = s16be(d + pos + 3);
            for (int k = 0; k < 28 && got < n; k++) out[(got++) * stride] = s16be(d + pos + 5 + 2 * k);
            pos += 61;
        } else {
            if (pos + 15 > dn) break;
            int ci = hdr >> 4 & 3, sh = 20 - (hdr & 15);
            for (int k = 0; k < 28; k++) {
                int b = d[pos + 1 + (k >> 1)];
                int nib = k & 1 ? b & 15 : b >> 4;
                if (nib > 7) nib -= 16;
                int s = ((nib << sh) + c1 * C1[ci] + c2 * C2[ci]) >> 8;
                s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
                c2 = c1; c1 = s;
                if (got < n) out[(got++) * stride] = (int16_t)s;
            }
            pos += 15;
        }
    }
    while (got < n) out[(got++) * stride] = 0;
    hist[c][0] = c1; hist[c][1] = c2;
}

int speech_nis_open(int bank, int sample, int *rate) {
    speech_nis_close();
    if (!g_ok || !g_db[DB_NIS].ok) return 0;
    int k = 0; SBank *b = NULL;
    for (int i = 0; i < g_nbank; i++) if (g_bank[i].db == DB_NIS && k++ == bank) { b = &g_bank[i]; break; }
    if (!b || sample < 0 || sample >= b->ns) return 0;
    uint32_t off, size;
    sample_range(b, sample, &off, &size);
    g_nis.data = size ? read_at(off, size) : NULL;
    if (!g_nis.data) return 0;
    g_nis.size = size; g_nis.ch = 1; g_nis.rate = 22050;
    /* SCHl tags: 0x82 channels, 0x84 rate */
    if (size >= 12 && !memcmp(g_nis.data, "SCHl", 4)) {
        uint32_t sz = le32(g_nis.data + 4), p = 12;
        while (p + 2 <= sz && p + 2 <= size && g_nis.data[p] != 0xff) {
            uint8_t t = g_nis.data[p];
            if (t == 0xfc || t == 0xfd || t == 0xfe) { p++; continue; }
            uint32_t ln = g_nis.data[p + 1], v = 0;
            for (uint32_t q = 0; q < ln && p + 2 + q < size; q++) v = v << 8 | g_nis.data[p + 2 + q];
            if (t == 0x82) g_nis.ch = v == 2 ? 2 : 1;
            if (t == 0x84) g_nis.rate = (int)v;
            p += 2 + ln;
        }
    }
    if (rate) *rate = g_nis.rate;
    return g_nis.ch;
}
int speech_nis_pull(int16_t *out, int nframes) {
    int got = 0, ch = g_nis.ch;
    while (got < nframes && g_nis.data) {
        if (g_nis.pcm_pos >= g_nis.pcm_n) {    /* next SCDl */
            int fed = 0;
            while (g_nis.pos + 8 <= g_nis.size) {
                const uint8_t *c = g_nis.data + g_nis.pos;
                uint32_t sz = le32(c + 4);
                if (sz < 8 || g_nis.pos + sz > g_nis.size) { g_nis.pos = g_nis.size; break; }
                g_nis.pos += sz;
                if (!memcmp(c, "SCEl", 4)) { g_nis.pos = g_nis.size; break; }
                if (memcmp(c, "SCDl", 4) || sz < 8 + 4 + 4 * (uint32_t)ch) continue;
                const uint8_t *body = c + 8; uint32_t bn = sz - 8;
                int ns = (int)le32(body);
                if (ns <= 0 || ns > 1 << 20) continue;
                if (ns * ch > g_nis.pcm_cap) {
                    int16_t *np = realloc(g_nis.pcm, sizeof(int16_t) * (size_t)ns * ch);
                    if (!np) { g_nis.pos = g_nis.size; break; }
                    g_nis.pcm = np; g_nis.pcm_cap = ns * ch;
                }
                for (int k = 0; k < ch; k++) {
                    uint32_t o = le32(body + 4 + 4 * k) + 4 * (uint32_t)(ch + 1);
                    nis_chan(body, bn, o, ns, k, g_nis.pcm + k, ch, g_nis.hist);
                }
                g_nis.pcm_n = ns; g_nis.pcm_pos = 0; fed = 1;
                break;
            }
            if (!fed) break;
        }
        int m = g_nis.pcm_n - g_nis.pcm_pos;
        if (m > nframes - got) m = nframes - got;
        memcpy(out + got * ch, g_nis.pcm + g_nis.pcm_pos * ch, sizeof(int16_t) * (size_t)m * ch);
        got += m; g_nis.pcm_pos += m;
    }
    return got;
}
void speech_nis_close(void) {
    free(g_nis.data); free(g_nis.pcm);
    memset(&g_nis, 0, sizeof g_nis);
}

/* A nisaudio clip on the speech stream: SoundFX_Select's thunder (bank 16398,
   mono EA-XA R3 at 32 kHz) goes through the same SpeechModule stream as the
   speech, so it is decoded here from the FIFO clip (channel 0) and resampled
   linearly to the stream's 22050 Hz (audio_set_speech in speech_init).
   Audio thread, under g_lock. */
static struct {
    uint32_t pos; int ch, rate;
    int16_t *pcm; int pcm_n, pcm_pos, pcm_cap;
    int hist[2][2];
    double t; int16_t s0, s1;
} g_xs;
static int xs_next(const Clip *c, int16_t *s) {
    while (g_xs.pcm_pos >= g_xs.pcm_n) {
        if (g_xs.pos + 8 > c->size) return 0;
        const uint8_t *k = c->data + g_xs.pos;
        uint32_t sz = le32(k + 4);
        if (sz < 8 || g_xs.pos + sz > c->size || !memcmp(k, "SCEl", 4)) { g_xs.pos = c->size; return 0; }
        g_xs.pos += sz;
        if (memcmp(k, "SCDl", 4) || sz < 8 + 4 + 4 * (uint32_t)g_xs.ch) continue;
        const uint8_t *body = k + 8; uint32_t bn = sz - 8;
        int ns = (int)le32(body);
        if (ns <= 0 || ns > 1 << 20) continue;
        if (ns > g_xs.pcm_cap) {
            int16_t *np = realloc(g_xs.pcm, sizeof(int16_t) * (size_t)ns);
            if (!np) { g_xs.pos = c->size; return 0; }
            g_xs.pcm = np; g_xs.pcm_cap = ns;
        }
        nis_chan(body, bn, le32(body + 4) + 4 * (uint32_t)(g_xs.ch + 1), ns, 0, g_xs.pcm, 1, g_xs.hist);
        g_xs.pcm_n = ns; g_xs.pcm_pos = 0;
    }
    *s = g_xs.pcm[g_xs.pcm_pos++];
    return 1;
}
static int xa_stream_read(const Clip *c, int start, int16_t *out, int n, int *done) {
    if (start) {
        g_xs.pos = 0; g_xs.ch = 1; g_xs.rate = 32000;
        g_xs.pcm_n = g_xs.pcm_pos = 0; memset(g_xs.hist, 0, sizeof g_xs.hist);
        g_xs.t = 2.0; g_xs.s0 = g_xs.s1 = 0;
        if (c->size >= 12 && !memcmp(c->data, "SCHl", 4)) {   /* 0x82 channels, 0x84 rate */
            uint32_t sz = le32(c->data + 4), p = 12;
            while (p + 2 <= sz && p + 2 <= c->size && c->data[p] != 0xff) {
                uint8_t tg = c->data[p];
                if (tg == 0xfc || tg == 0xfd || tg == 0xfe) { p++; continue; }
                uint32_t ln = c->data[p + 1], v = 0;
                for (uint32_t q = 0; q < ln && p + 2 + q < c->size; q++) v = v << 8 | c->data[p + 2 + q];
                if (tg == 0x82) g_xs.ch = v == 2 ? 2 : 1;
                if (tg == 0x84 && v > 0) g_xs.rate = (int)v;
                p += 2 + ln;
            }
        }
    }
    double step = g_xs.rate / 22050.0;
    int got = 0;
    while (got < n) {
        while (g_xs.t >= 1.0) {
            g_xs.s0 = g_xs.s1;
            if (!xs_next(c, &g_xs.s1)) { *done = 1; return got; }
            g_xs.t -= 1.0;
        }
        out[got++] = (int16_t)(g_xs.s0 + (g_xs.s1 - g_xs.s0) * g_xs.t);
        g_xs.t += step;
    }
    return got;
}
