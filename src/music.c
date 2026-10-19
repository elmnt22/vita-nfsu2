/* EA Trax streaming (see music.h) */
#include "music.h"
#include "eal3.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SPEED2.EXE 0x7ecd80: title, artist, album, where it plays */
static const char *const k_song[MUSIC_SONGS][4] = {
    { "\"Riders on the Storm (Fredwreck Remix)\"", "Snoop Dogg feat. The Doors", "Need for Speed Underground 2 Exclusive", "FE" },
    { "\"I Need Speed\"", "Capone", "Need for Speed Underground 2 Exclusive", "FE" },
    { "\"I Do\"", "Chingy", "PowerBallin'", "FE" },
    { "\"That'z My Name\"", "Sly Boogy", "Mistaken Identity", "FE" },
    { "\"LAX\"", "Xzibit", "Weapons of Mass Destruction", "FE" },
    { "\"Lean Back\"", "Terror Squad", "True Story", "FE" },
    { "\"Switch/Twitch\"", "Fluke", "Puppy", "IG" },
    { "\"Rush Hour\"", "Christopher Lawrence", "All or Nothing", "IG" },
    { "\"Rocket Ride (Soulwax Remix)\"", "Felix Da Housecat", "Devin Dazzle & the Neon Fever", "IG" },
    { "\"Hard EBM\"", "Sin", "Errare Digital EST", "IG" },
    { "\"Mind Killer (Jagz Kooner Remix)\"", "FREELAND", "Now & Them", "IG" },
    { "\"Nothing But You (Cirrus Remix)\"", "Paul Van Dyk", "Reflections", "IG" },
    { "\"E-Ville\"", "Sonic Animation", "Reality By Deception", "IG" },
    { "\"The Death & Resurrection Show\"", "Killing Joke", "Killing Joke", "IG" },
    { "\"Give it All\"", "Rise Against", "Siren Song of the Counter Culture", "IG" },
    { "\"Scavenger\"", "Killradio", "Raised on Whipped Cream", "IG" },
    { "\"Notice of Eviction\"", "The Bronx", "The Bronx", "IG" },
    { "\"No W\"", "Ministry", "Houses of the Mol\xe9", "IG" },
    { "\"In My Head\"", "Queens of the Stone Age", "Lullabies to Paralyze", "IG" },
    { "\"Determined\"", "Mudvayne", "", "IG" },
    { "\"I Am Weightless\"", "Septembre", "Rule 3: Conceal Your Intentions", "IG" },
    { "\"Crashing Foreign Cars\"", "Helmet", "Size Matters", "IG" },
    { "\"Back on a Mission\"", "Cirrus", "Back on a Mission", "IG" },
    { "\"Black Betty\"", "SpiderBait", "Tonight Alright", "IG" },
    { "\"Nobody\"", "Skindred", "Babylon", "IG" },
    { "\"Skeptic\"", "Snapcase", "Bright Flashes", "IG" },
    { "\"The Celebration Song\"", "Unwritten Law", "The Way", "IG" },
};

const char *music_title(int s)  { return s >= 0 && s < MUSIC_SONGS ? k_song[s][0] : ""; }
const char *music_artist(int s) { return s >= 0 && s < MUSIC_SONGS ? k_song[s][1] : ""; }
const char *music_album(int s)  { return s >= 0 && s < MUSIC_SONGS ? k_song[s][2] : ""; }

#define MAX_STREAMS 64
#define RING (1 << 16)              /* stereo frames, ~2 s at 32 kHz */

static char g_viv[512];
static uint32_t g_mus_off, g_mus_size;
static int g_nstreams;
static uint32_t g_stream_off[MAX_STREAMS];   /* in the .mus */
static int g_song_stream[MUSIC_SONGS];       /* -1 unmapped */
static int g_ok;

/* playlist (FUN_0046baa0 state) */
static unsigned char g_pl_song[MUSIC_SONGS], g_pl_mode[MUSIC_SONGS];
static int g_shuffle;
static uint32_t g_fe_mask, g_ig_mask, g_fe_left, g_ig_left;
static int g_fe_last = -1, g_ig_last = -1;
static int g_mode, g_cur = -1, g_serial, g_first_fe = 1;
static float g_vol_fe = 1.0f, g_vol_ig = 1.0f;
static uint32_t g_rng = 0x2545f491u;

/* decoder thread <-> main: a request generation; the thread streams the
   requested song into the ring until its SCEl */
