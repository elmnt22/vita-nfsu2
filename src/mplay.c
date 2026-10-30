/* mplay.c - a movie on screen (see mplay.h); moved out of shop.c's
   IG_PlayMovie so the post-race movies play the same way.

   The decoding runs on a worker thread (the spare cores on the Vita): it
   reads and decodes the VP6 frames, converts them to RGBA and queues up to
   MV_Q of them ahead; its EA-XA sound goes into movie.c's ring on the way.
   The main thread only takes the frame that is due and uploads it, so a
   slow frame never holds up the game loop or the mixer's feed. */
#include "mplay.h"
#include "movie.h"
#include "speech.h"
#include "audio.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MV_AUD 8192
#define MV_Q 4            /* RGBA frames decoded ahead */
struct MPlay {
    Movie *m;
    GLuint tex;
    int w, h, rate, ch, shown, vdone, newpic;
    float fps, t;
    /* the decoder thread's queue: slots q[n % MV_Q]; it writes qw, the main thread qr */
    unsigned char *q[MV_Q];
    volatile int qw, qr, eof, stop;
    SDL_Thread *th;
    /* worker timing, read for the log line */
    volatile unsigned dec_us, cvt_us, nstat, waits;
    /* the mixer thread's side: s16 frames waiting, the resampling position */
    int16_t buf[MV_AUD * 2];
    int have;
    float pos;
};

/* audio_set_movie's pull, on the mixer thread: linear resampling from the
   movie's rate (32 kHz EA-XA) to the mixer's */
static int mv_pull(void *u, float *lr, int n, int rate) {
    MPlay *v = (MPlay *)u;
    if (!v || !v->m || v->rate <= 0 || rate <= 0) return 0;
    float step = (float)v->rate / (float)rate;
    int need = (int)(v->pos + (float)n * step) + 2;
    if (need > MV_AUD) need = MV_AUD;
    if (v->have < need) {
        int got = movie_audio(v->m, v->buf + v->have * v->ch, need - v->have);
        if (got > 0) v->have += got;
    }
    int i;
    for (i = 0; i < n; i++) {
        float p = v->pos + (float)i * step;
        int i0 = (int)p;
        if (i0 + 1 >= v->have) break;
        float f = p - (float)i0;
        for (int c = 0; c < 2; c++) {
            int sc = v->ch == 1 ? 0 : c;
            float a = v->buf[i0 * v->ch + sc], b = v->buf[(i0 + 1) * v->ch + sc];
            lr[2 * i + c] = (a + (b - a) * f) * (1.0f / 32768.0f);
        }
    }
    float np = v->pos + (float)i * step;
    int k = (int)np;
    if (k > v->have) k = v->have;
    if (k > 0) memmove(v->buf, v->buf + k * v->ch, sizeof(int16_t) * (size_t)((v->have - k) * v->ch));
    v->have -= k;
    v->pos = np - (float)k;
    return i;
}

#ifdef __ARM_NEON
#include <arm_neon.h>
/* 16 pixels of mv_convert at once (the same integer maths, so the same
   bytes): the scalar loop took ~24 ms a frame on the Vita */
