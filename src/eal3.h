/* EALayer3 (EA's MPEG Layer III framing, SCHl codec2 0x17, "v1a") -- the
   music streams in SDATA/sdat.viv pfdata/MusicSFx.mus.
   An EA-frame holds one granule of a Layer III frame: a pcm flag byte
   (0x00, or 0xEE with a PCM block), then a cut-down header (version,
   rate, channel mode, mode extension = the MPEG header's 8 bits), the
   granule index and that granule's side info, its main data, byte padding
   and an optional big-endian PCM block. Two granules make one MPEG-1
   frame; the decoder here rebuilds that standard frame (no bit reservoir:
   EALayer3 doesn't use one) and decodes it with minimp3. Layout as
   documented by vgmstream (mpeg_custom_utils_ealayer3.c, after Zench's
   ealayer3 RE). The start of a stream drops 576 + 529 decoded samples
   (one granule + the synthesis delay) plus the samples a PCM block
   stands in for; the PCM block itself is output first. */
#ifndef EAL3_H
#define EAL3_H
#include <stdint.h>

typedef struct Eal3 Eal3;

Eal3 *eal3_new(void);
void  eal3_free(Eal3 *d);
/* start of a new stream (SCHl): resets the decoder and the start discard */
void  eal3_reset(Eal3 *d);
/* one SCDl block's payload (from block + 0x14 to its end): decoded
   interleaved stereo samples appended to out (room for max frames);
   returns the frames written, -1 on a parse error */
int   eal3_block(Eal3 *d, const unsigned char *p, int len, int16_t *out, int max);
/* the stream's channels / rate seen so far (0 before the first frame) */
int   eal3_rate(const Eal3 *d);
int   eal3_channels(const Eal3 *d);

#endif