static volatile int g_req_song = -1, g_req_gen, g_thr_gen;
static volatile int g_ended_gen = -1;       /* the generation whose stream hit its end */
static volatile int g_paused;
static volatile int g_quit;
/* the ring: the decoder writes w, the audio thread reads r; a flush asks
   the reader to jump to flush_to */
static int16_t *g_ring;
static volatile uint32_t g_w, g_r;
static volatile int g_flush, g_flush_ack;
static volatile uint32_t g_flush_to;
static volatile float g_gain;               /* the reader's ramp target */
static float g_gain_now;
static double g_frac;

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint32_t le16(const unsigned char *p) { return (uint32_t)p[1] << 8 | p[0]; }

/* the MPF (xDFP v4): node offsets (u16 * 4) at 0x20, the node count at
   0x12, the event count at 0x0f, the event offsets right after the last
   node; a sample table of {offset / 0x80, ms} closes the file */
static int parse_mpf(const unsigned char *b, int n) {
    if (n < 0x40 || memcmp(b, "xDFP", 4) || b[4] != 4) return 0;
    int nevents = b[0x0f], nnodes = (int)le16(b + 0x12);
    if (0x20 + nnodes * 2 > n) return 0;
    /* the sample table: u32 pair {start/4, end/4} with start == pair + 8
       and end == the file size */
    int st = -1;
    for (int p = 0x20; p + 8 <= n; p += 4)
        if (le32(b + p) * 4 == (uint32_t)p + 8 && le32(b + p + 4) * 4 == (uint32_t)n) { st = p + 8; break; }
    if (st < 0) return 0;
    g_nstreams = (n - st) / 8;
    if (g_nstreams > MAX_STREAMS) g_nstreams = MAX_STREAMS;
    for (int i = 0; i < g_nstreams; i++) g_stream_off[i] = le32(b + st + i * 8) * 0x80u;
    /* events: offsets after the last node (a type-4 node is 0x14 bytes) */
    int last = (int)le16(b + 0x20 + (nnodes - 1) * 2) * 4;
    int evt = last + 0x14;
    if (evt + nevents * 2 > n) return 0;
    for (int s = 0; s < MUSIC_SONGS; s++) {
        g_song_stream[s] = -1;
        if (s >= nevents) continue;
        int eo = (int)le16(b + evt + s * 2) * 4;
        if (eo + 0x20 > n) continue;
        int node = (int)le16(b + eo + 0x1c);
        /* follow next links (0x7f, next) to the sample node (type 4) */
        for (int k = 0; k < 8 && node >= 0 && node < nnodes; k++) {
            int no = (int)le16(b + 0x20 + node * 2) * 4;
            if (no + 0x14 > n) break;
            if (b[no + 7] == 4) {
                int idx = (int)le16(b + no) - 1;
                if (idx >= 0 && idx < g_nstreams) g_song_stream[s] = idx;
                break;
            }
            if (b[no + 0x11] != 0x7f) break;
            node = b[no + 0x12];
        }
    }
    return 1;
}

/* ---- decoder thread ---- */
static uint32_t ring_free(void) { return RING - 1 - ((g_w - g_r) & (RING - 1)); }

static int decoder_thread(void *ud) {
    (void)ud;
    FILE *f = fopen(g_viv, "rb");
    if (!f) return 0;
    Eal3 *d = eal3_new();
    static unsigned char blk[0x10000];
    static int16_t pcm[2 * 8192];
    int gen = g_req_gen, song = -1;
    uint32_t pos = 0, end = 0;
    while (!g_quit) {
        if (gen != g_req_gen) {
            gen = g_req_gen; song = g_req_song;
            /* drop what's queued: the reader jumps to w */
            g_flush_to = g_w; g_flush_ack = 0; __sync_synchronize(); g_flush = 1;
            for (int i = 0; i < 100 && !g_flush_ack; i++) SDL_Delay(1);
            if (!g_flush_ack) { g_r = g_w; g_flush = 0; }
            eal3_reset(d);
            pos = end = 0;
            if (song >= 0 && g_song_stream[song] >= 0) {
                int si = g_song_stream[song];
                pos = g_mus_off + g_stream_off[si];
                end = g_mus_off + (si + 1 < g_nstreams ? g_stream_off[si + 1] : g_mus_size);
                fseek(f, (long)pos, SEEK_SET);
            }
            g_thr_gen = gen;
        }
        if (pos >= end || g_paused || ring_free() < 8192) { SDL_Delay(pos >= end ? 10 : 4); continue; }
        unsigned char h[8];
        if (fread(h, 1, 8, f) != 8) { pos = end; g_ended_gen = gen; continue; }
        uint32_t sz = le32(h + 4);
        if (!memcmp(h, "SCEl", 4) || sz < 8 || sz > sizeof blk || pos + sz > end) {
            pos = end; g_ended_gen = gen; continue;
        }
        if (fread(blk, 1, sz - 8, f) != sz - 8) { pos = end; g_ended_gen = gen; continue; }
        pos += sz;
        if (memcmp(h, "SCDl", 4) || sz < 0x14) continue;      /* SCHl / SCCl */
        int got = eal3_block(d, blk + 0x0c, (int)sz - 0x14, pcm, 8192);
        for (int i = 0; i < got; i++) {
            uint32_t w = g_w & (RING - 1);
            g_ring[w * 2] = pcm[i * 2]; g_ring[w * 2 + 1] = pcm[i * 2 + 1];
            __sync_synchronize();
            g_w = g_w + 1;
        }
    }
    eal3_free(d);
    fclose(f);
    return 0;
}

