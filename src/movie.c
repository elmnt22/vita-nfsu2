/* movie.c - EA movie player core: container, VP6 video, EA-XA R3 audio.
 *
 * Container: a flat stream of chunks {char tag[4]; u32 le size incl. the
 * 8-byte header; payload}. MVhd = video header, SCHl = audio header,
 * SCCl = audio block count, MV0K / MV0F = VP6 key / inter frame,
 * SCDl = audio block, SCEl = end of audio (video chunks may follow it).
 *
 * Video: On2 VP6 as used by EA (stored bottom-up like the AVI "VP60"
 * flavour, not the top-down Flash "VP6F"). All game movies are VP6.0
 * (sub-version 7), simple profile: no filter header (so no loop filter,
 * bilinear sub-pel motion compensation), not interlaced, separate
 * coefficient partition. Modes / vectors / models are bool-coded; the
 * coefficients are huffman-coded in ~95% of frames and bool-coded in the
 * rest, so both coefficient paths are needed. Written from the public
 * format description; libavcodec vp56.c / vp6.c were the reference for the
 * bitstream details (the tables below are format constants), and output is
 * bit-exact with FFmpeg on every frame of every game movie. Filter headers
 * (bicubic MC / loop filter, VP6.1+) and interlacing are not implemented:
 * no game movie uses them, and such frames fail as decode errors.
 *
 * The decoder works in "coded" coordinates (row 0 = first coded row =
 * bottom of the picture). Coded pixel (x, y) of a plane lives at
 * corg + y*cstride + x with corg pointing at the last memory row and
 * cstride = -stride, so memory ends up top-down for the caller at no cost.
 *
 * MVhd's frame count includes video chunks stored after SCEl (the last
 * 5..134 frames of the story scenes); they are real frames and are played.
 * The tutorials instead end with ~3 s of audio after the last frame.
 *
 * Audio: SCHl carries an EA "PT" tag header (here wrapped in 'GSTR'):
 * version 3 without a codec tag means EA-XA R3. Each SCDl = u32be sample
 * count, u32be per-channel data offset, then per channel 15-byte EA-XA
 * frames of 28 samples (0xEE frames = 2 history + 28 raw s16be samples);
 * predictor history carries over from block to block. */
#include "movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * bool (range) decoder
 * ====================================================================== */

typedef struct {
    const uint8_t *p, *end;
    unsigned code;
    int high, bits;     /* bits is negated: -bits = bits still buffered */
    int end_hits;
} Rac;

static const uint8_t RAC_NORM[256] = {
    8,7,6,6,5,5,5,5,4,4,4,4,4,4,4,4, 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};   /* entries 128..255 are 0 */

/* the chunk buffer is zero-padded, so reading a little past `end` is safe */
static void rac_init(Rac *c, const uint8_t *p, const uint8_t *end) {
    c->high = 255; c->bits = -16; c->end_hits = 0;
    c->code = (unsigned)p[0] << 16 | p[1] << 8 | p[2];
    c->p = p + 3; c->end = end;
}

static inline unsigned rac_renorm(Rac *c) {
    int shift = RAC_NORM[c->high];
    unsigned code = c->code << shift;
    c->high <<= shift;
    c->bits += shift;
    if (c->bits >= 0 && c->p < c->end) {
        code |= (unsigned)(c->p[0] << 8 | c->p[1]) << c->bits;
        c->p += 2;
        c->bits -= 16;
    }
    return code;
}

static inline int rac_prob(Rac *c, int prob) {
    unsigned code = rac_renorm(c);
    unsigned low = 1 + (((c->high - 1) * prob) >> 8);
    unsigned lows = low << 16;
    if (code >= lows) { c->high -= low; c->code = code - lows; return 1; }
    c->high = low; c->code = code; return 0;
}

static inline int rac_bit(Rac *c) {
    unsigned code = rac_renorm(c);
    unsigned low = (c->high + 1) >> 1;
    unsigned lows = low << 16;
    if (code >= lows) { c->high -= low; c->code = code - lows; return 1; }
    c->high = low; c->code = code; return 0;
}

static int rac_bits(Rac *c, int n) {
    int v = 0;
    while (n--) v = v << 1 | rac_bit(c);
    return v;
}

/* 7-bit probability, never 0 */
static int rac_prob7(Rac *c) {
    int v = rac_bits(c, 7) << 1;
    return v + !v;
}

/* Binary tree walk: node = {jump, prob index}; a leaf is {-symbol, 0}
 * (jump <= 0). A 1 bit jumps `jump` entries ahead, a 0 bit goes to the
 * next entry. */
typedef struct { int8_t jump, prob; } TreeNode;

static int rac_tree(Rac *c, const TreeNode *t, const uint8_t *probs) {
    while (t->jump > 0)
        t += rac_prob(c, probs[t->prob]) ? t->jump : 1;
    return -t->jump;
}

/* true once the coder has run dry many times (= corrupt / desync) */
static int rac_exhausted(Rac *c) {
    if (c->p >= c->end && c->bits >= 0) c->end_hits++;
    return c->end_hits > 10;
}

/* ======================================================================
 * bit reader (huffman coefficient mode only)
 * ====================================================================== */

typedef struct { const uint8_t *p; int pos, len; } Bits;   /* pos/len in bits */

/* next n (<= 25) bits, msb first, without consuming them; the buffer is
 * zero-padded so the 4-byte load may run past the end */
static inline unsigned bits_peek(const Bits *b, int n) {
    const uint8_t *q = b->p + (b->pos >> 3);
    unsigned w = (unsigned)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3];
    return (w << (b->pos & 7)) >> (32 - n);
}

static inline int bits_get(Bits *b, int n) {
    int v = (int)bits_peek(b, n);
    b->pos += n;
    return v;
}

/* ======================================================================
 * VP6 constants
 * ====================================================================== */

enum { REF_NONE = -1, REF_CUR = 0, REF_PREV = 1, REF_GOLD = 2 };

enum {  /* macroblock types */
    MB_NOVEC_PF, MB_INTRA, MB_DELTA_PF, MB_V1_PF, MB_V2_PF,
    MB_NOVEC_GF, MB_DELTA_GF, MB_4V, MB_V1_GF, MB_V2_GF
};
static const int8_t MB_REF[10] = {
    REF_PREV, REF_CUR, REF_PREV, REF_PREV, REF_PREV,
    REF_GOLD, REF_GOLD, REF_PREV, REF_GOLD, REF_GOLD
};

