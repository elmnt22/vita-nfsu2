/* zlib.h — OpenUG2 PS Vita stand-in for the three zlib calls render.c's dev
 * PNG writer uses. compress2() emits a valid zlib stream of stored (level 0)
 * deflate blocks, so screenshots still decode everywhere; there is no real
 * compression and no inflate. Only render.c includes this header.
 */
#ifndef OPENUG2_VITA_ZLIB_H
#define OPENUG2_VITA_ZLIB_H

#include <stddef.h>

typedef unsigned char Bytef;
typedef unsigned long uLong;
typedef unsigned long uLongf;
#define Z_OK 0
#define Z_BUF_ERROR (-5)

static inline uLong crc32(uLong crc, const Bytef *buf, unsigned len) {
    static uLong table[256];
    static int ready = 0;
    if (!ready) {
        for (uLong n = 0; n < 256; n++) {
            uLong c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = 1;
    }
    if (!buf) return 0;
    crc = crc ^ 0xFFFFFFFFUL;
    while (len--) crc = table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    return (crc ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL;
}

static inline uLong compressBound(uLong n) {
    return n + (n / 65535 + 1) * 5 + 6;   /* header + per-block headers + adler */
}

static inline int compress2(Bytef *dst, uLongf *dst_len, const Bytef *src, uLong src_len, int level) {
    (void)level;
    if (*dst_len < compressBound(src_len)) return Z_BUF_ERROR;
    Bytef *o = dst;
    *o++ = 0x78; *o++ = 0x01;
    uLong a = 1, b = 0, left = src_len;
    const Bytef *p = src;
    do {
        unsigned blk = left > 65535 ? 65535 : (unsigned)left;
        *o++ = (Bytef)(left == blk);                    /* BFINAL, BTYPE=00 */
        *o++ = (Bytef)blk; *o++ = (Bytef)(blk >> 8);
        *o++ = (Bytef)~blk; *o++ = (Bytef)(~blk >> 8);
        for (unsigned i = 0; i < blk; i++) {
            o[i] = p[i];
            a = (a + p[i]) % 65521; b = (b + a) % 65521;
        }
        o += blk; p += blk; left -= blk;
    } while (left);
    uLong ad = (b << 16) | a;
    *o++ = (Bytef)(ad >> 24); *o++ = (Bytef)(ad >> 16); *o++ = (Bytef)(ad >> 8); *o++ = (Bytef)ad;
    *dst_len = (uLongf)(o - dst);
    return Z_OK;
}

#endif