int music_init(const char *dataroot) {
    snprintf(g_viv, sizeof g_viv, "%s/SDATA/sdat.viv", dataroot);
    FILE *f = fopen(g_viv, "rb");
    if (!f) { printf("music: no %s\n", g_viv); return 0; }
    unsigned char h[16];
    if (fread(h, 1, 16, f) != 16 || memcmp(h, "BIG", 3)) { fclose(f); return 0; }
    uint32_t n = be32(h + 8), hl = be32(h + 12);
    unsigned char *dir = malloc(hl);
    if (!dir || fread(dir, 1, hl - 16, f) != hl - 16) { free(dir); fclose(f); return 0; }
    uint32_t mpf_off = 0, mpf_size = 0, p = 0;
    for (uint32_t i = 0; i < n && p + 9 <= hl - 16; i++) {
        uint32_t off = be32(dir + p), sz = be32(dir + p + 4); p += 8;
        const char *nm = (const char *)dir + p;
        size_t ln = strnlen(nm, hl - 16 - p); p += (uint32_t)ln + 1;
        if (!strcmp(nm, "pfdata/MusicSFx.mus")) { g_mus_off = off; g_mus_size = sz; }
        if (!strcmp(nm, "pfdata/MusicSFx.mpf")) { mpf_off = off; mpf_size = sz; }
    }
    free(dir);
    if (!g_mus_size || !mpf_size || mpf_size > 0x100000) { fclose(f); printf("music: no MusicSFx in sdat.viv\n"); return 0; }
    unsigned char *mpf = malloc(mpf_size);
    int ok = mpf && fseek(f, (long)mpf_off, SEEK_SET) == 0 && fread(mpf, 1, mpf_size, f) == mpf_size
             && parse_mpf(mpf, (int)mpf_size);
    free(mpf); fclose(f);
    if (!ok) { printf("music: MusicSFx.mpf not understood\n"); return 0; }
    g_ring = calloc(RING, 4);
    if (!g_ring) return 0;
    unsigned char song[MUSIC_SONGS], def[MUSIC_SONGS];
    music_default_playlist(song, def);
    music_set_playlist(song, def, 0);
    printf("music: %d streams; songs ->", g_nstreams);
    for (int s = 0; s < MUSIC_SONGS; s++) printf(" %d", g_song_stream[s]);
    printf("\n");
    if (!SDL_CreateThread(decoder_thread, "music", NULL)) return 0;
    g_ok = 1;
    return 1;
}

void music_default_playlist(unsigned char song[MUSIC_SONGS], unsigned char mode[MUSIC_SONGS]) {
    for (int s = 0; s < MUSIC_SONGS; s++) {   /* FUN_00515aa0 */
        song[s] = (unsigned char)s;
        mode[s] = !strcmp(k_song[s][3], "FE") ? 1 : !strcmp(k_song[s][3], "IG") ? 2 : !strcmp(k_song[s][3], "AL") ? 3 : 0;
    }
}

void music_set_playlist(const unsigned char song[MUSIC_SONGS], const unsigned char mode[MUSIC_SONGS], int shuffle) {
    uint32_t fe = 0, ig = 0;   /* FUN_00479220 */
    for (int k = 0; k < MUSIC_SONGS; k++) {
        int s = song[k] < MUSIC_SONGS ? song[k] : k;
        g_pl_song[k] = (unsigned char)s;
        g_pl_mode[k] = mode[k];
        if (mode[k] & 1) fe |= 1u << s;
        if (mode[k] & 2) ig |= 1u << s;
    }
    g_fe_mask = g_fe_left = fe; g_ig_mask = g_ig_left = ig;
    g_fe_last = g_ig_last = -1;
    g_shuffle = shuffle;
}