static const uint8_t ZIGZAG[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static const uint8_t AC_DEQUANT[64] = {
    94, 92, 90, 88, 86, 82, 78, 74, 70, 66, 62, 58, 54, 53, 52, 51,
    50, 49, 48, 47, 46, 45, 44, 43, 42, 40, 39, 37, 36, 35, 34, 33,
    32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,
    16, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,
};
static const uint8_t DC_DEQUANT[64] = {
    47, 47, 47, 47, 45, 43, 43, 43, 43, 43, 42, 41, 41, 40, 40, 40,
    40, 35, 35, 35, 35, 33, 33, 33, 33, 32, 32, 32, 27, 27, 26, 26,
    25, 25, 24, 24, 23, 23, 19, 19, 19, 19, 18, 18, 17, 16, 16, 16,
    16, 16, 15, 11, 11, 11, 10, 10,  9,  8,  7,  5,  3,  3,  2,  2,
};

/* --- macroblock type models --- */
static const uint8_t DEF_MB_TYPE_STATS[3][10][2] = {
    { {69,42},{1,2},{1,7},{44,42},{6,22},{1,3},{0,2},{1,5},{0,1},{0,0} },
    { {229,8},{1,1},{0,8},{0,0},{0,0},{1,2},{0,1},{0,0},{1,1},{0,0} },
    { {122,35},{1,1},{1,6},{46,34},{0,0},{1,2},{0,1},{0,1},{1,1},{0,0} },
};

static const uint8_t PRE_MB_TYPE_STATS[16][3][10][2] = {
  { { {9,15},{32,25},{7,19},{9,21},{1,12},{14,12},{3,18},{14,23},{3,10},{0,4} },
    { {41,22},{1,0},{1,31},{0,0},{0,0},{0,1},{1,7},{0,1},{98,25},{4,10} },
    { {2,3},{2,3},{0,2},{0,2},{0,0},{11,4},{1,4},{0,2},{3,2},{0,4} } },
  { { {48,39},{1,2},{11,27},{29,44},{7,27},{1,4},{0,3},{1,6},{1,2},{0,0} },
    { {123,37},{6,4},{1,27},{0,0},{0,0},{5,8},{1,7},{0,1},{12,10},{0,2} },
    { {49,46},{3,4},{7,31},{42,41},{0,0},{2,6},{1,7},{1,4},{2,4},{0,1} } },
  { { {21,32},{1,2},{4,10},{32,43},{6,23},{2,3},{1,19},{1,6},{12,21},{0,7} },
    { {26,14},{14,12},{0,24},{0,0},{0,0},{55,17},{1,9},{0,36},{5,7},{1,3} },
    { {26,25},{1,1},{2,10},{67,39},{0,0},{1,1},{0,14},{0,2},{31,26},{1,6} } },
  { { {69,83},{0,0},{0,2},{10,29},{3,12},{0,1},{0,3},{0,3},{2,2},{0,0} },
    { {209,5},{0,0},{0,27},{0,0},{0,0},{0,1},{0,1},{0,1},{0,0},{0,0} },
    { {103,46},{1,2},{2,10},{33,42},{0,0},{1,4},{0,3},{0,1},{1,3},{0,0} } },
  { { {11,20},{1,4},{18,36},{43,48},{13,35},{0,2},{0,5},{3,12},{1,2},{0,0} },
    { {2,5},{4,5},{0,121},{0,0},{0,0},{0,3},{2,4},{1,4},{2,2},{0,1} },
    { {14,31},{9,13},{14,54},{22,29},{0,0},{2,6},{4,18},{6,13},{1,5},{0,1} } },
  { { {70,44},{0,1},{2,10},{37,46},{8,26},{0,2},{0,2},{0,2},{0,1},{0,0} },
    { {175,5},{0,1},{0,48},{0,0},{0,0},{0,2},{0,1},{0,2},{0,1},{0,0} },
    { {85,39},{0,0},{1,9},{69,40},{0,0},{0,1},{0,3},{0,1},{2,3},{0,0} } },
  { { {8,15},{0,1},{8,21},{74,53},{22,42},{0,1},{0,2},{0,3},{1,2},{0,0} },
    { {83,5},{2,3},{0,102},{0,0},{0,0},{1,3},{0,2},{0,1},{0,0},{0,0} },
    { {31,28},{0,0},{3,14},{130,34},{0,0},{0,1},{0,3},{0,1},{3,3},{0,1} } },
  { { {141,42},{0,0},{1,4},{11,24},{1,11},{0,1},{0,1},{0,2},{0,0},{0,0} },
    { {233,6},{0,0},{0,8},{0,0},{0,0},{0,1},{0,1},{0,0},{0,1},{0,0} },
    { {171,25},{0,0},{1,5},{25,21},{0,0},{0,1},{0,1},{0,0},{0,0},{0,0} } },
  { { {8,19},{4,10},{24,45},{21,37},{9,29},{0,3},{1,7},{11,25},{0,2},{0,1} },
    { {34,16},{112,21},{1,28},{0,0},{0,0},{6,8},{1,7},{0,3},{2,5},{0,2} },
    { {17,21},{68,29},{6,15},{13,22},{0,0},{6,12},{3,14},{4,10},{1,7},{0,3} } },
  { { {46,42},{0,1},{2,10},{54,51},{10,30},{0,2},{0,2},{0,1},{0,1},{0,0} },
    { {159,35},{2,2},{0,25},{0,0},{0,0},{3,6},{0,5},{0,1},{4,4},{0,1} },
    { {51,39},{0,1},{2,12},{91,44},{0,0},{0,2},{0,3},{0,1},{2,3},{0,1} } },
  { { {28,32},{0,0},{3,10},{75,51},{14,33},{0,1},{0,2},{0,1},{1,2},{0,0} },
    { {75,39},{5,7},{2,48},{0,0},{0,0},{3,11},{2,16},{1,4},{7,10},{0,2} },
    { {81,25},{0,0},{2,9},{106,26},{0,0},{0,1},{0,1},{0,1},{1,1},{0,0} } },
  { { {100,46},{0,1},{3,9},{21,37},{5,20},{0,1},{0,2},{1,2},{0,1},{0,0} },
    { {212,21},{0,1},{0,9},{0,0},{0,0},{1,2},{0,2},{0,0},{2,2},{0,0} },
    { {140,37},{0,1},{1,8},{24,33},{0,0},{1,2},{0,2},{0,1},{1,2},{0,0} } },
  { { {27,29},{0,1},{9,25},{53,51},{12,34},{0,1},{0,3},{1,5},{0,2},{0,0} },
    { {4,2},{0,0},{0,172},{0,0},{0,0},{0,1},{0,2},{0,0},{2,0},{0,0} },
    { {14,23},{1,3},{11,53},{90,31},{0,0},{0,3},{1,5},{2,6},{1,2},{0,0} } },
  { { {80,38},{0,0},{1,4},{69,33},{5,16},{0,1},{0,1},{0,0},{0,1},{0,0} },
    { {187,22},{1,1},{0,17},{0,0},{0,0},{3,6},{0,4},{0,1},{4,4},{0,1} },
    { {123,29},{0,0},{1,7},{57,30},{0,0},{0,1},{0,1},{0,1},{0,1},{0,0} } },
  { { {16,20},{0,0},{2,8},{104,49},{15,33},{0,1},{0,1},{0,1},{1,1},{0,0} },
    { {133,6},{1,2},{1,70},{0,0},{0,0},{0,2},{0,4},{0,3},{1,1},{0,0} },
    { {13,14},{0,0},{4,20},{175,20},{0,0},{0,1},{0,1},{0,1},{1,1},{0,0} } },
  { { {194,16},{0,0},{1,1},{1,9},{1,3},{0,0},{0,1},{0,1},{0,0},{0,0} },
    { {251,1},{0,0},{0,2},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0} },
    { {202,23},{0,0},{1,3},{2,9},{0,0},{0,1},{0,1},{0,1},{0,0},{0,0} } },
};

static const uint8_t MB_TYPE_MODEL_MODEL[6] = { 171, 83, 199, 140, 125, 104 };

/* stats delta magnitude */
static const TreeNode TREE_MBTM[] = {
    {4,0}, {2,1}, {-8,0}, {-4,0}, {8,2}, {6,3}, {4,4}, {2,5},
    {-24,0}, {-20,0}, {-16,0}, {-12,0}, {0,0},
};
/* macroblock type (probability indices 1..9 of the per-context model) */
static const TreeNode TREE_MBTYPE[] = {
    {8,1}, {4,2}, {2,4}, {-MB_NOVEC_PF,0}, {-MB_DELTA_PF,0},
    {2,5}, {-MB_V1_PF,0}, {-MB_V2_PF,0},
    {4,3}, {2,6}, {-MB_INTRA,0}, {-MB_4V,0},
    {4,7}, {2,8}, {-MB_NOVEC_GF,0}, {-MB_DELTA_GF,0},
    {2,9}, {-MB_V1_GF,0}, {-MB_V2_GF,0},
};

/* candidate predictor positions (dx, dy), closest first */
static const int8_t MV_CANDIDATE_POS[12][2] = {
    {0,-1}, {-1,0}, {-1,-1}, {1,-1}, {0,-2}, {-2,0},
    {-2,-1}, {-1,-2}, {1,-2}, {2,-1}, {-2,-2}, {2,-2},
};

/* --- motion vector models --- */
static const uint8_t DEF_FDV[2][8] = {
    { 247, 210, 135, 68, 138, 220, 239, 246 },
    { 244, 184, 201, 44, 173, 221, 239, 253 },
};
static const uint8_t DEF_PDV[2][7] = {
    { 225, 146, 172, 147, 214,  39, 156 },
    { 204, 170, 119, 235, 140, 230, 228 },
};
static const uint8_t SIG_DCT_PCT[2][2] = { { 237, 246 }, { 231, 243 } };
static const uint8_t PDV_PCT[2][7] = {
    { 253, 253, 254, 254, 254, 254, 254 },
    { 245, 253, 254, 254, 254, 254, 254 },
};
static const uint8_t FDV_PCT[2][8] = {
    { 254, 254, 254, 254, 254, 250, 250, 252 },
    { 254, 254, 254, 254, 254, 251, 251, 254 },
};
/* short vector delta 0..7 */
static const TreeNode TREE_PVA[] = {
    {8,0}, {4,1}, {2,2}, {0,0}, {-1,0}, {2,3}, {-2,0}, {-3,0},
    {4,4}, {2,5}, {-4,0}, {-5,0}, {2,6}, {-6,0}, {-7,0},
};

/* --- coefficient models --- */
static const uint8_t DEF_COEFF_REORDER[64] = {
     0,  0,  1,  1,  1,  2,  2,  2,  2,  2,  2,  3,  3,  4,  4,  4,
     5,  5,  5,  5,  6,  6,  7,  7,  7,  7,  7,  8,  8,  9,  9,  9,
     9,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12,
    12, 13, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15,
};
static const uint8_t DEF_RUNV[2][14] = {
    { 198, 197, 196, 146, 198, 204, 169, 142, 130, 136, 149, 149, 191, 249 },
    { 135, 201, 181, 154,  98, 117, 132, 126, 146, 169, 184, 240, 246, 254 },
};
static const uint8_t DCCV_PCT[2][11] = {
    { 146, 255, 181, 207, 232, 243, 238, 251, 244, 250, 249 },
    { 179, 255, 214, 240, 250, 255, 244, 255, 255, 255, 255 },
};
static const uint8_t COEFF_REORDER_PCT[64] = {
    255, 132, 132, 159, 153, 151, 161, 170, 164, 162, 136, 110, 103, 114, 129, 118,
    124, 125, 132, 136, 114, 110, 142, 135, 134, 123, 143, 126, 153, 183, 166, 161,
    171, 180, 179, 164, 203, 218, 225, 217, 215, 206, 203, 217, 229, 241, 248, 243,
    253, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};