static inline uint8x8_t mv_pack(int32x4_t a, int32x4_t b) {
    return vqmovn_u16(vcombine_u16(vqmovun_s32(vshrq_n_s32(a, 8)), vqmovun_s32(vshrq_n_s32(b, 8))));
}
static void mv_row16(unsigned char *o, const unsigned char *yr, const unsigned char *ur, const unsigned char *vr) {
    int16x8_t U = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(ur))), vdupq_n_s16(128));
    int16x8_t V = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(vr))), vdupq_n_s16(128));
    int32x4_t k128 = vdupq_n_s32(128), c[3][2];
    for (int h = 0; h < 2; h++) {   /* the 8 chroma samples, 4 at a time */
        int16x4_t u4 = h ? vget_high_s16(U) : vget_low_s16(U), v4 = h ? vget_high_s16(V) : vget_low_s16(V);
        c[0][h] = vmlal_n_s16(k128, v4, 409);
        c[1][h] = vmlal_n_s16(vmlal_n_s16(k128, u4, -100), v4, -208);
        c[2][h] = vmlal_n_s16(k128, u4, 516);
    }
    uint8x16_t y8 = vld1q_u8(yr);
    int16x8_t Ys[2] = { vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(y8))), vdupq_n_s16(16)),
                        vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(y8))), vdupq_n_s16(16)) };
    for (int h = 0; h < 2; h++) {   /* pixels 8h .. 8h + 7 use chroma 4h .. 4h + 3 */
        int32x4_t y0 = vmull_n_s16(vget_low_s16(Ys[h]), 298), y1 = vmull_n_s16(vget_high_s16(Ys[h]), 298);
        uint8x8x4_t px;
        for (int ch = 0; ch < 3; ch++) {
            int32x4x2_t d = vzipq_s32(c[ch][h], c[ch][h]);   /* each chroma sample on two pixels */
            px.val[ch] = mv_pack(vaddq_s32(y0, d.val[0]), vaddq_s32(y1, d.val[1]));
        }
        px.val[3] = vdup_n_u8(255);
        vst4_u8(o + h * 32, px);
    }
}
#endif

/* BT.601 studio range, as the movie test; two rows share a chroma row */
static void mv_convert(unsigned char *rgba, int w, int h, const unsigned char *y, const unsigned char *u,
                       const unsigned char *w2, int ys, int uvs) {
    for (int j = 0; j < h; j++) {
        unsigned char *o = rgba + (size_t)j * (size_t)w * 4;
        const unsigned char *yr = y + j * ys, *ur = u + (j / 2) * uvs, *vr = w2 + (j / 2) * uvs;
        int i = 0;
#ifdef __ARM_NEON
        for (; i + 16 <= w; i += 16, o += 64) mv_row16(o, yr + i, ur + i / 2, vr + i / 2);
#endif
        for (; i < w; i += 2) {
            int U = ur[i / 2] - 128, V = vr[i / 2] - 128;
            int cr = 409 * V + 128, cg = -100 * U - 208 * V + 128, cb = 516 * U + 128;
            for (int k = 0; k < 2; k++) {
                int Y = (yr[i + k] - 16) * 298;
                int r = (Y + cr) >> 8, g = (Y + cg) >> 8, b = (Y + cb) >> 8;
                o[0] = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
                o[1] = (unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g);
                o[2] = (unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b);
                o[3] = 255;
                o += 4;
            }
        }
    }
}

static int mv_thread(void *u) {
    MPlay *v = (MPlay *)u;
    while (!v->stop) {
        if (v->qw - v->qr >= MV_Q) { SDL_Delay(2); continue; }   /* the queue is full: the picture is ahead */
        const unsigned char *y = NULL, *pu = NULL, *pv = NULL;
        int ys = 0, uvs = 0;
        Uint64 t0 = SDL_GetPerformanceCounter();
        if (!movie_next_frame(v->m, &y, &pu, &pv, &ys, &uvs)) break;
        Uint64 t1 = SDL_GetPerformanceCounter();
        unsigned char *o = v->q[v->qw % MV_Q];
        if (y && pu && pv) mv_convert(o, v->w, v->h, y, pu, pv, ys, uvs);
        else if (v->qw > 0) memcpy(o, v->q[(v->qw - 1) % MV_Q], (size_t)v->w * (size_t)v->h * 4);
        Uint64 t2 = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
        v->dec_us += (unsigned)((t1 - t0) * 1000000u / f);
        v->cvt_us += (unsigned)((t2 - t1) * 1000000u / f);
        v->nstat++;
        __sync_synchronize();
        v->qw = v->qw + 1;
    }
    __sync_synchronize();
    v->eof = 1;
    return 0;
}