void music_set_volume(float fe, float ig) { g_vol_fe = fe; g_vol_ig = ig; }

/* FUN_0046b6f0 */
static int next_song(uint32_t mask, uint32_t *left, int *last) {
    if (!mask) return -1;
    if (!*left) { *left = mask; *last = -1; }
    if (!g_shuffle) {
        for (int slot = 0; slot < MUSIC_SONGS; slot++) {
            int s = g_pl_song[slot];   /* the jukebox's order */
            if (*last < slot && (*left & (1u << s))) { *last = slot; *left ^= 1u << s; return s; }
        }
        /* nothing after the last slot: the original returns -1 here and
           refills next time round */
        *left = mask; *last = -1;
        return next_song(mask, left, last);
    }
    int cnt = 0;
    for (int s = 0; s < MUSIC_SONGS; s++) cnt += (*left >> s) & 1;
    g_rng = g_rng * 1664525u + 1013904223u;
    int pick = (int)((g_rng >> 8) % (uint32_t)cnt), k = 0;
    for (int s = 0; s < MUSIC_SONGS; s++)
        if (*left & (1u << s)) { if (k == pick) { *left ^= 1u << s; return s; } k++; }
    return -1;
}

static void play(int song) {
    g_cur = song;
    g_req_song = song;
    __sync_synchronize();
    g_req_gen = g_req_gen + 1;
    if (song >= 0) g_serial++;
    if (song >= 0) printf("music: Play EATRAX %d %s\n", song, music_title(song));
}

static void start_next(void) {
    int s;
    if (g_mode == MUSIC_FE) {
        /* boot (FUN_0047ac50 state 1, param -2): slot 0 in the front-end
           list leaves song 0 the only one remaining */
        if (g_first_fe && (g_pl_mode[0] & 1)) { g_fe_left = 1; g_fe_last = -1; }
        g_first_fe = 0;
        s = next_song(g_fe_mask, &g_fe_left, &g_fe_last);
    } else if (g_mode == MUSIC_IG) s = next_song(g_ig_mask, &g_ig_left, &g_ig_last);
    else s = -1;
    play(s);
}

void music_set_mode(int mode) {
    if (!g_ok || mode == g_mode) return;
    g_mode = mode;
    g_paused = 0;
    start_next();
}

void music_pause(int on) { g_paused = on; }

void music_play_song(int song) {
    if (!g_ok || song < 0 || song >= MUSIC_SONGS) return;
    g_paused = 0;
    play(song);
}

void music_update(void) {
    if (!g_ok || g_mode == MUSIC_OFF || g_paused) return;
    /* the stream ended and the ring has played out (music_mix needs two
       frames to interpolate, so a lone last frame never drains) */
    if (g_ended_gen == g_req_gen && g_thr_gen == g_req_gen && ((g_w - g_r) & (RING - 1)) < 2)
        start_next();
}

int music_current(void) { return g_cur; }
int music_song_serial(void) { return g_serial; }

void music_mix(float *lr, int n, int rate) {
    if (!g_ring) return;
    if (g_flush) { g_r = g_flush_to; g_frac = 0; __sync_synchronize(); g_flush = 0; g_flush_ack = 1; }
    float target = g_paused ? 0.0f : (g_mode == MUSIC_FE ? g_vol_fe : g_mode == MUSIC_IG ? g_vol_ig : 0.0f);
    if (g_paused && g_gain_now <= 0.0f) return;
    float step = 32000.0f / (float)rate, ramp = 1.0f / (0.03f * (float)rate);
    for (int i = 0; i < n; i++) {
        uint32_t avail = (g_w - g_r) & (RING - 1);
        if (avail < 2) break;
        if (g_gain_now < target) { g_gain_now += ramp; if (g_gain_now > target) g_gain_now = target; }
        else if (g_gain_now > target) { g_gain_now -= ramp; if (g_gain_now < target) g_gain_now = target; }
        if (g_paused && g_gain_now <= 0.0f) break;      /* hold the position */
        uint32_t a = g_r & (RING - 1), b = (g_r + 1) & (RING - 1);
        float fr = (float)g_frac, k = g_gain_now / 32768.0f;
        lr[i * 2]     += ((float)g_ring[a * 2]     * (1.0f - fr) + (float)g_ring[b * 2]     * fr) * k;
        lr[i * 2 + 1] += ((float)g_ring[a * 2 + 1] * (1.0f - fr) + (float)g_ring[b * 2 + 1] * fr) * k;
        g_frac += step;
        while (g_frac >= 1.0) { g_frac -= 1.0; g_r = g_r + 1; }
    }
}
