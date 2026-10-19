/* EALayer3 v1a decoder (see eal3.h) */
#include "eal3.h"
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

/* MSB-first bit reader over one block */
typedef struct { const unsigned char *b; int nbits, pos; } Bits;
static uint32_t bget(Bits *r, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        int p = r->pos++;
        int bit = p < r->nbits ? (r->b[p >> 3] >> (7 - (p & 7))) & 1 : 0;
        v = (v << 1) | (uint32_t)bit;
    }
    return v;
}
typedef struct { unsigned char *b; int pos; } BitW;
static void bput(BitW *w, int n, uint32_t v) {
    for (int i = n - 1; i >= 0; i--) {
        int bit = (v >> i) & 1, p = w->pos++;
        if (bit) w->b[p >> 3] |= (unsigned char)(0x80 >> (p & 7));
    }
}

/* one granule as parsed (its main data copied out, so a pair may cross a block) */
typedef struct {
    uint32_t vidx, sridx, cmode, mext, gran;
    uint32_t scfsi[2], mds[2], o1[2], o2[2];
    int ch, mpeg1, rate;
    unsigned char data[2048]; int data_bits;
} Gran;

struct Eal3 {
    mp3dec_t mp3;
    int synced, discard, ch, rate;
    int have0; Gran g0, g1;
    unsigned char frame[2 * 1440 + 8];
};

static const int k_ver[4] = { 3, -1, 2, 1 };
static const int k_rate[4][4] = { { 11025, 12000, 8000, -1 }, { -1, -1, -1, -1 },
                                  { 22050, 24000, 16000, -1 }, { 44100, 48000, 32000, -1 } };

Eal3 *eal3_new(void) { Eal3 *d = (Eal3 *)calloc(1, sizeof *d); if (d) eal3_reset(d); return d; }
void eal3_free(Eal3 *d) { free(d); }
void eal3_reset(Eal3 *d) {
    mp3dec_init(&d->mp3);
    d->synced = 0; d->have0 = 0;
    d->discard = 576 + 529;          /* one granule + the synthesis filter delay */
}
int eal3_rate(const Eal3 *d) { return d->rate; }
int eal3_channels(const Eal3 *d) { return d->ch; }

/* the common part of an EA-frame at the reader (byte aligned); its size in bytes, -1 bad */
static int parse_common(Bits *r, Gran *g) {
    int start = r->pos;
    g->vidx = bget(r, 2); g->sridx = bget(r, 2); g->cmode = bget(r, 2); g->mext = bget(r, 2);
    if (!g->vidx && !g->sridx && !g->cmode && !g->mext) return -1;   /* empty frame */
    int ver = k_ver[g->vidx];
    if (ver < 0 || k_rate[g->vidx][g->sridx] < 0) return -1;
    g->rate = k_rate[g->vidx][g->sridx];
    g->ch = g->cmode == 3 ? 1 : 2;
    g->mpeg1 = ver == 1;
    int o2bits = g->mpeg1 ? 15 : 19;
    g->gran = bget(r, 1);
    if (g->mpeg1 && g->gran == 1)
        for (int i = 0; i < g->ch; i++) g->scfsi[i] = bget(r, 4);
    int total = 0;
    for (int i = 0; i < g->ch; i++) {
        g->mds[i] = bget(r, 12); g->o1[i] = bget(r, 32); g->o2[i] = bget(r, o2bits);
        total += (int)g->mds[i];
    }
    if (total > (int)sizeof g->data * 8) return -1;
    memset(g->data, 0, sizeof g->data);
    BitW w = { g->data, 0 };
    for (int i = 0; i < total; i++) bput(&w, 1, bget(r, 1));
    g->data_bits = total;
    int bits = r->pos - start;
    if (bits % 8) { r->pos += 8 - bits % 8; bits += 8 - bits % 8; }
    return bits / 8;
}

/* a standard MPEG-1 Layer III frame from granules 0 and 1: 320 kbps
   (index 14), no bit reservoir; 1440 bytes at 32 kHz */