MPlay *mplay_open(const char *path) {
    MPlay *v = (MPlay *)calloc(1, sizeof *v);
    int ok = 0;
    if (v) v->m = movie_open(path);
    if (v && v->m) {
        movie_info(v->m, &v->w, &v->h, &v->fps, NULL, &v->rate, &v->ch);
        if (v->ch < 1 || v->ch > 2) v->rate = 0;
        if (v->fps <= 0.0f) v->fps = 30.0f;
        ok = v->w > 0 && v->h > 0;
        for (int i = 0; i < MV_Q && ok; i++)
            ok = (v->q[i] = (unsigned char *)calloc((size_t)v->w * (size_t)v->h, 4)) != NULL;
    }
    if (!ok) {
        if (v && v->m) movie_close(v->m);
        for (int i = 0; v && i < MV_Q; i++) free(v->q[i]);
        free(v);
        return NULL;
    }
    glGenTextures(1, &v->tex);
    glBindTexture(GL_TEXTURE_2D, v->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, v->w, v->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, v->q[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    v->th = SDL_CreateThread(mv_thread, "movie", v);
    if (!v->th) printf("movie: no decoder thread, decoding on the frame\n");
    speech_stop();                                  /* a movie purges the speech */
    if (v->rate > 0) audio_set_movie(mv_pull, v);
    printf("movie: %s %dx%d %.2f fps, audio %d Hz x%d\n", path, v->w, v->h, v->fps, v->rate, v->ch);
    return v;
}

int mplay_update(MPlay *v, float dt) {
    if (!v) return 1;
    if (!v->th && !v->eof && v->qw - v->qr < 1) {   /* no thread: one frame here (the old way) */
        const unsigned char *y = NULL, *u = NULL, *w = NULL;
        int ys = 0, uvs = 0;
        if (movie_next_frame(v->m, &y, &u, &w, &ys, &uvs)) {
            if (y && u && w) mv_convert(v->q[v->qw % MV_Q], v->w, v->h, y, u, w, ys, uvs);
            v->qw++;
        } else v->eof = 1;
    }
    if (!v->vdone) {
        if (dt > 0.1f) dt = 0.1f;   /* a hitch: the picture slips instead of racing */
        int want = (int)(v->t * v->fps) + 1;
        if (v->shown < want && v->qw == v->qr && !v->eof) v->waits++;   /* the decoder is late: hold the clock */
        else v->t += dt;
        want = (int)(v->t * v->fps) + 1;
        const unsigned char *pic = NULL;
        while (v->shown < want && v->qr < v->qw) {
            pic = v->q[v->qr % MV_Q];
            v->shown++;
            if (v->shown < want && v->qr + 1 < v->qw) { __sync_synchronize(); v->qr = v->qr + 1; pic = NULL; continue; }
            break;
        }
        if (pic) {
            glBindTexture(GL_TEXTURE_2D, v->tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, v->w, v->h, GL_RGBA, GL_UNSIGNED_BYTE, pic);
            __sync_synchronize();
            v->qr = v->qr + 1;   /* the slot is free once uploaded */
        }
        if (v->eof && v->qr == v->qw) v->vdone = 1;
        if (v->nstat >= 250) {
            unsigned n = v->nstat;
            printf("movie: frame %d, worker %.1f ms decode + %.1f ms convert per frame, %u late frames\n",
                   v->shown, (float)v->dec_us / (float)n * 0.001f, (float)v->cvt_us / (float)n * 0.001f, v->waits);
            v->dec_us = v->cvt_us = 0; v->nstat = 0; v->waits = 0;
        }
    }
    /* over when the pictures ran out and the sound played out */
    return v->vdone && (v->rate <= 0 || (movie_audio_buffered(v->m) == 0 && v->have < 2));
}

GLuint mplay_tex(const MPlay *v, int *w, int *h) {
    if (!v) return 0;
    if (w) *w = v->w;
    if (h) *h = v->h;
    return v->tex;
}

void mplay_close(MPlay *v) {
    if (!v) return;
    audio_set_movie(NULL, NULL);
    v->stop = 1;
    if (v->th) SDL_WaitThread(v->th, NULL);
    if (v->tex) glDeleteTextures(1, &v->tex);
    if (v->m) movie_close(v->m);
    for (int i = 0; i < MV_Q; i++) free(v->q[i]);
    free(v);
}
