/* t_movie.c - decodes an EA .vp6 movie with src/movie.c.
 *
 *   t_movie <file.vp6> <outdir> [frame ...]   decode all, time it, write the
 *        listed frames (default 0 100 1000 3000 last) as PPM and the first
 *        20 s of audio as WAV into outdir
 *   t_movie <file.vp6> --raw                  write every frame as raw
 *        yuv420p to stdout (for the bit-exact comparison in run_movie.sh)
 *   t_movie <file.vp6> --pcm                  write all audio as s16le to stdout */
#include "movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
static double now_ms(void) {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
static double now_ms(void) { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }
#endif

static int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* BT.601 limited range, only for looking at the frames */
static void write_ppm(const char *path, int w, int h, const unsigned char *y, const unsigned char *u,
                      const unsigned char *v, int ys, int uvs) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    unsigned char *row = malloc((size_t)w * 3);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int Y = (y[j * ys + i] - 16) * 298, U = u[(j/2) * uvs + i/2] - 128, V = v[(j/2) * uvs + i/2] - 128;
            row[i*3+0] = (unsigned char)clampi((Y + 409 * V + 128) >> 8);
            row[i*3+1] = (unsigned char)clampi((Y - 100 * U - 208 * V + 128) >> 8);
            row[i*3+2] = (unsigned char)clampi((Y + 516 * U + 128) >> 8);
        }
        fwrite(row, 1, (size_t)w * 3, f);
    }
    free(row);
    fclose(f);
}

static void put32(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: t_movie file.vp6 outdir|--raw|--pcm [frames]\n"); return 2; }
    Movie *m = movie_open(argv[1]);
    if (!m) { fprintf(stderr, "movie_open failed: %s\n", argv[1]); return 1; }
    int w, h, nf, rate, ch; float fps;
    movie_info(m, &w, &h, &fps, &nf, &rate, &ch);
    const unsigned char *y, *u, *v; int ys, uvs;

    if (!strcmp(argv[2], "--raw") || !strcmp(argv[2], "--pcm")) {
        int raw = !strcmp(argv[2], "--raw");
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        static int16_t pcm[65536 * 2];
        while (movie_next_frame(m, &y, &u, &v, &ys, &uvs)) {
            if (raw) {
                for (int j = 0; j < h; j++) fwrite(y + j * ys, 1, w, stdout);
                for (int j = 0; j < h / 2; j++) fwrite(u + j * uvs, 1, w / 2, stdout);
                for (int j = 0; j < h / 2; j++) fwrite(v + j * uvs, 1, w / 2, stdout);
            } else if (ch) {
                int n = movie_audio(m, pcm, 65536);
                fwrite(pcm, sizeof(int16_t) * ch, n, stdout);
            }
        }
        for (int n; !raw && ch && (n = movie_audio(m, pcm, 65536)) > 0; )   /* audio tail */
            fwrite(pcm, sizeof(int16_t) * ch, n, stdout);
        fprintf(stderr, "%d frames, %d errors\n", movie_frame_index(m), movie_errors(m));
        movie_close(m);
        return 0;
    }

    const char *outdir = argv[2];
    const char *base = strrchr(argv[1], '/');
    base = base ? base + 1 : argv[1];
    char stem[256]; snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;

    int want[64], nwant = 0;
    for (int i = 3; i < argc && nwant < 63; i++) want[nwant++] = atoi(argv[i]);
    if (!nwant) { want[0] = 0; want[1] = 100; want[2] = 1000; want[3] = 3000; nwant = 4; }
    want[nwant++] = nf - 1;   /* last frame */

    printf("%s: %dx%d %.3f fps, %d frames, audio %d Hz x %d\n", base, w, h, fps, nf, rate, ch);

    /* 20 s of audio into a WAV */
    char path[512];
    snprintf(path, sizeof path, "%s/%s_audio.wav", outdir, stem);
    FILE *wav = ch ? fopen(path, "wb") : NULL;
    int wav_frames = 0, wav_max = rate * 20;
    if (wav) { fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav); put32(wav, 16); put16(wav, 1); put16(wav, ch);
               put32(wav, rate); put32(wav, rate * ch * 2); put16(wav, ch * 2); put16(wav, 16);
               fwrite("data\0\0\0\0", 1, 8, wav); }
    static int16_t pcm[65536 * 2];
    long audio_total = 0;

    double total = 0, worst = 0; int worst_at = 0, n = 0;
    for (;;) {
        double t0 = now_ms();
        int got = movie_next_frame(m, &y, &u, &v, &ys, &uvs);
        double dt = now_ms() - t0;
        if (!got) break;
        total += dt; if (dt > worst) { worst = dt; worst_at = n; }
        for (int i = 0; i < nwant; i++)
            if (want[i] == n) {
                snprintf(path, sizeof path, "%s/%s_%05d.ppm", outdir, stem, n);
                write_ppm(path, w, h, y, u, v, ys, uvs);
            }
        int a = movie_audio(m, pcm, 65536);
        audio_total += a;
        if (wav && wav_frames < wav_max) {
            int k = a < wav_max - wav_frames ? a : wav_max - wav_frames;
            fwrite(pcm, sizeof(int16_t) * ch, k, wav); wav_frames += k;
        }
        n++;
    }
    for (int a; (a = movie_audio(m, pcm, 65536)) > 0; ) audio_total += a;   /* audio tail */
    if (wav) {
        fseek(wav, 4, SEEK_SET); put32(wav, 36 + wav_frames * ch * 2);
        fseek(wav, 40, SEEK_SET); put32(wav, wav_frames * ch * 2);
        fclose(wav);
    }
    printf("decoded %d/%d frames, %d errors, audio %ld frames (%.2f s)\n",
           n, nf, movie_errors(m), audio_total, rate ? (double)audio_total / rate : 0.0);
    printf("decode time: avg %.3f ms/frame, worst %.3f ms (frame %d), total %.1f ms\n",
           n ? total / n : 0.0, worst, worst_at, total);
    int errors = movie_errors(m);
    movie_close(m);
    return errors ? 1 : 0;
}