static int rebuild(Eal3 *d, const Gran *a, const Gran *b) {
    int fsize = 144 * 320000 / a->rate;
    if (fsize > (int)sizeof d->frame - 8) return -1;
    memset(d->frame, 0, (size_t)fsize);
    BitW w = { d->frame, 0 };
    bput(&w, 11, 0x7ff); bput(&w, 2, a->vidx); bput(&w, 2, 1); bput(&w, 1, 1);
    bput(&w, 4, 14); bput(&w, 2, a->sridx); bput(&w, 1, 0); bput(&w, 1, 0);
    bput(&w, 2, a->cmode); bput(&w, 2, a->mext); bput(&w, 1, 1); bput(&w, 1, 1); bput(&w, 2, 0);
    bput(&w, 9, 0); bput(&w, a->ch == 1 ? 5 : 3, 0);
    for (int i = 0; i < b->ch; i++) bput(&w, 4, b->scfsi[i]);          /* granule 1 carries scfsi */
    for (int i = 0; i < a->ch; i++) { bput(&w, 12, a->mds[i]); bput(&w, 32, a->o1[i]); bput(&w, 15, a->o2[i]); }
    for (int i = 0; i < b->ch; i++) { bput(&w, 12, b->mds[i]); bput(&w, 32, b->o1[i]); bput(&w, 15, b->o2[i]); }
    if ((w.pos + a->data_bits + b->data_bits + 7) / 8 > fsize) return -1;
    Bits ra = { a->data, a->data_bits, 0 }, rb = { b->data, b->data_bits, 0 };
    for (int i = 0; i < a->data_bits; i++) bput(&w, 1, bget(&ra, 1));
    for (int i = 0; i < b->data_bits; i++) bput(&w, 1, bget(&rb, 1));
    return fsize;
}

/* decode one rebuilt frame; frames out (after the discard) */
static int decode(Eal3 *d, int fsize, int16_t *out, int max) {
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info;
    int n;
    if (!d->synced) {   /* the first frame: give the sync search a second header to match */
        memcpy(d->frame + fsize, d->frame, 4);
        n = mp3dec_decode_frame(&d->mp3, d->frame, fsize + 4, pcm, &info);
        d->synced = n > 0;
    } else n = mp3dec_decode_frame(&d->mp3, d->frame, fsize, pcm, &info);
    if (n <= 0) return 0;
    int ch = info.channels, skip = 0;
    if (d->discard > 0) { skip = d->discard < n ? d->discard : n; d->discard -= skip; }
    int k = 0;
    for (int i = skip; i < n && k < max; i++, k++) {
        out[k * 2] = pcm[i * ch];
        out[k * 2 + 1] = pcm[i * ch + (ch > 1)];
    }
    return k;
}

int eal3_block(Eal3 *d, const unsigned char *p, int len, int16_t *out, int max) {
    Bits r = { p, len * 8, 0 };
    int got = 0;
    while (r.pos + 8 <= r.nbits) {
        int at = r.pos / 8;
        uint32_t flag = bget(&r, 8);
        if (flag != 0x00 && flag != 0xee) return got ? got : -1;
        if (flag == 0xee) {   /* v1a: a PCM block without an EA-frame (big-endian, channels packed) */
            uint32_t offs = bget(&r, 16), pcmn = bget(&r, 16);
            (void)offs;
            int ch = d->ch ? d->ch : 2;
            const unsigned char *s = p + r.pos / 8;
            if (r.pos / 8 + (int)pcmn * ch * 2 > len) return -1;
            for (uint32_t i = 0; i < pcmn && got < max; i++, got++) {
                int16_t l = (int16_t)((s[(i * ch) * 2] << 8) | s[(i * ch) * 2 + 1]);
                int16_t rr = ch > 1 ? (int16_t)((s[(i * ch + 1) * 2] << 8) | s[(i * ch + 1) * 2 + 1]) : l;
                out[got * 2] = l; out[got * 2 + 1] = rr;
            }
            d->discard += (int)pcmn;   /* the decoded samples it stands in for */
            r.pos += (int)pcmn * ch * 16;
            continue;
        }
        Gran *g = d->have0 ? &d->g1 : &d->g0;
        int cs = parse_common(&r, g);
        if (cs < 0) return got ? got : -1;
        (void)at;
        d->ch = g->ch; d->rate = g->rate;
        if (!g->mpeg1) {      /* MPEG-2: one granule is a frame (not seen in the music) */
            return got ? got : -1;
        }
        if (!d->have0) {
            if (g->gran != 0) continue;            /* resync on a granule 0 */
            d->have0 = 1;
            continue;
        }
        d->have0 = 0;
        if (d->g1.gran != 1) { d->g0 = d->g1; d->have0 = d->g0.gran == 0; continue; }
        int fs = rebuild(d, &d->g0, &d->g1);
        if (fs < 0) continue;
        got += decode(d, fs, out + got * 2, max - got);
    }
    return got;
}
