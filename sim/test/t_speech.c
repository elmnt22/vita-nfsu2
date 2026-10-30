/* t_speech.c — standalone check of src/speech.c (not part of the game build).
 * Build (from OpenUG2/):
 *   python -m ziglang cc -target x86-windows-gnu -O2 -std=gnu99 -DSPEECH_STANDALONE \
 *     -Isrc -IE:/re-tools/simdeps/SDL2-2.30.9/i686-w64-mingw32/include/SDL2 \
 *     sim/test/t_speech.c src/speech.c -o sim/build/t_speech.exe
 * Run: t_speech <dataroot> <outdir> [seed]
 *   Plays a fixed command sequence through speech_cmd/speech_update/speech_pull
 *   and writes, per pick, <outdir>/c_<n>_b<bank>_<sub>_s<sample>.raw (s16 mono
 *   22050) and prints "pick n bank sub sample samples" lines. Compare with
 *   E:/re-tools/out/speech/t_speech_ref.py (spch.py picker + utk.py decoder). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "speech.h"
#include "career.h"

extern uint32_t speech_test_now_ms;
static int (*g_pull)(void *, int16_t *, int);
static void *g_u;
void audio_set_speech(int (*pull)(void *u, int16_t *mono, int n), void *u, int rate) { (void)rate; g_pull = pull; g_u = u; }
float audio_speech_gain(void) { return 1.0f; }

static const struct { int kind; uint32_t a, b; } k_seq[] = {
    { CSPK_CAREER, 0x8000, 0x20 },        /* Stage0Intro */
    { CSPK_CAREER, 0x800, 0x20 },         /* Stage0SMS */
    { CSPK_CAREER, 2, 0x20 },             /* Stage0Minimap */
    { CSPK_CAREER, 0x10000000, 0x20 },    /* Stage0Carlot */
    { CSPK_OUTRUN, 1, 0x200 },
    { CSPK_OUTRUN, 1, 0x200 },
    { CSPK_OUTRUN, 1, 4 },
    { CSPK_OUTRUN, 8, 0x200 },
    { CSPK_FES, 8, 0x20 },
    { CSPK_IE, 0x10, 8 },
    { CSPK_IA, 1, 0x20 },
    { CSPK_TIMED, 1, 0 },
    { CSPK_MAG, 0x10, 1 },
    { CSPK_DVD, 1, 1 },
    { CSPK_FESDYNO, 1, 0x10 },
    { CSPK_OUTRUN, 1, 0x200 },
};

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: t_speech dataroot outdir [seed]\n"); return 2; }
    if (!speech_init(argv[1])) { fprintf(stderr, "speech_init failed\n"); return 1; }
    speech_seed(argc > 3 ? atoi(argv[3]) : 1);
    if (!g_pull) { fprintf(stderr, "no pull registered\n"); return 1; }
    int fails = 0;
    static int16_t buf[1 << 22];
    for (unsigned n = 0; n < sizeof k_seq / sizeof k_seq[0]; n++) {
        speech_test_now_ms += 16;
        speech_cmd(k_seq[n].kind, k_seq[n].a, k_seq[n].b);
        if (speech_done() != 0) { printf("FAIL: done right after a command\n"); fails++; }
        speech_update();
        int bank, sub, smp;
        speech_last_pick(&bank, &sub, &smp);
        int total = 0;
        while (!speech_done() && total < (1 << 22) - 1024) {
            int r = g_pull(g_u, buf + total, 1024);
            total += r;
            if (r < 1024) break;
        }
        speech_test_now_ms += 16;
        speech_update();
        if (!speech_done()) { printf("FAIL: not done after the clip\n"); fails++; }
        printf("pick %u %d %d %d %d\n", n, bank, sub, smp, total);
        char p[600];
        snprintf(p, sizeof p, "%s/c_%02u_b%d_%d_s%03d.raw", argv[2], n, bank, sub, smp);
        FILE *f = fopen(p, "wb");
        if (f) { fwrite(buf, 2, (size_t)total, f); fclose(f); }
        speech_last_pick(NULL, NULL, NULL);
    }
    /* purge mid-clip: done again at once */
    speech_test_now_ms += 16;
    speech_cmd(CSPK_CAREER, 0x8000, 0x20);
    speech_update();
    g_pull(g_u, buf, 512);
    speech_cmd(CSPK_PURGE, 0, 0);
    if (!speech_done()) { printf("FAIL: purge\n"); fails++; }
    /* pause: no samples while paused */
    speech_cmd(CSPK_CAREER, 0x8000, 0x20);
    speech_update();
    speech_pause(1);
    if (g_pull(g_u, buf, 512) != 0) { printf("FAIL: pause\n"); fails++; }
    speech_pause(0);
    if (g_pull(g_u, buf, 512) != 512) { printf("FAIL: resume\n"); fails++; }
    speech_stop();
    /* NIS: first clip of each bank */
    for (int b = 0; b < 4; b++) {
        int rate = 0, ch = speech_nis_open(b, 0, &rate), total = 0, r;
        while (ch && (r = speech_nis_pull(buf + total * ch, 4096)) > 0 && (total + 4096) * ch < (1 << 22)) total += r;
        printf("nis %d ch %d rate %d frames %d\n", b, ch, rate, total);
        char p[600];
        snprintf(p, sizeof p, "%s/nis_%d.raw", argv[2], b);
        FILE *f = fopen(p, "wb");
        if (f) { fwrite(buf, 2, (size_t)total * (ch ? ch : 1), f); fclose(f); }
        speech_nis_close();
    }
    printf(fails ? "t_speech: %d FAIL\n" : "t_speech: state checks ok\n", fails);
    return fails != 0;
}