static const uint8_t RUNV_PCT[2][14] = {
    { 219, 246, 238, 249, 232, 239, 249, 255, 248, 253, 239, 244, 241, 248 },
    { 198, 232, 251, 253, 219, 241, 253, 255, 248, 249, 244, 238, 251, 255 },
};
/* [code type][plane type][coeff group][node]; unlisted = 255 */
static const uint8_t RACT_PCT[3][2][6][11] = {
  { { { 227, 246, 230, 247, 244, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 209, 231, 231, 249, 249, 253, 255, 255, 255 },
      { 255, 255, 225, 242, 241, 251, 253, 255, 255, 255, 255 },
      { 255, 255, 241, 253, 252, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 248, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
    { { 240, 255, 248, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 240, 253, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } },
  { { { 206, 203, 227, 239, 247, 255, 253, 255, 255, 255, 255 },
      { 207, 199, 220, 236, 243, 252, 252, 255, 255, 255, 255 },
      { 212, 219, 230, 243, 244, 253, 252, 255, 255, 255, 255 },
      { 236, 237, 247, 252, 253, 255, 255, 255, 255, 255, 255 },
      { 240, 240, 248, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
    { { 230, 233, 249, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 238, 238, 250, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 248, 251, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } },
  { { { 225, 239, 227, 231, 244, 253, 243, 255, 255, 253, 255 },
      { 232, 234, 224, 228, 242, 249, 242, 252, 251, 251, 255 },
      { 235, 249, 238, 240, 251, 255, 249, 255, 253, 253, 255 },
      { 249, 253, 251, 250, 255, 255, 255, 255, 255, 255, 255 },
      { 251, 250, 249, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } },
    { { 243, 244, 250, 250, 255, 255, 255, 255, 255, 255, 255 },
      { 249, 248, 250, 253, 255, 255, 255, 255, 255, 255, 255 },
      { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
      { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 } } },
};
/* DC coding-type probs = linear combination of the DC value probs,
 * per context (number of neighbours with a non-zero DC) */
static const int DCCV_LC[3][5][2] = {
    { { 122, 133 }, { 0, 1 }, {  78, 171 }, { 139, 117 }, { 168, 79 } },
    { { 133,  51 }, { 0, 1 }, { 169,  71 }, { 214,  44 }, { 210, 38 } },
    { { 142, -16 }, { 0, 1 }, { 221, -30 }, { 246,  -3 }, { 203, 17 } },
};
static const uint8_t COEFF_GROUPS[64] = {
    0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};
/* large coefficient categories (DCT_CAT1..6): base value, extra bits - 1,
 * and the fixed probabilities of the extra bits (msb first) */
static const uint8_t COEFF_BIAS[11] = { 0, 1, 2, 3, 4, 5, 7, 11, 19, 35, 67 };
static const uint8_t COEFF_BIT_LENGTH[6] = { 0, 1, 2, 3, 4, 10 };
static const uint8_t COEFF_PARSE_TABLE[6][11] = {
    { 159 },
    { 145, 165 },
    { 140, 148, 173 },
    { 135, 140, 155, 176 },
    { 130, 134, 141, 157, 180 },
    { 129, 130, 133, 140, 153, 177, 196, 230, 243, 254, 254 },
};
/* large coefficient category, probs 6..10 of the value model */
static const TreeNode TREE_PC[] = {
    {4,6}, {2,7}, {0,0}, {-1,0}, {4,8}, {2,9}, {-2,0}, {-3,0},
    {2,10}, {-4,0}, {-5,0},
};
/* zero run length 1..8, 0 = long run */
static const TreeNode TREE_RUN[] = {
    {8,0}, {4,1}, {2,2}, {-1,0}, {-2,0}, {2,3}, {-3,0}, {-4,0},
    {8,4}, {4,5}, {2,6}, {-5,0}, {-6,0}, {2,7}, {-7,0}, {-8,0}, {0,0},
};
/* huffman mode: tree leaf order of the 12 value tokens / 9 run tokens */
static const uint8_t HUFF_COEFF_MAP[22] = {
    13, 14, 11, 0, 1, 15, 16, 18, 2, 17, 3, 4, 19, 20, 5, 6, 21, 22, 7, 8, 9, 10
};
static const uint8_t HUFF_RUN_MAP[16] = {
    10, 13, 11, 12, 0, 1, 2, 3, 14, 8, 15, 16, 4, 5, 6, 7
};

/* ======================================================================
 * decoder state
 * ====================================================================== */

typedef struct { int16_t x, y; } Mv;
typedef struct { uint8_t type; Mv mv; } MbInfo;
typedef struct { uint8_t not_null_dc; int8_t ref; int16_t dc; } RefDc;

/* huffman tree built from the probability models: inner node k (symbol
 * id size+k) has children child[0/1][k]; lut = first HUFF_LUT_BITS bits ->
 * symbol | length << 8, or 0x8000 | inner node for longer codes */
#define HUFF_LUT_BITS 9
typedef struct { int8_t child[2][12]; int root; uint16_t lut[1 << HUFF_LUT_BITS]; } Huff;

typedef struct {
    uint8_t coeff_reorder[64];
    uint8_t index_to_pos[64];        /* coded index -> zigzag position */
    uint8_t index_to_selector[64];   /* coded index -> idct size class */
    uint8_t vector_sig[2], vector_dct[2];
    uint8_t vector_pdv[2][7], vector_fdv[2][8];
    uint8_t coeff_dccv[2][11];
    uint8_t coeff_ract[2][3][6][11];
    uint8_t coeff_dcct[2][3][5];
    uint8_t coeff_runv[2][14];
    uint8_t mb_type[3][10][10];
    uint8_t mb_types_stats[3][10][2];
} Model;

typedef struct {
    uint8_t *mem;                    /* Y then U then V, top-down memory */
    uint8_t *corg[3];                /* coded origin per plane */
} Frame;

typedef struct {
    int mb_w, mb_h, cw, ch;          /* macroblocks, coded size */
    int ystride, uvstride;
    Frame fr[3];
    int cur, prev, gold;             /* indices into fr, -1 = none */
    int have_key;

    Rac c, cc, *ccp;
    Bits gb;
    int use_huffman;
    int sub_version, filter_header, golden_flag, key;
    int quantizer, dequant_dc, dequant_ac;

    Model model;
    MbInfo *mbs;
    RefDc *above;                    /* 4*mb_w + 6 */
    RefDc left[4];
    int above_idx[6];
    int16_t prev_dc[3][3];           /* [plane][ref] */

    int mb_type;
    Mv mv[6];
    Mv cand[2];
    int cand_pos;

    int16_t blk[6][64];              /* natural (row-major) layout */
    int selector[6];
    uint8_t has_ac[6];               /* any AC coefficient written */

    Huff dccv_h[2], runv_h[2], ract_h[2][3][4];
    unsigned nb_null[2][2];
} Vp6;

/* ======================================================================
 * header + model parsing
 * ====================================================================== */

static void set_quantizer(Vp6 *s, int q) {
    s->quantizer = q;
    s->dequant_dc = DC_DEQUANT[q] << 2;
    s->dequant_ac = AC_DEQUANT[q] << 2;
}

static int rb16(const uint8_t *p) { return p[0] << 8 | p[1]; }

/* Frame header. buf is zero-padded past buf+size. 0 = ok. */
static int parse_header(Vp6 *s, const uint8_t *buf, int size) {
    const uint8_t *end = buf + size;
    int sep = buf[0] & 1, coeff_offset = 0;
    s->key = !(buf[0] & 0x80);
    set_quantizer(s, (buf[0] >> 1) & 63);
    if (s->key) {
        if (size < 8) return -1;
        s->sub_version = buf[1] >> 3;
        s->filter_header = buf[1] & 6;
        if (s->sub_version > 8 || (buf[1] & 1)) return -1;  /* interlaced: unused by the game */
        const uint8_t *p = buf;
        if (sep || !s->filter_header) { coeff_offset = rb16(buf + 2); p += 2; }
        if (p[2] != s->mb_h || p[3] != s->mb_w) return -1;  /* size fixed at open */
        rac_init(&s->c, p + 6, end);
        rac_bits(&s->c, 2);
        s->golden_flag = 0;
    } else {
        if (!s->have_key || size < 4) return -1;
        const uint8_t *p = buf;
        if (sep || !s->filter_header) { coeff_offset = rb16(buf + 1); p += 2; }
        rac_init(&s->c, p + 1, end);
        s->golden_flag = rac_bit(&s->c);
        if (s->filter_header) {
            if (rac_bit(&s->c)) rac_bit(&s->c);   /* deblock flag (+ unused bit) */
            if (s->sub_version > 7) rac_bit(&s->c);
        }
    }
    /* Filter info (only present with a filter header) selects the bicubic
     * filter / loop filter; no game movie has one. */
    if (s->filter_header) return -1;
    s->use_huffman = rac_bit(&s->c);
    s->ccp = &s->c;
    if (coeff_offset) {
        if (coeff_offset >= size) return -1;
        if (s->use_huffman) {
            s->gb.p = buf + coeff_offset; s->gb.pos = 0;
            s->gb.len = (size - coeff_offset) * 8;
        } else {
            rac_init(&s->cc, buf + coeff_offset, end);
            s->ccp = &s->cc;
        }
    } else if (s->use_huffman) {
        return -1;   /* huffman needs a separate partition */
    }
    return 0;
}

static void coeff_order_init(Vp6 *s) {
    Model *m = &s->model;
    int idx = 1;
    m->index_to_pos[0] = 0;
    for (int i = 0; i < 16; i++)
        for (int pos = 1; pos < 64; pos++)
            if (m->coeff_reorder[pos] == i) m->index_to_pos[idx++] = pos;
    int max = 0;
    for (idx = 0; idx < 64; idx++) {
        if (m->index_to_pos[idx] > max) max = m->index_to_pos[idx];
        m->index_to_selector[idx] = max + (s->sub_version > 6);
    }
}

static void default_models(Vp6 *s) {
    Model *m = &s->model;
    m->vector_dct[0] = 0xA2; m->vector_dct[1] = 0xA4;
    m->vector_sig[0] = 0x80; m->vector_sig[1] = 0x80;
    memcpy(m->mb_types_stats, DEF_MB_TYPE_STATS, sizeof m->mb_types_stats);
    memcpy(m->vector_fdv, DEF_FDV, sizeof m->vector_fdv);
    memcpy(m->vector_pdv, DEF_PDV, sizeof m->vector_pdv);
    memcpy(m->coeff_runv, DEF_RUNV, sizeof m->coeff_runv);
    memcpy(m->coeff_reorder, DEF_COEFF_REORDER, sizeof m->coeff_reorder);
    coeff_order_init(s);
}

/* per-context macroblock type probabilities from the type statistics */
static void parse_mb_type_models(Vp6 *s) {
    Rac *c = &s->c;
    Model *m = &s->model;
    for (int ctx = 0; ctx < 3; ctx++) {
        if (rac_prob(c, 174))
            memcpy(m->mb_types_stats[ctx], PRE_MB_TYPE_STATS[rac_bits(c, 4)][ctx],
                   sizeof m->mb_types_stats[ctx]);
        if (rac_prob(c, 254)) {
            for (int type = 0; type < 10; type++)
                for (int i = 0; i < 2; i++) {
                    if (!rac_prob(c, 205)) continue;
                    int sign = rac_bit(c);
                    int delta = rac_tree(c, TREE_MBTM, MB_TYPE_MODEL_MODEL);
                    if (!delta) delta = 4 * rac_bits(c, 7);
                    m->mb_types_stats[ctx][type][i] += sign ? -delta : delta;
                }
        }
    }
    for (int ctx = 0; ctx < 3; ctx++) {
        int p[10];
        for (int t = 0; t < 10; t++) p[t] = 100 * m->mb_types_stats[ctx][t][1];
        for (int t = 0; t < 10; t++) {
            uint8_t *mt = m->mb_type[ctx][t];
            int s0 = m->mb_types_stats[ctx][t][0], s1 = m->mb_types_stats[ctx][t][1];
            mt[0] = 255 - (255 * s0) / (1 + s0 + s1);   /* "same as previous" */
            p[t] = 0;                                    /* exclude own type */
            int p02 = p[0] + p[2], p34 = p[3] + p[4], p0234 = p02 + p34;
            int p17 = p[1] + p[7], p56 = p[5] + p[6], p89 = p[8] + p[9];
            int p5689 = p56 + p89, p156789 = p17 + p5689;
            mt[1] = 1 + 255 * p0234 / (1 + p0234 + p156789);
            mt[2] = 1 + 255 * p02 / (1 + p0234);
            mt[3] = 1 + 255 * p17 / (1 + p156789);
            mt[4] = 1 + 255 * p[0] / (1 + p02);
            mt[5] = 1 + 255 * p[3] / (1 + p34);
            mt[6] = 1 + 255 * p[1] / (1 + p17);
            mt[7] = 1 + 255 * p56 / (1 + p5689);
            mt[8] = 1 + 255 * p[5] / (1 + p56);
            mt[9] = 1 + 255 * p[8] / (1 + p89);
            p[t] = 100 * s1;
        }
    }
}

static void parse_vector_models(Vp6 *s) {
    Rac *c = &s->c;
    Model *m = &s->model;
    for (int comp = 0; comp < 2; comp++) {
        if (rac_prob(c, SIG_DCT_PCT[comp][0])) m->vector_dct[comp] = rac_prob7(c);
        if (rac_prob(c, SIG_DCT_PCT[comp][1])) m->vector_sig[comp] = rac_prob7(c);
    }
    for (int comp = 0; comp < 2; comp++)
        for (int n = 0; n < 7; n++)
            if (rac_prob(c, PDV_PCT[comp][n])) m->vector_pdv[comp][n] = rac_prob7(c);
    for (int comp = 0; comp < 2; comp++)
        for (int n = 0; n < 8; n++)
            if (rac_prob(c, FDV_PCT[comp][n])) m->vector_fdv[comp][n] = rac_prob7(c);
}

/* Huffman tree from a binary probability model: leaf weights are the
 * products of the branch probabilities along the model's tree (map[] says
 * where each branch goes: < size = leaf symbol, else inner node), then a
 * classic huffman merge. Ties: nodes are kept sorted by (count, then
 * descending symbol) and a merged node is inserted before equal counts;
 * the smaller node gets bit 0 (same tree as FFmpeg's ff_huff_build_tree
 * with HNODE_FIRST, verified bit-exact on every game movie). */
static void huff_fill_lut(Huff *h, int node, int code, int len, int size) {
    if (node < size || len == HUFF_LUT_BITS) {
        int n = 1 << (HUFF_LUT_BITS - len), at = code << (HUFF_LUT_BITS - len);
        uint16_t e = node < size ? (uint16_t)(node | len << 8) : (uint16_t)(0x8000 | node);
        for (int i = 0; i < n; i++) h->lut[at + i] = e;
        return;
    }
    huff_fill_lut(h, h->child[0][node - size], code << 1, len + 1, size);
    huff_fill_lut(h, h->child[1][node - size], code << 1 | 1, len + 1, size);
}

static void build_huff(Huff *h, const uint8_t *model, const uint8_t *map, int size) {
    int count[2 * 12], inner[12];
    inner[0] = 256;
    for (int i = 0; i < size - 1; i++) {
        int a = inner[i] * model[i] >> 8, b = inner[i] * (255 - model[i]) >> 8;
        int la = map[2*i], lb = map[2*i+1];
        if (la >= size) inner[la - size] = a + !a; else count[la] = a + !a;
        if (lb >= size) inner[lb - size] = b + !b; else count[lb] = b + !b;
    }
    int order[2 * 12], cnt = 0;
    for (int i = 0; i < size; i++) {                  /* insertion sort */
        int j = cnt++;
        while (j > 0 && (count[order[j-1]] - count[i]) * 16 + (i - order[j-1]) > 0) {
            order[j] = order[j-1]; j--;
        }
        order[j] = i;
    }
    int next = size, head = 0;
    while (cnt - head > 1) {                          /* merge two smallest */
        int a = order[head], b = order[head + 1], node = next++;
        head += 2;
        count[node] = count[a] + count[b];
        h->child[0][node - size] = (int8_t)a;
        h->child[1][node - size] = (int8_t)b;
        int j = cnt++;
        while (j > head && count[order[j-1]] >= count[node]) { order[j] = order[j-1]; j--; }
        order[j] = node;
    }
    h->root = next - 1;
    huff_fill_lut(h, h->root, 0, 0, size);
}

/* one symbol: table lookup for codes up to HUFF_LUT_BITS, then bit by bit */
static int huff_read(Bits *b, const Huff *h, int size) {
    unsigned e = h->lut[bits_peek(b, HUFF_LUT_BITS)];
    if (!(e & 0x8000)) { b->pos += e >> 8; return e & 255; }
    b->pos += HUFF_LUT_BITS;
    int node = e & 0x7fff;
    while (node >= size) node = h->child[bits_get(b, 1)][node - size];
    return node;
}

static void parse_coeff_models(Vp6 *s) {
    Rac *c = &s->c;
    Model *m = &s->model;
    int def_prob[11];
    for (int i = 0; i < 11; i++) def_prob[i] = 0x80;

    for (int pt = 0; pt < 2; pt++)
        for (int n = 0; n < 11; n++) {
            if (rac_prob(c, DCCV_PCT[pt][n])) {
                def_prob[n] = rac_prob7(c);
                m->coeff_dccv[pt][n] = def_prob[n];
            } else if (s->key) {
                m->coeff_dccv[pt][n] = def_prob[n];
            }
        }
    if (rac_bit(c)) {
        for (int pos = 1; pos < 64; pos++)
            if (rac_prob(c, COEFF_REORDER_PCT[pos])) m->coeff_reorder[pos] = rac_bits(c, 4);
        coeff_order_init(s);
    }
    for (int cg = 0; cg < 2; cg++)
        for (int n = 0; n < 14; n++)
            if (rac_prob(c, RUNV_PCT[cg][n])) m->coeff_runv[cg][n] = rac_prob7(c);
    for (int ct = 0; ct < 3; ct++)
        for (int pt = 0; pt < 2; pt++)
            for (int cg = 0; cg < 6; cg++)
                for (int n = 0; n < 11; n++) {
                    if (rac_prob(c, RACT_PCT[ct][pt][cg][n])) {
                        def_prob[n] = rac_prob7(c);
                        m->coeff_ract[pt][ct][cg][n] = def_prob[n];
                    } else if (s->key) {
                        m->coeff_ract[pt][ct][cg][n] = def_prob[n];
                    }
                }
    if (s->use_huffman) {
        for (int pt = 0; pt < 2; pt++) {
            build_huff(&s->dccv_h[pt], m->coeff_dccv[pt], HUFF_COEFF_MAP, 12);
            build_huff(&s->runv_h[pt], m->coeff_runv[pt], HUFF_RUN_MAP, 9);
            for (int ct = 0; ct < 3; ct++)
                for (int cg = 0; cg < 4; cg++)
                    build_huff(&s->ract_h[pt][ct][cg], m->coeff_ract[pt][ct][cg], HUFF_COEFF_MAP, 12);
        }
        memset(s->nb_null, 0, sizeof s->nb_null);
        return;
    }
    for (int pt = 0; pt < 2; pt++)
        for (int ctx = 0; ctx < 3; ctx++)
            for (int n = 0; n < 5; n++) {
                int v = ((m->coeff_dccv[pt][n] * DCCV_LC[ctx][n][0] + 128) >> 8) + DCCV_LC[ctx][n][1];
                m->coeff_dcct[pt][ctx][n] = v < 1 ? 1 : v > 255 ? 255 : v;
            }
}

/* ======================================================================
 * macroblock modes and motion vectors
 * ====================================================================== */

/* Collects up to two distinct non-zero vectors of nearby macroblocks that
 * use reference `ref`. Returns the MB type context 0..2 (0 = none found,
 * 1 = one, 2 = two). cand_pos keeps its old value when nothing is found. */
static int find_mv_candidates(Vp6 *s, int row, int col, int ref) {
    int n = 0;
    Mv v[2] = { {0,0}, {0,0} };
    for (int pos = 0; pos < 12; pos++) {
        int x = col + MV_CANDIDATE_POS[pos][0], y = row + MV_CANDIDATE_POS[pos][1];
        if (x < 0 || x >= s->mb_w || y < 0 || y >= s->mb_h) continue;
        const MbInfo *mb = &s->mbs[y * s->mb_w + x];
        if (MB_REF[mb->type] != ref) continue;
        if ((mb->mv.x == v[0].x && mb->mv.y == v[0].y) || (mb->mv.x == 0 && mb->mv.y == 0))
            continue;
        v[n++] = mb->mv;
        if (n > 1) { n = -1; break; }
        s->cand_pos = pos;
    }
    s->cand[0] = v[0];
    s->cand[1] = v[1];
    return n + 1;
}

/* vector = (nearest candidate, if close enough) + coded delta */
static void parse_mv_delta(Vp6 *s, Mv *out) {
    static const uint8_t LONG_ORDER[7] = { 0, 1, 2, 7, 6, 5, 4 };
    Rac *c = &s->c;
    Model *m = &s->model;
    Mv v = { 0, 0 };
    if (s->cand_pos < 2) v = s->cand[0];
    for (int comp = 0; comp < 2; comp++) {
        int delta = 0;
        if (rac_prob(c, m->vector_dct[comp])) {           /* long form: 8 bits */
            for (int i = 0; i < 7; i++) {
                int j = LONG_ORDER[i];
                delta |= rac_prob(c, m->vector_fdv[comp][j]) << j;
            }
            if (delta & 0xF0) delta |= rac_prob(c, m->vector_fdv[comp][3]) << 3;
            else              delta |= 8;
        } else {
            delta = rac_tree(c, TREE_PVA, m->vector_pdv[comp]);
        }
        if (delta && rac_prob(c, m->vector_sig[comp])) delta = -delta;
        if (comp) v.y += delta; else v.x += delta;
    }
    *out = v;
}

static int rshift_round(int a, int b) {   /* round to nearest, ties away from 0 */
    return a > 0 ? (a + (1 << b >> 1)) >> b : (a + (1 << b >> 1) - 1) >> b;
}

static void decode_4mv(Vp6 *s, int row, int col) {
    int type[4], sx = 0, sy = 0;
    for (int b = 0; b < 4; b++) {
        type[b] = rac_bits(&s->c, 2);
        if (type[b]) type[b]++;           /* NOVEC, DELTA, V1, V2 (all PF) */
    }
    for (int b = 0; b < 4; b++) {
        switch (type[b]) {
        case MB_NOVEC_PF: s->mv[b].x = s->mv[b].y = 0; break;
        case MB_DELTA_PF: parse_mv_delta(s, &s->mv[b]); break;
        case MB_V1_PF:    s->mv[b] = s->cand[0]; break;
        case MB_V2_PF:    s->mv[b] = s->cand[1]; break;
        }
        sx += s->mv[b].x; sy += s->mv[b].y;
    }
    s->mbs[row * s->mb_w + col].mv = s->mv[3];
    s->mv[4].x = s->mv[5].x = rshift_round(sx, 2);
    s->mv[4].y = s->mv[5].y = rshift_round(sy, 2);
}

static int decode_mb_mode(Vp6 *s, int row, int col) {
    Mv vec = { 0, 0 };
    const Mv *mv = &vec;
    MbInfo *mb = &s->mbs[row * s->mb_w + col];
    int ctx = find_mv_candidates(s, row, col, REF_PREV);
    const uint8_t *probs = s->model.mb_type[ctx][s->mb_type];
    if (!rac_prob(&s->c, probs[0]))
        s->mb_type = rac_tree(&s->c, TREE_MBTYPE, probs);
    mb->type = s->mb_type;

    switch (s->mb_type) {
    case MB_V1_PF: mv = &s->cand[0]; break;
    case MB_V2_PF: mv = &s->cand[1]; break;
    case MB_V1_GF: find_mv_candidates(s, row, col, REF_GOLD); mv = &s->cand[0]; break;
    case MB_V2_GF: find_mv_candidates(s, row, col, REF_GOLD); mv = &s->cand[1]; break;
    case MB_DELTA_PF: parse_mv_delta(s, &vec); break;
    case MB_DELTA_GF: find_mv_candidates(s, row, col, REF_GOLD); parse_mv_delta(s, &vec); break;
    case MB_4V: decode_4mv(s, row, col); return s->mb_type;
    default: break;
    }
    mb->mv = *mv;
    for (int b = 0; b < 6; b++) s->mv[b] = *mv;
    return s->mb_type;
}

/* ======================================================================
 * coefficients
 * ====================================================================== */

static int parse_coeffs_rac(Vp6 *s) {
    Rac *c = s->ccp;
    Model *m = &s->model;
    if (rac_exhausted(c)) return -1;
    for (int b = 0; b < 6; b++) {
        int pt = b > 3, ct = 1, run = 1, idx = 0;
        s->has_ac[b] = 0;
        int lb = b < 4 ? b >> 1 : b - 2;
        RefDc *ab = &s->above[s->above_idx[b]];
        int16_t *blk = s->blk[b];
        const uint8_t *m1 = m->coeff_dccv[pt];
        const uint8_t *m2 = m->coeff_dcct[pt][s->left[lb].not_null_dc + ab->not_null_dc];
        for (;;) {
            if ((idx > 1 && ct == 0) || rac_prob(c, m2[0])) {
                int v;                                    /* a coefficient */
                if (rac_prob(c, m2[2])) {
                    if (rac_prob(c, m2[3])) {
                        int cat = rac_tree(c, TREE_PC, m1);
                        v = COEFF_BIAS[cat + 5];
                        for (int i = COEFF_BIT_LENGTH[cat]; i >= 0; i--)
                            v += rac_prob(c, COEFF_PARSE_TABLE[cat][i]) << i;
                    } else if (rac_prob(c, m2[4])) {
                        v = 3 + rac_prob(c, m1[5]);
                    } else {
                        v = 2;
                    }
                    ct = 2;
                } else {
                    ct = 1; v = 1;
                }
                if (rac_bit(c)) v = -v;
                if (idx) v *= s->dequant_ac;
                blk[ZIGZAG[m->index_to_pos[idx]]] = (int16_t)v;
                s->has_ac[b] |= idx != 0;
                run = 1;
            } else {                                      /* zero run or end */
                ct = 0;
                if (idx > 0) {
                    if (!rac_prob(c, m2[1])) break;       /* end of block */
                    const uint8_t *m3 = m->coeff_runv[idx >= 6];
                    run = rac_tree(c, TREE_RUN, m3);
                    if (!run) {
                        run = 9;
                        for (int i = 0; i < 6; i++) run += rac_prob(c, m3[i + 8]) << i;
                    }
                }
            }
            idx += run;
            if (idx >= 64) break;
            m1 = m2 = m->coeff_ract[pt][ct][COEFF_GROUPS[idx]];
        }
        s->left[lb].not_null_dc = ab->not_null_dc = blk[0] != 0;
        s->selector[b] = m->index_to_selector[idx < 63 ? idx : 63];
    }
    return 0;
}

/* huffman mode: count of following blocks with a null DC / first AC */
static unsigned huff_nb_null(Bits *gb) {
    unsigned v = bits_get(gb, 2);
    if (v == 2) v += bits_get(gb, 2);
    else if (v == 3) { int e = bits_get(gb, 1) << 2; v = 6 + e + bits_get(gb, 2 + e); }
    return v;
}

static int parse_coeffs_huff(Vp6 *s) {
    Model *m = &s->model;
    Bits *gb = &s->gb;
    for (int b = 0; b < 6; b++) {
        int pt = b > 3, ct = 0, idx = 0;
        s->has_ac[b] = 0;
        int16_t *blk = s->blk[b];
        const Huff *h = &s->dccv_h[pt];
        for (;;) {
            int run = 1;
            if (idx < 2 && s->nb_null[idx][pt]) {
                s->nb_null[idx][pt]--;
                if (idx) break;
            } else {
                if (gb->pos >= gb->len) return -1;
                int tok = huff_read(gb, h, 12);
                if (tok < 0) return -1;
                if (tok == 0) {                           /* zero run */
                    if (idx) {
                        int r = huff_read(gb, &s->runv_h[idx >= 6], 9);
                        if (r < 0) return -1;
                        run += r;
                        if (run >= 9) run += bits_get(gb, 6);
                    } else {
                        s->nb_null[0][pt] = huff_nb_null(gb);
                    }
                    ct = 0;
                } else if (tok == 11) {                   /* end of block */
                    if (idx == 1) s->nb_null[1][pt] = huff_nb_null(gb);
                    break;
                } else {
                    int v = COEFF_BIAS[tok];
                    if (tok > 4) v += bits_get(gb, tok <= 9 ? tok - 4 : 11);
                    ct = 1 + (v > 1);
                    if (bits_get(gb, 1)) v = -v;
                    if (idx) v *= s->dequant_ac;
                    blk[ZIGZAG[m->index_to_pos[idx]]] = (int16_t)v;
                    s->has_ac[b] |= idx != 0;
                }
            }
            idx += run;
            if (idx >= 64) break;
            int cg = COEFF_GROUPS[idx] < 3 ? COEFF_GROUPS[idx] : 3;
            h = &s->ract_h[pt][ct][cg];
        }
        s->selector[b] = m->index_to_selector[idx < 63 ? idx : 63];
    }
    return 0;
}

/* DC prediction from the left / above blocks with the same reference */
static void add_dc_predictors(Vp6 *s, int ref) {
    static const uint8_t B2P[6] = { 0, 0, 0, 0, 1, 2 };
    for (int b = 0; b < 6; b++) {
        RefDc *ab = &s->above[s->above_idx[b]];
        RefDc *lb = &s->left[b < 4 ? b >> 1 : b - 2];
        int n = 0, dc = 0;
        if (lb->ref == ref) { dc += lb->dc; n++; }
        if (ab->ref == ref) { dc += ab->dc; n++; }
        if (n == 0) dc = s->prev_dc[B2P[b]][ref];
        else if (n == 2) dc /= 2;
        int16_t v = (int16_t)(s->blk[b][0] + dc);
        s->prev_dc[B2P[b]][ref] = v;
        ab->dc = lb->dc = v;
        ab->ref = lb->ref = (int8_t)ref;
        s->blk[b][0] = (int16_t)(v * s->dequant_dc);
    }
}

/* ======================================================================
 * inverse DCT (VP3 integer transform, bit-exact)
 * ====================================================================== */

#define C1S7 64277
#define C2S6 60547
#define C3S5 54491
#define C4S4 46341
#define C5S3 36410
#define C6S2 25080
#define C7S1 12785
#define MUL(a, b) ((int)((unsigned)(a) * (unsigned)(b)) >> 16)

static inline uint8_t clip8(int v) { return (unsigned)v > 255 ? (uint8_t)(~v >> 31) : (uint8_t)v; }

/* first pass: each row of 8 horizontal frequencies -> 8 pixels (in place);
 * `rows` = number of leading rows that may be non-zero */
static void idct_rows(int16_t *c, int rows) {
    for (int r = 0; r < rows; r++, c += 8) {
        if (!(c[0] | c[1] | c[2] | c[3] | c[4] | c[5] | c[6] | c[7])) continue;
        int A = MUL(C1S7, c[1]) + MUL(C7S1, c[7]);
        int B = MUL(C7S1, c[1]) - MUL(C1S7, c[7]);
        int C = MUL(C3S5, c[3]) + MUL(C5S3, c[5]);
        int D = MUL(C3S5, c[5]) - MUL(C5S3, c[3]);
        int Ad = MUL(C4S4, A - C), Bd = MUL(C4S4, B - D);
        int Cd = A + C, Dd = B + D;
        int E = MUL(C4S4, c[0] + c[4]), F = MUL(C4S4, c[0] - c[4]);
        int G = MUL(C2S6, c[2]) + MUL(C6S2, c[6]);
        int H = MUL(C6S2, c[2]) - MUL(C2S6, c[6]);
        int Ed = E - G, Gd = E + G, Add = F + Ad, Bdd = Bd - H, Fd = F - Ad, Hd = Bd + H;
        c[0] = (int16_t)(Gd + Cd);  c[7] = (int16_t)(Gd - Cd);
        c[1] = (int16_t)(Add + Hd); c[2] = (int16_t)(Add - Hd);
        c[3] = (int16_t)(Ed + Dd);  c[4] = (int16_t)(Ed - Dd);
        c[5] = (int16_t)(Fd + Bdd); c[6] = (int16_t)(Fd - Bdd);
    }
}

/* second pass over columns; the 8 columns are independent, written as a
 * loop over x so the compiler can vectorize it. out[y*8+x] = residual. */
static void idct_cols(const int16_t *c, int out[64], int bias) {
    for (int x = 0; x < 8; x++) {
        int c0 = c[x], c1 = c[8+x], c2 = c[16+x], c3 = c[24+x];
        int c4 = c[32+x], c5 = c[40+x], c6 = c[48+x], c7 = c[56+x];
        int A = MUL(C1S7, c1) + MUL(C7S1, c7);
        int B = MUL(C7S1, c1) - MUL(C1S7, c7);
        int C = MUL(C3S5, c3) + MUL(C5S3, c5);
        int D = MUL(C3S5, c5) - MUL(C5S3, c3);
        int Ad = MUL(C4S4, A - C), Bd = MUL(C4S4, B - D);
        int Cd = A + C, Dd = B + D;
        int E = MUL(C4S4, c0 + c4) + 8 + bias, F = MUL(C4S4, c0 - c4) + 8 + bias;
        int G = MUL(C2S6, c2) + MUL(C6S2, c6);
        int H = MUL(C6S2, c2) - MUL(C2S6, c6);
        int Ed = E - G, Gd = E + G, Add = F + Ad, Bdd = Bd - H, Fd = F - Ad, Hd = Bd + H;
        out[0*8+x] = (Gd + Cd) >> 4;  out[7*8+x] = (Gd - Cd) >> 4;
        out[1*8+x] = (Add + Hd) >> 4; out[2*8+x] = (Add - Hd) >> 4;
        out[3*8+x] = (Ed + Dd) >> 4;  out[4*8+x] = (Ed - Dd) >> 4;
        out[5*8+x] = (Fd + Bdd) >> 4; out[6*8+x] = (Fd - Bdd) >> 4;
    }
}

/* selector <= 10: only the top-left 4x4 coefficients can be non-zero */
static inline int idct_row_count(int selector) { return selector > 10 ? 8 : 4; }

/* Block with only a DC coefficient: the full transform gives the same
 * value for all 64 pixels (pass 1 keeps an int16 like idct_rows). */
static inline int idct_dc_value(int dc, int bias) {
    int r = (int16_t)MUL(C4S4, dc);
    return (MUL(C4S4, r) + 8 + bias) >> 4;
}

static void idct_put(uint8_t *dst, int stride, int16_t *c, int selector, int has_ac) {
    if (!has_ac) {
        uint8_t v = clip8(idct_dc_value(c[0], 16 * 128));
        c[0] = 0;
        for (int y = 0; y < 8; y++, dst += stride) memset(dst, v, 8);
        return;
    }
    int r[64];
    idct_rows(c, idct_row_count(selector));
    idct_cols(c, r, 16 * 128);
    for (int y = 0; y < 8; y++, dst += stride)
        for (int x = 0; x < 8; x++) dst[x] = clip8(r[y*8+x]);
    memset(c, 0, 64 * sizeof *c);
}

static void idct_add(uint8_t *dst, int stride, int16_t *c, int selector, int has_ac) {
    if (selector <= 1 || !has_ac) {
        /* selector <= 1 is the codec's own DC shortcut (never chosen by
         * VP6.0 streams); otherwise the exact DC-only transform */
        int dc = selector <= 1 ? (c[0] + 15) >> 5 : c[0] ? idct_dc_value(c[0], 0) : 0;
        c[0] = 0;
        if (selector <= 1) memset(c, 0, 64 * sizeof *c);
        if (!dc) return;
        for (int y = 0; y < 8; y++, dst += stride)
            for (int x = 0; x < 8; x++) dst[x] = clip8(dst[x] + dc);
        return;
    }
    int r[64];
    idct_rows(c, idct_row_count(selector));
    idct_cols(c, r, 0);
    for (int y = 0; y < 8; y++, dst += stride)
        for (int x = 0; x < 8; x++) dst[x] = clip8(dst[x] + r[y*8+x]);
    memset(c, 0, 64 * sizeof *c);
}

/* ======================================================================
 * motion compensation (bilinear, 1/4 pel luma, 1/8 pel chroma)
 * ====================================================================== */

static void copy8(uint8_t *dst, int ds, const uint8_t *src, int ss) {
    for (int y = 0; y < 8; y++, dst += ds, src += ss) memcpy(dst, src, 8);
}

/* 1D bilinear between src and src+step, f in eighths */
static void bilin8(uint8_t *dst, int ds, const uint8_t *src, int ss, int step, int f) {
    int a = 8 - f;
    for (int y = 0; y < 8; y++, dst += ds, src += ss)
        for (int x = 0; x < 8; x++)
            dst[x] = (uint8_t)((a * src[x] + f * src[x + step] + 4) >> 3);
}

/* 2D bilinear: horizontal pass over 9 rows, then vertical (two roundings) */
static void bilin8_2d(uint8_t *dst, int ds, const uint8_t *src, int ss, int fx, int fy) {
    uint8_t t[9 * 8];
    int ax = 8 - fx, ay = 8 - fy;
    for (int y = 0; y < 9; y++, src += ss)
        for (int x = 0; x < 8; x++)
            t[y*8+x] = (uint8_t)((ax * src[x] + fx * src[x + 1] + 4) >> 3);
    for (int y = 0; y < 8; y++, dst += ds)
        for (int x = 0; x < 8; x++)
            dst[x] = (uint8_t)((ay * t[y*8+x] + fy * t[y*8+8+x] + 4) >> 3);
}

/* Predicts the 8x8 block at coded (bx, by) of one plane from `ref`.
 * shift = log2(vector units per pixel): 2 luma, 3 chroma. Positions
 * outside the plane repeat the edge pixels. */
static void mc_block(uint8_t *dst, const uint8_t *ref, int cs, int pw, int ph,
                     int bx, int by, Mv mv, int shift) {
    int mask = (1 << shift) - 1;
    int fx = mv.x & mask, fy = mv.y & mask;
    int px = bx + (mv.x >> shift), py = by + (mv.y >> shift);
    if (shift == 2) { fx <<= 1; fy <<= 1; }
    const uint8_t *src = ref + py * cs + px;
    int ss = cs;
    uint8_t emu[9 * 9];
    if (px < 0 || py < 0 || px + 9 > pw || py + 9 > ph) {
        for (int y = 0; y < 9; y++) {
            int sy = py + y < 0 ? 0 : py + y >= ph ? ph - 1 : py + y;
            const uint8_t *row = ref + sy * cs;
            for (int x = 0; x < 9; x++) {
                int sx = px + x < 0 ? 0 : px + x >= pw ? pw - 1 : px + x;
                emu[y*9+x] = row[sx];
            }
        }
        src = emu; ss = 9;
    }
    if (!fx && !fy)  copy8(dst, cs, src, ss);
    else if (!fy)    bilin8(dst, cs, src, ss, 1, fx);
    else if (!fx)    bilin8(dst, cs, src, ss, ss, fy);
    else             bilin8_2d(dst, cs, src, ss, fx, fy);
}

/* ======================================================================
 * frame decode
 * ====================================================================== */

static void render_mb(Vp6 *s, int row, int col, int type) {
    int ref = MB_REF[type];
    Frame *cur = &s->fr[s->cur];
    Frame *rf = ref == REF_PREV ? &s->fr[s->prev] : ref == REF_GOLD ? &s->fr[s->gold] : cur;
    add_dc_predictors(s, ref);

    for (int b = 0; b < 6; b++) {
        int pl = b < 4 ? 0 : b - 3;
        int cs = pl ? -s->uvstride : -s->ystride;
        int bx, by, pw, ph, shift;
        if (pl == 0) {
            bx = col * 16 + (b & 1) * 8; by = row * 16 + (b >> 1) * 8;
            pw = s->cw; ph = s->ch; shift = 2;
        } else {
            bx = col * 8; by = row * 8;
            pw = s->cw >> 1; ph = s->ch >> 1; shift = 3;
        }
        uint8_t *dst = cur->corg[pl] + by * cs + bx;
        if (type == MB_INTRA) {
            idct_put(dst, cs, s->blk[b], s->selector[b], s->has_ac[b]);
            continue;
        }
        if (type == MB_NOVEC_PF || type == MB_NOVEC_GF)
            copy8(dst, cs, rf->corg[pl] + by * cs + bx, cs);
        else
            mc_block(dst, rf->corg[pl], cs, pw, ph, bx, by, s->mv[b], shift);
        idct_add(dst, cs, s->blk[b], s->selector[b], s->has_ac[b]);
    }
}

static int decode_mbs(Vp6 *s) {
    int (*parse)(Vp6 *) = s->use_huffman ? parse_coeffs_huff : parse_coeffs_rac;

    memset(s->prev_dc, 0, sizeof s->prev_dc);
    s->prev_dc[1][REF_CUR] = 128;
    s->prev_dc[2][REF_CUR] = 128;
    int nab = 4 * s->mb_w + 6;
    for (int i = 0; i < nab; i++) { s->above[i].ref = REF_NONE; s->above[i].dc = 0; s->above[i].not_null_dc = 0; }
    s->above[2 * s->mb_w + 2].ref = REF_CUR;
    s->above[3 * s->mb_w + 4].ref = REF_CUR;

    for (int row = 0; row < s->mb_h; row++) {
        for (int i = 0; i < 4; i++) { s->left[i].ref = REF_NONE; s->left[i].dc = 0; s->left[i].not_null_dc = 0; }
        /* above context: luma blocks 0/2 and 1/3 share a column slot */
        s->above_idx[0] = 1; s->above_idx[1] = 2;
        s->above_idx[2] = 1; s->above_idx[3] = 2;
        s->above_idx[4] = 2 * s->mb_w + 3;
        s->above_idx[5] = 3 * s->mb_w + 5;
        for (int col = 0; col < s->mb_w; col++) {
            int type = s->key ? MB_INTRA : decode_mb_mode(s, row, col);
            if (parse(s) < 0) return -1;
            render_mb(s, row, col, type);
            for (int i = 0; i < 4; i++) s->above_idx[i] += 2;
            s->above_idx[4]++; s->above_idx[5]++;
        }
    }
    return 0;
}

/* Decodes one MV0K/MV0F payload into a free frame buffer. On success the
 * new frame becomes "previous" (and golden when flagged). */
static int vp6_decode(Vp6 *s, const uint8_t *buf, int size) {
    if (size < 3 || parse_header(s, buf, size) < 0) return -1;
    int cur = 0;
    while (cur == s->prev || cur == s->gold) cur++;
    s->cur = cur;

    if (s->key) {
        default_models(s);
        for (int i = 0; i < s->mb_w * s->mb_h; i++) s->mbs[i].type = MB_INTRA;
    } else {
        parse_mb_type_models(s);
        parse_vector_models(s);
        s->mb_type = MB_NOVEC_PF;
    }
    parse_coeff_models(s);
    if (decode_mbs(s) < 0) {
        memset(s->blk, 0, sizeof s->blk);
        return -1;
    }
    if (s->key) s->have_key = 1;
    if (s->key || s->golden_flag) s->gold = cur;
    s->prev = cur;
    return 0;
}

static int vp6_init(Vp6 *s, int w, int h) {
    memset(s, 0, sizeof *s);
    s->mb_w = (w + 15) >> 4; s->mb_h = (h + 15) >> 4;
    s->cw = s->mb_w * 16; s->ch = s->mb_h * 16;
    s->ystride = s->cw; s->uvstride = s->cw >> 1;
    s->cur = s->prev = s->gold = -1;
    size_t ysz = (size_t)s->cw * s->ch, uvsz = ysz / 4;
    for (int i = 0; i < 3; i++) {
        Frame *f = &s->fr[i];
        f->mem = (uint8_t *)malloc(ysz + 2 * uvsz);
        if (!f->mem) return -1;
        memset(f->mem, 0, ysz);
        memset(f->mem + ysz, 128, 2 * uvsz);
        f->corg[0] = f->mem + (size_t)(s->ch - 1) * s->ystride;
        f->corg[1] = f->mem + ysz + (size_t)((s->ch >> 1) - 1) * s->uvstride;
        f->corg[2] = f->mem + ysz + uvsz + (size_t)((s->ch >> 1) - 1) * s->uvstride;
    }
    s->mbs = (MbInfo *)calloc((size_t)s->mb_w * s->mb_h, sizeof *s->mbs);
    s->above = (RefDc *)calloc((size_t)4 * s->mb_w + 6, sizeof *s->above);
    return s->mbs && s->above ? 0 : -1;
}

static void vp6_free(Vp6 *s) {
    for (int i = 0; i < 3; i++) free(s->fr[i].mem);
    free(s->mbs);
    free(s->above);
}

/* ======================================================================
 * EA-XA R3 audio: the nibble ADPCM of eaxa_decode() in audio.c, but
 * stateful across blocks, per-channel streams, and WITHOUT the +0x80
 * rounding: the 0xEE-frame ("v2") flavour of EA-XA truncates (matches
 * FFmpeg adpcm_ea_r3 and vgmstream decode_ea_xa_v2 sample for sample).
 * ====================================================================== */

static const int XA_C1[4] = { 0, 240, 460, 392 };
static const int XA_C2[4] = { 0, 0, -208, -220 };

static int s16be(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

/* Decodes nsamp samples of one channel; out is strided (interleaved).
 * cur/prev = predictor history. Returns bytes consumed or -1. */
static int xa_r3_channel(const uint8_t *d, int avail, int16_t *out, int ostride,
                         int nsamp, int *cur, int *prev) {
    int pos = 0, n = 0, c1 = *cur, c2 = *prev;
    while (n < nsamp) {
        if (pos >= avail) return -1;
        int hdr = d[pos];
        if (hdr == 0xEE) {                          /* raw frame + reseed */
            if (pos + 61 > avail) return -1;
            c1 = s16be(d + pos + 1); c2 = s16be(d + pos + 3);
            for (int k = 0; k < 28 && n < nsamp; k++, n++)
                out[n * ostride] = (int16_t)s16be(d + pos + 5 + 2 * k);
            pos += 61;
        } else {
            if (pos + 15 > avail) return -1;
            int ci = hdr >> 4 & 3, shift = 20 - (hdr & 15);
            for (int k = 0; k < 28; k++) {
                int byte = d[pos + 1 + (k >> 1)];
                int nib = (k & 1) ? byte & 15 : byte >> 4;
                if (nib > 7) nib -= 16;
                int smp = ((nib * (1 << shift)) + c1 * XA_C1[ci] + c2 * XA_C2[ci]) >> 8;
                if (smp > 32767) smp = 32767; else if (smp < -32768) smp = -32768;
                c2 = c1; c1 = smp;
                if (n < nsamp) out[n++ * ostride] = (int16_t)smp;
            }
            pos += 15;
        }
    }
    *cur = c1; *prev = c2;
    return pos;
}

/* ======================================================================
 * container + public API
 * ====================================================================== */

#define CHUNK_PAD 64
#define RING_FRAMES 131072                /* ~4 s at 32 kHz: tutorials end with
                                             ~3.3 s of audio after the last frame */
#define AUDIO_BLOCK_MAX 8192              /* samples per SCDl block */

struct Movie {
    FILE *f;
    char *fbuf;             /* its stdio buffer (ours: newlib's own does not survive the Vita malloc wrap) */
    int w, h, nframes;
    unsigned rate_num, rate_den;
    int arate, achannels;
    uint8_t *chunk;
    int chunk_cap;

    Vp6 v;
    int frame_index, errors, eof;

    int16_t *ring;                         /* interleaved PCM */
    unsigned ring_w, ring_r;               /* frame counters (SPSC) */
    int16_t *ablock;                       /* one decoded SCDl block */
    int xa_cur[6], xa_prev[6];
};

static void ring_push(Movie *m, const int16_t *pcm, int frames) {
    unsigned w = m->ring_w, r = __atomic_load_n(&m->ring_r, __ATOMIC_ACQUIRE);
    int ch = m->achannels;
    unsigned room = RING_FRAMES - (w - r);
    if ((unsigned)frames > room) frames = (int)room;   /* consumer not pulling: drop the excess */
    for (int i = 0; i < frames; i++, w++)
        memcpy(m->ring + (size_t)(w % RING_FRAMES) * ch, pcm + (size_t)i * ch, ch * sizeof *pcm);
    __atomic_store_n(&m->ring_w, w, __ATOMIC_RELEASE);
}

static void decode_audio_block(Movie *m, const uint8_t *p, int size) {
    int ch = m->achannels;
    if (ch <= 0 || size < 4 + 4 * ch) return;
    int nsamp = (int)rd32be(p);
    if (nsamp <= 0) return;
    if (nsamp > AUDIO_BLOCK_MAX) { m->errors++; return; }
    for (int c = 0; c < ch; c++) {
        int off = (int)rd32be(p + 4 + 4 * c) + 4 * (ch + 1);
        if (off < 0 || off >= size ||
            xa_r3_channel(p + off, size - off, m->ablock + c, ch, nsamp,
                          &m->xa_cur[c], &m->xa_prev[c]) < 0) {
            m->errors++;
            return;
        }
    }
    ring_push(m, m->ablock, nsamp);
}

/* SCHl: 'GSTR' (14-byte prefix) or 'PT\0\0' then EA tag/len/value list */
static void parse_audio_header(Movie *m, const uint8_t *p, int size) {
    int i = 0, version = 0, codec = -1;
    if (size >= 4 && !memcmp(p, "GSTR", 4)) i = 14;
    else if (size >= 4 && !memcmp(p, "PT\0\0", 4)) i = 4;
    else return;
    m->achannels = 1; m->arate = 22050;
    while (i < size) {
        int tag = p[i++];
        if (tag == 0xFF) break;
        if (tag == 0xFC || tag == 0xFD || tag == 0xFE) continue;   /* section markers */
        if (i >= size) break;
        int len = p[i++];
        unsigned val = 0;
        for (int k = 0; k < len && i < size; k++) val = val << 8 | p[i++];
        switch (tag) {
        case 0x80: version = (int)val; break;
        case 0x82: m->achannels = (int)val; break;
        case 0x83: codec = (int)val; break;
        case 0x84: m->arate = (int)val; break;
        }
    }
    /* Only EA-XA R3 (version 3, default codec) is implemented; it is all
     * the game's movies use. Anything else plays silent. */
    if (version != 3 || (codec != -1 && codec != 2) || m->achannels < 1 || m->achannels > 6)
        m->achannels = 0, m->arate = 0;
}

/* Reads the next chunk header + payload into m->chunk. 0 at end of file. */
static int read_chunk(Movie *m, char tag[4], int *size) {
    uint8_t h[8];
    if (fread(h, 1, 8, m->f) != 8) return 0;
    uint32_t sz = rd32le(h + 4);
    if (sz < 8) return 0;
    sz -= 8;
    memcpy(tag, h, 4);
    if (sz > (uint32_t)m->chunk_cap) {          /* bigger than any frame: skip */
        m->errors++;
        fseek(m->f, (long)sz, SEEK_CUR);
        *size = -1;
        return 1;
    }
    if (fread(m->chunk, 1, sz, m->f) != sz) return 0;
    memset(m->chunk + sz, 0, CHUNK_PAD);
    *size = (int)sz;
    return 1;
}

Movie *movie_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *fbuf = (char *)malloc(256 * 1024);   /* the chunks are small: few big card reads, not one per chunk */
    if (fbuf) setvbuf(f, fbuf, _IOFBF, 256 * 1024);   /* before any read */
    uint8_t h[40];
    if (fread(h, 1, 40, f) != 40 || memcmp(h, "MVhd", 4) || rd32le(h + 4) < 32) { fclose(f); free(fbuf); return NULL; }
    Movie *m = (Movie *)calloc(1, sizeof *m);
    if (!m) { fclose(f); free(fbuf); return NULL; }
    m->f = f;
    m->fbuf = fbuf;
    m->w = h[12] | h[13] << 8;
    m->h = h[14] | h[15] << 8;
    m->nframes = (int)rd32le(h + 16);
    uint32_t largest = rd32le(h + 20);
    m->rate_num = rd32le(h + 24);
    m->rate_den = rd32le(h + 28);
    fseek(f, (long)rd32le(h + 4), SEEK_SET);
    if (m->w <= 0 || m->h <= 0 || m->w > 2048 || m->h > 2048 || vp6_init(&m->v, m->w, m->h) < 0) {
        movie_close(m);
        return NULL;
    }
    m->chunk_cap = (int)(largest > 65536 ? largest : 65536) + 1024;
    m->chunk = (uint8_t *)malloc((size_t)m->chunk_cap + CHUNK_PAD);
    m->ablock = (int16_t *)malloc((size_t)AUDIO_BLOCK_MAX * 6 * sizeof *m->ablock);
    if (!m->chunk || !m->ablock) { movie_close(m); return NULL; }

    /* audio header normally directly follows MVhd */
    long at = ftell(f);
    char tag[4]; int size;
    if (read_chunk(m, tag, &size) && size > 0 && !memcmp(tag, "SCHl", 4))
        parse_audio_header(m, m->chunk, size);
    else
        fseek(f, at, SEEK_SET);
    if (m->achannels) {
        m->ring = (int16_t *)malloc((size_t)RING_FRAMES * m->achannels * sizeof *m->ring);
        if (!m->ring) { movie_close(m); return NULL; }
    }
    return m;
}

int movie_info(Movie *m, int *w, int *h, float *fps, int *frames, int *rate, int *channels) {
    if (w) *w = m->w;
    if (h) *h = m->h;
    if (fps) *fps = m->rate_den ? (float)m->rate_num / (float)m->rate_den : 25.0f;
    if (frames) *frames = m->nframes;
    if (rate) *rate = m->arate;
    if (channels) *channels = m->achannels;
    return 1;
}

int movie_next_frame(Movie *m, const unsigned char **y, const unsigned char **u,
                     const unsigned char **v, int *ystride, int *uvstride) {
    char tag[4]; int size;
    while (!m->eof) {
        if (!read_chunk(m, tag, &size)) { m->eof = 1; break; }
        if (size < 0) continue;
        if (!memcmp(tag, "SCDl", 4)) { decode_audio_block(m, m->chunk, size); continue; }
        if (memcmp(tag, "MV0K", 4) && memcmp(tag, "MV0F", 4)) continue;
        if (vp6_decode(&m->v, m->chunk, size) < 0) m->errors++;
        if (m->v.prev < 0) continue;                  /* nothing to show yet */
        Frame *fr = &m->v.fr[m->v.prev];
        size_t ysz = (size_t)m->v.cw * m->v.ch;
        /* coded height may exceed the display height: crop at the top of
         * memory (= end of the coded picture) */
        size_t ycrop = (size_t)(m->v.ch - m->h) * m->v.ystride;
        size_t uvcrop = (size_t)((m->v.ch - m->h) >> 1) * m->v.uvstride;
        if (y) *y = fr->mem + ycrop;
        if (u) *u = fr->mem + ysz + uvcrop;
        if (v) *v = fr->mem + ysz + ysz / 4 + uvcrop;
        if (ystride) *ystride = m->v.ystride;
        if (uvstride) *uvstride = m->v.uvstride;
        m->frame_index++;
        return 1;
    }
    return 0;
}

int movie_audio(Movie *m, int16_t *out, int max_frames) {
    if (!m->achannels) return 0;
    unsigned r = m->ring_r, w = __atomic_load_n(&m->ring_w, __ATOMIC_ACQUIRE);
    int n = (int)(w - r), ch = m->achannels;
    if (n > max_frames) n = max_frames;
    for (int i = 0; i < n; i++, r++)
        memcpy(out + (size_t)i * ch, m->ring + (size_t)(r % RING_FRAMES) * ch, ch * sizeof *out);
    __atomic_store_n(&m->ring_r, r, __ATOMIC_RELEASE);
    return n;
}

int movie_audio_buffered(Movie *m) {
    return (int)(__atomic_load_n(&m->ring_w, __ATOMIC_ACQUIRE) - __atomic_load_n(&m->ring_r, __ATOMIC_ACQUIRE));
}

int movie_frame_index(Movie *m) { return m->frame_index; }
int movie_errors(Movie *m) { return m->errors; }

void movie_close(Movie *m) {
    if (!m) return;
    if (m->f) fclose(m->f);
    free(m->fbuf);
    vp6_free(&m->v);
    free(m->chunk);
    free(m->ring);
    free(m->ablock);
    free(m);
}
